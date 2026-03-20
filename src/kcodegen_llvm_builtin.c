/*
 * kcodegen_llvm_builtin.c  —  LLVM IR 특수 노드 생성 (내장·계약·인터럽트)
 * version : v14.0.0
 *
 * ★ 이 파일은 kcodegen_llvm.c(unity build)에 #include 되어 컴파일됩니다.
 *    단독 컴파일 대상이 아닙니다.
 *
 * 담당 역할:
 *   [배열/컨테이너 리터럴]
 *   - gen_array_lit()         : [v0, v1, ...] → build_array_new + push 반복
 *   - gen_dict_lit()          : {k:v, ...} → kc_dict_new + kc_dict_set 반복
 *
 *   [멤버 접근 / 람다]
 *   - gen_member_expr()       : obj.field / arr.길이 → GEP 로드
 *   - gen_lambda()            : 람다 → 별도 kc_lambda_N() LLVM 함수 생성
 *   - gen_collect_lambdas()   : 1패스 AST 재귀 탐색 — 람다 사전 등록
 *
 *   [모듈 시스템 (#포함/가짐)]
 *   - gen_kc_include_call()   : #포함 → kc_include(i8*) 외부 함수 call
 *   - gen_kc_module_load_call(): 가짐 외부 모듈 → kc_module_load(i8*) call
 *   - gen_import_builtin_decls(): 내장 모듈(수학/파일/시간 등) 함수 declare 삽입
 *
 *   [계약 시스템]
 *   - sanction_to_int()       : 제재 토큰 → 정수 상수 변환
 *   - gen_contract_call2()    : 계약 외부 함수 call 공통 헬퍼
 *   - gen_contract_ir()       : 법령/법위반/헌법/법률/규정/복원지점 IR 생성
 *
 *   [인터럽트 시스템 3종]
 *   - sig_token_to_posix()    : 신호 이름 → POSIX 시그널 번호
 *   - gen_signal_register()   : signal() 등록 call
 *   - gen_signal_handler_ir() : 신호받기 핸들러 함수 + signal() 등록
 *   - gen_isr_handler_ir()    : 간섭 ISR 핸들러 함수 + kc_isr_register() 등록
 *   - gen_event_handler_ir()  : 행사 핸들러 함수 + kc_event_register() 등록
 *
 * 분리 이전: kcodegen_llvm.c 내 lines 760~1638
 *
 * MIT License
 * zerojat7
 */

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

/* ================================================================
 *  MCP 시스템 LLVM IR 생성 (v14.0.0)
 *
 *  NODE_MCP_SERVER 실행 시:
 *    kc_mcp_server_init / add_tool / add_resource / add_prompt /
 *    kc_mcp_server_run 을 extern 선언 후 call IR 생성
 * ================================================================ */
static void gen_mcp_server_ir(LLVMCodegen *cg, Node *n) {
    if (!block_is_open(cg)) return;

    LLVMTypeRef  i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
    LLVMTypeRef  void_t = LLVMVoidTypeInContext(cg->ctx);
    LLVMTypeRef  i32  = LLVMInt32TypeInContext(cg->ctx);

    /* KcMcpServer 구조체 스택 할당 — opaque i8 배열로 표현 */
    /* sizeof(KcMcpServer) 를 정확히 알기 어려우므로 4096 바이트 할당 */
    LLVMTypeRef  srv_ty  = LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), 4096);
    LLVMValueRef srv_ptr = LLVMBuildAlloca(cg->builder, srv_ty, "kc_mcp_srv");

    /* kc_mcp_server_init(srv*, name*, version*, desc*) */
    LLVMValueRef fn_init = LLVMGetNamedFunction(cg->module, "kc_mcp_server_init");
    if (!fn_init) {
        LLVMTypeRef p4[4] = { i8p, i8p, i8p, i8p };
        LLVMTypeRef ft = LLVMFunctionType(void_t, p4, 4, 0);
        fn_init = LLVMAddFunction(cg->module, "kc_mcp_server_init", ft);
        LLVMSetLinkage(fn_init, LLVMExternalLinkage);
    }
    LLVMValueRef srv_i8  = LLVMBuildBitCast(cg->builder, srv_ptr, i8p, "srv_i8");
    const char *sname = n->sval ? n->sval : "kcode-mcp";
    LLVMValueRef name_v = LLVMBuildGlobalStringPtr(cg->builder, sname, "mcp_name");
    LLVMValueRef ver_v  = LLVMBuildGlobalStringPtr(cg->builder, "1.0.0", "mcp_ver");
    LLVMValueRef desc_v = LLVMBuildGlobalStringPtr(cg->builder, "", "mcp_desc");
    LLVMTypeRef init_p[4] = { i8p, i8p, i8p, i8p };
    LLVMTypeRef init_ft = LLVMFunctionType(void_t, init_p, 4, 0);
    LLVMValueRef init_args[4] = { srv_i8, name_v, ver_v, desc_v };
    LLVMBuildCall2(cg->builder, init_ft, fn_init, init_args, 4, "");

    /* kc_mcp_server_run(srv*) */
    LLVMValueRef fn_run = LLVMGetNamedFunction(cg->module, "kc_mcp_server_run");
    if (!fn_run) {
        LLVMTypeRef p1[1] = { i8p };
        LLVMTypeRef ft = LLVMFunctionType(void_t, p1, 1, 0);
        fn_run = LLVMAddFunction(cg->module, "kc_mcp_server_run", ft);
        LLVMSetLinkage(fn_run, LLVMExternalLinkage);
    }
    LLVMTypeRef run_p[1] = { i8p };
    LLVMTypeRef run_ft = LLVMFunctionType(void_t, run_p, 1, 0);
    LLVMValueRef run_args[1] = { srv_i8 };
    LLVMBuildCall2(cg->builder, run_ft, fn_run, run_args, 1, "");

    (void)i32; /* 미사용 경고 방지 */
}
