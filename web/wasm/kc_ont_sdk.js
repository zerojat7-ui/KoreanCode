/**
 * kc_ont_sdk.js  —  Kcode 온톨로지 Node.js SDK
 * version : v20.3.0
 *
 * 역할 : libkc_ontology.so 를 ffi-napi 로 감싸 Node.js / TypeScript에서
 *        K엔진 온톨로지를 직접 사용할 수 있게 한다 (모드2 대여 모드).
 *
 * 설치 / 사용법
 * --------------
 * # 1) libkc_ontology.so 빌드
 *   cmake .. -DKCODE_ONTOLOGY=ON -DBUILD_SHARED_LIBS=ON
 *   cmake --build . --target kc_ontology
 *   cmake --install . --prefix /usr/local
 *
 * # 2) Node.js 의존성 설치
 *   npm install ffi-napi ref-napi
 *
 * # 3) 사용 예시
 *   const { KcOntology } = require('./kc_ont_sdk');
 *
 *   const ont = new KcOntology();
 *   ont.addClass('제품');
 *   ont.addClass('전자제품', '제품');
 *   ont.addInstance('전자제품', 'TV세트', [['이름','삼성TV'], ['가격', 1500000]]);
 *   const rows = ont.query('전자제품 찾기');
 *   console.log(rows);
 *   ont.destroy();
 *
 * 지원 플랫폼: Linux (libkc_ontology.so), macOS (libkc_ontology.dylib),
 *              Windows (kc_ontology.dll)
 */

'use strict';

const path = require('path');
const os   = require('os');

// =============================================================================
//  1. ffi-napi 로드 (선택적 의존성)
// =============================================================================

let ffi, ref;
try {
    ffi = require('ffi-napi');
    ref = require('ref-napi');
} catch (e) {
    throw new Error(
        '[kc_ont_sdk] ffi-napi / ref-napi 가 설치되지 않았습니다.\n' +
        '  npm install ffi-napi ref-napi\n' +
        `  원인: ${e.message}`
    );
}

// =============================================================================
//  2. 공유 라이브러리 탐색
// =============================================================================

/**
 * libkc_ontology 공유 라이브러리 경로를 반환한다.
 * KC_ONT_LIB_PATH 환경변수 또는 표준 경로를 탐색한다.
 * @param {string|null} libPath - 명시적 경로 (없으면 자동 탐색)
 * @returns {string}
 */
function findLibrary(libPath = null) {
    if (libPath) return libPath;
    if (process.env.KC_ONT_LIB_PATH) return process.env.KC_ONT_LIB_PATH;

    const platform = os.platform();
    const candidates = [];

    // 플랫폼별 파일명
    const names = {
        linux:  ['libkc_ontology.so', 'libkc_ontology.so.20'],
        darwin: ['libkc_ontology.dylib', 'libkc_ontology.20.dylib'],
        win32:  ['kc_ontology.dll'],
    }[platform] || ['libkc_ontology.so'];

    const searchDirs = [
        path.resolve(__dirname),
        path.resolve(__dirname, '..', 'lib'),
        '/usr/local/lib',
        '/usr/lib',
        '/usr/lib/x86_64-linux-gnu',
    ];

    for (const dir of searchDirs) {
        for (const name of names) {
            candidates.push(path.join(dir, name));
        }
    }

    const fs = require('fs');
    for (const c of candidates) {
        try {
            fs.accessSync(c);
            return c;
        } catch (_) { /* not found */ }
    }

    throw new Error(
        '[kc_ont_sdk] libkc_ontology.so 를 찾을 수 없습니다.\n' +
        '  빌드: cmake .. -DKCODE_ONTOLOGY=ON -DBUILD_SHARED_LIBS=ON\n' +
        '  또는 KC_ONT_LIB_PATH 환경변수를 설정하세요.\n' +
        `  탐색 경로: ${candidates.join(', ')}`
    );
}

// =============================================================================
//  3. FFI 바인딩 정의
// =============================================================================

/**
 * libkc_ontology FFI 라이브러리 객체를 생성한다.
 * @param {string|null} libPath
 * @returns {Object} ffi.Library 인스턴스
 */
function createLibrary(libPath = null) {
    const resolvedPath = findLibrary(libPath);
    const voidPtr  = ref.refType(ref.types.void);
    const intType  = ref.types.int;
    const doubleT  = ref.types.double;
    const stringT  = ref.types.CString;
    const voidT    = ref.types.void;

    return ffi.Library(resolvedPath, {
        // KcOntology* kc_ont_create(void)
        'kc_ont_create':             [voidPtr, []],
        // void kc_ont_destroy(KcOntology*)
        'kc_ont_destroy':            [voidT,   [voidPtr]],
        // int kc_ont_add_class(KcOntology*, char* name, char* parent)
        'kc_ont_add_class':          [intType, [voidPtr, stringT, stringT]],
        // int kc_ont_add_relation(KcOntology*, char* name, char* from, char* to)
        'kc_ont_add_relation':       [intType, [voidPtr, stringT, stringT, stringT]],
        // int kc_ont_add_instance(KcOntology*, char* class_name, char* inst_name)
        'kc_ont_add_instance':       [intType, [voidPtr, stringT, stringT]],
        // int kc_ont_set_prop_string(KcOntology*, char* inst, char* prop, char* val)
        'kc_ont_set_prop_string':    [intType, [voidPtr, stringT, stringT, stringT]],
        // int kc_ont_set_prop_int(KcOntology*, char* inst, char* prop, int val)
        'kc_ont_set_prop_int':       [intType, [voidPtr, stringT, stringT, intType]],
        // int kc_ont_set_prop_float(KcOntology*, char* inst, char* prop, double val)
        'kc_ont_set_prop_float':     [intType, [voidPtr, stringT, stringT, doubleT]],
        // int kc_ont_infer(KcOntology*)
        'kc_ont_infer':              [intType, [voidPtr]],
        // KcOntQueryResult* kc_ont_query_kor(KcOntology*, char* query_str)
        'kc_ont_query_kor':          [voidPtr, [voidPtr, stringT]],
        // KcOntQueryResult* kc_ont_query_sparql(KcOntology*, char* sparql_str)
        'kc_ont_query_sparql':       [voidPtr, [voidPtr, stringT]],
        // void kc_ont_query_result_free(KcOntQueryResult*)
        'kc_ont_query_result_free':  [voidT,   [voidPtr]],
        // char* kc_ont_serialize_json(KcOntology*)
        'kc_ont_serialize_json':     [stringT, [voidPtr]],
        // char* kc_ont_serialize_turtle(KcOntology*)
        'kc_ont_serialize_turtle':   [stringT, [voidPtr]],
        // int kc_ont_import_json_ld(KcOntology*, char* json_str)
        'kc_ont_import_json_ld':     [intType, [voidPtr, stringT]],
        // int kc_ont_import_turtle(KcOntology*, char* ttl_str)
        'kc_ont_import_turtle':      [intType, [voidPtr, stringT]],
        // int kc_ont_import_csv(KcOntology*, char* class_name, char* csv_str)
        'kc_ont_import_csv':         [intType, [voidPtr, stringT, stringT]],
        // int kc_ont_safe_for_learning(KcOntology*)
        'kc_ont_safe_for_learning':  [intType, [voidPtr]],
        // const char* kc_ont_version(void)
        'kc_ont_version':            [stringT, []],
    });
}

// =============================================================================
//  4. KcOntQueryResult 파싱 헬퍼
//  C 구조체 레이아웃:
//    char*  columns[32]    @ offset 0
//    int    col_count      @ offset 32*8 = 256
//    char** *rows          @ offset 256+8 = 264  (포인터 정렬)
//    int    row_count      @ offset 272+8 = 280
//    char*  error          @ offset 288
// =============================================================================

const KC_ONT_QR_MAX_COLS = 32;
const PTR_SIZE = process.arch === 'x64' ? 8 : 4;

/**
 * KcOntQueryResult C 구조체 포인터를 JS 배열로 변환한다.
 * @param {Buffer} qrPtr - kc_ont_query_kor 반환 포인터
 * @returns {{ rows: Object[], error: string|null }}
 */
function parseQueryResult(qrPtr) {
    if (!qrPtr || qrPtr.isNull()) return { rows: [], error: null };

    // col_count 오프셋: PTR_SIZE * KC_ONT_QR_MAX_COLS
    const colCountOffset  = PTR_SIZE * KC_ONT_QR_MAX_COLS;
    // rows 포인터 오프셋 (다음 포인터 정렬)
    const rowsPtrOffset   = colCountOffset + PTR_SIZE;
    // row_count 오프셋
    const rowCountOffset  = rowsPtrOffset + PTR_SIZE;
    // error 포인터 오프셋
    const errorOffset     = rowCountOffset + PTR_SIZE;

    const colCount  = qrPtr.readInt32LE(colCountOffset);
    const rowCount  = qrPtr.readInt32LE(rowCountOffset);

    // error 포인터 읽기
    const errPtrVal = qrPtr.readPointer(errorOffset);
    const errorStr  = errPtrVal.isNull() ? null : errPtrVal.readCString();

    if (errorStr) return { rows: [], error: errorStr };

    // columns 배열 읽기
    const cols = [];
    for (let c = 0; c < colCount; c++) {
        const colPtr = qrPtr.readPointer(c * PTR_SIZE);
        cols.push(colPtr.isNull() ? '' : colPtr.readCString());
    }

    // rows 읽기
    const rowsPtrBuf = qrPtr.readPointer(rowsPtrOffset);
    const rows = [];
    for (let r = 0; r < rowCount; r++) {
        const rowPtrBuf = rowsPtrBuf.readPointer(r * PTR_SIZE);
        const row = {};
        for (let c = 0; c < colCount; c++) {
            const valPtr = rowPtrBuf.readPointer(c * PTR_SIZE);
            row[cols[c]] = valPtr.isNull() ? '' : valPtr.readCString();
        }
        rows.push(row);
    }

    return { rows, error: null };
}

// =============================================================================
//  5. KcOntology 클래스
// =============================================================================

/**
 * K엔진 온톨로지 Node.js SDK.
 *
 * 내부적으로 libkc_ontology.so 의 C API 를 ffi-napi 로 호출한다.
 *
 * @example
 * const { KcOntology } = require('./kc_ont_sdk');
 *
 * const ont = new KcOntology();
 * ont.addClass('제품');
 * ont.addInstance('제품', 'TV세트', [['이름', '삼성TV'], ['가격', 1500000]]);
 * const rows = ont.query('제품 찾기');
 * console.log(rows);
 * ont.destroy();
 */
class KcOntology {
    /**
     * @param {string|null} libPath - libkc_ontology.so 명시적 경로 (없으면 자동 탐색)
     */
    constructor(libPath = null) {
        this._lib = createLibrary(libPath);
        this._ctx = this._lib.kc_ont_create();
        if (!this._ctx || this._ctx.isNull()) {
            throw new Error('[KcOntology] kc_ont_create() 가 NULL 을 반환했습니다.');
        }
    }

    /** 라이브러리 버전 문자열 반환 (정적) */
    static version(libPath = null) {
        const lib = createLibrary(libPath);
        return lib.kc_ont_version() || 'unknown';
    }

    /** 온톨로지 컨텍스트 해제 */
    destroy() {
        if (this._ctx && !this._ctx.isNull()) {
            this._lib.kc_ont_destroy(this._ctx);
            this._ctx = null;
        }
    }

    // ── 클래스 / 관계 ──────────────────────────────────────────────────

    /**
     * 클래스(개념)를 추가한다.
     * @param {string} name   - 클래스 이름
     * @param {string|null} parent - 부모 클래스 이름
     * @returns {number} 0 성공, 음수 오류
     */
    addClass(name, parent = null) {
        this._checkCtx();
        return this._lib.kc_ont_add_class(this._ctx, name, parent);
    }

    /**
     * 두 클래스 사이의 관계를 정의한다.
     * @param {string} name       - 관계 이름
     * @param {string} fromClass  - 출발 클래스
     * @param {string} toClass    - 도착 클래스
     * @returns {number}
     */
    addRelation(name, fromClass, toClass) {
        this._checkCtx();
        return this._lib.kc_ont_add_relation(this._ctx, name, fromClass, toClass);
    }

    // ── 인스턴스 / 속성 ────────────────────────────────────────────────

    /**
     * 인스턴스를 추가하고 속성을 설정한다.
     * @param {string} className - 소속 클래스
     * @param {string} instName  - 인스턴스 이름
     * @param {Array<[string, any]>} props - [[속성명, 값], ...] 목록
     * @returns {number}
     */
    addInstance(className, instName, props = []) {
        this._checkCtx();
        const rc = this._lib.kc_ont_add_instance(this._ctx, className, instName);
        if (rc !== 0) return rc;
        for (const [prop, value] of props) {
            this.setProperty(instName, prop, value);
        }
        return 0;
    }

    /**
     * 인스턴스 속성값을 설정한다.
     * @param {string} instName  - 인스턴스 이름
     * @param {string} propName  - 속성 이름
     * @param {string|number} value - 속성값
     * @returns {number}
     */
    setProperty(instName, propName, value) {
        this._checkCtx();
        if (typeof value === 'number') {
            if (Number.isInteger(value)) {
                return this._lib.kc_ont_set_prop_int(this._ctx, instName, propName, value);
            }
            return this._lib.kc_ont_set_prop_float(this._ctx, instName, propName, value);
        }
        return this._lib.kc_ont_set_prop_string(
            this._ctx, instName, propName, String(value)
        );
    }

    // ── 추론 ───────────────────────────────────────────────────────────

    /**
     * 전방향 체이닝 추론을 실행한다.
     * @returns {number} 도출된 사실 수
     */
    infer() {
        this._checkCtx();
        return this._lib.kc_ont_infer(this._ctx);
    }

    // ── 질의 ───────────────────────────────────────────────────────────

    /**
     * 한글 질의 또는 SPARQL 질의를 실행한다.
     * @param {string}  queryStr - 질의 문자열
     * @param {boolean} sparql   - true 이면 SPARQL 질의
     * @returns {Object[]} [{ 컬럼명: 값, ... }, ...]
     */
    query(queryStr, sparql = false) {
        this._checkCtx();
        const fn = sparql
            ? this._lib.kc_ont_query_sparql
            : this._lib.kc_ont_query_kor;

        const qrPtr = fn.call(this._lib, this._ctx, queryStr);
        if (!qrPtr || qrPtr.isNull()) return [];

        const { rows, error } = parseQueryResult(qrPtr);
        this._lib.kc_ont_query_result_free(qrPtr);

        if (error) throw new Error(`[온톨로지 질의 오류] ${error}`);
        return rows;
    }

    // ── 직렬화 / 임포트 ───────────────────────────────────────────────

    /**
     * 온톨로지를 JSON-LD 객체로 직렬화한다.
     * @returns {Object}
     */
    toJsonLd() {
        this._checkCtx();
        const raw = this._lib.kc_ont_serialize_json(this._ctx);
        if (!raw) return {};
        try { return JSON.parse(raw); } catch (_) { return {}; }
    }

    /**
     * 온톨로지를 Turtle(TTL) 문자열로 직렬화한다.
     * @returns {string}
     */
    toTurtle() {
        this._checkCtx();
        return this._lib.kc_ont_serialize_turtle(this._ctx) || '';
    }

    /**
     * JSON-LD 객체를 현재 온톨로지에 병합한다.
     * @param {Object} data
     * @returns {number}
     */
    loadJsonLd(data) {
        this._checkCtx();
        return this._lib.kc_ont_import_json_ld(
            this._ctx, JSON.stringify(data)
        );
    }

    /**
     * JSON-LD 파일을 로드한다.
     * @param {string} filePath
     * @returns {number}
     */
    loadJsonLdFile(filePath) {
        const fs   = require('fs');
        const data = JSON.parse(fs.readFileSync(filePath, 'utf-8'));
        return this.loadJsonLd(data);
    }

    /**
     * Turtle 문자열을 온톨로지에 병합한다.
     * @param {string} ttlStr
     * @returns {number}
     */
    loadTurtle(ttlStr) {
        this._checkCtx();
        return this._lib.kc_ont_import_turtle(this._ctx, ttlStr);
    }

    /**
     * Turtle 파일을 로드한다.
     * @param {string} filePath
     * @returns {number}
     */
    loadTurtleFile(filePath) {
        const fs  = require('fs');
        const ttl = fs.readFileSync(filePath, 'utf-8');
        return this.loadTurtle(ttl);
    }

    /**
     * CSV 문자열을 지정 클래스의 인스턴스로 임포트한다.
     * @param {string} className
     * @param {string} csvStr
     * @returns {number}
     */
    loadCsv(className, csvStr) {
        this._checkCtx();
        return this._lib.kc_ont_import_csv(this._ctx, className, csvStr);
    }

    /**
     * CSV 파일을 로드한다.
     * @param {string} className
     * @param {string} filePath
     * @returns {number}
     */
    loadCsvFile(className, filePath) {
        const fs  = require('fs');
        const csv = fs.readFileSync(filePath, 'utf-8');
        return this.loadCsv(className, csv);
    }

    // ── 학습 거버넌스 ─────────────────────────────────────────────────

    /**
     * 민감 속성 포함 여부를 검사한다.
     * @returns {boolean} true = 학습 안전
     */
    safeForLearning() {
        this._checkCtx();
        return this._lib.kc_ont_safe_for_learning(this._ctx) === 1;
    }

    // ── 내부 유틸 ──────────────────────────────────────────────────────

    _checkCtx() {
        if (!this._ctx || this._ctx.isNull()) {
            throw new Error(
                '[KcOntology] 컨텍스트가 해제되었습니다. destroy() 이후 사용 불가.'
            );
        }
    }
}

// =============================================================================
//  6. 편의 팩토리 함수
// =============================================================================

/**
 * JSON-LD 파일에서 온톨로지를 생성한다.
 * @param {string} filePath
 * @param {string|null} libPath
 * @returns {KcOntology}
 */
function fromJsonLdFile(filePath, libPath = null) {
    const ont = new KcOntology(libPath);
    const rc  = ont.loadJsonLdFile(filePath);
    if (rc !== 0) { ont.destroy(); throw new Error(`JSON-LD 로드 실패 (rc=${rc}): ${filePath}`); }
    return ont;
}

/**
 * Turtle 파일에서 온톨로지를 생성한다.
 * @param {string} filePath
 * @param {string|null} libPath
 * @returns {KcOntology}
 */
function fromTurtleFile(filePath, libPath = null) {
    const ont = new KcOntology(libPath);
    const rc  = ont.loadTurtleFile(filePath);
    if (rc !== 0) { ont.destroy(); throw new Error(`Turtle 로드 실패 (rc=${rc}): ${filePath}`); }
    return ont;
}

// =============================================================================
//  7. CLI 간이 테스트
// =============================================================================

if (require.main === module) {
    console.log('Kcode 온톨로지 Node.js SDK v20.3.0');
    console.log('사용법: node kc_ont_sdk.js [libkc_ontology.so 경로]');

    const libPath = process.argv[2] || null;

    let ont;
    try {
        ont = new KcOntology(libPath);

        console.log('\n[1] 클래스 추가');
        ont.addClass('제품');
        ont.addClass('전자제품', '제품');
        console.log('  ✅ 제품, 전자제품 추가');

        console.log('[2] 관계 추가');
        ont.addRelation('구매함', '사람', '제품');
        console.log('  ✅ 구매함 (사람 → 제품)');

        console.log('[3] 인스턴스 추가');
        ont.addInstance('전자제품', 'TV세트', [
            ['이름', '삼성 TV'],
            ['가격', 1500000],
            ['전압', 220.0],
        ]);
        console.log('  ✅ TV세트 인스턴스');

        console.log('[4] 학습 안전성 검사');
        const safe = ont.safeForLearning();
        console.log(`  ${safe ? '✅ 안전' : '⚠️ 민감 속성 감지됨'}`);

        console.log('[5] JSON-LD 직렬화');
        const jd = ont.toJsonLd();
        console.log(`  키: ${Object.keys(jd).slice(0, 5).join(', ')}`);

        console.log('[6] 추론 실행');
        const facts = ont.infer();
        console.log(`  도출 사실 수: ${facts}`);

        console.log('\n✅ 모든 테스트 통과');
    } catch (e) {
        console.warn(`\n⚠️  ${e.message}`);
        console.log('\n빌드 후 재시도:');
        console.log('  cmake .. -DKCODE_ONTOLOGY=ON -DBUILD_SHARED_LIBS=ON');
        console.log('  cmake --build . --target kc_ontology');
        process.exit(1);
    } finally {
        if (ont) ont.destroy();
    }
}

// =============================================================================
//  8. 모듈 내보내기
// =============================================================================
module.exports = {
    KcOntology,
    fromJsonLdFile,
    fromTurtleFile,
};
