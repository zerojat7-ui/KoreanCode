/*
 * kcodegen_llvm_stmt.c  —  LLVM IR 구문(Statement)·함수·프로그램 생성
 * version : v29.0.0
 *
 * v20.3.0:
 *   - gen_stmt() 온톨로지 노드 6종 LLVM IR 생성 추가
 *     NODE_ONT_BLOCK  : kc_ont_create/destroy 외부함수 선언 + 호출 IR
 *     NODE_ONT_CONCEPT: kc_ont_add_class 외부함수 호출 IR
 *     NODE_ONT_PROP   : kc_ont_add_prop  외부함수 호출 IR
 *     NODE_ONT_RELATE : kc_ont_add_relation 외부함수 호출 IR
 *     NODE_ONT_QUERY  : kc_ont_query_kor + kc_ont_query_result_free IR
 *     NODE_ONT_INFER  : kc_ont_infer 호출 IR
 *
 * ★ 이 파일은 kcodegen_llvm.c(unity build)에 #include 되어 컴파일됩니다.
 *    단독 컴파일 대상이 아닙니다.
 *
 * 담당 역할:
 *   [표현식 디스패처]
 *   - gen_expr()            : 모든 표현식 노드 → LLVMValueRef 디스패처
 *
 *   [구문 생성]
 *   - gen_var_decl()        : 변수 선언 → alloca + store
 *   - gen_if()              : 만약/아니면 → BasicBlock 분기 구조
 *   - gen_while()           : 동안 루프 → cond/body/end BasicBlock
 *   - gen_for_range()       : 반복(범위) → 정수 카운터 루프 IR
 *   - gen_stmt()            : 모든 구문 노드 최상위 디스패처
 *       포함 범위:
 *         변수 선언 / 대입 / 이진·단항 연산문 /
 *         만약/동안/반복/각각 /
 *         선택(switch)/경우/그외 /
 *         멈춤(break)/건너뜀(continue)/돌려줌(return) /
 *         시도/실패시/항상 (setjmp/longjmp) /
 *         이동(goto)/레이블 /
 *         오류(raise) /
 *         스크립트 블록 (Python/Java/JS — system + fopen) /
 *         가속기 블록 (GPU/NPU/CPU) /
 *         인터럽트 3종 (신호/간섭/행사) /
 *         계약 시스템 (법령/법위반/헌법/법률/규정/복원지점) /
 *         #포함 / 가짐 (모듈 시스템) /
 *         함수 호출문 / 표현식문
 *   - gen_block()           : 블록(들여쓰기) 순차 구문 생성
 *
 *   [함수·클래스 생성]
 *   - gen_func_decl()       : 함수(정의) 선언 → LLVMFunction IR
 *   - gen_class_decl()      : 객체(CLASS) → vtable struct + _new() 팩토리
 *
 *   [프로그램 최상위 / 최적화 / 공개 API]
 *   - gen_program()         : 전체 프로그램 IR 생성
 *                             (람다 수집 → 클래스 → 함수 → main)
 *   - run_optimization()    : LLVM Pass Manager 최적화 실행
 *   - llvm_codegen_run()    : 공개 API — AST → LLVMCodegenResult
 *   - llvm_codegen_result_free() : 결과 구조체 해제
 *   - llvm_codegen_to_json(): IDE 연동 JSON 출력
 *
 * 분리 이전: kcodegen_llvm.c 내 lines 2793~5367
 *
 * MIT License
 * zerojat7
 */

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
        /* ── 가속기 블록 LLVM IR (v29.0.0) ─────────────────────────────
         *  Zero-Copy SoA 연산은 kinterp.c 런타임 레이어에서 처리.
        /* ── 가속기 블록 LLVM IR (v2.0.0) ─────────────────────────────
         *  kc_accel.h API를 LLVM IR로 호출.
         *  begin(1회 업로드) → exec(VRAM 연산) → end(1회 회수)
         *  외부 프로세스 의존성 없음.
         * ================================================================ */
        case NODE_GPU_BLOCK: {
            if (!block_is_open(cg)) break;
            sourcemap_add(cg, n->line, n->col);

            /* NODE_GPU_OP 정보 수집 */
            int body_idx = -1;
            const char *op_name = NULL;
            const char *accel_type = n->sval ? n->sval : "AUTO";
            for (int i = (int)n->child_count - 1; i >= 0; i--) {
                if (n->children[i] && n->children[i]->type == NODE_BLOCK) {
                    body_idx = i; break;
                }
            }
            for (int i = 0; i < (body_idx >= 0 ? body_idx : (int)n->child_count); i++) {
                Node *ch = n->children[i];
                if (ch && ch->type == NODE_GPU_OP) { op_name = ch->sval; break; }
            }

            LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i64  = LLVMInt64TypeInContext(cg->ctx);
            LLVMTypeRef f32p = LLVMPointerType(LLVMFloatTypeInContext(cg->ctx), 0);

            /* kc_accel_detect() → i32 */
            LLVMValueRef fn_detect = LLVMGetNamedFunction(cg->module, "kc_accel_detect");
            if (!fn_detect) {
                LLVMTypeRef ft = LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx), NULL, 0, 0);
                fn_detect = LLVMAddFunction(cg->module, "kc_accel_detect", ft);
                LLVMSetLinkage(fn_detect, LLVMExternalLinkage);
            }

            /* kc_accel_begin(i32, f32*, i64, f32*, i64) → i8* */
            LLVMValueRef fn_begin = LLVMGetNamedFunction(cg->module, "kc_accel_begin");
            if (!fn_begin) {
                LLVMTypeRef ps[] = { LLVMInt32TypeInContext(cg->ctx), f32p, i64, f32p, i64 };
                LLVMTypeRef ft = LLVMFunctionType(i8p, ps, 5, 0);
                fn_begin = LLVMAddFunction(cg->module, "kc_accel_begin", ft);
                LLVMSetLinkage(fn_begin, LLVMExternalLinkage);
            }

            /* kc_accel_exec(i8*, i8*, i64) → i32 */
            LLVMValueRef fn_exec = LLVMGetNamedFunction(cg->module, "kc_accel_exec");
            if (!fn_exec) {
                LLVMTypeRef ps[] = { i8p, i8p, i64 };
                LLVMTypeRef ft = LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx), ps, 3, 0);
                fn_exec = LLVMAddFunction(cg->module, "kc_accel_exec", ft);
                LLVMSetLinkage(fn_exec, LLVMExternalLinkage);
            }

            /* kc_accel_end(i8*, i64*) → f32* */
            LLVMValueRef fn_end = LLVMGetNamedFunction(cg->module, "kc_accel_end");
            if (!fn_end) {
                LLVMTypeRef i64p = LLVMPointerType(i64, 0);
                LLVMTypeRef ps[] = { i8p, i64p };
                LLVMTypeRef ft = LLVMFunctionType(f32p, ps, 2, 0);
                fn_end = LLVMAddFunction(cg->module, "kc_accel_end", ft);
                LLVMSetLinkage(fn_end, LLVMExternalLinkage);
            }

            /* 가속기 타입 결정 */
            LLVMTypeRef ft_detect = LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx), NULL, 0, 0);
            LLVMValueRef accel_val;
            if (!accel_type || strcmp(accel_type, "AUTO") == 0) {
                accel_val = LLVMBuildCall2(cg->builder, ft_detect, fn_detect, NULL, 0, "ac_type");
            } else {
                int ac_int = 3; /* CPU default */
                if (strcmp(accel_type,"TPU")==0) ac_int=0;
                else if (strcmp(accel_type,"NPU")==0) ac_int=1;
                else if (strcmp(accel_type,"GPU")==0) ac_int=2;
                accel_val = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), (unsigned)ac_int, 0);
            }

            /* begin 호출 */
            LLVMTypeRef ps_begin[] = { LLVMInt32TypeInContext(cg->ctx), f32p, i64, f32p, i64 };
            LLVMTypeRef ft_begin = LLVMFunctionType(i8p, ps_begin, 5, 0);
            LLVMValueRef args_begin[] = {
                accel_val,
                LLVMConstNull(f32p), LLVMConstInt(i64, 0, 0),
                LLVMConstNull(f32p), LLVMConstInt(i64, 0, 0)
            };
            LLVMValueRef ctx_val = LLVMBuildCall2(cg->builder, ft_begin, fn_begin,
                                                    args_begin, 5, "_accel_ctx");

            /* exec 호출 */
            if (op_name && block_is_open(cg)) {
                LLVMValueRef op_str = LLVMBuildGlobalStringPtr(cg->builder, op_name, "accel_op");
                LLVMTypeRef ps_exec[] = { i8p, i8p, i64 };
                LLVMTypeRef ft_exec = LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx), ps_exec, 3, 0);
                LLVMValueRef args_exec[] = { ctx_val, op_str, LLVMConstInt(i64, 0, 0) };
                LLVMBuildCall2(cg->builder, ft_exec, fn_exec, args_exec, 3, "");
            }

            /* end 호출 */
            if (block_is_open(cg)) {
                LLVMTypeRef i64p = LLVMPointerType(i64, 0);
                LLVMValueRef out_n_slot = LLVMBuildAlloca(cg->builder, i64, "out_n");
                LLVMBuildStore(cg->builder, LLVMConstInt(i64, 0, 0), out_n_slot);
                LLVMTypeRef ps_end[] = { i8p, i64p };
                LLVMTypeRef ft_end = LLVMFunctionType(f32p, ps_end, 2, 0);
                LLVMValueRef args_end[] = { ctx_val, out_n_slot };
                LLVMBuildCall2(cg->builder, ft_end, fn_end, args_end, 2, "_accel_result");
            }

            (void)accel_type;

            /* 일반 본문 블록 위임 */
            if (body_idx >= 0 && n->children[body_idx])
                llvm_gen_stmt(cg, n->children[body_idx]);

            break;
        }
        /* ── 가속기 블록 끝 (v2.0.0) ───────────────────────── */

        case NODE_MCP_SERVER:
            gen_mcp_server_ir(cg, n);
            break;
        case NODE_MCP_TOOL:
        case NODE_MCP_RESOURCE:
        case NODE_MCP_PROMPT:
            /* NODE_MCP_SERVER 내부에서 처리됨 */
            break;
            /* 표현식 구문으로 처리 시도 */
            gen_expr(cg, n);
            break;

        /* ================================================================
         *  온톨로지 블록 (v20.3.0)
         *  libkc_ontology.so 의 C 함수를 외부 선언(declare)하고
         *  LLVMBuildCall2 로 호출하는 IR 을 생성한다.
         *  — 모든 온톨로지 함수는 i8*(opaque ptr) 기반으로 단순화.
         * ================================================================ */

        /* ── NODE_ONT_BLOCK ─────────────────────────────────────────── */
        case NODE_ONT_BLOCK: {
            int  mode = n->val.ival;   /* 0=내장, 1=대여, 2=접속 */
            LLVMTypeRef  i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef  i32  = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef  voidT = LLVMVoidTypeInContext(cg->ctx);

            /* ── kc_ont_create / kc_ont_remote_connect 선언 ── */
            LLVMValueRef ont_handle = NULL;

            if (mode != 2) {
                /* i8* kc_ont_create(void) */
                LLVMTypeRef  ft_create = LLVMFunctionType(i8p, NULL, 0, 0);
                LLVMValueRef fn_create = LLVMGetNamedFunction(cg->module, "kc_ont_create");
                if (!fn_create)
                    fn_create = LLVMAddFunction(cg->module, "kc_ont_create", ft_create);
                ont_handle = LLVMBuildCall2(cg->builder, ft_create, fn_create,
                                            NULL, 0, "_ont");
            } else {
                /* i8* kc_ont_remote_connect(i8* url) */
                LLVMTypeRef params_conn[1] = { i8p };
                LLVMTypeRef ft_conn = LLVMFunctionType(i8p, params_conn, 1, 0);
                LLVMValueRef fn_conn = LLVMGetNamedFunction(cg->module, "kc_ont_remote_connect");
                if (!fn_conn)
                    fn_conn = LLVMAddFunction(cg->module, "kc_ont_remote_connect", ft_conn);
                const char *url = n->sval ? n->sval : "";
                LLVMValueRef url_v = LLVMBuildGlobalStringPtr(cg->builder, url, "_ont_url");
                LLVMValueRef args_conn[1] = { url_v };
                ont_handle = LLVMBuildCall2(cg->builder, ft_conn, fn_conn,
                                            args_conn, 1, "_ont_remote");
            }

            /* 온톨로지 핸들을 로컬 스택에 저장 (하위 노드에서 재사용) */
            LLVMValueRef ont_slot = LLVMBuildAlloca(cg->builder, i8p, "_ont_slot");
            LLVMBuildStore(cg->builder, ont_handle, ont_slot);

            /* 블록 내 하위 노드 처리 */
            /* NOTE: 하위 NODE_ONT_CONCEPT/PROP/RELATE/QUERY/INFER 는
             *       gen_stmt() 재귀로 처리하되, 핸들이 필요한 노드는
             *       ont_slot 에서 load 한다. 단순화를 위해 전역 임시 변수
             *       _kc_ont_cur 를 module 에 선언하여 핸들 공유. */
            LLVMValueRef g_ont = LLVMGetNamedGlobal(cg->module, "_kc_ont_cur");
            if (!g_ont) {
                g_ont = LLVMAddGlobal(cg->module, i8p, "_kc_ont_cur");
                LLVMSetInitializer(g_ont,
                    LLVMConstNull(i8p));
                LLVMSetLinkage(g_ont, LLVMInternalLinkage);
            }
            LLVMBuildStore(cg->builder, ont_handle, g_ont);

            for (int i = 0; i < n->child_count; i++)
                gen_stmt(cg, n->children[i]);

            /* ── kc_ont_destroy / kc_ont_remote_disconnect 선언 ── */
            if (mode != 2) {
                /* void kc_ont_destroy(i8*) */
                LLVMTypeRef params_d[1] = { i8p };
                LLVMTypeRef ft_d = LLVMFunctionType(voidT, params_d, 1, 0);
                LLVMValueRef fn_d = LLVMGetNamedFunction(cg->module, "kc_ont_destroy");
                if (!fn_d)
                    fn_d = LLVMAddFunction(cg->module, "kc_ont_destroy", ft_d);
                LLVMValueRef loaded = LLVMBuildLoad2(cg->builder, i8p, ont_slot, "");
                LLVMValueRef args_d[1] = { loaded };
                LLVMBuildCall2(cg->builder, ft_d, fn_d, args_d, 1, "");
            } else {
                /* void kc_ont_remote_disconnect(i8*) */
                LLVMTypeRef params_dc[1] = { i8p };
                LLVMTypeRef ft_dc = LLVMFunctionType(voidT, params_dc, 1, 0);
                LLVMValueRef fn_dc = LLVMGetNamedFunction(cg->module, "kc_ont_remote_disconnect");
                if (!fn_dc)
                    fn_dc = LLVMAddFunction(cg->module, "kc_ont_remote_disconnect", ft_dc);
                LLVMValueRef loaded2 = LLVMBuildLoad2(cg->builder, i8p, ont_slot, "");
                LLVMValueRef args_dc[1] = { loaded2 };
                LLVMBuildCall2(cg->builder, ft_dc, fn_dc, args_dc, 1, "");
            }
            /* _kc_ont_cur 를 null 로 초기화 */
            LLVMBuildStore(cg->builder, LLVMConstNull(i8p), g_ont);
            (void)i32; /* suppress unused warning */
            break;
        }

        /* ── NODE_ONT_CONCEPT ───────────────────────────────────────── */
        case NODE_ONT_CONCEPT: {
            /* int kc_ont_add_class(i8* ont, i8* name, i8* parent) */
            LLVMTypeRef i8p   = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32   = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef params[3] = { i8p, i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(i32, params, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_ont_add_class");
            if (!fn) fn = LLVMAddFunction(cg->module, "kc_ont_add_class", ft);

            LLVMValueRef g_ont = LLVMGetNamedGlobal(cg->module, "_kc_ont_cur");
            LLVMValueRef ont_v = g_ont
                ? LLVMBuildLoad2(cg->builder, i8p, g_ont, "")
                : LLVMConstNull(i8p);

            const char *cname  = n->sval ? n->sval : "_cls";
            const char *parent = (n->child_count > 0
                                  && n->children[0]
                                  && n->children[0]->type == NODE_IDENT
                                  && n->children[0]->sval)
                                 ? n->children[0]->sval : NULL;

            LLVMValueRef name_v   = LLVMBuildGlobalStringPtr(cg->builder, cname, "_cls_nm");
            LLVMValueRef parent_v = parent
                ? LLVMBuildGlobalStringPtr(cg->builder, parent, "_cls_par")
                : LLVMConstNull(i8p);

            LLVMValueRef args[3] = { ont_v, name_v, parent_v };
            LLVMBuildCall2(cg->builder, ft, fn, args, 3, "");

            /* 내부 속성 노드 생성 */
            for (int i = 0; i < n->child_count; i++) {
                if (n->children[i] && n->children[i]->type == NODE_ONT_PROP)
                    gen_stmt(cg, n->children[i]);
            }
            break;
        }

        /* ── NODE_ONT_PROP ──────────────────────────────────────────── */
        case NODE_ONT_PROP: {
            /* int kc_ont_add_prop(i8* ont, i8* cls, i8* prop, i32 type, i32 sens, i32 anon) */
            LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef params[6] = { i8p, i8p, i8p, i32, i32, i32 };
            LLVMTypeRef ft = LLVMFunctionType(i32, params, 6, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_ont_add_prop");
            if (!fn) fn = LLVMAddFunction(cg->module, "kc_ont_add_prop", ft);

            LLVMValueRef g_ont = LLVMGetNamedGlobal(cg->module, "_kc_ont_cur");
            LLVMValueRef ont_v = g_ont
                ? LLVMBuildLoad2(cg->builder, i8p, g_ont, "")
                : LLVMConstNull(i8p);

            const char *prop = n->sval ? n->sval : "_prop";
            int vtype     = n->val.ival;
            int sensitive = (n->op == TOK_KW_ONT_SENSITIVE) ? 1 : 0;
            int anon      = (n->op == TOK_KW_ONT_ANON)      ? 1 : 0;

            LLVMValueRef prop_v   = LLVMBuildGlobalStringPtr(cg->builder, prop, "_prop_nm");
            LLVMValueRef null_cls = LLVMConstNull(i8p); /* 클래스명은 상위 CONCEPT에서 관리 */
            LLVMValueRef args[6]  = {
                ont_v, null_cls, prop_v,
                LLVMConstInt(i32, (unsigned long long)vtype,     0),
                LLVMConstInt(i32, (unsigned long long)sensitive, 0),
                LLVMConstInt(i32, (unsigned long long)anon,      0)
            };
            LLVMBuildCall2(cg->builder, ft, fn, args, 6, "");
            break;
        }

        /* ── NODE_ONT_RELATE ────────────────────────────────────────── */
        case NODE_ONT_RELATE: {
            /* int kc_ont_add_relation(i8* ont, i8* name, i8* from, i8* to) */
            LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef params[4] = { i8p, i8p, i8p, i8p };
            LLVMTypeRef ft = LLVMFunctionType(i32, params, 4, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_ont_add_relation");
            if (!fn) fn = LLVMAddFunction(cg->module, "kc_ont_add_relation", ft);

            LLVMValueRef g_ont = LLVMGetNamedGlobal(cg->module, "_kc_ont_cur");
            LLVMValueRef ont_v = g_ont
                ? LLVMBuildLoad2(cg->builder, i8p, g_ont, "")
                : LLVMConstNull(i8p);

            const char *rel  = n->sval ? n->sval : "_rel";
            const char *from = (n->child_count > 0 && n->children[0] && n->children[0]->sval)
                               ? n->children[0]->sval : "";
            const char *to   = (n->child_count > 1 && n->children[1] && n->children[1]->sval)
                               ? n->children[1]->sval : "";

            LLVMValueRef rel_v  = LLVMBuildGlobalStringPtr(cg->builder, rel,  "_rel_nm");
            LLVMValueRef from_v = LLVMBuildGlobalStringPtr(cg->builder, from, "_rel_fr");
            LLVMValueRef to_v   = LLVMBuildGlobalStringPtr(cg->builder, to,   "_rel_to");
            LLVMValueRef args[4] = { ont_v, rel_v, from_v, to_v };
            LLVMBuildCall2(cg->builder, ft, fn, args, 4, "");
            break;
        }

        /* ── NODE_ONT_QUERY ─────────────────────────────────────────── */
        case NODE_ONT_QUERY: {
            /*
             * i8* kc_ont_query_kor(i8* ont, i8* query_str)
             * void kc_ont_query_result_free(i8* qr)
             */
            LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef voidT = LLVMVoidTypeInContext(cg->ctx);

            /* kc_ont_query_kor 선언 */
            LLVMTypeRef params_q[2] = { i8p, i8p };
            LLVMTypeRef ft_q = LLVMFunctionType(i8p, params_q, 2, 0);
            LLVMValueRef fn_q = LLVMGetNamedFunction(cg->module, "kc_ont_query_kor");
            if (!fn_q) fn_q = LLVMAddFunction(cg->module, "kc_ont_query_kor", ft_q);

            /* kc_ont_query_result_free 선언 */
            LLVMTypeRef params_f[1] = { i8p };
            LLVMTypeRef ft_f = LLVMFunctionType(voidT, params_f, 1, 0);
            LLVMValueRef fn_f = LLVMGetNamedFunction(cg->module, "kc_ont_query_result_free");
            if (!fn_f) fn_f = LLVMAddFunction(cg->module, "kc_ont_query_result_free", ft_f);

            LLVMValueRef g_ont = LLVMGetNamedGlobal(cg->module, "_kc_ont_cur");
            LLVMValueRef ont_v = g_ont
                ? LLVMBuildLoad2(cg->builder, i8p, g_ont, "")
                : LLVMConstNull(i8p);

            const char *qstr = n->sval ? n->sval : "";
            LLVMValueRef q_v = LLVMBuildGlobalStringPtr(cg->builder, qstr, "_ont_q");
            LLVMValueRef args_q[2] = { ont_v, q_v };
            LLVMValueRef qr = LLVMBuildCall2(cg->builder, ft_q, fn_q, args_q, 2, "_qr");

            /* 결과 해제 */
            LLVMValueRef args_f[1] = { qr };
            LLVMBuildCall2(cg->builder, ft_f, fn_f, args_f, 1, "");
            break;
        }

        /* ── NODE_ONT_INFER ─────────────────────────────────────────── */
        case NODE_ONT_INFER: {
            /* int kc_ont_infer(i8* ont) */
            LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef params[1] = { i8p };
            LLVMTypeRef ft = LLVMFunctionType(i32, params, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(cg->module, "kc_ont_infer");
            if (!fn) fn = LLVMAddFunction(cg->module, "kc_ont_infer", ft);

            LLVMValueRef g_ont = LLVMGetNamedGlobal(cg->module, "_kc_ont_cur");
            LLVMValueRef ont_v = g_ont
                ? LLVMBuildLoad2(cg->builder, i8p, g_ont, "")
                : LLVMConstNull(i8p);

            LLVMValueRef args[1] = { ont_v };
            LLVMBuildCall2(cg->builder, ft, fn, args, 1, "_inf");

            /* 추론 블록 내부 구문 처리 */
            for (int i = 0; i < n->child_count; i++)
                gen_stmt(cg, n->children[i]);
            break;
        }

        /* ── Concept Identity / Vector Space (v22.0.0) ────────────── */
        case NODE_SEMANTIC_INFER: {
            /* [의미추론] kc_ont_reason() LLVM IR 호출 */
            LLVMTypeRef i8p   = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32   = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef ft_r[1] = { i8p };
            LLVMTypeRef ft_reason = LLVMFunctionType(i32, ft_r, 1, 0);
            LLVMValueRef fn_reason = LLVMGetNamedFunction(cg->module, "kc_ont_reason");
            if (!fn_reason) fn_reason = LLVMAddFunction(cg->module, "kc_ont_reason", ft_reason);
            LLVMValueRef g_ont = LLVMGetNamedGlobal(cg->module, "_kc_ont_cur");
            LLVMValueRef ont_v = g_ont
                ? LLVMBuildLoad2(cg->builder, i8p, g_ont, "")
                : LLVMConstNull(i8p);
            LLVMValueRef rargs[1] = { ont_v };
            LLVMBuildCall2(cg->builder, ft_reason, fn_reason, rargs, 1, "_si");
            for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
            break;
        }

        case NODE_VECTORIZE: {
            /* [벡터화] kc_vec_embed_all() + kc_vec_embed_result_free() LLVM IR */
            LLVMTypeRef i8p   = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32   = LLVMInt32TypeInContext(cg->ctx);
            /* kc_vec_embed_all(i8*, i8*, i32, i32) -> i8* */
            LLVMTypeRef emb_pt[4] = { i8p, i8p, i32, i32 };
            LLVMTypeRef ft_emb = LLVMFunctionType(i8p, emb_pt, 4, 0);
            LLVMValueRef fn_emb = LLVMGetNamedFunction(cg->module, "kc_vec_embed_all");
            if (!fn_emb) fn_emb = LLVMAddFunction(cg->module, "kc_vec_embed_all", ft_emb);
            LLVMValueRef g_ont = LLVMGetNamedGlobal(cg->module, "_kc_ont_cur");
            LLVMValueRef ont_v = g_ont
                ? LLVMBuildLoad2(cg->builder, i8p, g_ont, "")
                : LLVMConstNull(i8p);
            LLVMValueRef emb_args[4] = { ont_v, LLVMConstNull(i8p),
                                         LLVMConstInt(i32, 0, 0), LLVMConstInt(i32, 0, 0) };
            LLVMValueRef emb_res = LLVMBuildCall2(cg->builder, ft_emb, fn_emb, emb_args, 4, "_emb");
            /* kc_vec_embed_result_free(i8*) -> void */
            LLVMTypeRef void_t = LLVMVoidTypeInContext(cg->ctx);
            LLVMTypeRef ft_free[1] = { i8p };
            LLVMTypeRef ft_efree = LLVMFunctionType(void_t, ft_free, 1, 0);
            LLVMValueRef fn_efree = LLVMGetNamedFunction(cg->module, "kc_vec_embed_result_free");
            if (!fn_efree) fn_efree = LLVMAddFunction(cg->module, "kc_vec_embed_result_free", ft_efree);
            LLVMValueRef efree_args[1] = { emb_res };
            LLVMBuildCall2(cg->builder, ft_efree, fn_efree, efree_args, 1, "");
            for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
            break;
        }

        case NODE_SEM_RECON: {
            /* [의미복원] kc_vec_recon_cluster_labels() LLVM IR */
            LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef dbl  = LLVMDoubleTypeInContext(cg->ctx);
            /* kc_vec_recon_cluster_labels(i8*, i8*, i32, i8*, i32*, i32, double) -> i32 */
            LLVMTypeRef rcl_pt[7] = { i8p, i8p, i32, i8p, i8p, i32, dbl };
            LLVMTypeRef ft_rcl = LLVMFunctionType(i32, rcl_pt, 7, 0);
            LLVMValueRef fn_rcl = LLVMGetNamedFunction(cg->module, "kc_vec_recon_cluster_labels");
            if (!fn_rcl) fn_rcl = LLVMAddFunction(cg->module, "kc_vec_recon_cluster_labels", ft_rcl);
            LLVMValueRef g_ont = LLVMGetNamedGlobal(cg->module, "_kc_ont_cur");
            LLVMValueRef ont_v = g_ont
                ? LLVMBuildLoad2(cg->builder, i8p, g_ont, "")
                : LLVMConstNull(i8p);
            LLVMValueRef rcl_args[7] = {
                ont_v, LLVMConstNull(i8p), LLVMConstInt(i32, 0, 0),
                LLVMConstNull(i8p), LLVMConstNull(i8p),
                LLVMConstInt(i32, 3, 0), LLVMConstReal(dbl, 0.5)
            };
            LLVMBuildCall2(cg->builder, ft_rcl, fn_rcl, rcl_args, 7, "_rcl");
            for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
            break;
        }

        case NODE_REPRO_LABEL: {
            /* [재생산라벨] kc_learn_default_config() + kc_learn_cycle_complete() LLVM IR */
            LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32  = LLVMInt32TypeInContext(cg->ctx);
            /* kc_learn_default_config() → i8* (opaque struct 포인터로 처리) */
            LLVMTypeRef ft_lcfg = LLVMFunctionType(i8p, NULL, 0, 0);
            LLVMValueRef fn_lcfg = LLVMGetNamedFunction(cg->module, "kc_learn_default_config");
            if (!fn_lcfg) fn_lcfg = LLVMAddFunction(cg->module, "kc_learn_default_config", ft_lcfg);
            LLVMValueRef lcfg_v = LLVMBuildCall2(cg->builder, ft_lcfg, fn_lcfg, NULL, 0, "_lcfg");
            /* kc_learn_cycle_complete(i8*, i8*, i8*, i8*) -> i8* (opaque result) */
            LLVMTypeRef lcc_pt[4] = { i8p, i8p, i8p, i8p };
            LLVMTypeRef ft_lcc = LLVMFunctionType(i8p, lcc_pt, 4, 0);
            LLVMValueRef fn_lcc = LLVMGetNamedFunction(cg->module, "kc_learn_cycle_complete");
            if (!fn_lcc) fn_lcc = LLVMAddFunction(cg->module, "kc_learn_cycle_complete", ft_lcc);
            LLVMValueRef g_ont = LLVMGetNamedGlobal(cg->module, "_kc_ont_cur");
            LLVMValueRef ont_v = g_ont
                ? LLVMBuildLoad2(cg->builder, i8p, g_ont, "")
                : LLVMConstNull(i8p);
            LLVMValueRef lcc_args[4] = { ont_v, LLVMConstNull(i8p), LLVMConstNull(i8p), lcfg_v };
            LLVMBuildCall2(cg->builder, ft_lcc, fn_lcc, lcc_args, 4, "_lcc");
            for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
            break;
        }

        /* ============================================================
         *  지식 뱅크 LLVM IR (v22.7.0)
         * ============================================================ */

        /* ── NODE_KBANK ─────────────────────────────────────────── */
        case NODE_KBANK: {
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);
            /* kc_kbank_create(i8*,i8*,i8*,i32,i32)->i8* */
            LLVMTypeRef kcr_pt[5] = { i8p, i8p, i8p, i32, i32 };
            LLVMTypeRef ft_kcr = LLVMFunctionType(i8p, kcr_pt, 5, 0);
            LLVMValueRef fn_kcr = LLVMGetNamedFunction(cg->module, "kc_kbank_create");
            if (!fn_kcr) fn_kcr = LLVMAddFunction(cg->module, "kc_kbank_create", ft_kcr);
            /* kc_kbank_destroy(i8*)->void */
            LLVMTypeRef ft_kd = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
            LLVMValueRef fn_kd = LLVMGetNamedFunction(cg->module, "kc_kbank_destroy");
            if (!fn_kd) fn_kd = LLVMAddFunction(cg->module, "kc_kbank_destroy", ft_kd);
            /* kc_kbank_gate1_check(i8*)->i32 */
            LLVMTypeRef ft_g1 = LLVMFunctionType(i32, &i8p, 1, 0);
            LLVMValueRef fn_g1 = LLVMGetNamedFunction(cg->module, "kc_kbank_gate1_check");
            if (!fn_g1) fn_g1 = LLVMAddFunction(cg->module, "kc_kbank_gate1_check", ft_g1);
            /* kc_kbank_gate2_scan(i8*,i8*,i32)->void */
            LLVMTypeRef g2_pt[3] = { i8p, i8p, i32 };
            LLVMTypeRef ft_g2 = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), g2_pt, 3, 0);
            LLVMValueRef fn_g2 = LLVMGetNamedFunction(cg->module, "kc_kbank_gate2_scan");
            if (!fn_g2) fn_g2 = LLVMAddFunction(cg->module, "kc_kbank_gate2_scan", ft_g2);
            /* kc_kbank_save(i8*,i8*,i8*)->i32 */
            LLVMTypeRef ks_pt[3] = { i8p, i8p, i8p };
            LLVMTypeRef ft_ks = LLVMFunctionType(i32, ks_pt, 3, 0);
            LLVMValueRef fn_ks = LLVMGetNamedFunction(cg->module, "kc_kbank_save");
            if (!fn_ks) fn_ks = LLVMAddFunction(cg->module, "kc_kbank_save", ft_ks);

            const char *kname = (n->sval && n->sval[0]) ? n->sval : "unnamed";
            char sav_path[256]; snprintf(sav_path, sizeof(sav_path), "%s.kbank", kname);
            LLVMValueRef str_name  = LLVMBuildGlobalStringPtr(cg->builder, kname,    "_kbname");
            LLVMValueRef str_sys   = LLVMBuildGlobalStringPtr(cg->builder, "system",  "_kbsys");
            LLVMValueRef str_uid   = LLVMBuildGlobalStringPtr(cg->builder, "UID-001", "_kbuid");
            LLVMValueRef str_spath = LLVMBuildGlobalStringPtr(cg->builder, sav_path,  "_kbsp");
            LLVMValueRef kcr_args[5] = { str_name, str_sys, str_uid,
                LLVMConstInt(i32,0,0), LLVMConstInt(i32,1,0) };
            LLVMValueRef kb_v = LLVMBuildCall2(cg->builder, ft_kcr, fn_kcr, kcr_args, 5, "_kb");
            /* 전역 포인터 저장 */
            LLVMValueRef g_kb = LLVMGetNamedGlobal(cg->module, "_kc_kb_cur");
            if (!g_kb) { g_kb = LLVMAddGlobal(cg->module, i8p, "_kc_kb_cur");
                         LLVMSetInitializer(g_kb, LLVMConstNull(i8p)); }
            LLVMBuildStore(cg->builder, kb_v, g_kb);
            /* 하위 노드 */
            for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
            /* gate1, gate2, save */
            LLVMBuildCall2(cg->builder, ft_g1, fn_g1, &kb_v, 1, "_g1");
            LLVMValueRef g2_args[3] = { kb_v, LLVMConstNull(i8p), LLVMConstInt(i32,0,0) };
            LLVMBuildCall2(cg->builder, ft_g2, fn_g2, g2_args, 3, "");
            LLVMValueRef ks_args[3] = { kb_v, str_spath, str_sys };
            LLVMBuildCall2(cg->builder, ft_ks, fn_ks, ks_args, 3, "_ks");
            LLVMBuildCall2(cg->builder, ft_kd, fn_kd, &kb_v, 1, "");
            break;
        }

        /* ── NODE_KBANK_LOAD ────────────────────────────────────── */
        case NODE_KBANK_LOAD: {
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef kl_pt[2] = { i8p, i8p };
            LLVMTypeRef ft_kl = LLVMFunctionType(i8p, kl_pt, 2, 0);
            LLVMValueRef fn_kl = LLVMGetNamedFunction(cg->module, "kc_kbank_load");
            if (!fn_kl) fn_kl = LLVMAddFunction(cg->module, "kc_kbank_load", ft_kl);
            LLVMTypeRef g3_pt[2] = { i8p, i8p };
            LLVMTypeRef ft_g3 = LLVMFunctionType(i32, g3_pt, 2, 0);
            LLVMValueRef fn_g3 = LLVMGetNamedFunction(cg->module, "kc_kbank_gate3_check");
            if (!fn_g3) fn_g3 = LLVMAddFunction(cg->module, "kc_kbank_gate3_check", ft_g3);
            const char *lpath = (n->sval && n->sval[0]) ? n->sval : "unknown.kbank";
            LLVMValueRef str_lp  = LLVMBuildGlobalStringPtr(cg->builder, lpath,    "_kblp");
            LLVMValueRef str_uid = LLVMBuildGlobalStringPtr(cg->builder, "UID-001", "_kbluid");
            LLVMValueRef kl_args[2] = { str_lp, str_uid };
            LLVMValueRef kbl_v = LLVMBuildCall2(cg->builder, ft_kl, fn_kl, kl_args, 2, "_kbl");
            LLVMValueRef g_kb = LLVMGetNamedGlobal(cg->module, "_kc_kb_cur");
            if (!g_kb) { g_kb = LLVMAddGlobal(cg->module, i8p, "_kc_kb_cur");
                         LLVMSetInitializer(g_kb, LLVMConstNull(i8p)); }
            LLVMBuildStore(cg->builder, kbl_v, g_kb);
            LLVMValueRef g3_args[2] = { kbl_v, str_lp };
            LLVMBuildCall2(cg->builder, ft_g3, fn_g3, g3_args, 2, "_g3");
            break;
        }

        /* ── NODE_KBANK_COMPARE ─────────────────────────────────── */
        case NODE_KBANK_COMPARE: {
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef kl2_pt[2] = { i8p, i8p };
            LLVMTypeRef ft_kl2 = LLVMFunctionType(i8p, kl2_pt, 2, 0);
            LLVMValueRef fn_kl2 = LLVMGetNamedFunction(cg->module, "kc_kbank_load");
            if (!fn_kl2) fn_kl2 = LLVMAddFunction(cg->module, "kc_kbank_load", ft_kl2);
            LLVMTypeRef mb_pt[6] = { i8p, i8p, i8p, i32, i8p, i8p };
            LLVMTypeRef ft_mb = LLVMFunctionType(i32, mb_pt, 6, 0);
            LLVMValueRef fn_mb = LLVMGetNamedFunction(cg->module, "kc_merge_banks");
            if (!fn_mb) fn_mb = LLVMAddFunction(cg->module, "kc_merge_banks", ft_mb);
            LLVMTypeRef ft_kbd = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &i8p, 1, 0);
            LLVMValueRef fn_kbd = LLVMGetNamedFunction(cg->module, "kc_kbank_destroy");
            if (!fn_kbd) fn_kbd = LLVMAddFunction(cg->module, "kc_kbank_destroy", ft_kbd);
            LLVMValueRef str_sys = LLVMBuildGlobalStringPtr(cg->builder, "system",       "_mbsys");
            LLVMValueRef str_tgt = LLVMBuildGlobalStringPtr(cg->builder, "target.kbank", "_mbtgt");
            LLVMValueRef kl_a[2] = { str_tgt, str_sys };
            LLVMValueRef src_v = LLVMBuildCall2(cg->builder, ft_kl2, fn_kl2, kl_a, 2, "_mbsrc");
            LLVMValueRef g_kb = LLVMGetNamedGlobal(cg->module, "_kc_kb_cur");
            LLVMValueRef dst_v = g_kb
                ? LLVMBuildLoad2(cg->builder, i8p, g_kb, "_mbdst") : LLVMConstNull(i8p);
            LLVMValueRef mb_args[6] = { dst_v, src_v, str_sys,
                LLVMConstInt(i32,0,0), LLVMConstNull(i8p), LLVMConstNull(i8p) };
            LLVMBuildCall2(cg->builder, ft_mb, fn_mb, mb_args, 6, "_mbr");
            LLVMBuildCall2(cg->builder, ft_kbd, fn_kbd, &src_v, 1, "");
            break;
        }

        /* ── NODE_REPRO_LABEL_DECL ──────────────────────────────── */
        case NODE_REPRO_LABEL_DECL: {
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef ld_pt[4] = { i8p, i8p, i8p, i32 };
            LLVMTypeRef ft_ld = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), ld_pt, 4, 0);
            LLVMValueRef fn_ld = LLVMGetNamedFunction(cg->module, "kc_kbank_label_declare");
            if (!fn_ld) fn_ld = LLVMAddFunction(cg->module, "kc_kbank_label_declare", ft_ld);
            const char *memo = (n->sval && n->sval[0]) ? n->sval : NULL;
            LLVMValueRef memo_v = memo
                ? LLVMBuildGlobalStringPtr(cg->builder, memo, "_rld_memo")
                : LLVMConstNull(i8p);
            LLVMValueRef g_kb = LLVMGetNamedGlobal(cg->module, "_kc_kb_cur");
            LLVMValueRef kb_v = g_kb
                ? LLVMBuildLoad2(cg->builder, i8p, g_kb, "_rld_kb") : LLVMConstNull(i8p);
            LLVMValueRef ld_args[4] = { kb_v, memo_v, LLVMConstNull(i8p), LLVMConstInt(i32,0,0) };
            LLVMBuildCall2(cg->builder, ft_ld, fn_ld, ld_args, 4, "");
            break;
        }

        /* ── NODE_KBANK_PROOF ───────────────────────────────────── */
        case NODE_KBANK_PROOF: {
            LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(cg->ctx);
            LLVMTypeRef pe_pt[4] = { i8p, i8p, i8p, i8p };
            LLVMTypeRef ft_pe = LLVMFunctionType(i32, pe_pt, 4, 0);
            LLVMValueRef fn_pe = LLVMGetNamedFunction(cg->module, "kc_proof_export");
            if (!fn_pe) fn_pe = LLVMAddFunction(cg->module, "kc_proof_export", ft_pe);
            const char *bpath = (n->sval && n->sval[0]) ? n->sval : "_proof";
            LLVMValueRef str_bp  = LLVMBuildGlobalStringPtr(cg->builder, bpath,    "_pebp");
            LLVMValueRef str_sys = LLVMBuildGlobalStringPtr(cg->builder, "system", "_pesys");
            LLVMValueRef g_kb = LLVMGetNamedGlobal(cg->module, "_kc_kb_cur");
            LLVMValueRef kb_v = g_kb
                ? LLVMBuildLoad2(cg->builder, i8p, g_kb, "_pe_kb") : LLVMConstNull(i8p);
            LLVMValueRef pe_args[4] = { kb_v, str_sys, LLVMConstNull(i8p), str_bp };
            LLVMBuildCall2(cg->builder, ft_pe, fn_pe, pe_args, 4, "_pe");
            for (int i = 0; i < n->child_count; i++) gen_stmt(cg, n->children[i]);
            break;
        }

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
