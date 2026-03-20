/*
 * kcodegen_llvm_util.c  —  LLVM IR 코드 생성기 유틸리티 & 저수준 헬퍼
 * version : v13.0.0
 *
 * ★ 이 파일은 kcodegen_llvm.c(unity build)에 #include 되어 컴파일됩니다.
 *    단독 컴파일 대상이 아닙니다.
 *
 * 담당 역할:
 *   [오류/디버그]
 *   - llvm_cg_error()       : 오류 누적 (LLVMCodegenResult.errors)
 *   - sourcemap_add()       : .han 라인 ↔ IR 라인 매핑
 *
 *   [스코프/심볼 테이블]
 *   - scope_push/pop()      : 중첩 블록 스코프 관리 (push 시 새 배열 할당)
 *   - scope_set()           : 변수명 → LLVMValueRef 등록
 *   - scope_lookup()        : 변수명 조회 (안쪽 스코프 우선)
 *
 *   [레이블/루프 컨텍스트]
 *   - label_map_get_or_create() : goto 대상 BasicBlock lazy 등록
 *   - loop_push/pop/top()   : break/continue 대상 블록 스택
 *
 *   [타입 헬퍼]
 *   - ktype_to_llvm()       : 한글 자료형 토큰 → LLVMTypeRef
 *
 *   [외부 함수 선언 캐시 (get_* 시리즈)]
 *   - get_printf()          : printf (i8*, ... → i32)
 *   - make_global_str()     : 전역 문자열 상수 생성
 *   - get_kc_array_type()   : KcArray 구조체 타입
 *   - get_malloc/realloc/free() : 메모리 함수
 *   - get_system/setenv/remove() : 프로세스 함수
 *   - get_snprintf/fopen/fputs/fclose/fread/getpid() : 파일·I/O 함수
 *
 *   [배열 런타임 빌더]
 *   - build_array_new()     : 빈 배열 생성 (KcArray malloc)
 *   - build_array_push()    : 배열 원소 추가 (realloc + store)
 *   - build_array_len()     : 배열 길이 조회 (GEP len 필드)
 *   - build_array_get()     : 배열 원소 읽기 (GEP + load)
 *   - build_array_set()     : 배열 원소 쓰기 (GEP + store)
 *
 * 분리 이전: kcodegen_llvm.c 내 lines 219~759
 *
 * MIT License
 * zerojat7
 */

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
        case TOK_KW_HAENGNYEOL:
        case TOK_KW_TENSOR:     return LLVMPointerType(                  /* 텐서/행렬 → KcTensor* (i8*) */
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
