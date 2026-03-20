/*
 * kcodegen.h  —  Kcode 한글 프로그래밍 언어 C 코드 생성기 헤더
 * version : v10.1.0
 *
 * v10.1.0 변경:
 *   - kcodegen.c v10.1.0 동기화 (누락 내장 함수 36종 추가)
 *
 * v9.6.0 변경:
 *   - 글자 함수 21종 gen_builtin_call() 추가 (kcodegen.c v9.6.0 동기화)
 *
 * v9.0.0 변경:
 *   - 가속기 NPU 경로 완전 구현 (kcodegen.c v9.0.0 동기화)
 *
 * v8.1.0 변경:
 *   - 스크립트 블록 코드 생성 완전 구현 (Python/Java/JavaScript)
 *
 * v6.0.0 변경:
 *   - 인터럽트 시스템 3종 C 코드 변환 지원
 *   - gen_runtime_header() signal.h/unistd.h 자동 삽입
 *   - kcodegen.c v6.0.0 과 동기화
 *
 * AST(추상 구문 트리)를 C 소스코드(.c)로 변환한다.
 * gcc 등으로 컴파일하면 네이티브 실행파일을 생성할 수 있다.
 *
 * v5.1.0 변경:
 *   - 법위반(사후조건) 함수 래퍼 완전 구현
 *     - postcond_fns[] / postcond_fn_count 필드 추가
 *     - 법위반 대상 함수는 kc__fn_impl + kc_fn 래퍼 쌍으로 생성
 *     - 제재별 실제 C 코드 삽입 (경고/보고/중단/회귀/대체 모두 지원)
 *
 * IDE 연동 포인트:
 *   - CodegenResult 구조체: 생성 결과 + 소스맵 + 오류 목록
 *   - codegen_to_json()   : IDE 콘솔에 JSON 형식으로 결과 전달
 *   - codegen_sourcemap() : 한글 소스 라인 ↔ C 라인 매핑
 *
 * 실행 흐름:
 *   .han → 렉서 → 파서 → AST → [kcodegen] → .c → gcc → 실행파일
 *
 * MIT License
 * zerojat7
 */

#ifndef KCODE_CODEGEN_H
#define KCODE_CODEGEN_H

#include "kparser.h"
#include <stdio.h>
#include <stdint.h>

/* ================================================================
 *  소스맵 엔트리 — 한글 소스 라인 ↔ 생성된 C 라인 매핑
 *  IDE에서 오류 위치를 한글 소스 기준으로 표시할 때 사용
 * ================================================================ */
typedef struct {
    int han_line;   /* .han 원본 라인 번호 */
    int han_col;    /* .han 원본 열 번호   */
    int c_line;     /* 생성된 .c 라인 번호 */
} SourceMapEntry;

/* ================================================================
 *  코드 생성 오류
 * ================================================================ */
typedef struct {
    int  line;          /* 원본 .han 라인 */
    int  col;           /* 원본 .han 열   */
    char msg[256];      /* 오류 메시지    */
} CodegenError;

#define CODEGEN_MAX_ERRORS   64
#define CODEGEN_MAX_SOURCEMAP 4096

/* ================================================================
 *  코드 생성 결과 구조체
 *  IDE는 이 구조체를 통해 생성 결과 전체를 받는다
 * ================================================================ */
typedef struct {
    char  *c_source;        /* 생성된 C 소스 코드 (힙 할당, 호출자가 free) */
    size_t c_source_len;    /* c_source 길이 */

    /* 소스맵 */
    SourceMapEntry sourcemap[CODEGEN_MAX_SOURCEMAP];
    int            sourcemap_count;

    /* 오류 목록 */
    CodegenError   errors[CODEGEN_MAX_ERRORS];
    int            error_count;
    int            had_error;

    /* 통계 (IDE 정보 패널용) */
    int  func_count;        /* 생성된 함수 수  */
    int  var_count;         /* 전역 변수 수    */
    int  line_count;        /* 생성된 C 라인 수*/
} CodegenResult;

/* ================================================================
 *  코드 생성기 내부 상태
 * ================================================================ */
#define CODEGEN_BUF_INIT  (64 * 1024)   /* 초기 버퍼 64KB */

typedef struct {
    /* 출력 버퍼 */
    char  *buf;         /* 동적 확장 버퍼      */
    size_t buf_len;     /* 현재 사용 길이       */
    size_t buf_cap;     /* 버퍼 용량            */

    /* 들여쓰기 */
    int    indent;      /* 현재 들여쓰기 수준   */

    /* 현재 생성 중인 C 라인 번호 */
    int    c_line;

    /* 결과 */
    CodegenResult *result;

    /* 임시 변수 카운터 (충돌 방지) */
    int    tmp_counter;

    /* 현재 함수 안에 있는지 (반환값 처리용) */
    int    in_func;
    int    func_has_return; /* 함수가 반환값을 가지는지 (함수 vs 정의) */

    /* 오류 발생 시 계속 진행 여부 */
    int    had_error;

    /* ★ 계약 시스템 — 법령/법위반 노드 존재 여부 (1패스 선탐지) */
    int    has_contracts;   /* 1이면 #include <assert.h> 헤더 삽입 */

    /* ★ 법위반(사후조건) 래퍼 시스템 (v5.1.0) */
#define POSTCOND_FN_MAX 128
    const char *postcond_fns[POSTCOND_FN_MAX];  /* 법위반 대상 함수명 포인터 배열 (AST 수명 따름) */
    int         postcond_fn_count;               /* 등록된 수 */

    /* §14-4 온톨로지 블록 전송 계층 컨텍스트 (v26.2.0) */
    int    ont_mode;   /* 0=내장/1=대여/2=접속(KACP) — gen_stmt 하위 노드 전달용 */
    int    ont_line;   /* 현재 ont 블록의 라인 번호 — _ont_remote_{line} 변수명 생성용 */
} Codegen;

/* ================================================================
 *  공개 API
 * ================================================================ */

/*
 * codegen_run() — AST 전체를 C 코드로 변환
 *
 * 반환: CodegenResult* (호출자가 codegen_result_free()로 해제해야 함)
 *       오류 시에도 NULL 대신 had_error=1 인 결과를 반환한다.
 */
CodegenResult *codegen_run(Node *program);

/*
 * codegen_result_free() — 결과 구조체 해제
 */
void codegen_result_free(CodegenResult *r);

/*
 * codegen_to_json() — IDE 연동용 JSON 출력
 *
 * IDE는 이 JSON을 파싱해서:
 *   - "c_source"   : C 코드를 에디터에 표시
 *   - "sourcemap"  : 오류 위치를 한글 소스에 하이라이트
 *   - "errors"     : 오류 패널에 표시
 *   - "stats"      : 정보 패널에 표시
 */
void codegen_to_json(const CodegenResult *r, FILE *out);

/*
 * codegen_compile() — C 소스를 gcc로 컴파일해 실행파일 생성
 *
 * c_path  : 임시 .c 파일 경로
 * out_path: 실행파일 경로
 * 반환: 0=성공, 비0=실패
 */
int codegen_compile(const char *c_path, const char *out_path);

#endif /* KCODE_CODEGEN_H */
