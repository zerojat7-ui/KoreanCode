/*
 * kcodegen_llvm_expr.c  —  LLVM IR 표현식(Expression) 생성
 * version : v13.0.0
 *
 * ★ 이 파일은 kcodegen_llvm.c(unity build)에 #include 되어 컴파일됩니다.
 *    단독 컴파일 대상이 아닙니다.
 *
 * 담당 역할:
 *   [인덱스·반복]
 *   - gen_index_expr()    : arr[idx] → build_array_get() GEP+load
 *   - gen_for_each()      : 각각(foreach) 루프 — 배열 순회 BasicBlock 구조
 *
 *   [기본 유틸]
 *   - block_is_open()     : 현재 BasicBlock 이 종료(br/ret)됐는지 확인
 *
 *   [리터럴 생성]
 *   - gen_int_lit()       : 정수 리터럴 → LLVMConstInt i64
 *   - gen_float_lit()     : 실수 리터럴 → LLVMConstReal double
 *   - gen_bool_lit()      : 논리 리터럴 → LLVMConstInt i1
 *   - gen_char_lit()      : 문자 리터럴 → LLVMConstInt i32
 *   - gen_string_lit()    : 글자(문자열) 리터럴 → 전역 상수 i8* 포인터
 *   - gen_null_lit()      : 없음 → null i8*
 *
 *   [식별자 / 연산]
 *   - gen_ident()         : 변수 조회 → scope_lookup + alloca load
 *   - gen_binary()        : 이진 연산 — 산술/비교/논리/비트 모두 처리
 *   - gen_unary()         : 단항 연산 (neg / not / ++ / --)
 *
 *   [함수 호출]
 *   - gen_call()          : 함수 호출 + 내장 함수 전체 처리
 *       포함 범위: 출력/출력없이/입력/길이/범위 /
 *                  수학(절댓값·제곱·삼각·수열·AI) /
 *                  글자 21종 / 통계 13종 /
 *                  AI 활성함수 3종 / 호감도 / 파일 17종 /
 *                  형변환(정수·실수·문자) /
 *                  사용자 정의 함수 (ExternalLinkage declare + call)
 *
 * 분리 이전: kcodegen_llvm.c 내 lines 1639~2792
 *
 * MIT License
 * zerojat7
 */

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

#undef TO_F64
#undef TO_I64
        (void)margs; (void)marg;
    } /* end 수학/AI 내장 함수 */

    /* ── 텐서 내장 함수 (v12.0.0) — KcTensor* = i8* (opaque ptr) ── */
    {
        LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
        LLVMTypeRef f64  = LLVMDoubleTypeInContext(cg->ctx);
        LLVMTypeRef i64  = LLVMInt64TypeInContext(cg->ctx);
        LLVMTypeRef vt   = LLVMVoidTypeInContext(cg->ctx);
        (void)vt;

        int argc_t = n->child_count - 1;
        LLVMValueRef arg0 = (argc_t >= 1) ? gen_expr(cg, n->children[1]) : LLVMConstNull(i8p);
        LLVMValueRef arg1 = (argc_t >= 2) ? gen_expr(cg, n->children[2]) : LLVMConstNull(i8p);

#define TENSOR_FN1_F64(kn_utf8, cfn) \
        if (strcmp(fn_name, kn_utf8) == 0) { \
            LLVMTypeRef ps[1] = {i8p}; \
            LLVMTypeRef ft = LLVMFunctionType(f64, ps, 1, 0); \
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, cfn); \
            if (!fn) { fn = LLVMAddFunction(cg->module, cfn, ft); LLVMSetLinkage(fn, LLVMExternalLinkage); } \
            LLVMValueRef ca[1] = {arg0}; \
            return LLVMBuildCall2(cg->builder, ft, fn, ca, 1, cfn); \
        }
#define TENSOR_FN1_I64(kn_utf8, cfn) \
        if (strcmp(fn_name, kn_utf8) == 0) { \
            LLVMTypeRef ps[1] = {i8p}; \
            LLVMTypeRef ft = LLVMFunctionType(i64, ps, 1, 0); \
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, cfn); \
            if (!fn) { fn = LLVMAddFunction(cg->module, cfn, ft); LLVMSetLinkage(fn, LLVMExternalLinkage); } \
            LLVMValueRef ca[1] = {arg0}; \
            return LLVMBuildCall2(cg->builder, ft, fn, ca, 1, cfn); \
        }
#define TENSOR_FN1_I8P(kn_utf8, cfn) \
        if (strcmp(fn_name, kn_utf8) == 0) { \
            LLVMTypeRef ps[1] = {i8p}; \
            LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 1, 0); \
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, cfn); \
            if (!fn) { fn = LLVMAddFunction(cg->module, cfn, ft); LLVMSetLinkage(fn, LLVMExternalLinkage); } \
            LLVMValueRef ca[1] = {arg0}; \
            return LLVMBuildCall2(cg->builder, ft, fn, ca, 1, cfn); \
        }
#define TENSOR_FN2_I8P(kn_utf8, cfn) \
        if (strcmp(fn_name, kn_utf8) == 0) { \
            LLVMTypeRef ps[2] = {i8p, i8p}; \
            LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 2, 0); \
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, cfn); \
            if (!fn) { fn = LLVMAddFunction(cg->module, cfn, ft); LLVMSetLinkage(fn, LLVMExternalLinkage); } \
            LLVMValueRef ca[2] = {arg0, arg1}; \
            return LLVMBuildCall2(cg->builder, ft, fn, ca, 2, cfn); \
        }

        /* 텐서합 / 텐서평균 / 텐서최대 / 텐서최소 → double */
        TENSOR_FN1_F64("\xED\x85\x90\xEC\x84\x9C\xED\x95\xA9",       "kc_tensor_sum")       /* 텐서합 */
        TENSOR_FN1_F64("\xED\x85\x90\xEC\x84\x9C\xED\x8F\x89\xEA\xB7\xA0", "kc_tensor_mean")  /* 텐서평균 */
        TENSOR_FN1_F64("\xED\x85\x90\xEC\x84\x9C\xEC\xB5\x9C\xEB\x8C\x80", "kc_tensor_max")   /* 텐서최대 */
        TENSOR_FN1_F64("\xED\x85\x90\xEC\x84\x9C\xEC\xB5\x9C\xEC\x86\x8C", "kc_tensor_min")   /* 텐서최소 */

        /* 텐서크기 → i64 */
        TENSOR_FN1_I64("\xED\x85\x90\xEC\x84\x9C\xED\x81\xAC\xEA\xB8\xB0", "kc_tensor_numel") /* 텐서크기 */
        TENSOR_FN1_I64("\xED\x85\x90\xEC\x84\x9C\xEC\xB0\xA8\xEC\x9B\x90", "kc_tensor_ndim")  /* 텐서차원 */

        /* 텐서형태 → i8* (char*) */
        TENSOR_FN1_I8P("\xED\x85\x90\xEC\x84\x9C\xED\x98\x95\xED\x83\x9C", "kc_tensor_shape_str") /* 텐서형태 */

        /* 단항 → KcTensor* (i8*) */
        TENSOR_FN1_I8P("\xED\x85\x90\xEC\x84\x9C\xEC\xA0\x84\xEC\xB9\x98",       "kc_tensor_transpose") /* 텐서전치 */
        TENSOR_FN1_I8P("\xED\x85\x90\xEC\x84\x9C\xED\x8F\x89\xED\x83\x84\xED\x99\x94", "kc_tensor_flatten")   /* 텐서평탄화 */
        TENSOR_FN1_I8P("\xED\x85\x90\xEC\x84\x9C\xEB\xB3\xB5\xEC\x82\xAC",       "kc_tensor_copy")       /* 텐서복사 */

        /* ── 자동미분 함수 2종 (v13.0.0) ── */
        /* 역전파(t) → void kc_autograd_backward(KcTensor*) */
        if (strcmp(name, "\xEC\x97\xAD\xEC\xA0\x84\xED\x8C\x8C") == 0) { /* 역전파 */
            LLVMTypeRef fn_ty = LLVMFunctionType(
                LLVMVoidTypeInContext(cg->ctx),
                (LLVMTypeRef[]){ LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0) }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_autograd_backward");
            if (!fn) fn = LLVMAddFunction(cg->module, "kc_autograd_backward", fn_ty);
            LLVMValueRef arg0 = (n->child_count > 1) ? gen_expr(cg, n->children[1]) : LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0));
            LLVMBuildCall2(cg->builder, fn_ty, fn, &arg0, 1, "");
            return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0));
        }
        /* 기울기초기화(t) → void kc_autograd_zero_grad(KcTensor*) */
        if (strcmp(name, "\xEA\xB8\xB0\xEC\x9A\xB8\xEA\xB8\xB0\xEC\xB4\x88\xEA\xB8\xB0\xED\x99\x94") == 0) { /* 기울기초기화 */
            LLVMTypeRef fn_ty = LLVMFunctionType(
                LLVMVoidTypeInContext(cg->ctx),
                (LLVMTypeRef[]){ LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0) }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_autograd_zero_grad");
            if (!fn) fn = LLVMAddFunction(cg->module, "kc_autograd_zero_grad", fn_ty);
            LLVMValueRef arg0 = (n->child_count > 1) ? gen_expr(cg, n->children[1]) : LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0));
            LLVMBuildCall2(cg->builder, fn_ty, fn, &arg0, 1, "");
            return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0));
        }

#undef TENSOR_FN1_F64
#undef TENSOR_FN1_I64
#undef TENSOR_FN1_I8P
#undef TENSOR_FN2_I8P
    } /* end 텐서 내장 함수 */

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

