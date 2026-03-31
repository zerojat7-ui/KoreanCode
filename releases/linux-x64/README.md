# Kcode — 셀프 빌드 바이너리

> 이 디렉터리에는 Kcode가 **자기 자신을 컴파일**하여 생성한 최종 실행 파일이 저장됩니다.
>
> 소스: `src/*.han` / 부트스트랩 컴파일러: `../kinterp`, `../kbc`

---

## 버전 이력

| 버전 | 날짜 | 내용 | 빌드 방법 |
|------|------|------|-----------|
| v0.1.0 | 2026-03-22 | 부트스트랩 — C 컴파일러로 생성된 초기 바이너리 | `make` |
| v0.2.0 | 2026-03-22 | §LLVM-SH — Kcode AST → LLVM IR 코드생성기 소스 완성 (9파일) | `src/kc_llvm*.han` |
| v0.3.0 | 2026-03-22 | libkc_rt — Kcode 런타임 라이브러리 C 구현 + make rt | `make rt` |
| v0.4.0 | 2026-03-22 | kcode.han 빌드실행() 구현 — §LLVM-SH 파이프라인 통합 | `kinterp kcode.han` |
| v0.5.0 | 2026-03-22 | 쉐도우 스택 GC + 각각 codegen + 문자열 함수 8종 추가 | `make rt` |
| v0.6.0 | 2026-03-22 | 선택문 codegen + 시도/오류 예외처리 IR + setjmp 런타임 | `make rt` |
| v0.7.0 | 2026-03-23 | AST 정규화 패스 + 객체/멤버 codegen + KcObj 런타임 | `make rt` |
| v0.8.0 | 2026-03-23 | 배열/사전 리터럴 codegen + `추가`/`배열포함` 내장 + kc_arr_contains 런타임 | `make rt` |
| v0.9.0 | 2026-03-23 | 람다 codegen + 인덱스 대입 + `포함`/`찾기`/`자르기` 내장 + kc_str_contains 런타임 | `make rt` |
| v1.0.0 | 2026-03-23 | `src/kllvm.han` 공개 API + kc_arr_find 런타임 + kcode.han §LLVM-SH v1.0.0 통합 | `make rt` |
| v1.1.0 | 2026-03-23 | §LLVM-SH Hello World 엔드-투-엔드 컴파일 테스트 — 12 통과 / 0 실패 | `make test-llvm` |
| v1.2.0 | 2026-03-27 | §LLVM-SH 풀 파이프라인 — `kinterp kcode.han src/khello.han -o /tmp/khello.out` 네이티브 빌드 성공 | `./kinterp kcode.han src/khello.han -o /tmp/khello.out` |

---

## v0.1.0 — 부트스트랩 빌드 (2026-03-22)

**빌드 방법:** `make` (gcc -std=c11)

**포함 바이너리:**

| 파일 | 크기 | 설명 |
|------|------|------|
| `kinterp` | 1.1 MB | 인터프리터 — `.han` 파일 즉시 실행 |
| `kbc`     | 414 KB | 바이트코드 VM — `.kbc` 컴파일 + 실행 |
| `kcode_gen` | 371 KB | C 코드 생성기 |

**기반 소스 버전:**
- 인터프리터 엔진: `kinterp.c` v32.1.0
- 렉서: `klexer.c` v32.1.0
- 파서: `kparser.c` v32.1.0

**테스트 결과:** 98 통과 / 0 실패

---

## 디렉터리 구조

```
KcodeBuild/
├── bin/            ← 최종 셀프 빌드 바이너리 (이 디렉터리)
│   ├── README.md   ← 빌드 이력 (이 파일)
│   ├── kinterp     ← 인터프리터
│   ├── kbc         ← 바이트코드 VM
│   └── kcode_gen   ← C 코드 생성기
├── src/            ← Kcode 셀프 호스팅 소스
│   ├── klexer.han  ← 렉서 (Kcode 구현)
│   ├── kparser.han ← 파서 (Kcode 구현)
│   ├── kinterp.han ← 인터프리터 (Kcode 구현)
│   ├── kc_llvm.hg         ← §LLVM-SH 헤더
│   ├── kc_llvm_ctx.han    ← 컨텍스트/모듈/빌더
│   ├── kc_llvm_type.han   ← 타입 시스템
│   ├── kc_llvm_const.han  ← 상수 생성
│   ├── kc_llvm_inst.han   ← 명령어 생성
│   ├── kc_llvm_func.han   ← 함수 관리
│   ├── kc_llvm_gen.han    ← AST 순회 코드생성기
│   ├── kc_llvm_rt.han     ← 런타임 바인딩
│   └── kc_llvm_out.han    ← .ll 직렬화 + llc/clang
│   └── *.hg        ← 헤더 (상수/타입 정의)
├── kcode.han       ← 통합 CLI (셀프 호스팅 진입점)
└── Makefile        ← 부트스트랩 빌드 스크립트
```

---

## v0.2.0 — §LLVM-SH 코드생성기 소스 완성 (2026-03-22)

**내용:** Kcode AST → LLVM IR 9단계 파이프라인 소스 완성

| 파일 | 역할 |
|------|------|
| `src/kc_llvm.hg` | 헤더 — 타입/명령코드/구조 상수 |
| `src/kc_llvm_ctx.han` | 컨텍스트/모듈/빌더/기본블록 관리 |
| `src/kc_llvm_type.han` | 타입 시스템 (void/i1~i64/float/ptr/struct) |
| `src/kc_llvm_const.han` | 상수 생성 (정수/실수/문자열 전역/null) |
| `src/kc_llvm_inst.han` | 명령어 생성 (alloca/load/store/산술/br/phi/call/gep) |
| `src/kc_llvm_func.han` | 함수 관리 (선언/정의/extern/ABI) |
| `src/kc_llvm_gen.han` | AST 순회 코드생성기 (표현식/문장/제어흐름) |
| `src/kc_llvm_rt.han` | 런타임 바인딩 (C stdlib + Kcode RT extern 선언) |
| `src/kc_llvm_out.han` | 출력 파이프라인 (.ll 직렬화 + llc/clang/opt 호출) |

**다음 단계:** `libkc_rt.c` 구현 → llc/clang 링크 → 실행 파일 생성

---

## v0.6.0 — 선택문 codegen + 예외처리 IR (2026-03-22)

**내용:** `선택문(switch)` LLVM IR 생성 + `시도/오류발생/잡기/항상` 예외처리 IR + setjmp/longjmp 런타임

| 파일 | 변경 내용 |
|------|----------|
| `src/kc_llvm_inst.han` | `llvm빌드switch()` — LLVM switch 명령어 생성 |
| `src/kc_llvm_gen.han` | `llvm선택생성()` (정수→switch, 문자열→icmp 체인) + `llvm시도생성()` + `llvm오류발생생성()` |
| `src/kc_llvm_rt.han` | `kc_try_begin/end/throw/catch_msg` extern 선언 추가 |
| `kc_rt.h` | `KcExcFrame` 구조체 + 예외 함수 선언 |
| `kc_rt.c` | setjmp/longjmp 기반 예외 프레임 스택 구현 |

**예외 처리 패턴:**
```
%r = call i32 @kc_try_begin()     ; 0=정상, 1=예외
%cond = icmp ne i32 %r, 0
br i1 %cond, label %catch, label %try_body
try_body:   ... call @kc_try_end()  ...  br %finally
catch:      %msg = call ptr @kc_catch_msg()  ...  call @kc_try_end()
finally:    ...
```

---

## v0.7.0 — AST 정규화 + 객체/멤버 codegen (2026-03-23)

**내용:** kparser.han 원시 AST 형식과 kc_llvm_gen.han 기대 형식의 불일치를 `llvmAST정규화()` 패스로 해결 + 객체/멤버 IR 생성

| 파일 | 변경 내용 |
|------|----------|
| `src/kc_llvm_gen.han` | `llvmAST정규화()` — kparser.han `"자식들"/"문자열"/연산자int` → gen.han `"자식"/"이름"/연산자string` |
| `src/kc_llvm_gen.han` | `llvm클래스선언생성()` — 클래스 등록 + 메서드 함수 emit |
| `src/kc_llvm_gen.han` | `llvm멤버접근생성()` — `kc_obj_get(obj, field)` 호출 |
| `src/kc_llvm_gen.han` | `llvm호출생성()` — 클래스 생성자 탐지 + 메서드 호출 처리 |
| `src/kc_llvm_gen.han` | `llvm대입생성()` — `obj.field = val` → `kc_obj_set` 처리 |
| `src/kc_llvm_gen.han` | `llvm이항생성()` — 문자열 `+` → `kc_str_concat` 자동 처리 |
| `kc_rt.h` | `KcObj` 구조체 + `kc_obj_new/set/get/has` 선언 |
| `kc_rt.c` | `KcObj` 64-필드 문자열 키 맵 구현 (GC 관리) |
| `src/kc_llvm_rt.han` | `kc_obj_new/set/get/has` extern 선언 |

---

## v0.8.0 — 배열/사전 리터럴 codegen (2026-03-23)

**내용:** `NODE_ARRAY_LIT` / `NODE_DICT_LIT` IR 생성 + `추가`/`배열포함` 내장 함수 런타임 매핑 + `kc_arr_contains` 런타임 구현

| 파일 | 변경 내용 |
|------|----------|
| `src/kc_llvm_gen.han` | `llvmAST정규화()` — `NODE_ARRAY_LIT`/`NODE_DICT_LIT` 정규화 추가 |
| `src/kc_llvm_gen.han` | 표현식 디스패처 — `NODE_ARRAY_LIT` → `llvm배열리터럴생성()`, `NODE_DICT_LIT` → `llvm사전리터럴생성()` |
| `src/kc_llvm_gen.han` | `llvm배열리터럴생성()` — `kc_arr_new(cap)` + 원소별 `kc_arr_push` |
| `src/kc_llvm_gen.han` | `llvm사전리터럴생성()` — `kc_obj_new("")` + 키/값 쌍별 `kc_obj_set` |
| `src/kc_llvm_rt.han` | `kc_arr_contains` extern 선언 추가 + `추가`/`배열포함` 내장 매핑 추가 |
| `kc_rt.h` | `kc_arr_contains(ptr arr, i64 val) → i64` 선언 |
| `kc_rt.c` | `kc_arr_contains` 선형 탐색 구현 |

**배열 리터럴 IR 패턴:**
```
%arr = call ptr @kc_arr_new(i64 N)
call void @kc_arr_push(ptr %arr, i64 %elem0)
...
```

**사전 리터럴 IR 패턴:**
```
%obj = call ptr @kc_obj_new(ptr @.str.empty)
call void @kc_obj_set(ptr %obj, ptr @.str.key0, i64 %val0)
...
```

---

## v0.9.0 — 람다 codegen + 인덱스 대입 (2026-03-23)

**내용:** `NODE_LAMBDA` 익명 함수 IR 생성 (상태 저장/복원 패턴) + `arr[i] = val` 인덱스 대입 + 문자열 `포함`/`찾기`/`자르기` 내장 매핑 + `kc_str_contains` 런타임 추가

| 파일 | 변경 내용 |
|------|----------|
| `src/kc_llvm_gen.han` | `llvmAST정규화()` — `NODE_LAMBDA` 매개변수/본문 분리 |
| `src/kc_llvm_gen.han` | 표현식 디스패처 — `NODE_LAMBDA` → `llvm람다생성()` |
| `src/kc_llvm_gen.han` | `llvm람다생성()` — 함수 상태 저장/복원 + `__lambda_N` 함수 리프트 |
| `src/kc_llvm_gen.han` | `llvm대입생성()` — `NODE_INDEX` 대상 → `kc_arr_set_i64` 호출 |
| `src/kc_llvm_rt.han` | `포함` (str/arr), `찾기` (str/arr), `자르기` 내장 매핑 추가 |
| `src/kc_llvm_rt.han` | `kc_str_contains(ptr, ptr) → i64` extern 선언 추가 |
| `kc_rt.h` / `kc_rt.c` | `kc_str_contains()` 구현 추가 (`strstr` 기반) |

**람다 IR 패턴:**
```llvm
define internal i64 @__lambda_0(i64 %x.arg) #0 {
entry:
  %x.addr = alloca i64
  store i64 %x.arg, ptr %x.addr
  %0 = load i64, ptr %x.addr
  %1 = add i64 %0, 1
  ret i64 %1
}
; ... 호출 지점에서:
%fp = getelementptr ptr @__lambda_0, ...
```

**인덱스 대입 IR 패턴:**
```llvm
call void @kc_arr_set_i64(ptr %arr, i64 %idx, i64 %val)
```

---

## v1.0.0 — src/kllvm.han 공개 API + 파이프라인 통합 (2026-03-23)

**내용:** §LLVM-SH v0.2.0 고수준 공개 API 래퍼 `src/kllvm.han` 신규 작성 + `kc_arr_find` 런타임 + `kcode.han` §LLVM-SH v1.0.0 전환

| 파일 | 변경 내용 |
|------|----------|
| `src/kllvm.han` | **신규** — §LLVM-SH v0.2.0 공개 API: `kllvm_IR생성()`, `kllvm_IR저장()`, `kllvm_IR문자열()`, `kllvm_빌드()`, `kllvm_진단()`, `kllvm_오류메시지()` |
| `kcode.han` | `#포함 "src/kllvm.han"` 전환 + `kllvm_IR생성()` API 사용 + §LLVM-SH v1.0.0 주석 |
| `src/kc_llvm_rt.han` | `kc_arr_find(ptr, i64) → i64` extern 선언 추가 |
| `kc_rt.h` | `kc_arr_find()` 선언 추가 |
| `kc_rt.c` | `kc_arr_find()` 선형 탐색 구현 추가 |

**공개 API 요약:**
```kcode
목록 ctx = kllvm_IR생성(ast, 소스파일, 소스파일)   // AST → IR 컨텍스트
논리 성공 = kllvm_IR저장(ctx, "output.ll")         // .ll 파일 저장
문자 ir   = kllvm_IR문자열(ctx)                    // IR 문자열 추출
정수 코드  = kllvm_빌드(ast, "src.han", "a.out", "-O2") // 풀 빌드
```

---

## 다음 버전 계획

| 버전 | 내용 |
|------|------|
| ~~v0.3.0~~ | ✅ 완성 — `kc_rt.c` / `libkc_rt.a` |
| ~~v0.4.0~~ | ✅ 완성 — `kcode.han` `빌드실행()` 구현 |
| ~~v0.5.0~~ | ✅ 완성 — 쉐도우 스택 GC + `각각` codegen + 문자열 8종 |
| ~~v0.6.0~~ | ✅ 완성 — `선택문` codegen + `시도/오류발생` 예외처리 IR + setjmp 런타임 |
| ~~v0.7.0~~ | ✅ 완성 — AST 정규화 패스 + `객체/멤버` codegen + KcObj 런타임 |
| ~~v0.8.0~~ | ✅ 완성 — `배열/사전 리터럴` codegen + `추가`/`배열포함` 내장 매핑 + `kc_arr_contains` 런타임 |
| ~~v0.9.0~~ | ✅ 완성 — `람다(NODE_LAMBDA)` codegen + `인덱스 대입` + `포함`/`찾기`/`자르기` 내장 + `kc_str_contains` 런타임 |
| ~~v1.0.0~~ | ✅ 완성 — `src/kllvm.han` §LLVM-SH v0.2.0 공개 API + `kc_arr_find` + `kcode.han` 통합 업데이트 |
| ~~v1.1.0~~ | ✅ 완성 — §LLVM-SH Hello World 엔드-투-엔드 컴파일 테스트 12 통과 / 0 실패 |
| ~~v1.2.0~~ | ✅ 완성 — §LLVM-SH kinterp 경유 풀 파이프라인 — `./kinterp kcode.han src/khello.han -o /tmp/khello.out` → "Hello, World!" |
