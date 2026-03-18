/**
 * kcode-wasm.js — Kcode 웹 IDE WASM 로더
 * version : v16.5.0
 *
 * 실행 우선순위:
 *   1순위: kserver 로컬 실행 중 → POST /compile (LLVM IR, C코드 생성 등 고급 기능)
 *   2순위: kserver 없음          → kcode_vm.wasm 직접 실행 (기본 실행, 렉싱, 파싱)
 *
 * 사용법:
 *   <script src="/wasm/kcode_vm.js"></script>
 *   <script src="/wasm/kcode-wasm.js"></script>
 *   <script>
 *     const engine = new KcodeEngine();
 *     await engine.init();
 *     const result = await engine.run('출력("안녕, 세계!")');
 *     console.log(result.output);  // "안녕, 세계!\n"
 *   </script>
 */

class KcodeEngine {

    constructor(options = {}) {
        this.wasmReady  = false;
        this.module     = null;
        this.wasmPath   = options.wasmPath   || '/wasm/';
        this.kserverUrl = options.kserverUrl || 'http://localhost:8080';
        this.kserverAvailable = false;
        this._initPromise = null;
    }

    /* ================================================================
     *  초기화 — WASM 로드 + kserver 가용성 확인
     * ================================================================ */
    async init() {
        if (this._initPromise) return this._initPromise;
        this._initPromise = this._doInit();
        return this._initPromise;
    }

    async _doInit() {
        // 1. WASM 모듈 로드
        try {
            if (typeof KcodeVM === 'undefined') {
                throw new Error('KcodeVM 모듈이 로드되지 않았습니다. kcode_vm.js를 먼저 포함하세요.');
            }
            this.module = await KcodeVM({
                locateFile: (filename) => `${this.wasmPath}${filename}`
            });
            this.wasmReady = true;
            const ver = this.module.ccall('kc_wasm_version', 'string', [], []);
            console.log(`[Kcode WASM] 준비 완료 — 엔진 버전: ${ver}`);
        } catch (e) {
            console.warn('[Kcode WASM] WASM 로드 실패:', e.message);
        }

        // 2. kserver 가용성 확인 (타임아웃 500ms)
        try {
            const ctrl   = new AbortController();
            const timer  = setTimeout(() => ctrl.abort(), 500);
            const res    = await fetch(`${this.kserverUrl}/ping`, { signal: ctrl.signal });
            clearTimeout(timer);
            this.kserverAvailable = res.ok;
        } catch (_) {
            this.kserverAvailable = false;
        }

        if (this.kserverAvailable) {
            console.log('[Kcode] kserver 연결됨 — 고급 기능 사용 가능');
        } else {
            console.log('[Kcode] kserver 없음 — WASM 모드로 실행');
        }
    }

    /* ================================================================
     *  소스코드 실행 (kserver 우선 → WASM 폴백)
     * ================================================================ */
    async run(source) {
        await this.init();

        // 1순위: kserver
        if (this.kserverAvailable) {
            try {
                const res = await fetch(`${this.kserverUrl}/compile`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ action: 'run', source })
                });
                if (res.ok) return await res.json();
            } catch (_) {
                // kserver 연결 끊김 → WASM 폴백
                this.kserverAvailable = false;
                console.warn('[Kcode] kserver 연결 끊김 — WASM 모드로 전환');
            }
        }

        // 2순위: WASM
        return this._wasmRun(source);
    }

    /* ================================================================
     *  WASM 직접 실행
     * ================================================================ */
    _wasmRun(source) {
        if (!this.wasmReady || !this.module) {
            return { success: false, output: '', error: 'WASM 모듈이 로드되지 않았습니다' };
        }
        try {
            const json = this.module.ccall('kc_wasm_run', 'string', ['string'], [source]);
            return JSON.parse(json);
        } catch (e) {
            return { success: false, output: '', error: e.message };
        }
    }

    /* ================================================================
     *  소스코드 컴파일 (바이트코드 생성)
     * ================================================================ */
    async compile(source) {
        await this.init();
        if (!this.wasmReady || !this.module) {
            return { success: false, bytecode_b64: '', errors: ['WASM 미로드'] };
        }
        try {
            const json = this.module.ccall('kc_wasm_compile', 'string', ['string'], [source]);
            return JSON.parse(json);
        } catch (e) {
            return { success: false, bytecode_b64: '', errors: [e.message] };
        }
    }

    /* ================================================================
     *  바이트코드 실행
     * ================================================================ */
    async execBytecode(bytecodeB64) {
        await this.init();
        if (!this.wasmReady || !this.module) {
            return { success: false, output: '', error: 'WASM 미로드' };
        }
        try {
            const json = this.module.ccall('kc_wasm_exec_bc', 'string', ['string'], [bytecodeB64]);
            return JSON.parse(json);
        } catch (e) {
            return { success: false, output: '', error: e.message };
        }
    }

    /* ================================================================
     *  렉싱 — 토큰 목록 반환 (IDE 하이라이팅용)
     * ================================================================ */
    async lex(source) {
        await this.init();
        if (!this.wasmReady || !this.module) return { tokens: [] };
        try {
            const json = this.module.ccall('kc_wasm_lex', 'string', ['string'], [source]);
            return JSON.parse(json);
        } catch (e) {
            return { tokens: [] };
        }
    }

    /* ================================================================
     *  파싱 — AST 구문 분석 (오류 위치 반환)
     * ================================================================ */
    async parse(source) {
        await this.init();
        if (!this.wasmReady || !this.module) {
            return { success: false, error_count: 0, errors: [] };
        }
        try {
            const json = this.module.ccall('kc_wasm_parse', 'string', ['string'], [source]);
            return JSON.parse(json);
        } catch (e) {
            return { success: false, error_count: 1, errors: [e.message] };
        }
    }

    /* ================================================================
     *  엔진 상태 정보
     * ================================================================ */
    status() {
        return {
            wasmReady:        this.wasmReady,
            kserverAvailable: this.kserverAvailable,
            mode:             this.kserverAvailable ? 'kserver' : (this.wasmReady ? 'wasm' : '오프라인')
        };
    }

    /* ================================================================
     *  현재 실행 모드 문자열
     * ================================================================ */
    get mode() {
        if (this.kserverAvailable) return 'kserver';
        if (this.wasmReady)        return 'wasm';
        return '오프라인';
    }
}

/* ================================================================
 *  전역 싱글톤 (선택적)
 *  html에서 직접 사용 시: await kcodeEngine.run(source)
 * ================================================================ */
if (typeof window !== 'undefined') {
    window.KcodeEngine = KcodeEngine;

    // 자동 초기화 (data-kcode-auto-init 속성이 있을 때)
    document.addEventListener('DOMContentLoaded', () => {
        if (document.querySelector('[data-kcode-auto-init]')) {
            window.kcodeEngine = new KcodeEngine();
            window.kcodeEngine.init().then(() => {
                console.log(`[Kcode] 엔진 준비 — 모드: ${window.kcodeEngine.mode}`);
            });
        }
    });
}

/* ================================================================
 *  웹 IDE 실행 버튼 핸들러 예제
 *
 *  <button onclick="runCode()">실행</button>
 *  <textarea id="source">출력("안녕, 세계!")</textarea>
 *  <pre id="output"></pre>
 *
 *  async function runCode() {
 *      const engine = new KcodeEngine();
 *      const source = document.getElementById('source').value;
 *      const result = await engine.run(source);
 *      document.getElementById('output').textContent =
 *          result.success ? result.output : '오류: ' + result.error;
 *  }
 * ================================================================ */

/* Node.js / ES Module 환경 지원 */
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { KcodeEngine };
}
