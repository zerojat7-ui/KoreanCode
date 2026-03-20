/*
 * kcodegen_func.c  —  C 코드 생성기 함수/계약/프로그램 생성
 * version : v13.0.0
 *
 * ★ 이 파일은 kcodegen.c(unity build)에 #include 되어 컴파일됩니다.
 *    단독 컴파일 대상이 아닙니다.
 *
 * 담당 역할:
 *   - gen_block()           : 블록(들여쓰기) 코드 생성
 *   - gen_func_forward()    : 함수 전방 선언 출력
 *                             (법위반 대상은 _impl + 래퍼 전방선언 쌍)
 *   - gen_func_def()        : 함수 본체 C 코드 생성
 *                             (법위반 대상은 kc__fn_impl 로 생성)
 *   - gen_runtime_header()  : 런타임 헤더 자동 생성
 *                             (kc_runtime.h / stdint.h / assert.h /
 *                              signal.h / 행사 런타임 테이블 삽입)
 *   - gen_cond_to_buf()     : 조건식을 문자열 버퍼로 변환 (계약 메시지용)
 *   - gen_contract()        : 법령/법위반 계약 시스템 C 코드 변환
 *                             (사전조건 → assert / 사후조건 → 래퍼 위임)
 *   - emit_postcond_check() : 사후조건 검증 코드 1건 출력
 *   - emit_all_postconds()  : 해당 함수의 모든 법위반 조건 검증 출력
 *   - gen_postcond_wrapper(): 법위반 래퍼 함수 kc_fn() 생성 (v5.1.0)
 *   - gen_checkpoint()      : 복원지점 주석 삽입
 *   - detect_contracts()    : 계약 노드 존재 여부 1패스 선탐지
 *   - detect_postconds()    : 법위반 대상 함수명 수집
 *   - is_postcond_fn()      : 함수명이 법위반 대상인지 조회
 *   - gen_program()         : 최상위 프로그램 C 코드 생성
 *                             (전방선언 → 함수본체 → 래퍼 → main 진입점)
 *   - codegen_run()         : 공개 API — AST → C 코드 변환 진입점
 *   - codegen_result_free() : 결과 구조체 해제
 *   - codegen_to_json()     : IDE 연동 JSON 출력
 *   - codegen_compile()     : gcc 컴파일 실행
 *
 * 분리 이전: kcodegen.c 내 lines 2773~3608
 *
 * MIT License
 * zerojat7
 */

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
    /* ── 텐서 런타임 (v12.0.0) ── */
    emitln(cg, "/* ── KcTensor 런타임 (v12.0.0) ── */");
    emitln(cg, "#include <float.h>  /* DBL_MAX */");
    emitln(cg, "/* ── GradFn 전방선언 (v13.0.0 autograd) ── */");
    emitln(cg, "typedef struct GradFn GradFn;");
    emitln(cg, "typedef struct KcTensor {");
    emitln(cg, "    double  *data;");
    emitln(cg, "    int64_t *shape;");
    emitln(cg, "    int64_t *strides;");
    emitln(cg, "    int      ndim;");
    emitln(cg, "    int64_t  numel;");
    emitln(cg, "    int      ref_count;");
    emitln(cg, "    double  *grad;           /* v13.0.0 기울기 버퍼 */");
    emitln(cg, "    int      requires_grad;  /* v13.0.0 미분추적 플래그 */");
    emitln(cg, "    GradFn  *grad_fn;        /* v13.0.0 역전파 함수 노드 */");
    emitln(cg, "} KcTensor;");
    emitln(cg, "static KcTensor *kc_tensor_new_raw(const int64_t *shape, int ndim) {");
    emitln(cg, "    int64_t n=1; for(int i=0;i<ndim;i++) n*=shape[i];");
    emitln(cg, "    KcTensor *t=(KcTensor*)calloc(1,sizeof(KcTensor));");
    emitln(cg, "    if(!t) return NULL;");
    emitln(cg, "    t->ndim=ndim; t->numel=n; t->ref_count=1;");
    emitln(cg, "    if(ndim>0){t->shape=(int64_t*)malloc(sizeof(int64_t)*(size_t)ndim);");
    emitln(cg, "               t->strides=(int64_t*)malloc(sizeof(int64_t)*(size_t)ndim);");
    emitln(cg, "               memcpy(t->shape,shape,sizeof(int64_t)*(size_t)ndim);");
    emitln(cg, "               t->strides[ndim-1]=1;");
    emitln(cg, "               for(int i=ndim-2;i>=0;i--) t->strides[i]=t->strides[i+1]*shape[i+1];}");
    emitln(cg, "    t->data=(double*)calloc((size_t)n,sizeof(double));");
    emitln(cg, "    return t;");
    emitln(cg, "}");
    emitln(cg, "static void kc_tensor_free(KcTensor *t){if(!t)return;free(t->data);free(t->shape);free(t->strides);free(t->grad);/* grad_fn은 별도 관리 */free(t);}");
    emitln(cg, "/* ── GradFn 구조체 (v13.0.0) ── */");
    emitln(cg, "#define KC_GRADFN_MAX_SAVED 4");
    emitln(cg, "#define KC_GRADFN_MAX_NEXT  4");
    emitln(cg, "struct GradFn {");
    emitln(cg, "    void (*backward)(GradFn*,const double*,int64_t);");
    emitln(cg, "    KcTensor *saved[KC_GRADFN_MAX_SAVED];");
    emitln(cg, "    int n_saved;");
    emitln(cg, "    GradFn *next_fns[KC_GRADFN_MAX_NEXT];");
    emitln(cg, "    int n_next;");
    emitln(cg, "    KcTensor *input_tensors[KC_GRADFN_MAX_NEXT];");
    emitln(cg, "    int n_inputs;");
    emitln(cg, "    int visited;");
    emitln(cg, "};");
    emitln(cg, "static GradFn *kc_gradfn_new(void){return (GradFn*)calloc(1,sizeof(GradFn));}");
    emitln(cg, "static void kc_tensor_ensure_grad(KcTensor *t){");
    emitln(cg, "    if(!t||t->grad)return;");
    emitln(cg, "    t->grad=(double*)calloc((size_t)t->numel,sizeof(double));");
    emitln(cg, "}");
    emitln(cg, "static void kc_tensor_accum_grad(KcTensor *t,const double *d,int64_t n){");
    emitln(cg, "    if(!t||!t->requires_grad)return;");
    emitln(cg, "    kc_tensor_ensure_grad(t);");
    emitln(cg, "    for(int64_t i=0;i<n&&i<t->numel;i++) t->grad[i]+=d[i];");
    emitln(cg, "}");
    emitln(cg, "static void kc_backward_add(GradFn *s,const double *g,int64_t n){");
    emitln(cg, "    if(s->input_tensors[0]) kc_tensor_accum_grad(s->input_tensors[0],g,n);");
    emitln(cg, "    if(s->input_tensors[1]) kc_tensor_accum_grad(s->input_tensors[1],g,n);");
    emitln(cg, "}");
    emitln(cg, "static void kc_backward_mul(GradFn *s,const double *g,int64_t n){");
    emitln(cg, "    KcTensor *a=s->saved[0],*b=s->saved[1];");
    emitln(cg, "    if(s->input_tensors[0]&&b){double *da=(double*)malloc((size_t)n*sizeof(double));");
    emitln(cg, "        if(da){for(int64_t i=0;i<n;i++)da[i]=g[i]*b->data[i];kc_tensor_accum_grad(s->input_tensors[0],da,n);free(da);}}");
    emitln(cg, "    if(s->input_tensors[1]&&a){double *db=(double*)malloc((size_t)n*sizeof(double));");
    emitln(cg, "        if(db){for(int64_t i=0;i<n;i++)db[i]=g[i]*a->data[i];kc_tensor_accum_grad(s->input_tensors[1],db,n);free(db);}}");
    emitln(cg, "}");
    emitln(cg, "static void kc_backward_relu(GradFn *s,const double *g,int64_t n){");
    emitln(cg, "    KcTensor *a=s->saved[0]; if(!a||!s->input_tensors[0])return;");
    emitln(cg, "    double *da=(double*)malloc((size_t)n*sizeof(double)); if(!da)return;");
    emitln(cg, "    for(int64_t i=0;i<n;i++)da[i]=(a->data[i]>0.0)?g[i]:0.0;");
    emitln(cg, "    kc_tensor_accum_grad(s->input_tensors[0],da,n);free(da);");
    emitln(cg, "}");
    emitln(cg, "static void kc_backward_sigmoid(GradFn *s,const double *g,int64_t n){");
    emitln(cg, "    KcTensor *z=s->saved[0]; if(!z||!s->input_tensors[0])return;");
    emitln(cg, "    double *da=(double*)malloc((size_t)n*sizeof(double)); if(!da)return;");
    emitln(cg, "    for(int64_t i=0;i<n;i++){double sv=z->data[i];da[i]=g[i]*sv*(1.0-sv);}");
    emitln(cg, "    kc_tensor_accum_grad(s->input_tensors[0],da,n);free(da);");
    emitln(cg, "}");
    emitln(cg, "static void kc_backward_tanh(GradFn *s,const double *g,int64_t n){");
    emitln(cg, "    KcTensor *z=s->saved[0]; if(!z||!s->input_tensors[0])return;");
    emitln(cg, "    double *da=(double*)malloc((size_t)n*sizeof(double)); if(!da)return;");
    emitln(cg, "    for(int64_t i=0;i<n;i++){double t=z->data[i];da[i]=g[i]*(1.0-t*t);}");
    emitln(cg, "    kc_tensor_accum_grad(s->input_tensors[0],da,n);free(da);");
    emitln(cg, "}");
    emitln(cg, "static void kc_backward_log(GradFn *s,const double *g,int64_t n){");
    emitln(cg, "    KcTensor *a=s->saved[0]; if(!a||!s->input_tensors[0])return;");
    emitln(cg, "    double *da=(double*)malloc((size_t)n*sizeof(double)); if(!da)return;");
    emitln(cg, "    for(int64_t i=0;i<n;i++)da[i]=(a->data[i]!=0.0)?g[i]/a->data[i]:0.0;");
    emitln(cg, "    kc_tensor_accum_grad(s->input_tensors[0],da,n);free(da);");
    emitln(cg, "}");
    emitln(cg, "static void kc_backward_sum(GradFn *s,const double *g,int64_t n){");
    emitln(cg, "    KcTensor *ta=s->input_tensors[0]; if(!ta||!ta->requires_grad)return;");
    emitln(cg, "    double *da=(double*)malloc((size_t)ta->numel*sizeof(double)); if(!da)return;");
    emitln(cg, "    for(int64_t i=0;i<ta->numel;i++)da[i]=g[0];");
    emitln(cg, "    kc_tensor_accum_grad(ta,da,ta->numel);free(da);(void)n;");
    emitln(cg, "}");
    emitln(cg, "static void kc_backward_mean(GradFn *s,const double *g,int64_t n){");
    emitln(cg, "    KcTensor *ta=s->input_tensors[0]; if(!ta||!ta->requires_grad)return;");
    emitln(cg, "    double gv=g[0]/(double)ta->numel;");
    emitln(cg, "    double *da=(double*)malloc((size_t)ta->numel*sizeof(double)); if(!da)return;");
    emitln(cg, "    for(int64_t i=0;i<ta->numel;i++)da[i]=gv;");
    emitln(cg, "    kc_tensor_accum_grad(ta,da,ta->numel);free(da);(void)n;");
    emitln(cg, "}");
    emitln(cg, "static void kc_backward_pow2(GradFn *s,const double *g,int64_t n){");
    emitln(cg, "    KcTensor *a=s->saved[0]; if(!a||!s->input_tensors[0])return;");
    emitln(cg, "    double *da=(double*)malloc((size_t)n*sizeof(double)); if(!da)return;");
    emitln(cg, "    for(int64_t i=0;i<n;i++)da[i]=g[i]*2.0*a->data[i];");
    emitln(cg, "    kc_tensor_accum_grad(s->input_tensors[0],da,n);free(da);");
    emitln(cg, "}");
    /* autograd forward ops */
    emitln(cg, "static KcTensor *kc_tensor_add(const KcTensor *a,const KcTensor *b){");
    emitln(cg, "    if(!a||!b||a->numel!=b->numel)return NULL;");
    emitln(cg, "    KcTensor *r=kc_tensor_new_raw(a->shape,a->ndim); if(!r)return NULL;");
    emitln(cg, "    for(int64_t i=0;i<a->numel;i++)r->data[i]=a->data[i]+b->data[i]; return r;");
    emitln(cg, "}");
    emitln(cg, "static KcTensor *kc_tensor_mul(const KcTensor *a,const KcTensor *b){");
    emitln(cg, "    if(!a||!b||a->numel!=b->numel)return NULL;");
    emitln(cg, "    KcTensor *r=kc_tensor_new_raw(a->shape,a->ndim); if(!r)return NULL;");
    emitln(cg, "    for(int64_t i=0;i<a->numel;i++)r->data[i]=a->data[i]*b->data[i]; return r;");
    emitln(cg, "}");
    emitln(cg, "static void kc_autograd_reset_visited(GradFn *fn){");
    emitln(cg, "    if(!fn||fn->visited==0)return; fn->visited=0;");
    emitln(cg, "    for(int i=0;i<fn->n_next;i++)kc_autograd_reset_visited(fn->next_fns[i]);");
    emitln(cg, "}");
    emitln(cg, "static void kc_run_backward(GradFn *fn,KcTensor *out){");
    emitln(cg, "    if(!fn||fn->visited||!out||!out->grad)return;");
    emitln(cg, "    fn->visited=1;");
    emitln(cg, "    if(fn->backward)fn->backward(fn,out->grad,out->numel);");
    emitln(cg, "    for(int i=0;i<fn->n_next&&i<fn->n_inputs;i++){");
    emitln(cg, "        KcTensor *inp=fn->input_tensors[i];");
    emitln(cg, "        if(inp&&inp->grad_fn)kc_run_backward(inp->grad_fn,inp);}");
    emitln(cg, "}");
    emitln(cg, "static void kc_autograd_backward(KcTensor *root){");
    emitln(cg, "    if(!root)return;");
    emitln(cg, "    kc_tensor_ensure_grad(root);");
    emitln(cg, "    for(int64_t i=0;i<root->numel;i++)root->grad[i]=1.0;");
    emitln(cg, "    if(!root->grad_fn)return;");
    emitln(cg, "    kc_autograd_reset_visited(root->grad_fn);");
    emitln(cg, "    kc_run_backward(root->grad_fn,root);");
    emitln(cg, "}");
    emitln(cg, "static void kc_autograd_zero_grad(KcTensor *t){");
    emitln(cg, "    if(!t||!t->grad)return;");
    emitln(cg, "    memset(t->grad,0,(size_t)t->numel*sizeof(double));");
    emitln(cg, "}");
    emitln(cg, "static KcTensor *kc_tensor_copy(const KcTensor *t){");
    emitln(cg, "    if(!t)return NULL;");
    emitln(cg, "    KcTensor *r=kc_tensor_new_raw(t->shape,t->ndim);");
    emitln(cg, "    if(!r)return NULL;");
    emitln(cg, "    memcpy(r->data,t->data,sizeof(double)*(size_t)t->numel);");
    emitln(cg, "    return r;");
    emitln(cg, "}");
    emitln(cg, "static char *kc_tensor_shape_str(const KcTensor *t){");
    emitln(cg, "    if(!t)return strdup(\"[]\");");
    emitln(cg, "    char *buf=(char*)malloc(64*22+4); int pos=0;");
    emitln(cg, "    buf[pos++]='[';");
    emitln(cg, "    for(int i=0;i<t->ndim;i++){if(i>0){buf[pos++]=',';buf[pos++]=' ';}");
    emitln(cg, "        pos+=sprintf(buf+pos,\"%lld\",(long long)t->shape[i]);}");
    emitln(cg, "    buf[pos++]=']'; buf[pos]='\\0'; return buf;");
    emitln(cg, "}");
    emitln(cg, "static int64_t kc_tensor_numel(const KcTensor *t){return t?t->numel:0;}");
    emitln(cg, "static int kc_tensor_ndim(const KcTensor *t){return t?t->ndim:0;}");
    emitln(cg, "static double kc_tensor_sum(const KcTensor *t){double s=0;if(!t||!t->data)return 0;for(int64_t i=0;i<t->numel;i++)s+=t->data[i];return s;}");
    emitln(cg, "static double kc_tensor_mean(const KcTensor *t){return(!t||!t->numel)?0:kc_tensor_sum(t)/(double)t->numel;}");
    emitln(cg, "static double kc_tensor_max(const KcTensor *t){if(!t||!t->numel||!t->data)return -DBL_MAX;double m=t->data[0];for(int64_t i=1;i<t->numel;i++)if(t->data[i]>m)m=t->data[i];return m;}");
    emitln(cg, "static double kc_tensor_min(const KcTensor *t){if(!t||!t->numel||!t->data)return DBL_MAX;double m=t->data[0];for(int64_t i=1;i<t->numel;i++)if(t->data[i]<m)m=t->data[i];return m;}");
    emitln(cg, "static KcTensor *kc_tensor_matmul(const KcTensor *a,const KcTensor *b){");
    emitln(cg, "    if(!a||!b||a->ndim!=2||b->ndim!=2||a->shape[1]!=b->shape[0])return NULL;");
    emitln(cg, "    int64_t m=a->shape[0],k=a->shape[1],n=b->shape[1];");
    emitln(cg, "    int64_t sh[2]={m,n}; KcTensor *r=kc_tensor_new_raw(sh,2); if(!r)return NULL;");
    emitln(cg, "    for(int64_t i=0;i<m;i++) for(int64_t j=0;j<n;j++){");
    emitln(cg, "        double s=0; for(int64_t p=0;p<k;p++) s+=a->data[i*k+p]*b->data[p*n+j];");
    emitln(cg, "        r->data[i*n+j]=s;} return r;");
    emitln(cg, "}");
    emitln(cg, "static KcTensor *kc_tensor_transpose(const KcTensor *t){");
    emitln(cg, "    if(!t||t->ndim!=2)return t?(KcTensor*)t:NULL;");
    emitln(cg, "    int64_t sh[2]={t->shape[1],t->shape[0]};");
    emitln(cg, "    KcTensor *r=kc_tensor_new_raw(sh,2); if(!r)return NULL;");
    emitln(cg, "    for(int64_t i=0;i<t->shape[0];i++) for(int64_t j=0;j<t->shape[1];j++)");
    emitln(cg, "        r->data[j*t->shape[0]+i]=t->data[i*t->shape[1]+j];");
    emitln(cg, "    return r;");
    emitln(cg, "}");
    emitln(cg, "static KcTensor *kc_tensor_flatten(const KcTensor *t){");
    emitln(cg, "    if(!t)return NULL;");
    emitln(cg, "    int64_t sh=t->numel; KcTensor *r=kc_tensor_new_raw(&sh,1); if(!r)return NULL;");
    emitln(cg, "    memcpy(r->data,t->data,sizeof(double)*(size_t)t->numel); return r;");
    emitln(cg, "}");
    /* 텐서생성 헬퍼 — kc_array_t 배열에서 KcTensor 생성 */
    emitln(cg, "static KcTensor *kc_tensor_builtin_create(kc_array_t *data_arr, kc_array_t *shape_arr) {");
    emitln(cg, "    if(!data_arr||!shape_arr)return NULL;");
    emitln(cg, "    int ndim=(int)shape_arr->len;");
    emitln(cg, "    int64_t *shape=(int64_t*)malloc(sizeof(int64_t)*(ndim>0?(size_t)ndim:1));");
    emitln(cg, "    if(!shape)return NULL;");
    emitln(cg, "    kc_value_t *sitems=(kc_value_t*)shape_arr->data;");
    emitln(cg, "    for(int i=0;i<ndim;i++) shape[i]=(int64_t)sitems[i].v.ival;");
    emitln(cg, "    KcTensor *t=kc_tensor_new_raw(shape,ndim); free(shape); if(!t)return NULL;");
    emitln(cg, "    kc_value_t *items=(kc_value_t*)data_arr->data;");
    emitln(cg, "    int64_t n=data_arr->len<t->numel?data_arr->len:t->numel;");
    emitln(cg, "    for(int64_t i=0;i<n;i++) t->data[i]=(items[i].type==1)?items[i].v.fval:(double)items[i].v.ival;");
    emitln(cg, "    return t;");
    emitln(cg, "}");
    emitln(cg, "static KcTensor *kc_tensor_builtin_reshape(KcTensor *t, kc_array_t *shape_arr) {");
    emitln(cg, "    if(!t||!shape_arr)return t;");
    emitln(cg, "    int ndim=(int)shape_arr->len;");
    emitln(cg, "    int64_t *shape=(int64_t*)malloc(sizeof(int64_t)*(ndim>0?(size_t)ndim:1));");
    emitln(cg, "    if(!shape)return t;");
    emitln(cg, "    kc_value_t *sitems=(kc_value_t*)shape_arr->data;");
    emitln(cg, "    for(int i=0;i<ndim;i++) shape[i]=(int64_t)sitems[i].v.ival;");
    emitln(cg, "    int64_t nn=1; for(int i=0;i<ndim;i++) nn*=shape[i];");
    emitln(cg, "    if(nn!=t->numel){free(shape);return t;}");
    emitln(cg, "    KcTensor *r=kc_tensor_new_raw(shape,ndim); free(shape); if(!r)return t;");
    emitln(cg, "    memcpy(r->data,t->data,sizeof(double)*(size_t)t->numel); return r;");
    emitln(cg, "}");

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
