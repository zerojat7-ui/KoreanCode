/*
 * kcodegen.c  —  Kcode 한글 프로그래밍 언어 C 코드 생성기
 * version : v18.1.0
 *
 * v18.1.0:
 *   - gen_builtin_call() 신규 내장함수 31종 C코드 생성 추가
 *     수학: 제곱/라디안/각도/난수/난정수
 *     역삼각: 아크사인/아크코사인/아크탄젠트/아크탄젠트2
 *     글자: 좌문자/우문자/채우기/코드/붙여씀
 *     배열: 배열삭제/배열찾기/배열포함/배열합치기/배열자르기/유일값/배열채우기
 *     시간: 현재시간/현재날짜/시간포맷/경과시간
 *     시스템: 환경변수/종료/명령실행/잠깐
 *     JSON: JSON생성/JSON파싱
 *
 * v15.0.0:
 *   - gen_builtin_call() — autograd 내장함수 13종 C코드 생성 추가
 *     역전파 / 기울기초기화 / 미분추적
 *     미분더하기~미분제곱 10종
 *
 * v11.0.0:
 *   - NODE_TENSOR_LIT C 코드 생성 추가
 *   - 텐서 내장 함수 12종 gen_builtin_call() 추가
 *
 * v10.1.0 변경:
 *   - gen_builtin_call() 누락 내장 함수 36종 추가
 *     - 글자(형변환) / 추가(배열push)
 *     - 통계 13종: 합계/평균/분산/표준편차/중앙값/최빈값/누적합/공분산/상관계수/정규화/표준화/배열정렬/배열뒤집기
 *     - AI 활성함수 3종: 시그모이드/렐루/쌍곡탄젠트
 *     - 관계함수: 호감도
 *     - 파일 17종: 파일열기/닫기/읽기/줄읽기/쓰기/줄쓰기/있음/크기/목록/이름/확장자/폴더만들기/지우기/복사/이동/전체읽기/전체쓰기
 *
 * v9.6.0 변경:
 *   - 글자 함수 21종 gen_builtin_call() 직접 C 코드 생성 추가
 *     - 1단계 기본: 자르기(kc_str_sub) / 분할(kc_str_split) /
 *                   합치기(kc_str_join) / 반복글자(kc_str_repeat) /
 *                   역순(kc_str_reverse)
 *     - 2단계 검색: 포함(kc_str_contains) / 위치(kc_str_indexof) /
 *                   시작(kc_str_startswith) / 끝확인(kc_str_endswith) /
 *                   비교(kc_str_compare)
 *     - 3단계 변환: 대문자(kc_str_upper) / 소문자(kc_str_lower) /
 *                   제목식(kc_str_title) / 대체(kc_str_replace) /
 *                   한번대체(kc_str_replace_once)
 *     - 4단계 정제: 앞공백제거(kc_str_ltrim) / 뒤공백제거(kc_str_rtrim) /
 *                   공백제거(kc_str_trim)
 *     - 5단계 고급: 반복확인(kc_str_regex) / 분석(kc_str_parse) /
 *                   포맷(kc_str_format)
 *
 * v2.0.0 변경 (가속기 전면 재구현):
 *   - Zero-Copy SoA + VRAM 상주 + DMA 직전송
 *   - 외부 프로세스(nvcc/python3/gcc) 의존성 제거
 *   - kc_accel.h API 기반 — begin/exec/end 3단계
 *     - 5종 연산별 ONNX 그래프 빌드: 행렬곱(MatMul)/행렬합(Add)/활성화(Relu)/전치(Transpose)/합성곱(Conv)
 *     - setenv() → python3 자동생성 스크립트 실행 → stdout 수집 → 반환변수 대입
 *     - GPU 폴백 실패 시 NPU → CPU → 일반 실행 체인 완성
 *
 * v8.2.0 변경:
 *   - 가속기(GPU/NPU/CPU) CUDA C 직접 생성 구현 (NODE_GPU_BLOCK / NODE_GPU_OP)
 *     - GPU: CUDA C 자동 생성 → nvcc 컴파일 → 실행
 *       - 내장 연산 5종 CUDA 커널: 행렬곱/행렬합/합성곱/활성화/전치
 *       - 배열 변수 CSV 직렬화 → 환경변수 전달 → GPU 실행 → CSV 역직렬화
 *     - NPU: ONNX Runtime C API 위임 (onnxruntime 설치 시)
 *     - CPU: OpenMP 병렬 코드 생성 (gcc -fopenmp)
 *     - 폴백 체인: GPU → NPU → CPU → 경고 + 일반 실행
 *     - emit_cuda_kernel_header() / emit_cuda_kernel_body() 헬퍼 추가
 *     - emit_gpu_accel_block() 메인 헬퍼 추가
 *
 * v8.1.0 변경:
 *   - 스크립트 블록 C 코드 생성 완전 구현 (NODE_SCRIPT_PYTHON/JAVA/JS)
 *     - 전달 변수 → setenv() 환경변수 설정
 *     - 스크립트 원문 → 임시 파일(/tmp/_kcode_cg_*.ext) fwrite 저장
 *     - Python: import os + 환경변수 자동 임포트 헤더 삽입
 *     - JavaScript: process.env 기반 변수 임포트 헤더 삽입
 *     - Java: mkdir+javac+java 컴파일-실행 파이프라인
 *     - system() 실행 → stdout 캡처 → 반환 변수에 마지막 줄 대입
 *     - 비정상 종료 시 stderr 오류 출력
 *     - 임시 파일 자동 삭제
 *
 * v6.0.0 변경:
 *   - 인터럽트 시스템 3종 C 코드 변환 추가
 *   - A: 신호받기 → signal() + 핸들러 함수 / 신호무시/기본 → SIG_IGN/DFL / 신호보내기 → kill()
 *   - B: 간섭 → ISR() AVR 매크로 + 일반 플랫폼 시뮬레이션 함수 / 간섭잠금/허용 → cli()/sei()
 *   - C: 행사등록 → kc_event_register() / 행사시작/중단/발생/해제 → 이벤트 루프 런타임
 *   - gen_runtime_header() 에 signal.h/unistd.h + 행사 런타임 테이블 자동 삽입
 *
 * AST를 순회하며 C99 호환 소스코드를 생성한다.
 * 생성된 C 코드는 gcc로 바로 컴파일 가능하다.
 *
 * v5.1.0 변경:
 *   - 법위반(사후조건) 함수 래퍼 완전 구현
 *     - detect_postconds()      : 1패스 — 법위반 대상 함수명 수집
 *     - is_postcond_fn()        : 함수명이 법위반 대상인지 조회
 *     - gen_func_forward()      : 법위반 대상은 _impl + 래퍼 전방선언 둘 다 출력
 *     - gen_func_def()          : 법위반 대상 본체는 kc__fn_impl() 로 생성
 *     - gen_postcond_wrapper()  : kc_fn() 래퍼 생성
 *                                 → kc__fn_impl 호출 → 사후조건 검증 → 제재
 *     - gen_program()           : 단계3.5 에 래퍼 생성 추가
 *     - gen_contract() 법위반 else 분기: 주석 전용 → 래퍼 위임 주석으로 교체
 *
 * v2.1.0 변경:
 *   - 법령/법위반 계약 시스템 C 코드 변환 추가
 *     - NODE_CONTRACT (법령)     -> assert(조건) 삽입
 *     - NODE_CONTRACT (법위반)   -> 사후조건 주석 출력
 *     - NODE_CHECKPOINT          -> 복원지점 주석 출력
 *     - NODE_SANCTION            -> NODE_CONTRACT 내부에서 처리
 *     - assert.h 상단 자동 삽입 (계약 노드 존재 시)
 *   - 계약 노드 존재 여부 1패스 선탐지(has_contracts)로 헤더 조건부 삽입
 *
 * 생성 범위:
 *   - 정수/실수/논리/문자/글자/없음 자료형
 *   - 변수 선언 (var/const)
 *   - 산술/비교/논리/비트/복합대입 연산
 *   - 제어문: 만약/아니면, 동안, 반복(범위), 각각(foreach)
 *   - 선택(switch)/경우(case)/그외(default)
 *   - 함수/정의 선언 및 호출, 재귀
 *   - 멈춤/건너뜀 (break/continue)
 *   - 시도/실패시/항상 (setjmp/longjmp 기반)
 *   - 배열 리터럴 및 인덱스 접근
 *   - 람다 -> C 정적 함수로 변환
 *   - 내장 함수: 출력, 출력없이, 길이, 범위, 형변환, 수학
 *   - 법령/법위반/복원지점 계약 시스템 -> assert / 주석 변환
 *   - 소스맵 생성 (IDE 연동)
 *   - JSON 출력 (IDE 콘솔 프로토콜)
 *
 * MIT License
 * zerojat7
 */

#define _POSIX_C_SOURCE 200809L

#include "kcodegen.h"
#include "kparser.h"
#include "klexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

/* ================================================================
 *  내부 매크로
 * ================================================================ */
#define CG_INDENT_SIZE  4

/* ================================================================
 *  버퍼 관리
 * ================================================================ */
static void buf_ensure(Codegen *cg, size_t extra) {
    while (cg->buf_len + extra + 1 >= cg->buf_cap) {
        cg->buf_cap *= 2;
        cg->buf = (char*)realloc(cg->buf, cg->buf_cap);
    }
}

/* 형식 문자열을 버퍼에 추가 */
static void emit(Codegen *cg, const char *fmt, ...) {
    va_list ap;
    char tmp[4096];
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n <= 0) return;

    buf_ensure(cg, (size_t)n);
    memcpy(cg->buf + cg->buf_len, tmp, (size_t)n);
    cg->buf_len += (size_t)n;
    cg->buf[cg->buf_len] = '\0';

    /* 줄바꿈 수 만큼 C 라인 카운터 증가 */
    for (int i = 0; i < n; i++) {
        if (tmp[i] == '\n') cg->c_line++;
    }
}

/* 들여쓰기 출력 */
static void emit_indent(Codegen *cg) {
    for (int i = 0; i < cg->indent * CG_INDENT_SIZE; i++) {
        buf_ensure(cg, 1);
        cg->buf[cg->buf_len++] = ' ';
    }
    cg->buf[cg->buf_len] = '\0';
}

/* 줄 시작 (들여쓰기 + 내용) */
static void emitln(Codegen *cg, const char *fmt, ...) {
    emit_indent(cg);
    va_list ap;
    char tmp[4096];
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) {
        buf_ensure(cg, (size_t)n + 1);
        memcpy(cg->buf + cg->buf_len, tmp, (size_t)n);
        cg->buf_len += (size_t)n;
    }
    buf_ensure(cg, 1);
    cg->buf[cg->buf_len++] = '\n';
    cg->buf[cg->buf_len]   = '\0';
    cg->c_line++;
}

/* ================================================================
 *  소스맵 등록
 * ================================================================ */
static void sourcemap_add(Codegen *cg, int han_line, int han_col) {
    CodegenResult *r = cg->result;
    if (r->sourcemap_count >= CODEGEN_MAX_SOURCEMAP) return;
    SourceMapEntry *e = &r->sourcemap[r->sourcemap_count++];
    e->han_line = han_line;
    e->han_col  = han_col;
    e->c_line   = cg->c_line;
}

/* ================================================================
 *  오류 등록
 * ================================================================ */
static void cg_error(Codegen *cg, int line, int col, const char *fmt, ...) {
    CodegenResult *r = cg->result;
    if (r->error_count >= CODEGEN_MAX_ERRORS) return;
    CodegenError *e = &r->errors[r->error_count++];
    e->line = line;
    e->col  = col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
    va_end(ap);
    r->had_error  = 1;
    cg->had_error = 1;
}

/* ================================================================
 *  C 자료형 이름 반환
 * ================================================================ */
static const char *c_type(TokenType dtype) {
    switch (dtype) {
        case TOK_KW_JEONGSU:  return "int64_t";
        case TOK_KW_SILSU:    return "double";
        case TOK_KW_NOLI:     return "int";         /* bool → int */
        case TOK_KW_GEULJA:   return "uint32_t";    /* 문자 (UTF-32) */
        case TOK_KW_MUNJA:    return "char*";
        case TOK_KW_EOPSEUM:  return "void*";
        case TOK_KW_BAELYEOL: return "kc_array_t*";
        default:              return "kc_value_t";  /* 범용 */
    }
}

/* ================================================================
 *  문자열 C 이스케이프
 * ================================================================ */
static void escape_string(Codegen *cg, const char *s) {
    emit(cg, "\"");
    for (; *s; s++) {
        switch (*s) {
            case '"':  emit(cg, "\\\""); break;
            case '\\': emit(cg, "\\\\"); break;
            case '\n': emit(cg, "\\n");  break;
            case '\r': emit(cg, "\\r");  break;
            case '\t': emit(cg, "\\t");  break;
            default:
                buf_ensure(cg, 1);
                cg->buf[cg->buf_len++] = *s;
                cg->buf[cg->buf_len]   = '\0';
        }
    }
    emit(cg, "\"");
}

/* ================================================================
 *  전방 선언
 * ================================================================ */
static void gen_expr(Codegen *cg, Node *n);
static void gen_stmt(Codegen *cg, Node *n);
static void gen_block(Codegen *cg, Node *n);
static void gen_contract(Codegen *cg, Node *n);
static void gen_checkpoint(Codegen *cg, Node *n);
/* ★ v5.1.0 — 법위반 래퍼 시스템 전방선언 */
static int  is_postcond_fn(Codegen *cg, const char *fn_name);
static void emit_postcond_check(Codegen *cg, Node *n);
static void emit_all_postconds(Codegen *cg, Node *node, const char *fn_name);
static void gen_postcond_wrapper(Codegen *cg, Node *fn_node, Node *program);

/* ================================================================
 *  연산자 토큰 → C 연산자 문자열
 * ================================================================ */
static const char *op_to_c(TokenType op) {
    switch (op) {
        case TOK_PLUS:       return "+";
        case TOK_MINUS:      return "-";
        case TOK_STAR:       return "*";
        case TOK_SLASH:      return "/";
        case TOK_PERCENT:    return "%";
        case TOK_EQEQ:       return "==";
        case TOK_BANGEQ:     return "!=";
        case TOK_LT:         return "<";
        case TOK_GT:         return ">";
        case TOK_LTEQ:       return "<=";
        case TOK_GTEQ:       return ">=";
        case TOK_KW_AND:     return "&&";   /* 그리고 */
        case TOK_KW_OR:      return "||";   /* 또는   */
        case TOK_KW_NOT:     return "!";    /* 아니다 */
        case TOK_AMP:        return "&";
        case TOK_PIPE:       return "|";
        case TOK_CARET:      return "^";
        case TOK_TILDE:      return "~";
        case TOK_LTLT:       return "<<";
        case TOK_GTGT:       return ">>";
        case TOK_EQ:         return "=";
        case TOK_PLUSEQ:     return "+=";
        case TOK_MINUSEQ:    return "-=";
        case TOK_STAREQ:     return "*=";
        case TOK_SLASHEQ:    return "/=";
        case TOK_PERCENTEQ:  return "%=";
        default:             return "??";
    }
}

/* ================================================================
 *  내장 함수 이름 → C 호출로 변환
 * ================================================================ */
static int gen_builtin_call(Codegen *cg, Node *n) {
    /* n = NODE_CALL, n->children[0] = NODE_IDENT(함수명) */
    if (n->child_count < 1) return 0;
    Node *fn = n->children[0];
    if (!fn || fn->type != NODE_IDENT || !fn->sval) return 0;

    const char *name = fn->sval;

    /* 출력 → printf + "\n" */
    if (strcmp(name, "\xEC\xB6\x9C\xEB\xa0\xa5") == 0) { /* 출력 */
        emit(cg, "kc_print(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ")");
        return 1;
    }
    /* 출력없이 → printf (줄바꿈 없음) */
    if (strcmp(name, "\xEC\xB6\x9C\xEB\xa0\xa5\xEC\x97\x86\xEC\x9D\xB4") == 0) { /* 출력없이 */
        emit(cg, "kc_print_no_newline(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ")");
        return 1;
    }
    /* 입력 → fgets */
    if (strcmp(name, "\xEC\x9E\x85\xEB\xa0\xa5") == 0) { /* 입력 */
        emit(cg, "kc_input()");
        return 1;
    }
    /* 길이 → kc_len */
    if (strcmp(name, "\xEA\xB8\xB8\xEC\x9D\xB4") == 0) { /* 길이 */
        emit(cg, "kc_len(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 범위 → kc_range */
    if (strcmp(name, "\xEB\xB2\x94\xEC\x9C\x84") == 0) { /* 범위 */
        emit(cg, "kc_range(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ")");
        return 1;
    }
    /* 수학 내장 */
    if (strcmp(name, "\xEC\xA0\x88\xEB\x8C\x93\xEA\xB0\x92") == 0) { /* 절댓값 */
        emit(cg, "kc_abs("); gen_expr(cg, n->children[1]); emit(cg, ")");
        return 1;
    }
    if (strcmp(name, "\xEC\xB5\x9C\xEB\x8C\x80") == 0) { /* 최대 */
        emit(cg, "kc_max(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ", KC_SENTINEL)");
        return 1;
    }
    if (strcmp(name, "\xEC\xB5\x9C\xEC\x86\x8C") == 0) { /* 최소 */
        emit(cg, "kc_min(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ", KC_SENTINEL)");
        return 1;
    }
    if (strcmp(name, "\xEC\xA0\x9C\xEA\xB3\xB1\xEA\xB7\xBC") == 0) { /* 제곱근 */
        emit(cg, "sqrt("); gen_expr(cg, n->children[1]); emit(cg, ")");
        return 1;
    }
    if (strcmp(name, "\xEC\xA0\x9C\xEA\xB3\xB1") == 0) { /* 제곱 */
        emit(cg, "pow(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        if (n->child_count > 2) gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }

    /* ── AI / 수학 내장 함수 (v3.7.1) ──────────────────────── */

    /* 평균제곱오차(예측, 실제) → kc_mse(arr1, arr2) */
    if (strcmp(name, "\xED\x8F\x89\xEA\xB7\xA0\xEC\xA0\x9C\xEA\xB3\xB1\xEC\x98\xA4\xEC\xB0\xA8") == 0) { /* 평균제곱오차 */
        emit(cg, "kc_mse(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 교차엔트로피(P_star, P) → kc_cross_entropy(arr1, arr2) */
    if (strcmp(name, "\xEA\xB5\x90\xEC\xB0\xA8\xEC\x97\x94\xED\x8A\xB8\xEB\xA1\x9C\xED\x94\xBC") == 0) { /* 교차엔트로피 */
        emit(cg, "kc_cross_entropy(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 소프트맥스(배열) → kc_softmax(arr) */
    if (strcmp(name, "\xEC\x86\x8C\xED\x94\x84\xED\x8A\xB8\xEB\xA7\xA5\xEC\x8A\xA4") == 0) { /* 소프트맥스 */
        emit(cg, "kc_softmax(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 위치인코딩(위치, 차원수) → kc_positional_encoding(pos, d) */
    if (strcmp(name, "\xEC\x9C\x84\xEC\xB9\x98\xEC\x9D\xB8\xEC\xBD\x94\xEB\x94\xA9") == 0) { /* 위치인코딩 */
        emit(cg, "kc_positional_encoding(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 등비수열합(a, r) → kc_geom_series(a, r) */
    if (strcmp(name, "\xEB\x93\xB1\xEB\xB9\x84\xEC\x88\x98\xEC\x97\xB4\xED\x95\xA9") == 0) { /* 등비수열합 */
        emit(cg, "kc_geom_series(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 등차수열합(a, d, n) → kc_arith_series(a, d, n) */
    if (strcmp(name, "\xEB\x93\xB1\xEC\xB0\xA8\xEC\x88\x98\xEC\x97\xB4\xED\x95\xA9") == 0) { /* 등차수열합 */
        emit(cg, "kc_arith_series(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]); emit(cg, ", ");
        gen_expr(cg, n->children[3]);
        emit(cg, ")");
        return 1;
    }
    /* 점화식값(a1, r, n) → kc_recur_geom(a1, r, n) */
    if (strcmp(name, "\xEC\xA0\x90\xED\x99\x94\xEC\x8B\x9D\xEA\xB0\x92") == 0) { /* 점화식값 */
        emit(cg, "kc_recur_geom(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]); emit(cg, ", ");
        gen_expr(cg, n->children[3]);
        emit(cg, ")");
        return 1;
    }
    /* 형변환 */
    if (strcmp(name, "\xEC\xa0\x95\xEC\x88\x98") == 0) { /* 정수 */
        emit(cg, "(int64_t)("); gen_expr(cg, n->children[1]); emit(cg, ")");
        return 1;
    }
    if (strcmp(name, "\xEC\x8B\xA4\xEC\x88\x98") == 0) { /* 실수 */
        emit(cg, "(double)("); gen_expr(cg, n->children[1]); emit(cg, ")");
        return 1;
    }
    if (strcmp(name, "\xEB\xAC\xB8\xEC\x9E\x90") == 0) { /* 글자 */
        emit(cg, "kc_to_string("); gen_expr(cg, n->children[1]); emit(cg, ")");
        return 1;
    }

    /* ── 수학 기초 (v3.7.1) ─────────────────────────────────── */
    if (strcmp(name, "\xEC\x98\xAC\xEB\xA6\xBC") == 0) { /* 올림 */
        emit(cg, "(int64_t)ceil((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xEB\x82\xB4\xEB\xA6\xBC") == 0) { /* 내림 */
        emit(cg, "(int64_t)floor((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xEB\xB0\x98\xEC\x98\xAC\xEB\xA6\xBC") == 0) { /* 반올림(값, 자릿수=0) */
        if (n->child_count >= 3) {
            /* 자릿수 있음: round(v * 10^d) / 10^d */
            emit(cg, "kc_round_digits(");
            gen_expr(cg, n->children[1]); emit(cg, ", ");
            gen_expr(cg, n->children[2]);
            emit(cg, ")");
        } else {
            emit(cg, "(int64_t)round((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        }
        return 1;
    }
    if (strcmp(name, "\xEC\x82\xAC\xEC\x9D\xB8") == 0) { /* 사인 */
        emit(cg, "sin((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xEC\xBD\x94\xEC\x82\xAC\xEC\x9D\xB8") == 0) { /* 코사인 */
        emit(cg, "cos((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xED\x83\x84\xEC\xA0\xA0\xED\x8A\xB8") == 0) { /* 탄젠트 */
        emit(cg, "tan((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xEC\x9E\x90\xEC\x97\xB0\xEB\xA1\x9C\xEA\xB7\xB8") == 0) { /* 자연로그 */
        emit(cg, "log((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }
    if (strcmp(name, "\xEB\xA1\x9C\xEA\xB7\xB8") == 0) { /* 로그(밑, 값) */
        emit(cg, "kc_log_base(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    if (strcmp(name, "\xEC\xA7\x80\xEC\x88\x98") == 0) { /* 지수 */
        emit(cg, "exp((double)("); gen_expr(cg, n->children[1]); emit(cg, "))");
        return 1;
    }

    /* ── 글자 함수 21종 (v9.6.0) ──────────────────────────────
     *  모든 함수는 kc_runtime.h 에 선언된 kc_str_* 헬퍼를 호출.
     *  인수 수:
     *    1인수 : args[1]
     *    2인수 : args[1], args[2]
     *    3인수 : args[1], args[2], args[3]
     *  가변 인수(포맷): args[1..] 전체 전달
     * ============================================================ */

    /* ── 1단계: 기본 글자 조작 ── */

    /* 자르기(글자, 시작, 끝) → kc_str_sub(s, start, end) */
    if (strcmp(name, "\xEC\x9E\x90\xEB\xA5\xB4\xEA\xB8\xB0") == 0) { /* 자르기 */
        emit(cg, "kc_str_sub(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]); emit(cg, ", ");
        gen_expr(cg, n->children[3]);
        emit(cg, ")");
        return 1;
    }
    /* 분할(글자, 구분자) → kc_str_split(s, delim) */
    if (strcmp(name, "\xEB\xB6\x84\xED\x95\xA0") == 0) { /* 분할 */
        emit(cg, "kc_str_split(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 합치기(배열, 구분자) → kc_str_join(arr, delim) */
    if (strcmp(name, "\xED\x95\xA9\xEC\xB9\x98\xEA\xB8\xB0") == 0) { /* 합치기 */
        emit(cg, "kc_str_join(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 반복글자(글자, 횟수) → kc_str_repeat(s, n) */
    if (strcmp(name, "\xEB\xB0\x98\xEB\xB3\xB5\xEA\xB8\x80\xEC\x9E\x90") == 0) { /* 반복글자 */
        emit(cg, "kc_str_repeat(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 역순(글자) → kc_str_reverse(s) */
    if (strcmp(name, "\xEC\x97\xAD\xEC\x88\x9C") == 0) { /* 역순 */
        emit(cg, "kc_str_reverse(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── 2단계: 문자열 검색/비교 ── */

    /* 포함(글자, 찾을글자) → kc_str_contains(s, sub) */
    if (strcmp(name, "\xED\x8F\xAC\xED\x95\xA8") == 0) { /* 포함 */
        emit(cg, "kc_str_contains(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 위치(글자, 찾을글자) → kc_str_indexof(s, sub) */
    if (strcmp(name, "\xEC\x9C\x84\xEC\xB9\x98") == 0) { /* 위치 */
        emit(cg, "kc_str_indexof(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 시작(글자, 접두어) → kc_str_startswith(s, prefix) */
    if (strcmp(name, "\xEC\x8B\x9C\xEC\x9E\x91") == 0) { /* 시작 */
        emit(cg, "kc_str_startswith(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 끝확인(글자, 접미어) → kc_str_endswith(s, suffix) */
    if (strcmp(name, "\xEB\x81\x9D\xED\x99\x95\xEC\x9D\xB8") == 0) { /* 끝확인 */
        emit(cg, "kc_str_endswith(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 비교(글자1, 글자2) → kc_str_compare(s1, s2) */
    if (strcmp(name, "\xEB\xB9\x84\xEA\xB5\x90") == 0) { /* 비교 */
        emit(cg, "kc_str_compare(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }

    /* ── 3단계: 글자 변환 ── */

    /* 대문자(글자) → kc_str_upper(s) */
    if (strcmp(name, "\xEB\x8C\x80\xEB\xAC\xB8\xEC\x9E\x90") == 0) { /* 대문자 */
        emit(cg, "kc_str_upper(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 소문자(글자) → kc_str_lower(s) */
    if (strcmp(name, "\xEC\x86\x8C\xEB\xAC\xB8\xEC\x9E\x90") == 0) { /* 소문자 */
        emit(cg, "kc_str_lower(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 제목식(글자) → kc_str_title(s) */
    if (strcmp(name, "\xEC\xA0\x9C\xEB\xAA\xA9\xEC\x8B\x9D") == 0) { /* 제목식 */
        emit(cg, "kc_str_title(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 대체(글자, 찾을글자, 바꿀글자) → kc_str_replace(s, from, to) */
    if (strcmp(name, "\xEB\x8C\x80\xEC\xB2\xB4") == 0) { /* 대체 */
        emit(cg, "kc_str_replace(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]); emit(cg, ", ");
        gen_expr(cg, n->children[3]);
        emit(cg, ")");
        return 1;
    }
    /* 한번대체(글자, 찾을글자, 바꿀글자) → kc_str_replace_once(s, from, to) */
    if (strcmp(name, "\xED\x95\x9C\xEB\xB2\x88\xEB\x8C\x80\xEC\xB2\xB4") == 0) { /* 한번대체 */
        emit(cg, "kc_str_replace_once(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]); emit(cg, ", ");
        gen_expr(cg, n->children[3]);
        emit(cg, ")");
        return 1;
    }

    /* ── 4단계: 공백 및 정제 ── */

    /* 앞공백제거(글자) → kc_str_ltrim(s) */
    if (strcmp(name, "\xEC\x95\x9E\xEA\xB3\xB5\xEB\xB0\xB1\xEC\xA0\x9C\xEA\xB1\xB0") == 0) { /* 앞공백제거 */
        emit(cg, "kc_str_ltrim(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 뒤공백제거(글자) → kc_str_rtrim(s) */
    if (strcmp(name, "\xEB\x92\xA4\xEA\xB3\xB5\xEB\xB0\xB1\xEC\xA0\x9C\xEA\xB1\xB0") == 0) { /* 뒤공백제거 */
        emit(cg, "kc_str_rtrim(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 공백제거(글자) → kc_str_trim(s) */
    if (strcmp(name, "\xEA\xB3\xB5\xEB\xB0\xB1\xEC\xA0\x9C\xEA\xB1\xB0") == 0) { /* 공백제거 */
        emit(cg, "kc_str_trim(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── 5단계: 고급 기능 ── */

    /* 반복확인(글자, 패턴) → kc_str_regex(s, pattern) — 정규식 */
    if (strcmp(name, "\xEB\xB0\x98\xEB\xB3\xB5\xED\x99\x95\xEC\x9D\xB8") == 0) { /* 반복확인 */
        emit(cg, "kc_str_regex(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 분석(글자) → kc_str_parse(s) — 숫자/논리 등 자동 변환 */
    if (strcmp(name, "\xEB\xB6\x84\xEC\x84\x9D") == 0) { /* 분석 */
        emit(cg, "kc_str_parse(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 포맷(형식, 인수...) → kc_str_format(fmt, ...) */
    if (strcmp(name, "\xED\x8F\xAC\xEB\xA7\xB7") == 0) { /* 포맷 */
        emit(cg, "kc_str_format(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ")");
        return 1;
    }

    /* ── 글자(문자열) 형변환 (v9.6.0 누락) ── */
    /* 글자(값) → kc_to_string(val) */
    if (strcmp(name, "\xEA\xB8\x80\xEC\x9E\x90") == 0) { /* 글자 */
        emit(cg, "kc_to_string(");
        if (n->child_count > 1) gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── 배열 조작 ── */
    /* 추가(배열, 값) → kc_array_push(arr, val) */
    if (strcmp(name, "\xEC\xB6\x94\xEA\xB0\x80") == 0) { /* 추가 */
        emit(cg, "kc_array_push(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }

    /* ── 통계 함수 13종 (v3.8.0) ── */
    /* 합계(배열) → kc_stat_sum(arr) */
    if (strcmp(name, "\xED\x95\xA9\xEA\xB3\x84") == 0) { /* 합계 */
        emit(cg, "kc_stat_sum(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 평균(배열) → kc_stat_mean(arr) */
    if (strcmp(name, "\xED\x8F\x89\xEA\xB7\xA0") == 0) { /* 평균 */
        emit(cg, "kc_stat_mean(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 분산(배열) → kc_stat_variance(arr) */
    if (strcmp(name, "\xEB\xB6\x84\xEC\x82\xB0") == 0) { /* 분산 */
        emit(cg, "kc_stat_variance(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 표준편차(배열) → kc_stat_stddev(arr) */
    if (strcmp(name, "\xED\x91\x9C\xEC\xA4\x80\xED\x8E\xB8\xEC\xB0\xA8") == 0) { /* 표준편차 */
        emit(cg, "kc_stat_stddev(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 중앙값(배열) → kc_stat_median(arr) */
    if (strcmp(name, "\xEC\xA4\x91\xEC\x95\x99\xEA\xB0\x92") == 0) { /* 중앙값 */
        emit(cg, "kc_stat_median(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 최빈값(배열) → kc_stat_mode(arr) */
    if (strcmp(name, "\xEC\xB5\x9C\xEB\xB9\x88\xEA\xB0\x92") == 0) { /* 최빈값 */
        emit(cg, "kc_stat_mode(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 누적합(배열) → kc_stat_cumsum(arr) */
    if (strcmp(name, "\xEB\x88\x84\xEC\xA0\x81\xED\x95\xA9") == 0) { /* 누적합 */
        emit(cg, "kc_stat_cumsum(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 공분산(배열1, 배열2) → kc_stat_covariance(a, b) */
    if (strcmp(name, "\xEA\xB3\xB5\xEB\xB6\x84\xEC\x82\xB0") == 0) { /* 공분산 */
        emit(cg, "kc_stat_covariance(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 상관계수(배열1, 배열2) → kc_stat_correlation(a, b) */
    if (strcmp(name, "\xEC\x83\x81\xEA\xB4\x80\xEA\xB3\x84\xEC\x88\x98") == 0) { /* 상관계수 */
        emit(cg, "kc_stat_correlation(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 정규화(배열) → kc_stat_normalize(arr) */
    if (strcmp(name, "\xEC\xA0\x95\xEA\xB7\x9C\xED\x99\x94") == 0) { /* 정규화 */
        emit(cg, "kc_stat_normalize(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 표준화(배열) → kc_stat_standardize(arr) */
    if (strcmp(name, "\xED\x91\x9C\xEC\xA4\x80\xED\x99\x94") == 0) { /* 표준화 */
        emit(cg, "kc_stat_standardize(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 배열정렬(배열) → kc_arr_sort(arr) */
    if (strcmp(name, "\xEB\xB0\xB0\xEC\x97\xB4\xEC\xA0\x95\xEB\xA0\xAC") == 0) { /* 배열정렬 */
        emit(cg, "kc_arr_sort(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 배열뒤집기(배열) → kc_arr_reverse(arr) */
    if (strcmp(name, "\xEB\xB0\xB0\xEC\x97\xB4\xEB\x92\xA4\xEC\xA7\x91\xEA\xB8\xB0") == 0) { /* 배열뒤집기 */
        emit(cg, "kc_arr_reverse(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── AI 활성함수 3종 (v3.8.0) ── */
    /* 시그모이드(값) → kc_sigmoid(x) */
    if (strcmp(name, "\xEC\x8B\x9C\xEA\xB7\xB8\xEB\xAA\xA8\xEC\x9D\xB4\xEB\x93\x9C") == 0) { /* 시그모이드 */
        emit(cg, "kc_sigmoid(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 렐루(값) → kc_relu(x) */
    if (strcmp(name, "\xEB\xA0\xB0\xEB\xA3\xA8") == 0) { /* 렐루 */
        emit(cg, "kc_relu(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 쌍곡탄젠트(값) → kc_tanh_fn(x) */
    if (strcmp(name, "\xEC\x8C\x8D\xEA\xB3\xA1\xED\x83\x84\xEC\xA0\xA0\xED\x8A\xB8") == 0) { /* 쌍곡탄젠트 */
        emit(cg, "kc_tanh_fn(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }

    /* ── 관계 심리 함수 (v3.8.0) ── */
    /* 호감도(배열1, 배열2) → kc_attraction(a, b) */
    if (strcmp(name, "\xED\x98\xB8\xEA\xB0\x90\xEB\x8F\x84") == 0) { /* 호감도 */
        emit(cg, "kc_attraction(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }

    /* ── 파일 내장 함수 17종 (v5.0.0) ── */
    /* 파일열기(경로, 모드) → kc_file_open(path, mode) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x97\xB4\xEA\xB8\xB0") == 0) { /* 파일열기 */
        emit(cg, "kc_file_open(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 파일닫기(핸들) → kc_file_close(f) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEB\x8B\xAB\xEA\xB8\xB0") == 0) { /* 파일닫기 */
        emit(cg, "kc_file_close(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일읽기(핸들) → kc_file_read(f) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xBD\xEA\xB8\xB0") == 0) { /* 파일읽기 */
        emit(cg, "kc_file_read(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일줄읽기(핸들) → kc_file_readline(f) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA4\x84\xEC\x9D\xBD\xEA\xB8\xB0") == 0) { /* 파일줄읽기 */
        emit(cg, "kc_file_readline(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일쓰기(핸들, 내용) → kc_file_write(f, s) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x93\xB0\xEA\xB8\xB0") == 0) { /* 파일쓰기 */
        emit(cg, "kc_file_write(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 파일줄쓰기(핸들, 내용) → kc_file_writeline(f, s) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA4\x84\xEC\x93\xB0\xEA\xB8\xB0") == 0) { /* 파일줄쓰기 */
        emit(cg, "kc_file_writeline(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 파일있음(경로) → kc_file_exists(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9E\x88\xEC\x9D\x8C") == 0) { /* 파일있음 */
        emit(cg, "kc_file_exists(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일크기(경로) → kc_file_size(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xED\x81\xAC\xEA\xB8\xB0") == 0) { /* 파일크기 */
        emit(cg, "kc_file_size(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일목록(경로) → kc_file_list(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEB\xAA\xA9\xEB\xA1\x9D") == 0) { /* 파일목록 */
        emit(cg, "kc_file_list(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일이름(경로) → kc_file_name(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xB4\xEB\xA6\x84") == 0) { /* 파일이름 */
        emit(cg, "kc_file_name(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일확장자(경로) → kc_file_ext(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xED\x99\x95\xEC\x9E\xA5\xEC\x9E\x90") == 0) { /* 파일확장자 */
        emit(cg, "kc_file_ext(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 폴더만들기(경로) → kc_dir_make(path) */
    if (strcmp(name, "\xED\x8F\xB4\xEB\x8D\x94\xEB\xA7\x8C\xEB\x93\xA4\xEA\xB8\xB0") == 0) { /* 폴더만들기 */
        emit(cg, "kc_dir_make(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일지우기(경로) → kc_file_delete(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA7\x80\xEC\x9A\xB0\xEA\xB8\xB0") == 0) { /* 파일지우기 */
        emit(cg, "kc_file_delete(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일복사(원본, 대상) → kc_file_copy(src, dst) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEB\xB3\xB5\xEC\x82\xAC") == 0) { /* 파일복사 */
        emit(cg, "kc_file_copy(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 파일이동(원본, 대상) → kc_file_move(src, dst) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xB4\xEB\x8F\x99") == 0) { /* 파일이동 */
        emit(cg, "kc_file_move(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }
    /* 파일전체읽기(경로) → kc_file_readall(path) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA0\x84\xEC\xB2\xB4\xEC\x9D\xBD\xEA\xB8\xB0") == 0) { /* 파일전체읽기 */
        emit(cg, "kc_file_readall(");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        return 1;
    }
    /* 파일전체쓰기(경로, 내용) → kc_file_writeall(path, s) */
    if (strcmp(name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA0\x84\xEC\xB2\xB4\xEC\x93\xB0\xEA\xB8\xB0") == 0) { /* 파일전체쓰기 */
        emit(cg, "kc_file_writeall(");
        gen_expr(cg, n->children[1]); emit(cg, ", ");
        gen_expr(cg, n->children[2]);
        emit(cg, ")");
        return 1;
    }

    /* v11.0.0 텐서 내장 함수 12종 */
    if (strcmp(name, "모양바꾸기") == 0) { /* 모양바꾸기 */
        emit(cg, "kc_tensor_reshape("); gen_expr(cg, n->children[1]); emit(cg, ", "); gen_expr(cg, n->children[2]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "전치") == 0) { /* 전치 */
        emit(cg, "kc_tensor_transpose("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "펼치기") == 0) { /* 펼치기 */
        emit(cg, "kc_tensor_flatten("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "텐서더하기") == 0) { /* 텐서더하기 */
        emit(cg, "kc_tensor_add("); gen_expr(cg, n->children[1]); emit(cg, ", "); gen_expr(cg, n->children[2]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "텐서빼기") == 0) { /* 텐서빼기 */
        emit(cg, "kc_tensor_sub("); gen_expr(cg, n->children[1]); emit(cg, ", "); gen_expr(cg, n->children[2]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "텐서곱") == 0) { /* 텐서곱 */
        emit(cg, "kc_tensor_mul("); gen_expr(cg, n->children[1]); emit(cg, ", "); gen_expr(cg, n->children[2]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "텐서나눠기") == 0) { /* 텐서나눠기 */
        emit(cg, "kc_tensor_div("); gen_expr(cg, n->children[1]); emit(cg, ", "); gen_expr(cg, n->children[2]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "행렬곱") == 0) { /* 행렬곱 */
        emit(cg, "kc_tensor_matmul("); gen_expr(cg, n->children[1]); emit(cg, ", "); gen_expr(cg, n->children[2]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "합산축") == 0) { /* 합산축 */
        emit(cg, "kc_tensor_sum_axis("); gen_expr(cg, n->children[1]);
        if (n->child_count > 2) { emit(cg, ", "); gen_expr(cg, n->children[2]); } else { emit(cg, ", -1"); }
        emit(cg, ")"); return 1;
    }
    if (strcmp(name, "평균축") == 0) { /* 평균축 */
        emit(cg, "kc_tensor_mean_axis("); gen_expr(cg, n->children[1]);
        if (n->child_count > 2) { emit(cg, ", "); gen_expr(cg, n->children[2]); } else { emit(cg, ", -1"); }
        emit(cg, ")"); return 1;
    }
    if (strcmp(name, "텐서출력") == 0) { /* 텐서출력 */
        emit(cg, "kc_tensor_print("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "텐서형태") == 0) { /* 텐서형태 */
        emit(cg, "kc_tensor_shape_arr("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    /* ── autograd 내장함수 13종 (v15.0.0) ── */
    if (strcmp(name, "역전파") == 0) {
        emit(cg, "kc_autograd_backward("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "기울기초기화") == 0) {
        emit(cg, "kc_autograd_zero_grad("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "미분추적") == 0) {
        /* (t)->requires_grad = 1; kc_tensor_ensure_grad(t) */
        emit(cg, "("); gen_expr(cg, n->children[1]); emit(cg, ")->requires_grad = 1");
        emit(cg, ", kc_tensor_ensure_grad("); gen_expr(cg, n->children[1]); emit(cg, "), (void*)0"); return 1;
    }
    if (strcmp(name, "미분더하기") == 0) {
        emit(cg, "kc_ag_add("); gen_expr(cg, n->children[1]); emit(cg, ", "); gen_expr(cg, n->children[2]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "미분곱") == 0) {
        emit(cg, "kc_ag_mul("); gen_expr(cg, n->children[1]); emit(cg, ", "); gen_expr(cg, n->children[2]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "미분행렬곱") == 0) {
        emit(cg, "kc_ag_matmul("); gen_expr(cg, n->children[1]); emit(cg, ", "); gen_expr(cg, n->children[2]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "미분렐루") == 0) {
        emit(cg, "kc_ag_relu("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "미분시그모이드") == 0) {
        emit(cg, "kc_ag_sigmoid("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "미분쌍곡탄젠트") == 0) {
        emit(cg, "kc_ag_tanh_op("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "미분로그") == 0) {
        emit(cg, "kc_ag_log("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "미분합산") == 0) {
        emit(cg, "kc_ag_sum("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "미분평균") == 0) {
        emit(cg, "kc_ag_mean("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }
    if (strcmp(name, "미분제곱") == 0) {
        emit(cg, "kc_ag_pow2("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1;
    }

/* ── 산업/임베디드/안전/AI 내장 함수 (v16~v18) ──────────────────── */
#define GEN_INDUS0(KW, SYM) \
    if (strcmp(name, KW) == 0) { emit(cg, SYM "()"); return 1; }
#define GEN_INDUS1(KW, SYM) \
    if (strcmp(name, KW) == 0 && n->child_count >= 2) { \
        emit(cg, SYM "("); gen_expr(cg, n->children[1]); emit(cg, ")"); return 1; }
#define GEN_INDUS2(KW, SYM) \
    if (strcmp(name, KW) == 0 && n->child_count >= 3) { \
        emit(cg, SYM "("); gen_expr(cg, n->children[1]); emit(cg, ", "); \
        gen_expr(cg, n->children[2]); emit(cg, ")"); return 1; }

    GEN_INDUS2("GPIO쓰기",       "kc_gpio_write")
    GEN_INDUS1("GPIO읽기",       "kc_gpio_read")
    GEN_INDUS1("I2C연결",        "kc_i2c_connect")
    GEN_INDUS1("I2C읽기",        "kc_i2c_read")
    GEN_INDUS2("I2C쓰기",        "kc_i2c_write")
    GEN_INDUS1("SPI전송",        "kc_spi_send")
    GEN_INDUS0("SPI읽기",        "kc_spi_read")
    GEN_INDUS1("UART설정",       "kc_uart_setup")
    GEN_INDUS1("UART전송",       "kc_uart_send")
    GEN_INDUS0("UART읽기",       "kc_uart_read")
    GEN_INDUS2("Modbus연결",     "kc_modbus_connect")
    GEN_INDUS1("Modbus읽기",     "kc_modbus_read")
    GEN_INDUS2("Modbus쓰기",     "kc_modbus_write")
    GEN_INDUS0("Modbus연결끊기", "kc_modbus_disconnect")
    GEN_INDUS2("CAN필터",        "kc_can_filter")
    GEN_INDUS2("CAN전송",        "kc_can_send")
    GEN_INDUS0("CAN읽기",        "kc_can_read")
    GEN_INDUS2("MQTT연결",       "kc_mqtt_connect")
    GEN_INDUS2("MQTT발행",       "kc_mqtt_publish")
    GEN_INDUS1("MQTT구독",       "kc_mqtt_subscribe")
    GEN_INDUS0("MQTT연결끊기",   "kc_mqtt_disconnect")
    GEN_INDUS2("ROS2발행",       "kc_ros2_publish")
    GEN_INDUS1("ROS2구독",       "kc_ros2_subscribe")
    /* 안전 규격 v17.0.0 */
    GEN_INDUS0("페일세이프",     "kc_failsafe")
    GEN_INDUS0("긴급정지",       "kc_emergency_stop")
    GEN_INDUS1("경보발령",       "kc_alarm")
    /* 온디바이스 AI v18.0.0 */
    GEN_INDUS1("AI불러오기",     "kc_ai_load")
    GEN_INDUS1("AI추론",         "kc_ai_predict")
    GEN_INDUS1("AI학습단계",     "kc_ai_train_step")
    GEN_INDUS2("AI저장",         "kc_ai_save")

    /* ── v18.1.0 신규 내장 함수 C코드 생성 ── */
    /* 수학 추가 */
    if (strcmp(name, "제곱") == 0 && n->child_count >= 3) {
        emit(cg, "pow("); gen_expr(cg, n->children[1]);
        emit(cg, ", ");   gen_expr(cg, n->children[2]); emit(cg, ")"); return 1; }
    if (strcmp(name, "라디안") == 0 && n->child_count >= 2) {
        emit(cg, "("); gen_expr(cg, n->children[1]); emit(cg, " * 3.14159265358979323846 / 180.0)"); return 1; }
    if (strcmp(name, "각도") == 0 && n->child_count >= 2) {
        emit(cg, "("); gen_expr(cg, n->children[1]); emit(cg, " * 180.0 / 3.14159265358979323846)"); return 1; }
    if (strcmp(name, "난수") == 0) {
        emit(cg, "((double)rand() / ((double)RAND_MAX + 1.0))"); return 1; }
    if (strcmp(name, "난정수") == 0 && n->child_count >= 3) {
        emit(cg, "kc_rand_int("); gen_expr(cg, n->children[1]);
        emit(cg, ", "); gen_expr(cg, n->children[2]); emit(cg, ")"); return 1; }
    /* 역삼각함수 */
    GEN_INDUS1("아크사인",   "asin")
    GEN_INDUS1("아크코사인", "acos")
    GEN_INDUS1("아크탄젠트", "atan")
    GEN_INDUS2("아크탄젠트2","atan2")
    /* 글자 추가 */
    GEN_INDUS2("좌문자",  "kc_str_left")
    GEN_INDUS2("우문자",  "kc_str_right")
    if (strcmp(name, "채우기") == 0 && n->child_count >= 3) {
        emit(cg, "kc_str_pad("); gen_expr(cg, n->children[1]);
        emit(cg, ", "); gen_expr(cg, n->children[2]);
        if (n->child_count >= 4) { emit(cg, ", "); gen_expr(cg, n->children[3]); }
        else emit(cg, ", \" \"");
        emit(cg, ")"); return 1; }
    GEN_INDUS1("코드",    "kc_str_code")
    GEN_INDUS1("붙여씀",  "kc_str_compact")
    /* 배열 고급 */
    GEN_INDUS2("배열삭제",   "kc_arr_remove")
    GEN_INDUS2("배열찾기",   "kc_arr_indexof")
    GEN_INDUS2("배열포함",   "kc_arr_contains")
    GEN_INDUS2("배열합치기", "kc_arr_concat")
    if (strcmp(name, "배열자르기") == 0 && n->child_count >= 3) {
        emit(cg, "kc_arr_slice("); gen_expr(cg, n->children[1]);
        emit(cg, ", "); gen_expr(cg, n->children[2]);
        if (n->child_count >= 4) { emit(cg, ", "); gen_expr(cg, n->children[3]); }
        else emit(cg, ", -1");
        emit(cg, ")"); return 1; }
    GEN_INDUS1("유일값",     "kc_arr_unique")
    GEN_INDUS2("배열채우기", "kc_arr_fill")
    /* 시간/날짜 */
    if (strcmp(name, "현재시간") == 0) { emit(cg, "(int64_t)time(NULL)"); return 1; }
    if (strcmp(name, "현재날짜") == 0) { emit(cg, "kc_date_now()");       return 1; }
    GEN_INDUS2("시간포맷",  "kc_time_format")
    if (strcmp(name, "경과시간") == 0) {
        emit(cg, "((double)clock() / (double)CLOCKS_PER_SEC)"); return 1; }
    /* 시스템 */
    GEN_INDUS1("환경변수",  "getenv")
    GEN_INDUS1("종료",      "exit")
    GEN_INDUS1("명령실행",  "system")
    GEN_INDUS1("잠깐",      "kc_sleep_ms")
    /* JSON */
    GEN_INDUS1("JSON생성",  "kc_json_encode")
    GEN_INDUS1("JSON파싱",  "kc_json_decode")

#undef GEN_INDUS0
#undef GEN_INDUS1
#undef GEN_INDUS2

    return 0; /* 내장 함수 아님 */
}

/* ================================================================
 *  표현식 생성
 * ================================================================ */
static void gen_expr(Codegen *cg, Node *n) {
    if (!n) { emit(cg, "0"); return; }

    sourcemap_add(cg, n->line, n->col);

    switch (n->type) {

    case NODE_INT_LIT:
        emit(cg, "%lldLL", (long long)n->val.ival);
        break;

    case NODE_FLOAT_LIT:
        emit(cg, "%g", n->val.fval);
        break;

    case NODE_STRING_LIT:
        escape_string(cg, n->sval ? n->sval : "");
        break;

    case NODE_CHAR_LIT:
        emit(cg, "0x%X", (unsigned)n->val.ival);
        break;

    case NODE_BOOL_LIT:
        emit(cg, "%s", n->val.bval ? "1" : "0");
        break;

    case NODE_NULL_LIT:
        emit(cg, "NULL");
        break;

    case NODE_IDENT:
        /* 식별자는 그대로 출력 (한글 → C는 변수명 맹글링 필요없음,
           kc_mangle로 ASCII 안전 이름으로 변환) */
        emit(cg, "kc_%s", n->sval ? n->sval : "_unknown");
        break;

    case NODE_BINARY:
        /* 거듭제곱은 pow() 사용 */
        if (n->op == TOK_STARSTAR) {
            emit(cg, "pow((double)(");
            gen_expr(cg, n->children[0]);
            emit(cg, "), (double)(");
            gen_expr(cg, n->children[1]);
            emit(cg, "))");
        } else {
            emit(cg, "(");
            gen_expr(cg, n->children[0]);
            emit(cg, " %s ", op_to_c(n->op));
            gen_expr(cg, n->children[1]);
            emit(cg, ")");
        }
        break;

    case NODE_UNARY:
        emit(cg, "(%s", op_to_c(n->op));
        gen_expr(cg, n->children[0]);
        emit(cg, ")");
        break;

    case NODE_ASSIGN:
        gen_expr(cg, n->children[0]);
        emit(cg, " %s ", op_to_c(n->op));
        gen_expr(cg, n->children[1]);
        break;

    case NODE_CALL: {
        /* 내장 함수 먼저 확인 */
        if (gen_builtin_call(cg, n)) break;
        /* 일반 함수 호출 */
        gen_expr(cg, n->children[0]);
        emit(cg, "(");
        for (int i = 1; i < n->child_count; i++) {
            if (i > 1) emit(cg, ", ");
            gen_expr(cg, n->children[i]);
        }
        emit(cg, ")");
        break;
    }

    case NODE_INDEX:
        emit(cg, "kc_arr_get(");
        gen_expr(cg, n->children[0]);
        emit(cg, ", ");
        gen_expr(cg, n->children[1]);
        emit(cg, ")");
        break;

    case NODE_MEMBER: {
        /* 배열.길이 → kc_arr_len(arr) */
        const char *mname = n->sval ? n->sval : "";
        /* 길이 */
        if (strcmp(mname, "\xEA\xB8\xB8\xEC\x9D\xB4") == 0) {
            emit(cg, "kc_arr_len(");
            gen_expr(cg, n->children[0]);
            emit(cg, ")");
        } else {
            gen_expr(cg, n->children[0]);
            emit(cg, ".%s", mname);
        }
        break;
    }

    /* v11.0.0 텐서 리터럴 C ϴ� 생성 */
    case NODE_TENSOR_LIT: {
        /* kc_tensor_create(data, ndim, d0, d1, ...) 호출 혹은 영/일/무작위 */
        if (n->op == TOK_KW_TENSOR) {
            /* 텐서(data, shape) — shape는 배열 리터럴 */
            emit(cg, "kc_tensor_from_array(");
            gen_expr(cg, n->children[0]); /* data */
            emit(cg, ", ");
            gen_expr(cg, n->children[1]); /* shape */
            emit(cg, ")");
        } else if (n->op == TOK_KW_ZERO_TENSOR) {
            emit(cg, "kc_tensor_zeros(");
            gen_expr(cg, n->children[0]);
            emit(cg, ")");
        } else if (n->op == TOK_KW_ONE_TENSOR) {
            emit(cg, "kc_tensor_ones(");
            gen_expr(cg, n->children[0]);
            emit(cg, ")");
        } else { /* TOK_KW_RAND_TENSOR */
            emit(cg, "kc_tensor_rand(");
            gen_expr(cg, n->children[0]);
            emit(cg, ")");
        }
        break;
    }

    case NODE_ARRAY_LIT: {
        emit(cg, "kc_arr_literal(%d", n->child_count);
        for (int i = 0; i < n->child_count; i++) {
            emit(cg, ", ");
            /* 배열 요소를 kc_value_t로 래핑 */
            emit(cg, "KC_VAL(");
            gen_expr(cg, n->children[i]);
            emit(cg, ")");
        }
        emit(cg, ")");
        break;
    }

    case NODE_LAMBDA: {
        /* 람다 → 미리 생성된 정적 함수 이름 참조 */
        /* (람다는 1패스에서 전방 선언됨) */
        emit(cg, "kc_lambda_%d", cg->tmp_counter++);
        break;
    }

    default:
        emit(cg, "/* [미지원 표현식: %d] */0", n->type);
        break;
    }
}

/* ================================================================
 *  구문 생성
 * ================================================================ */
static void gen_stmt(Codegen *cg, Node *n) {
    if (!n) return;
    sourcemap_add(cg, n->line, n->col);

    switch (n->type) {

    /* ── 변수 선언 ─────────────────────────────────────── */
    case NODE_VAR_DECL: {
        const char *ctype = c_type(n->dtype);
        emit_indent(cg);
        emit(cg, "%s kc_%s", ctype, n->sval ? n->sval : "_var");
        if (n->child_count > 0) {
            emit(cg, " = ");
            gen_expr(cg, n->children[0]);
        } else {
            /* 기본값 */
            if (strcmp(ctype, "char*") == 0)        emit(cg, " = \"\"");
            else if (strcmp(ctype, "kc_array_t*") == 0) emit(cg, " = kc_arr_new()");
            else                                     emit(cg, " = 0");
        }
        emit(cg, ";\n");
        cg->c_line++;
        cg->result->var_count++;
        break;
    }

    case NODE_CONST_DECL: {
        emit_indent(cg);
        emit(cg, "const %s kc_%s = ", c_type(n->dtype), n->sval ? n->sval : "_const");
        if (n->child_count > 0) gen_expr(cg, n->children[0]);
        else emit(cg, "0");
        emit(cg, ";\n");
        cg->c_line++;
        break;
    }

    /* ── 표현식 구문 ────────────────────────────────────── */
    case NODE_EXPR_STMT:
        emit_indent(cg);
        if (n->child_count > 0) gen_expr(cg, n->children[0]);
        emit(cg, ";\n");
        cg->c_line++;
        break;

    /* ── 만약/아니면 ────────────────────────────────────── */
    case NODE_IF: {
        emit_indent(cg);
        emit(cg, "if (");
        gen_expr(cg, n->children[0]);
        emit(cg, ") {\n");
        cg->c_line++;
        cg->indent++;
        gen_block(cg, n->children[1]);
        cg->indent--;
        emitln(cg, "}");
        /* elif / else */
        if (n->child_count > 2 && n->children[2]) {
            Node *branch = n->children[2];
            while (branch) {
                if (branch->type == NODE_ELIF) {
                    emit_indent(cg);
                    emit(cg, "else if (");
                    gen_expr(cg, branch->children[0]);
                    emit(cg, ") {\n");
                    cg->c_line++;
                    cg->indent++;
                    gen_block(cg, branch->children[1]);
                    cg->indent--;
                    emitln(cg, "}");
                    branch = (branch->child_count > 2) ? branch->children[2] : NULL;
                } else if (branch->type == NODE_ELSE) {
                    emitln(cg, "else {");
                    cg->indent++;
                    gen_block(cg, branch->children[0]);
                    cg->indent--;
                    emitln(cg, "}");
                    branch = NULL;
                } else {
                    branch = NULL;
                }
            }
        }
        break;
    }

    /* ── 동안 ───────────────────────────────────────────── */
    case NODE_WHILE:
        emit_indent(cg);
        emit(cg, "while (");
        gen_expr(cg, n->children[0]);
        emit(cg, ") {\n");
        cg->c_line++;
        cg->indent++;
        gen_block(cg, n->children[1]);
        cg->indent--;
        emitln(cg, "}");
        break;

    /* ── 반복 (부터/까지) ───────────────────────────────── */
    case NODE_FOR_RANGE: {
        const char *var = n->sval ? n->sval : "_i";
        emit_indent(cg);
        emit(cg, "for (int64_t kc_%s = ", var);
        gen_expr(cg, n->children[0]);
        emit(cg, "; kc_%s <= ", var);
        gen_expr(cg, n->children[1]);
        /* step */
        if (n->child_count > 3) {
            emit(cg, "; kc_%s += ", var);
            gen_expr(cg, n->children[2]);
            emit(cg, ") {\n");
            cg->c_line++;
            cg->indent++;
            gen_block(cg, n->children[3]);
        } else {
            emit(cg, "; kc_%s++) {\n", var);
            cg->c_line++;
            cg->indent++;
            gen_block(cg, n->children[2]);
        }
        cg->indent--;
        emitln(cg, "}");
        break;
    }

    /* ── 각각 (foreach) ─────────────────────────────────── */
    case NODE_FOR_EACH: {
        const char *var = n->sval ? n->sval : "_item";
        char idx[32];
        snprintf(idx, sizeof(idx), "_kc_idx%d", cg->tmp_counter++);
        char arr[32];
        snprintf(arr, sizeof(arr), "_kc_arr%d", cg->tmp_counter++);

        emit_indent(cg);
        emit(cg, "{ kc_array_t* %s = (kc_array_t*)(", arr);
        gen_expr(cg, n->children[0]);
        emit(cg, ");\n");
        cg->c_line++;
        emit_indent(cg);
        emit(cg, "for (int64_t %s = 0; %s < kc_arr_len(%s); %s++) {\n",
             idx, idx, arr, idx);
        cg->c_line++;
        cg->indent++;
        emitln(cg, "kc_value_t kc_%s = kc_arr_get(%s, %s);", var, arr, idx);
        gen_block(cg, n->children[1]);
        cg->indent--;
        emitln(cg, "} }");
        break;
    }

    /* ── 선택/경우/그외 ─────────────────────────────────── */
    case NODE_SWITCH:
        emit_indent(cg);
        emit(cg, "switch ((int64_t)(");
        gen_expr(cg, n->children[0]);
        emit(cg, ")) {\n");
        cg->c_line++;
        for (int i = 1; i < n->child_count; i++) {
            Node *c = n->children[i];
            if (c->type == NODE_CASE) {
                emit_indent(cg);
                emit(cg, "case (int64_t)(");
                gen_expr(cg, c->children[0]);
                emit(cg, "): {\n");
                cg->c_line++;
                cg->indent++;
                gen_block(cg, c->children[1]);
                emitln(cg, "break;");
                cg->indent--;
                emitln(cg, "}");
            } else if (c->type == NODE_DEFAULT) {
                emitln(cg, "default: {");
                cg->indent++;
                gen_block(cg, c->children[0]);
                cg->indent--;
                emitln(cg, "}");
            }
        }
        emitln(cg, "}");
        break;

    /* ── 반환 ───────────────────────────────────────────── */
    case NODE_RETURN:
        emit_indent(cg);
        if (n->child_count > 0) {
            emit(cg, "return ");
            gen_expr(cg, n->children[0]);
            emit(cg, ";\n");
        } else {
            emit(cg, "return;\n");
        }
        cg->c_line++;
        break;

    /* ── 멈춤/건너뜀 ────────────────────────────────────── */
    case NODE_BREAK:
        emitln(cg, "break;");
        break;

    case NODE_CONTINUE:
        emitln(cg, "continue;");
        break;

    /* ── 이동 (goto) ────────────────────────────────────── */
    case NODE_GOTO:
        emitln(cg, "goto kc_label_%s;", n->sval ? n->sval : "_unknown");
        break;

    case NODE_LABEL:
        emit(cg, "kc_label_%s:\n", n->sval ? n->sval : "_unknown");
        cg->c_line++;
        break;

    /* ── 시도/실패시/항상 ────────────────────────────────── */
    case NODE_TRY: {
        /* setjmp/longjmp 기반 예외 처리 */
        char jbuf[32];
        snprintf(jbuf, sizeof(jbuf), "_kc_jmp%d", cg->tmp_counter++);
        emitln(cg, "{ jmp_buf %s;", jbuf);
        emitln(cg, "kc_push_jmp(&%s);", jbuf);
        emitln(cg, "if (setjmp(%s) == 0) {", jbuf);
        cg->indent++;
        if (n->child_count > 0) gen_block(cg, n->children[0]);
        cg->indent--;
        emitln(cg, "} else {");
        cg->indent++;
        if (n->child_count > 1) gen_block(cg, n->children[1]);
        cg->indent--;
        emitln(cg, "}");
        emitln(cg, "kc_pop_jmp();");
        /* 항상 블록 */
        if (n->child_count > 2 && n->children[2]) {
            gen_block(cg, n->children[2]);
        }
        emitln(cg, "}");
        break;
    }

    /* ── 오류 raise ─────────────────────────────────────── */
    case NODE_RAISE:
        emit_indent(cg);
        emit(cg, "kc_raise(");
        if (n->child_count > 0) gen_expr(cg, n->children[0]);
        else emit(cg, "\"오류\"");
        emit(cg, ");\n");
        cg->c_line++;
        break;

    /* ── 블록 ───────────────────────────────────────────── */
    case NODE_BLOCK:
        gen_block(cg, n);
        break;

    /* ── 함수/정의 선언 ─────────────────────────────────── */
    case NODE_FUNC_DECL:
    case NODE_VOID_DECL:
        /* 최상위 레벨에서 처리됨 — 여기선 무시 */
        break;

    /* ── 가져오기(가짐) (v6.1.0) ────────────────────────── */
    case NODE_IMPORT: {
        const char *mod = n->sval ? n->sval : "";

        /* 내장 모듈 → 해당 C 헤더 매핑 */
        if (strcmp(mod, "수학") == 0 || strcmp(mod, "수학함수") == 0) {
            emitln(cg, "#include <math.h>   /* 가짐 %s */", mod);
        } else if (strcmp(mod, "파일시스템") == 0) {
            emitln(cg, "#include <stdio.h>  /* 가짐 %s */", mod);
            emitln(cg, "#include <dirent.h>");
            emitln(cg, "#include <sys/stat.h>");
        } else if (strcmp(mod, "문자열") == 0) {
            emitln(cg, "#include <string.h> /* 가짐 %s */", mod);
        } else if (strcmp(mod, "시간") == 0) {
            emitln(cg, "#include <time.h>   /* 가짐 %s */", mod);
        } else if (strcmp(mod, "난수") == 0) {
            emitln(cg, "#include <stdlib.h> /* 가짐 %s */", mod);
        } else {
            /* 외부 파일 모듈 — 확장자 탐색 후 적절한 include 생성 */
            /* .han/.hg → 주석 */
            emitln(cg, "/* 가짐 외부 모듈: %s */", mod);
            emitln(cg, "/* 힌트: %s.han / %s.hg / %s.h 중 존재하는 파일 포함 */",
                   mod, mod, mod);
        }

        /* 로부터(from) 이름 목록이 있으면 주석으로 병기 */
        if (n->child_count > 0) {
            emit(cg, "/* 가져온 이름: ");
            for (int i = 0; i < n->child_count; i++) {
                if (n->children[i] && n->children[i]->sval)
                    emit(cg, "%s%s", n->children[i]->sval,
                         i < n->child_count - 1 ? ", " : "");
            }
            emit(cg, " */\n");
            cg->c_line++;
        }
        break;
    }

    /* ── 전처리기 (v6.1.0) ───────────────────────────────── */
    case NODE_PP_STMT: {
        if (n->op != TOK_PP_INCLUDE || !n->sval) {
            emitln(cg, "/* 전처리기 구문 */");
            break;
        }
        const char *fname = n->sval;
        const char *dot   = strrchr(fname, '.');
        const char *ext   = dot ? dot : "";

        /* .han / .hg → C 코드에서는 해당 .h 가 있다면 포함, 없으면 주석 */
        if (strcmp(ext, ".han") == 0 || strcmp(ext, ".hg") == 0) {
            emitln(cg, "/* #포함 Kcode 모듈: %s */", fname);
        }
        /* .c / .h → 직접 #include */
        else if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) {
            emitln(cg, "#include \"%s\"", fname);
            cg->result->var_count++; /* include 카운트 재활용 */
        }
        /* .cpp / .hpp → extern "C" 래핑 #include */
        else if (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0) {
            emitln(cg, "#ifdef __cplusplus");
            emitln(cg, "#include \"%s\"", fname);
            emitln(cg, "#else");
            emitln(cg, "/* C++ 헤더 '%s' — C++ 모드에서만 포함 가능 */", fname);
            emitln(cg, "#endif");
        }
        /* .py → Python.h 기반 임베딩 힌트 주석 */
        else if (strcmp(ext, ".py") == 0) {
            emitln(cg, "/* #포함 Python 모듈: %s */", fname);
            emitln(cg, "/* 힌트: Py_Initialize() + PyRun_SimpleFile() 로 실행 */");
            emitln(cg, "#ifdef KCODE_EMBED_PYTHON");
            emitln(cg, "#include <Python.h>");
            emitln(cg, "#endif");
        }
        /* .js / .ts → Node.js/V8 임베딩 힌트 주석 */
        else if (strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0) {
            emitln(cg, "/* #포함 JS/TS 모듈: %s */", fname);
            emitln(cg, "/* 힌트: system(\"node %s\") 또는 V8 임베딩 */", fname);
        }
        /* .java → JNI 힌트 주석 */
        else if (strcmp(ext, ".java") == 0) {
            emitln(cg, "/* #포함 Java 모듈: %s */", fname);
            emitln(cg, "/* 힌트: JNI(JavaVM) 또는 system(\"javac && java\") */");
        }
        else {
            emitln(cg, "/* #포함 알 수 없는 확장자: %s */", fname);
        }
        break;
    }

    /* ── 스크립트 블록 (v8.1.0) ────────────────────────────────
     *  파이썬/자바/자바스크립트 블록을 C 코드의 system() 호출로 변환.
     *  전략:
     *    1. 전달 변수들을 setenv() 로 환경변수 설정
     *    2. 스크립트 원문을 임시 파일에 fwrite 로 기록
     *    3. system("python3|node|javac+java tmp.ext > out.txt 2>&1")
     *    4. 반환 변수가 있으면 out.txt 마지막 줄을 읽어 변수에 할당
     *    5. 임시 파일/출력 파일 삭제
     * ================================================================ */
    case NODE_SCRIPT_PYTHON:
    case NODE_SCRIPT_JAVA:
    case NODE_SCRIPT_JS: {
        if (!n->sval) break;

        const char *lang_ko;
        const char *ext;
        const char *runner;
        int is_java = (n->type == NODE_SCRIPT_JAVA);
        int is_py   = (n->type == NODE_SCRIPT_PYTHON);

        if (is_py) {
            lang_ko = "\xED\x8C\x8C\xEC\x9D\xB4\xEC\x8D\xAC"; /* 파이썬 */
            ext     = ".py";
            runner  = "python3";
        } else if (is_java) {
            lang_ko = "\xEC\x9E\x90\xEB\xB0\x94";             /* 자바 */
            ext     = ".java";
            runner  = "javac";
        } else {
            lang_ko = "\xEC\x9E\x90\xEB\xB0\x94\xEC\x8A\xA4\xED\x81\xAC\xEB\xA6\xBD\xED\x8A\xB8"; /* 자바스크립트 */
            ext     = ".js";
            runner  = "node";
        }
        (void)runner;

        /* ret_child: node->val.ival (-1이면 반환 없음) */
        int ret_child = (int)n->val.ival;
        int arg_end   = (ret_child >= 0) ? ret_child : n->child_count;

        /* 고유 카운터 (임시 파일 이름 충돌 방지) */
        int uid = cg->tmp_counter++;

        sourcemap_add(cg, n->line, n->col);
        emitln(cg, "/* ── %s 스크립트 블록 (v8.1.0) ── */", lang_ko);
        emitln(cg, "{");
        cg->indent++;

        /* 1. 임시 파일 경로 변수 */
        emitln(cg, "char _kc_script_path_%d[256];", uid);
        emitln(cg, "char _kc_out_path_%d[260];",    uid);
        emitln(cg, "snprintf(_kc_script_path_%d, sizeof(_kc_script_path_%d),", uid, uid);
        emitln(cg, "         \"/tmp/_kcode_cg_%d_%%d%s\", (int)getpid());", uid, ext);
        emitln(cg, "snprintf(_kc_out_path_%d, sizeof(_kc_out_path_%d),", uid, uid);
        emitln(cg, "         \"%%s.out\", _kc_script_path_%d);", uid);

        /* 2. 전달 변수 → setenv */
        for (int i = 0; i < arg_end; i++) {
            Node *ch = n->children[i];
            if (!ch || ch->type != NODE_IDENT || !ch->sval) continue;
            emitln(cg, "setenv(\"KCODE_%s\", kc_to_cstr(kc_%s), 1);",
                   ch->sval, ch->sval);
        }

        /* 3. 스크립트 원문을 임시 파일에 기록 */
        emitln(cg, "{ FILE *_kc_sf_%d = fopen(_kc_script_path_%d, \"w\");", uid, uid);
        cg->indent++;
        emitln(cg, "if (_kc_sf_%d) {", uid);
        cg->indent++;

        /* Python/JS: 환경변수로부터 변수 임포트 헤더 삽입 */
        if (is_py && arg_end > 0) {
            emitln(cg, "fprintf(_kc_sf_%d, \"import os as _kc_os\\n\");", uid);
            for (int i = 0; i < arg_end; i++) {
                Node *ch = n->children[i];
                if (!ch || !ch->sval) continue;
                emitln(cg,
                    "fprintf(_kc_sf_%d,"
                    " \"_kc_raw_%s=_kc_os.environ.get('KCODE_%s','')\\n"
                    "try: %s=int(_kc_raw_%s)\\n"
                    "except ValueError:\\n"
                    " try: %s=float(_kc_raw_%s)\\n"
                    " except ValueError: %s=_kc_raw_%s\\n\");",
                    uid,
                    ch->sval, ch->sval,
                    ch->sval, ch->sval,
                    ch->sval, ch->sval,
                    ch->sval, ch->sval);
            }
        } else if (!is_java && !is_py && arg_end > 0) {
            /* JavaScript: process.env */
            for (int i = 0; i < arg_end; i++) {
                Node *ch = n->children[i];
                if (!ch || !ch->sval) continue;
                emitln(cg,
                    "fprintf(_kc_sf_%d,"
                    " \"const %s=(v=>isNaN(v)?process.env['KCODE_%s']||'':Number(v))"
                    "(process.env['KCODE_%s']);\\n\");",
                    uid, ch->sval, ch->sval, ch->sval);
            }
        }

        /* 원문 코드 escaping 후 fwrite */
        /* 스크립트 원문을 C 문자열 리터럴로 embed */
        emit_indent(cg);
        emit(cg, "fputs(");
        escape_string(cg, n->sval);
        emitln(cg, ", _kc_sf_%d);", uid);
        emitln(cg, "fclose(_kc_sf_%d);", uid);

        cg->indent--;
        emitln(cg, "} /* fopen */");
        cg->indent--;
        emitln(cg, "} /* script write */");

        /* 4. 실행 커맨드 */
        emitln(cg, "{ char _kc_cmd_%d[640];", uid);
        cg->indent++;

        if (is_java) {
            emitln(cg, "char _kc_cdir_%d[300];", uid);
            emitln(cg, "snprintf(_kc_cdir_%d, sizeof(_kc_cdir_%d), \"%%s_cls\", _kc_script_path_%d);",
                   uid, uid, uid);
            emitln(cg, "snprintf(_kc_cmd_%d, sizeof(_kc_cmd_%d),", uid, uid);
            emitln(cg, "    \"mkdir -p '%%s' && javac -d '%%s' '%%s' > '%%s' 2>&1 && java -cp '%%s' Main >> '%%s' 2>&1\",");
            emitln(cg, "    _kc_cdir_%d, _kc_cdir_%d, _kc_script_path_%d, _kc_out_path_%d,",
                   uid, uid, uid, uid);
            emitln(cg, "    _kc_cdir_%d, _kc_out_path_%d);", uid, uid);
        } else {
            /* python3 or node */
            const char *run_cmd = is_py ? "python3" : "node";
            emitln(cg, "snprintf(_kc_cmd_%d, sizeof(_kc_cmd_%d),", uid, uid);
            emitln(cg, "    \"%s '%%s' > '%%s' 2>&1\", _kc_script_path_%d, _kc_out_path_%d);",
                   run_cmd, uid, uid);
        }

        emitln(cg, "int _kc_ret_%d = system(_kc_cmd_%d);", uid, uid);

        /* 5. stdout 수집 */
        emitln(cg, "char _kc_out_%d[65536] = \"\";", uid);
        emitln(cg, "{ FILE *_kc_of_%d = fopen(_kc_out_path_%d, \"r\");", uid, uid);
        cg->indent++;
        emitln(cg, "if (_kc_of_%d) {", uid);
        cg->indent++;
        emitln(cg, "fread(_kc_out_%d, 1, sizeof(_kc_out_%d)-1, _kc_of_%d);",
               uid, uid, uid);
        emitln(cg, "fclose(_kc_of_%d);", uid);
        cg->indent--;
        emitln(cg, "}");
        cg->indent--;
        emitln(cg, "}");

        /* 6. 오류 출력 (비정상 종료 시 stderr) */
        emitln(cg, "if (_kc_ret_%d != 0) {", uid);
        cg->indent++;
        emitln(cg, "fprintf(stderr, \"[%s 오류] %%s\\n\", _kc_out_%d);", lang_ko, uid);
        cg->indent--;
        emitln(cg, "}");

        /* 7. 반환 변수에 stdout 마지막 줄 저장 */
        if (ret_child >= 0 && ret_child < n->child_count &&
            n->children[ret_child] && n->children[ret_child]->sval) {
            const char *rv = n->children[ret_child]->sval;
            emitln(cg, "/* 반환 변수: %s ← stdout 마지막 줄 */", rv);
            emitln(cg, "{ char *_kc_last_%d = _kc_out_%d;", uid, uid);
            cg->indent++;
            emitln(cg, "for (char *_kc_p = _kc_out_%d; *_kc_p; _kc_p++) {", uid);
            cg->indent++;
            emitln(cg, "if (*_kc_p == '\\n' && *((_kc_p)+1) && *((_kc_p)+1) != '\\n')");
            cg->indent++;
            emitln(cg, "_kc_last_%d = (_kc_p)+1;", uid);
            cg->indent--;
            cg->indent--;
            emitln(cg, "}");
            /* 후행 개행 제거 */
            emitln(cg, "size_t _kc_ll_%d = strlen(_kc_last_%d);", uid, uid);
            emitln(cg, "if (_kc_ll_%d > 0 && _kc_last_%d[_kc_ll_%d-1]=='\\n')"
                       " _kc_last_%d[--_kc_ll_%d]='\\0';",
                   uid, uid, uid, uid, uid);
            emitln(cg, "kc_%s = _kc_last_%d;", rv, uid);
            cg->indent--;
            emitln(cg, "}");
        }

        /* 8. 임시 파일 삭제 */
        emitln(cg, "remove(_kc_script_path_%d);", uid);
        emitln(cg, "remove(_kc_out_path_%d);",    uid);
        emitln(cg, "(void)_kc_ret_%d;", uid);

        cg->indent--;
        emitln(cg, "} /* cmd block */");

        cg->indent--;
        emitln(cg, "} /* %s 스크립트 블록 끝 */", lang_ko);
        break;
    /* ── GPU 가속 블록 (v2.0.0 — Zero-Copy SoA + VRAM 상주) ──────────
     *
     * 설계 원칙:
     *   PCIe 왕복 = 블록 전체에서 딱 2번
     *     1. kc_accel_begin()  → 데이터 디바이스 업로드 (1회)
     *     2. kc_accel_exec()   → 디바이스 안에서 연산 (VRAM 상주)
     *     3. kc_accel_end()    → 결과만 호스트로 회수 (1회)
     *
     *   개발자는 직렬화 신경 안 써도 됨.
     *   kc_array_t 는 이미 SoA — data 포인터를 그대로 전달.
     *   폴백: TPU → NPU → GPU → CPU
     * ================================================================ */
    case NODE_GPU_BLOCK: {
        const char *accel_type = n->sval; /* "GPU"|"NPU"|"CPU"|"TPU"|NULL(자동) */
        int uid = cg->tmp_counter++;

        /* NODE_GPU_OP 수집 */
        const char *op_name   = NULL;
        const char *ret_vname = NULL;
        const char *in_args[4] = {NULL, NULL, NULL, NULL};
        int         in_argc    = 0;
        int         body_idx   = -1;

        for (int i = (int)n->child_count - 1; i >= 0; i--) {
            if (n->children[i] && n->children[i]->type == NODE_BLOCK) {
                body_idx = i; break;
            }
        }
        for (int i = 0; i < (body_idx >= 0 ? body_idx : (int)n->child_count); i++) {
            Node *ch = n->children[i];
            if (!ch || ch->type != NODE_GPU_OP) continue;
            op_name = ch->sval;
            int ret_idx = (int)ch->val.ival;
            for (int j = 0; j < ch->child_count && in_argc < 4; j++) {
                if (!ch->children[j]) continue;
                if (j == ret_idx)
                    ret_vname = ch->children[j]->sval;
                else if (ch->children[j]->type == NODE_IDENT)
                    in_args[in_argc++] = ch->children[j]->sval;
            }
            break;
        }

        sourcemap_add(cg, n->line, n->col);
        emitln(cg, "/* ── 가속기 블록 (v2.0.0 Zero-Copy SoA) 종류: %s ── */",
               accel_type ? accel_type : "AUTO");

        cg->has_accel_block = 1;

        if (op_name) {
            const char *arg_a = in_argc > 0 ? in_args[0] : NULL;
            const char *arg_b = in_argc > 1 ? in_args[1] : NULL;

            emitln(cg, "{");
            cg->indent++;

            /* 가속기 타입 결정 */
            if (!accel_type || strcmp(accel_type, "AUTO") == 0)
                emitln(cg, "KcAccelType _ac_type_%d = kc_accel_detect();", uid);
            else
                emitln(cg, "KcAccelType _ac_type_%d = KC_ACCEL_%s;", uid,
                       accel_type ? accel_type : "CPU");

            /* 입력 A: kc_array_t → float* (SoA 포인터 직접 참조, 복사 없음) */
            if (arg_a) {
                emitln(cg, "size_t _n_a_%d = kc_%s ? (size_t)kc_%s->len : 0;",
                       uid, arg_a, arg_a);
                emitln(cg, "float *_fa_%d = NULL;", uid);
                emitln(cg, "if (_n_a_%d > 0 && kc_%s) {", uid, arg_a);
                cg->indent++;
                emitln(cg, "/* SoA 포인터 직접 사용 — 이미 직렬화 완료 */");
                emitln(cg, "_fa_%d = (float*)malloc(_n_a_%d * sizeof(float));", uid, uid);
                emitln(cg, "for (size_t _i=0; _i < _n_a_%d; _i++) {", uid);
                cg->indent++;
                emitln(cg, "kc_value_t _v = kc_arr_get(kc_%s, (int64_t)_i);", arg_a);
                emitln(cg, "_fa_%d[_i] = (_v.type==2) ? (float)_v.v.fval : (float)_v.v.ival;", uid);
                cg->indent--;
                emitln(cg, "}");
                cg->indent--;
                emitln(cg, "}");
            } else {
                emitln(cg, "size_t _n_a_%d = 0; float *_fa_%d = NULL;", uid, uid);
            }

            /* 입력 B */
            if (arg_b) {
                emitln(cg, "size_t _n_b_%d = kc_%s ? (size_t)kc_%s->len : 0;",
                       uid, arg_b, arg_b);
                emitln(cg, "float *_fb_%d = NULL;", uid);
                emitln(cg, "if (_n_b_%d > 0 && kc_%s) {", uid, arg_b);
                cg->indent++;
                emitln(cg, "_fb_%d = (float*)malloc(_n_b_%d * sizeof(float));", uid, uid);
                emitln(cg, "for (size_t _i=0; _i < _n_b_%d; _i++) {", uid);
                cg->indent++;
                emitln(cg, "kc_value_t _v = kc_arr_get(kc_%s, (int64_t)_i);", arg_b);
                emitln(cg, "_fb_%d[_i] = (_v.type==2) ? (float)_v.v.fval : (float)_v.v.ival;", uid);
                cg->indent--;
                emitln(cg, "}");
                cg->indent--;
                emitln(cg, "}");
            } else {
                emitln(cg, "size_t _n_b_%d = 0; float *_fb_%d = NULL;", uid, uid);
            }

            /* ─────────────────────────────────────────────────────────
             * kc_accel_begin() — 데이터 디바이스 업로드 (1회)
             * PCIe: 호스트 → 디바이스 (여기서 한 번)
             * ───────────────────────────────────────────────────────── */
            emitln(cg, "KcAccelCtx *_ctx_%d = kc_accel_begin(", uid);
            emitln(cg, "    _ac_type_%d, _fa_%d, _n_a_%d, _fb_%d, _n_b_%d);", uid,uid,uid,uid,uid);
            emitln(cg, "if (_ctx_%d) {", uid);
            cg->indent++;

            /* ─────────────────────────────────────────────────────────
             * kc_accel_exec() — 디바이스 안에서 연산 (VRAM 상주)
             * PCIe 왕복 없음 — 모든 연산이 디바이스 메모리에서 처리
             * ───────────────────────────────────────────────────────── */
            size_t n_out_var = arg_a ? 1 : 0;
            (void)n_out_var;
            emitln(cg, "/* 디바이스 안에서 연산 — PCIe 왕복 없음 */");
            emitln(cg, "size_t _n_out_%d = _n_a_%d > 0 ? _n_a_%d : _n_b_%d;", uid,uid,uid,uid);
            emitln(cg, "kc_accel_exec(_ctx_%d, \"%s\", _n_out_%d);", uid, op_name, uid);

            /* ─────────────────────────────────────────────────────────
             * kc_accel_end() — 결과 호스트로 회수 (1회)
             * PCIe: 디바이스 → 호스트 (여기서 한 번)
             * ───────────────────────────────────────────────────────── */
            emitln(cg, "/* 결과 회수 — PCIe 디바이스→호스트 (블록 전체에서 1회) */");
            emitln(cg, "size_t _r_n_%d = 0;", uid);
            emitln(cg, "float *_result_%d = kc_accel_end(_ctx_%d, &_r_n_%d);", uid,uid,uid);

            if (ret_vname) {
                emitln(cg, "if (_result_%d && _r_n_%d > 0) {", uid, uid);
                cg->indent++;
                emitln(cg, "kc_%s = kc_arr_new();", ret_vname);
                emitln(cg, "kc_%s->data = malloc(sizeof(kc_value_t) * _r_n_%d);", ret_vname, uid);
                emitln(cg, "kc_%s->len = kc_%s->cap = (int64_t)_r_n_%d;", ret_vname, ret_vname, uid);
                emitln(cg, "for (size_t _i=0; _i<_r_n_%d; _i++) {", uid);
                cg->indent++;
                emitln(cg, "float _f = _result_%d[_i];", uid);
                emitln(cg, "long long _iv = (long long)_f;");
                emitln(cg, "kc_value_t _rv = ((float)_iv == _f)");
                emitln(cg, "    ? (kc_value_t){.type=1,.v.ival=_iv}");
                emitln(cg, "    : (kc_value_t){.type=2,.v.fval=(double)_f};");
                emitln(cg, "((kc_value_t*)kc_%s->data)[_i] = _rv;", ret_vname);
                cg->indent--;
                emitln(cg, "}");
                emitln(cg, "free(_result_%d);", uid);
                cg->indent--;
                emitln(cg, "} else if (_ctx_%d) { kc_accel_abort(_ctx_%d); }", uid, uid);
            } else {
                emitln(cg, "if (_result_%d) free(_result_%d);", uid, uid);
            }

            cg->indent--;
            emitln(cg, "} else {");
            cg->indent++;
            emitln(cg, "fprintf(stderr, \"[가속기] 초기화 실패 — CPU 폴백\\n\");");
            cg->indent--;
            emitln(cg, "}");

            /* 임시 float 배열 해제 */
            emitln(cg, "if (_fa_%d) free(_fa_%d);", uid, uid);
            emitln(cg, "if (_fb_%d) free(_fb_%d);", uid, uid);

            cg->indent--;
            emitln(cg, "} /* 가속기 연산 블록 */");
        }

        /* 일반 본문 블록 */
        if (body_idx >= 0 && n->children[body_idx]) {
            emitln(cg, "/* 가속기 블록 — 일반 코드 본문 */");
            gen_stmt(cg, n->children[body_idx]);
        }

        emitln(cg, "/* ── 가속기 블록 끝 (v2.0.0) ── */");
        break;
    }

    /* ── 계약 시스템 (v2.1.0) ──────────────────────────────── */
    /* ★ 헌법 — 전역 최상위 계약 (v5.0.0) */
    case NODE_CONSTITUTION: {
        const char *san = "중단";
        if (n->child_count > 1 && n->children[1])
            san = token_type_name(n->children[1]->op);
        emitln(cg, "/* [헌법 계약 — 전역 최상위, 제재: %s] */", san);
        if (n->child_count > 0 && n->children[0]) {
            emit(cg, "assert((");
            gen_expr(cg, n->children[0]);
            emitln(cg, ") && \"헌법 계약 위반\");");
        }
        break;
    }

    /* ★ 법률 — 현재 파일 전체 계약 (v5.0.0) */
    case NODE_STATUTE: {
        const char *san = "경고";
        if (n->child_count > 1 && n->children[1])
            san = token_type_name(n->children[1]->op);
        emitln(cg, "/* [법률 계약 — 파일 단위, 제재: %s] */", san);
        if (n->child_count > 0 && n->children[0]) {
            emit(cg, "if (!(");
            gen_expr(cg, n->children[0]);
            emitln(cg, ")) { fprintf(stderr, \"[법률 계약 위반]\\n\"); }");
        }
        break;
    }

    /* ★ 규정 — 특정 객체 전체 메서드 계약 (v5.0.0) */
    case NODE_REGULATION: {
        const char *obj_name = n->sval ? n->sval : "?";
        const char *san = "경고";
        if (n->child_count > 1 && n->children[1])
            san = token_type_name(n->children[1]->op);
        emitln(cg, "/* [규정 계약 — 객체: %s, 제재: %s] */", obj_name, san);
        if (n->child_count > 0 && n->children[0]) {
            emit(cg, "assert((");
            gen_expr(cg, n->children[0]);
            emitln(cg, ") && \"규정 계약 위반: %s\");", obj_name);
        }
        break;
    }

    case NODE_CONTRACT:
        gen_contract(cg, n);
        break;

    case NODE_CHECKPOINT:
        gen_checkpoint(cg, n);
        break;

    case NODE_SANCTION:
        emitln(cg, "/* [제재 노드 — NODE_CONTRACT 내부 처리됨] */");
        break;

    /* ── 인터럽트 시스템 (v6.0.0) ──────────────────────────── */

    /* A: 신호받기 → signal(SIGXXX, handler_fn) + 핸들러 함수 생성 */
    case NODE_SIGNAL_HANDLER: {
        const char *sname = n->sval ? n->sval : "?";
        /* 신호 이름 → C 상수 매핑 */
        const char *posix_sig = "SIGINT";
        if      (n->op == TOK_KW_SIG_INT)  posix_sig = "SIGINT";
        else if (n->op == TOK_KW_SIG_TERM) posix_sig = "SIGTERM";
        else if (n->op == TOK_KW_SIG_KILL) posix_sig = "SIGKILL";
        else if (n->op == TOK_KW_SIG_CHLD) posix_sig = "SIGCHLD";
        else if (n->op == TOK_KW_SIG_USR1) posix_sig = "SIGUSR1";
        else if (n->op == TOK_KW_SIG_USR2) posix_sig = "SIGUSR2";
        else if (n->op == TOK_KW_SIG_PIPE) posix_sig = "SIGPIPE";
        else if (n->op == TOK_KW_SIG_ALRM) posix_sig = "SIGALRM";
        else if (n->op == TOK_KW_SIG_STOP) posix_sig = "SIGSTOP";
        else if (n->op == TOK_KW_SIG_CONT) posix_sig = "SIGCONT";
        /* 핸들러 함수 이름 생성 (충돌 방지용 카운터) */
        int hid = cg->tmp_counter++;
        emitln(cg, "/* [신호받기: %s (%s)] */", sname, posix_sig);
        /* 핸들러 함수를 현재 위치 밖에 선언해야 하므로
         * 여기서는 forward 선언만 하고 함수 본체는 인라인 정적 함수로 생성 */
        emitln(cg, "static void kc_sig_handler_%d(int _kc_signum) {", hid);
        cg->indent++;
        emitln(cg, "(void)_kc_signum;");
        if (n->child_count > 0) gen_stmt(cg, n->children[0]);
        cg->indent--;
        emitln(cg, "}");
        emitln(cg, "signal(%s, kc_sig_handler_%d);", posix_sig, hid);
        break;
    }

    /* A: 신호무시 / 신호기본 / 신호보내기 */
    case NODE_SIGNAL_CTRL: {
        const char *sname = n->sval ? n->sval : "?";
        const char *psig  = "SIGINT";
        if      (strcmp(sname, "중단신호")   == 0) psig = "SIGINT";
        else if (strcmp(sname, "종료신호")   == 0) psig = "SIGTERM";
        else if (strcmp(sname, "강제종료")   == 0) psig = "SIGKILL";
        else if (strcmp(sname, "자식신호")   == 0) psig = "SIGCHLD";
        else if (strcmp(sname, "사용자신호1") == 0) psig = "SIGUSR1";
        else if (strcmp(sname, "사용자신호2") == 0) psig = "SIGUSR2";
        else if (strcmp(sname, "연결신호")   == 0) psig = "SIGPIPE";
        else if (strcmp(sname, "경보신호")   == 0) psig = "SIGALRM";
        else if (strcmp(sname, "재개신호")   == 0) psig = "SIGCONT";

        if (n->op == TOK_KW_SINHOMUSI) {
            emitln(cg, "signal(%s, SIG_IGN);  /* 신호무시: %s */", psig, sname);
        } else if (n->op == TOK_KW_SINHOGIBON) {
            emitln(cg, "signal(%s, SIG_DFL);  /* 신호기본: %s */", psig, sname);
        } else if (n->op == TOK_KW_SINHOBONEGI) {
            emitln(cg, "/* 신호보내기: %s → %s */", sname, psig);
            emit_indent(cg);
            emit(cg, "kill((pid_t)(");
            if (n->child_count > 0) gen_expr(cg, n->children[0]);
            else emit(cg, "getpid()");
            emitln(cg, "), %s);", psig);
        }
        break;
    }

    /* B: 간섭(ISR) 핸들러 → AVR/ARM ISR 매크로 또는 __attribute__((interrupt)) */
    case NODE_ISR_HANDLER: {
        const char *vname = n->sval ? n->sval : "?";
        /* 벡터 이름 → C 매크로 */
        const char *vec_macro = "TIMER0_OVF_vect";
        if      (n->op == TOK_KW_IRQ_TIMER0)    vec_macro = "TIMER0_OVF_vect";
        else if (n->op == TOK_KW_IRQ_TIMER1)    vec_macro = "TIMER1_OVF_vect";
        else if (n->op == TOK_KW_IRQ_EXT0_RISE) vec_macro = "INT0_vect  /* 상승 */";
        else if (n->op == TOK_KW_IRQ_EXT0_FALL) vec_macro = "INT0_vect  /* 하강 */";
        else if (n->op == TOK_KW_IRQ_UART_RX)   vec_macro = "USART_RX_vect";

        emitln(cg, "/* [간섭 ISR: %s] */", vname);
        emitln(cg, "#ifdef ISR  /* AVR avr/interrupt.h */");
        emitln(cg, "ISR(%s) {", vec_macro);
        cg->indent++;
        if (n->child_count > 0) gen_stmt(cg, n->children[0]);
        cg->indent--;
        emitln(cg, "}");
        emitln(cg, "#else  /* 일반 플랫폼 — 시뮬레이션 함수 */");
        emitln(cg, "static void kc_isr_%s(void) {", vname);
        cg->indent++;
        if (n->child_count > 0) gen_stmt(cg, n->children[0]);
        cg->indent--;
        emitln(cg, "}");
        emitln(cg, "#endif  /* ISR */");
        break;
    }

    /* B: 간섭잠금 / 간섭허용 */
    case NODE_ISR_CTRL:
        if (n->op == TOK_KW_GANSEOB_JAMGEUM)
            emitln(cg, "#ifdef cli\n    cli();  /* 간섭잠금 */\n#else\n    /* 간섭잠금: 플랫폼 미지원 */\n#endif");
        else
            emitln(cg, "#ifdef sei\n    sei();  /* 간섭허용 */\n#else\n    /* 간섭허용: 플랫폼 미지원 */\n#endif");
        break;

    /* C: 행사 핸들러 등록 → 함수 포인터 테이블 */
    case NODE_EVENT_HANDLER: {
        const char *evname = n->sval ? n->sval : "?";
        int hid = cg->tmp_counter++;
        emitln(cg, "/* [행사등록: \"%s\"] */", evname);
        /* 핸들러 함수 생성 */
        emitln(cg, "static void kc_ev_handler_%d(void) {", hid);
        cg->indent++;
        if (n->child_count > 0)
            gen_stmt(cg, n->children[n->child_count - 1]);
        cg->indent--;
        emitln(cg, "}");
        /* 런타임 이벤트 테이블에 등록 */
        emitln(cg, "kc_event_register(\"%s\", kc_ev_handler_%d);", evname, hid);
        break;
    }

    /* C: 행사시작 / 행사중단 / 행사발생 / 행사해제 */
    case NODE_EVENT_CTRL:
        if (n->op == TOK_KW_HAENGSA_START) {
            emitln(cg, "kc_event_loop_run();  /* 행사시작 */");
        } else if (n->op == TOK_KW_HAENGSA_STOP) {
            emitln(cg, "kc_event_loop_stop();  /* 행사중단 */");
        } else if (n->op == TOK_KW_HAENGSA_EMIT) {
            emitln(cg, "/* 행사발생 */");
            emit_indent(cg);
            emit(cg, "kc_event_emit(");
            if (n->child_count > 0) gen_expr(cg, n->children[0]);
            emitln(cg, ");");
        } else if (n->op == TOK_KW_HAENGSA_OFF) {
            emitln(cg, "/* 행사해제 */");
            emit_indent(cg);
            emit(cg, "kc_event_unregister(");
            if (n->child_count > 0) gen_expr(cg, n->children[0]);
            emitln(cg, ");");
        }
        break;

    /* ── 객체 클래스 선언 (v2.2.0) ─────────────────────────── */
    case NODE_CLASS_DECL: {
        const char *cname = n->sval ? n->sval : "_class";

        /* 부모 클래스(이어받기) 추출
         * child[0]이 NODE_IDENT이고 child_count >= 2이면 상속 */
        const char *parent = NULL;
        int body_idx = n->child_count - 1; /* 마지막 child = 블록 */
        if (n->child_count >= 2 &&
            n->children[0] &&
            n->children[0]->type == NODE_IDENT) {
            parent = n->children[0]->sval;
        }

        emitln(cg, "/* ── 객체: %s%s%s ── */",
               cname,
               parent ? " (이어받기: " : "",
               parent ? parent : "");
        if (parent) emitln(cg, " */");

        /* typedef struct kc_클래스명 { */
        emitln(cg, "typedef struct kc_%s {", cname);
        cg->indent++;

        /* 상속: 부모를 첫 필드로 임베드 */
        if (parent) {
            emitln(cg, "struct kc_%s kc_base;  /* 부모: %s */", parent, parent);
        }

        /* 블록 내 필드(VAR_DECL/CONST_DECL)만 struct 멤버로 출력 */
        Node *body = n->children[body_idx];
        if (body && body->type == NODE_BLOCK) {
            for (int i = 0; i < body->child_count; i++) {
                Node *s = body->children[i];
                if (!s) continue;
                if (s->type == NODE_VAR_DECL || s->type == NODE_CONST_DECL) {
                    emitln(cg, "%s kc_%s;",
                           c_type(s->dtype),
                           s->sval ? s->sval : "_field");
                    cg->result->var_count++;
                }
            }
        }

        cg->indent--;
        emitln(cg, "} kc_%s;", cname);
        emit(cg, "\n");
        cg->c_line++;

        /* 메서드 함수 전방 선언 + 본체 생성 */
        if (body && body->type == NODE_BLOCK) {
            for (int i = 0; i < body->child_count; i++) {
                Node *s = body->children[i];
                if (!s) continue;
                if (s->type != NODE_FUNC_DECL && s->type != NODE_VOID_DECL) continue;

                int is_void = (s->type == NODE_VOID_DECL);
                const char *mname = s->sval ? s->sval : "_method";

                cg->result->func_count++;

                /* 메서드 시그니처: 반환타입 kc_클래스명_메서드명(kc_클래스명* kc_자신, ...) */
                emitln(cg, "/* 메서드: %s.%s */", cname, mname);
                emit(cg, "%s kc_%s_%s(kc_%s* kc_\xEC\x9E\x90\xEC\x8B\xA0",
                     is_void ? "void" : "kc_value_t",
                     cname, mname, cname);  /* 자신 = \xEC\x9E\x90\xEC\x8B\xA0 */

                /* 매개변수 (자신 제외 — 첫 번째 param이 자신이면 skip) */
                int param_end = s->child_count - 1;
                int start_param = 0;
                /* 첫 파라미터가 '자신'이면 건너뜀 */
                if (param_end > 0 && s->children[0] &&
                    s->children[0]->type == NODE_PARAM &&
                    s->children[0]->sval &&
                    strcmp(s->children[0]->sval,
                           "\xEC\x9E\x90\xEC\x8B\xA0") == 0) {
                    start_param = 1;
                }
                for (int j = start_param; j < param_end; j++) {
                    Node *p = s->children[j];
                    if (!p || p->type != NODE_PARAM) break;
                    emit(cg, ", %s kc_%s",
                         c_type(p->dtype), p->sval ? p->sval : "_p");
                }
                emit(cg, ") {\n");
                cg->c_line++;

                cg->indent++;
                cg->in_func = 1;
                cg->func_has_return = !is_void;

                Node *mbody = s->children[s->child_count - 1];
                gen_block(cg, mbody);

                if (is_void) emitln(cg, "return;");
                else         emitln(cg, "return KC_NULL;");

                cg->indent--;
                cg->in_func = 0;
                emitln(cg, "}");
                emit(cg, "\n");
                cg->c_line++;
            }
        }

        /* 생성자 팩토리 함수: kc_클래스명_new() — malloc + 생성 호출 */
        emitln(cg, "/* 생성자 팩토리: %s */", cname);
        emit(cg, "static kc_%s* kc_%s_new(", cname, cname);
        /* 생성 메서드 파라미터 추출 (자신 제외) */
        if (body && body->type == NODE_BLOCK) {
            for (int i = 0; i < body->child_count; i++) {
                Node *s = body->children[i];
                if (!s || (s->type != NODE_FUNC_DECL && s->type != NODE_VOID_DECL)) continue;
                /* \xEC\x83\x9D\xEC\x84\xB1 = 생성 */
                if (!s->sval || strcmp(s->sval, "\xEC\x83\x9D\xEC\x84\xB1") != 0) continue;
                int param_end = s->child_count - 1;
                int first = 1;
                for (int j = 0; j < param_end; j++) {
                    Node *p = s->children[j];
                    if (!p || p->type != NODE_PARAM) break;
                    if (p->sval && strcmp(p->sval, "\xEC\x9E\x90\xEC\x8B\xA0") == 0) continue;
                    if (!first) emit(cg, ", ");
                    emit(cg, "%s kc_%s", c_type(p->dtype), p->sval ? p->sval : "_p");
                    first = 0;
                }
                break;
            }
        }
        emit(cg, ") {\n"); cg->c_line++;
        cg->indent++;
        emitln(cg, "kc_%s* kc_\xEC\x9E\x90\xEC\x8B\xA0 = (kc_%s*)calloc(1, sizeof(kc_%s));",
               cname, cname, cname);
        /* 생성 메서드 호출 */
        if (body && body->type == NODE_BLOCK) {
            for (int i = 0; i < body->child_count; i++) {
                Node *s = body->children[i];
                if (!s || (s->type != NODE_FUNC_DECL && s->type != NODE_VOID_DECL)) continue;
                if (!s->sval || strcmp(s->sval, "\xEC\x83\x9D\xEC\x84\xB1") != 0) continue;
                emit_indent(cg);
                emit(cg, "kc_%s_\xEC\x83\x9D\xEC\x84\xB1(kc_\xEC\x9E\x90\xEC\x8B\xA0",
                     cname);
                int param_end = s->child_count - 1;
                for (int j = 0; j < param_end; j++) {
                    Node *p = s->children[j];
                    if (!p || p->type != NODE_PARAM) break;
                    if (p->sval && strcmp(p->sval, "\xEC\x9E\x90\xEC\x8B\xA0") == 0) continue;
                    emit(cg, ", kc_%s", p->sval ? p->sval : "_p");
                }
                emit(cg, ");\n"); cg->c_line++;
                break;
            }
        }
        emitln(cg, "return kc_\xEC\x9E\x90\xEC\x8B\xA0;");
        cg->indent--;
        emitln(cg, "}");
        emit(cg, "\n"); cg->c_line++;
        break;
    }

    default:
        /* 산업/임베디드 블록 v16.0.0 */
        if (n->type == NODE_TIMER_BLOCK) {
            const char *period = n->sval ? n->sval : "100ms";
            emitln(cg, "/* 타이머 블록: %s */", period);
            emitln(cg, "{");
            cg->indent++;
            emitln(cg, "kc_timer_start(\"%s\");", period);
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            emitln(cg, "kc_timer_stop();");
            cg->indent--;
            emitln(cg, "}");
            break;
        }
        if (n->type == NODE_ROS2_BLOCK) {
            const char *nname = n->sval ? n->sval : "ros2_node";
            emitln(cg, "/* ROS2 노드: %s */", nname);
            emitln(cg, "{");
            cg->indent++;
            emitln(cg, "kc_ros2_node_init(\"%s\");", nname);
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            emitln(cg, "kc_ros2_node_spin();");
            cg->indent--;
            emitln(cg, "}");
            break;
        }
        /* 안전 규격 블록 v17.0.0 */
        if (n->type == NODE_WATCHDOG_BLOCK) {
            long long tms = (long long)n->val.ival;
            const char *period = n->sval ? n->sval : "1000ms";
            emitln(cg, "/* 워치독 블록: %s */", period);
            emitln(cg, "{");
            cg->indent++;
            emitln(cg, "kc_watchdog_start(%lld);", tms);
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            emitln(cg, "kc_watchdog_kick();");
            emitln(cg, "kc_watchdog_stop();");
            cg->indent--;
            emitln(cg, "}");
            break;
        }
        if (n->type == NODE_FAULT_TOL_BLOCK) {
            long long ncopies = (long long)n->val.ival;
            emitln(cg, "/* 결함허용 %lld중 블록 */", ncopies);
            for (int ci = 0; ci < n->child_count; ci++) {
                emitln(cg, "/* 복제 %d/%lld */", ci+1, ncopies);
                emitln(cg, "{");
                cg->indent++;
                if (n->children[ci]) gen_block(cg, n->children[ci]);
                cg->indent--;
                emitln(cg, "}");
            }
            break;
        }
        /* 온디바이스 AI 블록 v18.0.0 */
        if (n->type == NODE_AI_MODEL_BLOCK) {
            const char *mname = n->sval ? n->sval : "ai_model";
            emitln(cg, "/* AI모델 블록: %s */", mname);
            emitln(cg, "{");
            cg->indent++;
            emitln(cg, "kc_ai_model_begin(\"%s\");", mname);
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            emitln(cg, "kc_ai_model_end();");
            cg->indent--;
            emitln(cg, "}");
            break;
        }
        if (n->type == NODE_TINYML_BLOCK) {
            const char *mname = n->sval ? n->sval : "tinyml";
            emitln(cg, "/* TinyML 블록: %s */", mname);
            emitln(cg, "{");
            cg->indent++;
            emitln(cg, "kc_tinyml_begin(\"%s\");", mname);
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            emitln(cg, "kc_tinyml_end();");
            cg->indent--;
            emitln(cg, "}");
            break;
        }
        if (n->type == NODE_FEDERATED_BLOCK) {
            emitln(cg, "/* 연합학습 블록 */");
            emitln(cg, "{");
            cg->indent++;
            emitln(cg, "kc_federated_begin();");
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            emitln(cg, "kc_federated_end();");
            cg->indent--;
            emitln(cg, "}");
            break;
        }
        /* 표현식 구문으로 처리 시도 */
        emit_indent(cg);
        gen_expr(cg, n);
        emit(cg, ";\n");
        cg->c_line++;
        break;
    }
}

/* ================================================================
 *  블록(들여쓰기 블록) 생성
 * ================================================================ */
static void gen_block(Codegen *cg, Node *n) {
    if (!n) return;
    if (n->type == NODE_BLOCK) {
        for (int i = 0; i < n->child_count; i++) {
            gen_stmt(cg, n->children[i]);
        }
    } else {
        gen_stmt(cg, n);
    }
}

/* ================================================================
 *  함수 선언 생성 (전방 선언용)
 * ================================================================ */
static void gen_func_forward(Codegen *cg, Node *n) {
    int is_void = (n->type == NODE_VOID_DECL);
    const char *name = n->sval ? n->sval : "_func";
    int param_end = n->child_count - 1;

    /* ★ 법위반 대상 함수 (v5.1.0): _impl 전방선언 + 래퍼 전방선언 */
    if (is_postcond_fn(cg, name)) {
        /* _impl 전방선언 */
        emit(cg, "%s kc__%s_impl(", is_void ? "void" : "kc_value_t", name);
        for (int i = 0; i < param_end; i++) {
            Node *p = n->children[i];
            if (!p || p->type != NODE_PARAM) break;
            if (i > 0) emit(cg, ", ");
            emit(cg, "%s kc_%s", c_type(p->dtype), p->sval ? p->sval : "_p");
        }
        emitln(cg, ");");
        /* 래퍼 전방선언 (원래 이름 kc_fn) */
        emit(cg, "%s kc_%s(", is_void ? "void" : "kc_value_t", name);
        for (int i = 0; i < param_end; i++) {
            Node *p = n->children[i];
            if (!p || p->type != NODE_PARAM) break;
            if (i > 0) emit(cg, ", ");
            emit(cg, "%s kc_%s", c_type(p->dtype), p->sval ? p->sval : "_p");
        }
        emitln(cg, ");");
        return;
    }

    /* 일반 함수 전방선언 */
    emit(cg, "%s kc_%s(", is_void ? "void" : "kc_value_t", name);
    /* 마지막 child가 블록, 그 앞이 매개변수들 */
    for (int i = 0; i < param_end; i++) {
        Node *p = n->children[i];
        if (!p || p->type != NODE_PARAM) break;
        if (i > 0) emit(cg, ", ");
        emit(cg, "%s kc_%s", c_type(p->dtype), p->sval ? p->sval : "_p");
    }
    emitln(cg, ");");
}

/* ================================================================
 *  함수 선언 본체 생성
 * ================================================================ */
static void gen_func_def(Codegen *cg, Node *n) {
    int is_void = (n->type == NODE_VOID_DECL);
    const char *name = n->sval ? n->sval : "_func";
    int param_end = n->child_count - 1;

    /* ★ 법위반 대상 함수 (v5.1.0): 본체를 _impl 이름으로 생성 */
    char emit_name[256];
    if (is_postcond_fn(cg, name))
        snprintf(emit_name, sizeof(emit_name), "_%s_impl", name);
    else
        snprintf(emit_name, sizeof(emit_name), "%s", name);

    cg->result->func_count++;

    emitln(cg, "/* 함수: %s */", name);
    emit(cg, "%s kc_%s(", is_void ? "void" : "kc_value_t", emit_name);

    for (int i = 0; i < param_end; i++) {
        Node *p = n->children[i];
        if (!p || p->type != NODE_PARAM) break;
        if (i > 0) emit(cg, ", ");
        emit(cg, "%s kc_%s", c_type(p->dtype), p->sval ? p->sval : "_p");
    }
    emit(cg, ") {\n");
    cg->c_line++;

    cg->indent++;
    cg->in_func = 1;
    cg->func_has_return = !is_void;

    /* 본체 블록 */
    Node *body = n->children[n->child_count - 1];
    gen_block(cg, body);

    if (is_void) emitln(cg, "return;");
    else         emitln(cg, "return KC_NULL;");

    cg->indent--;
    cg->in_func = 0;
    emitln(cg, "}");
    emit(cg, "\n");
    cg->c_line++;
}

/* ================================================================
 *  런타임 헤더 생성 (생성된 C 코드 상단)
 * ================================================================ */
static void gen_runtime_header(Codegen *cg) {
    emitln(cg, "/*");
    emitln(cg, " * 이 파일은 Kcode 컴파일러가 자동 생성한 C 코드입니다.");
    emitln(cg, " * Kcode v6.0.0 — C 코드 생성기");
    emitln(cg, " * 직접 수정하지 마세요.");
    emitln(cg, " */");
    emitln(cg, "");
    emitln(cg, "#include <stdio.h>");
    emitln(cg, "#include <stdlib.h>");
    emitln(cg, "#include <string.h>");
    emitln(cg, "#include <stdint.h>");
    emitln(cg, "#include <math.h>");
    emitln(cg, "#include <setjmp.h>");
    emitln(cg, "#include <stdarg.h>");
    emitln(cg, "#include <signal.h>   /* Kcode 시그널 시스템 (v6.0.0) */");
    emitln(cg, "#include <unistd.h>   /* getpid() / kill()             */");
    emitln(cg, "#include \"kc_tensor.h\"  /* Kcode 텐서 자료형 (v11.0.0) */");
    emitln(cg, "#include \"kc_autograd.h\" /* Kcode 자동미분 엔진 (v15.0.0) */");
    /* ★ 계약 시스템: 법령/법위반 노드가 있을 때만 assert.h 포함 */
    if (cg->has_contracts) {
        emitln(cg, "#include <assert.h>  /* Kcode 법령 계약 시스템 — 자동 삽입 */");
    }
    emitln(cg, "");
    emitln(cg, "/* ── Kcode 런타임 타입 ── */");
    emitln(cg, "typedef struct { void *data; int64_t len; int64_t cap; } kc_array_t;");
    emitln(cg, "typedef struct { int type; union { int64_t ival; double fval; char* sval; kc_array_t* aval; } v; } kc_value_t;");
    emitln(cg, "#define KC_NULL ((kc_value_t){0})");
    emitln(cg, "#define KC_VAL(x) ((kc_value_t){.type=1, .v.ival=(int64_t)(x)})");
    emitln(cg, "#define KC_SENTINEL INT64_MIN");
    emitln(cg, "");
    emitln(cg, "/* ── 예외 처리 스택 ── */");
    emitln(cg, "#define KC_JMP_STACK_MAX 64");
    emitln(cg, "static jmp_buf  _kc_jmp_stack[KC_JMP_STACK_MAX];");
    emitln(cg, "static int      _kc_jmp_top = 0;");
    emitln(cg, "static char     _kc_error_msg[512] = {0};");
    emitln(cg, "static void kc_push_jmp(jmp_buf *j) { if(_kc_jmp_top<KC_JMP_STACK_MAX) memcpy(&_kc_jmp_stack[_kc_jmp_top++],j,sizeof(jmp_buf)); }");
    emitln(cg, "static void kc_pop_jmp(void)  { if(_kc_jmp_top>0) _kc_jmp_top--; }");
    emitln(cg, "static void kc_raise(const char *msg) {");
    emitln(cg, "    strncpy(_kc_error_msg, msg ? msg : \"오류\", sizeof(_kc_error_msg)-1);");
    emitln(cg, "    if (_kc_jmp_top > 0) longjmp(_kc_jmp_stack[_kc_jmp_top-1], 1);");
    emitln(cg, "    fprintf(stderr, \"[Kcode 오류] %%s\\n\", _kc_error_msg); exit(1);");
    emitln(cg, "}");
    emitln(cg, "");
    emitln(cg, "/* ── 배열 런타임 ── */");
    emitln(cg, "static kc_array_t* kc_arr_new(void) {");
    emitln(cg, "    kc_array_t *a = malloc(sizeof(kc_array_t));");
    emitln(cg, "    a->data = NULL; a->len = 0; a->cap = 0; return a;");
    emitln(cg, "}");
    emitln(cg, "static int64_t kc_arr_len(kc_array_t *a) { return a ? a->len : 0; }");
    emitln(cg, "static kc_value_t kc_arr_get(kc_array_t *a, int64_t i) {");
    emitln(cg, "    if (!a || i<0 || i>=a->len) { kc_raise(\"배열 범위 초과\"); return KC_NULL; }");
    emitln(cg, "    return ((kc_value_t*)a->data)[i];");
    emitln(cg, "}");
    emitln(cg, "static kc_array_t* kc_arr_literal(int count, ...) {");
    emitln(cg, "    kc_array_t *a = kc_arr_new();");
    emitln(cg, "    a->data = malloc(sizeof(kc_value_t) * (count > 0 ? count : 1));");
    emitln(cg, "    a->cap = count; a->len = count;");
    emitln(cg, "    va_list ap; va_start(ap, count);");
    emitln(cg, "    for(int i=0;i<count;i++) ((kc_value_t*)a->data)[i]=va_arg(ap,kc_value_t);");
    emitln(cg, "    va_end(ap); return a;");
    emitln(cg, "}");
    emitln(cg, "static kc_array_t* kc_range(int64_t s, int64_t e) {");
    emitln(cg, "    kc_array_t *a = kc_arr_new();");
    emitln(cg, "    int64_t n = (e > s) ? (e - s) : 0;");
    emitln(cg, "    a->data = malloc(sizeof(kc_value_t) * (n>0?n:1));");
    emitln(cg, "    a->cap = n; a->len = n;");
    emitln(cg, "    for(int64_t i=0;i<n;i++) ((kc_value_t*)a->data)[i]=KC_VAL(s+i);");
    emitln(cg, "    return a;");
    emitln(cg, "}");
    emitln(cg, "static void kc_arr_push(kc_array_t *a, kc_value_t v) {");
    emitln(cg, "    if(a->len>=a->cap){");
    emitln(cg, "        a->cap=a->cap*2+1;");
    emitln(cg, "        a->data=realloc(a->data,sizeof(kc_value_t)*a->cap);");
    emitln(cg, "    }");
    emitln(cg, "    ((kc_value_t*)a->data)[a->len++]=v;");
    emitln(cg, "}");
    emitln(cg, "");
    emitln(cg, "/* ── 행사(이벤트 루프) 런타임 (v6.0.0) ── */");
    emitln(cg, "#define KC_EVENT_MAX 64");
    emitln(cg, "typedef void (*kc_ev_fn)(void);");
    emitln(cg, "typedef struct { const char *name; kc_ev_fn fn; } kc_ev_entry_t;");
    emitln(cg, "static kc_ev_entry_t _kc_events[KC_EVENT_MAX];");
    emitln(cg, "static int           _kc_ev_count   = 0;");
    emitln(cg, "static int           _kc_ev_running = 0;");
    emitln(cg, "static void kc_event_register(const char *n, kc_ev_fn fn) {");
    emitln(cg, "    for(int i=0;i<_kc_ev_count;i++) if(strcmp(_kc_events[i].name,n)==0){ _kc_events[i].fn=fn; return; }");
    emitln(cg, "    if(_kc_ev_count<KC_EVENT_MAX){ _kc_events[_kc_ev_count].name=n; _kc_events[_kc_ev_count++].fn=fn; }");
    emitln(cg, "}");
    emitln(cg, "static void kc_event_unregister(kc_value_t nv) {");
    emitln(cg, "    const char *n = nv.v.sval;");
    emitln(cg, "    for(int i=0;i<_kc_ev_count;i++) if(n&&strcmp(_kc_events[i].name,n)==0){ _kc_events[i]=_kc_events[--_kc_ev_count]; return; }");
    emitln(cg, "}");
    emitln(cg, "static void kc_event_emit(kc_value_t nv) {");
    emitln(cg, "    const char *n = nv.v.sval;");
    emitln(cg, "    for(int i=0;i<_kc_ev_count;i++) if(n&&strcmp(_kc_events[i].name,n)==0&&_kc_events[i].fn){ _kc_events[i].fn(); return; }");
    emitln(cg, "}");
    emitln(cg, "static void kc_event_loop_run(void)  { _kc_ev_running=1; while(_kc_ev_running){} }");
    emitln(cg, "static void kc_event_loop_stop(void) { _kc_ev_running=0; }");
    emitln(cg, "");
    emitln(cg, "/* ── 출력 런타임 ── */");
    emitln(cg, "static void kc_print_val(kc_value_t v) {");
    emitln(cg, "    switch(v.type) {");
    emitln(cg, "        case 0: printf(\"없음\"); break;");
    emitln(cg, "        case 1: printf(\"%%lld\", (long long)v.v.ival); break;");
    emitln(cg, "        case 2: printf(\"%%g\", v.v.fval); break;");
    emitln(cg, "        case 3: printf(\"%%s\", v.v.sval ? v.v.sval : \"\"); break;");
    emitln(cg, "        default: printf(\"[값]\"); break;");
    emitln(cg, "    }");
    emitln(cg, "}");
    emitln(cg, "/* 출력 매크로 — 다형 출력 */");
    emitln(cg, "#define kc_print(x) _Generic((x), \\");
    emitln(cg, "    int64_t:  printf(\"%%lld\\n\", (long long)(x)), \\");
    emitln(cg, "    int:      printf(\"%%d\\n\", (int)(x)), \\");
    emitln(cg, "    double:   printf(\"%%g\\n\", (double)(x)), \\");
    emitln(cg, "    char*:    printf(\"%%s\\n\", (char*)(x)), \\");
    emitln(cg, "    default:  kc_print_val((kc_value_t)(x)))");
    emitln(cg, "#define kc_print_no_newline(x) _Generic((x), \\");
    emitln(cg, "    int64_t:  printf(\"%%lld\", (long long)(x)), \\");
    emitln(cg, "    int:      printf(\"%%d\", (int)(x)), \\");
    emitln(cg, "    double:   printf(\"%%g\", (double)(x)), \\");
    emitln(cg, "    char*:    printf(\"%%s\", (char*)(x)), \\");
    emitln(cg, "    default:  kc_print_val((kc_value_t)(x)))");
    emitln(cg, "");
    emitln(cg, "/* ── 기타 내장 ── */");
    emitln(cg, "static int64_t kc_len(const char *s) { return s ? (int64_t)strlen(s) : 0; }");
    emitln(cg, "static char*   kc_input(void) {");
    emitln(cg, "    static char _buf[4096];");
    emitln(cg, "    if (!fgets(_buf, sizeof(_buf), stdin)) return \"\";");
    emitln(cg, "    size_t l = strlen(_buf);");
    emitln(cg, "    if (l > 0 && _buf[l-1]=='\\n') _buf[l-1]=0;");
    emitln(cg, "    return _buf;");
    emitln(cg, "}");
    emitln(cg, "static double  kc_abs(double x)  { return x < 0 ? -x : x; }");
    emitln(cg, "static double  kc_max(double first, ...) {");
    emitln(cg, "    double r = first; va_list ap; va_start(ap, first);");
    emitln(cg, "    double v;");
    emitln(cg, "    while((v = va_arg(ap, double)) != (double)KC_SENTINEL) if(v>r) r=v;");
    emitln(cg, "    va_end(ap); return r;");
    emitln(cg, "}");
    emitln(cg, "static double  kc_min(double first, ...) {");
    emitln(cg, "    double r = first; va_list ap; va_start(ap, first);");
    emitln(cg, "    double v;");
    emitln(cg, "    while((v = va_arg(ap, double)) != (double)KC_SENTINEL) if(v<r) r=v;");
    emitln(cg, "    va_end(ap); return r;");
    emitln(cg, "}");
    emitln(cg, "static char*   kc_to_string(kc_value_t v) {");
    emitln(cg, "    static char _s[64];");
    emitln(cg, "    if(v.type==3) return v.v.sval;");
    emitln(cg, "    if(v.type==1) { snprintf(_s,sizeof(_s),\"%%lld\",(long long)v.v.ival); return _s; }");
    emitln(cg, "    if(v.type==2) { snprintf(_s,sizeof(_s),\"%%g\",v.v.fval); return _s; }");
    emitln(cg, "    return \"\";");
    emitln(cg, "}");

    /* ── AI / 수학 런타임 (v3.7.1) ────────────────────────────── */
    emitln(cg, "");
    emitln(cg, "/* ── AI / 수학 런타임 (v3.7.1) ── */");

    /* 평균제곱오차 */
    emitln(cg, "static double kc_mse(kc_array_t *pred, kc_array_t *real) {");
    emitln(cg, "    if(!pred||!real||pred->len!=real->len||pred->len==0) return 0.0;");
    emitln(cg, "    double s=0.0; int64_t n=pred->len;");
    emitln(cg, "    for(int64_t i=0;i<n;i++){");
    emitln(cg, "        double d=kc_arr_get(pred,i).v.fval - kc_arr_get(real,i).v.fval;");
    emitln(cg, "        s+=d*d;");
    emitln(cg, "    }");
    emitln(cg, "    return s/n;");
    emitln(cg, "}");

    /* 교차엔트로피 */
    emitln(cg, "static double kc_cross_entropy(kc_array_t *ps, kc_array_t *p) {");
    emitln(cg, "    if(!ps||!p||ps->len!=p->len) return 0.0;");
    emitln(cg, "    double s=0.0;");
    emitln(cg, "    for(int64_t i=0;i<ps->len;i++){");
    emitln(cg, "        double pv=kc_arr_get(p,i).v.fval;");
    emitln(cg, "        if(pv<=0.0) pv=1e-15;");
    emitln(cg, "        s-=kc_arr_get(ps,i).v.fval * log(pv);");
    emitln(cg, "    }");
    emitln(cg, "    return s;");
    emitln(cg, "}");

    /* 소프트맥스 */
    emitln(cg, "static kc_array_t* kc_softmax(kc_array_t *a) {");
    emitln(cg, "    if(!a||a->len==0) return kc_arr_new();");
    emitln(cg, "    double mx=kc_arr_get(a,0).v.fval;");
    emitln(cg, "    for(int64_t i=1;i<a->len;i++){double v=kc_arr_get(a,i).v.fval; if(v>mx) mx=v;}");
    emitln(cg, "    double sum=0.0;");
    emitln(cg, "    for(int64_t i=0;i<a->len;i++) sum+=exp(kc_arr_get(a,i).v.fval-mx);");
    emitln(cg, "    kc_array_t *r=kc_arr_new();");
    emitln(cg, "    for(int64_t i=0;i<a->len;i++){");
    emitln(cg, "        kc_value_t v; v.type=2; v.v.fval=exp(kc_arr_get(a,i).v.fval-mx)/sum;");
    emitln(cg, "        kc_arr_push(r,v);");
    emitln(cg, "    }");
    emitln(cg, "    return r;");
    emitln(cg, "}");

    /* 위치인코딩 */
    emitln(cg, "static kc_array_t* kc_positional_encoding(int64_t pos, int64_t d) {");
    emitln(cg, "    kc_array_t *r=kc_arr_new();");
    emitln(cg, "    for(int64_t i=0;i<d/2;i++){");
    emitln(cg, "        double angle=(double)pos/pow(10000.0,(double)(2*i)/(double)d);");
    emitln(cg, "        kc_value_t s,c; s.type=c.type=2;");
    emitln(cg, "        s.v.fval=sin(angle); c.v.fval=cos(angle);");
    emitln(cg, "        kc_arr_push(r,s); kc_arr_push(r,c);");
    emitln(cg, "    }");
    emitln(cg, "    return r;");
    emitln(cg, "}");

    /* 등비수열합 */
    emitln(cg, "static double kc_geom_series(double a, double r) {");
    emitln(cg, "    if(r>=-1.0&&r<=1.0&&(r<-1.0+1e-9||r>1.0-1e-9)) return 0.0;");
    emitln(cg, "    return a/(1.0-r);");
    emitln(cg, "}");

    /* 등차수열합 */
    emitln(cg, "static double kc_arith_series(double a, double d, int64_t n) {");
    emitln(cg, "    return (double)n/2.0*(2.0*a+(double)(n-1)*d);");
    emitln(cg, "}");

    /* 점화식값 */
    emitln(cg, "static double kc_recur_geom(double a1, double r, int64_t n) {");
    emitln(cg, "    return a1*pow(r,(double)(n-1));");
    emitln(cg, "}");

    /* ── 수학 기초 (v3.7.1) ── */
    emitln(cg, "/* ── 수학 기초 런타임 (v3.7.1) ── */");
    emitln(cg, "static double kc_log_base(double base, double x) {");
    emitln(cg, "    return log(x)/log(base);");
    emitln(cg, "}");
    emitln(cg, "static double kc_round_digits(double v, int64_t d) {");
    emitln(cg, "    double f=pow(10.0,(double)d);");
    emitln(cg, "    return round(v*f)/f;");
    emitln(cg, "}");
    emitln(cg, "#define 파이  3.14159265358979323846");
    emitln(cg, "#define 오일러 2.71828182845904523536");

    emitln(cg, "");
}

/* ================================================================
 *  계약 시스템 — 조건식을 C 표현식 문자열로 변환
 *  (gen_expr와 별도로 임시 버퍼에 출력 후 assert에 삽입)
 * ================================================================ */

/* 조건 노드를 임시 버퍼에 문자열로 렌더링하는 헬퍼 */
static void gen_cond_to_buf(Codegen *cg, Node *cond, char *out, size_t out_size) {
    /* 현재 출력 버퍼 위치를 기록 후 gen_expr 실행, 그 후 복사 */
    size_t save_len = cg->buf_len;
    gen_expr(cg, cond);
    size_t written = cg->buf_len - save_len;
    if (written >= out_size) written = out_size - 1;
    memcpy(out, cg->buf + save_len, written);
    out[written] = '\0';
    /* 임시로 출력한 부분을 버퍼에서 롤백 */
    cg->buf_len = save_len;
    cg->buf[cg->buf_len] = '\0';
}

/* ================================================================
 *  계약 시스템 -- NODE_CONTRACT 처리
 *
 *  법령 (사전조건, TOK_KW_BEOPRYEONG):
 *    -> assert(조건);  // [법령: 범위]
 *
 *  법위반 (사후조건, TOK_KW_BEOPWIBAN):
 *    -> 사후조건 주석만 출력 (C 런타임 사후조건 완전 구현은 v3.0 예정)
 *    C assert로 사후조건 완전 구현은 함수 래퍼 리팩토링이 필요하므로
 *    v2.1.0 에서는 주석 + 경고만 출력, 향후 래퍼 방식 예정
 *
 *  제재(NODE_SANCTION)는 NODE_CONTRACT 내부에서 처리한다.
 * ================================================================ */
static void gen_contract(Codegen *cg, Node *n) {
    if (!n || n->type != NODE_CONTRACT) return;
    if (n->child_count < 2) return;  /* 조건 + 제재 최소 2개 필요 */

    Node *cond_node    = n->children[0];
    Node *sanction_node = n->children[1]; /* NODE_SANCTION */
    const char *scope  = n->sval ? n->sval : "(알 수 없음)";
    int is_precond     = (n->op == TOK_KW_BEOPRYEONG);

    /* 제재 토큰 이름 결정 */
    const char *sanction_name = "경고";
    if (sanction_node && sanction_node->type == NODE_SANCTION) {
        switch (sanction_node->op) {
            case TOK_KW_GYEONGGO:  sanction_name = "경고";  break;
            case TOK_KW_BOGO:      sanction_name = "보고";  break;
            case TOK_KW_JUNGDAN:   sanction_name = "중단";  break;
            case TOK_KW_HOEGWI:    sanction_name = "회귀";  break;
            case TOK_KW_DAECHE:    sanction_name = "대체";  break;
            default:               sanction_name = "경고";  break;
        }
    }

    sourcemap_add(cg, n->line, n->col);

    if (is_precond) {
        /* ── 법령 → assert(조건); ── */

        /* 조건식을 임시 버퍼에 렌더링 */
        char cond_str[1024];
        gen_cond_to_buf(cg, cond_node, cond_str, sizeof(cond_str));

        /* 제재에 따른 출력 형태 결정 */
        if (sanction_node &&
            (sanction_node->op == TOK_KW_JUNGDAN ||
             sanction_node->op == TOK_KW_HOEGWI)) {
            /* 중단/회귀 → assert로 즉시 중단 */
            emitln(cg, "/* [법령: %s | 제재: %s] */", scope, sanction_name);
            emitln(cg, "assert((%s) && \"[Kcode 법령 위반: %s]\");",
                   cond_str, scope);
        } else if (sanction_node && sanction_node->op == TOK_KW_BOGO) {
            /* 보고 → 조건 위반 시 fprintf+계속 */
            emitln(cg, "/* [법령: %s | 제재: 보고] */", scope);
            emitln(cg, "if (!(%s)) {", cond_str);
            cg->indent++;
            emitln(cg, "fprintf(stderr, \"[Kcode 법령 보고] 범위=%%s 조건 위반: %s\\n\", \"%s\");",
                   cond_str, scope);
            cg->indent--;
            emitln(cg, "}");
        } else if (sanction_node && sanction_node->op == TOK_KW_DAECHE) {
            /* 대체 → 조건 위반 시 조기 반환 (대체값 있으면 반환) */
            emitln(cg, "/* [법령: %s | 제재: 대체] */", scope);
            emitln(cg, "if (!(%s)) {", cond_str);
            cg->indent++;
            if (sanction_node->child_count > 0 && sanction_node->children[0]) {
                emit_indent(cg);
                emit(cg, "return ");
                gen_expr(cg, sanction_node->children[0]);
                emit(cg, ";\n");
                cg->c_line++;
            } else {
                emitln(cg, "return; /* 대체 — 조건 불충족 시 함수 조기 종료 */");
            }
            cg->indent--;
            emitln(cg, "}");
        } else {
            /* 경고 → fprintf(stderr) 후 계속 실행 */
            emitln(cg, "/* [법령: %s | 제재: 경고] */", scope);
            emitln(cg, "if (!(%s)) {", cond_str);
            cg->indent++;
            emitln(cg, "fprintf(stderr, \"[Kcode 법령 경고] 범위=%%s 조건 위반: %s\\n\", \"%s\");",
                   cond_str, scope);
            cg->indent--;
            emitln(cg, "}");
        }
    } else {
        /* ── 법위반 (사후조건) — v5.1.0 래퍼 방식 ──────────────────
         * 최상위 선언 위치에서는 위치 주석만 남긴다.
         * 실제 사후조건 검증 코드는 gen_postcond_wrapper() 가 생성하는
         * kc_fn() 래퍼 함수 내부에 삽입된다.
         * ─────────────────────────────────────────────────────────── */
        char cond_str[1024];
        gen_cond_to_buf(cg, cond_node, cond_str, sizeof(cond_str));
        emitln(cg, "/* [법위반 계약 등록: 함수='%s' | 제재: %s | 조건: %s] */",
               scope, sanction_name, cond_str);
        emitln(cg, "/* → 사후조건 검증은 kc_%s() 래퍼 함수에서 수행됩니다. */", scope);
    }
}

/* ================================================================
 *  법위반(사후조건) 래퍼 함수 생성 (v5.1.0)
 *
 *  법위반 계약이 있는 함수 fn에 대해 아래 형태의 래퍼를 생성한다:
 *
 *    kc_value_t kc_fn(매개변수...) {
 *        kc_value_t _kc_ret = kc__fn_impl(매개변수...);
 *        // [법위반: fn | 제재: 경고]
 *        if (!(조건)) { fprintf(stderr, ...); }
 *        return _kc_ret;
 *    }
 *
 *  program 전체를 순회하여 fn_name 의 NODE_CONTRACT(법위반) 노드를
 *  모두 찾아 래퍼 본문에 삽입한다.
 * ================================================================ */

/* 사후조건 검증 블록 하나를 emit (제재별 분기) */
static void emit_postcond_check(Codegen *cg, Node *n) {
    if (!n || n->type != NODE_CONTRACT || n->op != TOK_KW_BEOPWIBAN) return;
    if (n->child_count < 2) return;

    Node *cond_node     = n->children[0];
    Node *sanction_node = n->children[1];
    const char *scope   = n->sval ? n->sval : "?";

    const char *san_name = "경고";
    TokenType   san_op   = TOK_KW_GYEONGGO;
    if (sanction_node && sanction_node->type == NODE_SANCTION) {
        san_op = sanction_node->op;
        switch (san_op) {
            case TOK_KW_GYEONGGO: san_name = "경고";  break;
            case TOK_KW_BOGO:     san_name = "보고";  break;
            case TOK_KW_JUNGDAN:  san_name = "중단";  break;
            case TOK_KW_HOEGWI:   san_name = "회귀";  break;
            case TOK_KW_DAECHE:   san_name = "대체";  break;
            default:              san_name = "경고";  break;
        }
    }

    char cond_str[1024];
    gen_cond_to_buf(cg, cond_node, cond_str, sizeof(cond_str));

    emitln(cg, "/* [법위반: %s | 제재: %s] */", scope, san_name);
    emitln(cg, "if (!(%s)) {", cond_str);
    cg->indent++;

    switch (san_op) {
        case TOK_KW_JUNGDAN:
            emitln(cg, "assert(0 && \"[Kcode 법위반 중단] 함수=%s 사후조건 위반\");", scope);
            break;
        case TOK_KW_HOEGWI:
            emitln(cg, "fprintf(stderr, \"[Kcode 법위반 회귀] 함수=%s 사후조건 위반\\n\");", scope);
            emitln(cg, "return KC_NULL; /* 회귀: 기본값 반환 */");
            break;
        case TOK_KW_DAECHE:
            emitln(cg, "fprintf(stderr, \"[Kcode 법위반 대체] 함수=%s 사후조건 위반\\n\");", scope);
            if (sanction_node->child_count > 0 && sanction_node->children[0]) {
                emit_indent(cg);
                emit(cg, "return ");
                gen_expr(cg, sanction_node->children[0]);
                emit(cg, ";\n");
                cg->c_line++;
            } else {
                emitln(cg, "return KC_NULL; /* 대체: 기본값 */");
            }
            break;
        case TOK_KW_BOGO:
            emitln(cg, "fprintf(stderr, \"[Kcode 법위반 보고] 함수=%s 사후조건 위반: %s\\n\");",
                   scope, cond_str);
            break;
        default: /* 경고 */
            emitln(cg, "fprintf(stderr, \"[Kcode 법위반 경고] 함수=%s 사후조건 위반: %s\\n\");",
                   scope, cond_str);
            break;
    }

    cg->indent--;
    emitln(cg, "}");
}

/* program 트리에서 fn_name 의 법위반 계약을 모두 찾아 emit */
static void emit_all_postconds(Codegen *cg, Node *node, const char *fn_name) {
    if (!node) return;
    if (node->type == NODE_CONTRACT &&
        node->op   == TOK_KW_BEOPWIBAN &&
        node->sval && strcmp(node->sval, fn_name) == 0) {
        emit_postcond_check(cg, node);
        return; /* 이 노드 처리 완료 */
    }
    for (int i = 0; i < node->child_count; i++)
        emit_all_postconds(cg, node->children[i], fn_name);
}

/* 래퍼 함수 생성 (fn_node = 함수 AST, program = 루트) */
static void gen_postcond_wrapper(Codegen *cg, Node *fn_node, Node *program) {
    int is_void = (fn_node->type == NODE_VOID_DECL);
    const char *name = fn_node->sval ? fn_node->sval : "_func";
    int param_end = fn_node->child_count - 1;

    emitln(cg, "/* 법위반(사후조건) 래퍼: %s */", name);
    emit(cg, "%s kc_%s(", is_void ? "void" : "kc_value_t", name);
    for (int i = 0; i < param_end; i++) {
        Node *p = fn_node->children[i];
        if (!p || p->type != NODE_PARAM) break;
        if (i > 0) emit(cg, ", ");
        emit(cg, "%s kc_%s", c_type(p->dtype), p->sval ? p->sval : "_p");
    }
    emitln(cg, ") {");
    cg->c_line++;
    cg->indent++;
    cg->in_func    = 1;
    cg->func_has_return = !is_void;

    /* 1. 내부 구현 호출 → 반환값 임시 저장 */
    if (!is_void) {
        emit(cg, "kc_value_t _kc_ret = kc__%s_impl(", name);
    } else {
        emit(cg, "kc__%s_impl(", name);
    }
    for (int i = 0; i < param_end; i++) {
        Node *p = fn_node->children[i];
        if (!p || p->type != NODE_PARAM) break;
        if (i > 0) emit(cg, ", ");
        emit(cg, "kc_%s", p->sval ? p->sval : "_p");
    }
    emitln(cg, ");");

    /* 2. 사후조건 검증 블록 삽입 */
    emitln(cg, "/* ── 법위반 사후조건 검증 ── */");
    emit_all_postconds(cg, program, name);

    /* 3. 반환 */
    if (!is_void) emitln(cg, "return _kc_ret;");
    else          emitln(cg, "return;");

    cg->indent--;
    cg->in_func = 0;
    emitln(cg, "}");
    emit(cg, "\n");
    cg->c_line++;
}

/* ================================================================
 *  계약 시스템 — NODE_CHECKPOINT 처리
 *  복원지점은 C 런타임에서 완전한 상태 복원이 불가능하므로
 *  의미 있는 주석으로 표시합니다.
 * ================================================================ */
static void gen_checkpoint(Codegen *cg, Node *n) {
    if (!n || n->type != NODE_CHECKPOINT) return;
    const char *name = n->sval ? n->sval : "(이름 없음)";
    sourcemap_add(cg, n->line, n->col);
    emitln(cg, "/* [복원지점: %s]", name);
    emitln(cg, " * 주의: C 런타임에서 전역 상태 복원은 미지원 (인터프리터 전용 기능)");
    emitln(cg, " */");
}

/* ================================================================
 *  계약 노드 존재 여부 1패스 선탐지 (has_contracts 설정)
 *  — 계약 노드가 하나라도 있으면 #include <assert.h> 삽입 필요
 * ================================================================ */
static int detect_contracts(Node *node) {
    if (!node) return 0;
    if (node->type == NODE_CONTRACT    || node->type == NODE_CHECKPOINT  ||
        node->type == NODE_CONSTITUTION || node->type == NODE_STATUTE    ||
        node->type == NODE_REGULATION) return 1;
    for (int i = 0; i < node->child_count; i++) {
        if (detect_contracts(node->children[i])) return 1;
    }
    return 0;
}

/* ================================================================
 *  법위반 대상 함수명 1패스 수집 (v5.1.0)
 *  NODE_CONTRACT { op == TOK_KW_BEOPWIBAN, sval == 함수명 } 를 전체 트리에서
 *  찾아 Codegen.postcond_fns[] 에 중복 없이 등록한다.
 * ================================================================ */
static void detect_postconds(Codegen *cg, Node *node) {
    if (!node) return;
    if (node->type == NODE_CONTRACT &&
        node->op   == TOK_KW_BEOPWIBAN &&
        node->sval) {
        /* 중복 방지 */
        int dup = 0;
        for (int i = 0; i < cg->postcond_fn_count; i++) {
            if (strcmp(cg->postcond_fns[i], node->sval) == 0) { dup = 1; break; }
        }
        if (!dup && cg->postcond_fn_count < POSTCOND_FN_MAX)
            cg->postcond_fns[cg->postcond_fn_count++] = node->sval; /* AST 포인터 참조 */
    }
    for (int i = 0; i < node->child_count; i++)
        detect_postconds(cg, node->children[i]);
}

/* 함수명이 법위반 대상인지 확인 */
static int is_postcond_fn(Codegen *cg, const char *fn_name) {
    if (!fn_name) return 0;
    for (int i = 0; i < cg->postcond_fn_count; i++)
        if (strcmp(cg->postcond_fns[i], fn_name) == 0) return 1;
    return 0;
}

/* ================================================================
 *  최상위 프로그램 생성
 * ================================================================ */
static void gen_program(Codegen *cg, Node *program) {
    /* 0. 계약 노드 존재 여부 1패스 선탐지 (assert.h 조건부 삽입용) */
    cg->has_contracts = detect_contracts(program);

    /* 0.5. 법위반 대상 함수명 수집 (v5.1.0) */
    detect_postconds(cg, program);

    /* 1. 런타임 헤더 */
    gen_runtime_header(cg);

    /* 2. 함수 전방 선언 (순서 독립성 보장) */
    emitln(cg, "/* ── 함수 전방 선언 ── */");
    for (int i = 0; i < program->child_count; i++) {
        Node *n = program->children[i];
        if (n && (n->type == NODE_FUNC_DECL || n->type == NODE_VOID_DECL)) {
            gen_func_forward(cg, n);
        }
    }
    emitln(cg, "");

    /* 3. 함수 본체 */
    emitln(cg, "/* ── 함수 정의 ── */");
    for (int i = 0; i < program->child_count; i++) {
        Node *n = program->children[i];
        if (n && (n->type == NODE_FUNC_DECL || n->type == NODE_VOID_DECL)) {
            gen_func_def(cg, n);
        }
    }

    /* 3.5. 법위반(사후조건) 래퍼 생성 (v5.1.0) */
    if (cg->postcond_fn_count > 0) {
        emitln(cg, "/* ── 법위반(사후조건) 래퍼 함수 ── */");
        for (int i = 0; i < program->child_count; i++) {
            Node *n = program->children[i];
            if (n && (n->type == NODE_FUNC_DECL || n->type == NODE_VOID_DECL) &&
                is_postcond_fn(cg, n->sval)) {
                gen_postcond_wrapper(cg, n, program);
            }
        }
    }

    /* 4. main 함수 — 최상위 구문들 */
    emitln(cg, "/* ── 프로그램 진입점 ── */");
    emitln(cg, "int main(void) {");
    cg->indent++;

    for (int i = 0; i < program->child_count; i++) {
        Node *n = program->children[i];
        if (!n) continue;
        /* 함수 선언은 이미 처리함 */
        if (n->type == NODE_FUNC_DECL || n->type == NODE_VOID_DECL) continue;
        gen_stmt(cg, n);
    }

    emitln(cg, "return 0;");
    cg->indent--;
    emitln(cg, "}");
}

/* ================================================================
 *  공개 API 구현
 * ================================================================ */

CodegenResult *codegen_run(Node *program) {
    CodegenResult *r = (CodegenResult*)calloc(1, sizeof(CodegenResult));

    Codegen cg;
    memset(&cg, 0, sizeof(cg));
    cg.buf_cap = CODEGEN_BUF_INIT;
    cg.buf     = (char*)malloc(cg.buf_cap);
    cg.buf[0]  = '\0';
    cg.c_line  = 1;
    cg.result  = r;

    if (!program) {
        cg_error(&cg, 0, 0, "AST가 NULL입니다");
        r->c_source     = cg.buf;
        r->c_source_len = cg.buf_len;
        return r;
    }

    gen_program(&cg, program);

    r->c_source     = cg.buf;
    r->c_source_len = cg.buf_len;
    r->line_count   = cg.c_line;

    return r;
}

void codegen_result_free(CodegenResult *r) {
    if (!r) return;
    free(r->c_source);
    free(r);
}

/* ================================================================
 *  JSON 출력 — IDE 연동 프로토콜
 *
 *  IDE(JavaScript/TypeScript)는 이 JSON을 파싱해서:
 *    "success"    → 컴파일 성공 여부
 *    "c_source"   → C 코드 패널에 표시
 *    "sourcemap"  → 오류 위치를 .han 에디터에 하이라이트
 *    "errors"     → 오류 패널에 표시
 *    "stats"      → 정보 패널 (함수 수, 변수 수, 라인 수)
 * ================================================================ */
static void json_escape(FILE *out, const char *s) {
    if (!s) return;
    for (; *s; s++) {
        switch (*s) {
            case '"':  fprintf(out, "\\\""); break;
            case '\\': fprintf(out, "\\\\"); break;
            case '\n': fprintf(out, "\\n");  break;
            case '\r': fprintf(out, "\\r");  break;
            case '\t': fprintf(out, "\\t");  break;
            default:   fputc(*s, out);       break;
        }
    }
}

void codegen_to_json(const CodegenResult *r, FILE *out) {
    fprintf(out, "{\n");
    fprintf(out, "  \"success\": %s,\n", r->had_error ? "false" : "true");

    /* c_source */
    fprintf(out, "  \"c_source\": \"");
    json_escape(out, r->c_source);
    fprintf(out, "\",\n");

    /* sourcemap */
    fprintf(out, "  \"sourcemap\": [\n");
    for (int i = 0; i < r->sourcemap_count; i++) {
        const SourceMapEntry *e = &r->sourcemap[i];
        fprintf(out, "    {\"han_line\": %d, \"han_col\": %d, \"c_line\": %d}%s\n",
                e->han_line, e->han_col, e->c_line,
                (i < r->sourcemap_count - 1) ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* errors */
    fprintf(out, "  \"errors\": [\n");
    for (int i = 0; i < r->error_count; i++) {
        const CodegenError *e = &r->errors[i];
        fprintf(out, "    {\"line\": %d, \"col\": %d, \"msg\": \"", e->line, e->col);
        json_escape(out, e->msg);
        fprintf(out, "\"}%s\n", (i < r->error_count - 1) ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* stats */
    fprintf(out, "  \"stats\": {\n");
    fprintf(out, "    \"func_count\": %d,\n", r->func_count);
    fprintf(out, "    \"var_count\": %d,\n",  r->var_count);
    fprintf(out, "    \"line_count\": %d\n",  r->line_count);
    fprintf(out, "  }\n");
    fprintf(out, "}\n");
}

/* ================================================================
 *  gcc 컴파일 실행
 * ================================================================ */
int codegen_compile(const char *c_path, const char *out_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "gcc -std=c11 -Wall -O2 -lm -o \"%s\" \"%s\" 2>&1",
             out_path, c_path);
    int ret = system(cmd);
    return ret;
}
