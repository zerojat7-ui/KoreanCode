/*
 * kcodegen_llvm.c  —  Kcode LLVM IR 코드 생성기 구현
 * version : v18.1.0
 *
 * v10.0.0 변경:
 *   - gen_call() 미구현 내장 함수 20종 LLVM IR 추가
 *     - I/O:       출력없이(printf 줄바꿈 없음) / 입력(fgets stdin)
 *     - 배열:      추가(kc_array_push:i8*,i64→void)
 *     - 통계 11종: 합계/평균/분산/표준편차/중앙값/최빈값/누적합/
 *                  공분산/상관계수/정규화/표준화 (kc_stat_* : i8*→double/i8*)
 *     - 배열 유틸: 배열정렬/배열뒤집기 (kc_arr_sort/reverse:i8*→void)
 *     - AI 활성함수 3종: 시그모이드/렐루/쌍곡탄젠트 (kc_sigmoid/relu/tanh_fn:double→double)
 *     - 관계 함수: 호감도 (kc_attraction:i8*,i8*→double)
 *
 * v9.9.0 변경:
 *   - gen_call() 수학/AI 내장 함수 LLVM IR 추가
 *     - 기초 수학: 절댓값(kc_abs:i64→i64) /
 *                  최대/최소(kc_max/min:vararg i64→i64) /
 *                  길이(kc_len:i8*→i64) /
 *                  범위(kc_range:i64,i64→i8*)
 *     - AI 수학:   평균제곱오차(kc_mse:i8*,i8*→double) /
 *                  교차엔트로피(kc_cross_entropy:i8*,i8*→double) /
 *                  소프트맥스(kc_softmax:i8*→i8*) /
 *                  위치인코딩(kc_positional_encoding:i64,i64→i8*)
 *     - 수열:      등비수열합(kc_geom_series:double,double→double) /
 *                  등차수열합(kc_arith_series:double,double,i64→double) /
 *                  점화식값(kc_recur_geom:double,double,i64→double)
 *     - 제곱근(sqrt:double→double) /
 *       제곱(pow:double,double→double) 중복 없이 추가
 *
 * v9.8.0 변경:
 *   - gen_call() 파일 내장 함수 17종 LLVM IR 추가
 *     - 파일열기(path,mode→i8*) / 파일닫기(fp→void) /
 *       파일읽기/파일줄읽기(fp→i8*) /
 *       파일쓰기/파일줄쓰기(fp,str→void) /
 *       파일있음(path→i1) / 파일크기(path→i64) /
 *       파일목록(path→i8*) / 파일이름(path→i8*) /
 *       파일확장자(path→i8*) / 폴더만들기(path→void) /
 *       파일지우기(path→void) / 파일복사(src,dst→void) /
 *       파일이동(src,dst→void) /
 *       파일전체읽기(path→i8*) / 파일전체쓰기(path,str→void)
 *   - gen_call() 형변환 내장 함수 3종 LLVM IR 추가
 *     - 정수(v→i64): FPToSI / PtrToInt / SExt
 *     - 실수(v→double): SIToFP / UIToFP / BitCast
 *     - 문자(v→i8*): kc_to_string(i64→i8*) 외부 함수 call
 *
 * v9.7.0 변경:
 *   - gen_call() 글자 함수 21종 LLVM IR 추가
 *     - 1단계: 자르기(kc_str_sub:i8*,i64,i64→i8*) / 분할(i8*,i8*→i8*) /
 *              합치기(i8*,i8*→i8*) / 반복글자(i8*,i64→i8*) /
 *              역순(i8*→i8*)
 *     - 2단계: 포함/시작/끝확인/반복확인(i8*,i8*→i1) /
 *              위치/비교(i8*,i8*→i64)
 *     - 3단계: 대문자/소문자/제목식(i8*→i8*) /
 *              대체/한번대체(i8*,i8*,i8*→i8*)
 *     - 4단계: 앞공백제거/뒤공백제거/공백제거(i8*→i8*)
 *     - 5단계: 반복확인(i8*,i8*→i1) / 분석(i8*→i8*) /
 *              포맷(i8*,...→i8* vararg)
 *     - 모든 kc_str_* 외부 함수 ExternalLinkage declare 후 call
 *
 * v9.5.0 변경:
 *   - 인터럽트 시스템 6종 LLVM IR 구현
 *     - NODE_SIGNAL_HANDLER (신호받기):
 *         핸들러 함수 kc_sig_handler_N(i32 signum) → void 생성
 *         signal(POSIX_SIG, handler_fn) 외부 함수 call
 *         신호 이름 → POSIX 상수 i32 값 매핑 (SIGINT=2/SIGTERM=15 등)
 *     - NODE_SIGNAL_CTRL (신호무시/신호기본/신호보내기):
 *         kc_signal_ctrl(sig: i32, action: i32) 외부 함수 call
 *         kc_kill(pid: i64, sig: i32) 외부 함수 call (신호보내기)
 *     - NODE_ISR_HANDLER (간섭 ISR):
 *         kc_isr_register(vec: i8*, handler_fn: i8*) 외부 함수 call
 *         핸들러 함수 kc_isr_N() → void 생성
 *     - NODE_ISR_CTRL (간섭잠금/간섭허용):
 *         kc_isr_lock() / kc_isr_unlock() 외부 함수 call
 *     - NODE_EVENT_HANDLER (행사등록):
 *         핸들러 함수 kc_ev_handler_N() → void 생성
 *         kc_event_register(name: i8*, handler_fn: i8*) 외부 함수 call
 *     - NODE_EVENT_CTRL (행사시작/중단/발생/해제):
 *         kc_event_loop_run/stop/emit/unregister 외부 함수 call
 *     - gen_signal_handler_ir() / gen_isr_handler_ir() /
 *       gen_event_handler_ir() 내부 헬퍼 신규
 *     - gen_stmt()에 6종 NODE case 추가
 *
 * v9.4.0 변경:
 *   - 계약 시스템 6종 LLVM IR 구현
 *     - NODE_CONTRACT (법령/법위반):
 *         법령(사전조건): 조건 gen_expr → kc_assert(cond, msg) 외부 함수 call
 *         법위반(사후조건): kc_postcond(fn_name, cond, san, msg) 외부 함수 call
 *         제재 5종: 경고/보고/중단/회귀/대체 → sanction i32 상수로 전달
 *     - NODE_CONSTITUTION (헌법): kc_constitution(cond, msg) 외부 함수 call
 *     - NODE_STATUTE (법률): kc_statute(cond, msg) 외부 함수 call
 *     - NODE_REGULATION (규정): kc_regulation(obj, cond, msg) 외부 함수 call
 *     - NODE_CHECKPOINT (복원지점): kc_checkpoint(name) 외부 함수 call
 *     - NODE_SANCTION: NODE_CONTRACT 내부 처리 (독립 case 주석만 삽입)
 *     - gen_contract_ir() / gen_contract_call_ir() 내부 헬퍼 신규
 *     - gen_stmt()에 NODE_CONTRACT / NODE_CONSTITUTION / NODE_STATUTE /
 *       NODE_REGULATION / NODE_CHECKPOINT / NODE_SANCTION case 추가
 *
 * v9.3.0 변경:
 *   - NODE_PP_STMT (#포함) LLVM IR 구현
 *     - .c/.h : 해당 파일 심볼을 런타임에 로드하는 kc_include(i8*) 외부 함수 call
 *     - .han/.hg: 주석 메타데이터만 삽입 (LLVM IR 주석 불가 → @llvm.module.flags)
 *     - .py/.js/.ts/.java: 스크립트 실행 힌트 주석 메타데이터
 *   - NODE_IMPORT (가짐) LLVM IR 구현
 *     - 내장 모듈(수학/문자열/파일시스템/시간/난수):
 *       자주 쓰이는 C 런타임 함수 declare 삽입
 *       (sin/cos/sqrt/strlen/fopen/time/srand 등)
 *     - 외부 모듈: kc_module_load(i8*) 외부 함수 call 패턴
 *     - gen_stmt()에 NODE_PP_STMT / NODE_IMPORT case 추가
 *
 * v9.2.0 변경:
 *   - NODE_GOTO / NODE_LABEL (이동/레이블) LLVM IR 구현
 *     - LLVMCodegen.label_map[] 추가 — 이름 → BasicBlockRef lazy 등록
 *     - NODE_LABEL: 레이블 블록 append → 현재 블록에서 무조건 분기 → 레이블 블록으로 이동
 *     - NODE_GOTO:  label_map 조회 후 LLVMBuildBr (forward: 빈 블록 미리 등록)
 *     - gen_stmt()에 NODE_GOTO / NODE_LABEL case 추가
 *   - NODE_LAMBDA (람다) LLVM IR 구현
 *     - lambda_counter 필드 추가 — kc_lambda_0, kc_lambda_1, ...
 *     - gen_lambda(): 람다 AST → 별도 LLVM 함수 IR 생성
 *     - gen_collect_lambdas(): gen_program() 1패스에서 AST 재귀 탐색
 *     - 표현식 gen_expr()에서 함수 포인터(i8*로 bitcast) 반환
 *   - NODE_DICT_LIT (딕셔너리 리터럴) LLVM IR 구현
 *     - kc_dict_new() / kc_dict_set(dict, key, val) 외부 함수 call 패턴
 *     - gen_dict_lit(): DICT_ENTRY 순회 → key·val gen_expr() → kc_dict_set 호출
 *     - gen_expr()에 NODE_DICT_LIT case 추가
 *
 * v9.1.0 변경:
 *   - NODE_MEMBER (멤버 접근) LLVM IR 구현
 *     - 배열.길이 → KcArray 구조체 len 필드 GEP 로드
 *     - 객체.필드 → 클래스 레지스트리에서 struct_ty 탐색 후 GEP 로드
 *     - gen_member_expr() 신규 함수 추가
 *   - NODE_SWITCH / NODE_CASE / NODE_DEFAULT (선택문) LLVM IR 구현
 *     - LLVMBuildSwitch 사용 — case 마다 basicblock 생성
 *     - default 없으면 end_bb 로 폴스루
 *   - NODE_TRY (시도/실패시/항상) LLVM IR 구현
 *     - setjmp/longjmp 외부 함수 선언
 *     - jmp_buf: [192 x i8] alloca (entry 블록)
 *     - try / catch / finally / end basicblock 구조
 *   - NODE_RAISE (오류) LLVM IR 구현
 *     - kc_raise(i8*) 외부 함수 선언 + call
 *     - raise 후 LLVMBuildUnreachable 삽입
 *
 * v9.0.0 변경:
 *   - 가속기 NPU 경로 LLVM IR 완전 구현
 *     - Python onnxruntime 스크립트 자동생성 IR (fopen/fputs/fclose 패턴)
 *     - 5종 연산 ONNX 그래프: MatMul/Add/Relu/Transpose/Conv
 *     - do_npu 플래그 추가 — GPU→NPU→CPU 폴백 체인 완성
 *     - 결과 수집: fopen(py_out,"r") → fread → 반환변수 alloca store
 *
 * v8.2.0 변경:
 *   - 가속기 블록 LLVM IR 완전 구현 (NODE_GPU_BLOCK / NODE_GPU_OP)
 *     - GPU: CUDA C 파일 자동 생성 (fopen/fputs/fclose) → nvcc 컴파일 → 실행
 *       - CUDA 커널 5종 IR 문자열: 행렬곱/행렬합/합성곱/활성화/전치
 *       - 입력 변수 → setenv() CSV 직렬화, 결과 → fread → 반환 변수 store
 *     - CPU: #ifdef _OPENMP / system("gcc -fopenmp") 위임
 *     - 폴백: nvcc 실패 시 CPU OpenMP 경로
 *     - gen_gpu_block_ir() 내부 헬퍼 신규 — 기존 9종 헬퍼 완전 재활용
 *     - remove() 로 임시 .cu/.log/.out 파일 삭제
 *
 * v8.1.0 변경:
 *   - 스크립트 블록 LLVM IR 완전 구현 (NODE_SCRIPT_PYTHON/JAVA/JS)
 *     - 스크립트 원문: fopen/fputs/fclose 로 임시 파일 작성
 *     - 전달 변수: setenv() 로 환경변수 설정 (snprintf 직렬화)
 *     - Python/JS: system("python3|node path > out") call
 *     - Java: system("mkdir + javac + java") 파이프라인
 *     - 반환 변수: fopen/fread/fclose 로 stdout 수집 후 store
 *     - remove() 로 임시 파일 삭제
 *   - 스크립트 블록용 C 라이브러리 함수 선언 헬퍼 9종 추가
 *     (system/setenv/remove/snprintf/fopen/fputs/fclose/fread/getpid)
 *
 * v6.2.0 변경:
 *   - 객체(CLASS) LLVM IR 완전 구현
 *     - gen_class_decl() 신규: vtable struct 타입, 전역 vtable 인스턴스,
 *       객체 struct 타입, 메서드 함수 IR, vtable_init(), _new() 팩토리
 *     - gen_program() 1단계에서 NODE_CLASS_DECL 처리 (전방 선언)
 *     - gen_program() 2단계 main에서 CLASS_DECL 건너뜀
 *     - gen_stmt()에 NODE_CLASS_DECL case 추가
 *
 * v3.7.1:
 *
 * MIT License
 * zerojat7
 */

#ifdef KCODE_LLVM

#define _POSIX_C_SOURCE 200809L

#include "kcodegen_llvm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ================================================================
 *  내부 헬퍼 매크로
 * ================================================================ */
#define CG_ERROR(cg, node, fmt, ...) \
    do { \
        llvm_cg_error((cg), (node) ? (node)->line : 0, \
                             (node) ? (node)->col  : 0, fmt, ##__VA_ARGS__); \
    } while(0)

/* ================================================================
 *  전방 선언
 * ================================================================ */
static LLVMValueRef gen_expr(LLVMCodegen *cg, Node *n);
static void         gen_stmt(LLVMCodegen *cg, Node *n);
static void         gen_block(LLVMCodegen *cg, Node *n);
static int          block_is_open(LLVMCodegen *cg);
static LLVMValueRef gen_member_expr(LLVMCodegen *cg, Node *n);
static LLVMValueRef gen_lambda(LLVMCodegen *cg, Node *n);
static LLVMValueRef gen_dict_lit(LLVMCodegen *cg, Node *n);
static void         gen_collect_lambdas(LLVMCodegen *cg, Node *n);

/* ================================================================
 *  오류 기록
 * ================================================================ */
static void llvm_cg_error(LLVMCodegen *cg, int line, int col,
                           const char *fmt, ...)
{
    cg->had_error = 1;
    cg->result->had_error = 1;
    if (cg->result->error_count >= LLVM_CODEGEN_MAX_ERRORS) return;

    LLVMCodegenError *e = &cg->result->errors[cg->result->error_count++];
    e->line = line;
    e->col  = col;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
    va_end(ap);

    fprintf(stderr, "[LLVM 코드 생성 오류] %d:%d: %s\n", line, col, e->msg);
}

/* ================================================================
 *  소스맵 기록
 *  구문 생성 시작 시점에 호출 — .han 라인과 현재 ir_line 을 매핑
 * ================================================================ */
static void sourcemap_add(LLVMCodegen *cg, int han_line, int han_col) {
    LLVMCodegenResult *r = cg->result;
    if (r->sourcemap_count >= LLVM_CODEGEN_MAX_SOURCEMAP) return;
    LLVMSourceMapEntry *e = &r->sourcemap[r->sourcemap_count++];
    e->han_line = han_line;
    e->han_col  = han_col;
    e->ir_line  = cg->ir_line;
}

/* ================================================================
 *  스코프 관리
 * ================================================================ */
static LLVMScope *scope_push(LLVMCodegen *cg)
{
    LLVMScope *s = calloc(1, sizeof(LLVMScope));
    s->parent    = cg->scope;
    cg->scope    = s;
    return s;
}

static void scope_pop(LLVMCodegen *cg)
{
    LLVMScope *s = cg->scope;
    if (!s) return;
    cg->scope = s->parent;
    free(s);
}

static void scope_set(LLVMCodegen *cg, const char *name,
                      LLVMValueRef alloca, LLVMTypeRef type)
{
    LLVMScope *s = cg->scope;
    if (!s || s->count >= LLVM_SYM_MAX) return;
    LLVMSymbol *sym = &s->syms[s->count++];
    strncpy(sym->name, name, sizeof(sym->name) - 1);
    sym->alloca = alloca;
    sym->type   = type;
}

static LLVMSymbol *scope_lookup(LLVMCodegen *cg, const char *name)
{
    for (LLVMScope *s = cg->scope; s; s = s->parent) {
        for (int i = 0; i < s->count; i++) {
            if (strcmp(s->syms[i].name, name) == 0)
                return &s->syms[i];
        }
    }
    return NULL;
}

/* ================================================================
 *  goto/label 맵 헬퍼 (v9.2.0)
 *  label_map: 이름 → BasicBlockRef 매핑
 *  - label_map_get_or_create(): 이름으로 bb 검색, 없으면 새 bb 생성 후 등록
 *    (forward goto 지원 — goto가 label보다 먼저 나와도 정상 동작)
 * ================================================================ */
static LLVMBasicBlockRef label_map_get_or_create(LLVMCodegen *cg,
                                                   const char  *name)
{
    /* 이미 등록된 레이블 탐색 */
    for (int i = 0; i < cg->label_count; i++) {
        if (strcmp(cg->label_map[i].name, name) == 0)
            return cg->label_map[i].bb;
    }
    /* 없으면 새 BasicBlock 생성 후 등록 */
    if (cg->label_count >= LLVM_LABEL_MAX) return NULL;
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(
        cg->ctx, cg->cur_func, name);
    strncpy(cg->label_map[cg->label_count].name, name,
            sizeof(cg->label_map[0].name) - 1);
    cg->label_map[cg->label_count].bb = bb;
    cg->label_count++;
    return bb;
}


static void loop_push(LLVMCodegen *cg,
                      LLVMBasicBlockRef brk, LLVMBasicBlockRef cont)
{
    if (cg->loop_depth >= LLVM_LOOP_MAX) return;
    cg->loop_stack[cg->loop_depth].break_bb    = brk;
    cg->loop_stack[cg->loop_depth].continue_bb = cont;
    cg->loop_depth++;
}

static void loop_pop(LLVMCodegen *cg) {
    if (cg->loop_depth > 0) cg->loop_depth--;
}

static LLVMLoopCtx *loop_top(LLVMCodegen *cg) {
    if (cg->loop_depth == 0) return NULL;
    return &cg->loop_stack[cg->loop_depth - 1];
}

/* ================================================================
 *  LLVM 타입 변환 — Kcode 자료형 토큰 → LLVMTypeRef
 * ================================================================ */
static LLVMTypeRef ktype_to_llvm(LLVMCodegen *cg, TokenType dtype)
{
    switch (dtype) {
        case TOK_KW_JEONGSU:    return LLVMInt64TypeInContext(cg->ctx);
        case TOK_KW_SILSU:      return LLVMDoubleTypeInContext(cg->ctx);
        case TOK_KW_NOLI:       return LLVMInt1TypeInContext(cg->ctx);
        case TOK_KW_MUNJA:      return LLVMInt32TypeInContext(cg->ctx);  /* 문자 (char, UTF-32) */
        case TOK_KW_GEULJA:     return LLVMPointerType(                  /* 글자 (string, i8*) */
                                           LLVMInt8TypeInContext(cg->ctx), 0);
        case TOK_KW_EOPSEUM:    return LLVMVoidTypeInContext(cg->ctx);
        default:                return LLVMInt64TypeInContext(cg->ctx);  /* 기본값: 정수 */
    }
}

/* ================================================================
 *  내장 함수 선언 캐시 — printf
 * ================================================================ */
static LLVMValueRef get_printf(LLVMCodegen *cg)
{
    if (cg->fn_printf) return cg->fn_printf;

    LLVMTypeRef  param_types[] = {
        LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)
    };
    LLVMTypeRef fn_type = LLVMFunctionType(
        LLVMInt32TypeInContext(cg->ctx),
        param_types, 1, /* isVarArg = */ 1
    );
    cg->fn_printf = LLVMAddFunction(cg->module, "printf", fn_type);
    LLVMSetLinkage(cg->fn_printf, LLVMExternalLinkage);
    return cg->fn_printf;
}

/* ================================================================
 *  전역 문자열 상수 생성 (출력 포맷 등)
 * ================================================================ */
static LLVMValueRef make_global_str(LLVMCodegen *cg,
                                    const char *str, const char *name)
{
    LLVMValueRef gs = LLVMAddGlobal(
        cg->module,
        LLVMArrayType(LLVMInt8TypeInContext(cg->ctx),
                      (unsigned)(strlen(str) + 1)),
        name
    );
    LLVMSetInitializer(gs, LLVMConstString(str, (unsigned)strlen(str), 0));
    LLVMSetGlobalConstant(gs, 1);
    LLVMSetLinkage(gs, LLVMPrivateLinkage);

    /* GEP(i8* 포인터로 변환) */
    LLVMValueRef indices[2] = {
        LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0),
        LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0)
    };
    return LLVMBuildGEP2(
        cg->builder,
        LLVMArrayType(LLVMInt8TypeInContext(cg->ctx),
                      (unsigned)(strlen(str) + 1)),
        gs, indices, 2, "str_ptr"
    );
}

/* ================================================================
 *  배열 런타임 헬퍼
 *
 *  LLVM IR 레벨의 KcArray 구조체:
 *    %KcArray = type { i64, i64, i8** }
 *                     len  cap  data
 *
 *  배열 원소는 모두 i64로 저장한다.
 *  (인터프리터와 달리 LLVM 백엔드는 정수 원소 배열에 집중)
 * ================================================================ */

/* KcArray 구조체 타입을 한 번만 생성하여 캐시 */
static LLVMTypeRef get_kc_array_type(LLVMCodegen *cg)
{
    if (cg->ty_kc_array) return cg->ty_kc_array;

    LLVMTypeRef i64  = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i64p = LLVMPointerType(i64, 0);  /* i64* — 원소 배열 */

    /* { len: i64, cap: i64, data: i64* } */
    LLVMTypeRef fields[3] = { i64, i64, i64p };
    cg->ty_kc_array     = LLVMStructTypeInContext(cg->ctx, fields, 3, 0);
    cg->ty_kc_array_ptr = LLVMPointerType(cg->ty_kc_array, 0);
    (void)i8p;
    return cg->ty_kc_array;
}

/* malloc 선언 캐시 */
static LLVMValueRef get_malloc(LLVMCodegen *cg)
{
    if (cg->fn_malloc) return cg->fn_malloc;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef fn_t = LLVMFunctionType(i8p, &i64, 1, 0);
    cg->fn_malloc = LLVMAddFunction(cg->module, "malloc", fn_t);
    LLVMSetLinkage(cg->fn_malloc, LLVMExternalLinkage);
    return cg->fn_malloc;
}

/* realloc 선언 캐시 */
static LLVMValueRef get_realloc(LLVMCodegen *cg)
{
    if (cg->fn_realloc) return cg->fn_realloc;
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i64  = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef params[2] = { i8p, i64 };
    LLVMTypeRef fn_t = LLVMFunctionType(i8p, params, 2, 0);
    cg->fn_realloc = LLVMAddFunction(cg->module, "realloc", fn_t);
    LLVMSetLinkage(cg->fn_realloc, LLVMExternalLinkage);
    return cg->fn_realloc;
}

/* free 선언 캐시 */
static LLVMValueRef get_free_fn(LLVMCodegen *cg)
{
    if (cg->fn_free) return cg->fn_free;
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef fn_t = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
    cg->fn_free = LLVMAddFunction(cg->module, "free", fn_t);
    LLVMSetLinkage(cg->fn_free, LLVMExternalLinkage);
    return cg->fn_free;
}

/* ================================================================
 *  스크립트 블록용 C 표준 라이브러리 함수 선언 헬퍼 (v8.1.0)
 *  system / setenv / snprintf / fopen / fputs / fclose /
 *  fread / remove — 각 함수를 모듈에 한 번만 선언하고 캐시한다.
 * ================================================================ */

/* system(const char*) → int */
static LLVMValueRef get_system_fn(LLVMCodegen *cg)
{
    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "system");
    if (fn) return fn;
    LLVMTypeRef i8p    = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i32    = LLVMInt32TypeInContext(cg->ctx);
    LLVMTypeRef fn_t   = LLVMFunctionType(i32, &i8p, 1, 0);
    fn = LLVMAddFunction(cg->module, "system", fn_t);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
}

/* setenv(const char*, const char*, int) → int */
static LLVMValueRef get_setenv_fn(LLVMCodegen *cg)
{
    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "setenv");
    if (fn) return fn;
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
    LLVMTypeRef ps[] = { i8p, i8p, i32 };
    LLVMTypeRef fn_t = LLVMFunctionType(i32, ps, 3, 0);
    fn = LLVMAddFunction(cg->module, "setenv", fn_t);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
}

/* remove(const char*) → int */
static LLVMValueRef get_remove_fn(LLVMCodegen *cg)
{
    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "remove");
    if (fn) return fn;
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
    LLVMTypeRef fn_t = LLVMFunctionType(i32, &i8p, 1, 0);
    fn = LLVMAddFunction(cg->module, "remove", fn_t);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
}

/* snprintf(char*, size_t, const char*, ...) → int (variadic) */
static LLVMValueRef get_snprintf_fn(LLVMCodegen *cg)
{
    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "snprintf");
    if (fn) return fn;
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i64  = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
    LLVMTypeRef ps[] = { i8p, i64, i8p };
    LLVMTypeRef fn_t = LLVMFunctionType(i32, ps, 3, /*isVarArg*/1);
    fn = LLVMAddFunction(cg->module, "snprintf", fn_t);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
}

/* fopen(const char*, const char*) → i8* (FILE*) */
static LLVMValueRef get_fopen_fn(LLVMCodegen *cg)
{
    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "fopen");
    if (fn) return fn;
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef ps[] = { i8p, i8p };
    LLVMTypeRef fn_t = LLVMFunctionType(i8p, ps, 2, 0);
    fn = LLVMAddFunction(cg->module, "fopen", fn_t);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
}

/* fputs(const char*, FILE*) → int */
static LLVMValueRef get_fputs_fn(LLVMCodegen *cg)
{
    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "fputs");
    if (fn) return fn;
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
    LLVMTypeRef ps[] = { i8p, i8p };
    LLVMTypeRef fn_t = LLVMFunctionType(i32, ps, 2, 0);
    fn = LLVMAddFunction(cg->module, "fputs", fn_t);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
}

/* fclose(FILE*) → int */
static LLVMValueRef get_fclose_fn(LLVMCodegen *cg)
{
    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "fclose");
    if (fn) return fn;
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
    LLVMTypeRef fn_t = LLVMFunctionType(i32, &i8p, 1, 0);
    fn = LLVMAddFunction(cg->module, "fclose", fn_t);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
}

/* fread(void*, size_t, size_t, FILE*) → size_t */
static LLVMValueRef get_fread_fn(LLVMCodegen *cg)
{
    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "fread");
    if (fn) return fn;
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i64  = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef ps[] = { i8p, i64, i64, i8p };
    LLVMTypeRef fn_t = LLVMFunctionType(i64, ps, 4, 0);
    fn = LLVMAddFunction(cg->module, "fread", fn_t);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
}

/* getpid() → i32 (POSIX) */
static LLVMValueRef get_getpid_fn(LLVMCodegen *cg)
{
    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "getpid");
    if (fn) return fn;
    LLVMTypeRef fn_t = LLVMFunctionType(
        LLVMInt32TypeInContext(cg->ctx), NULL, 0, 0);
    fn = LLVMAddFunction(cg->module, "getpid", fn_t);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
}

/*
 * build_array_new() — 빈 KcArray* 를 힙에 할당하고 포인터 반환
 *
 *   %raw = call i8* @malloc(i64 sizeof(KcArray))
 *   %arr = bitcast i8* %raw to %KcArray*
 *   ; len = 0, cap = 0, data = null 초기화
 */
static LLVMValueRef build_array_new(LLVMCodegen *cg)
{
    LLVMTypeRef arr_t  = get_kc_array_type(cg);
    LLVMTypeRef arr_pt = LLVMPointerType(arr_t, 0);
    LLVMTypeRef i64    = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef i8p    = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);

    /* malloc(sizeof(KcArray)) */
    LLVMValueRef sz   = LLVMSizeOf(arr_t);           /* i64 sizeof */
    LLVMValueRef raw  = LLVMBuildCall2(cg->builder,
        LLVMFunctionType(i8p, &i64, 1, 0),
        get_malloc(cg), &sz, 1, "arr_raw");
    LLVMValueRef arr  = LLVMBuildBitCast(cg->builder, raw, arr_pt, "arr");

    /* len = 0 */
    LLVMValueRef gep_len = LLVMBuildStructGEP2(cg->builder, arr_t, arr, 0, "gep_len");
    LLVMBuildStore(cg->builder, LLVMConstInt(i64, 0, 0), gep_len);

    /* cap = 0 */
    LLVMValueRef gep_cap = LLVMBuildStructGEP2(cg->builder, arr_t, arr, 1, "gep_cap");
    LLVMBuildStore(cg->builder, LLVMConstInt(i64, 0, 0), gep_cap);

    /* data = null */
    LLVMTypeRef i64p  = LLVMPointerType(i64, 0);
    LLVMValueRef gep_data = LLVMBuildStructGEP2(cg->builder, arr_t, arr, 2, "gep_data");
    LLVMBuildStore(cg->builder, LLVMConstPointerNull(i64p), gep_data);

    return arr;
}

/*
 * build_array_push() — KcArray* 에 i64 원소를 추가
 *
 *   if len >= cap: realloc(data, (cap*2+1) * 8)
 *   data[len] = val
 *   len++
 */
static void build_array_push(LLVMCodegen *cg,
                              LLVMValueRef arr, LLVMValueRef val)
{
    LLVMTypeRef  arr_t   = get_kc_array_type(cg);
    LLVMTypeRef  i64     = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef  i8p     = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef  i64p    = LLVMPointerType(i64, 0);
    LLVMValueRef fn      = cg->cur_func;

    /* 현재 len, cap, data 읽기 */
    LLVMValueRef gep_len  = LLVMBuildStructGEP2(cg->builder, arr_t, arr, 0, "gep_len");
    LLVMValueRef gep_cap  = LLVMBuildStructGEP2(cg->builder, arr_t, arr, 1, "gep_cap");
    LLVMValueRef gep_data = LLVMBuildStructGEP2(cg->builder, arr_t, arr, 2, "gep_data");

    LLVMValueRef cur_len  = LLVMBuildLoad2(cg->builder, i64, gep_len,  "cur_len");
    LLVMValueRef cur_cap  = LLVMBuildLoad2(cg->builder, i64, gep_cap,  "cur_cap");
    LLVMValueRef cur_data = LLVMBuildLoad2(cg->builder, i64p, gep_data, "cur_data");

    /* need_grow = len >= cap */
    LLVMValueRef need_grow = LLVMBuildICmp(cg->builder, LLVMIntSGE,
                                            cur_len, cur_cap, "need_grow");

    LLVMBasicBlockRef grow_bb  = LLVMAppendBasicBlockInContext(cg->ctx, fn, "arr.grow");
    LLVMBasicBlockRef cont_bb  = LLVMAppendBasicBlockInContext(cg->ctx, fn, "arr.push");
    LLVMBuildCondBr(cg->builder, need_grow, grow_bb, cont_bb);

    /* ── grow 블록 ────────────────────── */
    LLVMPositionBuilderAtEnd(cg->builder, grow_bb);

    /* new_cap = cap * 2 + 1 */
    LLVMValueRef new_cap = LLVMBuildAdd(cg->builder,
        LLVMBuildMul(cg->builder, cur_cap, LLVMConstInt(i64, 2, 0), "cap2"),
        LLVMConstInt(i64, 1, 0), "new_cap");

    /* new_sz = new_cap * sizeof(i64) = new_cap * 8 */
    LLVMValueRef new_sz  = LLVMBuildMul(cg->builder, new_cap,
                               LLVMConstInt(i64, 8, 0), "new_sz");

    /* realloc(data_as_i8p, new_sz) */
    LLVMValueRef data_i8p = LLVMBuildBitCast(cg->builder, cur_data, i8p, "data_i8p");
    LLVMTypeRef  rc_params[2] = { i8p, i64 };
    LLVMTypeRef  rc_t = LLVMFunctionType(i8p, rc_params, 2, 0);
    LLVMValueRef rc_args[2]   = { data_i8p, new_sz };
    LLVMValueRef new_raw  = LLVMBuildCall2(cg->builder, rc_t,
                                get_realloc(cg), rc_args, 2, "new_raw");
    LLVMValueRef new_data = LLVMBuildBitCast(cg->builder, new_raw, i64p, "new_data");

    LLVMBuildStore(cg->builder, new_cap,  gep_cap);
    LLVMBuildStore(cg->builder, new_data, gep_data);
    LLVMBuildBr(cg->builder, cont_bb);

    /* ── push 블록 ────────────────────── */
    LLVMPositionBuilderAtEnd(cg->builder, cont_bb);

    /* PHI로 최신 data 선택 */
    LLVMValueRef phi_data = LLVMBuildPhi(cg->builder, i64p, "phi_data");
    LLVMValueRef phi_inc[2] = { cur_data, new_data };
    LLVMBasicBlockRef phi_bb[2] = {
        LLVMGetPreviousBasicBlock(grow_bb),  /* before grow */
        grow_bb
    };
    /* 앞 블록은 need_grow 평가 블록 */
    LLVMValueRef from_before = cur_data;
    LLVMBasicBlockRef bb_before = LLVMGetPreviousBasicBlock(grow_bb);
    LLVMAddIncoming(phi_data, &from_before, &bb_before, 1);
    LLVMAddIncoming(phi_data, &new_data,    &grow_bb,   1);
    (void)phi_inc; (void)phi_bb;

    /* data[len] = val */
    LLVMValueRef slot = LLVMBuildGEP2(cg->builder, i64, phi_data, &cur_len, 1, "slot");
    LLVMBuildStore(cg->builder, val, slot);

    /* len++ */
    LLVMValueRef new_len = LLVMBuildAdd(cg->builder, cur_len,
                                         LLVMConstInt(i64, 1, 0), "new_len");
    LLVMBuildStore(cg->builder, new_len, gep_len);
}

/*
 * build_array_len() — KcArray*.len 읽기 → i64
 */
static LLVMValueRef build_array_len(LLVMCodegen *cg, LLVMValueRef arr)
{
    LLVMTypeRef arr_t   = get_kc_array_type(cg);
    LLVMTypeRef i64     = LLVMInt64TypeInContext(cg->ctx);
    LLVMValueRef gep    = LLVMBuildStructGEP2(cg->builder, arr_t, arr, 0, "gep_len");
    return LLVMBuildLoad2(cg->builder, i64, gep, "arr_len");
}

/*
 * build_array_get() — KcArray*.data[idx] → i64
 */
static LLVMValueRef build_array_get(LLVMCodegen *cg,
                                     LLVMValueRef arr, LLVMValueRef idx)
{
    LLVMTypeRef arr_t   = get_kc_array_type(cg);
    LLVMTypeRef i64     = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef i64p    = LLVMPointerType(i64, 0);
    LLVMValueRef gep_d  = LLVMBuildStructGEP2(cg->builder, arr_t, arr, 2, "gep_data");
    LLVMValueRef data   = LLVMBuildLoad2(cg->builder, i64p, gep_d, "data");
    LLVMValueRef slot   = LLVMBuildGEP2(cg->builder, i64, data, &idx, 1, "elem_ptr");
    return LLVMBuildLoad2(cg->builder, i64, slot, "elem");
}

/*
 * build_array_set() — KcArray*.data[idx] = val
 */
static void build_array_set(LLVMCodegen *cg,
                             LLVMValueRef arr, LLVMValueRef idx,
                             LLVMValueRef val)
{
    LLVMTypeRef arr_t   = get_kc_array_type(cg);
    LLVMTypeRef i64     = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef i64p    = LLVMPointerType(i64, 0);
    LLVMValueRef gep_d  = LLVMBuildStructGEP2(cg->builder, arr_t, arr, 2, "gep_data");
    LLVMValueRef data   = LLVMBuildLoad2(cg->builder, i64p, gep_d, "data");
    LLVMValueRef slot   = LLVMBuildGEP2(cg->builder, i64, data, &idx, 1, "elem_ptr");
    LLVMBuildStore(cg->builder, val, slot);
}

/* ================================================================
 *  배열 리터럴: [v0, v1, ...]
 *  → 빈 배열 생성 후 원소마다 push
 * ================================================================ */
static LLVMValueRef gen_array_lit(LLVMCodegen *cg, Node *n)
{
    LLVMValueRef arr = build_array_new(cg);
    LLVMTypeRef  i64 = LLVMInt64TypeInContext(cg->ctx);

    for (int i = 0; i < n->child_count; i++) {
        LLVMValueRef elem = gen_expr(cg, n->children[i]);
        if (!elem) elem = LLVMConstInt(i64, 0, 0);

        /* 원소를 i64로 강제 변환 */
        LLVMTypeKind k = LLVMGetTypeKind(LLVMTypeOf(elem));
        if (k == LLVMDoubleTypeKind)
            elem = LLVMBuildFPToSI(cg->builder, elem, i64, "f2i");
        else if (k == LLVMIntegerTypeKind &&
                 LLVMGetIntTypeWidth(LLVMTypeOf(elem)) < 64)
            elem = LLVMBuildSExt(cg->builder, elem, i64, "ext");

        build_array_push(cg, arr, elem);
    }
    return arr;
}

/* ================================================================
 *  멤버 접근: 객체.멤버 / 배열.길이  (v9.1.0)
 *
 *  - 배열.길이  → KcArray 구조체 필드 [0] (len) 로드
 *  - 객체.필드  → 클래스 레지스트리에서 필드 인덱스 탐색 후 GEP 로드
 * ================================================================ */
static LLVMValueRef gen_member_expr(LLVMCodegen *cg, Node *n)
{
    if (!n || n->child_count < 1) {
        CG_ERROR(cg, n, "멤버 접근: 피연산자 없음");
        return LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0);
    }
    const char  *mname = n->sval ? n->sval : "";
    LLVMTypeRef  i64   = LLVMInt64TypeInContext(cg->ctx);

    /* ── 배열.길이 ── */
    /* UTF-8: 길이 = EA B8 B8 EC 9D B4 */
    if (strcmp(mname, "\xEA\xB8\xB8\xEC\x9D\xB4") == 0) {
        LLVMValueRef arr = gen_expr(cg, n->children[0]);
        if (!arr) {
            CG_ERROR(cg, n, "멤버.길이: 배열 표현식 생성 실패");
            return LLVMConstInt(i64, 0, 0);
        }
        LLVMTypeRef arr_t  = get_kc_array_type(cg);
        LLVMValueRef gep   = LLVMBuildStructGEP2(cg->builder, arr_t, arr, 0, "arr_len_ptr");
        return LLVMBuildLoad2(cg->builder, i64, gep, "arr_len");
    }

    /* ── 객체.필드 — 클래스 레지스트리에서 필드 인덱스 탐색 ── */
    LLVMValueRef obj = gen_expr(cg, n->children[0]);
    if (!obj) {
        CG_ERROR(cg, n, "멤버 접근: 객체 표현식 생성 실패 (멤버: %s)", mname);
        return LLVMConstInt(i64, 0, 0);
    }

    /* 포인터 타입에서 pointee 구조체 찾기 */
    LLVMTypeRef obj_ty = LLVMTypeOf(obj);
    if (LLVMGetTypeKind(obj_ty) == LLVMPointerTypeKind)
        obj_ty = LLVMGetElementType(obj_ty);

    /* 클래스 레지스트리 순회 — 같은 struct_ty 찾기 */
    for (int ci = 0; ci < cg->class_count; ci++) {
        if (!cg->class_reg[ci].is_valid) continue;
        LLVMTypeRef st = cg->class_reg[ci].struct_ty;
        if (st != obj_ty) continue;

        /* 구조체 필드 인덱스 탐색:
         *  index 0 = vtable 포인터 (건너뜀)
         *  index 1.. = 실제 필드 */
        unsigned fc = LLVMCountStructElementTypes(st);
        for (unsigned fi = 1; fi < fc; fi++) {
            /* 필드 이름은 LLVM IR 수준에서 없으므로
             * 선언 순서대로 (fi-1)번째 VAR_DECL/CONST_DECL 이름과 비교 */
            /* 이름 매칭은 mname을 "kc_멤버명" 변환 없이 그냥 비교
             * — kcodegen.c 패턴과 동일: sval 그대로 */
            char field_ir_name[160];
            snprintf(field_ir_name, sizeof(field_ir_name),
                     "kc_%s_f%u", cg->class_reg[ci].name, fi - 1);
            LLVMValueRef gep = LLVMBuildStructGEP2(
                cg->builder, st, obj, fi, field_ir_name);
            LLVMTypeRef ft = LLVMStructGetTypeAtIndex(st, fi);
            /* 첫 번째 필드를 반환 (단일 필드 접근 시 정확하지만
             * 다중 필드는 인터프리터 런타임에서 처리 — LLVM 백엔드는
             * 필드 이름 정보가 AST에만 있으므로 fi=1 부터 순차 탐색) */
            (void)mname; /* 현재는 첫 비-vtable 필드 반환; 향후 이름 맵 추가 */
            return LLVMBuildLoad2(cg->builder, ft, gep, "member");
        }
    }

    /* 클래스 정보 없음 — 0 반환 + 경고 */
    CG_ERROR(cg, n, "멤버 접근: 클래스 정보를 찾을 수 없음 (멤버: %s)", mname);
    return LLVMConstInt(i64, 0, 0);
}

/* ================================================================
 *  람다 함수 IR 생성 (v9.2.0)
 *
 *  NODE_LAMBDA:
 *    child[0..n-1] = NODE_PARAM  (매개변수)
 *    child[n]      = 표현식/블록  (몸체)
 *
 *  전략:
 *    - 모듈 레벨에 kc_lambda_N 함수 추가 (모든 매개변수는 i64로 단순화)
 *    - 몸체 표현식 평가 후 반환
 *    - 표현식 컨텍스트에서는 함수 포인터(i8*로 bitcast)를 값으로 반환
 * ================================================================ */
static LLVMValueRef gen_lambda(LLVMCodegen *cg, Node *n)
{
    if (!n) return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0));

    /* 람다 번호 할당 */
    int lnum = cg->lambda_counter++;

    /* 매개변수 수 계산 (마지막 child = 몸체이므로 child_count-1) */
    int param_count = (n->child_count > 0) ? n->child_count - 1 : 0;
    int body_idx    = param_count;   /* 마지막 child = 몸체 */

    LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);

    /* 모든 매개변수 타입을 i64로 단순화 */
    LLVMTypeRef *param_types = NULL;
    if (param_count > 0) {
        param_types = malloc((size_t)param_count * sizeof(LLVMTypeRef));
        for (int i = 0; i < param_count; i++) param_types[i] = i64;
    }

    /* 함수 타입 — 반환값 i64 */
    LLVMTypeRef fn_type = LLVMFunctionType(i64,
        param_types, (unsigned)param_count, 0);
    free(param_types);

    /* 함수 이름 */
    char fn_name[64];
    snprintf(fn_name, sizeof(fn_name), "kc_lambda_%d", lnum);

    /* 이미 선언된 경우 재사용 */
    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, fn_name);
    if (!fn) {
        fn = LLVMAddFunction(cg->module, fn_name, fn_type);
        LLVMSetLinkage(fn, LLVMPrivateLinkage);  /* static */
    }

    /* 현재 컨텍스트 저장 */
    LLVMValueRef    saved_func         = cg->cur_func;
    LLVMTypeRef     saved_ret_type     = cg->cur_func_ret_type;
    int             saved_is_void      = cg->cur_func_is_void;
    int             saved_label_count  = cg->label_count;

    /* 람다 함수 몸체 생성 */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
        cg->ctx, fn, "lambda_entry");
    LLVMPositionBuilderAtEnd(cg->builder, entry);

    cg->cur_func          = fn;
    cg->cur_func_ret_type = i64;
    cg->cur_func_is_void  = 0;
    cg->label_count       = 0;  /* 람다 내부 레이블 초기화 */

    scope_push(cg);

    /* 매개변수를 alloca로 등록 */
    for (int i = 0; i < param_count; i++) {
        Node *p = n->children[i];
        const char *pname = p ? (p->sval ? p->sval : "_p") : "_p";
        LLVMValueRef pval = LLVMGetParam(fn, (unsigned)i);
        LLVMValueRef pa   = LLVMBuildAlloca(cg->builder, i64, pname);
        LLVMBuildStore(cg->builder, pval, pa);
        scope_set(cg, pname, pa, i64);
    }

    /* 몸체 평가 */
    LLVMValueRef ret_val = NULL;
    if (body_idx < n->child_count) {
        Node *body = n->children[body_idx];
        if (body->type == NODE_BLOCK) {
            /* 블록형 람다 */
            for (int i = 0; i < body->child_count && !cg->had_error; i++) {
                if (!block_is_open(cg)) break;
                gen_stmt(cg, body->children[i]);
            }
        } else {
            /* 표현식형 람다 — 결과를 반환값으로 */
            ret_val = gen_expr(cg, body);
        }
    }

    /* 반환 명령어 삽입 */
    if (block_is_open(cg)) {
        if (ret_val) {
            /* 타입 통일 (double → i64) */
            if (LLVMGetTypeKind(LLVMTypeOf(ret_val)) == LLVMDoubleTypeKind)
                ret_val = LLVMBuildFPToSI(cg->builder, ret_val, i64, "lret_cast");
            else if (LLVMGetTypeKind(LLVMTypeOf(ret_val)) == LLVMIntegerTypeKind &&
                     LLVMGetIntTypeWidth(LLVMTypeOf(ret_val)) != 64)
                ret_val = LLVMBuildSExt(cg->builder, ret_val, i64, "lret_ext");
            LLVMBuildRet(cg->builder, ret_val);
        } else {
            LLVMBuildRet(cg->builder, LLVMConstInt(i64, 0, 0));
        }
    }

    scope_pop(cg);

    /* 컨텍스트 복원 */
    cg->cur_func          = saved_func;
    cg->cur_func_ret_type = saved_ret_type;
    cg->cur_func_is_void  = saved_is_void;
    cg->label_count       = saved_label_count;

    /* 이전 블록으로 빌더 복귀 */
    if (saved_func) {
        LLVMBasicBlockRef last_bb = LLVMGetLastBasicBlock(saved_func);
        if (last_bb) LLVMPositionBuilderAtEnd(cg->builder, last_bb);
    }

    /* 함수 포인터를 i8* 로 bitcast 해서 반환 (값으로 전달 가능) */
    return LLVMBuildBitCast(cg->builder, fn, i8p, "lambda_ptr");
}

/* ================================================================
 *  딕셔너리 리터럴 IR 생성 (v9.2.0)
 *
 *  NODE_DICT_LIT:  child[0..] = NODE_DICT_ENTRY
 *  NODE_DICT_ENTRY: child[0]=키, child[1]=값
 *
 *  전략:
 *    - kc_dict_new()  : () → i8*           외부 함수 call
 *    - kc_dict_set()  : (i8*, i8*, i64) → void  외부 함수 call
 *    - 키는 글자(i8*) 또는 정수 → snprintf 없이 직접 전달
 *    - 생성된 dict 핸들(i8*)을 반환
 * ================================================================ */
static LLVMValueRef gen_dict_lit(LLVMCodegen *cg, Node *n)
{
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);

    /* kc_dict_new() 선언 — () → i8* */
    LLVMValueRef fn_new = LLVMGetNamedFunction(cg->module, "kc_dict_new");
    if (!fn_new) {
        LLVMTypeRef ft = LLVMFunctionType(i8p, NULL, 0, 0);
        fn_new = LLVMAddFunction(cg->module, "kc_dict_new", ft);
        LLVMSetLinkage(fn_new, LLVMExternalLinkage);
    }

    /* kc_dict_set(dict: i8*, key: i8*, val: i64) → void 선언 */
    LLVMValueRef fn_set = LLVMGetNamedFunction(cg->module, "kc_dict_set");
    if (!fn_set) {
        LLVMTypeRef params[3] = { i8p, i8p, i64 };
        LLVMTypeRef ft = LLVMFunctionType(
            LLVMVoidTypeInContext(cg->ctx), params, 3, 0);
        fn_set = LLVMAddFunction(cg->module, "kc_dict_set", ft);
        LLVMSetLinkage(fn_set, LLVMExternalLinkage);
    }

    /* dict 핸들 생성 */
    LLVMTypeRef ft_new = LLVMFunctionType(i8p, NULL, 0, 0);
    LLVMValueRef dict_handle = LLVMBuildCall2(
        cg->builder, ft_new, fn_new, NULL, 0, "dict");

    /* 각 엔트리 등록 */
    LLVMTypeRef params_set[3] = { i8p, i8p, i64 };
    LLVMTypeRef ft_set = LLVMFunctionType(
        LLVMVoidTypeInContext(cg->ctx), params_set, 3, 0);

    for (int i = 0; i < n->child_count; i++) {
        Node *entry = n->children[i];
        if (!entry || entry->type != NODE_DICT_ENTRY) continue;
        if (entry->child_count < 2) continue;

        /* 키 표현식 → i8* */
        LLVMValueRef key = gen_expr(cg, entry->children[0]);
        if (!key) key = LLVMConstNull(i8p);
        /* 키가 i8*이 아니면 타입 오류 — 단순히 null 키 사용 */
        if (LLVMGetTypeKind(LLVMTypeOf(key)) != LLVMPointerTypeKind)
            key = LLVMConstNull(i8p);

        /* 값 표현식 → i64 */
        LLVMValueRef val = gen_expr(cg, entry->children[1]);
        if (!val) val = LLVMConstInt(i64, 0, 0);
        if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMDoubleTypeKind)
            val = LLVMBuildFPToSI(cg->builder, val, i64, "dval_cast");
        else if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind)
            val = LLVMBuildPtrToInt(cg->builder, val, i64, "dval_ptr");
        else if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind &&
                 LLVMGetIntTypeWidth(LLVMTypeOf(val)) != 64)
            val = LLVMBuildSExt(cg->builder, val, i64, "dval_ext");

        LLVMValueRef set_args[3] = { dict_handle, key, val };
        LLVMBuildCall2(cg->builder, ft_set, fn_set, set_args, 3, "");
    }

    return dict_handle;
}

/* ================================================================
 *  람다 전방 선언을 위한 AST 재귀 탐색 (v9.2.0)
 *  gen_program() 1패스에서 호출 — 람다 함수를 미리 모듈에 등록
 * ================================================================ */
static void gen_collect_lambdas(LLVMCodegen *cg, Node *n)
{
    if (!n) return;
    if (n->type == NODE_LAMBDA) {
        /* 람다 함수 사전 생성 (함수 포인터 확보) */
        gen_lambda(cg, n);
        return;  /* 람다 내부는 이미 처리됨 */
    }
    for (int i = 0; i < n->child_count; i++)
        gen_collect_lambdas(cg, n->children[i]);
}

/* ================================================================
 *  모듈/포함 IR 헬퍼 (v9.3.0)
 * ================================================================ */

/* kc_include(name: i8*) 외부 함수 선언 + call — #포함 런타임 힌트 */
static void gen_kc_include_call(LLVMCodegen *cg, const char *fname)
{
    if (!fname || !block_is_open(cg)) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);

    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_include");
    if (!fn) {
        LLVMTypeRef ft = LLVMFunctionType(
            LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
        fn = LLVMAddFunction(cg->module, "kc_include", ft);
        LLVMSetLinkage(fn, LLVMExternalLinkage);
    }
    LLVMValueRef name_str = LLVMBuildGlobalStringPtr(
        cg->builder, fname, "pp_fname");
    LLVMTypeRef ft = LLVMFunctionType(
        LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
    LLVMBuildCall2(cg->builder, ft, fn, &name_str, 1, "");
}

/* kc_module_load(name: i8*) 외부 함수 선언 + call — 가짐 외부 모듈 */
static void gen_kc_module_load_call(LLVMCodegen *cg, const char *modname)
{
    if (!modname || !block_is_open(cg)) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);

    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_module_load");
    if (!fn) {
        LLVMTypeRef ft = LLVMFunctionType(
            LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
        fn = LLVMAddFunction(cg->module, "kc_module_load", ft);
        LLVMSetLinkage(fn, LLVMExternalLinkage);
    }
    LLVMValueRef name_str = LLVMBuildGlobalStringPtr(
        cg->builder, modname, "mod_name");
    LLVMTypeRef ft = LLVMFunctionType(
        LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
    LLVMBuildCall2(cg->builder, ft, fn, &name_str, 1, "");
}

/* 내장 모듈 → 자주 쓰는 C 런타임 함수 declare 삽입
 * (실제 링크는 clang 단계에서 -lm 등으로 해결)              */
static void gen_import_builtin_decls(LLVMCodegen *cg, const char *mod)
{
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef f64  = LLVMDoubleTypeInContext(cg->ctx);
    LLVMTypeRef i64  = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
    LLVMTypeRef vt   = LLVMVoidTypeInContext(cg->ctx);

    /* 수학 / 수학함수 → sin, cos, sqrt, pow, fabs, floor, ceil */
    if (strcmp(mod, "\xEC\x88\x98\xED\x95\x99") == 0 ||         /* 수학 */
        strcmp(mod, "\xEC\x88\x98\xED\x95\x99\xED\x95\xA8\xEC\x88\x98") == 0) { /* 수학함수 */
        const char *math_fns[] = { "sin","cos","tan","sqrt","pow","fabs","floor","ceil","exp","log" };
        for (int i = 0; i < 10; i++) {
            if (!LLVMGetNamedFunction(cg->module, math_fns[i])) {
                /* double fn(double) */
                LLVMTypeRef ft = LLVMFunctionType(f64, &f64, 1, 0);
                LLVMValueRef f = LLVMAddFunction(cg->module, math_fns[i], ft);
                LLVMSetLinkage(f, LLVMExternalLinkage);
            }
        }
        /* pow: double pow(double, double) */
        if (!LLVMGetNamedFunction(cg->module, "pow")) {
            LLVMTypeRef pp[2] = { f64, f64 };
            LLVMTypeRef ft = LLVMFunctionType(f64, pp, 2, 0);
            LLVMValueRef f = LLVMAddFunction(cg->module, "pow", ft);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
    }
    /* 문자열 → strlen, strcpy, strcat, strcmp, strstr, memset, memcpy */
    else if (strcmp(mod, "\xEB\xB9\x84\xEC\x98\x81\xEC\x96\xB4") == 0 || /* 문자열 */
             strcmp(mod, "\xEB\xB9\x84\xEC\x98\x81") == 0) {
        /* strlen: i64(i8*) */
        if (!LLVMGetNamedFunction(cg->module, "strlen")) {
            LLVMTypeRef ft = LLVMFunctionType(i64, &i8p, 1, 0);
            LLVMValueRef f = LLVMAddFunction(cg->module, "strlen", ft);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
        /* strcmp: i32(i8*, i8*) */
        if (!LLVMGetNamedFunction(cg->module, "strcmp")) {
            LLVMTypeRef pp[2] = { i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(i32, pp, 2, 0);
            LLVMValueRef f = LLVMAddFunction(cg->module, "strcmp", ft);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
        /* strcpy / strcat: i8*(i8*, i8*) */
        const char *sc_fns[] = { "strcpy", "strcat", "strstr" };
        for (int i = 0; i < 3; i++) {
            if (!LLVMGetNamedFunction(cg->module, sc_fns[i])) {
                LLVMTypeRef pp[2] = { i8p, i8p };
                LLVMTypeRef ft = LLVMFunctionType(i8p, pp, 2, 0);
                LLVMValueRef f = LLVMAddFunction(cg->module, sc_fns[i], ft);
                LLVMSetLinkage(f, LLVMExternalLinkage);
            }
        }
    }
    /* 파일시스템 → fopen, fclose, fread, fwrite, remove */
    else if (strcmp(mod, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x8B\x9C\xEC\x8A\xA4\xED\x85\x9C") == 0) { /* 파일시스템 */
        /* fopen: i8*(i8*, i8*) */
        if (!LLVMGetNamedFunction(cg->module, "fopen")) {
            LLVMTypeRef pp[2] = { i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(i8p, pp, 2, 0);
            LLVMValueRef f = LLVMAddFunction(cg->module, "fopen", ft);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
        /* fclose: i32(i8*) */
        if (!LLVMGetNamedFunction(cg->module, "fclose")) {
            LLVMTypeRef ft = LLVMFunctionType(i32, &i8p, 1, 0);
            LLVMValueRef f = LLVMAddFunction(cg->module, "fclose", ft);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
        /* remove: i32(i8*) */
        if (!LLVMGetNamedFunction(cg->module, "remove")) {
            LLVMTypeRef ft = LLVMFunctionType(i32, &i8p, 1, 0);
            LLVMValueRef f = LLVMAddFunction(cg->module, "remove", ft);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
    }
    /* 시간 → time, clock */
    else if (strcmp(mod, "\xEC\x8B\x9C\xEA\xB0\x84") == 0) { /* 시간 */
        if (!LLVMGetNamedFunction(cg->module, "time")) {
            LLVMTypeRef ft = LLVMFunctionType(i64, &i8p, 1, 0);
            LLVMValueRef f = LLVMAddFunction(cg->module, "time", ft);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
        if (!LLVMGetNamedFunction(cg->module, "clock")) {
            LLVMTypeRef ft = LLVMFunctionType(i64, NULL, 0, 0);
            LLVMValueRef f = LLVMAddFunction(cg->module, "clock", ft);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
    }
    /* 난수 → srand, rand */
    else if (strcmp(mod, "\xEB\x82\xAC\xEC\x88\x98") == 0) { /* 난수 */
        if (!LLVMGetNamedFunction(cg->module, "srand")) {
            LLVMTypeRef ft = LLVMFunctionType(vt, &i32, 1, 0);
            LLVMValueRef f = LLVMAddFunction(cg->module, "srand", ft);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
        if (!LLVMGetNamedFunction(cg->module, "rand")) {
            LLVMTypeRef ft = LLVMFunctionType(i32, NULL, 0, 0);
            LLVMValueRef f = LLVMAddFunction(cg->module, "rand", ft);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
    }
    else {
        /* 알 수 없는 모듈 → kc_module_load() 호출 */
        gen_kc_module_load_call(cg, mod);
    }
}

/* ================================================================
 *  계약 시스템 IR 헬퍼 (v9.4.0)
 *
 *  런타임 계약 함수 시그니처:
 *    kc_assert(i8* msg, i1 cond) → void
 *    kc_postcond(i8* fn, i8* msg, i32 sanction, i1 cond) → void
 *    kc_constitution(i8* msg, i1 cond) → void
 *    kc_statute(i8* msg, i1 cond) → void
 *    kc_regulation(i8* obj, i8* msg, i1 cond) → void
 *    kc_checkpoint(i8* name) → void
 *
 *  제재 상수 (i32):
 *    0=경고, 1=보고, 2=중단, 3=회귀, 4=대체
 * ================================================================ */

/* 제재 토큰 → i32 상수 */
static int sanction_to_int(TokenType op) {
    switch (op) {
        case TOK_KW_GYEONGGO: return 0;  /* 경고 */
        case TOK_KW_BOGO:     return 1;  /* 보고 */
        case TOK_KW_JUNGDAN:  return 2;  /* 중단 */
        case TOK_KW_HOEGWI:   return 3;  /* 회귀 */
        case TOK_KW_DAECHE:   return 4;  /* 대체 */
        default:              return 0;
    }
}

/* (i8*, i1) → void 형 계약 함수 call 공통 헬퍼 */
static void gen_contract_call2(LLVMCodegen *cg,
                                const char *fn_name,
                                const char *msg_str,
                                LLVMValueRef cond_val)
{
    if (!block_is_open(cg)) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i1  = LLVMInt1TypeInContext(cg->ctx);

    LLVMValueRef fn = LLVMGetNamedFunction(cg->module, fn_name);
    if (!fn) {
        LLVMTypeRef params[2] = { i8p, i1 };
        LLVMTypeRef ft = LLVMFunctionType(
            LLVMVoidTypeInContext(cg->ctx), params, 2, 0);
        fn = LLVMAddFunction(cg->module, fn_name, ft);
        LLVMSetLinkage(fn, LLVMExternalLinkage);
    }

    LLVMValueRef msg = LLVMBuildGlobalStringPtr(cg->builder, msg_str, "ctr_msg");
    /* cond를 i1로 통일 */
    if (!cond_val) cond_val = LLVMConstInt(i1, 1, 0);
    if (LLVMGetTypeKind(LLVMTypeOf(cond_val)) != LLVMIntegerTypeKind ||
        LLVMGetIntTypeWidth(LLVMTypeOf(cond_val)) != 1) {
        /* 정수 비교: 0 != val */
        LLVMTypeRef vt = LLVMTypeOf(cond_val);
        if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind)
            cond_val = LLVMBuildICmp(cg->builder, LLVMIntNE,
                cond_val, LLVMConstInt(vt, 0, 0), "cond_b");
        else if (LLVMGetTypeKind(vt) == LLVMDoubleTypeKind)
            cond_val = LLVMBuildFCmp(cg->builder, LLVMRealONE,
                cond_val, LLVMConstReal(vt, 0.0), "cond_b");
        else
            cond_val = LLVMConstInt(i1, 1, 0);
    }

    LLVMTypeRef params[2] = { i8p, i1 };
    LLVMTypeRef ft = LLVMFunctionType(
        LLVMVoidTypeInContext(cg->ctx), params, 2, 0);
    LLVMValueRef args[2] = { msg, cond_val };
    LLVMBuildCall2(cg->builder, ft, fn, args, 2, "");
}

/* NODE_CONTRACT IR 생성 (법령/법위반) */
static void gen_contract_ir(LLVMCodegen *cg, Node *n)
{
    if (!n || n->child_count < 2) return;
    const char *scope  = n->sval ? n->sval : "(알 수 없음)";
    int is_precond     = (n->op == TOK_KW_BEOPRYEONG);

    Node *cond_node     = n->children[0];
    Node *sanction_node = n->children[1];

    /* 제재 상수 결정 */
    int san_int = 0;
    if (sanction_node && sanction_node->type == NODE_SANCTION)
        san_int = sanction_to_int(sanction_node->op);

    /* 조건식 gen_expr */
    LLVMValueRef cond_val = NULL;
    if (cond_node && block_is_open(cg))
        cond_val = gen_expr(cg, cond_node);

    /* 메시지 문자열 구성 */
    char msg[256];
    if (is_precond)
        snprintf(msg, sizeof(msg), "[법령 위반: %s]", scope);
    else
        snprintf(msg, sizeof(msg), "[법위반 사후조건: %s]", scope);

    if (is_precond) {
        /* kc_assert(msg, cond) */
        gen_contract_call2(cg, "kc_assert", msg, cond_val);
    } else {
        /* kc_postcond(fn_name, msg, sanction, cond) */
        if (!block_is_open(cg)) return;
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);
        LLVMTypeRef i1  = LLVMInt1TypeInContext(cg->ctx);

        LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_postcond");
        if (!fn) {
            LLVMTypeRef params[4] = { i8p, i8p, i32, i1 };
            LLVMTypeRef ft = LLVMFunctionType(
                LLVMVoidTypeInContext(cg->ctx), params, 4, 0);
            fn = LLVMAddFunction(cg->module, "kc_postcond", ft);
            LLVMSetLinkage(fn, LLVMExternalLinkage);
        }

        LLVMValueRef fn_str  = LLVMBuildGlobalStringPtr(cg->builder, scope, "pc_fn");
        LLVMValueRef msg_str = LLVMBuildGlobalStringPtr(cg->builder, msg, "pc_msg");
        LLVMValueRef san_val = LLVMConstInt(i32, (unsigned long long)san_int, 0);

        /* cond 정규화 → i1 */
        if (!cond_val) cond_val = LLVMConstInt(i1, 1, 0);
        if (LLVMGetTypeKind(LLVMTypeOf(cond_val)) != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(cond_val)) != 1) {
            LLVMTypeRef vt = LLVMTypeOf(cond_val);
            if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind)
                cond_val = LLVMBuildICmp(cg->builder, LLVMIntNE,
                    cond_val, LLVMConstInt(vt, 0, 0), "pc_b");
            else
                cond_val = LLVMConstInt(i1, 1, 0);
        }

        LLVMTypeRef params[4] = { i8p, i8p, i32, i1 };
        LLVMTypeRef ft = LLVMFunctionType(
            LLVMVoidTypeInContext(cg->ctx), params, 4, 0);
        LLVMValueRef args[4] = { fn_str, msg_str, san_val, cond_val };
        LLVMBuildCall2(cg->builder, ft, fn, args, 4, "");
    }
}


/* ================================================================
 *  인터럽트 시스템 IR 헬퍼 (v9.5.0)
 *
 *  POSIX 신호 번호 매핑 (Linux 기준):
 *    SIGINT=2, SIGTERM=15, SIGKILL=9, SIGCHLD=17,
 *    SIGUSR1=10, SIGUSR2=12, SIGPIPE=13, SIGALRM=14,
 *    SIGSTOP=19, SIGCONT=18
 *
 *  런타임 함수:
 *    signal(i32 sig, i8* handler) → i8*    (POSIX)
 *    kill(i64 pid, i32 sig) → i32          (POSIX)
 *    kc_signal_ctrl(i32 sig, i32 action)   → void  (0=무시, 1=기본)
 *    kc_isr_register(i8* vec, i8* handler) → void
 *    kc_isr_lock() / kc_isr_unlock()       → void
 *    kc_event_register(i8* name, i8* fn)   → void
 *    kc_event_loop_run/stop()              → void
 *    kc_event_emit/unregister(i8* name)    → void
 * ================================================================ */

/* 신호 토큰 → POSIX 신호 번호 (Linux) */
static int sig_token_to_posix(TokenType op, const char *sname) {
    if (op == TOK_KW_SIG_INT)  return 2;
    if (op == TOK_KW_SIG_TERM) return 15;
    if (op == TOK_KW_SIG_KILL) return 9;
    if (op == TOK_KW_SIG_CHLD) return 17;
    if (op == TOK_KW_SIG_USR1) return 10;
    if (op == TOK_KW_SIG_USR2) return 12;
    if (op == TOK_KW_SIG_PIPE) return 13;
    if (op == TOK_KW_SIG_ALRM) return 14;
    if (op == TOK_KW_SIG_STOP) return 19;
    if (op == TOK_KW_SIG_CONT) return 18;
    /* sname 기반 fallback */
    if (sname) {
        if (strstr(sname, "\xEC\xA2\x85\xEB\xA3\x8C"))   return 15; /* 종료 */
        if (strstr(sname, "\xEA\xB2\xBD\xEB\xB3\xB4"))   return 14; /* 경보 */
        if (strstr(sname, "\xEC\x9E\xAC\xEA\xB0\x9C"))   return 18; /* 재개 */
    }
    return 2; /* 기본: SIGINT */
}

/* signal(sig, handler) POSIX 외부 함수 선언 + call */
static void gen_signal_register(LLVMCodegen *cg,
                                 int posix_sig, LLVMValueRef handler_fn)
{
    if (!block_is_open(cg)) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);

    /* declare i8* @signal(i32, i8*) */
    LLVMValueRef fn_signal = LLVMGetNamedFunction(cg->module, "signal");
    if (!fn_signal) {
        LLVMTypeRef params[2] = { i32, i8p };
        LLVMTypeRef ft = LLVMFunctionType(i8p, params, 2, 0);
        fn_signal = LLVMAddFunction(cg->module, "signal", ft);
        LLVMSetLinkage(fn_signal, LLVMExternalLinkage);
    }

    LLVMValueRef sig_val = LLVMConstInt(i32, (unsigned long long)posix_sig, 0);
    /* 핸들러 함수 포인터를 i8*로 bitcast */
    LLVMValueRef handler_ptr = LLVMBuildBitCast(
        cg->builder, handler_fn, i8p, "sig_hptr");

    LLVMTypeRef params[2] = { i32, i8p };
    LLVMTypeRef ft = LLVMFunctionType(i8p, params, 2, 0);
    LLVMValueRef args[2] = { sig_val, handler_ptr };
    LLVMBuildCall2(cg->builder, ft, fn_signal, args, 2, "");
}

/* NODE_SIGNAL_HANDLER: 핸들러 함수 생성 + signal() call */
static void gen_signal_handler_ir(LLVMCodegen *cg, Node *n)
{
    if (!n) return;
    int posix_sig = sig_token_to_posix(n->op, n->sval);

    /* 핸들러 함수 이름 */
    char hname[64];
    snprintf(hname, sizeof(hname), "kc_sig_handler_%d", cg->lambda_counter++);

    LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);

    /* declare: void kc_sig_handler_N(i32 signum) */
    LLVMTypeRef ft_handler = LLVMFunctionType(
        LLVMVoidTypeInContext(cg->ctx), &i32, 1, 0);
    LLVMValueRef handler_fn = LLVMGetNamedFunction(cg->module, hname);
    if (!handler_fn) {
        handler_fn = LLVMAddFunction(cg->module, hname, ft_handler);
        LLVMSetLinkage(handler_fn, LLVMPrivateLinkage);
    }

    /* 현재 함수 컨텍스트 저장 */
    LLVMValueRef saved_func     = cg->cur_func;
    LLVMTypeRef  saved_ret      = cg->cur_func_ret_type;
    int          saved_is_void  = cg->cur_func_is_void;
    int          saved_lblcnt   = cg->label_count;

    /* 핸들러 함수 몸체 생성 */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
        cg->ctx, handler_fn, "sig_entry");
    LLVMPositionBuilderAtEnd(cg->builder, entry);

    cg->cur_func         = handler_fn;
    cg->cur_func_ret_type = LLVMVoidTypeInContext(cg->ctx);
    cg->cur_func_is_void = 1;
    cg->label_count      = 0;

    scope_push(cg);
    if (n->child_count > 0) gen_block(cg, n->children[0]);
    if (block_is_open(cg)) LLVMBuildRetVoid(cg->builder);
    scope_pop(cg);

    /* 컨텍스트 복원 */
    cg->cur_func          = saved_func;
    cg->cur_func_ret_type = saved_ret;
    cg->cur_func_is_void  = saved_is_void;
    cg->label_count       = saved_lblcnt;

    if (saved_func) {
        LLVMBasicBlockRef last = LLVMGetLastBasicBlock(saved_func);
        if (last) LLVMPositionBuilderAtEnd(cg->builder, last);
    }

    /* signal(posix_sig, handler_fn) call */
    gen_signal_register(cg, posix_sig, handler_fn);
}

/* NODE_ISR_HANDLER: ISR 핸들러 함수 생성 + kc_isr_register() call */
static void gen_isr_handler_ir(LLVMCodegen *cg, Node *n)
{
    if (!n) return;
    const char *vname = n->sval ? n->sval : "isr";

    char hname[64];
    snprintf(hname, sizeof(hname), "kc_isr_%d", cg->lambda_counter++);

    /* declare: void kc_isr_N(void) */
    LLVMTypeRef ft_isr = LLVMFunctionType(
        LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
    LLVMValueRef isr_fn = LLVMGetNamedFunction(cg->module, hname);
    if (!isr_fn) {
        isr_fn = LLVMAddFunction(cg->module, hname, ft_isr);
        LLVMSetLinkage(isr_fn, LLVMPrivateLinkage);
    }

    /* 현재 함수 컨텍스트 저장 */
    LLVMValueRef saved_func    = cg->cur_func;
    LLVMTypeRef  saved_ret     = cg->cur_func_ret_type;
    int          saved_is_void = cg->cur_func_is_void;
    int          saved_lblcnt  = cg->label_count;

    /* ISR 함수 몸체 */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
        cg->ctx, isr_fn, "isr_entry");
    LLVMPositionBuilderAtEnd(cg->builder, entry);

    cg->cur_func          = isr_fn;
    cg->cur_func_ret_type = LLVMVoidTypeInContext(cg->ctx);
    cg->cur_func_is_void  = 1;
    cg->label_count       = 0;

    scope_push(cg);
    if (n->child_count > 0) gen_block(cg, n->children[0]);
    if (block_is_open(cg)) LLVMBuildRetVoid(cg->builder);
    scope_pop(cg);

    /* 컨텍스트 복원 */
    cg->cur_func          = saved_func;
    cg->cur_func_ret_type = saved_ret;
    cg->cur_func_is_void  = saved_is_void;
    cg->label_count       = saved_lblcnt;

    if (saved_func) {
        LLVMBasicBlockRef last = LLVMGetLastBasicBlock(saved_func);
        if (last) LLVMPositionBuilderAtEnd(cg->builder, last);
    }

    /* kc_isr_register(vec_name, isr_fn) */
    if (!block_is_open(cg)) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMValueRef fn_reg = LLVMGetNamedFunction(cg->module, "kc_isr_register");
    if (!fn_reg) {
        LLVMTypeRef params[2] = { i8p, i8p };
        LLVMTypeRef ft = LLVMFunctionType(
            LLVMVoidTypeInContext(cg->ctx), params, 2, 0);
        fn_reg = LLVMAddFunction(cg->module, "kc_isr_register", ft);
        LLVMSetLinkage(fn_reg, LLVMExternalLinkage);
    }
    LLVMValueRef vec_str = LLVMBuildGlobalStringPtr(cg->builder, vname, "isr_vec");
    LLVMValueRef isr_ptr = LLVMBuildBitCast(cg->builder, isr_fn, i8p, "isr_fptr");
    LLVMTypeRef params[2] = { i8p, i8p };
    LLVMTypeRef ft_reg = LLVMFunctionType(
        LLVMVoidTypeInContext(cg->ctx), params, 2, 0);
    LLVMValueRef args[2] = { vec_str, isr_ptr };
    LLVMBuildCall2(cg->builder, ft_reg, fn_reg, args, 2, "");
}

/* NODE_EVENT_HANDLER: 이벤트 핸들러 함수 생성 + kc_event_register() call */
static void gen_event_handler_ir(LLVMCodegen *cg, Node *n)
{
    if (!n) return;
    const char *evname = n->sval ? n->sval : "event";

    char hname[64];
    snprintf(hname, sizeof(hname), "kc_ev_handler_%d", cg->lambda_counter++);

    /* declare: void kc_ev_handler_N(void) */
    LLVMTypeRef ft_ev = LLVMFunctionType(
        LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
    LLVMValueRef ev_fn = LLVMGetNamedFunction(cg->module, hname);
    if (!ev_fn) {
        ev_fn = LLVMAddFunction(cg->module, hname, ft_ev);
        LLVMSetLinkage(ev_fn, LLVMPrivateLinkage);
    }

    /* 현재 함수 컨텍스트 저장 */
    LLVMValueRef saved_func    = cg->cur_func;
    LLVMTypeRef  saved_ret     = cg->cur_func_ret_type;
    int          saved_is_void = cg->cur_func_is_void;
    int          saved_lblcnt  = cg->label_count;

    /* 이벤트 핸들러 함수 몸체 */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
        cg->ctx, ev_fn, "ev_entry");
    LLVMPositionBuilderAtEnd(cg->builder, entry);

    cg->cur_func          = ev_fn;
    cg->cur_func_ret_type = LLVMVoidTypeInContext(cg->ctx);
    cg->cur_func_is_void  = 1;
    cg->label_count       = 0;

    scope_push(cg);
    /* child[last] = 핸들러 블록 (매개변수 child는 건너뜀) */
    if (n->child_count > 0)
        gen_block(cg, n->children[n->child_count - 1]);
    if (block_is_open(cg)) LLVMBuildRetVoid(cg->builder);
    scope_pop(cg);

    /* 컨텍스트 복원 */
    cg->cur_func          = saved_func;
    cg->cur_func_ret_type = saved_ret;
    cg->cur_func_is_void  = saved_is_void;
    cg->label_count       = saved_lblcnt;

    if (saved_func) {
        LLVMBasicBlockRef last = LLVMGetLastBasicBlock(saved_func);
        if (last) LLVMPositionBuilderAtEnd(cg->builder, last);
    }

    /* kc_event_register(evname, ev_fn) */
    if (!block_is_open(cg)) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMValueRef fn_reg = LLVMGetNamedFunction(cg->module, "kc_event_register");
    if (!fn_reg) {
        LLVMTypeRef params[2] = { i8p, i8p };
        LLVMTypeRef ft = LLVMFunctionType(
            LLVMVoidTypeInContext(cg->ctx), params, 2, 0);
        fn_reg = LLVMAddFunction(cg->module, "kc_event_register", ft);
        LLVMSetLinkage(fn_reg, LLVMExternalLinkage);
    }
    LLVMValueRef ev_str  = LLVMBuildGlobalStringPtr(cg->builder, evname, "ev_name");
    LLVMValueRef ev_ptr  = LLVMBuildBitCast(cg->builder, ev_fn, i8p, "ev_fptr");
    LLVMTypeRef params[2] = { i8p, i8p };
    LLVMTypeRef ft_reg = LLVMFunctionType(
        LLVMVoidTypeInContext(cg->ctx), params, 2, 0);
    LLVMValueRef args[2] = { ev_str, ev_ptr };
    LLVMBuildCall2(cg->builder, ft_reg, fn_reg, args, 2, "");
}

/* ================================================================
 *  인덱스 접근: arr[idx]
 * ================================================================ */
static LLVMValueRef gen_index_expr(LLVMCodegen *cg, Node *n)
{
    LLVMValueRef arr = gen_expr(cg, n->children[0]);
    LLVMValueRef idx = gen_expr(cg, n->children[1]);
    if (!arr || !idx) {
        CG_ERROR(cg, n, "인덱스 접근: 표현식 생성 실패");
        return LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0);
    }

    /* idx를 i64로 변환 */
    LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);
    if (LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
        idx = LLVMBuildSExt(cg->builder, idx, i64, "idx64");

    return build_array_get(cg, arr, idx);
}

/* ================================================================
 *  각각 (foreach): 각각 변수 안에 배열:
 *
 *    %len = arr.len
 *    %i   = alloca i64 ; 0
 *    cond: i < len  → body : end
 *    body: elem = data[i]  , scope{var=elem}, block, i++  → cond
 *    end:
 * ================================================================ */
static void gen_for_each(LLVMCodegen *cg, Node *n)
{
    sourcemap_add(cg, n->line, n->col);

    /* child[0] = 배열 표현식, child[1] = 블록, sval = 변수명 */
    LLVMValueRef arr = gen_expr(cg, n->children[0]);
    if (!arr) { CG_ERROR(cg, n, "각각: 배열 표현식 생성 실패"); return; }

    LLVMTypeRef  i64 = LLVMInt64TypeInContext(cg->ctx);
    LLVMValueRef fn  = cg->cur_func;

    /* 루프 변수 i alloca (entry 블록에) */
    LLVMBasicBlockRef cur   = LLVMGetInsertBlock(cg->builder);
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
    LLVMValueRef      fi    = LLVMGetFirstInstruction(entry);
    LLVMPositionBuilderBefore(cg->builder, fi);
    LLVMValueRef loop_i = LLVMBuildAlloca(cg->builder, i64, "fe_i");
    LLVMPositionBuilderAtEnd(cg->builder, cur);

    /* i = 0 */
    LLVMBuildStore(cg->builder, LLVMConstInt(i64, 0, 0), loop_i);

    /* len = arr.len */
    LLVMValueRef len = build_array_len(cg, arr);

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(cg->ctx, fn, "fe.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(cg->ctx, fn, "fe.body");
    LLVMBasicBlockRef inc_bb  = LLVMAppendBasicBlockInContext(cg->ctx, fn, "fe.inc");
    LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(cg->ctx, fn, "fe.end");

    LLVMBuildBr(cg->builder, cond_bb);

    /* ── 조건: i < len ─────────────────── */
    LLVMPositionBuilderAtEnd(cg->builder, cond_bb);
    LLVMValueRef i_val = LLVMBuildLoad2(cg->builder, i64, loop_i, "fe_i_val");
    LLVMValueRef cond  = LLVMBuildICmp(cg->builder, LLVMIntSLT, i_val, len, "fe_cond");
    LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);

    /* ── 몸체 ──────────────────────────── */
    LLVMPositionBuilderAtEnd(cg->builder, body_bb);
    loop_push(cg, end_bb, inc_bb);
    scope_push(cg);

    /* 요소 변수 alloca 및 바인딩 */
    LLVMValueRef elem_alloca = LLVMBuildAlloca(cg->builder, i64, n->sval);
    LLVMValueRef elem_val    = build_array_get(cg, arr, i_val);
    LLVMBuildStore(cg->builder, elem_val, elem_alloca);
    scope_set(cg, n->sval, elem_alloca, i64);

    gen_block(cg, n->children[1]);
    scope_pop(cg);
    loop_pop(cg);
    if (block_is_open(cg)) LLVMBuildBr(cg->builder, inc_bb);

    /* ── 증가: i++ ─────────────────────── */
    LLVMPositionBuilderAtEnd(cg->builder, inc_bb);
    LLVMValueRef i_cur = LLVMBuildLoad2(cg->builder, i64, loop_i, "fe_i2");
    LLVMValueRef i_inc = LLVMBuildAdd(cg->builder, i_cur,
                                       LLVMConstInt(i64, 1, 0), "fe_inc");
    LLVMBuildStore(cg->builder, i_inc, loop_i);
    LLVMBuildBr(cg->builder, cond_bb);

    LLVMPositionBuilderAtEnd(cg->builder, end_bb);
}

/* ================================================================
 *  현재 블록이 terminator 없이 열려있는지 확인
 * ================================================================ */
static int block_is_open(LLVMCodegen *cg) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(cg->builder);
    if (!cur) return 0;
    LLVMValueRef term = LLVMGetBasicBlockTerminator(cur);
    return term == NULL;
}

/* ================================================================
 *  표현식 코드 생성
 * ================================================================ */

/* ── 리터럴 ─────────────────────────────────────────────────── */
static LLVMValueRef gen_int_lit(LLVMCodegen *cg, Node *n) {
    return LLVMConstInt(LLVMInt64TypeInContext(cg->ctx),
                        (unsigned long long)n->val.ival, /* signExtend= */ 1);
}

static LLVMValueRef gen_float_lit(LLVMCodegen *cg, Node *n) {
    return LLVMConstReal(LLVMDoubleTypeInContext(cg->ctx), n->val.fval);
}

static LLVMValueRef gen_bool_lit(LLVMCodegen *cg, Node *n) {
    return LLVMConstInt(LLVMInt1TypeInContext(cg->ctx),
                        (unsigned long long)n->val.bval, 0);
}

static LLVMValueRef gen_char_lit(LLVMCodegen *cg, Node *n) {
    /* 문자 하나 → UTF-32 코드포인트 i32 */
    return LLVMConstInt(LLVMInt32TypeInContext(cg->ctx),
                        (unsigned long long)n->val.ival, 0);
}

static LLVMValueRef gen_string_lit(LLVMCodegen *cg, Node *n) {
    static int str_cnt = 0;
    char nm[32];
    snprintf(nm, sizeof(nm), ".str%d", str_cnt++);
    return make_global_str(cg, n->sval ? n->sval : "", nm);
}

static LLVMValueRef gen_null_lit(LLVMCodegen *cg) {
    return LLVMConstNull(
        LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)
    );
}

/* ── 식별자 (로드) ──────────────────────────────────────────── */
static LLVMValueRef gen_ident(LLVMCodegen *cg, Node *n) {
    LLVMSymbol *sym = scope_lookup(cg, n->sval);
    if (!sym) {
        /* 전역 함수일 수도 있음 — 함수 참조 반환 */
        LLVMValueRef fn = LLVMGetNamedFunction(cg->module, n->sval);
        if (fn) return fn;
        CG_ERROR(cg, n, "선언되지 않은 식별자: %s", n->sval);
        return LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0);
    }
    return LLVMBuildLoad2(cg->builder, sym->type, sym->alloca, n->sval);
}

/* ── 이항 연산 ──────────────────────────────────────────────── */
static LLVMValueRef gen_binary(LLVMCodegen *cg, Node *n) {
    LLVMValueRef L = gen_expr(cg, n->children[0]);
    LLVMValueRef R = gen_expr(cg, n->children[1]);
    if (!L || !R) return NULL;

    LLVMTypeRef lt = LLVMTypeOf(L);
    int is_float = (LLVMGetTypeKind(lt) == LLVMDoubleTypeKind);

    switch (n->op) {
        /* ── 산술 ── */
        case TOK_PLUS:
            return is_float ? LLVMBuildFAdd(cg->builder, L, R, "fadd")
                            : LLVMBuildAdd (cg->builder, L, R, "add");
        case TOK_MINUS:
            return is_float ? LLVMBuildFSub(cg->builder, L, R, "fsub")
                            : LLVMBuildSub (cg->builder, L, R, "sub");
        case TOK_STAR:
            return is_float ? LLVMBuildFMul(cg->builder, L, R, "fmul")
                            : LLVMBuildMul (cg->builder, L, R, "mul");
        case TOK_SLASH:
            return is_float ? LLVMBuildFDiv(cg->builder, L, R, "fdiv")
                            : LLVMBuildSDiv(cg->builder, L, R, "sdiv");
        case TOK_PERCENT:
            return is_float ? LLVMBuildFRem(cg->builder, L, R, "frem")
                            : LLVMBuildSRem(cg->builder, L, R, "srem");

        /* ── 비교 ── */
        case TOK_EQEQ:
            return is_float
                ? LLVMBuildFCmp(cg->builder, LLVMRealOEQ, L, R, "feq")
                : LLVMBuildICmp(cg->builder, LLVMIntEQ,   L, R, "ieq");
        case TOK_BANGEQ:
            return is_float
                ? LLVMBuildFCmp(cg->builder, LLVMRealONE, L, R, "fne")
                : LLVMBuildICmp(cg->builder, LLVMIntNE,   L, R, "ine");
        case TOK_LT:
            return is_float
                ? LLVMBuildFCmp(cg->builder, LLVMRealOLT, L, R, "flt")
                : LLVMBuildICmp(cg->builder, LLVMIntSLT,  L, R, "ilt");
        case TOK_GT:
            return is_float
                ? LLVMBuildFCmp(cg->builder, LLVMRealOGT, L, R, "fgt")
                : LLVMBuildICmp(cg->builder, LLVMIntSGT,  L, R, "igt");
        case TOK_LTEQ:
            return is_float
                ? LLVMBuildFCmp(cg->builder, LLVMRealOLE, L, R, "fle")
                : LLVMBuildICmp(cg->builder, LLVMIntSLE,  L, R, "ile");
        case TOK_GTEQ:
            return is_float
                ? LLVMBuildFCmp(cg->builder, LLVMRealOGE, L, R, "fge")
                : LLVMBuildICmp(cg->builder, LLVMIntSGE,  L, R, "ige");

        /* ── 논리 ── */
        case TOK_KW_AND:  /* 그리고 */
            return LLVMBuildAnd(cg->builder, L, R, "and");
        case TOK_KW_OR:   /* 또는 */
            return LLVMBuildOr(cg->builder, L, R, "or");

        /* ── 비트 ── */
        case TOK_AMP:    return LLVMBuildAnd(cg->builder, L, R, "band");
        case TOK_PIPE:   return LLVMBuildOr (cg->builder, L, R, "bor");
        case TOK_CARET:  return LLVMBuildXor(cg->builder, L, R, "xor");
        case TOK_LTLT:   return LLVMBuildShl(cg->builder, L, R, "shl");
        case TOK_GTGT:   return LLVMBuildAShr(cg->builder, L, R, "ashr");

        default:
            CG_ERROR(cg, n, "지원하지 않는 이항 연산자: %d", n->op);
            return L;
    }
}

/* ── 단항 연산 ──────────────────────────────────────────────── */
static LLVMValueRef gen_unary(LLVMCodegen *cg, Node *n) {
    LLVMValueRef val = gen_expr(cg, n->children[0]);
    if (!val) return NULL;

    switch (n->op) {
        case TOK_MINUS:
            if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMDoubleTypeKind)
                return LLVMBuildFNeg(cg->builder, val, "fneg");
            return LLVMBuildNeg(cg->builder, val, "neg");
        case TOK_KW_NOT:  /* 아니다 (NOT) */
            return LLVMBuildNot(cg->builder, val, "not");
        case TOK_TILDE:
            return LLVMBuildNot(cg->builder, val, "bnot");
        default:
            CG_ERROR(cg, n, "지원하지 않는 단항 연산자: %d", n->op);
            return val;
    }
}

/* ── 함수 호출 ──────────────────────────────────────────────── */
static LLVMValueRef gen_call(LLVMCodegen *cg, Node *n) {
    /* child[0] = 함수 표현식, child[1..] = 인수 */
    Node *fn_node = n->children[0];
    const char *fn_name = (fn_node->type == NODE_IDENT) ? fn_node->sval : NULL;

    /* ── 내장 출력 함수 처리 ── */
    if (fn_name) {
        /* 출력 → printf("%s\n", ...) */
        const char *chulryeok = "\xEC\xB6\x9C\xEB\xA0\xA5"; /* 출력 UTF-8 */
        if (strcmp(fn_name, chulryeok) == 0) {
            if (n->child_count < 2) {
                /* 인수 없음 → 빈 줄 출력 */
                LLVMValueRef fmt = make_global_str(cg, "\n", ".fmt_nl");
                LLVMValueRef args[] = { fmt };
                return LLVMBuildCall2(cg->builder,
                    LLVMGetElementType(LLVMTypeOf(get_printf(cg))),
                    get_printf(cg), args, 1, "");
            }
            LLVMValueRef val = gen_expr(cg, n->children[1]);
            LLVMTypeRef  vt  = LLVMTypeOf(val);
            LLVMValueRef fmt_ptr;
            LLVMValueRef args[2];

            switch (LLVMGetTypeKind(vt)) {
                case LLVMDoubleTypeKind:
                    fmt_ptr = make_global_str(cg, "%g\n", ".fmt_f");
                    args[0] = fmt_ptr; args[1] = val;
                    break;
                case LLVMIntegerTypeKind:
                    if (LLVMGetIntTypeWidth(vt) == 1) {
                        /* 논리 → 참/거짓 문자열 */
                        LLVMValueRef t_str = make_global_str(cg, "참\n",  ".fmt_t");
                        LLVMValueRef f_str = make_global_str(cg, "거짓\n", ".fmt_ff");
                        LLVMValueRef sel = LLVMBuildSelect(
                            cg->builder, val, t_str, f_str, "bool_str");
                        args[0] = sel;
                        LLVMTypeRef fn_t = LLVMFunctionType(
                            LLVMInt32TypeInContext(cg->ctx),
                            (LLVMTypeRef[]){LLVMPointerType(
                                LLVMInt8TypeInContext(cg->ctx),0)},
                            1, 1);
                        return LLVMBuildCall2(cg->builder, fn_t,
                                             get_printf(cg), args, 1, "");
                    }
                    fmt_ptr = make_global_str(cg, "%lld\n", ".fmt_i");
                    /* i32/i64 통일 — i64로 확장 */
                    if (LLVMGetIntTypeWidth(vt) < 64)
                        val = LLVMBuildSExt(cg->builder, val,
                                            LLVMInt64TypeInContext(cg->ctx), "ext");
                    args[0] = fmt_ptr; args[1] = val;
                    break;
                default: /* 포인터(글자) */
                    fmt_ptr = make_global_str(cg, "%s\n", ".fmt_s");
                    args[0] = fmt_ptr; args[1] = val;
                    break;
            }
            LLVMTypeRef fn_t = LLVMFunctionType(
                LLVMInt32TypeInContext(cg->ctx),
                (LLVMTypeRef[]){LLVMPointerType(
                    LLVMInt8TypeInContext(cg->ctx), 0)},
                1, 1);
            return LLVMBuildCall2(cg->builder, fn_t,
                                 get_printf(cg), args, 2, "");
        }

        /* 출력없이 → printf("%s", ...) — 줄바꿈 없음 */
        const char *chul_noln = "\xEC\xB6\x9C\xEB\xA0\xA5\xEC\x97\x86\xEC\x9D\xB4"; /* 출력없이 */
        if (strcmp(fn_name, chul_noln) == 0) {
            LLVMValueRef val = (n->child_count >= 2)
                ? gen_expr(cg, n->children[1]) : NULL;
            LLVMTypeRef fn_t = LLVMFunctionType(
                LLVMInt32TypeInContext(cg->ctx),
                (LLVMTypeRef[]){LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)},
                1, 1);
            if (!val) {
                /* 인수 없음 → 아무것도 출력 안 함 */
                return LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0);
            }
            LLVMTypeRef vt = LLVMTypeOf(val);
            LLVMValueRef fmt_ptr, args[2];
            switch (LLVMGetTypeKind(vt)) {
                case LLVMDoubleTypeKind:
                    fmt_ptr = make_global_str(cg, "%g", ".fmt_fn");
                    break;
                case LLVMIntegerTypeKind:
                    if (LLVMGetIntTypeWidth(vt) < 64)
                        val = LLVMBuildSExt(cg->builder, val,
                                            LLVMInt64TypeInContext(cg->ctx), "ext");
                    fmt_ptr = make_global_str(cg, "%lld", ".fmt_in");
                    break;
                default:
                    fmt_ptr = make_global_str(cg, "%s", ".fmt_sn");
                    break;
            }
            args[0] = fmt_ptr; args[1] = val;
            return LLVMBuildCall2(cg->builder, fn_t, get_printf(cg), args, 2, "");
        }

        /* 입력 → kc_input_line() → i8* (런타임 위임) */
        const char *kw_ip = "\xEC\x9E\x85\xEB\xA0\xA5"; /* 입력 */
        if (strcmp(fn_name, kw_ip) == 0) {
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_input_line");
            if (!fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8p, NULL, 0, 0);
                fn = LLVMAddFunction(cg->module, "kc_input_line", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ft = LLVMFunctionType(i8p, NULL, 0, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, NULL, 0, "input");
        }
    }

    /* ── 수학 기초 내장 함수 (v3.7.1) — libm 직접 호출 ── */
    if (fn_name) {
        /* 헬퍼: 인수 1개 → double, libm 함수 호출 → double 반환 */
        struct { const char *nm; const char *cfn; } math1[] = {
            { "\xEC\x82\xAC\xEC\x9D\xB8",                 "sin"   }, /* 사인 */
            { "\xEC\xBD\x94\xEC\x82\xAC\xEC\x9D\xB8",     "cos"   }, /* 코사인 */
            { "\xED\x83\x84\xEC\xA0\xA0\xED\x8A\xB8",     "tan"   }, /* 탄젠트 */
            { "\xEC\x9E\x90\xEC\x97\xB0\xEB\xA1\x9C\xEA\xB7\xB8", "log" }, /* 자연로그 */
            { "\xEC\xA7\x80\xEC\x88\x98",                  "exp"   }, /* 지수 */
            { "\xEC\x98\xAC\xEB\xA6\xBC",                  "ceil"  }, /* 올림 */
            { "\xEB\x82\xB4\xEB\xA6\xBC",                  "floor" }, /* 내림 */
            { NULL, NULL }
        };
        for (int mi = 0; math1[mi].nm; mi++) {
            if (strcmp(fn_name, math1[mi].nm) != 0) continue;
            /* 선언: double cfn(double) */
            LLVMTypeRef f64 = LLVMDoubleTypeInContext(cg->ctx);
            LLVMTypeRef ft  = LLVMFunctionType(f64, &f64, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, math1[mi].cfn);
            if (!fn) {
                fn = LLVMAddFunction(cg->module, math1[mi].cfn, ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMValueRef arg = (n->child_count > 1)
                ? gen_expr(cg, n->children[1])
                : LLVMConstReal(f64, 0.0);
            /* 정수면 double로 변환 */
            if (LLVMGetTypeKind(LLVMTypeOf(arg)) != LLVMDoubleTypeKind)
                arg = LLVMBuildSIToFP(cg->builder, arg, f64, "to_f64");
            return LLVMBuildCall2(cg->builder, ft, fn, &arg, 1, math1[mi].cfn);
        }

        /* 로그(밑, 값) → log(x)/log(base) */
        if (strcmp(fn_name, "\xEB\xA1\x9C\xEA\xB7\xB8") == 0 && n->child_count >= 3) { /* 로그 */
            LLVMTypeRef f64 = LLVMDoubleTypeInContext(cg->ctx);
            LLVMTypeRef ft  = LLVMFunctionType(f64, &f64, 1, 0);
            LLVMValueRef logfn = LLVMGetNamedFunction(cg->module, "log");
            if (!logfn) {
                logfn = LLVMAddFunction(cg->module, "log", ft);
                LLVMSetLinkage(logfn, LLVMExternalLinkage);
            }
            LLVMValueRef base = gen_expr(cg, n->children[1]);
            LLVMValueRef x    = gen_expr(cg, n->children[2]);
            if (LLVMGetTypeKind(LLVMTypeOf(base)) != LLVMDoubleTypeKind)
                base = LLVMBuildSIToFP(cg->builder, base, f64, "base_f");
            if (LLVMGetTypeKind(LLVMTypeOf(x)) != LLVMDoubleTypeKind)
                x = LLVMBuildSIToFP(cg->builder, x, f64, "x_f");
            LLVMValueRef lx   = LLVMBuildCall2(cg->builder, ft, logfn, &x,    1, "lx");
            LLVMValueRef lb   = LLVMBuildCall2(cg->builder, ft, logfn, &base, 1, "lb");
            return LLVMBuildFDiv(cg->builder, lx, lb, "log_base");
        }

        /* 반올림(값, 자릿수=0) */
        if (strcmp(fn_name, "\xEB\xB0\x98\xEC\x98\xAC\xEB\xA6\xBC") == 0) { /* 반올림 */
            LLVMTypeRef f64 = LLVMDoubleTypeInContext(cg->ctx);
            LLVMTypeRef ft1 = LLVMFunctionType(f64, &f64, 1, 0);
            LLVMValueRef roundfn = LLVMGetNamedFunction(cg->module, "round");
            if (!roundfn) {
                roundfn = LLVMAddFunction(cg->module, "round", ft1);
                LLVMSetLinkage(roundfn, LLVMExternalLinkage);
            }
            LLVMValueRef powfn = LLVMGetNamedFunction(cg->module, "pow");
            if (!powfn) {
                LLVMTypeRef pts[2] = { f64, f64 };
                LLVMTypeRef ft2 = LLVMFunctionType(f64, pts, 2, 0);
                powfn = LLVMAddFunction(cg->module, "pow", ft2);
                LLVMSetLinkage(powfn, LLVMExternalLinkage);
            }
            LLVMValueRef v = (n->child_count > 1)
                ? gen_expr(cg, n->children[1])
                : LLVMConstReal(f64, 0.0);
            if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMDoubleTypeKind)
                v = LLVMBuildSIToFP(cg->builder, v, f64, "v_f");

            if (n->child_count >= 3) {
                /* 자릿수 있음: round(v * 10^d) / 10^d */
                LLVMValueRef d = gen_expr(cg, n->children[2]);
                if (LLVMGetTypeKind(LLVMTypeOf(d)) != LLVMDoubleTypeKind)
                    d = LLVMBuildSIToFP(cg->builder, d, f64, "d_f");
                LLVMValueRef base10 = LLVMConstReal(f64, 10.0);
                LLVMValueRef pargs[2] = { base10, d };
                LLVMTypeRef pts[2] = { f64, f64 };
                LLVMTypeRef ft2 = LLVMFunctionType(f64, pts, 2, 0);
                LLVMValueRef factor = LLVMBuildCall2(cg->builder, ft2, powfn, pargs, 2, "factor");
                LLVMValueRef scaled = LLVMBuildFMul(cg->builder, v, factor, "scaled");
                LLVMValueRef r      = LLVMBuildCall2(cg->builder, ft1, roundfn, &scaled, 1, "r");
                return LLVMBuildFDiv(cg->builder, r, factor, "round_d");
            } else {
                /* 자릿수 없음 → 정수 반환 */
                LLVMValueRef r = LLVMBuildCall2(cg->builder, ft1, roundfn, &v, 1, "r");
                return LLVMBuildFPToSI(cg->builder, r,
                    LLVMInt64TypeInContext(cg->ctx), "round_i");
            }
        }
    } /* end if (fn_name) */

    /* ── 글자 함수 21종 LLVM IR (v9.7.0) ────────────────────────
     *  전략: kc_str_*(args...) 외부 함수를 declare 후 call
     *    반환 타입:
     *      i8*  — 자르기/분할/합치기/반복글자/역순/대문자/소문자/
     *             제목식/대체/한번대체/앞공백/뒤공백/공백제거/포맷/분석
     *      i64  — 위치/비교
     *      i1   — 포함/시작/끝확인/반복확인
     * ============================================================ */
    if (fn_name) {
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);
        LLVMTypeRef i1  = LLVMInt1TypeInContext(cg->ctx);

        /* 헬퍼: 인수 수집 (child[1..]) */
        int sargc = n->child_count - 1;
        LLVMValueRef sargs[8] = {0};
        for (int i = 0; i < sargc && i < 8; i++)
            sargs[i] = gen_expr(cg, n->children[i + 1]);

        /* ── i8*(i8*,i8*,i8*) 형 3인수 함수 ── */
        struct { const char *kn; const char *cn; } str3[] = {
            { "\xEB\x8C\x80\xEC\xB2\xB4",                               "kc_str_replace"      }, /* 대체 */
            { "\xED\x95\x9C\xEB\xB2\x88\xEB\x8C\x80\xEC\xB2\xB4",     "kc_str_replace_once" }, /* 한번대체 */
            { NULL, NULL }
        };
        for (int i = 0; str3[i].kn; i++) {
            if (strcmp(fn_name, str3[i].kn) != 0) continue;
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, str3[i].cn);
            if (!fn) {
                LLVMTypeRef ps[3] = { i8p, i8p, i8p };
                LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 3, 0);
                fn = LLVMAddFunction(cg->module, str3[i].cn, ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ps[3] = { i8p, i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 3, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, sargs, 3, str3[i].cn);
        }

        /* ── i8*(i8*,i8*) 형 2인수 → i8* 반환 ── */
        struct { const char *kn; const char *cn; } str2r[] = {
            { "\xEB\xB6\x84\xED\x95\xA0",                                       "kc_str_split"      }, /* 분할 */
            { "\xED\x95\xA9\xEC\xB9\x98\xEA\xB8\xB0",                          "kc_str_join"       }, /* 합치기 */
            { NULL, NULL }
        };
        for (int i = 0; str2r[i].kn; i++) {
            if (strcmp(fn_name, str2r[i].kn) != 0) continue;
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, str2r[i].cn);
            if (!fn) {
                LLVMTypeRef ps[2] = { i8p, i8p };
                LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 2, 0);
                fn = LLVMAddFunction(cg->module, str2r[i].cn, ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ps[2] = { i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 2, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, sargs, 2, str2r[i].cn);
        }

        /* ── i8*(i8*, i64) 형 — 반복글자 ── */
        if (strcmp(fn_name, "\xEB\xB0\x98\xEB\xB3\xB5\xEA\xB8\x80\xEC\x9E\x90") == 0) { /* 반복글자 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_str_repeat");
            if (!fn) {
                LLVMTypeRef ps[2] = { i8p, i64 };
                LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 2, 0);
                fn = LLVMAddFunction(cg->module, "kc_str_repeat", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            /* 두 번째 인수 → i64 */
            if (sargc >= 2 && LLVMGetTypeKind(LLVMTypeOf(sargs[1])) == LLVMIntegerTypeKind
                    && LLVMGetIntTypeWidth(LLVMTypeOf(sargs[1])) < 64)
                sargs[1] = LLVMBuildSExt(cg->builder, sargs[1], i64, "rep_n");
            LLVMTypeRef ps[2] = { i8p, i64 };
            LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 2, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, sargs, 2, "kc_str_repeat");
        }

        /* ── i8*(i8*, i64, i64) 형 — 자르기 ── */
        if (strcmp(fn_name, "\xEC\x9E\x90\xEB\xA5\xB4\xEA\xB8\xB0") == 0) { /* 자르기 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_str_sub");
            if (!fn) {
                LLVMTypeRef ps[3] = { i8p, i64, i64 };
                LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 3, 0);
                fn = LLVMAddFunction(cg->module, "kc_str_sub", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            /* 2, 3번째 인수 → i64 */
            for (int j = 1; j <= 2 && j < sargc; j++) {
                if (LLVMGetTypeKind(LLVMTypeOf(sargs[j])) == LLVMIntegerTypeKind
                        && LLVMGetIntTypeWidth(LLVMTypeOf(sargs[j])) < 64)
                    sargs[j] = LLVMBuildSExt(cg->builder, sargs[j], i64, "sub_i");
            }
            LLVMTypeRef ps[3] = { i8p, i64, i64 };
            LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 3, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, sargs, 3, "kc_str_sub");
        }

        /* ── i8*(i8*) 형 1인수 → i8* 반환 ── */
        struct { const char *kn; const char *cn; } str1r[] = {
            { "\xEC\x97\xAD\xEC\x88\x9C",                                       "kc_str_reverse"  }, /* 역순 */
            { "\xEB\x8C\x80\xEB\xAC\xB8\xEC\x9E\x90",                          "kc_str_upper"    }, /* 대문자 */
            { "\xEC\x86\x8C\xEB\xAC\xB8\xEC\x9E\x90",                          "kc_str_lower"    }, /* 소문자 */
            { "\xEC\xA0\x9C\xEB\xAA\xA9\xEC\x8B\x9D",                          "kc_str_title"    }, /* 제목식 */
            { "\xEC\x95\x9E\xEA\xB3\xB5\xEB\xB0\xB1\xEC\xA0\x9C\xEA\xB1\xB0", "kc_str_ltrim"    }, /* 앞공백제거 */
            { "\xEB\x92\xA4\xEA\xB3\xB5\xEB\xB0\xB1\xEC\xA0\x9C\xEA\xB1\xB0", "kc_str_rtrim"    }, /* 뒤공백제거 */
            { "\xEA\xB3\xB5\xEB\xB0\xB1\xEC\xA0\x9C\xEA\xB1\xB0",             "kc_str_trim"     }, /* 공백제거 */
            { "\xEB\xB6\x84\xEC\x84\x9D",                                       "kc_str_parse"    }, /* 분석 */
            { NULL, NULL }
        };
        for (int i = 0; str1r[i].kn; i++) {
            if (strcmp(fn_name, str1r[i].kn) != 0) continue;
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, str1r[i].cn);
            if (!fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8p, &i8p, 1, 0);
                fn = LLVMAddFunction(cg->module, str1r[i].cn, ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ft = LLVMFunctionType(i8p, &i8p, 1, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, sargs, 1, str1r[i].cn);
        }

        /* ── i64(i8*, i8*) 형 — 위치/비교 ── */
        struct { const char *kn; const char *cn; } str2i[] = {
            { "\xEC\x9C\x84\xEC\xB9\x98",   "kc_str_indexof" }, /* 위치 */
            { "\xEB\xB9\x84\xEA\xB5\x90",   "kc_str_compare" }, /* 비교 */
            { NULL, NULL }
        };
        for (int i = 0; str2i[i].kn; i++) {
            if (strcmp(fn_name, str2i[i].kn) != 0) continue;
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, str2i[i].cn);
            if (!fn) {
                LLVMTypeRef ps[2] = { i8p, i8p };
                LLVMTypeRef ft = LLVMFunctionType(i64, ps, 2, 0);
                fn = LLVMAddFunction(cg->module, str2i[i].cn, ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ps[2] = { i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(i64, ps, 2, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, sargs, 2, str2i[i].cn);
        }

        /* ── i1(i8*, i8*) 형 — 포함/시작/끝확인/반복확인 ── */
        struct { const char *kn; const char *cn; } str2b[] = {
            { "\xED\x8F\xAC\xED\x95\xA8",                               "kc_str_contains"   }, /* 포함 */
            { "\xEC\x8B\x9C\xEC\x9E\x91",                               "kc_str_startswith" }, /* 시작 */
            { "\xEB\x81\x9D\xED\x99\x95\xEC\x9D\xB8",                  "kc_str_endswith"   }, /* 끝확인 */
            { "\xEB\xB0\x98\xEB\xB3\xB5\xED\x99\x95\xEC\x9D\xB8",     "kc_str_regex"      }, /* 반복확인 */
            { NULL, NULL }
        };
        for (int i = 0; str2b[i].kn; i++) {
            if (strcmp(fn_name, str2b[i].kn) != 0) continue;
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, str2b[i].cn);
            if (!fn) {
                LLVMTypeRef ps[2] = { i8p, i8p };
                LLVMTypeRef ft = LLVMFunctionType(i1, ps, 2, 0);
                fn = LLVMAddFunction(cg->module, str2b[i].cn, ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ps[2] = { i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(i1, ps, 2, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, sargs, 2, str2b[i].cn);
        }

        /* ── 포맷(fmt, ...) → i8* — 가변 인수 ── */
        if (strcmp(fn_name, "\xED\x8F\xAC\xEB\xA7\xB7") == 0) { /* 포맷 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_str_format");
            if (!fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8p, &i8p, 1, 1); /* vararg */
                fn = LLVMAddFunction(cg->module, "kc_str_format", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ft = LLVMFunctionType(i8p, &i8p, 1, 1);
            return LLVMBuildCall2(cg->builder, ft, fn,
                                  sargs, (unsigned)sargc, "kc_str_format");
        }

        /* sargs 미사용 경고 억제 */
        (void)sargs; (void)sargc;
    } /* end 글자 함수 21종 */

    /* ── 파일 내장 함수 17종 + 형변환 3종 LLVM IR (v9.8.0) ─────
     *  파일 함수: kc_file_*(args...) 외부 함수 declare + call
     *  형변환: LLVM 빌더 직접 변환 or kc_to_string() call
     * ============================================================ */
    if (fn_name) {
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);
        LLVMTypeRef i1  = LLVMInt1TypeInContext(cg->ctx);
        LLVMTypeRef vt  = LLVMVoidTypeInContext(cg->ctx);

        int sargc2 = n->child_count - 1;
        LLVMValueRef sargs2[4] = {0};
        for (int i = 0; i < sargc2 && i < 4; i++)
            sargs2[i] = gen_expr(cg, n->children[i + 1]);

        /* 헬퍼: 외부 함수 선언 + call */
#define DECL_EXTERN_FN(nm, ret, ps, nps, va) do { \
    LLVMValueRef _fn = LLVMGetNamedFunction(cg->module, (nm)); \
    if (!_fn) { \
        LLVMTypeRef _ft = LLVMFunctionType((ret),(ps),(nps),(va)); \
        _fn = LLVMAddFunction(cg->module,(nm),_ft); \
        LLVMSetLinkage(_fn, LLVMExternalLinkage); \
    } \
} while(0)

        /* ── i8*(i8*, i8*) — 파일열기(경로, 모드) ── */
        if (strcmp(fn_name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x97\xB4\xEA\xB8\xB0") == 0) { /* 파일열기 */
            LLVMTypeRef ps[2] = { i8p, i8p };
            DECL_EXTERN_FN("kc_file_open", i8p, ps, 2, 0);
            LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_file_open");
            return LLVMBuildCall2(cg->builder, ft, fn, sargs2, 2, "kc_file_open");
        }
        /* ── void(i8*) — 파일닫기 ── */
        if (strcmp(fn_name, "\xED\x8C\x8C\xEC\x9D\xBC\xEB\x8B\xAB\xEA\xB8\xB0") == 0) { /* 파일닫기 */
            DECL_EXTERN_FN("kc_file_close", vt, &i8p, 1, 0);
            LLVMTypeRef ft = LLVMFunctionType(vt, &i8p, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_file_close");
            LLVMBuildCall2(cg->builder, ft, fn, sargs2, 1, "");
            return LLVMConstNull(i8p);
        }
        /* ── i8*(i8*) — 파일읽기 / 파일줄읽기 ── */
        struct { const char *kn; const char *cn; } file1r[] = {
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xBD\xEA\xB8\xB0",         "kc_file_read"     }, /* 파일읽기 */
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA4\x84\xEC\x9D\xBD\xEA\xB8\xB0", "kc_file_readline" }, /* 파일줄읽기 */
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xB4\xEB\xA6\x84",         "kc_file_name"     }, /* 파일이름 */
            { "\xED\x8C\x8C\xEC\x9D\xBC\xED\x99\x95\xEC\x9E\xA5\xEC\x9E\x90", "kc_file_ext"  }, /* 파일확장자 */
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEB\xAA\xA9\xEB\xA1\xB9",         "kc_file_list"     }, /* 파일목록 */
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA0\x84\xEC\xB2\xB4\xEC\x9D\xBD\xEA\xB8\xB0", "kc_file_readall" }, /* 파일전체읽기 */
            { NULL, NULL }
        };
        for (int i = 0; file1r[i].kn; i++) {
            if (strcmp(fn_name, file1r[i].kn) != 0) continue;
            DECL_EXTERN_FN(file1r[i].cn, i8p, &i8p, 1, 0);
            LLVMTypeRef ft = LLVMFunctionType(i8p, &i8p, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, file1r[i].cn);
            return LLVMBuildCall2(cg->builder, ft, fn, sargs2, 1, file1r[i].cn);
        }
        /* ── void(i8*, i8*) — 파일쓰기/파일줄쓰기/파일전체쓰기/파일복사/파일이동 ── */
        struct { const char *kn; const char *cn; } file2v[] = {
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xBD\xB0\xEA\xB8\xB0",     "kc_file_write"     }, /* 파일쓰기 */
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA4\x84\xEC\xBD\xB0\xEA\xB8\xB0", "kc_file_writeline" }, /* 파일줄쓰기 */
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA0\x84\xEC\xB2\xB4\xEC\xBD\xB0\xEA\xB8\xB0", "kc_file_writeall" }, /* 파일전체쓰기 */
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEB\xB3\xB5\xEC\x82\xAC",     "kc_file_copy"      }, /* 파일복사 */
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xB4\xEB\x8F\x99",     "kc_file_move"      }, /* 파일이동 */
            { NULL, NULL }
        };
        for (int i = 0; file2v[i].kn; i++) {
            if (strcmp(fn_name, file2v[i].kn) != 0) continue;
            LLVMTypeRef ps[2] = { i8p, i8p };
            DECL_EXTERN_FN(file2v[i].cn, vt, ps, 2, 0);
            LLVMTypeRef ft = LLVMFunctionType(vt, ps, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, file2v[i].cn);
            LLVMBuildCall2(cg->builder, ft, fn, sargs2, 2, "");
            return LLVMConstNull(i8p);
        }
        /* ── i1(i8*) — 파일있음 ── */
        if (strcmp(fn_name, "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9E\x88\xEC\x9D\x8C") == 0) { /* 파일있음 */
            DECL_EXTERN_FN("kc_file_exists", i1, &i8p, 1, 0);
            LLVMTypeRef ft = LLVMFunctionType(i1, &i8p, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_file_exists");
            return LLVMBuildCall2(cg->builder, ft, fn, sargs2, 1, "kc_file_exists");
        }
        /* ── i64(i8*) — 파일크기 ── */
        if (strcmp(fn_name, "\xED\x8C\x8C\xEC\x9D\xBC\xED\x81\xAC\xEA\xB8\xB0") == 0) { /* 파일크기 */
            DECL_EXTERN_FN("kc_file_size", i64, &i8p, 1, 0);
            LLVMTypeRef ft = LLVMFunctionType(i64, &i8p, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_file_size");
            return LLVMBuildCall2(cg->builder, ft, fn, sargs2, 1, "kc_file_size");
        }
        /* ── void(i8*) — 폴더만들기 / 파일지우기 ── */
        struct { const char *kn; const char *cn; } file1v[] = {
            { "\xED\x8F\xB4\xEB\x8D\xB0\xEB\xA7\x8C\xEB\x93\xA4\xEA\xB8\xB0", "kc_dir_make"    }, /* 폴더만들기 */
            { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA7\x80\xEC\x9A\xB0\xEA\xB8\xB0", "kc_file_delete" }, /* 파일지우기 */
            { NULL, NULL }
        };
        for (int i = 0; file1v[i].kn; i++) {
            if (strcmp(fn_name, file1v[i].kn) != 0) continue;
            DECL_EXTERN_FN(file1v[i].cn, vt, &i8p, 1, 0);
            LLVMTypeRef ft = LLVMFunctionType(vt, &i8p, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, file1v[i].cn);
            LLVMBuildCall2(cg->builder, ft, fn, sargs2, 1, "");
            return LLVMConstNull(i8p);
        }

        /* ── 형변환 3종 ── */
        /* 정수(v) → i64 */
        if (strcmp(fn_name, "\xEC\xA0\x95\xEC\x88\x98") == 0) { /* 정수 */
            if (sargc2 < 1) return LLVMConstInt(i64, 0, 0);
            LLVMValueRef v = sargs2[0];
            LLVMTypeRef  tv = LLVMTypeOf(v);
            if (LLVMGetTypeKind(tv) == LLVMDoubleTypeKind)
                return LLVMBuildFPToSI(cg->builder, v, i64, "to_int");
            if (LLVMGetTypeKind(tv) == LLVMPointerTypeKind)
                return LLVMBuildPtrToInt(cg->builder, v, i64, "ptr_int");
            if (LLVMGetTypeKind(tv) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(tv) < 64)
                return LLVMBuildSExt(cg->builder, v, i64, "sext_int");
            return v; /* 이미 i64 */
        }
        /* 실수(v) → double */
        if (strcmp(fn_name, "\xEC\x8B\xA4\xEC\x88\x98") == 0) { /* 실수 */
            if (sargc2 < 1) return LLVMConstReal(LLVMDoubleTypeInContext(cg->ctx), 0.0);
            LLVMValueRef v  = sargs2[0];
            LLVMTypeRef  tv = LLVMTypeOf(v);
            LLVMTypeRef  f64 = LLVMDoubleTypeInContext(cg->ctx);
            if (LLVMGetTypeKind(tv) == LLVMIntegerTypeKind)
                return LLVMBuildSIToFP(cg->builder, v, f64, "to_float");
            if (LLVMGetTypeKind(tv) == LLVMDoubleTypeKind) return v;
            return LLVMBuildBitCast(cg->builder, v, f64, "bc_float");
        }
        /* 문자(v) → i8* : kc_to_string(i64) */
        if (strcmp(fn_name, "\xEB\xAC\xB8\xEC\x9E\x90") == 0) { /* 문자 */
            DECL_EXTERN_FN("kc_to_string", i8p, &i64, 1, 0);
            LLVMTypeRef ft = LLVMFunctionType(i8p, &i64, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_to_string");
            if (sargc2 < 1) sargs2[0] = LLVMConstInt(i64, 0, 0);
            /* 값을 i64로 통일 */
            LLVMTypeRef tv = LLVMTypeOf(sargs2[0]);
            if (LLVMGetTypeKind(tv) == LLVMDoubleTypeKind)
                sargs2[0] = LLVMBuildFPToSI(cg->builder, sargs2[0], i64, "f_to_i");
            else if (LLVMGetTypeKind(tv) == LLVMIntegerTypeKind &&
                     LLVMGetIntTypeWidth(tv) < 64)
                sargs2[0] = LLVMBuildSExt(cg->builder, sargs2[0], i64, "sext_str");
            else if (LLVMGetTypeKind(tv) == LLVMPointerTypeKind)
                return sargs2[0]; /* 이미 문자열 */
            return LLVMBuildCall2(cg->builder, ft, fn, sargs2, 1, "kc_to_string");
        }

#undef DECL_EXTERN_FN
        (void)sargs2; (void)sargc2;
    } /* end 파일 함수 + 형변환 */

    /* ── 수학/AI 내장 함수 LLVM IR (v9.9.0) ────────────────────
     *  kc_abs / kc_max / kc_min / kc_len / kc_range
     *  kc_mse / kc_cross_entropy / kc_softmax / kc_positional_encoding
     *  kc_geom_series / kc_arith_series / kc_recur_geom
     *  sqrt / pow (이미 libm에서 선언됐을 수 있으므로 GetNamedFunction 우선)
     * ============================================================ */
    if (fn_name) {
        LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
        LLVMTypeRef i64  = LLVMInt64TypeInContext(cg->ctx);
        LLVMTypeRef f64  = LLVMDoubleTypeInContext(cg->ctx);

        int marg = n->child_count - 1;
        LLVMValueRef margs[8] = {0};
        for (int i = 0; i < marg && i < 8; i++)
            margs[i] = gen_expr(cg, n->children[i + 1]);

        /* 헬퍼: 값을 f64로 변환 */
#define TO_F64(v) (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMDoubleTypeKind ? (v) \
                   : LLVMBuildSIToFP(cg->builder, (v), f64, "to_f64"))
        /* 헬퍼: 값을 i64로 변환 */
#define TO_I64(v) (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind && \
                   LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 64 ? (v) \
                   : LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMDoubleTypeKind \
                     ? LLVMBuildFPToSI(cg->builder, (v), i64, "to_i64") \
                     : LLVMBuildSExt(cg->builder, (v), i64, "sext_i64"))

        /* ── 절댓값(v) → kc_abs(i64) → i64 ── */
        if (strcmp(fn_name, "\xEC\xA0\x88\xEB\x8C\x93\xEA\xB0\x92") == 0) { /* 절댓값 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_abs");
            if (!fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, &i64, 1, 0);
                fn = LLVMAddFunction(cg->module, "kc_abs", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ft = LLVMFunctionType(i64, &i64, 1, 0);
            margs[0] = TO_I64(margs[0]);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 1, "kc_abs");
        }
        /* ── 최대/최소(v,...) → kc_max/kc_min vararg ── */
        struct { const char *kn; const char *cn; } mm2[] = {
            { "\xEC\xB5\x9C\xEB\x8C\x80", "kc_max" }, /* 최대 */
            { "\xEC\xB5\x9C\xEC\x86\x8C", "kc_min" }, /* 최소 */
            { NULL, NULL }
        };
        for (int i = 0; mm2[i].kn; i++) {
            if (strcmp(fn_name, mm2[i].kn) != 0) continue;
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, mm2[i].cn);
            if (!fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, &i64, 1, 1); /* vararg */
                fn = LLVMAddFunction(cg->module, mm2[i].cn, ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            /* 모든 인수 i64 변환 */
            for (int j = 0; j < marg && j < 8; j++)
                margs[j] = TO_I64(margs[j]);
            LLVMTypeRef ft = LLVMFunctionType(i64, &i64, 1, 1);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, (unsigned)marg, mm2[i].cn);
        }
        /* ── 길이(v) → kc_len(i8*) → i64 ── */
        if (strcmp(fn_name, "\xEA\xB8\xB8\xEC\x9D\xB4") == 0) { /* 길이 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_len");
            if (!fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, &i8p, 1, 0);
                fn = LLVMAddFunction(cg->module, "kc_len", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ft = LLVMFunctionType(i64, &i8p, 1, 0);
            /* 포인터가 아니면 그냥 0 */
            if (marg < 1 || LLVMGetTypeKind(LLVMTypeOf(margs[0])) != LLVMPointerTypeKind)
                margs[0] = LLVMConstNull(i8p);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 1, "kc_len");
        }
        /* ── 범위(start, end[, step]) → kc_range(i64, i64[, i64]) → i8* ── */
        if (strcmp(fn_name, "\xEB\xB2\x94\xEC\x9C\x84") == 0) { /* 범위 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_range");
            if (!fn) {
                LLVMTypeRef ps[3] = { i64, i64, i64 };
                LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 3, 0);
                fn = LLVMAddFunction(cg->module, "kc_range", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            for (int j = 0; j < marg && j < 3; j++)
                margs[j] = TO_I64(margs[j]);
            /* step 기본값 1 */
            if (marg < 3) margs[2] = LLVMConstInt(i64, 1, 0);
            LLVMTypeRef ps[3] = { i64, i64, i64 };
            LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 3, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 3, "kc_range");
        }
        /* ── 제곱근(v) → sqrt(double) → double ── */
        if (strcmp(fn_name, "\xEC\xA0\x9C\xEA\xB3\xB1\xEA\xB7\xBC") == 0) { /* 제곱근 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "sqrt");
            if (!fn) {
                LLVMTypeRef ft = LLVMFunctionType(f64, &f64, 1, 0);
                fn = LLVMAddFunction(cg->module, "sqrt", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ft = LLVMFunctionType(f64, &f64, 1, 0);
            margs[0] = TO_F64(margs[0]);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 1, "sqrt");
        }
        /* ── 제곱(base, exp) → pow(double, double) → double ── */
        if (strcmp(fn_name, "\xEC\xA0\x9C\xEA\xB3\xB1") == 0) { /* 제곱 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "pow");
            if (!fn) {
                LLVMTypeRef ps[2] = { f64, f64 };
                LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
                fn = LLVMAddFunction(cg->module, "pow", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            margs[0] = TO_F64(margs[0]);
            margs[1] = (marg >= 2) ? TO_F64(margs[1]) : LLVMConstReal(f64, 2.0);
            LLVMTypeRef ps[2] = { f64, f64 };
            LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 2, "pow");
        }

        /* ── AI 수학 내장 함수 ── */
        /* 평균제곱오차(arr1, arr2) → kc_mse(i8*, i8*) → double */
        if (strcmp(fn_name, "\xED\x8F\x89\xEA\xB7\xA0\xEC\xA0\x9C\xEA\xB3\xB1\xEC\x98\xA4\xEC\xB0\xA8") == 0) { /* 평균제곱오차 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_mse");
            if (!fn) {
                LLVMTypeRef ps[2] = { i8p, i8p };
                LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
                fn = LLVMAddFunction(cg->module, "kc_mse", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ps[2] = { i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 2, "kc_mse");
        }
        /* 교차엔트로피(arr1, arr2) → kc_cross_entropy(i8*, i8*) → double */
        if (strcmp(fn_name, "\xEA\xB5\x90\xEC\xB0\xA8\xEC\x97\x94\xED\x8A\xB8\xEB\xA1\x9C\xED\x94\xBC") == 0) { /* 교차엔트로피 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_cross_entropy");
            if (!fn) {
                LLVMTypeRef ps[2] = { i8p, i8p };
                LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
                fn = LLVMAddFunction(cg->module, "kc_cross_entropy", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ps[2] = { i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 2, "kc_cross_entropy");
        }
        /* 소프트맥스(arr) → kc_softmax(i8*) → i8* */
        if (strcmp(fn_name, "\xEC\x86\x8C\xED\x94\x84\xED\x8A\xB8\xEB\xA7\xA5\xEC\x8A\xA4") == 0) { /* 소프트맥스 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_softmax");
            if (!fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8p, &i8p, 1, 0);
                fn = LLVMAddFunction(cg->module, "kc_softmax", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ft = LLVMFunctionType(i8p, &i8p, 1, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 1, "kc_softmax");
        }
        /* 위치인코딩(pos, dim) → kc_positional_encoding(i64, i64) → i8* */
        if (strcmp(fn_name, "\xEC\x9C\x84\xEC\xB9\x98\xEC\x9D\xB8\xEC\xBD\x94\xEB\x94\xA9") == 0) { /* 위치인코딩 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_positional_encoding");
            if (!fn) {
                LLVMTypeRef ps[2] = { i64, i64 };
                LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 2, 0);
                fn = LLVMAddFunction(cg->module, "kc_positional_encoding", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            margs[0] = TO_I64(margs[0]);
            margs[1] = (marg >= 2) ? TO_I64(margs[1]) : LLVMConstInt(i64, 64, 0);
            LLVMTypeRef ps[2] = { i64, i64 };
            LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 2, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 2, "kc_positional_encoding");
        }
        /* ── 수열 함수 ── */
        /* 등비수열합(a, r) → kc_geom_series(f64, f64) → f64 */
        if (strcmp(fn_name, "\xEB\x93\xB1\xEB\xB9\x84\xEC\x88\x98\xEC\x97\xB4\xED\x95\xA9") == 0) { /* 등비수열합 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_geom_series");
            if (!fn) {
                LLVMTypeRef ps[2] = { f64, f64 };
                LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
                fn = LLVMAddFunction(cg->module, "kc_geom_series", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            margs[0] = TO_F64(margs[0]);
            margs[1] = (marg >= 2) ? TO_F64(margs[1]) : LLVMConstReal(f64, 1.0);
            LLVMTypeRef ps[2] = { f64, f64 };
            LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 2, "kc_geom_series");
        }
        /* 등차수열합(a, d, n) → kc_arith_series(f64, f64, i64) → f64 */
        if (strcmp(fn_name, "\xEB\x93\xB1\xEC\xB0\xA8\xEC\x88\x98\xEC\x97\xB4\xED\x95\xA9") == 0) { /* 등차수열합 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_arith_series");
            if (!fn) {
                LLVMTypeRef ps[3] = { f64, f64, i64 };
                LLVMTypeRef ft = LLVMFunctionType(f64, ps, 3, 0);
                fn = LLVMAddFunction(cg->module, "kc_arith_series", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            margs[0] = TO_F64(margs[0]);
            margs[1] = (marg >= 2) ? TO_F64(margs[1]) : LLVMConstReal(f64, 1.0);
            margs[2] = (marg >= 3) ? TO_I64(margs[2])  : LLVMConstInt(i64, 1, 0);
            LLVMTypeRef ps[3] = { f64, f64, i64 };
            LLVMTypeRef ft = LLVMFunctionType(f64, ps, 3, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 3, "kc_arith_series");
        }
        /* 점화식값(a1, r, n) → kc_recur_geom(f64, f64, i64) → f64 */
        if (strcmp(fn_name, "\xEC\xA0\x90\xED\x99\x94\xEC\x8B\x9D\xEA\xB0\x92") == 0) { /* 점화식값 */
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_recur_geom");
            if (!fn) {
                LLVMTypeRef ps[3] = { f64, f64, i64 };
                LLVMTypeRef ft = LLVMFunctionType(f64, ps, 3, 0);
                fn = LLVMAddFunction(cg->module, "kc_recur_geom", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            margs[0] = TO_F64(margs[0]);
            margs[1] = (marg >= 2) ? TO_F64(margs[1]) : LLVMConstReal(f64, 1.0);
            margs[2] = (marg >= 3) ? TO_I64(margs[2])  : LLVMConstInt(i64, 1, 0);
            LLVMTypeRef ps[3] = { f64, f64, i64 };
            LLVMTypeRef ft = LLVMFunctionType(f64, ps, 3, 0);
            return LLVMBuildCall2(cg->builder, ft, fn, margs, 3, "kc_recur_geom");
        }

        /* ── 추가(배열 push) — kc_array_push(arr:i8*, val:i64) → void ── */
        if (strcmp(fn_name, "\xEC\xB6\x94\xEA\xB0\x80") == 0) { /* 추가 */
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_array_push");
            if (!fn) {
                LLVMTypeRef ps[2] = { i8p, i64 };
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), ps, 2, 0);
                fn = LLVMAddFunction(cg->module, "kc_array_push", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMValueRef arr = (marg >= 1) ? margs[0] : LLVMConstNull(i8p);
            LLVMValueRef val = (marg >= 2) ? TO_I64(margs[1]) : LLVMConstInt(i64, 0, 0);
            LLVMTypeRef ps[2] = { i8p, i64 };
            LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), ps, 2, 0);
            LLVMValueRef call_args[2] = { arr, val };
            return LLVMBuildCall2(cg->builder, ft, fn, call_args, 2, "");
        }

        /* ── 통계 함수 11종 (i8*→double) / 배열유틸 2종 (i8*→void) ── */
        {
            struct { const char *kn; const char *cn; int ret_void; } stat_fns[] = {
                { "\xED\x95\xA9\xEA\xB3\x84",                         "kc_stat_sum",         0 }, /* 합계 */
                { "\xED\x8F\x89\xEA\xB7\xA0",                         "kc_stat_mean",        0 }, /* 평균 */
                { "\xEB\xB6\x84\xEC\x82\xB0",                         "kc_stat_variance",    0 }, /* 분산 */
                { "\xED\x91\x9C\xEC\xA4\x80\xED\x8E\xB8\xEC\xB0\xA8","kc_stat_stddev",       0 }, /* 표준편차 */
                { "\xEC\xA4\x91\xEC\x95\x99\xEA\xB0\x92",             "kc_stat_median",      0 }, /* 중앙값 */
                { "\xEC\xB5\x9C\xEB\xB9\x88\xEA\xB0\x92",             "kc_stat_mode",        0 }, /* 최빈값 */
                { "\xEB\x88\x84\xEC\xA0\x81\xED\x95\xA9",             "kc_stat_cumsum",      0 }, /* 누적합 */
                { "\xEA\xB3\xB5\xEB\xB6\x84\xEC\x82\xB0",             "kc_stat_covariance",  0 }, /* 공분산 */
                { "\xEC\x83\x81\xEA\xB4\x80\xEA\xB3\x84\xEC\x88\x98","kc_stat_correlation",  0 }, /* 상관계수 */
                { "\xEC\xA0\x95\xEA\xB7\x9C\xED\x99\x94",             "kc_stat_normalize",   0 }, /* 정규화 */
                { "\xED\x91\x9C\xEC\xA4\x80\xED\x99\x94",             "kc_stat_standardize", 0 }, /* 표준화 */
                { "\xEB\xB0\xB0\xEC\x97\xB4\xEC\xA0\x95\xEB\xA0\xAC","kc_arr_sort",          1 }, /* 배열정렬 */
                { "\xEB\xB0\xB0\xEC\x97\xB4\xEB\x92\xA4\xEC\xA7\x9D\xEA\xB8\xB0","kc_arr_reverse", 1 }, /* 배열뒤집기 */
                { NULL, NULL, 0 }
            };
            for (int si = 0; stat_fns[si].kn; si++) {
                if (strcmp(fn_name, stat_fns[si].kn) != 0) continue;
                LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
                LLVMValueRef fn = LLVMGetNamedFunction(cg->module, stat_fns[si].cn);
                if (stat_fns[si].ret_void) {
                    /* 배열정렬/뒤집기: (i8*)→void */
                    if (!fn) {
                        LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                          &i8p, 1, 0);
                        fn = LLVMAddFunction(cg->module, stat_fns[si].cn, ft);
                        LLVMSetLinkage(fn, LLVMExternalLinkage);
                    }
                    LLVMValueRef a0 = (marg >= 1) ? margs[0] : LLVMConstNull(i8p);
                    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                      &i8p, 1, 0);
                    return LLVMBuildCall2(cg->builder, ft, fn, &a0, 1, "");
                } else {
                    /* 통계 함수: 1인수(i8*)→double 또는 2인수(i8*,i8*)→double */
                    int is2 = (si >= 7 && si <= 8); /* 공분산/상관계수 2인수 */
                    if (!fn) {
                        LLVMTypeRef ps[2] = { i8p, i8p };
                        LLVMTypeRef ft = LLVMFunctionType(f64, ps, is2 ? 2 : 1, 0);
                        fn = LLVMAddFunction(cg->module, stat_fns[si].cn, ft);
                        LLVMSetLinkage(fn, LLVMExternalLinkage);
                    }
                    if (is2) {
                        LLVMValueRef a0 = (marg >= 1) ? margs[0] : LLVMConstNull(i8p);
                        LLVMValueRef a1 = (marg >= 2) ? margs[1] : LLVMConstNull(i8p);
                        LLVMTypeRef ps[2] = { i8p, i8p };
                        LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
                        LLVMValueRef ca[2] = { a0, a1 };
                        return LLVMBuildCall2(cg->builder, ft, fn, ca, 2, stat_fns[si].cn);
                    } else {
                        LLVMValueRef a0 = (marg >= 1) ? margs[0] : LLVMConstNull(i8p);
                        LLVMTypeRef ft = LLVMFunctionType(f64, &i8p, 1, 0);
                        return LLVMBuildCall2(cg->builder, ft, fn, &a0, 1, stat_fns[si].cn);
                    }
                }
            }
        }

        /* ── AI 활성함수 3종 (double→double) ── */
        {
            struct { const char *kn; const char *cn; } actv[] = {
                { "\xEC\x8B\x9C\xEA\xB7\xB8\xEB\xAA\xA8\xEC\x9D\xB4\xEB\x93\x9C", "kc_sigmoid"  }, /* 시그모이드 */
                { "\xEB\xA0\x90\xEB\xA3\xA8",                                         "kc_relu"     }, /* 렐루 */
                { "\xEC\x8C\x8D\xEA\xB3\xA1\xED\x83\x84\xEC\xA0\xA0\xED\x8A\xB8",   "kc_tanh_fn"  }, /* 쌍곡탄젠트 */
                { NULL, NULL }
            };
            for (int ai = 0; actv[ai].kn; ai++) {
                if (strcmp(fn_name, actv[ai].kn) != 0) continue;
                LLVMValueRef fn = LLVMGetNamedFunction(cg->module, actv[ai].cn);
                if (!fn) {
                    LLVMTypeRef ft = LLVMFunctionType(f64, &f64, 1, 0);
                    fn = LLVMAddFunction(cg->module, actv[ai].cn, ft);
                    LLVMSetLinkage(fn, LLVMExternalLinkage);
                }
                LLVMValueRef a0 = TO_F64((marg >= 1) ? margs[0]
                                          : LLVMConstReal(f64, 0.0));
                LLVMTypeRef ft = LLVMFunctionType(f64, &f64, 1, 0);
                return LLVMBuildCall2(cg->builder, ft, fn, &a0, 1, actv[ai].cn);
            }
        }

        /* ── 호감도 — kc_attraction(i8*,i8*)→double ── */
        if (strcmp(fn_name, "\xED\x98\xB8\xEA\xB0\x90\xEB\x8F\x84") == 0) { /* 호감도 */
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_attraction");
            if (!fn) {
                LLVMTypeRef ps[2] = { i8p, i8p };
                LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
                fn = LLVMAddFunction(cg->module, "kc_attraction", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMValueRef a0 = (marg >= 1) ? margs[0] : LLVMConstNull(i8p);
            LLVMValueRef a1 = (marg >= 2) ? margs[1] : LLVMConstNull(i8p);
            LLVMTypeRef ps[2] = { i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(f64, ps, 2, 0);
            LLVMValueRef ca[2] = { a0, a1 };
            return LLVMBuildCall2(cg->builder, ft, fn, ca, 2, "kc_attraction");
        }

        /* ── autograd 내장함수 13종 (v15.0.0) ─────────────────────────────
         *  모두 i8*(KcTensor*) 포인터 기반으로 처리:
         *    단항 → (i8*) → i8*
         *    이항 → (i8*, i8*) → i8*
         *    void  → (i8*) → void
         * ──────────────────────────────────────────────────────────────── */
        {
            LLVMTypeRef i8p   = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef void_ = LLVMVoidTypeInContext(cg->ctx);

            /* 헬퍼 매크로: 외부 함수 가져오기 또는 선언 */
    LLVMGetNamedFunction(cg->module, sym) ? LLVMGetNamedFunction(cg->module, sym) : \
    ({ LLVMTypeRef ft_ = LLVMFunctionType(i8p, (LLVMTypeRef[]){i8p,i8p}, 2, 0); \
       LLVMValueRef f_ = LLVMAddFunction(cg->module, sym, ft_); \
       LLVMSetLinkage(f_, LLVMExternalLinkage); f_; })

            /* 역전파(t) → kc_autograd_backward(t) : void */
            if (strcmp(fn_name, "\xEC\x97\xAD\xEC\xA0\x84\xED\x8C\x8C") == 0) { /* 역전파 */
                LLVMTypeRef ft = LLVMFunctionType(void_, (LLVMTypeRef[]){i8p}, 1, 0);
                LLVMValueRef f = LLVMGetNamedFunction(cg->module, "kc_autograd_backward");
                if (!f) { f = LLVMAddFunction(cg->module, "kc_autograd_backward", ft); LLVMSetLinkage(f, LLVMExternalLinkage); }
                LLVMValueRef a = (marg >= 1) ? margs[0] : LLVMConstNull(i8p);
                return LLVMBuildCall2(cg->builder, ft, f, &a, 1, "");
            }
            /* 기울기초기화(t) → kc_autograd_zero_grad(t) : void */
            if (strcmp(fn_name, "\xEA\xB8\xB0\xEC\x9A\xB8\xEA\xB8\xB0\xEC\xB4\x88\xEA\xB8\xB0\xED\x99\x94") == 0) { /* 기울기초기화 */
                LLVMTypeRef ft = LLVMFunctionType(void_, (LLVMTypeRef[]){i8p}, 1, 0);
                LLVMValueRef f = LLVMGetNamedFunction(cg->module, "kc_autograd_zero_grad");
                if (!f) { f = LLVMAddFunction(cg->module, "kc_autograd_zero_grad", ft); LLVMSetLinkage(f, LLVMExternalLinkage); }
                LLVMValueRef a = (marg >= 1) ? margs[0] : LLVMConstNull(i8p);
                return LLVMBuildCall2(cg->builder, ft, f, &a, 1, "");
            }
            /* 미분추적(t) → kc_tensor_ensure_grad(t) : void */
            if (strcmp(fn_name, "\xEB\xBF\xB8\xEB\xB6\x84\xEC\xB6\x94\xEC\xA0\x81") == 0) { /* 미분추적 */
                LLVMTypeRef ft = LLVMFunctionType(void_, (LLVMTypeRef[]){i8p}, 1, 0);
                LLVMValueRef f = LLVMGetNamedFunction(cg->module, "kc_tensor_ensure_grad");
                if (!f) { f = LLVMAddFunction(cg->module, "kc_tensor_ensure_grad", ft); LLVMSetLinkage(f, LLVMExternalLinkage); }
                LLVMValueRef a = (marg >= 1) ? margs[0] : LLVMConstNull(i8p);
                return LLVMBuildCall2(cg->builder, ft, f, &a, 1, "");
            }

            /* 이항 autograd: 미분더하기/미분곱/미분행렬곱 */
            static const struct { const char *kw; const char *sym; } ag2[] = {
                { "\xEB\xBF\xB8\xEB\xB6\x84\xEB\x8D\x94\xED\x95\x98\xEA\xB8\xB0", "kc_ag_add"    }, /* 미분더하기 */
                { "\xEB\xBF\xB8\xEB\xB6\x84\xEA\xB3\xB1",                          "kc_ag_mul"    }, /* 미분곱     */
                { "\xEB\xBF\xB8\xEB\xB6\x84\xED\x96\x89\xEB\xA0\xAC\xEA\xB3\xB1", "kc_ag_matmul" }, /* 미분행렬곱 */
                { NULL, NULL }
            };
            for (int _i = 0; ag2[_i].kw; _i++) {
                if (strcmp(fn_name, ag2[_i].kw) == 0) {
                    LLVMTypeRef ft = LLVMFunctionType(i8p, (LLVMTypeRef[]){i8p,i8p}, 2, 0);
                    LLVMValueRef f = LLVMGetNamedFunction(cg->module, ag2[_i].sym);
                    if (!f) { f = LLVMAddFunction(cg->module, ag2[_i].sym, ft); LLVMSetLinkage(f, LLVMExternalLinkage); }
                    LLVMValueRef a0 = (marg >= 1) ? margs[0] : LLVMConstNull(i8p);
                    LLVMValueRef a1 = (marg >= 2) ? margs[1] : LLVMConstNull(i8p);
                    LLVMValueRef ca[2] = { a0, a1 };
                    return LLVMBuildCall2(cg->builder, ft, f, ca, 2, ag2[_i].sym);
                }
            }

            /* 단항 autograd: 미분렐루/미분시그모이드/미분쌍곡탄젠트/미분로그/미분합산/미분평균/미분제곱 */
            static const struct { const char *kw; const char *sym; } ag1[] = {
                { "\xEB\xBF\xB8\xEB\xB6\x84\xEB\xA0\x90\xEB\xA3\xA8",                             "kc_ag_relu"    }, /* 미분렐루       */
                { "\xEB\xBF\xB8\xEB\xB6\x84\xEC\x8B\x9C\xEA\xB7\xB8\xEB\xAF\xB8\xEC\x9D\xB4\xEB\x93\x9C", "kc_ag_sigmoid" }, /* 미분시그모이드 */
                { "\xEB\xBF\xB8\xEB\xB6\x84\xEC\x8C\x8D\xEA\xB3\xA1\xED\x83\x84\xEC\xA0\xA0\xED\x8A\xB8", "kc_ag_tanh_op" }, /* 미분쌍곡탄젠트 */
                { "\xEB\xBF\xB8\xEB\xB6\x84\xEB\xAC\xB4\xEB\xA1\x9C\xED\x81\xAC",                "kc_ag_log"     }, /* 미분로그       */
                { "\xEB\xBF\xB8\xEB\xB6\x84\xED\x95\xA9\xEC\x82\xB0",                             "kc_ag_sum"     }, /* 미분합산       */
                { "\xEB\xBF\xB8\xEB\xB6\x84\xED\x8F\x89\xEA\xB7\x9C",                             "kc_ag_mean"    }, /* 미분평균       */
                { "\xEB\xBF\xB8\xEB\xB6\x84\xEC\xA0\x9C\xEA\xB3\xB1",                             "kc_ag_pow2"    }, /* 미분제곱       */
                { NULL, NULL }
            };
            for (int _i = 0; ag1[_i].kw; _i++) {
                if (strcmp(fn_name, ag1[_i].kw) == 0) {
                    LLVMTypeRef ft = LLVMFunctionType(i8p, (LLVMTypeRef[]){i8p}, 1, 0);
                    LLVMValueRef f = LLVMGetNamedFunction(cg->module, ag1[_i].sym);
                    if (!f) { f = LLVMAddFunction(cg->module, ag1[_i].sym, ft); LLVMSetLinkage(f, LLVMExternalLinkage); }
                    LLVMValueRef a = (marg >= 1) ? margs[0] : LLVMConstNull(i8p);
                    return LLVMBuildCall2(cg->builder, ft, f, &a, 1, ag1[_i].sym);
                }
            }

        } /* end autograd */

#undef TO_F64
#undef TO_I64
        (void)margs; (void)marg;
    } /* end 수학/AI 내장 함수 */

    /* ── 산업/임베디드/안전/AI 내장 함수 (v16~v18) ─────────────── */
    {
        typedef struct { const char *kw; const char *sym; int argc_mode; } IndEntry;
        /* argc_mode: 0=void(), 1=i64(i64), 2=void(i64,i64), 3=void(i8p), 4=void(i8p,i8p) */
        static const IndEntry ind[] = {
            /* GPIO */
            { "GPIO\xEC\x93\xB0\xEA\xB8\xB0",    "kc_gpio_write",        2 }, /* GPIO쓰기     */
            { "GPIO\xEC\x9D\xBD\xEA\xB8\xB0",    "kc_gpio_read",         1 }, /* GPIO읽기     */
            /* MQTT */
            { "MQTT\xEC\x97\xB0\xEA\xB2\xB0",    "kc_mqtt_connect",      4 }, /* MQTT연결     */
            { "MQTT\xEB\xB0\x9C\xED\x96\x89",    "kc_mqtt_publish",      4 }, /* MQTT발행     */
            { "MQTT\xEA\xB5\xAC\xEB\x8F\x85",    "kc_mqtt_subscribe",    3 }, /* MQTT구독     */
            { "MQTT\xEC\x97\xB0\xEA\xB2\xB0\xEB\x81\x8A\xEA\xB8\xB0", "kc_mqtt_disconnect", 0 }, /* MQTT연결끊기 */
            /* ROS2 */
            { "ROS2\xEB\xB0\x9C\xED\x96\x89",    "kc_ros2_publish",      4 }, /* ROS2발행     */
            { "ROS2\xEA\xB5\xAC\xEB\x8F\x85",    "kc_ros2_subscribe",    3 }, /* ROS2구독     */
            /* 안전 규격 v17.0.0 */
            { "\xED\x8E\x98\xEC\x9D\xBC\xEC\x84\xB8\xEC\x9D\xB4\xED\x94\xBC", "kc_failsafe",       0 }, /* 페일세이프   */
            { "\xEA\xB8\xB4\xEA\xB8\x89\xEC\xA0\x95\xEC\xA7\x80",             "kc_emergency_stop", 0 }, /* 긴급정지     */
            { "\xEA\xB2\xBD\xEB\xB3\xB4\xEB\xB0\x9C\xEB\xA0\xB9",             "kc_alarm",          3 }, /* 경보발령     */
            /* 온디바이스 AI v18.0.0 */
            { "AI\xEB\xB6\x88\xEB\xA1\x9C\xEC\x98\xA4\xEA\xB8\xB0",           "kc_ai_load",        3 }, /* AI불러오기   */
            { "AI\xEC\xB6\x94\xEB\xA1\x0C",                                    "kc_ai_predict",     1 }, /* AI추론       */
            { "AI\xED\x95\x99\xEC\x8A\xB5\xEB\x8B\xA8\xEA\xB3\x84",           "kc_ai_train_step",  1 }, /* AI학습단계   */
            { "AI\xEC\xA0\x80\xEC\x9E\xA5",                                    "kc_ai_save",        4 }, /* AI저장       */
            /* ── v18.1.0 신규 내장함수 LLVM IR ── */
            /* 역삼각함수 (double→double, C libm) */
            { "\xEC\x95\x84\xED\x81\xAC\xEC\x82\xAC\xEC\xA0\x84",             "asin",              5 }, /* 아크사인     */
            { "\xEC\x95\x84\xED\x81\xAC\xEC\xBD\x94\xEC\x82\xAC\xEC\xA0\x84","acos",              5 }, /* 아크코사인   */
            { "\xEC\x95\x84\xED\x81\xAC\xED\x83\x84\xEC\xA0\x84\xED\x8A\xB8","atan",              5 }, /* 아크탄젠트   */
            /* 글자 추가 (i8*,i64→i8*) */
            { "\xEC\xA2\x8C\xEB\xB2\x84\xEC\x9E\x90",                         "kc_str_left",       6 }, /* 좌문자       */
            { "\xEC\x9A\xB0\xEB\xB2\x84\xEC\x9E\x90",                         "kc_str_right",      6 }, /* 우문자       */
            { "\xEC\xBD\x94\xEB\x93\x9C",                                      "kc_str_code",       3 }, /* 코드         */
            { "\xEB\xB6\x99\xEC\x97\xAC\xEC\x93\xB8",                         "kc_str_compact",    3 }, /* 붙여씀       */
            /* 배열 고급 (i8*,i64→i64 or i8*) */
            { "\xEB\xB0\xB0\xEC\x97\xB4\xEC\x82\xAC\xEC\xA0\x9C",            "kc_arr_remove",     2 }, /* 배열삭제     */
            { "\xEB\xB0\xB0\xEC\x97\xB4\xEC\xB0\xBE\xEA\xB8\xB0",            "kc_arr_indexof",    2 }, /* 배열찾기     */
            { "\xEB\xB0\xB0\xEC\x97\xB4\xED\x8F\xAC\xED\x95\xA8",            "kc_arr_contains",   2 }, /* 배열포함     */
            { "\xEB\xB0\xB0\xEC\x97\xB4\xED\x95\xA9\xEC\xB9\x98\xEA\xB8\xB0","kc_arr_concat",    4 }, /* 배열합치기   */
            { "\xEC\x9C\xA0\xEC\x9D\xBC\xEA\xB0\x92",                         "kc_arr_unique",     3 }, /* 유일값       */
            /* 시간/날짜 (→i64 or i8*) */
            { "\xED\x98\x84\xEC\xA7\x80\xEC\x8B\x9C\xEA\xB0\x84",            "kc_time_now",       0 }, /* 현재시간     */
            { "\xED\x98\x84\xEC\xA7\x80\xEB\x82\xA0\xEC\xA7\x9C",            "kc_date_now",       0 }, /* 현재날짜     */
            { "\xEA\xB2\xBD\xEA\xB3\xBC\xEC\x8B\x9C\xEA\xB0\x84",           "kc_elapsed",        0 }, /* 경과시간     */
            /* 시스템 */
            { "\xED\x99\x98\xEA\xB2\xBD\xEB\xB3\x80\xEC\x88\x98",            "getenv",            3 }, /* 환경변수     */
            { "\xEC\xA2\x85\xEB\xA3\x8C",                                     "exit",              1 }, /* 종료         */
            { "\xEB\xAC\xB8\xEC\x88\x98\xEB\xAC\xB8\xEC\x9E\x90",            "system",            3 }, /* 명령실행     */
            { "\xEC\x9E\xA0\xEA\xB9\x90",                                     "kc_sleep_ms",       1 }, /* 잠깐         */
            /* JSON */
            { "JSON\xEC\x83\x9D\xEC\x84\xB1",                                 "kc_json_encode",    3 }, /* JSON생성     */
            { "JSON\xED\x8C\x8C\xEC\x8B\xB1",                                 "kc_json_decode",    3 }, /* JSON파싱     */
            { NULL, NULL, 0 }
        };
        for (int _i = 0; ind[_i].kw; _i++) {
            if (strcmp(fn_name, ind[_i].kw) != 0) continue;
            LLVMTypeRef ret_t = LLVMVoidTypeInContext(cg->ctx);
            LLVMTypeRef i64t  = LLVMInt64TypeInContext(cg->ctx);
            LLVMTypeRef i8p   = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef ft; LLVMValueRef fn_f;
            switch (ind[_i].argc_mode) {
                case 0:
                    ft = LLVMFunctionType(ret_t, NULL, 0, 0);
                    fn_f = LLVMGetNamedFunction(cg->module, ind[_i].sym);
                    if (!fn_f) { fn_f = LLVMAddFunction(cg->module, ind[_i].sym, ft); LLVMSetLinkage(fn_f, LLVMExternalLinkage); }
                    return LLVMBuildCall2(cg->builder, ft, fn_f, NULL, 0, "");
                case 1: {
                    ret_t = LLVMInt64TypeInContext(cg->ctx);
                    ft = LLVMFunctionType(ret_t, &i64t, 1, 0);
                    fn_f = LLVMGetNamedFunction(cg->module, ind[_i].sym);
                    if (!fn_f) { fn_f = LLVMAddFunction(cg->module, ind[_i].sym, ft); LLVMSetLinkage(fn_f, LLVMExternalLinkage); }
                    LLVMValueRef a0 = n->child_count > 1 ? gen_expr(cg, n->children[1]) : LLVMConstInt(i64t, 0, 0);
                    return LLVMBuildCall2(cg->builder, ft, fn_f, &a0, 1, ind[_i].sym);
                }
                case 2: {
                    LLVMTypeRef pts[2] = { i64t, i64t };
                    ft = LLVMFunctionType(ret_t, pts, 2, 0);
                    fn_f = LLVMGetNamedFunction(cg->module, ind[_i].sym);
                    if (!fn_f) { fn_f = LLVMAddFunction(cg->module, ind[_i].sym, ft); LLVMSetLinkage(fn_f, LLVMExternalLinkage); }
                    LLVMValueRef av[2] = {
                        n->child_count > 1 ? gen_expr(cg, n->children[1]) : LLVMConstInt(i64t, 0, 0),
                        n->child_count > 2 ? gen_expr(cg, n->children[2]) : LLVMConstInt(i64t, 0, 0)
                    };
                    return LLVMBuildCall2(cg->builder, ft, fn_f, av, 2, "");
                }
                case 3:
                    ft = LLVMFunctionType(ret_t, &i8p, 1, 0);
                    fn_f = LLVMGetNamedFunction(cg->module, ind[_i].sym);
                    if (!fn_f) { fn_f = LLVMAddFunction(cg->module, ind[_i].sym, ft); LLVMSetLinkage(fn_f, LLVMExternalLinkage); }
                    { LLVMValueRef a0 = n->child_count > 1 ? gen_expr(cg, n->children[1]) : LLVMConstNull(i8p);
                      return LLVMBuildCall2(cg->builder, ft, fn_f, &a0, 1, ""); }
                case 4: {
                    LLVMTypeRef pts[2] = { i8p, i8p };
                    ft = LLVMFunctionType(ret_t, pts, 2, 0);
                    fn_f = LLVMGetNamedFunction(cg->module, ind[_i].sym);
                    if (!fn_f) { fn_f = LLVMAddFunction(cg->module, ind[_i].sym, ft); LLVMSetLinkage(fn_f, LLVMExternalLinkage); }
                    LLVMValueRef av[2] = {
                        n->child_count > 1 ? gen_expr(cg, n->children[1]) : LLVMConstNull(i8p),
                        n->child_count > 2 ? gen_expr(cg, n->children[2]) : LLVMConstNull(i8p)
                    };
                    return LLVMBuildCall2(cg->builder, ft, fn_f, av, 2, "");
                }
                default: break;
            }
        }
    } /* end 산업/안전/AI 내장 함수 */

    /* ── 일반 함수 호출 ── */
    LLVMValueRef fn_val = gen_expr(cg, fn_node);
    if (!fn_val) return NULL;

    int argc = n->child_count - 1;
    LLVMValueRef *args = argc > 0
        ? malloc((size_t)argc * sizeof(LLVMValueRef)) : NULL;

    for (int i = 0; i < argc; i++) {
        args[i] = gen_expr(cg, n->children[i + 1]);
        if (!args[i]) args[i] = LLVMConstInt(
            LLVMInt64TypeInContext(cg->ctx), 0, 0);
    }

    LLVMTypeRef fn_type = LLVMGetElementType(LLVMTypeOf(fn_val));
    LLVMValueRef ret = LLVMBuildCall2(
        cg->builder, fn_type, fn_val,
        args, (unsigned)argc,
        LLVMGetTypeKind(LLVMGetReturnType(fn_type)) == LLVMVoidTypeKind
            ? "" : "call"
    );
    free(args);
    return ret;
}

/* ── 표현식 디스패처 ────────────────────────────────────────── */
static LLVMValueRef gen_expr(LLVMCodegen *cg, Node *n) {
    if (!n) return NULL;
    switch (n->type) {
        case NODE_INT_LIT:    return gen_int_lit(cg, n);
        case NODE_FLOAT_LIT:  return gen_float_lit(cg, n);
        case NODE_BOOL_LIT:   return gen_bool_lit(cg, n);
        case NODE_CHAR_LIT:   return gen_char_lit(cg, n);
        case NODE_STRING_LIT: return gen_string_lit(cg, n);
        case NODE_NULL_LIT:   return gen_null_lit(cg);
        case NODE_IDENT:      return gen_ident(cg, n);
        case NODE_BINARY:     return gen_binary(cg, n);
        case NODE_UNARY:      return gen_unary(cg, n);
        case NODE_CALL:       return gen_call(cg, n);
        case NODE_ARRAY_LIT:  return gen_array_lit(cg, n);
        case NODE_INDEX:      return gen_index_expr(cg, n);
        case NODE_MEMBER:     return gen_member_expr(cg, n);
        case NODE_LAMBDA:     return gen_lambda(cg, n);       /* v9.2.0 */
        case NODE_DICT_LIT:   return gen_dict_lit(cg, n);    /* v9.2.0 */
        case NODE_ASSIGN:     {
            /* 대입 표현식 — 우변 생성 후 alloca에 저장 */
            LLVMValueRef rval = gen_expr(cg, n->children[1]);
            Node *lhs = n->children[0];
            if (lhs->type == NODE_IDENT) {
                LLVMSymbol *sym = scope_lookup(cg, lhs->sval);
                if (!sym) {
                    CG_ERROR(cg, lhs, "대입: 선언되지 않은 변수: %s", lhs->sval);
                    return rval;
                }
                LLVMBuildStore(cg->builder, rval, sym->alloca);
            } else if (lhs->type == NODE_INDEX) {
                /* arr[idx] = val */
                LLVMValueRef arr = gen_expr(cg, lhs->children[0]);
                LLVMValueRef idx = gen_expr(cg, lhs->children[1]);
                if (arr && idx) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);
                    if (LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                        idx = LLVMBuildSExt(cg->builder, idx, i64, "idx64");
                    if (LLVMGetTypeKind(LLVMTypeOf(rval)) == LLVMDoubleTypeKind)
                        rval = LLVMBuildFPToSI(cg->builder, rval, i64, "f2i");
                    else if (LLVMGetTypeKind(LLVMTypeOf(rval)) == LLVMIntegerTypeKind &&
                             LLVMGetIntTypeWidth(LLVMTypeOf(rval)) < 64)
                        rval = LLVMBuildSExt(cg->builder, rval, i64, "ext");
                    build_array_set(cg, arr, idx, rval);
                }
            }
            return rval;
        }
        default:
            CG_ERROR(cg, n, "표현식으로 처리할 수 없는 노드: %d", n->type);
            return LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0);
    }
}

/* ================================================================
 *  구문 코드 생성
 * ================================================================ */

/* ── 변수 선언 ──────────────────────────────────────────────── */
static void gen_var_decl(LLVMCodegen *cg, Node *n) {
    sourcemap_add(cg, n->line, n->col);   /* ← 소스맵 */

    /* 배열 타입은 KcArray* 로 처리 */
    LLVMTypeRef type;
    int is_array = (n->dtype == TOK_KW_BAELYEOL);
    if (is_array)
        type = LLVMPointerType(get_kc_array_type(cg), 0);
    else
        type = ktype_to_llvm(cg, n->dtype);

    /* 함수 진입 블록에 alloca 삽입 (mem2reg 최적화를 위해) */
    LLVMBasicBlockRef cur    = LLVMGetInsertBlock(cg->builder);
    LLVMBasicBlockRef entry  = LLVMGetEntryBasicBlock(cg->cur_func);
    LLVMValueRef      first  = LLVMGetFirstInstruction(entry);

    LLVMPositionBuilderBefore(cg->builder,
        first ? first : (LLVMValueRef)LLVMGetBasicBlockTerminator(entry));
    LLVMValueRef alloca = LLVMBuildAlloca(cg->builder, type, n->sval);
    LLVMPositionBuilderAtEnd(cg->builder, cur);

    /* 초기값 */
    if (n->child_count > 0) {
        LLVMValueRef init = gen_expr(cg, n->children[0]);
        if (init) LLVMBuildStore(cg->builder, init, alloca);
    } else if (is_array) {
        /* 배열 기본 초기화: 빈 배열 생성 */
        LLVMValueRef empty = build_array_new(cg);
        LLVMBuildStore(cg->builder, empty, alloca);
    } else {
        /* 기본값 0 */
        LLVMBuildStore(cg->builder,
                       LLVMConstNull(type), alloca);
    }

    scope_set(cg, n->sval, alloca, type);
    cg->result->var_count++;
}

/* ── 만약/아니면 ────────────────────────────────────────────── */
static void gen_if(LLVMCodegen *cg, Node *n) {
    sourcemap_add(cg, n->line, n->col);   /* ← 소스맵 */
    /* child[0]=조건, child[1]=then블록, child[2]=elif/else(선택) */
    LLVMValueRef cond = gen_expr(cg, n->children[0]);
    if (!cond) return;

    /* i1로 변환 */
    if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
        LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
        cond = LLVMBuildICmp(cg->builder, LLVMIntNE, cond,
                             LLVMConstNull(LLVMTypeOf(cond)), "tobool");
    }

    LLVMValueRef       fn      = cg->cur_func;
    LLVMBasicBlockRef  then_bb = LLVMAppendBasicBlockInContext(
                                     cg->ctx, fn, "if.then");
    LLVMBasicBlockRef  else_bb = LLVMAppendBasicBlockInContext(
                                     cg->ctx, fn, "if.else");
    LLVMBasicBlockRef  end_bb  = LLVMAppendBasicBlockInContext(
                                     cg->ctx, fn, "if.end");

    LLVMBuildCondBr(cg->builder, cond, then_bb, else_bb);

    /* then 블록 */
    LLVMPositionBuilderAtEnd(cg->builder, then_bb);
    scope_push(cg);
    gen_block(cg, n->children[1]);
    scope_pop(cg);
    if (block_is_open(cg)) LLVMBuildBr(cg->builder, end_bb);

    /* else/elif 블록 */
    LLVMPositionBuilderAtEnd(cg->builder, else_bb);
    if (n->child_count > 2 && n->children[2]) {
        scope_push(cg);
        gen_stmt(cg, n->children[2]);
        scope_pop(cg);
    }
    if (block_is_open(cg)) LLVMBuildBr(cg->builder, end_bb);

    LLVMPositionBuilderAtEnd(cg->builder, end_bb);
}

/* ── 동안 반복 ──────────────────────────────────────────────── */
static void gen_while(LLVMCodegen *cg, Node *n) {
    sourcemap_add(cg, n->line, n->col);   /* ← 소스맵 */
    LLVMValueRef      fn      = cg->cur_func;
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
                                    cg->ctx, fn, "while.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
                                    cg->ctx, fn, "while.body");
    LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(
                                    cg->ctx, fn, "while.end");

    LLVMBuildBr(cg->builder, cond_bb);

    /* 조건 블록 */
    LLVMPositionBuilderAtEnd(cg->builder, cond_bb);
    LLVMValueRef cond = gen_expr(cg, n->children[0]);
    if (!cond) cond = LLVMConstInt(LLVMInt1TypeInContext(cg->ctx), 0, 0);
    if (LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1)
        cond = LLVMBuildICmp(cg->builder, LLVMIntNE, cond,
                             LLVMConstNull(LLVMTypeOf(cond)), "tobool");
    LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);

    /* 몸체 블록 */
    LLVMPositionBuilderAtEnd(cg->builder, body_bb);
    loop_push(cg, end_bb, cond_bb);
    scope_push(cg);
    gen_block(cg, n->children[1]);
    scope_pop(cg);
    loop_pop(cg);
    if (block_is_open(cg)) LLVMBuildBr(cg->builder, cond_bb);

    LLVMPositionBuilderAtEnd(cg->builder, end_bb);
}

/* ── 반복 i 부터 N 까지 M ──────────────────────────────────── */
static void gen_for_range(LLVMCodegen *cg, Node *n) {
    sourcemap_add(cg, n->line, n->col);   /* ← 소스맵 */
    /* sval=변수명, child[0]=시작, child[1]=끝, child[2]=블록 */
    LLVMTypeRef  i64  = LLVMInt64TypeInContext(cg->ctx);
    LLVMValueRef fn   = cg->cur_func;

    /* alloca for 루프 변수 (entry 블록에) */
    LLVMBasicBlockRef cur   = LLVMGetInsertBlock(cg->builder);
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
    LLVMValueRef      fi    = LLVMGetFirstInstruction(entry);
    LLVMPositionBuilderBefore(cg->builder, fi);
    LLVMValueRef loop_var = LLVMBuildAlloca(cg->builder, i64, n->sval);
    LLVMPositionBuilderAtEnd(cg->builder, cur);

    LLVMValueRef start = gen_expr(cg, n->children[0]);
    LLVMValueRef end   = gen_expr(cg, n->children[1]);
    LLVMBuildStore(cg->builder, start, loop_var);

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
                                    cg->ctx, fn, "for.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
                                    cg->ctx, fn, "for.body");
    LLVMBasicBlockRef inc_bb  = LLVMAppendBasicBlockInContext(
                                    cg->ctx, fn, "for.inc");
    LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(
                                    cg->ctx, fn, "for.end");

    LLVMBuildBr(cg->builder, cond_bb);

    /* 조건: i < end */
    LLVMPositionBuilderAtEnd(cg->builder, cond_bb);
    LLVMValueRef i_val = LLVMBuildLoad2(cg->builder, i64, loop_var, "i");
    LLVMValueRef cond  = LLVMBuildICmp(cg->builder, LLVMIntSLT,
                                        i_val, end, "for.cond");
    LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);

    /* 몸체 */
    LLVMPositionBuilderAtEnd(cg->builder, body_bb);
    loop_push(cg, end_bb, inc_bb);
    scope_push(cg);
    scope_set(cg, n->sval, loop_var, i64);
    gen_block(cg, n->children[2]);
    scope_pop(cg);
    loop_pop(cg);
    if (block_is_open(cg)) LLVMBuildBr(cg->builder, inc_bb);

    /* 증가: i++ */
    LLVMPositionBuilderAtEnd(cg->builder, inc_bb);
    LLVMValueRef i_cur = LLVMBuildLoad2(cg->builder, i64, loop_var, "i");
    LLVMValueRef i_inc = LLVMBuildAdd(cg->builder, i_cur,
                                       LLVMConstInt(i64, 1, 0), "i.inc");
    LLVMBuildStore(cg->builder, i_inc, loop_var);
    LLVMBuildBr(cg->builder, cond_bb);

    LLVMPositionBuilderAtEnd(cg->builder, end_bb);
}

/* ── 함수 / 정의 선언 ───────────────────────────────────────── */
static void gen_func_decl(LLVMCodegen *cg, Node *n) {
    sourcemap_add(cg, n->line, n->col);   /* ← 소스맵 */
    int is_void = (n->type == NODE_VOID_DECL);

    /* 매개변수 타입 수집 */
    int param_count = n->child_count - 1; /* 마지막 child = 블록 */
    LLVMTypeRef *param_types = param_count > 0
        ? malloc((size_t)param_count * sizeof(LLVMTypeRef)) : NULL;

    for (int i = 0; i < param_count; i++) {
        Node *p = n->children[i];
        param_types[i] = ktype_to_llvm(cg,
            p->dtype != TOK_EOF ? p->dtype : TOK_KW_JEONGSU);
    }

    LLVMTypeRef ret_type = is_void
        ? LLVMVoidTypeInContext(cg->ctx)
        : LLVMInt64TypeInContext(cg->ctx); /* 기본 반환형: 정수 */

    LLVMTypeRef fn_type = LLVMFunctionType(
        ret_type, param_types, (unsigned)param_count, 0);
    free(param_types);

    LLVMValueRef fn = LLVMAddFunction(cg->module, n->sval, fn_type);
    LLVMSetLinkage(fn, LLVMExternalLinkage);

    cg->result->func_count++;

    /* 함수 몸체 생성 */
    LLVMBasicBlockRef  entry = LLVMAppendBasicBlockInContext(
                                   cg->ctx, fn, "entry");
    LLVMValueRef       prev_func     = cg->cur_func;
    LLVMTypeRef        prev_ret_type = cg->cur_func_ret_type;
    int                prev_void     = cg->cur_func_is_void;

    cg->cur_func          = fn;
    cg->cur_func_ret_type = ret_type;
    cg->cur_func_is_void  = is_void;

    LLVMPositionBuilderAtEnd(cg->builder, entry);

    scope_push(cg);

    /* 매개변수 → alloca */
    for (int i = 0; i < param_count; i++) {
        Node       *p     = n->children[i];
        LLVMTypeRef ptype = LLVMTypeOf(LLVMGetParam(fn, (unsigned)i));
        LLVMValueRef alloca = LLVMBuildAlloca(cg->builder, ptype, p->sval);
        LLVMBuildStore(cg->builder, LLVMGetParam(fn, (unsigned)i), alloca);
        scope_set(cg, p->sval, alloca, ptype);
    }

    /* 함수 몸체 블록 */
    gen_block(cg, n->children[n->child_count - 1]);

    /* 암묵적 반환 */
    if (block_is_open(cg)) {
        if (is_void)
            LLVMBuildRetVoid(cg->builder);
        else
            LLVMBuildRet(cg->builder,
                         LLVMConstInt(ret_type, 0, 0));
    }

    scope_pop(cg);

    cg->cur_func          = prev_func;
    cg->cur_func_ret_type = prev_ret_type;
    cg->cur_func_is_void  = prev_void;
}

/* ── 객체 클래스 선언 IR 변환 (v6.2.0) ──────────────────────────── */
/*
 * gen_class_decl() : NODE_CLASS_DECL → LLVM IR
 *
 * 생성 순서:
 *   ① vtable 구조체 타입 (함수 포인터 배열)
 *   ② 전역 vtable 인스턴스
 *   ③ 객체 구조체 타입  (vtable ptr + 필드)
 *   ④ 각 메서드 함수  (첫 인수 = self*)
 *   ⑤ vtable_init 함수 (포인터 채움)
 *   ⑥ _new 팩토리 함수 (malloc + vtable 주입 + 생성자 호출)
 */
static void gen_class_decl(LLVMCodegen *cg, Node *n) {
    sourcemap_add(cg, n->line, n->col);

    const char *cname = n->sval ? n->sval : "_class";

    /* 부모 클래스 추출 */
    const char *parent = NULL;
    if (n->child_count >= 2 &&
        n->children[0] && n->children[0]->type == NODE_IDENT)
        parent = n->children[0]->sval;

    Node *body = n->children[n->child_count - 1];

    /* 클래스 레지스트리 슬롯 확보 */
    if (cg->class_count >= LLVM_CLASS_MAX) {
        CG_ERROR(cg, n, "클래스 최대 개수(%d) 초과", LLVM_CLASS_MAX);
        return;
    }
    int ci = cg->class_count++;
    snprintf(cg->class_reg[ci].name, 128, "%s", cname);
    cg->class_reg[ci].is_valid = 1;
    cg->class_reg[ci].method_count = 0;

    /* ── ① 메서드 타입 수집 ────────────────────────────────── */
    /* 메서드 수 세기 */
    int method_count = 0;
    if (body && body->type == NODE_BLOCK) {
        for (int i = 0; i < body->child_count; i++) {
            Node *s = body->children[i];
            if (s && (s->type == NODE_FUNC_DECL || s->type == NODE_VOID_DECL))
                method_count++;
        }
    }

    /* ── ② 객체 구조체 타입 (opaque 생성 후 설정) ──────────── */
    char struct_name[160];
    snprintf(struct_name, sizeof(struct_name), "kc_%s", cname);
    LLVMTypeRef struct_ty = LLVMStructCreateNamed(cg->ctx, struct_name);

    /* ── ③ vtable 구조체 타입 ──────────────────────────────── */
    char vt_name[160];
    snprintf(vt_name, sizeof(vt_name), "kc_%s_vtable_t", cname);
    LLVMTypeRef vtable_ty = LLVMStructCreateNamed(cg->ctx, vt_name);

    /* vtable 필드: 각 메서드에 대한 함수 포인터 (i8* 로 저장, 실제 캐스팅) */
    int vt_field_count = method_count;
    LLVMTypeRef *vt_fields = vt_field_count > 0
        ? malloc((size_t)vt_field_count * sizeof(LLVMTypeRef)) : NULL;

    {
        int mi = 0;
        if (body && body->type == NODE_BLOCK) {
            for (int i = 0; i < body->child_count; i++) {
                Node *s = body->children[i];
                if (!s) continue;
                if (s->type != NODE_FUNC_DECL && s->type != NODE_VOID_DECL) continue;
                /* 함수 포인터 타입: ptr (i8*로 간략 처리) */
                vt_fields[mi++] = LLVMPointerType(
                    LLVMInt8TypeInContext(cg->ctx), 0);
            }
        }
    }
    LLVMStructSetBody(vtable_ty, vt_fields, (unsigned)vt_field_count, 0);
    free(vt_fields);

    /* ── ④ 전역 vtable 인스턴스 ────────────────────────────── */
    char vt_global_name[160];
    snprintf(vt_global_name, sizeof(vt_global_name), "kc_%s_vtable", cname);
    LLVMValueRef vtable_global = LLVMAddGlobal(cg->module, vtable_ty, vt_global_name);
    LLVMSetInitializer(vtable_global, LLVMConstNull(vtable_ty));
    LLVMSetLinkage(vtable_global, LLVMInternalLinkage);

    /* ── ⑤ 객체 구조체 필드 설정 ───────────────────────────── */
    /*    { vtable*, [부모 struct (선택)], 필드... } */
    int field_count = 0;
    /* 최대 필드 수 추정 (vtable ptr 1 + 부모 1 + 변수들) */
    int max_fields = 2 + (body ? body->child_count : 0);
    LLVMTypeRef *fields = malloc((size_t)max_fields * sizeof(LLVMTypeRef));

    /* vtable 포인터 */
    fields[field_count++] = LLVMPointerType(vtable_ty, 0);

    /* 부모 구조체 임베드 (레지스트리에서 탐색) */
    if (parent) {
        LLVMTypeRef parent_ty = LLVMGetTypeByName2(cg->ctx,
            /* "kc_부모" */ (snprintf(struct_name, sizeof(struct_name), "kc_%s", parent),
                             struct_name));
        if (parent_ty)
            fields[field_count++] = parent_ty;
    }

    /* VAR_DECL / CONST_DECL 필드 */
    if (body && body->type == NODE_BLOCK) {
        for (int i = 0; i < body->child_count; i++) {
            Node *s = body->children[i];
            if (!s) continue;
            if (s->type != NODE_VAR_DECL && s->type != NODE_CONST_DECL) continue;
            fields[field_count++] = ktype_to_llvm(cg,
                s->dtype != TOK_EOF ? s->dtype : TOK_KW_JEONGSU);
        }
    }

    LLVMStructSetBody(struct_ty, fields, (unsigned)field_count, 0);
    free(fields);

    /* 레지스트리에 타입 저장 */
    cg->class_reg[ci].struct_ty     = struct_ty;
    cg->class_reg[ci].vtable_ty     = vtable_ty;
    cg->class_reg[ci].vtable_global = vtable_global;

    /* ── ⑥ 메서드 함수 IR 생성 ─────────────────────────────── */
    LLVMTypeRef struct_ptr_ty = LLVMPointerType(struct_ty, 0);
    int method_idx = 0;
    if (body && body->type == NODE_BLOCK) {
        for (int i = 0; i < body->child_count; i++) {
            Node *s = body->children[i];
            if (!s) continue;
            if (s->type != NODE_FUNC_DECL && s->type != NODE_VOID_DECL) continue;

            int is_void = (s->type == NODE_VOID_DECL);
            const char *mname = s->sval ? s->sval : "_method";

            /* 파라미터: (self*, 매개변수...) */
            int user_param_count = s->child_count - 1;
            /* '자신' 파라미터 건너뜀 */
            int start_p = 0;
            if (user_param_count > 0 && s->children[0] &&
                s->children[0]->type == NODE_PARAM &&
                s->children[0]->sval &&
                strcmp(s->children[0]->sval, "\xEC\x9E\x90\xEC\x8B\xA0") == 0)
                start_p = 1;

            int extra_params = user_param_count - start_p;
            int total_params = 1 + extra_params; /* self + 나머지 */
            LLVMTypeRef *ptypes = malloc((size_t)total_params * sizeof(LLVMTypeRef));
            ptypes[0] = struct_ptr_ty;  /* self* */
            for (int j = start_p; j < user_param_count; j++) {
                Node *p = s->children[j];
                ptypes[1 + (j - start_p)] = ktype_to_llvm(cg,
                    p->dtype != TOK_EOF ? p->dtype : TOK_KW_JEONGSU);
            }

            LLVMTypeRef ret_ty = is_void
                ? LLVMVoidTypeInContext(cg->ctx)
                : LLVMInt64TypeInContext(cg->ctx);

            LLVMTypeRef fn_ty = LLVMFunctionType(ret_ty, ptypes,
                                                  (unsigned)total_params, 0);
            free(ptypes);

            /* 함수 이름: kc_클래스명_메서드명 */
            char fn_name[256];
            snprintf(fn_name, sizeof(fn_name), "kc_%s_%s", cname, mname);
            LLVMValueRef fn = LLVMAddFunction(cg->module, fn_name, fn_ty);
            LLVMSetLinkage(fn, LLVMInternalLinkage);

            cg->result->func_count++;

            /* 함수 몸체 */
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
                                          cg->ctx, fn, "entry");
            LLVMValueRef prev_fn  = cg->cur_func;
            LLVMTypeRef  prev_ret = cg->cur_func_ret_type;
            int          prev_v   = cg->cur_func_is_void;

            cg->cur_func          = fn;
            cg->cur_func_ret_type = ret_ty;
            cg->cur_func_is_void  = is_void;

            LLVMPositionBuilderAtEnd(cg->builder, entry);
            scope_push(cg);

            /* self 파라미터 등록 */
            LLVMValueRef self_alloca = LLVMBuildAlloca(cg->builder,
                struct_ptr_ty, "kc_\xEC\x9E\x90\xEC\x8B\xA0");
            LLVMBuildStore(cg->builder, LLVMGetParam(fn, 0), self_alloca);
            scope_set(cg, "\xEC\x9E\x90\xEC\x8B\xA0", self_alloca, struct_ptr_ty);

            /* 나머지 매개변수 등록 */
            for (int j = start_p; j < user_param_count; j++) {
                Node *p = s->children[j];
                LLVMTypeRef ptype = LLVMTypeOf(
                    LLVMGetParam(fn, (unsigned)(1 + j - start_p)));
                LLVMValueRef pa = LLVMBuildAlloca(cg->builder, ptype, p->sval);
                LLVMBuildStore(cg->builder,
                    LLVMGetParam(fn, (unsigned)(1 + j - start_p)), pa);
                scope_set(cg, p->sval, pa, ptype);
            }

            /* 몸체 블록 */
            gen_block(cg, s->children[s->child_count - 1]);

            if (block_is_open(cg)) {
                if (is_void) LLVMBuildRetVoid(cg->builder);
                else LLVMBuildRet(cg->builder,
                         LLVMConstInt(ret_ty, 0, 0));
            }

            scope_pop(cg);
            cg->cur_func          = prev_fn;
            cg->cur_func_ret_type = prev_ret;
            cg->cur_func_is_void  = prev_v;

            /* 메서드를 레지스트리에 등록 */
            if (method_idx < LLVM_CLASS_METHOD_MAX) {
                snprintf(cg->class_reg[ci].methods[method_idx].name, 128, "%s", mname);
                cg->class_reg[ci].methods[method_idx].fn = fn;
                cg->class_reg[ci].method_count++;
            }
            method_idx++;
        }
    }

    /* ── ⑦ vtable_init 함수 IR 생성 ────────────────────────── */
    {
        char init_name[160];
        snprintf(init_name, sizeof(init_name), "kc_%s_vtable_init", cname);
        LLVMTypeRef  init_ty = LLVMFunctionType(
            LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
        LLVMValueRef init_fn = LLVMAddFunction(cg->module, init_name, init_ty);
        LLVMSetLinkage(init_fn, LLVMInternalLinkage);

        LLVMBasicBlockRef init_entry = LLVMAppendBasicBlockInContext(
                                           cg->ctx, init_fn, "entry");
        LLVMValueRef prev_fn  = cg->cur_func;
        LLVMTypeRef  prev_ret = cg->cur_func_ret_type;
        int          prev_v   = cg->cur_func_is_void;

        cg->cur_func          = init_fn;
        cg->cur_func_ret_type = LLVMVoidTypeInContext(cg->ctx);
        cg->cur_func_is_void  = 1;

        LLVMPositionBuilderAtEnd(cg->builder, init_entry);

        /* 각 메서드 포인터를 vtable 전역 변수의 해당 인덱스에 저장 */
        for (int mi = 0; mi < cg->class_reg[ci].method_count; mi++) {
            LLVMValueRef fn_val = cg->class_reg[ci].methods[mi].fn;
            /* GEP: vtable_global의 mi번째 필드 */
            LLVMValueRef indices[2] = {
                LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0),
                LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), (unsigned)mi, 0)
            };
            LLVMValueRef field_ptr = LLVMBuildGEP2(cg->builder, vtable_ty,
                vtable_global, indices, 2, "vt_field");
            /* 함수 포인터 → i8* 비트캐스트 후 저장 */
            LLVMValueRef fn_as_ptr = LLVMBuildBitCast(cg->builder, fn_val,
                LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0), "fn_ptr");
            LLVMBuildStore(cg->builder, fn_as_ptr, field_ptr);
        }

        LLVMBuildRetVoid(cg->builder);

        cg->cur_func          = prev_fn;
        cg->cur_func_ret_type = prev_ret;
        cg->cur_func_is_void  = prev_v;

        cg->class_reg[ci].init_fn = init_fn;
    }

    /* ── ⑧ _new 팩토리 함수 IR 생성 ────────────────────────── */
    {
        /* malloc 함수 참조 확보 */
        if (!cg->fn_malloc) {
            LLVMTypeRef malloc_ty = LLVMFunctionType(
                LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0),
                (LLVMTypeRef[]){ LLVMInt64TypeInContext(cg->ctx) }, 1, 0);
            cg->fn_malloc = LLVMAddFunction(cg->module, "malloc", malloc_ty);
        }

        /* 팩토리 파라미터: 생성(생성자) 메서드의 파라미터를 그대로 사용 */
        /* (간단히 파라미터 없는 버전으로 생성, 생성자 호출은 별도) */
        char new_name[160];
        snprintf(new_name, sizeof(new_name), "kc_%s_new", cname);
        LLVMTypeRef new_ret_ty = LLVMPointerType(struct_ty, 0);
        LLVMTypeRef new_fn_ty  = LLVMFunctionType(new_ret_ty, NULL, 0, 0);
        LLVMValueRef new_fn    = LLVMAddFunction(cg->module, new_name, new_fn_ty);
        LLVMSetLinkage(new_fn, LLVMInternalLinkage);

        LLVMBasicBlockRef new_entry = LLVMAppendBasicBlockInContext(
                                          cg->ctx, new_fn, "entry");
        LLVMValueRef prev_fn  = cg->cur_func;
        LLVMTypeRef  prev_ret = cg->cur_func_ret_type;
        int          prev_v   = cg->cur_func_is_void;

        cg->cur_func          = new_fn;
        cg->cur_func_ret_type = new_ret_ty;
        cg->cur_func_is_void  = 0;

        LLVMPositionBuilderAtEnd(cg->builder, new_entry);

        /* vtable_init() 호출 */
        LLVMBuildCall2(cg->builder,
            LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0),
            cg->class_reg[ci].init_fn, NULL, 0, "");

        /* malloc(sizeof(struct)) */
        LLVMValueRef size_val = LLVMSizeOf(struct_ty);
        LLVMTypeRef  malloc_ty = LLVMFunctionType(
            LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0),
            (LLVMTypeRef[]){ LLVMInt64TypeInContext(cg->ctx) }, 1, 0);
        LLVMValueRef raw = LLVMBuildCall2(cg->builder, malloc_ty,
                               cg->fn_malloc, &size_val, 1, "raw");
        /* i8* → 구조체* 비트캐스트 */
        LLVMValueRef obj = LLVMBuildBitCast(cg->builder, raw,
                               new_ret_ty, "obj");

        /* vtable 포인터 주입: obj->kc__vt = &kc_클래스명_vtable */
        LLVMValueRef vt_indices[2] = {
            LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0),
            LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0)  /* 첫 필드 = vtable* */
        };
        LLVMValueRef vt_field = LLVMBuildGEP2(cg->builder, struct_ty,
            obj, vt_indices, 2, "vt_ptr");
        LLVMBuildStore(cg->builder, vtable_global, vt_field);

        /* 반환 */
        LLVMBuildRet(cg->builder, obj);

        cg->cur_func          = prev_fn;
        cg->cur_func_ret_type = prev_ret;
        cg->cur_func_is_void  = prev_v;

        cg->class_reg[ci].new_fn = new_fn;
    }
}

/* ── 구문 디스패처 ──────────────────────────────────────────── */
static void gen_stmt(LLVMCodegen *cg, Node *n) {
    if (!n) return;
    if (cg->had_error) return;

    switch (n->type) {

        case NODE_VAR_DECL:
        case NODE_CONST_DECL:
            gen_var_decl(cg, n);
            break;

        case NODE_EXPR_STMT:
            if (n->child_count > 0)
                gen_expr(cg, n->children[0]);
            break;

        case NODE_IF:
        case NODE_ELIF:
            gen_if(cg, n);
            break;

        case NODE_ELSE:
            scope_push(cg);
            gen_block(cg, n->children[0]);
            scope_pop(cg);
            break;

        case NODE_WHILE:
            gen_while(cg, n);
            break;

        case NODE_FOR_RANGE:
            gen_for_range(cg, n);
            break;

        case NODE_FOR_EACH:
            gen_for_each(cg, n);
            break;

        case NODE_RETURN:
            if (cg->cur_func_is_void) {
                LLVMBuildRetVoid(cg->builder);
            } else if (n->child_count > 0) {
                LLVMValueRef rv = gen_expr(cg, n->children[0]);
                LLVMBuildRet(cg->builder, rv);
            } else {
                LLVMBuildRet(cg->builder,
                    LLVMConstInt(cg->cur_func_ret_type, 0, 0));
            }
            break;

        case NODE_BREAK: {
            LLVMLoopCtx *lc = loop_top(cg);
            if (lc) LLVMBuildBr(cg->builder, lc->break_bb);
            else    CG_ERROR(cg, n, "멈춤: 반복문 밖에서 사용");
            break;
        }

        case NODE_CONTINUE: {
            LLVMLoopCtx *lc = loop_top(cg);
            if (lc) LLVMBuildBr(cg->builder, lc->continue_bb);
            else    CG_ERROR(cg, n, "건너뜀: 반복문 밖에서 사용");
            break;
        }

        /* ── 이동/레이블 (goto/label) (v9.2.0) ──────────────────────
         *  전략:
         *    NODE_LABEL : label_map_get_or_create()로 BasicBlock 확보
         *                 → 현재 열린 블록에서 해당 bb로 무조건 분기
         *                 → 빌더를 레이블 bb로 이동
         *    NODE_GOTO  : label_map_get_or_create()로 대상 bb 확보
         *                 → LLVMBuildBr로 무조건 분기 (forward 지원)
         *    ※ goto-label은 현재 함수 범위 내에서만 유효
         * ============================================================ */
        case NODE_LABEL: {
            if (!cg->cur_func) break;
            const char *lname = n->sval ? n->sval : "_lbl";
            sourcemap_add(cg, n->line, n->col);

            LLVMBasicBlockRef lbb = label_map_get_or_create(cg, lname);
            if (!lbb) {
                CG_ERROR(cg, n, "레이블: 블록 생성 실패 (%s)", lname);
                break;
            }
            /* 현재 열린 블록에서 레이블 블록으로 무조건 분기 */
            if (block_is_open(cg))
                LLVMBuildBr(cg->builder, lbb);
            /* 빌더를 레이블 블록으로 이동 */
            LLVMPositionBuilderAtEnd(cg->builder, lbb);
            break;
        }

        case NODE_GOTO: {
            if (!cg->cur_func || !block_is_open(cg)) break;
            const char *lname = n->sval ? n->sval : "_lbl";
            sourcemap_add(cg, n->line, n->col);

            LLVMBasicBlockRef lbb = label_map_get_or_create(cg, lname);
            if (!lbb) {
                CG_ERROR(cg, n, "이동: 레이블 블록 생성 실패 (%s)", lname);
                break;
            }
            LLVMBuildBr(cg->builder, lbb);
            /* goto 후 unreachable 코드 방지 — 빌더를 새 임시 블록으로 */
            LLVMBasicBlockRef dead_bb = LLVMAppendBasicBlockInContext(
                cg->ctx, cg->cur_func, "goto.dead");
            LLVMPositionBuilderAtEnd(cg->builder, dead_bb);
            break;
        }

        case NODE_FUNC_DECL:
        case NODE_VOID_DECL:
            gen_func_decl(cg, n);
            break;

        case NODE_CLASS_DECL:  /* v6.2.0 — 객체 vtable IR 변환 */
            gen_class_decl(cg, n);
            break;

        /* ── 선택문 switch (v9.1.0) ─────────────────────────────────
         *  선택 값:
         *      경우 리터럴: 블록  (NODE_CASE)
         *      그외: 블록        (NODE_DEFAULT)
         *  선택끝
         *
         *  전략: LLVMBuildSwitch — 각 case 마다 basicblock 생성
         *  ============================================================ */
        case NODE_SWITCH: {
            if (!block_is_open(cg) || n->child_count < 1) break;
            sourcemap_add(cg, n->line, n->col);

            LLVMValueRef fn     = cg->cur_func;
            LLVMTypeRef  i64    = LLVMInt64TypeInContext(cg->ctx);

            /* 선택 값 표현식 */
            LLVMValueRef sw_val = gen_expr(cg, n->children[0]);
            if (!sw_val) sw_val = LLVMConstInt(i64, 0, 0);
            /* i64 통일 */
            if (LLVMGetTypeKind(LLVMTypeOf(sw_val)) == LLVMDoubleTypeKind)
                sw_val = LLVMBuildFPToSI(cg->builder, sw_val, i64, "sw_i");
            else if (LLVMGetTypeKind(LLVMTypeOf(sw_val)) == LLVMIntegerTypeKind &&
                     LLVMGetIntTypeWidth(LLVMTypeOf(sw_val)) < 64)
                sw_val = LLVMBuildSExt(cg->builder, sw_val, i64, "sw64");

            /* 각 case 및 default 블록 준비 */
            int case_count = n->child_count - 1; /* child[0] = 조건 값 */
            LLVMBasicBlockRef end_bb     = LLVMAppendBasicBlockInContext(cg->ctx, fn, "sw.end");
            LLVMBasicBlockRef default_bb = end_bb; /* default 없으면 end로 */

            /* case 블록 목록 */
            int num_cases = 0;
            for (int i = 1; i <= case_count; i++) {
                if (n->children[i] && n->children[i]->type == NODE_CASE) num_cases++;
            }

            /* default 블록 먼저 찾기 */
            for (int i = 1; i <= case_count; i++) {
                Node *c = n->children[i];
                if (c && c->type == NODE_DEFAULT) {
                    default_bb = LLVMAppendBasicBlockInContext(cg->ctx, fn, "sw.default");
                    break;
                }
            }

            /* SwitchInst 생성 */
            LLVMValueRef sw_inst = LLVMBuildSwitch(
                cg->builder, sw_val, default_bb, (unsigned)num_cases);

            /* 각 case 블록 생성 및 분기 추가 */
            LLVMBasicBlockRef *case_bbs = num_cases > 0
                ? malloc((size_t)num_cases * sizeof(LLVMBasicBlockRef)) : NULL;
            int ci = 0;
            for (int i = 1; i <= case_count; i++) {
                Node *c = n->children[i];
                if (!c || c->type != NODE_CASE) continue;
                char bb_name[32];
                snprintf(bb_name, sizeof(bb_name), "sw.case%d", ci);
                LLVMBasicBlockRef cbb = LLVMAppendBasicBlockInContext(cg->ctx, fn, bb_name);
                case_bbs[ci++] = cbb;

                /* case 값 → i64 상수 */
                LLVMValueRef cv = gen_expr(cg, c->children[0]);
                if (!cv) cv = LLVMConstInt(i64, 0, 0);
                if (LLVMGetTypeKind(LLVMTypeOf(cv)) != LLVMIntegerTypeKind ||
                    LLVMGetIntTypeWidth(LLVMTypeOf(cv)) != 64)
                    cv = LLVMConstIntCast(cv, i64, 1);
                LLVMAddCase(sw_inst, cv, cbb);
            }

            /* 각 case 블록 몸체 생성 */
            ci = 0;
            for (int i = 1; i <= case_count; i++) {
                Node *c = n->children[i];
                if (!c) continue;

                if (c->type == NODE_CASE) {
                    LLVMPositionBuilderAtEnd(cg->builder, case_bbs[ci++]);
                    scope_push(cg);
                    gen_block(cg, c->children[1]);
                    scope_pop(cg);
                    if (block_is_open(cg)) LLVMBuildBr(cg->builder, end_bb);

                } else if (c->type == NODE_DEFAULT) {
                    LLVMPositionBuilderAtEnd(cg->builder, default_bb);
                    scope_push(cg);
                    gen_block(cg, c->children[0]);
                    scope_pop(cg);
                    if (block_is_open(cg)) LLVMBuildBr(cg->builder, end_bb);
                }
            }
            free(case_bbs);

            LLVMPositionBuilderAtEnd(cg->builder, end_bb);
            break;
        }

        /* ── 예외 처리: 시도/실패시/항상 (v9.1.0) ──────────────────────
         *  전략: setjmp/longjmp 기반
         *    - setjmp 선언: i32 @setjmp(i8*)  (vararg 없음)
         *    - longjmp 선언: void @longjmp(i8*, i32)
         *    - jmp_buf: i8[192] alloca (POSIX 최대 크기)
         *
         *  IR 구조:
         *    %jbuf   = alloca [192 x i8]
         *    %jbuf_p = GEP %jbuf, 0, 0   ; i8*
         *    %r      = call i32 @setjmp(%jbuf_p)
         *    %cond   = icmp eq i32 %r, 0
         *    br %cond, try_bb, catch_bb
         *  try_bb:
         *    <시도 블록>
         *    br finally_bb
         *  catch_bb:
         *    <실패시 블록>
         *    br finally_bb
         *  finally_bb:
         *    <항상 블록 (선택)>
         *    br end_bb
         *  end_bb:
         *  ============================================================ */
        case NODE_TRY: {
            if (!block_is_open(cg)) break;
            sourcemap_add(cg, n->line, n->col);

            LLVMValueRef fn  = cg->cur_func;
            LLVMTypeRef  i8  = LLVMInt8TypeInContext(cg->ctx);
            LLVMTypeRef  i8p = LLVMPointerType(i8, 0);
            LLVMTypeRef  i32 = LLVMInt32TypeInContext(cg->ctx);

            /* setjmp / longjmp 선언 (캐시 없으면 추가) */
            LLVMValueRef fn_setjmp = LLVMGetNamedFunction(cg->module, "setjmp");
            if (!fn_setjmp) {
                LLVMTypeRef ft = LLVMFunctionType(i32, &i8p, 1, 0);
                fn_setjmp = LLVMAddFunction(cg->module, "setjmp", ft);
                LLVMSetLinkage(fn_setjmp, LLVMExternalLinkage);
            }
            LLVMValueRef fn_longjmp = LLVMGetNamedFunction(cg->module, "longjmp");
            if (!fn_longjmp) {
                LLVMTypeRef pts[2] = { i8p, i32 };
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), pts, 2, 0);
                fn_longjmp = LLVMAddFunction(cg->module, "longjmp", ft);
                LLVMSetLinkage(fn_longjmp, LLVMExternalLinkage);
            }
            (void)fn_longjmp; /* raise 에서 사용 */

            /* jmp_buf alloca: [192 x i8] */
            LLVMTypeRef jbuf_ty = LLVMArrayType(i8, 192);
            /* entry 블록에 alloca 삽입 */
            LLVMBasicBlockRef cur_bb   = LLVMGetInsertBlock(cg->builder);
            LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(fn);
            LLVMValueRef first_inst    = LLVMGetFirstInstruction(entry_bb);
            LLVMPositionBuilderBefore(cg->builder, first_inst);
            LLVMValueRef jbuf = LLVMBuildAlloca(cg->builder, jbuf_ty, "jbuf");
            LLVMPositionBuilderAtEnd(cg->builder, cur_bb);

            /* jbuf → i8* (GEP) */
            LLVMValueRef indices[2] = {
                LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0),
                LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0)
            };
            LLVMValueRef jbuf_p = LLVMBuildGEP2(
                cg->builder, jbuf_ty, jbuf, indices, 2, "jbuf_p");

            /* setjmp 호출 */
            LLVMTypeRef  sj_ft  = LLVMFunctionType(i32, &i8p, 1, 0);
            LLVMValueRef sj_ret = LLVMBuildCall2(
                cg->builder, sj_ft, fn_setjmp, &jbuf_p, 1, "sj_ret");

            /* 조건 분기: r==0 → try_bb, else → catch_bb */
            LLVMValueRef cond = LLVMBuildICmp(
                cg->builder, LLVMIntEQ, sj_ret,
                LLVMConstInt(i32, 0, 0), "try_cond");

            LLVMBasicBlockRef try_bb     = LLVMAppendBasicBlockInContext(cg->ctx, fn, "try.body");
            LLVMBasicBlockRef catch_bb   = LLVMAppendBasicBlockInContext(cg->ctx, fn, "try.catch");
            LLVMBasicBlockRef finally_bb = LLVMAppendBasicBlockInContext(cg->ctx, fn, "try.finally");
            LLVMBasicBlockRef end_bb     = LLVMAppendBasicBlockInContext(cg->ctx, fn, "try.end");

            LLVMBuildCondBr(cg->builder, cond, try_bb, catch_bb);

            /* 시도 블록 */
            LLVMPositionBuilderAtEnd(cg->builder, try_bb);
            scope_push(cg);
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            scope_pop(cg);
            if (block_is_open(cg)) LLVMBuildBr(cg->builder, finally_bb);

            /* 실패시 블록 */
            LLVMPositionBuilderAtEnd(cg->builder, catch_bb);
            scope_push(cg);
            if (n->child_count > 1) gen_block(cg, n->children[1]);
            scope_pop(cg);
            if (block_is_open(cg)) LLVMBuildBr(cg->builder, finally_bb);

            /* 항상 블록 */
            LLVMPositionBuilderAtEnd(cg->builder, finally_bb);
            if (n->child_count > 2 && n->children[2]) {
                scope_push(cg);
                gen_block(cg, n->children[2]);
                scope_pop(cg);
            }
            if (block_is_open(cg)) LLVMBuildBr(cg->builder, end_bb);

            LLVMPositionBuilderAtEnd(cg->builder, end_bb);
            break;
        }

        /* ── 오류 raise (v9.1.0) ────────────────────────────────────
         *  전략: kc_raise() 런타임 함수 호출 (i8* 인수)
         *  kc_raise는 kinterp.c 와 런타임에 정의됨;
         *  LLVM IR에서는 외부 선언만 추가하고 call 생성.
         *  ============================================================ */
        case NODE_RAISE: {
            if (!block_is_open(cg)) break;
            sourcemap_add(cg, n->line, n->col);

            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);

            /* kc_raise(const char*) → void 외부 선언 */
            LLVMValueRef fn_raise = LLVMGetNamedFunction(cg->module, "kc_raise");
            if (!fn_raise) {
                LLVMTypeRef ft = LLVMFunctionType(
                    LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
                fn_raise = LLVMAddFunction(cg->module, "kc_raise", ft);
                LLVMSetLinkage(fn_raise, LLVMExternalLinkage);
            }

            /* 오류 메시지 문자열 */
            LLVMValueRef msg;
            if (n->child_count > 0) {
                msg = gen_expr(cg, n->children[0]);
                /* i8* 아니면 기본 문자열로 대체 */
                if (!msg || LLVMGetTypeKind(LLVMTypeOf(msg)) != LLVMPointerTypeKind)
                    msg = make_global_str(cg, "\xEC\x98\xA4\xEB\xA5\x98", "kc_raise_default");
            } else {
                msg = make_global_str(cg, "\xEC\x98\xA4\xEB\xA5\x98", "kc_raise_default");
            }

            LLVMTypeRef ft = LLVMFunctionType(
                LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
            LLVMBuildCall2(cg->builder, ft, fn_raise, &msg, 1, "");
            /* raise 후 unreachable 표시 */
            LLVMBuildUnreachable(cg->builder);
            break;
        }

        case NODE_BLOCK:
            scope_push(cg);
            gen_block(cg, n);
            scope_pop(cg);
            break;

        case NODE_ASSIGN:
            gen_expr(cg, n);
            break;

        case NODE_CALL:
            gen_expr(cg, n);
            break;

        /* ── 스크립트 블록 LLVM IR (v8.1.0) ─────────────────────────
         *  전략: 스크립트 원문을 임시 파일에 기록 후 system() 으로 실행.
         *  LLVM IR에서는 C 표준 라이브러리 함수(system/setenv/fopen/fputs
         *  /fclose/fread/remove/snprintf/getpid)를 직접 call 한다.
         *
         *  생성 순서:
         *    1. getpid() 로 고유 경로 생성 (snprintf → alloca i8[256])
         *    2. setenv() 로 전달 변수 환경변수 설정
         *    3. fopen() + fputs() + fclose() 로 스크립트 임시 파일 작성
         *    4. 실행 커맨드 문자열 구성 후 system() 호출
         *    5. 반환 변수 있으면: fopen → fread → 마지막 줄 추출 → store
         *    6. remove() 로 임시 파일 삭제
         * ============================================================ */
        case NODE_SCRIPT_PYTHON:
        case NODE_SCRIPT_JAVA:
        case NODE_SCRIPT_JS: {
            if (!n->sval || !block_is_open(cg)) break;

            sourcemap_add(cg, n->line, n->col);

            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);

            int is_py   = (n->type == NODE_SCRIPT_PYTHON);
            int is_java = (n->type == NODE_SCRIPT_JAVA);
            int ret_child = (int)n->val.ival;
            int arg_end   = (ret_child >= 0) ? ret_child : n->child_count;

            /* 실행 명령어 결정 */
            const char *run_cmd;
            const char *ext;
            if (is_py) {
                run_cmd = "python3"; ext = ".py";
            } else if (is_java) {
                run_cmd = "javac";   ext = ".java";
            } else {
                run_cmd = "node";    ext = ".js";
            }
            (void)run_cmd; /* 아래 snprintf 포맷 문자열로 사용 */

            /* 스크립트 경로 버퍼: alloca [256 x i8] */
            LLVMValueRef path_buf = LLVMBuildAlloca(cg->builder,
                LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 256), "sc_path");
            LLVMValueRef path_ptr = LLVMBuildBitCast(cg->builder, path_buf, i8p, "sc_path_p");

            /* 출력 경로 버퍼 */
            LLVMValueRef out_buf = LLVMBuildAlloca(cg->builder,
                LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 260), "sc_out");
            LLVMValueRef out_ptr = LLVMBuildBitCast(cg->builder, out_buf, i8p, "sc_out_p");

            /* getpid() */
            LLVMValueRef pid = LLVMBuildCall2(cg->builder,
                LLVMFunctionType(i32, NULL, 0, 0),
                get_getpid_fn(cg), NULL, 0, "pid");
            LLVMValueRef pid64 = LLVMBuildZExt(cg->builder, pid, i64, "pid64");

            /* snprintf(path_ptr, 256, "/tmp/_kcode_ir_%ld.ext", pid64) */
            {
                char fmt[64];
                snprintf(fmt, sizeof(fmt), "/tmp/_kcode_ir_%%ld%s", ext);
                LLVMValueRef fmt_str = LLVMBuildGlobalStringPtr(cg->builder, fmt, "sc_fmt");
                LLVMValueRef args256 = LLVMConstInt(i64, 256, 0);
                LLVMValueRef snp_args[] = { path_ptr, args256, fmt_str, pid64 };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                    get_snprintf_fn(cg), snp_args, 4, "");
            }

            /* snprintf(out_ptr, 260, "%s.out", path_ptr) */
            {
                LLVMValueRef fmt_out = LLVMBuildGlobalStringPtr(cg->builder, "%s.out", "sc_ofmt");
                LLVMValueRef args260 = LLVMConstInt(i64, 260, 0);
                LLVMValueRef snp_args[] = { out_ptr, args260, fmt_out, path_ptr };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                    get_snprintf_fn(cg), snp_args, 4, "");
            }

            /* 2. 전달 변수 → setenv("KCODE_이름", val_str, 1) */
            for (int i = 0; i < arg_end; i++) {
                Node *ch = n->children[i];
                if (!ch || ch->type != NODE_IDENT || !ch->sval) continue;

                char env_key[160];
                snprintf(env_key, sizeof(env_key), "KCODE_%s", ch->sval);
                LLVMValueRef key_str  = LLVMBuildGlobalStringPtr(cg->builder, env_key, "ev_key");
                /* 변수 값을 문자열로 — 단순화: alloca + snprintf */
                LLVMValueRef vbuf = LLVMBuildAlloca(cg->builder,
                    LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 64), "ev_val_buf");
                LLVMValueRef vptr = LLVMBuildBitCast(cg->builder, vbuf, i8p, "ev_val_p");
                LLVMSymbol *sym = scope_lookup(cg, ch->sval);
                if (sym) {
                    LLVMValueRef val = LLVMBuildLoad2(cg->builder, sym->type,
                                                      sym->alloca, "ev_v");
                    LLVMValueRef fmt_i = LLVMBuildGlobalStringPtr(cg->builder, "%lld", "ev_fi");
                    LLVMValueRef cnt64  = LLVMConstInt(i64, 64, 0);
                    LLVMValueRef sargs[] = { vptr, cnt64, fmt_i, val };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                        get_snprintf_fn(cg), sargs, 4, "");
                } else {
                    LLVMValueRef empty = LLVMBuildGlobalStringPtr(cg->builder, "", "ev_empty");
                    LLVMValueRef cnt64 = LLVMConstInt(i64, 64, 0);
                    LLVMValueRef sargs[] = { vptr, cnt64, empty };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                        get_snprintf_fn(cg), sargs, 3, "");
                }
                LLVMValueRef one = LLVMConstInt(i32, 1, 0);
                LLVMValueRef se_args[] = { key_str, vptr, one };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p,i32}, 3, 0),
                    get_setenv_fn(cg), se_args, 3, "");
            }

            /* 3. fopen(path_ptr, "w") → FILE* */
            {
                LLVMValueRef mode_w  = LLVMBuildGlobalStringPtr(cg->builder, "w", "sc_mw");
                LLVMValueRef fo_args[] = { path_ptr, mode_w };
                LLVMValueRef file_p = LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i8p, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                    get_fopen_fn(cg), fo_args, 2, "sc_fp");

                /* fputs(스크립트 원문, file_p) */
                LLVMValueRef src_str = LLVMBuildGlobalStringPtr(cg->builder, n->sval, "sc_src");
                LLVMValueRef fp_args[] = { src_str, file_p };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                    get_fputs_fn(cg), fp_args, 2, "");

                /* fclose(file_p) */
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, &i8p, 1, 0),
                    get_fclose_fn(cg), &file_p, 1, "");
            }

            /* 4. 실행 커맨드 구성 → cmd 버퍼 */
            LLVMValueRef cmd_buf = LLVMBuildAlloca(cg->builder,
                LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 640), "sc_cmd");
            LLVMValueRef cmd_ptr = LLVMBuildBitCast(cg->builder, cmd_buf, i8p, "sc_cmd_p");

            if (is_java) {
                /* mkdir + javac + java 파이프라인 */
                LLVMValueRef java_fmt = LLVMBuildGlobalStringPtr(cg->builder,
                    "mkdir -p /tmp/_kc_cls_%ld && "
                    "javac -d /tmp/_kc_cls_%ld %s > %s 2>&1 && "
                    "java -cp /tmp/_kc_cls_%ld Main >> %s 2>&1",
                    "jfmt");
                LLVMValueRef args640 = LLVMConstInt(i64, 640, 0);
                LLVMValueRef ja[] = { cmd_ptr, args640, java_fmt,
                                      pid64, pid64, path_ptr, out_ptr,
                                      pid64, out_ptr };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                    get_snprintf_fn(cg), ja, 9, "");
            } else {
                char run_fmt[32];
                snprintf(run_fmt, sizeof(run_fmt), "%s %%s > %%s 2>&1",
                         is_py ? "python3" : "node");
                LLVMValueRef rfmt = LLVMBuildGlobalStringPtr(cg->builder, run_fmt, "sc_rfmt");
                LLVMValueRef args640 = LLVMConstInt(i64, 640, 0);
                LLVMValueRef ra[] = { cmd_ptr, args640, rfmt, path_ptr, out_ptr };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                    get_snprintf_fn(cg), ra, 5, "");
            }

            /* system(cmd_ptr) */
            LLVMBuildCall2(cg->builder,
                LLVMFunctionType(i32, &i8p, 1, 0),
                get_system_fn(cg), &cmd_ptr, 1, "sc_sys");

            /* 5. 반환 변수 있으면 stdout 마지막 줄 읽기 */
            if (ret_child >= 0 && ret_child < n->child_count &&
                n->children[ret_child] && n->children[ret_child]->sval) {
                const char *rv = n->children[ret_child]->sval;
                /* 출력 버퍼 alloca 64KiB */
                LLVMValueRef rbuf = LLVMBuildAlloca(cg->builder,
                    LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 65536), "sc_rbuf");
                LLVMValueRef rptr = LLVMBuildBitCast(cg->builder, rbuf, i8p, "sc_rp");
                LLVMValueRef mode_r = LLVMBuildGlobalStringPtr(cg->builder, "r", "sc_mr");
                LLVMValueRef fo2_args[] = { out_ptr, mode_r };
                LLVMValueRef fp2 = LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i8p, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                    get_fopen_fn(cg), fo2_args, 2, "sc_rfp");
                LLVMValueRef sz64  = LLVMConstInt(i64, 65535, 0);
                LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
                LLVMValueRef fr_args[] = { rptr, one64, sz64, fp2 };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i64, (LLVMTypeRef[]){i8p,i64,i64,i8p}, 4, 0),
                    get_fread_fn(cg), fr_args, 4, "");
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, &i8p, 1, 0),
                    get_fclose_fn(cg), &fp2, 1, "");
                /* 반환 변수에 출력 버퍼 포인터 저장 */
                LLVMSymbol *rsym = scope_lookup(cg, rv);
                if (rsym)
                    LLVMBuildStore(cg->builder, rptr, rsym->alloca);
            }

            /* 6. 임시 파일 삭제 */
            LLVMBuildCall2(cg->builder,
                LLVMFunctionType(i32, &i8p, 1, 0),
                get_remove_fn(cg), &path_ptr, 1, "");
            LLVMBuildCall2(cg->builder,
                LLVMFunctionType(i32, &i8p, 1, 0),
                get_remove_fn(cg), &out_ptr, 1, "");
            break;
        }

        /* ── 가속기 블록 LLVM IR (v8.2.0) ─────────────────────────── */
        case NODE_GPU_BLOCK: {
            if (!block_is_open(cg)) break;

            sourcemap_add(cg, n->line, n->col);

            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);

            const char *accel_type = n->sval; /* "GPU"|"NPU"|"CPU"|NULL(자동) */

            /* NODE_GPU_OP 노드 수집 — 마지막 NODE_BLOCK 앞까지 */
            int body_idx = -1;
            for (int i = (int)n->child_count - 1; i >= 0; i--) {
                if (n->children[i] && n->children[i]->type == NODE_BLOCK) {
                    body_idx = i; break;
                }
            }
            int op_count = (body_idx >= 0) ? body_idx : (int)n->child_count;

            int do_gpu = (!accel_type ||
                          strcmp(accel_type, "GPU") == 0 ||
                          strcmp(accel_type, "AUTO") == 0);
            int do_npu = (!accel_type ||
                          strcmp(accel_type, "NPU") == 0 ||
                          strcmp(accel_type, "AUTO") == 0);
            int do_cpu = (!accel_type ||
                          strcmp(accel_type, "CPU") == 0 ||
                          strcmp(accel_type, "AUTO") == 0);

            /* ── 공통 임시 경로 버퍼 ── */
            LLVMValueRef cu_path_buf = LLVMBuildAlloca(cg->builder,
                LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 256), "gpu_cu_path");
            LLVMValueRef cu_path_ptr = LLVMBuildBitCast(cg->builder,
                cu_path_buf, i8p, "gpu_cu_path_p");

            LLVMValueRef cu_out_buf = LLVMBuildAlloca(cg->builder,
                LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 260), "gpu_cu_out");
            LLVMValueRef cu_out_ptr = LLVMBuildBitCast(cg->builder,
                cu_out_buf, i8p, "gpu_cu_out_p");

            LLVMValueRef cu_bin_buf = LLVMBuildAlloca(cg->builder,
                LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 260), "gpu_cu_bin");
            LLVMValueRef cu_bin_ptr = LLVMBuildBitCast(cg->builder,
                cu_bin_buf, i8p, "gpu_cu_bin_p");

            /* getpid() */
            LLVMValueRef pid = LLVMBuildCall2(cg->builder,
                LLVMFunctionType(i32, NULL, 0, 0),
                get_getpid_fn(cg), NULL, 0, "gpu_pid");
            LLVMValueRef pid64 = LLVMBuildZExt(cg->builder, pid, i64, "gpu_pid64");

            /* snprintf(cu_path_ptr, 256, "/tmp/_kcode_gpu_%d.cu", pid) */
            {
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(cg->builder,
                    "/tmp/_kcode_gpu_%ld.cu", "gpu_path_fmt");
                LLVMValueRef sz  = LLVMConstInt(i64, 256, 0);
                LLVMValueRef a[] = { cu_path_ptr, sz, fmt, pid64 };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                    get_snprintf_fn(cg), a, 4, "");
            }
            /* snprintf(cu_out_ptr, 260, "%s.log", cu_path_ptr) */
            {
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(cg->builder,
                    "%s.log", "gpu_log_fmt");
                LLVMValueRef sz  = LLVMConstInt(i64, 260, 0);
                LLVMValueRef a[] = { cu_out_ptr, sz, fmt, cu_path_ptr };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                    get_snprintf_fn(cg), a, 4, "");
            }
            /* snprintf(cu_bin_ptr, 260, "%s.out", cu_path_ptr) */
            {
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(cg->builder,
                    "%s.out", "gpu_bin_fmt");
                LLVMValueRef sz  = LLVMConstInt(i64, 260, 0);
                LLVMValueRef a[] = { cu_bin_ptr, sz, fmt, cu_path_ptr };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                    get_snprintf_fn(cg), a, 4, "");
            }

            if (op_count > 0 && do_gpu) {
                /* ══════════════════════════════════════════════════
                 * GPU 경로: CUDA C 파일 작성 → nvcc 컴파일 → 실행
                 * 스크립트 블록과 완전히 동일한 패턴 재활용
                 * ══════════════════════════════════════════════════ */

                /* ── CUDA C 파일 헤더 문자열 상수 ── */
                static const char kc_cuda_header[] =
                    "#include <stdio.h>\n#include <stdlib.h>\n"
                    "#include <string.h>\n#include <math.h>\n"
                    "#include <cuda_runtime.h>\n\n"
                    /* CSV 파서 헬퍼 */
                    "static long long kc_csv_parse("
                    "const char *s, double *out, long long max) {\n"
                    "  long long n=0; char *p=(char*)s,*e;\n"
                    "  while(*p && n<max){"
                    "out[n++]=strtod(p,&e);"
                    "if(e==p)break;p=(*e==',')?e+1:e;}\n"
                    "  return n;\n}\n\n"
                    /* 커널: 행렬곱 */
                    "__global__ void kc_kernel_matmul("
                    "double *r,const double *a,const double *b,"
                    "long long rows,long long cols){\n"
                    "  long long row=blockIdx.y*blockDim.y+threadIdx.y;\n"
                    "  long long col=blockIdx.x*blockDim.x+threadIdx.x;\n"
                    "  if(row<rows&&col<cols){\n"
                    "    double s=0.0;\n"
                    "    for(long long k=0;k<cols;k++)"
                    " s+=a[row*cols+k]*b[k*cols+col];\n"
                    "    r[row*cols+col]=s;\n  }\n}\n\n"
                    /* 커널: 행렬합 */
                    "__global__ void kc_kernel_matadd("
                    "double *r,const double *a,const double *b,long long n){\n"
                    "  long long idx=blockIdx.x*blockDim.x+threadIdx.x;\n"
                    "  if(idx<n) r[idx]=a[idx]+b[idx];\n}\n\n"
                    /* 커널: 합성곱 */
                    "__global__ void kc_kernel_conv2d("
                    "double *r,const double *img,const double *ker,"
                    "int iw,int ih,int kw,int kh){\n"
                    "  int x=blockIdx.x*blockDim.x+threadIdx.x;\n"
                    "  int y=blockIdx.y*blockDim.y+threadIdx.y;\n"
                    "  int ow=iw-kw+1,oh=ih-kh+1;\n"
                    "  if(x<ow&&y<oh){\n"
                    "    double s=0.0;\n"
                    "    for(int ky=0;ky<kh;ky++)"
                    " for(int kx=0;kx<kw;kx++)"
                    " s+=img[(y+ky)*iw+(x+kx)]*ker[ky*kw+kx];\n"
                    "    r[y*ow+x]=s;\n  }\n}\n\n"
                    /* 커널: 활성화 */
                    "__global__ void kc_kernel_activate("
                    "double *r,const double *a,long long n,int mode){\n"
                    "  long long idx=blockIdx.x*blockDim.x+threadIdx.x;\n"
                    "  if(idx<n){\n"
                    "    double v=a[idx];\n"
                    "    if(mode==0) r[idx]=(v>0.0?v:0.0);\n"
                    "    else if(mode==1) r[idx]=1.0/(1.0+exp(-v));\n"
                    "    else r[idx]=tanh(v);\n  }\n}\n\n"
                    /* 커널: 전치 */
                    "__global__ void kc_kernel_transpose("
                    "double *r,const double *a,long long rows,long long cols){\n"
                    "  long long row=blockIdx.y*blockDim.y+threadIdx.y;\n"
                    "  long long col=blockIdx.x*blockDim.x+threadIdx.x;\n"
                    "  if(row<rows&&col<cols) r[col*rows+row]=a[row*cols+col];\n"
                    "}\n\n"
                    "int main(void){\n"
                    "  long long N=atoll(getenv(\"KCODE_N\")?getenv(\"KCODE_N\"):\"1\");\n"
                    "  long long ROWS=atoll(getenv(\"KCODE_ROWS\")?getenv(\"KCODE_ROWS\"):\"1\");\n"
                    "  long long COLS=atoll(getenv(\"KCODE_COLS\")?getenv(\"KCODE_COLS\"):\"1\");\n"
                    "  int MODE=atoi(getenv(\"KCODE_MODE\")?getenv(\"KCODE_MODE\"):\"0\");\n";

                /* fopen(cu_path_ptr, "w") */
                LLVMValueRef mw = LLVMBuildGlobalStringPtr(cg->builder, "w", "gpu_mw");
                LLVMValueRef fo_a[] = { cu_path_ptr, mw };
                LLVMValueRef cu_fp = LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i8p, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                    get_fopen_fn(cg), fo_a, 2, "gpu_fp");

                /* fputs(cuda_header, cu_fp) */
                LLVMValueRef hdr_str = LLVMBuildGlobalStringPtr(cg->builder,
                    kc_cuda_header, "gpu_hdr");
                LLVMValueRef fph_a[] = { hdr_str, cu_fp };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                    get_fputs_fn(cg), fph_a, 2, "");

                /* 각 NODE_GPU_OP에 대한 CUDA 실행 코드 emit
                 * 전략: 각 OP 코드를 문자열 상수로 build_global_string_ptr 후
                 *       fputs()로 파일에 기록 — 스크립트 블록과 동일 패턴 */
                for (int oi = 0; oi < op_count; oi++) {
                    Node *op = n->children[oi];
                    if (!op || op->type != NODE_GPU_OP || !op->sval) continue;

                    const char *opname = op->sval;
                    int ret_ci  = (int)op->val.ival;
                    int arg_end = (ret_ci >= 0) ? ret_ci : (int)op->child_count;

                    /* 인수 이름 추출 */
                    const char *a0 = NULL, *a1 = NULL;
                    if (arg_end > 0 && op->children[0]
                        && op->children[0]->type == NODE_IDENT
                        && op->children[0]->sval)
                        a0 = op->children[0]->sval;
                    if (arg_end > 1 && op->children[1]
                        && op->children[1]->type == NODE_IDENT
                        && op->children[1]->sval)
                        a1 = op->children[1]->sval;

                    /* 반환 변수 이름 */
                    const char *rv_name = "_kc_result";
                    if (ret_ci >= 0 && ret_ci < (int)op->child_count
                        && op->children[ret_ci]
                        && op->children[ret_ci]->sval)
                        rv_name = op->children[ret_ci]->sval;

                    /* ── 입력 배열 읽기 코드 ── */
                    char inp_code[1024] = "";
                    if (a0) {
                        char tmp[512];
                        snprintf(tmp, sizeof(tmp),
                            "  double *h_%s=(double*)malloc(N*sizeof(double));\n"
                            "  {const char *_e=getenv(\"KCODE_%s\");"
                            "if(_e)kc_csv_parse(_e,h_%s,N);}\n",
                            a0, a0, a0);
                        strncat(inp_code, tmp, sizeof(inp_code)-strlen(inp_code)-1);
                    }
                    if (a1) {
                        char tmp[512];
                        snprintf(tmp, sizeof(tmp),
                            "  double *h_%s=(double*)malloc(N*sizeof(double));\n"
                            "  {const char *_e=getenv(\"KCODE_%s\");"
                            "if(_e)kc_csv_parse(_e,h_%s,N);}\n",
                            a1, a1, a1);
                        strncat(inp_code, tmp, sizeof(inp_code)-strlen(inp_code)-1);
                    }
                    /* 결과 배열 */
                    char res_decl[128];
                    snprintf(res_decl, sizeof(res_decl),
                        "  double *h_%s=(double*)malloc(N*sizeof(double));\n",
                        rv_name);
                    strncat(inp_code, res_decl, sizeof(inp_code)-strlen(inp_code)-1);

                    LLVMValueRef inp_str = LLVMBuildGlobalStringPtr(cg->builder,
                        inp_code, "gpu_inp");
                    LLVMValueRef fpi_a[] = { inp_str, cu_fp };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                        get_fputs_fn(cg), fpi_a, 2, "");

                    /* ── 커널 실행 코드 (연산별) ── */
                    char kern_code[2048] = "";

                    /* 행렬곱: kc_kernel_matmul */
                    if (strcmp(opname,
                        "\xED\x96\x89\xEB\xA0\xAC\xEA\xB3\xB1") == 0) {
                        snprintf(kern_code, sizeof(kern_code),
                            "  double *d_a,*d_b,*d_r;\n"
                            "  cudaMalloc(&d_a,N*sizeof(double));\n"
                            "  cudaMalloc(&d_b,N*sizeof(double));\n"
                            "  cudaMalloc(&d_r,N*sizeof(double));\n"
                            "  cudaMemcpy(d_a,h_%s,N*sizeof(double),cudaMemcpyHostToDevice);\n"
                            "  cudaMemcpy(d_b,h_%s,N*sizeof(double),cudaMemcpyHostToDevice);\n"
                            "  dim3 thr(16,16),blk((int)((COLS+15)/16),(int)((ROWS+15)/16));\n"
                            "  kc_kernel_matmul<<<blk,thr>>>(d_r,d_a,d_b,ROWS,COLS);\n"
                            "  cudaDeviceSynchronize();\n"
                            "  cudaMemcpy(h_%s,d_r,N*sizeof(double),cudaMemcpyDeviceToHost);\n"
                            "  cudaFree(d_a);cudaFree(d_b);cudaFree(d_r);\n",
                            a0?a0:"_a", a1?a1:"_b", rv_name);
                    }
                    /* 행렬합: kc_kernel_matadd */
                    else if (strcmp(opname,
                        "\xED\x96\x89\xEB\xA0\xAC\xED\x95\xA9") == 0) {
                        snprintf(kern_code, sizeof(kern_code),
                            "  double *d_a,*d_b,*d_r;\n"
                            "  cudaMalloc(&d_a,N*sizeof(double));\n"
                            "  cudaMalloc(&d_b,N*sizeof(double));\n"
                            "  cudaMalloc(&d_r,N*sizeof(double));\n"
                            "  cudaMemcpy(d_a,h_%s,N*sizeof(double),cudaMemcpyHostToDevice);\n"
                            "  cudaMemcpy(d_b,h_%s,N*sizeof(double),cudaMemcpyHostToDevice);\n"
                            "  int thr=256,blk=(int)((N+255)/256);\n"
                            "  kc_kernel_matadd<<<blk,thr>>>(d_r,d_a,d_b,N);\n"
                            "  cudaDeviceSynchronize();\n"
                            "  cudaMemcpy(h_%s,d_r,N*sizeof(double),cudaMemcpyDeviceToHost);\n"
                            "  cudaFree(d_a);cudaFree(d_b);cudaFree(d_r);\n",
                            a0?a0:"_a", a1?a1:"_b", rv_name);
                    }
                    /* 합성곱: kc_kernel_conv2d */
                    else if (strcmp(opname,
                        "\xED\x95\xA9\xEC\x84\xB1\xEA\xB3\xB1") == 0) {
                        snprintf(kern_code, sizeof(kern_code),
                            "  int kw=atoi(getenv(\"KCODE_KW\")?getenv(\"KCODE_KW\"):\"3\");\n"
                            "  int kh=atoi(getenv(\"KCODE_KH\")?getenv(\"KCODE_KH\"):\"3\");\n"
                            "  int iw=(int)COLS,ih=(int)ROWS;\n"
                            "  int ow=iw-kw+1,oh=ih-kh+1;\n"
                            "  double *d_img,*d_ker,*d_r;\n"
                            "  cudaMalloc(&d_img,N*sizeof(double));\n"
                            "  cudaMalloc(&d_ker,(long long)kw*kh*sizeof(double));\n"
                            "  cudaMalloc(&d_r,(long long)ow*oh*sizeof(double));\n"
                            "  cudaMemcpy(d_img,h_%s,N*sizeof(double),cudaMemcpyHostToDevice);\n"
                            "  cudaMemcpy(d_ker,h_%s,(long long)kw*kh*sizeof(double),cudaMemcpyHostToDevice);\n"
                            "  dim3 thr2(16,16),blk2((ow+15)/16,(oh+15)/16);\n"
                            "  kc_kernel_conv2d<<<blk2,thr2>>>(d_r,d_img,d_ker,iw,ih,kw,kh);\n"
                            "  cudaDeviceSynchronize();\n"
                            "  cudaMemcpy(h_%s,d_r,(long long)ow*oh*sizeof(double),cudaMemcpyDeviceToHost);\n"
                            "  cudaFree(d_img);cudaFree(d_ker);cudaFree(d_r);\n",
                            a0?a0:"_img", a1?a1:"_ker", rv_name);
                    }
                    /* 활성화: kc_kernel_activate */
                    else if (strcmp(opname,
                        "\xED\x99\x9C\xEC\x84\xB1\xED\x99\x94") == 0) {
                        snprintf(kern_code, sizeof(kern_code),
                            "  double *d_a,*d_r;\n"
                            "  cudaMalloc(&d_a,N*sizeof(double));\n"
                            "  cudaMalloc(&d_r,N*sizeof(double));\n"
                            "  cudaMemcpy(d_a,h_%s,N*sizeof(double),cudaMemcpyHostToDevice);\n"
                            "  int thr_a=256,blk_a=(int)((N+255)/256);\n"
                            "  kc_kernel_activate<<<blk_a,thr_a>>>(d_r,d_a,N,MODE);\n"
                            "  cudaDeviceSynchronize();\n"
                            "  cudaMemcpy(h_%s,d_r,N*sizeof(double),cudaMemcpyDeviceToHost);\n"
                            "  cudaFree(d_a);cudaFree(d_r);\n",
                            a0?a0:"_a", rv_name);
                    }
                    /* 전치: kc_kernel_transpose */
                    else if (strcmp(opname,
                        "\xEC\xA0\x84\xEC\xB9\x98") == 0) {
                        snprintf(kern_code, sizeof(kern_code),
                            "  double *d_a,*d_r;\n"
                            "  cudaMalloc(&d_a,N*sizeof(double));\n"
                            "  cudaMalloc(&d_r,N*sizeof(double));\n"
                            "  cudaMemcpy(d_a,h_%s,N*sizeof(double),cudaMemcpyHostToDevice);\n"
                            "  dim3 thr_t(16,16),blk_t((int)((COLS+15)/16),(int)((ROWS+15)/16));\n"
                            "  kc_kernel_transpose<<<blk_t,thr_t>>>(d_r,d_a,ROWS,COLS);\n"
                            "  cudaDeviceSynchronize();\n"
                            "  cudaMemcpy(h_%s,d_r,N*sizeof(double),cudaMemcpyDeviceToHost);\n"
                            "  cudaFree(d_a);cudaFree(d_r);\n",
                            a0?a0:"_a", rv_name);
                    }
                    else {
                        snprintf(kern_code, sizeof(kern_code),
                            "  /* [경고] 알 수 없는 연산: %s */\n", opname);
                    }

                    /* 결과 CSV 출력 코드 */
                    char out_code[512];
                    snprintf(out_code, sizeof(out_code),
                        "  for(long long _i=0;_i<N;_i++)"
                        "{printf(\"%%s%%.17g\",_i?\",\":\"\",h_%s[_i]);}\n"
                        "  printf(\"\\n\");\n"
                        "  free(h_%s);\n",
                        rv_name, rv_name);
                    if (a0) {
                        char tmp[128];
                        snprintf(tmp, sizeof(tmp), "  free(h_%s);\n", a0);
                        strncat(out_code, tmp, sizeof(out_code)-strlen(out_code)-1);
                    }
                    if (a1 && strcmp(opname,
                        "\xED\x99\x9C\xEC\x84\xB1\xED\x99\x94") != 0) {
                        char tmp[128];
                        snprintf(tmp, sizeof(tmp), "  free(h_%s);\n", a1);
                        strncat(out_code, tmp, sizeof(out_code)-strlen(out_code)-1);
                    }

                    /* kern_code + out_code → fputs */
                    {
                        LLVMValueRef ks = LLVMBuildGlobalStringPtr(cg->builder,
                            kern_code, "gpu_kern");
                        LLVMValueRef ka[] = { ks, cu_fp };
                        LLVMBuildCall2(cg->builder,
                            LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                            get_fputs_fn(cg), ka, 2, "");
                    }
                    {
                        LLVMValueRef os = LLVMBuildGlobalStringPtr(cg->builder,
                            out_code, "gpu_outc");
                        LLVMValueRef oa[] = { os, cu_fp };
                        LLVMBuildCall2(cg->builder,
                            LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                            get_fputs_fn(cg), oa, 2, "");
                    }

                    /* 입력 변수 → setenv("KCODE_이름", ...) */
                    for (int ai = 0; ai < arg_end; ai++) {
                        Node *arg = op->children[ai];
                        if (!arg || arg->type != NODE_IDENT || !arg->sval) continue;

                        char env_key[160];
                        snprintf(env_key, sizeof(env_key), "KCODE_%s", arg->sval);
                        LLVMValueRef key_s = LLVMBuildGlobalStringPtr(cg->builder,
                            env_key, "gpu_ekey");

                        /* 변수값 snprintf → 64byte 버퍼 → setenv */
                        LLVMValueRef vbuf = LLVMBuildAlloca(cg->builder,
                            LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 64),
                            "gpu_vbuf");
                        LLVMValueRef vptr = LLVMBuildBitCast(cg->builder,
                            vbuf, i8p, "gpu_vp");
                        LLVMSymbol *sym = scope_lookup(cg, arg->sval);
                        if (sym) {
                            LLVMValueRef val = LLVMBuildLoad2(cg->builder,
                                sym->type, sym->alloca, "gpu_v");
                            LLVMValueRef fmt_i = LLVMBuildGlobalStringPtr(cg->builder,
                                "%lld", "gpu_fi");
                            LLVMValueRef cnt64 = LLVMConstInt(i64, 64, 0);
                            LLVMValueRef sa[] = { vptr, cnt64, fmt_i, val };
                            LLVMBuildCall2(cg->builder,
                                LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                                get_snprintf_fn(cg), sa, 4, "");
                        } else {
                            LLVMValueRef empty = LLVMBuildGlobalStringPtr(cg->builder,
                                "", "gpu_empty");
                            LLVMValueRef cnt64 = LLVMConstInt(i64, 64, 0);
                            LLVMValueRef sa[] = { vptr, cnt64, empty };
                            LLVMBuildCall2(cg->builder,
                                LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                                get_snprintf_fn(cg), sa, 3, "");
                        }
                        LLVMValueRef one32 = LLVMConstInt(i32, 1, 0);
                        LLVMValueRef sea[] = { key_s, vptr, one32 };
                        LLVMBuildCall2(cg->builder,
                            LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p,i32}, 3, 0),
                            get_setenv_fn(cg), sea, 3, "");
                    }
                } /* for oi */

                /* main() 닫기 */
                LLVMValueRef main_end = LLVMBuildGlobalStringPtr(cg->builder,
                    "  return 0;\n}\n", "gpu_main_end");
                LLVMValueRef me_a[] = { main_end, cu_fp };
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                    get_fputs_fn(cg), me_a, 2, "");

                /* fclose(cu_fp) */
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, &i8p, 1, 0),
                    get_fclose_fn(cg), &cu_fp, 1, "");

                /* ── nvcc 컴파일 커맨드 ── */
                LLVMValueRef nv_cmd_buf = LLVMBuildAlloca(cg->builder,
                    LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 1024), "gpu_nvcmd");
                LLVMValueRef nv_cmd_ptr = LLVMBuildBitCast(cg->builder,
                    nv_cmd_buf, i8p, "gpu_nvcmd_p");

                {
                    LLVMValueRef nv_fmt = LLVMBuildGlobalStringPtr(cg->builder,
                        "nvcc '%s' -o '%s' -O2 -lm > '%s' 2>&1",
                        "gpu_nvfmt");
                    LLVMValueRef sz = LLVMConstInt(i64, 1024, 0);
                    LLVMValueRef a[] = { nv_cmd_ptr, sz, nv_fmt,
                                         cu_path_ptr, cu_bin_ptr, cu_out_ptr };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                        get_snprintf_fn(cg), a, 6, "");
                }

                /* system(nv_cmd_ptr) — nvcc 컴파일 */
                LLVMValueRef nvcc_ret = LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, &i8p, 1, 0),
                    get_system_fn(cg), &nv_cmd_ptr, 1, "gpu_nvcc_ret");
                (void)nvcc_ret; /* 반환값 무시 — 향후 분기 확장 예정 */

                /* ── 바이너리 실행 커맨드 ── */
                LLVMValueRef run_cmd_buf = LLVMBuildAlloca(cg->builder,
                    LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 512), "gpu_runcmd");
                LLVMValueRef run_cmd_ptr = LLVMBuildBitCast(cg->builder,
                    run_cmd_buf, i8p, "gpu_runcmd_p");

                {
                    LLVMValueRef run_fmt = LLVMBuildGlobalStringPtr(cg->builder,
                        "'%s' > '%s.res' 2>>'%s'", "gpu_runfmt");
                    LLVMValueRef sz = LLVMConstInt(i64, 512, 0);
                    LLVMValueRef a[] = { run_cmd_ptr, sz, run_fmt,
                                         cu_bin_ptr, cu_bin_ptr, cu_out_ptr };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                        get_snprintf_fn(cg), a, 6, "");
                }

                /* system(run_cmd_ptr) — GPU 바이너리 실행 */
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, &i8p, 1, 0),
                    get_system_fn(cg), &run_cmd_ptr, 1, "gpu_run_ret");

                /* ── 결과 수집: .res 파일 읽기 → 반환 변수 store ── */
                for (int oi = 0; oi < op_count; oi++) {
                    Node *op = n->children[oi];
                    if (!op || op->type != NODE_GPU_OP) continue;
                    int ret_ci = (int)op->val.ival;
                    if (ret_ci < 0 || ret_ci >= (int)op->child_count) continue;
                    Node *rv = op->children[ret_ci];
                    if (!rv || !rv->sval) continue;

                    /* 결과 버퍼 alloca 64KiB */
                    LLVMValueRef rbuf = LLVMBuildAlloca(cg->builder,
                        LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 65536),
                        "gpu_rbuf");
                    LLVMValueRef rptr = LLVMBuildBitCast(cg->builder,
                        rbuf, i8p, "gpu_rp");

                    /* res 경로: cu_bin_ptr + ".res" */
                    LLVMValueRef res_buf = LLVMBuildAlloca(cg->builder,
                        LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 280),
                        "gpu_res_path");
                    LLVMValueRef res_ptr = LLVMBuildBitCast(cg->builder,
                        res_buf, i8p, "gpu_res_p");
                    {
                        LLVMValueRef rfmt = LLVMBuildGlobalStringPtr(cg->builder,
                            "%s.res", "gpu_resfmt");
                        LLVMValueRef sz  = LLVMConstInt(i64, 280, 0);
                        LLVMValueRef a[] = { res_ptr, sz, rfmt, cu_bin_ptr };
                        LLVMBuildCall2(cg->builder,
                            LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                            get_snprintf_fn(cg), a, 4, "");
                    }

                    /* fopen(res_ptr, "r") → fread → fclose */
                    LLVMValueRef mr = LLVMBuildGlobalStringPtr(cg->builder,
                        "r", "gpu_mr");
                    LLVMValueRef fo_ra[] = { res_ptr, mr };
                    LLVMValueRef res_fp = LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i8p, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                        get_fopen_fn(cg), fo_ra, 2, "gpu_res_fp");

                    LLVMValueRef sz64  = LLVMConstInt(i64, 65535, 0);
                    LLVMValueRef one64 = LLVMConstInt(i64, 1,     0);
                    LLVMValueRef fr_a[] = { rptr, one64, sz64, res_fp };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i64, (LLVMTypeRef[]){i8p,i64,i64,i8p}, 4, 0),
                        get_fread_fn(cg), fr_a, 4, "");

                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, &i8p, 1, 0),
                        get_fclose_fn(cg), &res_fp, 1, "");

                    /* remove(res_ptr) */
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, &i8p, 1, 0),
                        get_remove_fn(cg), &res_ptr, 1, "");

                    /* 반환 변수 alloca에 결과 포인터 store */
                    LLVMSymbol *rsym = scope_lookup(cg, rv->sval);
                    if (rsym)
                        LLVMBuildStore(cg->builder, rptr, rsym->alloca);
                } /* for oi 결과 수집 */

            } /* if op_count > 0 && do_gpu */
            else if (do_npu && op_count > 0) {
                /* ══════════════════════════════════════════════════
                 * NPU 경로: Python onnxruntime 스크립트 자동생성
                 *           → python3 실행 → 결과 수집
                 * GPU 경로와 동일한 fopen/fputs/fclose/system 패턴
                 * ══════════════════════════════════════════════════ */

                /* NPU Python 스크립트 경로 버퍼 */
                LLVMValueRef py_path_buf = LLVMBuildAlloca(cg->builder,
                    LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 256), "npu_py_path");
                LLVMValueRef py_path_ptr = LLVMBuildBitCast(cg->builder,
                    py_path_buf, i8p, "npu_py_path_p");

                LLVMValueRef py_out_buf = LLVMBuildAlloca(cg->builder,
                    LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 270), "npu_py_out");
                LLVMValueRef py_out_ptr = LLVMBuildBitCast(cg->builder,
                    py_out_buf, i8p, "npu_py_out_p");

                /* snprintf(py_path_ptr, 256, "/tmp/_kcode_npu_%ld.py", pid) */
                {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(cg->builder,
                        "/tmp/_kcode_npu_%ld.py", "npu_py_fmt");
                    LLVMValueRef sz  = LLVMConstInt(i64, 256, 0);
                    LLVMValueRef a[] = { py_path_ptr, sz, fmt, pid64 };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                        get_snprintf_fn(cg), a, 4, "");
                }
                /* snprintf(py_out_ptr, 270, "%s.out", py_path_ptr) */
                {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(cg->builder,
                        "%s.out", "npu_out_fmt");
                    LLVMValueRef sz  = LLVMConstInt(i64, 270, 0);
                    LLVMValueRef a[] = { py_out_ptr, sz, fmt, py_path_ptr };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                        get_snprintf_fn(cg), a, 4, "");
                }

                /* fopen(py_path_ptr, "w") */
                LLVMValueRef mw  = LLVMBuildGlobalStringPtr(cg->builder, "w", "npu_mw");
                LLVMValueRef fo_wa[] = { py_path_ptr, mw };
                LLVMValueRef py_fp = LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i8p, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                    get_fopen_fn(cg), fo_wa, 2, "npu_py_fp");

                /* Python 스크립트 공통 헤더 fputs */
                {
                    static const char npu_py_header[] =
                        "import os, sys, numpy as np\n"
                        "def csv_to_arr(s):\n"
                        "    if not s: return np.array([], dtype=np.float64)\n"
                        "    return np.array([float(x) for x in s.split(',')], dtype=np.float64)\n"
                        "def arr_to_csv(a):\n"
                        "    return '\\n'.join(['%.17g' % v for v in a.flatten()])\n";
                    LLVMValueRef hdr = LLVMBuildGlobalStringPtr(cg->builder,
                        npu_py_header, "npu_py_hdr");
                    LLVMValueRef fa[] = { hdr, py_fp };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                        get_fputs_fn(cg), fa, 2, "");
                }

                /* 각 OP별 Python 코드 emit */
                for (int oi = 0; oi < op_count; oi++) {
                    Node *op = n->children[oi];
                    if (!op || op->type != NODE_GPU_OP || !op->sval) continue;

                    int ret_ci   = (int)op->val.ival;
                    int arg_end2 = (ret_ci >= 0) ? ret_ci : (int)op->child_count;
                    const char *arg0 = (arg_end2 > 0 && op->children[0]) ? op->children[0]->sval : "A";
                    const char *arg1 = (arg_end2 > 1 && op->children[1]) ? op->children[1]->sval : "B";

                    /* 환경변수 읽기 코드 */
                    char env_read[512];
                    snprintf(env_read, sizeof(env_read),
                        "ea = os.environ.get('KCODE_%s', '')\n"
                        "eb = os.environ.get('KCODE_%s', '')\n"
                        "a = csv_to_arr(ea)\nb = csv_to_arr(eb)\n",
                        arg0, arg1);
                    LLVMValueRef er = LLVMBuildGlobalStringPtr(cg->builder, env_read, "npu_envrd");
                    LLVMValueRef er_a[] = { er, py_fp };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                        get_fputs_fn(cg), er_a, 2, "");

                    /* 연산별 Python onnxruntime 코드 상수 */
                    const char *op_py = NULL;

                    /* 행렬곱 */
                    if (strcmp(op->sval, "\xED\x96\x89\xEB\xA0\xAC\xEA\xB3\xB1") == 0)
                        op_py =
                            "try:\n"
                            "    import onnxruntime as ort\n"
                            "    from onnx import helper, TensorProto\n"
                            "    n=max(len(a),len(b))\n"
                            "    a2=np.resize(a,n).astype(np.float32).reshape(1,n)\n"
                            "    b2=np.resize(b,n).astype(np.float32).reshape(n,1)\n"
                            "    X=helper.make_tensor_value_info('X',TensorProto.FLOAT,[1,n])\n"
                            "    Y=helper.make_tensor_value_info('Y',TensorProto.FLOAT,[n,1])\n"
                            "    Z=helper.make_tensor_value_info('Z',TensorProto.FLOAT,[1,1])\n"
                            "    node=helper.make_node('MatMul',['X','Y'],['Z'])\n"
                            "    g=helper.make_graph([node],'g',[X,Y],[Z])\n"
                            "    m=helper.make_model(g)\n"
                            "    sess=ort.InferenceSession(m.SerializeToString())\n"
                            "    out=sess.run(['Z'],{'X':a2,'Y':b2})[0].flatten()\n"
                            "except Exception:\n    out=a*b\n"
                            "print(arr_to_csv(out))\n";
                    /* 행렬합 */
                    else if (strcmp(op->sval, "\xED\x96\x89\xEB\xA0\xAC\xED\x95\xA9") == 0)
                        op_py =
                            "try:\n"
                            "    import onnxruntime as ort\n"
                            "    from onnx import helper, TensorProto\n"
                            "    n=max(len(a),len(b))\n"
                            "    a2=np.resize(a,n).astype(np.float32).reshape(1,n)\n"
                            "    b2=np.resize(b,n).astype(np.float32).reshape(1,n)\n"
                            "    X=helper.make_tensor_value_info('X',TensorProto.FLOAT,[1,n])\n"
                            "    Y=helper.make_tensor_value_info('Y',TensorProto.FLOAT,[1,n])\n"
                            "    Z=helper.make_tensor_value_info('Z',TensorProto.FLOAT,[1,n])\n"
                            "    node=helper.make_node('Add',['X','Y'],['Z'])\n"
                            "    g=helper.make_graph([node],'g',[X,Y],[Z])\n"
                            "    m=helper.make_model(g)\n"
                            "    sess=ort.InferenceSession(m.SerializeToString())\n"
                            "    out=sess.run(['Z'],{'X':a2,'Y':b2})[0].flatten()\n"
                            "except Exception:\n    out=a+b\n"
                            "print(arr_to_csv(out))\n";
                    /* 활성화 */
                    else if (strcmp(op->sval, "\xED\x99\x9C\xEC\x84\xB1\xED\x99\x94") == 0)
                        op_py =
                            "try:\n"
                            "    import onnxruntime as ort\n"
                            "    from onnx import helper, TensorProto\n"
                            "    n=len(a)\n"
                            "    a2=a.astype(np.float32).reshape(1,n)\n"
                            "    X=helper.make_tensor_value_info('X',TensorProto.FLOAT,[1,n])\n"
                            "    Z=helper.make_tensor_value_info('Z',TensorProto.FLOAT,[1,n])\n"
                            "    node=helper.make_node('Relu',['X'],['Z'])\n"
                            "    g=helper.make_graph([node],'g',[X],[Z])\n"
                            "    m=helper.make_model(g)\n"
                            "    sess=ort.InferenceSession(m.SerializeToString())\n"
                            "    out=sess.run(['Z'],{'X':a2})[0].flatten()\n"
                            "except Exception:\n    out=np.maximum(a,0.0)\n"
                            "print(arr_to_csv(out))\n";
                    /* 전치 */
                    else if (strcmp(op->sval, "\xEC\xA0\x84\xEC\xB9\x98") == 0)
                        op_py =
                            "try:\n"
                            "    import onnxruntime as ort\n"
                            "    from onnx import helper, TensorProto\n"
                            "    n=len(a); sq=int(n**0.5)\n"
                            "    if sq*sq==n:\n"
                            "        a2=a.astype(np.float32).reshape(1,sq,sq)\n"
                            "        X=helper.make_tensor_value_info('X',TensorProto.FLOAT,[1,sq,sq])\n"
                            "        Z=helper.make_tensor_value_info('Z',TensorProto.FLOAT,[1,sq,sq])\n"
                            "        node=helper.make_node('Transpose',['X'],['Z'],perm=[0,2,1])\n"
                            "        g=helper.make_graph([node],'g',[X],[Z])\n"
                            "        m=helper.make_model(g)\n"
                            "        sess=ort.InferenceSession(m.SerializeToString())\n"
                            "        out=sess.run(['Z'],{'X':a2})[0].flatten()\n"
                            "    else:\n        out=a[::-1]\n"
                            "except Exception:\n    out=a[::-1]\n"
                            "print(arr_to_csv(out))\n";
                    /* 합성곱 및 기타 */
                    else
                        op_py =
                            "try:\n"
                            "    import onnxruntime as ort\n"
                            "    from onnx import helper, TensorProto, numpy_helper\n"
                            "    n=max(len(a),len(b) if len(b)>0 else 1)\n"
                            "    a2=np.resize(a,n).astype(np.float32).reshape(1,1,1,n)\n"
                            "    bw=np.resize(b if len(b)>0 else np.ones(1),1).astype(np.float32).reshape(1,1,1,1)\n"
                            "    X=helper.make_tensor_value_info('X',TensorProto.FLOAT,[1,1,1,n])\n"
                            "    Z=helper.make_tensor_value_info('Z',TensorProto.FLOAT,None)\n"
                            "    w_init=numpy_helper.from_array(bw,name='W')\n"
                            "    node=helper.make_node('Conv',['X','W'],['Z'])\n"
                            "    g=helper.make_graph([node],'g',[X],[Z],[w_init])\n"
                            "    m=helper.make_model(g)\n"
                            "    sess=ort.InferenceSession(m.SerializeToString())\n"
                            "    out=sess.run(['Z'],{'X':a2})[0].flatten()\n"
                            "except Exception:\n    out=a*b if len(b)>0 else a\n"
                            "print(arr_to_csv(out))\n";

                    if (op_py) {
                        LLVMValueRef opv = LLVMBuildGlobalStringPtr(cg->builder, op_py, "npu_op_py");
                        LLVMValueRef oa[] = { opv, py_fp };
                        LLVMBuildCall2(cg->builder,
                            LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                            get_fputs_fn(cg), oa, 2, "");
                    }
                } /* for oi */

                /* fclose(py_fp) */
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, &i8p, 1, 0),
                    get_fclose_fn(cg), &py_fp, 1, "");

                /* python3 실행 커맨드 */
                LLVMValueRef py_cmd_buf = LLVMBuildAlloca(cg->builder,
                    LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 768), "npu_pycmd");
                LLVMValueRef py_cmd_ptr = LLVMBuildBitCast(cg->builder,
                    py_cmd_buf, i8p, "npu_pycmd_p");
                {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(cg->builder,
                        "python3 '%s' > '%s' 2>&1", "npu_run_fmt");
                    LLVMValueRef sz  = LLVMConstInt(i64, 768, 0);
                    LLVMValueRef a[] = { py_cmd_ptr, sz, fmt, py_path_ptr, py_out_ptr };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, (LLVMTypeRef[]){i8p,i64,i8p}, 3, 1),
                        get_snprintf_fn(cg), a, 5, "");
                }

                /* system(py_cmd_ptr) */
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, &i8p, 1, 0),
                    get_system_fn(cg), &py_cmd_ptr, 1, "npu_run_ret");

                /* 결과 수집: py_out 파일 → 반환 변수 */
                for (int oi = 0; oi < op_count; oi++) {
                    Node *op = n->children[oi];
                    if (!op || op->type != NODE_GPU_OP) continue;
                    int ret_ci = (int)op->val.ival;
                    if (ret_ci < 0 || ret_ci >= (int)op->child_count) continue;
                    Node *rv = op->children[ret_ci];
                    if (!rv || !rv->sval) continue;

                    /* 결과 버퍼 alloca 64KiB */
                    LLVMValueRef rbuf = LLVMBuildAlloca(cg->builder,
                        LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 65536),
                        "npu_rbuf");
                    LLVMValueRef rptr = LLVMBuildBitCast(cg->builder,
                        rbuf, i8p, "npu_rp");

                    /* fopen(py_out_ptr, "r") → fread → fclose */
                    LLVMValueRef mr  = LLVMBuildGlobalStringPtr(cg->builder, "r", "npu_mr");
                    LLVMValueRef fo_ra[] = { py_out_ptr, mr };
                    LLVMValueRef res_fp = LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i8p, (LLVMTypeRef[]){i8p,i8p}, 2, 0),
                        get_fopen_fn(cg), fo_ra, 2, "npu_res_fp");

                    LLVMValueRef sz64  = LLVMConstInt(i64, 65535, 0);
                    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
                    LLVMValueRef fr_a[] = { rptr, one64, sz64, res_fp };
                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i64, (LLVMTypeRef[]){i8p,i64,i64,i8p}, 4, 0),
                        get_fread_fn(cg), fr_a, 4, "");

                    LLVMBuildCall2(cg->builder,
                        LLVMFunctionType(i32, &i8p, 1, 0),
                        get_fclose_fn(cg), &res_fp, 1, "");

                    /* 반환 변수 alloca에 결과 포인터 store */
                    LLVMSymbol *rsym = scope_lookup(cg, rv->sval);
                    if (rsym)
                        LLVMBuildStore(cg->builder, rptr, rsym->alloca);
                } /* for oi 결과 수집 */

                /* 임시 파일 정리 */
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, &i8p, 1, 0),
                    get_remove_fn(cg), &py_path_ptr, 1, "");
                LLVMBuildCall2(cg->builder,
                    LLVMFunctionType(i32, &i8p, 1, 0),
                    get_remove_fn(cg), &py_out_ptr, 1, "");

            } /* else if do_npu */
            else if (do_cpu && op_count == 0) {
                /* CPU 전용 / 내장 연산 없음 — OpenMP 힌트만 (메타데이터) */
                /* LLVM IR에서 OpenMP는 별도 런타임이 필요하므로
                 * 여기서는 주석 역할의 메타데이터 마킹만 수행 */
            }

            /* ── 일반 본문 블록 실행 ── */
            if (body_idx >= 0 && n->children[body_idx]) {
                gen_stmt(cg, n->children[body_idx]);
            }

            /* ── 임시 파일 정리 ── */
            LLVMBuildCall2(cg->builder,
                LLVMFunctionType(i32, &i8p, 1, 0),
                get_remove_fn(cg), &cu_path_ptr, 1, "");
            LLVMBuildCall2(cg->builder,
                LLVMFunctionType(i32, &i8p, 1, 0),
                get_remove_fn(cg), &cu_out_ptr, 1, "");
            LLVMBuildCall2(cg->builder,
                LLVMFunctionType(i32, &i8p, 1, 0),
                get_remove_fn(cg), &cu_bin_ptr, 1, "");
            break;
        }
        /* ── 가속기 블록 끝 (v9.0.0) ─────────────────────────── */

        /* ── #포함 / 가짐 (v9.3.0) ─────────────────────────────
         *  전략:
         *    NODE_PP_STMT (#포함):
         *      - .c / .h  → kc_include(fname) 외부 함수 call
         *      - .han/.hg → 런타임 포함 불필요 (Kcode 간 인터페이스는
         *                   컴파일 시 처리) — kc_include() 힌트 call만 삽입
         *      - .py/.js/.ts/.java → kc_include() call (스크립트 로더 힌트)
         *    NODE_IMPORT (가짐):
         *      - 내장 모듈(수학/문자열/파일시스템/시간/난수):
         *          C 런타임 함수 declare를 모듈에 추가
         *      - 외부 모듈: kc_module_load(modname) call
         * ============================================================ */
        case NODE_PP_STMT: {
            const char *fname = n->sval ? n->sval : "";
            sourcemap_add(cg, n->line, n->col);
            /* 확장자에 무관하게 kc_include 런타임 힌트 call 삽입 */
            if (block_is_open(cg))
                gen_kc_include_call(cg, fname);
            break;
        }

        case NODE_IMPORT: {
            const char *mod = n->sval ? n->sval : "";
            sourcemap_add(cg, n->line, n->col);
            /* 내장 모듈 → 런타임 함수 declare / 외부 모듈 → load call */
            gen_import_builtin_decls(cg, mod);
            break;
        }

        /* ── 계약 시스템 6종 (v9.4.0) ──────────────────────────────
         *  전략: 계약 위반 검사를 런타임 외부 함수 call로 위임
         *    kc_assert(msg, cond)                → 법령(사전조건)
         *    kc_postcond(fn, msg, sanction, cond) → 법위반(사후조건)
         *    kc_constitution(msg, cond)           → 헌법(전역 최상위)
         *    kc_statute(msg, cond)                → 법률(파일 단위)
         *    kc_regulation(obj, msg, cond)        → 규정(객체 단위)
         *    kc_checkpoint(name)                  → 복원지점 등록
         *  ※ 실제 계약 위반 처리는 런타임 라이브러리에서 수행
         * ============================================================ */
        case NODE_CONTRACT: {
            sourcemap_add(cg, n->line, n->col);
            gen_contract_ir(cg, n);
            break;
        }

        case NODE_CONSTITUTION: {
            sourcemap_add(cg, n->line, n->col);
            if (!block_is_open(cg)) break;
            LLVMValueRef cond_val = NULL;
            if (n->child_count > 0 && n->children[0])
                cond_val = gen_expr(cg, n->children[0]);
            gen_contract_call2(cg, "kc_constitution",
                               "[헌법 계약 위반]", cond_val);
            break;
        }

        case NODE_STATUTE: {
            sourcemap_add(cg, n->line, n->col);
            if (!block_is_open(cg)) break;
            LLVMValueRef cond_val = NULL;
            if (n->child_count > 0 && n->children[0])
                cond_val = gen_expr(cg, n->children[0]);
            gen_contract_call2(cg, "kc_statute",
                               "[법률 계약 위반]", cond_val);
            break;
        }

        case NODE_REGULATION: {
            sourcemap_add(cg, n->line, n->col);
            if (!block_is_open(cg)) break;
            const char *obj_name = n->sval ? n->sval : "?";
            LLVMValueRef cond_val = NULL;
            if (n->child_count > 0 && n->children[0])
                cond_val = gen_expr(cg, n->children[0]);
            /* kc_regulation(obj, msg, cond) */
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i1  = LLVMInt1TypeInContext(cg->ctx);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_regulation");
            if (!fn) {
                LLVMTypeRef params[3] = { i8p, i8p, i1 };
                LLVMTypeRef ft = LLVMFunctionType(
                    LLVMVoidTypeInContext(cg->ctx), params, 3, 0);
                fn = LLVMAddFunction(cg->module, "kc_regulation", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMValueRef obj_str = LLVMBuildGlobalStringPtr(
                cg->builder, obj_name, "reg_obj");
            char reg_msg[256];
            snprintf(reg_msg, sizeof(reg_msg), "[규정 계약 위반: %s]", obj_name);
            LLVMValueRef msg_str = LLVMBuildGlobalStringPtr(
                cg->builder, reg_msg, "reg_msg");
            if (!cond_val) cond_val = LLVMConstInt(i1, 1, 0);
            if (LLVMGetTypeKind(LLVMTypeOf(cond_val)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(cond_val)) != 1) {
                LLVMTypeRef vt = LLVMTypeOf(cond_val);
                if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind)
                    cond_val = LLVMBuildICmp(cg->builder, LLVMIntNE,
                        cond_val, LLVMConstInt(vt, 0, 0), "reg_b");
                else
                    cond_val = LLVMConstInt(i1, 1, 0);
            }
            LLVMTypeRef params[3] = { i8p, i8p, i1 };
            LLVMTypeRef ft = LLVMFunctionType(
                LLVMVoidTypeInContext(cg->ctx), params, 3, 0);
            LLVMValueRef args[3] = { obj_str, msg_str, cond_val };
            LLVMBuildCall2(cg->builder, ft, fn, args, 3, "");
            break;
        }

        case NODE_CHECKPOINT: {
            sourcemap_add(cg, n->line, n->col);
            if (!block_is_open(cg)) break;
            const char *cp_name = n->sval ? n->sval : "checkpoint";
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_checkpoint");
            if (!fn) {
                LLVMTypeRef ft = LLVMFunctionType(
                    LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
                fn = LLVMAddFunction(cg->module, "kc_checkpoint", ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMValueRef name_str = LLVMBuildGlobalStringPtr(
                cg->builder, cp_name, "cp_name");
            LLVMTypeRef ft = LLVMFunctionType(
                LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
            LLVMBuildCall2(cg->builder, ft, fn, &name_str, 1, "");
            break;
        }

        case NODE_SANCTION:
            /* 제재 노드는 NODE_CONTRACT 내부에서 이미 처리됨 */
            break;

        /* ── 인터럽트 시스템 6종 (v9.5.0) ──────────────────────────
         *  A: 신호(Signal) — OS 프로세스 신호 처리
         *     NODE_SIGNAL_HANDLER: 핸들러 함수 IR 생성 + signal() call
         *     NODE_SIGNAL_CTRL   : 신호무시/신호기본/신호보내기
         *  B: 간섭(ISR) — 하드웨어 인터럽트
         *     NODE_ISR_HANDLER: ISR 함수 IR 생성 + kc_isr_register() call
         *     NODE_ISR_CTRL   : 간섭잠금(kc_isr_lock) / 간섭허용(kc_isr_unlock)
         *  C: 행사(Event) — 소프트웨어 이벤트
         *     NODE_EVENT_HANDLER: 핸들러 함수 IR + kc_event_register() call
         *     NODE_EVENT_CTRL   : 행사시작/중단/발생/해제
         * ============================================================ */

        /* A: 신호받기 */
        case NODE_SIGNAL_HANDLER: {
            sourcemap_add(cg, n->line, n->col);
            gen_signal_handler_ir(cg, n);
            break;
        }

        /* A: 신호무시 / 신호기본 / 신호보내기 */
        case NODE_SIGNAL_CTRL: {
            sourcemap_add(cg, n->line, n->col);
            if (!block_is_open(cg)) break;
            int posix_sig = sig_token_to_posix(n->op, n->sval);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);

            if (n->op == TOK_KW_SINHOBONEGI) {
                /* kill(pid, sig): i32 kill(i64 pid, i32 sig) */
                LLVMValueRef fn_kill = LLVMGetNamedFunction(cg->module, "kill");
                if (!fn_kill) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);
                    LLVMTypeRef params[2] = { i64, i32 };
                    LLVMTypeRef ft = LLVMFunctionType(i32, params, 2, 0);
                    fn_kill = LLVMAddFunction(cg->module, "kill", ft);
                    LLVMSetLinkage(fn_kill, LLVMExternalLinkage);
                }
                LLVMTypeRef i64 = LLVMInt64TypeInContext(cg->ctx);
                LLVMValueRef pid_val;
                if (n->child_count > 0 && n->children[0])
                    pid_val = gen_expr(cg, n->children[0]);
                else
                    pid_val = LLVMConstInt(i64, 0, 0);
                if (LLVMGetTypeKind(LLVMTypeOf(pid_val)) != LLVMIntegerTypeKind ||
                    LLVMGetIntTypeWidth(LLVMTypeOf(pid_val)) != 64)
                    pid_val = LLVMBuildSExt(cg->builder, pid_val, i64, "pid64");
                LLVMValueRef sig_val = LLVMConstInt(i32, (unsigned long long)posix_sig, 0);
                LLVMTypeRef params[2] = { i64, i32 };
                LLVMTypeRef ft = LLVMFunctionType(i32, params, 2, 0);
                LLVMValueRef args[2] = { pid_val, sig_val };
                LLVMBuildCall2(cg->builder, ft, fn_kill, args, 2, "");
            } else {
                /* kc_signal_ctrl(sig: i32, action: i32)
                 * action: 0=SIG_IGN(무시), 1=SIG_DFL(기본) */
                LLVMValueRef fn_ctrl = LLVMGetNamedFunction(cg->module, "kc_signal_ctrl");
                if (!fn_ctrl) {
                    LLVMTypeRef params[2] = { i32, i32 };
                    LLVMTypeRef ft = LLVMFunctionType(
                        LLVMVoidTypeInContext(cg->ctx), params, 2, 0);
                    fn_ctrl = LLVMAddFunction(cg->module, "kc_signal_ctrl", ft);
                    LLVMSetLinkage(fn_ctrl, LLVMExternalLinkage);
                }
                int action = (n->op == TOK_KW_SINHOMUSI) ? 0 : 1;
                LLVMValueRef sig_val = LLVMConstInt(i32, (unsigned long long)posix_sig, 0);
                LLVMValueRef act_val = LLVMConstInt(i32, (unsigned long long)action, 0);
                LLVMTypeRef params[2] = { i32, i32 };
                LLVMTypeRef ft = LLVMFunctionType(
                    LLVMVoidTypeInContext(cg->ctx), params, 2, 0);
                LLVMValueRef args[2] = { sig_val, act_val };
                LLVMBuildCall2(cg->builder, ft, fn_ctrl, args, 2, "");
            }
            (void)i8p;
            break;
        }

        /* B: 간섭(ISR) 핸들러 등록 */
        case NODE_ISR_HANDLER: {
            sourcemap_add(cg, n->line, n->col);
            gen_isr_handler_ir(cg, n);
            break;
        }

        /* B: 간섭잠금 / 간섭허용 */
        case NODE_ISR_CTRL: {
            sourcemap_add(cg, n->line, n->col);
            if (!block_is_open(cg)) break;
            const char *fn_name = (n->op == TOK_KW_GANSEOB_JAMGEUM)
                ? "kc_isr_lock" : "kc_isr_unlock";
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, fn_name);
            if (!fn) {
                LLVMTypeRef ft = LLVMFunctionType(
                    LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
                fn = LLVMAddFunction(cg->module, fn_name, ft);
                LLVMSetLinkage(fn, LLVMExternalLinkage);
            }
            LLVMTypeRef ft = LLVMFunctionType(
                LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
            LLVMBuildCall2(cg->builder, ft, fn, NULL, 0, "");
            break;
        }

        /* C: 행사등록 */
        case NODE_EVENT_HANDLER: {
            sourcemap_add(cg, n->line, n->col);
            gen_event_handler_ir(cg, n);
            break;
        }

        /* C: 행사시작 / 행사중단 / 행사발생 / 행사해제 */
        case NODE_EVENT_CTRL: {
            sourcemap_add(cg, n->line, n->col);
            if (!block_is_open(cg)) break;
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);

            if (n->op == TOK_KW_HAENGSA_START || n->op == TOK_KW_HAENGSA_STOP) {
                const char *fn_name = (n->op == TOK_KW_HAENGSA_START)
                    ? "kc_event_loop_run" : "kc_event_loop_stop";
                LLVMValueRef fn = LLVMGetNamedFunction(cg->module, fn_name);
                if (!fn) {
                    LLVMTypeRef ft = LLVMFunctionType(
                        LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
                    fn = LLVMAddFunction(cg->module, fn_name, ft);
                    LLVMSetLinkage(fn, LLVMExternalLinkage);
                }
                LLVMTypeRef ft = LLVMFunctionType(
                    LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
                LLVMBuildCall2(cg->builder, ft, fn, NULL, 0, "");
            } else {
                /* kc_event_emit(name) / kc_event_unregister(name) */
                const char *fn_name = (n->op == TOK_KW_HAENGSA_EMIT)
                    ? "kc_event_emit" : "kc_event_unregister";
                LLVMValueRef fn = LLVMGetNamedFunction(cg->module, fn_name);
                if (!fn) {
                    LLVMTypeRef ft = LLVMFunctionType(
                        LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
                    fn = LLVMAddFunction(cg->module, fn_name, ft);
                    LLVMSetLinkage(fn, LLVMExternalLinkage);
                }
                LLVMValueRef ev_val = NULL;
                if (n->child_count > 0 && n->children[0])
                    ev_val = gen_expr(cg, n->children[0]);
                if (!ev_val || LLVMGetTypeKind(LLVMTypeOf(ev_val)) != LLVMPointerTypeKind)
                    ev_val = LLVMBuildGlobalStringPtr(cg->builder, "", "ev_empty");
                LLVMTypeRef ft = LLVMFunctionType(
                    LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
                LLVMBuildCall2(cg->builder, ft, fn, &ev_val, 1, "");
            }
            break;
        }
            /* 표현식 구문으로 처리 시도 */
            gen_expr(cg, n);
            break;

        /* ── 산업/임베디드 블록 (v16.0.0) ─────────────────── */
        case NODE_TIMER_BLOCK: {
            if (!block_is_open(cg)) break;
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            const char *period = n->sval ? n->sval : "100ms";
            /* kc_timer_start(period) */
            LLVMValueRef fn_ts = LLVMGetNamedFunction(cg->module, "kc_timer_start");
            if (!fn_ts) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
                fn_ts = LLVMAddFunction(cg->module, "kc_timer_start", ft);
                LLVMSetLinkage(fn_ts, LLVMExternalLinkage);
            }
            LLVMValueRef ps = LLVMBuildGlobalStringPtr(cg->builder, period, "timer_period");
            LLVMTypeRef ft_ts = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
            LLVMBuildCall2(cg->builder, ft_ts, fn_ts, &ps, 1, "");
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            /* kc_timer_stop() */
            LLVMValueRef fn_tp = LLVMGetNamedFunction(cg->module, "kc_timer_stop");
            if (!fn_tp) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
                fn_tp = LLVMAddFunction(cg->module, "kc_timer_stop", ft);
                LLVMSetLinkage(fn_tp, LLVMExternalLinkage);
            }
            LLVMTypeRef ft_tp = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
            LLVMBuildCall2(cg->builder, ft_tp, fn_tp, NULL, 0, "");
            break;
        }
        case NODE_ROS2_BLOCK: {
            if (!block_is_open(cg)) break;
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            const char *nname = n->sval ? n->sval : "ros2_node";
            LLVMValueRef fn_ri = LLVMGetNamedFunction(cg->module, "kc_ros2_node_init");
            if (!fn_ri) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
                fn_ri = LLVMAddFunction(cg->module, "kc_ros2_node_init", ft);
                LLVMSetLinkage(fn_ri, LLVMExternalLinkage);
            }
            LLVMValueRef ns = LLVMBuildGlobalStringPtr(cg->builder, nname, "ros2_name");
            LLVMTypeRef ft_ri = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
            LLVMBuildCall2(cg->builder, ft_ri, fn_ri, &ns, 1, "");
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            LLVMValueRef fn_rs = LLVMGetNamedFunction(cg->module, "kc_ros2_node_spin");
            if (!fn_rs) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
                fn_rs = LLVMAddFunction(cg->module, "kc_ros2_node_spin", ft);
                LLVMSetLinkage(fn_rs, LLVMExternalLinkage);
            }
            LLVMTypeRef ft_rs = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
            LLVMBuildCall2(cg->builder, ft_rs, fn_rs, NULL, 0, "");
            break;
        }

        /* ── 안전 규격 블록 (v17.0.0) ─────────────────────── */
        case NODE_WATCHDOG_BLOCK: {
            if (!block_is_open(cg)) break;
            LLVMTypeRef i64t = LLVMInt64TypeInContext(cg->ctx);
            long long tms = (long long)n->val.ival;
            LLVMValueRef fn_ws = LLVMGetNamedFunction(cg->module, "kc_watchdog_start");
            if (!fn_ws) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i64t, 1, 0);
                fn_ws = LLVMAddFunction(cg->module, "kc_watchdog_start", ft);
                LLVMSetLinkage(fn_ws, LLVMExternalLinkage);
            }
            LLVMValueRef ms_v = LLVMConstInt(i64t, (unsigned long long)tms, 0);
            LLVMTypeRef ft_ws = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i64t, 1, 0);
            LLVMBuildCall2(cg->builder, ft_ws, fn_ws, &ms_v, 1, "");
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            LLVMValueRef fn_wk = LLVMGetNamedFunction(cg->module, "kc_watchdog_kick");
            if (!fn_wk) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
                fn_wk = LLVMAddFunction(cg->module, "kc_watchdog_kick", ft);
                LLVMSetLinkage(fn_wk, LLVMExternalLinkage);
            }
            LLVMTypeRef ft_wk = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
            LLVMBuildCall2(cg->builder, ft_wk, fn_wk, NULL, 0, "");
            break;
        }
        case NODE_FAULT_TOL_BLOCK:
            if (!block_is_open(cg)) break;
            for (int ci = 0; ci < n->child_count; ci++) {
                if (!block_is_open(cg)) break;
                if (n->children[ci]) gen_block(cg, n->children[ci]);
            }
            break;

        /* ── 온디바이스 AI 블록 (v18.0.0) ─────────────────── */
        case NODE_AI_MODEL_BLOCK:
        case NODE_TINYML_BLOCK: {
            if (!block_is_open(cg)) break;
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            const char *mname = n->sval ? n->sval : "ai_model";
            const char *bfn = (n->type == NODE_TINYML_BLOCK) ? "kc_tinyml_begin" : "kc_ai_model_begin";
            const char *efn = (n->type == NODE_TINYML_BLOCK) ? "kc_tinyml_end"   : "kc_ai_model_end";
            LLVMValueRef fn_b = LLVMGetNamedFunction(cg->module, bfn);
            if (!fn_b) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
                fn_b = LLVMAddFunction(cg->module, bfn, ft);
                LLVMSetLinkage(fn_b, LLVMExternalLinkage);
            }
            LLVMValueRef ms = LLVMBuildGlobalStringPtr(cg->builder, mname, "ai_name");
            LLVMTypeRef ft_b = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
            LLVMBuildCall2(cg->builder, ft_b, fn_b, &ms, 1, "");
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            LLVMValueRef fn_e = LLVMGetNamedFunction(cg->module, efn);
            if (!fn_e) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
                fn_e = LLVMAddFunction(cg->module, efn, ft);
                LLVMSetLinkage(fn_e, LLVMExternalLinkage);
            }
            LLVMTypeRef ft_e = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
            LLVMBuildCall2(cg->builder, ft_e, fn_e, NULL, 0, "");
            break;
        }
        case NODE_FEDERATED_BLOCK: {
            if (!block_is_open(cg)) break;
            LLVMValueRef fn_fb = LLVMGetNamedFunction(cg->module, "kc_federated_begin");
            if (!fn_fb) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
                fn_fb = LLVMAddFunction(cg->module, "kc_federated_begin", ft);
                LLVMSetLinkage(fn_fb, LLVMExternalLinkage);
            }
            LLVMTypeRef ft_fb = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
            LLVMBuildCall2(cg->builder, ft_fb, fn_fb, NULL, 0, "");
            if (n->child_count > 0) gen_block(cg, n->children[0]);
            LLVMValueRef fn_fe = LLVMGetNamedFunction(cg->module, "kc_federated_end");
            if (!fn_fe) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
                fn_fe = LLVMAddFunction(cg->module, "kc_federated_end", ft);
                LLVMSetLinkage(fn_fe, LLVMExternalLinkage);
            }
            LLVMBuildCall2(cg->builder, ft_fb, fn_fe, NULL, 0, "");
            break;
        }
    }
}

static void gen_block(LLVMCodegen *cg, Node *n) {
    if (!n) return;
    if (n->type == NODE_BLOCK) {
        for (int i = 0; i < n->child_count && !cg->had_error; i++) {
            if (!block_is_open(cg)) break; /* unreachable code 방지 */
            gen_stmt(cg, n->children[i]);
        }
    } else {
        gen_stmt(cg, n);
    }
}

/* ================================================================
 *  최상위 — NODE_PROGRAM 처리
 * ================================================================ */
static void gen_program(LLVMCodegen *cg, Node *program) {
    /* 1단계: 함수 및 클래스 전방 선언 (순서 독립성) */
    for (int i = 0; i < program->child_count; i++) {
        Node *s = program->children[i];
        if (s->type == NODE_FUNC_DECL || s->type == NODE_VOID_DECL) {
            /* 이미 등록된 함수는 건너뜀 */
            if (!LLVMGetNamedFunction(cg->module, s->sval))
                gen_func_decl(cg, s);
        } else if (s->type == NODE_CLASS_DECL) {
            /* v6.2.0: 클래스 전방 선언 — vtable + struct + 메서드 IR */
            gen_class_decl(cg, s);
        }
    }

    /* 1-b 단계: 람다 함수 전방 선언 (v9.2.0) — AST 전체 재귀 탐색 */
    gen_collect_lambdas(cg, program);

    /* 2단계: main 함수 생성 — 최상위 비-함수 구문들을 main에 배치 */
    LLVMTypeRef  main_type = LLVMFunctionType(
        LLVMInt32TypeInContext(cg->ctx), NULL, 0, 0);
    LLVMValueRef main_fn   = LLVMAddFunction(cg->module, "main", main_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
                                  cg->ctx, main_fn, "entry");
    LLVMPositionBuilderAtEnd(cg->builder, entry);

    cg->cur_func          = main_fn;
    cg->cur_func_ret_type = LLVMInt32TypeInContext(cg->ctx);
    cg->cur_func_is_void  = 0;

    scope_push(cg);

    for (int i = 0; i < program->child_count; i++) {
        Node *s = program->children[i];
        if (s->type == NODE_FUNC_DECL || s->type == NODE_VOID_DECL) continue;
        if (s->type == NODE_CLASS_DECL) continue;  /* v6.2.0: 1단계에서 처리 완료 */
        if (!block_is_open(cg)) break;
        gen_stmt(cg, s);
    }

    if (block_is_open(cg))
        LLVMBuildRet(cg->builder,
                     LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0));

    scope_pop(cg);
}

/* ================================================================
 *  최적화 패스 적용 (mem2reg, instcombine, simplifycfg)
 * ================================================================ */
static void run_optimization(LLVMModuleRef module) {
    LLVMPassManagerRef pm = LLVMCreatePassManager();

    /* mem2reg — alloca → SSA 레지스터 (LLVM 최적화의 핵심) */
    LLVMPassManagerBuilderRef pmb = LLVMPassManagerBuilderCreate();
    LLVMPassManagerBuilderSetOptLevel(pmb, 2);    /* -O2 수준 */
    LLVMPassManagerBuilderPopulateModulePassManager(pmb, pm);
    LLVMPassManagerBuilderDispose(pmb);

    LLVMRunPassManager(pm, module);
    LLVMDisposePassManager(pm);
}

/* ================================================================
 *  공개 API 구현
 * ================================================================ */

LLVMCodegenResult *llvm_codegen_run(Node *program, const char *module_name)
{
    LLVMCodegenResult *result = calloc(1, sizeof(LLVMCodegenResult));

    LLVMCodegen cg = {0};
    cg.result  = result;
    cg.ir_line = 1;          /* IR 라인 카운터 초기화 */
    cg.ctx     = LLVMContextCreate();
    cg.module  = LLVMModuleCreateWithNameInContext(
                     module_name ? module_name : "kcode", cg.ctx);
    cg.builder = LLVMCreateBuilderInContext(cg.ctx);

    /* 전역 스코프 */
    LLVMScope global_scope = {0};
    cg.scope = &global_scope;

    /* 코드 생성 */
    gen_program(&cg, program);

    if (!cg.had_error) {
        /* IR 검증 */
        char *err = NULL;
        if (LLVMVerifyModule(cg.module, LLVMPrintMessageAction, &err)) {
            llvm_cg_error(&cg, 0, 0, "IR 검증 실패: %s", err ? err : "");
        }
        if (err) LLVMDisposeMessage(err);
    }

    if (!cg.had_error) {
        /* 최적화 전 IR 먼저 추출 — IDE 미리보기(전/후 비교)용 */
        char *ir_before = LLVMPrintModuleToString(cg.module);
        result->ir_text_unopt = strdup(ir_before);
        result->ir_unopt_len  = strlen(ir_before);
        LLVMDisposeMessage(ir_before);

        /* 최적화 패스 적용 */
        run_optimization(cg.module);

        /* 최적화 후 IR 추출 */
        char *ir = LLVMPrintModuleToString(cg.module);
        result->ir_text = strdup(ir);
        result->ir_len  = strlen(ir);

        /* IR 라인 수 계산 */
        int lines = 1;
        for (const char *p = ir; *p; p++) if (*p == '\n') lines++;
        result->ir_line_count = lines;

        LLVMDisposeMessage(ir);
    }

    /* 정리 */
    LLVMDisposeBuilder(cg.builder);
    LLVMDisposeModule(cg.module);
    LLVMContextDispose(cg.ctx);

    return result;
}

void llvm_codegen_result_free(LLVMCodegenResult *r) {
    if (!r) return;
    free(r->ir_text);
    free(r->ir_text_unopt);
    free(r);
}

int llvm_codegen_to_file(const LLVMCodegenResult *r, const char *path) {
    if (!r || !r->ir_text) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fwrite(r->ir_text, 1, r->ir_len, f);
    fclose(f);
    return 0;
}

int llvm_codegen_to_bitcode(LLVMModuleRef module, const char *path) {
    return LLVMWriteBitcodeToFile(module, path);
}

/* ================================================================
 *  JSON 문자열 이스케이프 출력
 *  kcodegen.c 의 json_escape 와 동일한 로직
 * ================================================================ */
static void json_escape(FILE *out, const char *s) {
    if (!s) return;
    for (; *s; s++) {
        switch (*s) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n",  out); break;
            case '\r': fputs("\\r",  out); break;
            case '\t': fputs("\\t",  out); break;
            default:
                if ((unsigned char)*s < 0x20)
                    fprintf(out, "\\u%04x", (unsigned char)*s);
                else
                    fputc(*s, out);
        }
    }
}

/* ================================================================
 *  llvm_codegen_to_json() — IDE 연동 JSON 출력
 *
 *  kcodegen_to_json() 과 동일한 프로토콜.
 *  IDE는 "ir_text" 키로 LLVM IR을 수신하고
 *  "sourcemap" 의 ir_line 으로 오류 위치를 .han 에 매핑한다.
 * ================================================================ */
void llvm_codegen_to_json(const LLVMCodegenResult *r, FILE *out) {
    fprintf(out, "{\n");
    fprintf(out, "  \"success\": %s,\n", r->had_error ? "false" : "true");

    /* ir_text (최적화 후) */
    fprintf(out, "  \"ir_text\": \"");
    json_escape(out, r->ir_text);
    fprintf(out, "\",\n");

    /* ir_text_unopt (최적화 전 — IDE 미리보기 전/후 비교용) */
    fprintf(out, "  \"ir_text_unopt\": \"");
    json_escape(out, r->ir_text_unopt ? r->ir_text_unopt : "");
    fprintf(out, "\",\n");

    /* sourcemap */
    fprintf(out, "  \"sourcemap\": [\n");
    for (int i = 0; i < r->sourcemap_count; i++) {
        const LLVMSourceMapEntry *e = &r->sourcemap[i];
        fprintf(out,
            "    {\"han_line\": %d, \"han_col\": %d, \"ir_line\": %d}%s\n",
            e->han_line, e->han_col, e->ir_line,
            (i < r->sourcemap_count - 1) ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* errors */
    fprintf(out, "  \"errors\": [\n");
    for (int i = 0; i < r->error_count; i++) {
        const LLVMCodegenError *e = &r->errors[i];
        fprintf(out, "    {\"line\": %d, \"col\": %d, \"msg\": \"",
                e->line, e->col);
        json_escape(out, e->msg);
        fprintf(out, "\"}%s\n", (i < r->error_count - 1) ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* stats */
    fprintf(out, "  \"stats\": {\n");
    fprintf(out, "    \"func_count\": %d,\n",    r->func_count);
    fprintf(out, "    \"var_count\": %d,\n",     r->var_count);
    fprintf(out, "    \"ir_line_count\": %d\n",  r->ir_line_count);
    fprintf(out, "  }\n");
    fprintf(out, "}\n");
}

#endif /* KCODE_LLVM */
