# API & 도구 (API & Tools)

## 1. kserver — 로컬 HTTP 서버

웹 IDE(GitHub Pages 등)와 로컬 Kcode 컴파일러를 연결하는 경량 HTTP 서버입니다.  
외부 라이브러리 없이 POSIX 소켓만 사용합니다.

### 실행

```bash
./kserver                        # 기본 포트 8080
./kserver 3000                   # 포트 지정
./kserver 8080 -llvm             # LLVM 컴파일러 사용
./kserver 8080 -cgen             # C 코드 생성기 사용
```

### 엔드포인트

| 메서드 | 경로 | 설명 |
|--------|------|------|
| `POST` | `/compile` | 소스 컴파일 → JSON 응답 |
| `GET` | `/health` | 서버 상태 확인 |
| `GET` | `/version` | 컴파일러 버전 반환 |
| `OPTIONS` | `*` | CORS preflight 처리 |

### 컴파일 요청 형식

```json
POST /compile
Content-Type: application/json

{
  "action": "compile",
  "source": "출력(\"안녕하세요\")"
}
```

### 컴파일 응답 형식

```json
{
  "success": true,
  "ir_text": "...",
  "sourcemap": [...],
  "errors": [],
  "stats": {
    "parse_ms": 2,
    "codegen_ms": 5
  }
}
```

### CORS
GitHub Pages 등 외부 도메인에서 호출 가능하도록  
`Access-Control-Allow-Origin: *` 헤더가 모든 응답에 포함됩니다.

---

## 2. WASM API — 브라우저 실행

kserver 없이 브라우저에서 직접 Kcode를 실행합니다.

### 노출 함수 5종

| 함수 | 반환 | 설명 |
|------|------|------|
| `kc_wasm_run(source)` | JSON | 소스코드 실행 |
| `kc_wasm_compile(source)` | JSON | 바이트코드 컴파일 |
| `kc_wasm_exec_bc(bytecode_b64)` | JSON | 바이트코드 실행 |
| `kc_wasm_lex(source)` | JSON | 토큰 목록 (IDE 하이라이팅용) |
| `kc_wasm_parse(source)` | JSON | AST 반환 (구문 분석용) |

### 웹에서 사용

```html
<script src="kcode-wasm.js"></script>
<script>
  KcodeWasm.ready.then(kc => {
    const result = kc.run('출력("안녕하세요")');
    console.log(result.output);
    // → 안녕하세요
  });
</script>
```

### 실행 모드 (웹 IDE)

| 모드 | 조건 | 가능 기능 |
|------|------|----------|
| **WASM 모드** | kserver 없음 (기본) | 실행 / 렉싱 / 파싱 / 바이트코드 VM |
| **kserver 모드** | kserver 로컬 실행 중 | WASM 전체 + LLVM IR + C코드 생성 + 최적화 |

---

## 3. kcode_llvm — LLVM 드라이버

```bash
./kcode_llvm <파일.han>               # LLVM IR stdout 출력
./kcode_llvm <파일.han> -o out.ll     # IR 파일 저장
./kcode_llvm <파일.han> -b            # IR 생성 + clang 빌드
./kcode_llvm <파일.han> -b -o out     # 빌드 후 실행파일 이름 지정
./kcode_llvm <파일.han> -bc           # LLVM 비트코드(.bc) 저장
./kcode_llvm <파일.han> -json         # IDE 연동 JSON 출력
./kcode_llvm <파일.han> -unopt        # 최적화 전 IR 출력
./kcode_llvm -ide                     # IDE 서버 모드 (stdin JSON)
```

---

## 4. kbc — 바이트코드 VM

```bash
./kbc exec    소스.han     # 컴파일 + 즉시 실행
./kbc compile 소스.han     # → 소스.kbc 파일 생성
./kbc run     소스.kbc     # 바이트코드 실행
./kbc dump    소스.kbc     # 바이트코드 디스어셈블 (디버깅용)
```

---

## 5. Python SDK

`libkc_ontology.so`를 ctypes로 감싸서 Python에서 K엔진 온톨로지를 사용합니다.

### 설치 (빌드 필요)

```bash
cmake .. -DKCODE_ONTOLOGY=ON -DBUILD_SHARED_LIBS=ON
cmake --build . --target kc_ontology
cmake --install . --prefix /usr/local
```

### 사용 예시

```python
# kc_ont_sdk.py 사용
from kc_ont_sdk import KcOntology

ont = KcOntology()
ont.add_class("제품")
ont.add_class("전자제품", "제품")
ont.add_instance("전자제품", "TV세트", [["이름","삼성TV"], ["가격", 1500000]])

rows = ont.query("전자제품 찾기")
print(rows)

ont.destroy()
```

---

## 6. Node.js SDK

`ffi-napi`를 사용해 Node.js / TypeScript에서 K엔진을 직접 사용합니다.

### 설치

```bash
npm install ffi-napi ref-napi
```

### 사용 예시

```javascript
const { KcOntology } = require('./kc_ont_sdk');

const ont = new KcOntology();
ont.addClass('제품');
ont.addClass('전자제품', '제품');
ont.addInstance('전자제품', 'TV세트', [['이름','삼성TV'], ['가격', 1500000]]);

const rows = ont.query('전자제품 찾기');
console.log(rows);

ont.destroy();
```

### 지원 플랫폼

| OS | 라이브러리 |
|----|-----------|
| Linux | `libkc_ontology.so` |
| macOS | `libkc_ontology.dylib` |
| Windows | `kc_ontology.dll` |

---

## 7. 실행파일 전체 목록

| 실행파일 | 빌드 방법 | 설명 |
|---------|----------|------|
| `kbc` | `make kvm` | 바이트코드 VM (기본 실행기) |
| `kcode_gen` | `make gen` | C 코드 생성기 |
| `kcode_llvm` | `make LLVM=1` | LLVM IR 생성기 |
| `kserver` | `gcc -O2 -o kserver kserver.c` | 로컬 HTTP 서버 |
| `kcode_vm.wasm` | `emcmake cmake -DBUILD_WASM=ON` | WASM 브라우저 실행 |

---

다음: [[버전 히스토리]] →
