/*
 * kc_wasm_api.h — Kcode WebAssembly 브릿지 헤더
 * version : v16.5.1
 *
 * Emscripten으로 컴파일 시 JS에서 직접 호출 가능한
 * 브릿지 함수 5종 + 내부 출력 캡처 API를 선언한다.
 *
 * 빌드 방법:
 *   emcmake cmake .. -DBUILD_WASM=ON
 *   emmake make kcode_vm
 */

#ifndef KCODE_WASM_API_H
#define KCODE_WASM_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  WASM 빌드 여부 확인
 *  -DKCODE_WASM_BUILD=1 이 설정된 경우에만 WASM API 활성화
 * ================================================================ */
#ifdef KCODE_WASM_BUILD
#  include <emscripten.h>
#  define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#  define WASM_EXPORT  /* 일반 빌드에서는 무시 */
#endif

/* ================================================================
 *  내부 출력 버퍼 API (kc_vm.c / kinterp.c 에서 사용)
 * ================================================================ */

/* 출력 버퍼 초기화 (각 실행 전 호출) */
void kc_wasm_output_reset(void);

/* printf 대신 사용하는 출력 함수 */
void kc_wasm_output_append(const char *str);

/* 현재 버퍼 내용 반환 (NUL 종료 문자열, 내부 소유) */
const char *kc_wasm_output_get(void);

/* ================================================================
 *  JS ↔ WASM 브릿지 함수 5종
 *  모든 반환값은 JSON 문자열 (malloc 할당 — JS 측에서 free 불필요,
 *  다음 호출 시 자동 해제)
 * ================================================================ */

/*
 * kc_wasm_run(source)
 *   소스코드를 컴파일하고 바로 실행한다.
 *   반환: {"success":true, "output":"...", "error":""}
 */
WASM_EXPORT char *kc_wasm_run(const char *source);

/*
 * kc_wasm_compile(source)
 *   소스코드를 바이트코드로 컴파일만 한다.
 *   반환: {"success":true, "bytecode_b64":"...", "errors":[]}
 */
WASM_EXPORT char *kc_wasm_compile(const char *source);

/*
 * kc_wasm_exec_bc(bytecode_b64)
 *   Base64 인코딩된 바이트코드를 실행한다.
 *   반환: {"success":true, "output":"..."}
 */
WASM_EXPORT char *kc_wasm_exec_bc(const char *bytecode_b64);

/*
 * kc_wasm_lex(source)
 *   소스코드를 렉싱하고 토큰 목록을 반환한다. (IDE 하이라이팅용)
 *   반환: {"tokens":[{"type":"...","value":"...","line":1,"col":1},...]}
 */
WASM_EXPORT char *kc_wasm_lex(const char *source);

/*
 * kc_wasm_parse(source)
 *   소스코드를 파싱하고 AST를 JSON으로 반환한다. (IDE 구문 분석용)
 *   반환: {"success":true, "ast":{...}}
 */
WASM_EXPORT char *kc_wasm_parse(const char *source);

/*
 * kc_wasm_version()
 *   엔진 버전 문자열 반환
 */
WASM_EXPORT const char *kc_wasm_version(void);

/*
 * kc_wasm_free(ptr)
 *   WASM 내부에서 malloc한 메모리를 해제한다.
 *   ccall/cwrap 을 통해 JS에서 직접 호출 가능.
 */
WASM_EXPORT void kc_wasm_free(char *ptr);

#ifdef __cplusplus
}
#endif

#endif /* KCODE_WASM_API_H */
