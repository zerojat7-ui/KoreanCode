/*
 * kcodegen_llvm.h  —  Kcode LLVM IR 코드 생성기 헤더
 * version : v10.0.0
 *
 * v10.0.0 변경:
 *   - gen_call() 미구현 내장 함수 20종 LLVM IR 추가
 *     (kcodegen_llvm.c v10.0.0 동기화)
 *
 * v9.9.0 변경:
 *   - gen_call() 수학/AI 내장 함수 LLVM IR 추가
 *     (kcodegen_llvm.c v9.9.0 동기화)
 *
 * v9.8.0 변경:
 *   - gen_call() 파일 내장 함수 17종 + 형변환 3종 LLVM IR 추가
 *     (kcodegen_llvm.c v9.8.0 동기화)
 *
 * v9.7.0 변경:
 *   - gen_call() 글자 함수 21종 LLVM IR 추가
 *     (kcodegen_llvm.c v9.7.0 동기화)
 *
 * v9.5.0 변경:
 *   - 인터럽트 시스템 6종 LLVM IR 구현
 *     (kcodegen_llvm.c v9.5.0 동기화)
 *   - lambda_counter: 신호/ISR/이벤트 핸들러 함수 이름 번호 공유
 *
 * v9.4.0 변경:
 *   - 계약 시스템 6종 LLVM IR 구현
 *     (kcodegen_llvm.c v9.4.0 동기화)
 *
 * v9.3.0 변경:
 *   - NODE_PP_STMT / NODE_IMPORT LLVM IR 구현
 *     (kcodegen_llvm.c v9.3.0 동기화)
 *
 * v9.2.0 변경:
 *   - NODE_GOTO / NODE_LABEL / NODE_LAMBDA / NODE_DICT_LIT LLVM IR 구현
 *     - label_map[]: 이름 → BasicBlockRef 매핑 (forward resolve 지원)
 *     - lambda_counter: 람다/신호/ISR/이벤트 핸들러 함수 번호 관리
 *   - LLVMCodegen 구조체에 label_map[] / label_count / lambda_counter 필드 추가
 *     (kcodegen_llvm.c v9.2.0 동기화)
 *
 * v9.1.0 변경:
 *   - NODE_MEMBER / NODE_SWITCH / NODE_TRY / NODE_RAISE LLVM IR 구현
 *     (kcodegen_llvm.c v9.1.0 동기화)
 *
 * v9.0.0 변경:
 *   - 가속기 NPU 경로 LLVM IR 완전 구현 (kcodegen_llvm.c v9.0.0 동기화)
 *   - 객체(CLASS) LLVM IR 변환 완전 구현
 *     - LLVMClassEntry  : 클래스별 구조체 타입/vtable 타입/메서드 포인터 캐시
 *     - LLVMCodegen에 class_reg[]/class_count 추가
 *     - gen_class_decl() : CLASS_DECL → LLVM struct + vtable + 메서드 함수 IR
 *
 * v3.7.1 기준:
 *   - 기본 자료형: 정수(i64), 실수(double), 논리(i1), 문자(i32), 글자(i8*)
 *   - 전역 / 지역 변수 선언 및 대입
 *   - 산술 / 비교 / 논리 이항 연산
 *   - 단항 연산 (부정, 논리 NOT)
 *   - 함수 선언(반환값 있음) / 정의(void)
 *   - 함수 호출
 *   - 조건문 (만약 / 아니면)
 *   - 반복문 (동안)
 *   - 횟수 반복 (반복 i 부터 N 까지 M)
 *   - 반환 (반환 / 끝냄)
 *   - 멈춤 / 건너뜀 (break / continue)
 *   - 내장 출력 함수 (출력 → printf 래핑)
 *   - 기본 최적화 패스 (mem2reg, -O2)
 *   - 소스맵 (.han 라인 ↔ IR 라인 매핑)
 *   - IDE JSON 출력 (--json 모드)
 *   - IDE 서버 모드 (--ide, stdin JSON 수신)
 *   - 최적화 전/후 IR 동시 제공 (ir_text_unopt)  ← v3.1.0 추가
 *
 * IDE 연동 프로토콜 (kcodegen과 동일):
 *   stdin  ← {"action":"compile", "source":"...han 소스..."}
 *   stdout → {"success":true, "ir_text":"...", "sourcemap":[...],
 *             "errors":[], "stats":{...}}
 *
 * 실행 흐름:
 *   .han → 렉서 → 파서 → AST → [kcodegen_llvm] → LLVM IR(.ll)
 *                                                     ↓
 *                                               llc / clang
 *                                                     ↓
 *                                               네이티브 실행파일
 *
 * 빌드 (LLVM 필요):
 *   gcc $(llvm-config --cflags) -o kcode_llvm \
 *       klexer.c kparser.c kcodegen_llvm.c kcodegen_llvm_main.c \
 *       $(llvm-config --ldflags --libs core analysis native bitwriter)
 *
 * MIT License
 * zerojat7
 */

#ifndef KCODE_CODEGEN_LLVM_H
#define KCODE_CODEGEN_LLVM_H

#ifdef KCODE_LLVM   /* make LLVM=1 일 때만 활성화 */

#include "kparser.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>
#include <stdio.h>
#include <stdint.h>

/* ================================================================
 *  오류 엔트리
 * ================================================================ */
typedef struct {
    int  line;
    int  col;
    char msg[256];
} LLVMCodegenError;

#define LLVM_CODEGEN_MAX_ERRORS   64

/* ================================================================
 *  소스맵 엔트리 — .han 라인 ↔ IR 라인 매핑
 *  IDE에서 오류/경고 위치를 한글 소스 기준으로 표시할 때 사용.
 *  kcodegen의 SourceMapEntry와 동일한 필드 구성 (IDE 호환).
 * ================================================================ */
typedef struct {
    int han_line;   /* .han 원본 라인 번호 */
    int han_col;    /* .han 원본 열 번호   */
    int ir_line;    /* 대응하는 IR 라인 번호 (IR 텍스트 기준) */
} LLVMSourceMapEntry;

#define LLVM_CODEGEN_MAX_SOURCEMAP 4096

/* ================================================================
 *  심볼 테이블 — 변수명 → LLVMValueRef (alloca 포인터)
 * ================================================================ */
#define LLVM_SYM_MAX 1024

typedef struct {
    char         name[128];
    LLVMValueRef alloca;    /* alloca 명령어 결과 (포인터) */
    LLVMTypeRef  type;      /* 변수의 LLVM 타입           */
} LLVMSymbol;

typedef struct LLVMScope {
    LLVMSymbol        syms[LLVM_SYM_MAX];
    int               count;
    struct LLVMScope *parent;
} LLVMScope;

/* ================================================================
 *  루프 컨텍스트 — break / continue 타겟 블록 추적
 * ================================================================ */
#define LLVM_LOOP_MAX 64

typedef struct {
    LLVMBasicBlockRef break_bb;     /* 멈춤 → 점프할 블록    */
    LLVMBasicBlockRef continue_bb;  /* 건너뜀 → 점프할 블록 */
} LLVMLoopCtx;

/* ================================================================
 *  LLVM 코드 생성 결과
 *  IDE는 이 구조체를 통해 생성 결과 전체를 받는다.
 *  kcodegen의 CodegenResult와 필드 의미를 맞춰 IDE 호환성 유지.
 * ================================================================ */
typedef struct {
    char  *ir_text;         /* 생성된 LLVM IR 텍스트 — 최적화 후 (힙 할당, 호출자가 free) */
    size_t ir_len;

    char  *ir_text_unopt;   /* 최적화 전 LLVM IR 텍스트 (힙 할당, 호출자가 free)
                             * IDE 미리보기에서 최적화 전/후 비교에 사용.
                             * NULL 이면 최적화 전 IR을 추출하지 않은 것. */
    size_t ir_unopt_len;

    /* 소스맵 */
    LLVMSourceMapEntry sourcemap[LLVM_CODEGEN_MAX_SOURCEMAP];
    int                sourcemap_count;

    /* 오류 목록 */
    LLVMCodegenError   errors[LLVM_CODEGEN_MAX_ERRORS];
    int                error_count;
    int                had_error;

    /* 통계 (IDE 정보 패널용) */
    int func_count;     /* 생성된 함수 수  */
    int var_count;      /* 선언된 변수 수  */
    int ir_line_count;  /* 최적화 후 IR 라인 수 */
} LLVMCodegenResult;

/* ================================================================
 *  LLVM 코드 생성기 내부 상태
 * ================================================================ */
typedef struct {
    /* LLVM 핵심 객체 */
    LLVMContextRef  ctx;
    LLVMModuleRef   module;
    LLVMBuilderRef  builder;

    /* 현재 함수 */
    LLVMValueRef    cur_func;
    LLVMTypeRef     cur_func_ret_type;
    int             cur_func_is_void;

    /* 심볼 테이블 (스코프 체인) */
    LLVMScope      *scope;

    /* 루프 스택 */
    LLVMLoopCtx     loop_stack[LLVM_LOOP_MAX];
    int             loop_depth;

    /* 소스맵 기록용 현재 IR 라인 카운터 */
    int             ir_line;

    /* 결과 */
    LLVMCodegenResult *result;
    int                had_error;

    /* 내장 함수 캐시 */
    LLVMValueRef    fn_printf;
    LLVMValueRef    fn_scanf;

    /* 배열 런타임 함수 캐시 (malloc / realloc / free) */
    LLVMValueRef    fn_malloc;
    LLVMValueRef    fn_realloc;
    LLVMValueRef    fn_free;

    /* 배열 구조체 타입 캐시 (KcArray*) */
    LLVMTypeRef     ty_kc_array;    /* { i64 len, i64 cap, i8** data } */
    LLVMTypeRef     ty_kc_array_ptr;

    /* ★ goto/label 맵 (v9.2.0) — 이름 → BasicBlockRef */
#define LLVM_LABEL_MAX 256
    struct {
        char              name[128];
        LLVMBasicBlockRef bb;
    } label_map[LLVM_LABEL_MAX];
    int label_count;

    /* ★ 람다 순번 카운터 (v9.2.0) */
    int lambda_counter;

    /* ★ 클래스 레지스트리 (v6.2.0) */
#define LLVM_CLASS_MAX       64
#define LLVM_CLASS_METHOD_MAX 64
    struct {
        char         name[128];          /* 클래스 이름 */
        LLVMTypeRef  struct_ty;          /* 객체 구조체 타입 (opaque 완성 후) */
        LLVMTypeRef  vtable_ty;          /* vtable 구조체 타입 */
        LLVMValueRef vtable_global;      /* 전역 vtable 인스턴스 */
        LLVMValueRef init_fn;            /* vtable_init 함수 */
        LLVMValueRef new_fn;             /* _new 팩토리 함수 */
        /* 메서드 목록 */
        struct {
            char         name[128];
            LLVMValueRef fn;             /* LLVM 함수 값 */
        } methods[LLVM_CLASS_METHOD_MAX];
        int method_count;
        int is_valid;
    } class_reg[LLVM_CLASS_MAX];
    int class_count;
} LLVMCodegen;

/* ================================================================
 *  공개 API
 * ================================================================ */

/*
 * llvm_codegen_run() — AST 전체를 LLVM IR로 변환
 *
 * module_name : LLVM 모듈 이름 (보통 파일명)
 * 반환: LLVMCodegenResult* (호출자가 llvm_codegen_result_free()로 해제)
 *       오류 시에도 NULL 대신 had_error=1 인 결과를 반환한다.
 */
LLVMCodegenResult *llvm_codegen_run(Node *program, const char *module_name);

/*
 * llvm_codegen_result_free() — 결과 구조체 해제
 */
void llvm_codegen_result_free(LLVMCodegenResult *r);

/*
 * llvm_codegen_to_file() — IR을 .ll 파일로 저장
 * 반환: 0=성공, 비0=실패
 */
int llvm_codegen_to_file(const LLVMCodegenResult *r, const char *path);

/*
 * llvm_codegen_to_bitcode() — IR을 .bc 비트코드로 저장
 * 반환: 0=성공, 비0=실패
 */
int llvm_codegen_to_bitcode(LLVMModuleRef module, const char *path);

/*
 * llvm_codegen_to_json() — IDE 연동용 JSON 출력
 *
 * kcodegen_to_json()과 동일한 프로토콜.
 * IDE는 이 JSON을 파싱해서:
 *   "ir_text"   : IR을 에디터/패널에 표시
 *   "sourcemap" : 오류 위치를 한글 소스에 하이라이트
 *   "errors"    : 오류 패널에 표시
 *   "stats"     : 정보 패널에 표시
 *
 * JSON 구조:
 * {
 *   "success": true|false,
 *   "ir_text": "...",
 *   "sourcemap": [{"han_line":N, "han_col":N, "ir_line":N}, ...],
 *   "errors":   [{"line":N, "col":N, "msg":"..."}, ...],
 *   "stats":    {"func_count":N, "var_count":N, "ir_line_count":N}
 * }
 */
void llvm_codegen_to_json(const LLVMCodegenResult *r, FILE *out);

#endif /* KCODE_LLVM */
#endif /* KCODE_CODEGEN_LLVM_H */
