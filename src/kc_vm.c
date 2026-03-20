/*
 * kc_vm.c  —  Kcode KVM 스택 가상머신 실행 엔진
 * version : v16.0.2
 *
 * v16.0.2 변경:
 *   - kc_tensor.h / kc_autograd.h 연동
 *   - VAL_TENSOR 타입 지원 (kc_val_tensor / kc_val_free / kc_val_print)
 *   - 텐서 13종 run_builtin 완전 구현 (stub 제거)
 *   - 자동미분 2종 run_builtin 완전 구현 (stub 제거)
 *
 * v16.0.0 변경:
 *   - run_builtin() 102종 전체 구현
 *   - BUILTIN_BOOL / BUILTIN_LN 추가
 *   - BUILTIN_FLOAT / BUILTIN_STR / BUILTIN_STDEV 기존 누락 case 복원
 *   - 글자 14종 신규 구현 (자르기/분할/합치기/반복글자/역순/위치/시작/끝확인/비교/제목식/한번대체/앞공백제거/뒤공백제거/반복확인/분석/포맷)
 *   - 통계 9종 신규 구현 (합계/분산/중앙값/최빈값/누적합/공분산/상관계수/정규화/표준화)
 *   - AI 7종 신규 구현 (MSE/교차엔트로피/소프트맥스/위치인코딩/등비수열합/등차수열합/점화식값)
 *   - 호감도 / 파일 17종 / 텐서 13종 / 역전파 / 기울기초기화 / MCP오류 stub 구현
 *
 * MIT License
 * zerojat7
 */

#include "kc_vm.h"
#include "kc_tensor.h"
#include "kc_autograd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <regex.h>       /* 반복확인 — POSIX regex */
#include <sys/stat.h>    /* 파일크기/폴더만들기    */
#include <dirent.h>      /* 파일목록               */
#include <libgen.h>      /* basename/dirname       */

/* ================================================================
 *  내부 매크로
 * ================================================================ */
#define VM_ERROR(vm, ...) do { \
    snprintf((vm)->error_msg, sizeof((vm)->error_msg), __VA_ARGS__); \
    (vm)->had_error = 1; \
} while(0)

#define PUSH(vm, v)  ((vm)->stack[(vm)->stack_top++] = (v))
#define POP(vm)      ((vm)->stack[--(vm)->stack_top])
#define PEEK(vm, n)  ((vm)->stack[(vm)->stack_top - 1 - (n)])

/* ================================================================
 *  메모리 유틸
 * ================================================================ */
static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "[KVM] 메모리 부족\n"); exit(1); }
    return p;
}

static char *xstrdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = (char *)xmalloc(len);
    memcpy(d, s, len);
    return d;
}

/* ================================================================
 *  KcVal 생성
 * ================================================================ */
KcVal kc_val_int(int64_t v)  { KcVal r; r.type=VAL_INT;   r.u.ival=v; return r; }
KcVal kc_val_float(double v) { KcVal r; r.type=VAL_FLOAT; r.u.fval=v; return r; }
KcVal kc_val_bool(int v)     { KcVal r; r.type=VAL_BOOL;  r.u.bval=v?1:0; return r; }
KcVal kc_val_null(void)      { KcVal r; r.type=VAL_NULL;  r.u.ival=0; return r; }

KcVal kc_val_str(const char *s)
{
    KcVal r;
    r.type   = VAL_STR;
    r.u.sval = xstrdup(s ? s : "");
    return r;
}

/* 텐서 KcVal 래핑 -- 소유권 이전 */
KcVal kc_val_tensor(KcTensor *t)
{
    KcVal r;
    r.type     = VAL_TENSOR;
    r.u.tensor = t;
    return r;
}

void kc_val_free(KcVal *v)
{
    if (!v) return;
    switch (v->type) {
        case VAL_STR:
            free(v->u.sval);
            v->u.sval = NULL;
            break;
        case VAL_ARRAY:
            if (v->u.arr) {
                v->u.arr->ref_count--;
                if (v->u.arr->ref_count <= 0) {
                    for (int i = 0; i < v->u.arr->len; i++)
                        kc_val_free(&v->u.arr->items[i]);
                    free(v->u.arr->items);
                    free(v->u.arr);
                }
                v->u.arr = NULL;
            }
            break;
        case VAL_DICT:
            if (v->u.dict) {
                v->u.dict->ref_count--;
                if (v->u.dict->ref_count <= 0) {
                    for (int i = 0; i < v->u.dict->len; i++) {
                        free(v->u.dict->entries[i].key);
                        kc_val_free(&v->u.dict->entries[i].val);
                    }
                    free(v->u.dict->entries);
                    free(v->u.dict);
                }
                v->u.dict = NULL;
            }
            break;
        case VAL_TENSOR:
            if (v->u.tensor) {
                v->u.tensor->ref_count--;
                if (v->u.tensor->ref_count <= 0)
                    kc_tensor_free(v->u.tensor);
                v->u.tensor = NULL;
            }
            break;
        default:
            break;
    }
    v->type = VAL_NULL;
}

/* ================================================================
 *  KcVal 출력
 * ================================================================ */

/* WASM 빌드에서 출력을 버퍼로 캡처하기 위한 매크로 */
#ifdef KCODE_WASM_BUILD
#  include "kc_wasm_api.h"
#  define KC_PRINTF(fmt, ...)  do { \
        char _buf[1024]; \
        snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
        kc_wasm_output_append(_buf); \
    } while(0)
#else
#  define KC_PRINTF(fmt, ...)  printf(fmt, ##__VA_ARGS__)
#endif

void kc_val_print(const KcVal *v)
{
    switch (v->type) {
        case VAL_INT:   KC_PRINTF("%lld",  (long long)v->u.ival); break;
        case VAL_FLOAT: KC_PRINTF("%g",    v->u.fval); break;
        case VAL_STR:   KC_PRINTF("%s",    v->u.sval ? v->u.sval : ""); break;
        case VAL_BOOL:  KC_PRINTF("%s",    v->u.bval ? "참" : "거짓"); break;
        case VAL_NULL:  KC_PRINTF("없음"); break;
        case VAL_ARRAY: {
            KC_PRINTF("[");
            if (v->u.arr) {
                for (int i = 0; i < v->u.arr->len; i++) {
                    if (i) KC_PRINTF(", ");
                    kc_val_print(&v->u.arr->items[i]);
                }
            }
            KC_PRINTF("]");
            break;
        }
        case VAL_DICT: {
            KC_PRINTF("{");
            if (v->u.dict) {
                for (int i = 0; i < v->u.dict->len; i++) {
                    if (i) KC_PRINTF(", ");
                    KC_PRINTF("\"%s\": ", v->u.dict->entries[i].key);
                    kc_val_print(&v->u.dict->entries[i].val);
                }
            }
            KC_PRINTF("}");
            break;
        }
        case VAL_FUNC:    KC_PRINTF("<함수:%s>", v->u.func ? v->u.func->name : "?"); break;
        case VAL_CLOSURE: KC_PRINTF("<클로저>"); break;
        case VAL_TENSOR:
            if (v->u.tensor) kc_tensor_print(v->u.tensor);
            else KC_PRINTF("텐서(없음)");
            break;
        default:          KC_PRINTF("<알수없음>"); break;
    }
}

int kc_val_truthy(const KcVal *v)
{
    switch (v->type) {
        case VAL_BOOL:  return v->u.bval;
        case VAL_INT:   return v->u.ival != 0;
        case VAL_FLOAT: return v->u.fval != 0.0;
        case VAL_STR:   return v->u.sval && v->u.sval[0] != '\0';
        case VAL_NULL:  return 0;
        case VAL_ARRAY: return v->u.arr && v->u.arr->len > 0;
        default:        return 1;
    }
}

/* ================================================================
 *  값 연산
 * ================================================================ */
KcVal kc_val_add(KcVM *vm, KcVal a, KcVal b)
{
    /* 문자열 연결 */
    if (a.type == VAL_STR || b.type == VAL_STR) {
        char buf_a[256] = {0}, buf_b[256] = {0};
        if (a.type == VAL_STR) strncpy(buf_a, a.u.sval, 255);
        else if (a.type == VAL_INT) snprintf(buf_a, 255, "%lld", (long long)a.u.ival);
        else snprintf(buf_a, 255, "%g", a.u.fval);
        if (b.type == VAL_STR) strncpy(buf_b, b.u.sval, 255);
        else if (b.type == VAL_INT) snprintf(buf_b, 255, "%lld", (long long)b.u.ival);
        else snprintf(buf_b, 255, "%g", b.u.fval);
        char *result = (char *)xmalloc(strlen(buf_a) + strlen(buf_b) + 1);
        strcpy(result, buf_a);
        strcat(result, buf_b);
        KcVal r = kc_val_str(result);
        free(result);
        return r;
    }
    if (a.type == VAL_INT && b.type == VAL_INT)
        return kc_val_int(a.u.ival + b.u.ival);
    double fa = (a.type == VAL_INT) ? (double)a.u.ival : a.u.fval;
    double fb = (b.type == VAL_INT) ? (double)b.u.ival : b.u.fval;
    (void)vm;
    return kc_val_float(fa + fb);
}

KcVal kc_val_eq(KcVal a, KcVal b)
{
    if (a.type == VAL_NULL && b.type == VAL_NULL) return kc_val_bool(1);
    if (a.type == VAL_NULL || b.type == VAL_NULL) return kc_val_bool(0);
    if (a.type == VAL_BOOL && b.type == VAL_BOOL) return kc_val_bool(a.u.bval == b.u.bval);
    if (a.type == VAL_STR  && b.type == VAL_STR)
        return kc_val_bool(strcmp(a.u.sval, b.u.sval) == 0);
    double fa = (a.type == VAL_INT) ? (double)a.u.ival : a.u.fval;
    double fb = (b.type == VAL_INT) ? (double)b.u.ival : b.u.fval;
    return kc_val_bool(fa == fb);
}

/* ================================================================
 *  전역 변수 테이블
 * ================================================================ */
static KcVal *global_get(KcVM *vm, const char *name)
{
    for (int i = 0; i < vm->global_count; i++)
        if (strcmp(vm->globals[i].name, name) == 0)
            return &vm->globals[i].val;
    return NULL;
}

static void global_set(KcVM *vm, const char *name, KcVal val)
{
    for (int i = 0; i < vm->global_count; i++) {
        if (strcmp(vm->globals[i].name, name) == 0) {
            kc_val_free(&vm->globals[i].val);
            vm->globals[i].val = val;
            return;
        }
    }
    if (vm->global_count < KVM_GLOBAL_MAX) {
        strncpy(vm->globals[vm->global_count].name, name, 127);
        vm->globals[vm->global_count].val = val;
        vm->global_count++;
    }
}

/* ================================================================
 *  상수 풀 → KcVal 변환
 * ================================================================ */
static KcVal const_to_val(KcVM *vm, KcChunk *chunk, int idx)
{
    if (idx < 0 || idx >= chunk->const_len) {
        VM_ERROR(vm, "상수 인덱스 범위 초과: %d", idx);
        return kc_val_null();
    }
    KcConst *c = &chunk->consts[idx];
    switch (c->type) {
        case KC_CONST_INT:   return kc_val_int(c->u.ival);
        case KC_CONST_FLOAT: return kc_val_float(c->u.fval);
        case KC_CONST_STR:   return kc_val_str(c->u.sval);
        case KC_CONST_BOOL:  return kc_val_bool(c->u.bval);
        case KC_CONST_NULL:  return kc_val_null();
        case KC_CONST_FUNC: {
            if (c->u.func_idx < chunk->sub_count) {
                KcVal r;
                r.type   = VAL_FUNC;
                r.u.func = chunk->sub_chunks[c->u.func_idx];
                return r;
            }
            return kc_val_null();
        }
    }
    return kc_val_null();
}

/* ================================================================
 *  내부 헬퍼
 * ================================================================ */
/* 배열 원소를 double 로 꺼내기 */
static double arr_dbl(KcArray *arr, int i)
{
    if (!arr || i < 0 || i >= arr->len) return 0.0;
    return arr->items[i].type == VAL_INT
           ? (double)arr->items[i].u.ival : arr->items[i].u.fval;
}

/* 새 KcArray 생성 */
static KcArray *new_arr(void)
{
    KcArray *a = (KcArray *)xmalloc(sizeof(KcArray));
    a->cap = 8; a->len = 0; a->ref_count = 1;
    a->items = (KcVal *)xmalloc(a->cap * sizeof(KcVal));
    return a;
}

/* KcArray 에 값 추가 */
static void arr_push(KcArray *a, KcVal v)
{
    if (a->len >= a->cap) {
        a->cap *= 2;
        a->items = (KcVal *)realloc(a->items, a->cap * sizeof(KcVal));
    }
    a->items[a->len++] = v;
}

/* 정렬 비교 함수 */
static int cmp_dbl_asc(const void *a, const void *b)
{
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

/* ================================================================
 *  내장 함수 실행 — 102종 완전 구현
 * ================================================================ */
static KcVal run_builtin(KcVM *vm, int id, KcVal *args, int argc)
{
    (void)vm;

/* 헬퍼 매크로 */
#define ARG_DBL(i) ((args[i].type==VAL_INT)?(double)args[i].u.ival:args[i].u.fval)
#define ARR0 (args[0].u.arr)
#define NEED_ARR(n) if(argc<(n)||args[0].type!=VAL_ARRAY||!args[0].u.arr) return kc_val_null()

    switch ((KcBuiltinID)id) {

    /* ── 기본 I/O ─────────────────────────────────────────────── */
    case BUILTIN_PRINT: {
        for (int i = 0; i < argc; i++) { if (i) printf(" "); kc_val_print(&args[i]); }
        printf("\n");
        return kc_val_null();
    }
    case BUILTIN_PRINT_RAW: {
        for (int i = 0; i < argc; i++) kc_val_print(&args[i]);
        return kc_val_null();
    }
    case BUILTIN_INPUT: {
        char buf[1024];
        if (argc > 0) kc_val_print(&args[0]);
        if (fgets(buf, sizeof(buf), stdin)) {
            size_t l = strlen(buf);
            if (l > 0 && buf[l-1] == '\n') buf[l-1] = '\0';
            return kc_val_str(buf);
        }
        return kc_val_str("");
    }
    case BUILTIN_LEN: {
        if (argc < 1) return kc_val_int(0);
        if (args[0].type == VAL_STR)   return kc_val_int((int64_t)strlen(args[0].u.sval));
        if (args[0].type == VAL_ARRAY && ARR0) return kc_val_int((int64_t)ARR0->len);
        return kc_val_int(0);
    }
    case BUILTIN_RANGE: {
        int64_t start=0, end=0, step=1;
        if (argc==1) { if(args[0].type!=VAL_INT) return kc_val_null(); end=args[0].u.ival; }
        else if (argc==2) { start=args[0].u.ival; end=args[1].u.ival; }
        else if (argc>=3) { start=args[0].u.ival; end=args[1].u.ival; step=args[2].u.ival; if(!step) return kc_val_null(); }
        KcArray *a = new_arr();
        if (step>0) for(int64_t i=start;i<end;i+=step) { KcVal v=kc_val_int(i); arr_push(a,v); }
        else        for(int64_t i=start;i>end;i+=step) { KcVal v=kc_val_int(i); arr_push(a,v); }
        KcVal r; r.type=VAL_ARRAY; r.u.arr=a; return r;
    }

    /* ── 형변환 ─────────────────────────────────────────────────── */
    case BUILTIN_INT: {
        if (argc < 1) return kc_val_int(0);
        if (args[0].type==VAL_INT)   return args[0];
        if (args[0].type==VAL_FLOAT) return kc_val_int((int64_t)args[0].u.fval);
        if (args[0].type==VAL_STR)   return kc_val_int(atoll(args[0].u.sval));
        if (args[0].type==VAL_BOOL)  return kc_val_int(args[0].u.bval ? 1 : 0);
        return kc_val_int(0);
    }
    case BUILTIN_FLOAT: {
        if (argc < 1) return kc_val_float(0.0);
        if (args[0].type==VAL_FLOAT) return args[0];
        if (args[0].type==VAL_INT)   return kc_val_float((double)args[0].u.ival);
        if (args[0].type==VAL_STR)   return kc_val_float(atof(args[0].u.sval));
        if (args[0].type==VAL_BOOL)  return kc_val_float(args[0].u.bval ? 1.0 : 0.0);
        return kc_val_float(0.0);
    }
    case BUILTIN_STR: {
        if (argc < 1) return kc_val_str("");
        char buf[256];
        if (args[0].type==VAL_STR)   return args[0];
        if (args[0].type==VAL_INT)   { snprintf(buf,255,"%lld",(long long)args[0].u.ival); return kc_val_str(buf); }
        if (args[0].type==VAL_FLOAT) { snprintf(buf,255,"%g",args[0].u.fval); return kc_val_str(buf); }
        if (args[0].type==VAL_BOOL)  return kc_val_str(args[0].u.bval ? "참" : "거짓");
        if (args[0].type==VAL_NULL)  return kc_val_str("없음");
        return kc_val_str("");
    }
    case BUILTIN_BOOL: {
        if (argc < 1) return kc_val_bool(0);
        return kc_val_bool(kc_val_truthy(&args[0]));
    }

    /* ── 수학 ────────────────────────────────────────────────────── */
    case BUILTIN_SQRT:  { if(argc<1) return kc_val_float(0); return kc_val_float(sqrt(ARG_DBL(0))); }
    case BUILTIN_ABS:   {
        if(argc<1) return kc_val_int(0);
        if(args[0].type==VAL_INT) return kc_val_int(args[0].u.ival<0?-args[0].u.ival:args[0].u.ival);
        return kc_val_float(fabs(ARG_DBL(0)));
    }
    case BUILTIN_MAX: {
        if (argc < 1) return kc_val_null();
        if (argc == 1 && args[0].type == VAL_ARRAY && ARR0 && ARR0->len > 0) {
            double mx = arr_dbl(ARR0, 0);
            for (int i=1; i<ARR0->len; i++) if (arr_dbl(ARR0,i) > mx) mx = arr_dbl(ARR0,i);
            return kc_val_float(mx);
        }
        double mx = ARG_DBL(0);
        for (int i=1; i<argc; i++) if (ARG_DBL(i) > mx) mx = ARG_DBL(i);
        return kc_val_float(mx);
    }
    case BUILTIN_MIN: {
        if (argc < 1) return kc_val_null();
        if (argc == 1 && args[0].type == VAL_ARRAY && ARR0 && ARR0->len > 0) {
            double mn = arr_dbl(ARR0, 0);
            for (int i=1; i<ARR0->len; i++) if (arr_dbl(ARR0,i) < mn) mn = arr_dbl(ARR0,i);
            return kc_val_float(mn);
        }
        double mn = ARG_DBL(0);
        for (int i=1; i<argc; i++) if (ARG_DBL(i) < mn) mn = ARG_DBL(i);
        return kc_val_float(mn);
    }
    case BUILTIN_SIN:   { if(argc<1) return kc_val_float(0); return kc_val_float(sin(ARG_DBL(0))); }
    case BUILTIN_COS:   { if(argc<1) return kc_val_float(0); return kc_val_float(cos(ARG_DBL(0))); }
    case BUILTIN_TAN:   { if(argc<1) return kc_val_float(0); return kc_val_float(tan(ARG_DBL(0))); }
    case BUILTIN_LOG:   {
        if(argc<2) return kc_val_float(argc?log(ARG_DBL(0)):0);
        return kc_val_float(log(ARG_DBL(1)) / log(ARG_DBL(0)));
    }
    case BUILTIN_LN:    { if(argc<1) return kc_val_float(0); return kc_val_float(log(ARG_DBL(0))); }
    case BUILTIN_EXP:   { if(argc<1) return kc_val_float(1); return kc_val_float(exp(ARG_DBL(0))); }
    case BUILTIN_ROUND: {
        if(argc<1) return kc_val_float(0);
        double v=ARG_DBL(0);
        if(argc==1) return kc_val_int((int64_t)round(v));
        double f=pow(10.0,ARG_DBL(1)); return kc_val_float(round(v*f)/f);
    }
    case BUILTIN_FLOOR: {
        if(argc<1) return kc_val_float(0);
        double v=ARG_DBL(0);
        if(argc==1) return kc_val_int((int64_t)floor(v));
        double f=pow(10.0,ARG_DBL(1)); return kc_val_float(floor(v*f)/f);
    }
    case BUILTIN_CEIL: {
        if(argc<1) return kc_val_float(0);
        double v=ARG_DBL(0);
        if(argc==1) return kc_val_int((int64_t)ceil(v));
        double f=pow(10.0,ARG_DBL(1)); return kc_val_float(ceil(v*f)/f);
    }

    /* ── 배열 조작 ──────────────────────────────────────────────── */
    case BUILTIN_APPEND: {
        if (argc<2 || args[0].type!=VAL_ARRAY || !ARR0) return kc_val_null();
        arr_push(ARR0, args[1]);
        return kc_val_null();
    }
    case BUILTIN_ARR_SORT: {
        NEED_ARR(1);
        int n = ARR0->len;
        int desc = (argc>=2) ? (int)ARG_DBL(1) : 0;
        double *tmp = (double *)xmalloc(n * sizeof(double));
        for (int i=0; i<n; i++) tmp[i] = arr_dbl(ARR0,i);
        qsort(tmp, (size_t)n, sizeof(double), cmp_dbl_asc);
        KcArray *ra = new_arr();
        for (int i=0; i<n; i++) { KcVal v=kc_val_float(tmp[desc?n-1-i:i]); arr_push(ra,v); }
        free(tmp);
        KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
    }
    case BUILTIN_ARR_REVERSE: {
        NEED_ARR(1);
        int n = ARR0->len;
        KcArray *ra = new_arr();
        for (int i=n-1; i>=0; i--) arr_push(ra, ARR0->items[i]);
        KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
    }

    /* ── 글자 19종 ──────────────────────────────────────────────── */
    case BUILTIN_UPPER: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        char *s=xstrdup(args[0].u.sval);
        for(char *p=s;*p;p++) *p=(char)toupper((unsigned char)*p);
        KcVal r=kc_val_str(s); free(s); return r;
    }
    case BUILTIN_LOWER: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        char *s=xstrdup(args[0].u.sval);
        for(char *p=s;*p;p++) *p=(char)tolower((unsigned char)*p);
        KcVal r=kc_val_str(s); free(s); return r;
    }
    case BUILTIN_CONTAINS: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_bool(0);
        return kc_val_bool(strstr(args[0].u.sval, args[1].u.sval) != NULL);
    }
    case BUILTIN_STRIP: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        const char *s=args[0].u.sval;
        while(isspace((unsigned char)*s)) s++;
        size_t l=strlen(s);
        while(l>0&&isspace((unsigned char)s[l-1])) l--;
        char *r=(char*)xmalloc(l+1); memcpy(r,s,l); r[l]='\0';
        KcVal rv=kc_val_str(r); free(r); return rv;
    }
    case BUILTIN_REPLACE:
    case BUILTIN_STR_REPLACE1: {
        /* 대체(s, from, to) / 한번대체(s, from, to) */
        if(argc<3||args[0].type!=VAL_STR||args[1].type!=VAL_STR||args[2].type!=VAL_STR)
            return argc?args[0]:kc_val_str("");
        const char *src=args[0].u.sval, *from=args[1].u.sval, *to=args[2].u.sval;
        int first_only=(id==BUILTIN_STR_REPLACE1);
        size_t fl=strlen(from), tl=strlen(to);
        if(fl==0) return args[0];
        /* 교체 횟수 계산 */
        size_t cnt=0; const char *cur=src, *f;
        while((f=strstr(cur,from))!=NULL){cnt++;cur=f+fl;if(first_only)break;}
        if(cnt==0) return args[0];
        size_t sl=strlen(src);
        size_t nl=sl+cnt*(tl>fl?tl-fl:0)-cnt*(fl>tl?fl-tl:0);
        char *buf=(char*)xmalloc(nl+1); char *out=buf; cur=src;
        int done=0;
        while(*cur){
            if(!done&&(f=strstr(cur,from))){
                memcpy(out,cur,f-cur); out+=f-cur;
                memcpy(out,to,tl); out+=tl; cur=f+fl;
                if(first_only) done=1;
            } else { *out++=*cur++; }
        }
        *out='\0';
        KcVal rv=kc_val_str(buf); free(buf); return rv;
    }
    case BUILTIN_STRLEN: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_int(0);
        return kc_val_int((int64_t)strlen(args[0].u.sval));
    }
    case BUILTIN_STR_SUB: { /* 자르기(s, start, len) */
        if(argc<2||args[0].type!=VAL_STR) return kc_val_str("");
        const char *s=args[0].u.sval;
        int64_t sl=(int64_t)strlen(s);
        int64_t start=(args[1].type==VAL_INT)?args[1].u.ival:(int64_t)args[1].u.fval;
        if(start<0) start=0;
        if(start>=sl) return kc_val_str("");
        int64_t len=(argc>=3)? ((args[2].type==VAL_INT)?args[2].u.ival:(int64_t)args[2].u.fval) : sl-start;
        if(len<=0) return kc_val_str("");
        if(start+len>sl) len=sl-start;
        char *buf=(char*)xmalloc((size_t)len+1);
        memcpy(buf,s+start,(size_t)len); buf[len]='\0';
        KcVal rv=kc_val_str(buf); free(buf); return rv;
    }
    case BUILTIN_STR_SPLIT: { /* 분할(s, delim) */
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR)
            return kc_val_null();
        const char *s=args[0].u.sval, *d=args[1].u.sval;
        size_t dl=strlen(d);
        KcArray *ra=new_arr();
        if(dl==0){ KcVal v=kc_val_str(s); arr_push(ra,v); KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r; }
        const char *cur=s, *f;
        while((f=strstr(cur,d))){
            size_t plen=f-cur;
            char *piece=(char*)xmalloc(plen+1); memcpy(piece,cur,plen); piece[plen]='\0';
            KcVal v=kc_val_str(piece); free(piece); arr_push(ra,v);
            cur=f+dl;
        }
        KcVal v=kc_val_str(cur); arr_push(ra,v);
        KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
    }
    case BUILTIN_STR_JOIN: { /* 합치기(배열, 구분자) */
        if(argc<1||args[0].type!=VAL_ARRAY||!ARR0) return kc_val_str("");
        const char *sep=(argc>=2&&args[1].type==VAL_STR)?args[1].u.sval:"";
        size_t sl=strlen(sep), total=0;
        for(int i=0;i<ARR0->len;i++){
            if(ARR0->items[i].type==VAL_STR) total+=strlen(ARR0->items[i].u.sval);
            if(i<ARR0->len-1) total+=sl;
        }
        char *buf=(char*)xmalloc(total+1); char *out=buf;
        for(int i=0;i<ARR0->len;i++){
            if(ARR0->items[i].type==VAL_STR){ size_t il=strlen(ARR0->items[i].u.sval); memcpy(out,ARR0->items[i].u.sval,il); out+=il; }
            if(i<ARR0->len-1){ memcpy(out,sep,sl); out+=sl; }
        }
        *out='\0';
        KcVal rv=kc_val_str(buf); free(buf); return rv;
    }
    case BUILTIN_STR_REPEAT: { /* 반복글자(s, n) */
        if(argc<2||args[0].type!=VAL_STR) return kc_val_str("");
        const char *s=args[0].u.sval;
        int64_t n=(args[1].type==VAL_INT)?args[1].u.ival:(int64_t)args[1].u.fval;
        if(n<=0) return kc_val_str("");
        size_t sl=strlen(s);
        char *buf=(char*)xmalloc(sl*(size_t)n+1);
        for(int64_t i=0;i<n;i++) memcpy(buf+sl*(size_t)i,s,sl);
        buf[sl*(size_t)n]='\0';
        KcVal rv=kc_val_str(buf); free(buf); return rv;
    }
    case BUILTIN_STR_REVERSE: { /* 역순(s) */
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        size_t l=strlen(args[0].u.sval);
        char *buf=(char*)xmalloc(l+1);
        for(size_t i=0;i<l;i++) buf[i]=args[0].u.sval[l-1-i];
        buf[l]='\0';
        KcVal rv=kc_val_str(buf); free(buf); return rv;
    }
    case BUILTIN_STR_INDEXOF: { /* 위치(s, sub) */
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_int(-1);
        const char *f=strstr(args[0].u.sval, args[1].u.sval);
        return f ? kc_val_int((int64_t)(f-args[0].u.sval)) : kc_val_int(-1);
    }
    case BUILTIN_STR_STARTSWITH: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_bool(0);
        size_t pl=strlen(args[1].u.sval);
        return kc_val_bool(strncmp(args[0].u.sval,args[1].u.sval,pl)==0);
    }
    case BUILTIN_STR_ENDSWITH: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_bool(0);
        size_t sl=strlen(args[0].u.sval), pl=strlen(args[1].u.sval);
        if(pl>sl) return kc_val_bool(0);
        return kc_val_bool(strcmp(args[0].u.sval+sl-pl,args[1].u.sval)==0);
    }
    case BUILTIN_STR_COMPARE: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_int(0);
        return kc_val_int(strcmp(args[0].u.sval,args[1].u.sval));
    }
    case BUILTIN_STR_TITLE: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        char *buf=xstrdup(args[0].u.sval); int nw=1;
        for(int i=0;buf[i];i++){
            if(isspace((unsigned char)buf[i])){nw=1;}
            else if(nw){buf[i]=(char)toupper((unsigned char)buf[i]);nw=0;}
            else buf[i]=(char)tolower((unsigned char)buf[i]);
        }
        KcVal rv=kc_val_str(buf); free(buf); return rv;
    }
    case BUILTIN_STR_LTRIM: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        const char *s=args[0].u.sval;
        while(isspace((unsigned char)*s)) s++;
        return kc_val_str(s);
    }
    case BUILTIN_STR_RTRIM: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        size_t l=strlen(args[0].u.sval);
        char *buf=xstrdup(args[0].u.sval);
        while(l>0&&isspace((unsigned char)buf[l-1])) l--;
        buf[l]='\0';
        KcVal rv=kc_val_str(buf); free(buf); return rv;
    }
    case BUILTIN_STR_REGEX: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_bool(0);
        regex_t re; int r=regcomp(&re,args[1].u.sval,REG_EXTENDED|REG_NOSUB);
        if(r!=0) return kc_val_bool(0);
        int matched=(regexec(&re,args[0].u.sval,0,NULL,0)==0);
        regfree(&re);
        return kc_val_bool(matched);
    }
    case BUILTIN_STR_PARSE: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_null();
        const char *s=args[0].u.sval;
        if(strcmp(s,"참")==0) return kc_val_bool(1);
        if(strcmp(s,"거짓")==0) return kc_val_bool(0);
        char *ep; int64_t iv=strtoll(s,&ep,10);
        if(ep!=s&&*ep=='\0') return kc_val_int(iv);
        double dv=strtod(s,&ep);
        if(ep!=s&&*ep=='\0') return kc_val_float(dv);
        return args[0];
    }
    case BUILTIN_STR_FORMAT: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        const char *fmt=args[0].u.sval;
        char *buf=(char*)xmalloc(65536); char *out=buf; size_t rem=65535;
        int ai=1;
        for(const char *fp=fmt; *fp&&rem>0; fp++){
            if(*fp=='%'&&*(fp+1)){
                fp++;
                if(*fp=='%'){ *out++='%'; rem--; }
                else if((*fp=='d'||*fp=='i')&&ai<argc){
                    int64_t v=(args[ai].type==VAL_INT)?args[ai].u.ival:(int64_t)args[ai].u.fval;
                    int w=(int)snprintf(out,rem,"%lld",(long long)v);
                    if(w>0){out+=w;rem-=(size_t)w;} ai++;
                } else if(*fp=='f'&&ai<argc){
                    double v=(args[ai].type==VAL_FLOAT)?args[ai].u.fval:(double)args[ai].u.ival;
                    int w=(int)snprintf(out,rem,"%f",v);
                    if(w>0){out+=w;rem-=(size_t)w;} ai++;
                } else if(*fp=='g'&&ai<argc){
                    double v=(args[ai].type==VAL_FLOAT)?args[ai].u.fval:(double)args[ai].u.ival;
                    int w=(int)snprintf(out,rem,"%g",v);
                    if(w>0){out+=w;rem-=(size_t)w;} ai++;
                } else if(*fp=='s'&&ai<argc){
                    char tmp[256]; if(args[ai].type==VAL_STR) snprintf(tmp,255,"%s",args[ai].u.sval);
                    else if(args[ai].type==VAL_INT) snprintf(tmp,255,"%lld",(long long)args[ai].u.ival);
                    else snprintf(tmp,255,"%g",args[ai].u.fval);
                    int w=(int)snprintf(out,rem,"%s",tmp);
                    if(w>0){out+=w;rem-=(size_t)w;} ai++;
                } else { *out++='%'; *out++=*fp; rem-=2; }
            } else { *out++=*fp; rem--; }
        }
        *out='\0';
        KcVal rv=kc_val_str(buf); free(buf); return rv;
    }

    /* ── AI 활성함수 ──────────────────────────────────────────── */
    case BUILTIN_SIGMOID: {
        if(argc<1) return kc_val_float(0.5);
        if(args[0].type==VAL_ARRAY&&ARR0){
            KcArray *ra=new_arr();
            for(int i=0;i<ARR0->len;i++){ KcVal v=kc_val_float(1.0/(1.0+exp(-arr_dbl(ARR0,i)))); arr_push(ra,v); }
            KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
        }
        return kc_val_float(1.0/(1.0+exp(-ARG_DBL(0))));
    }
    case BUILTIN_RELU: {
        if(argc<1) return kc_val_float(0);
        if(args[0].type==VAL_ARRAY&&ARR0){
            KcArray *ra=new_arr();
            for(int i=0;i<ARR0->len;i++){ double v=arr_dbl(ARR0,i); KcVal kv=kc_val_float(v>0?v:0); arr_push(ra,kv); }
            KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
        }
        double v=ARG_DBL(0); return kc_val_float(v>0?v:0);
    }
    case BUILTIN_TANH_FN: {
        if(argc<1) return kc_val_float(0);
        if(args[0].type==VAL_ARRAY&&ARR0){
            KcArray *ra=new_arr();
            for(int i=0;i<ARR0->len;i++){ KcVal v=kc_val_float(tanh(arr_dbl(ARR0,i))); arr_push(ra,v); }
            KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
        }
        return kc_val_float(tanh(ARG_DBL(0)));
    }

    /* ── AI/수열 함수 ─────────────────────────────────────────── */
    case BUILTIN_MSE: { /* 평균제곱오차(예측배열, 실제배열) */
        if(argc<2||args[0].type!=VAL_ARRAY||args[1].type!=VAL_ARRAY||!ARR0||!args[1].u.arr) return kc_val_float(0);
        int n=ARR0->len; if(n==0) return kc_val_float(0);
        double s=0;
        for(int i=0;i<n;i++){ double d=arr_dbl(ARR0,i)-arr_dbl(args[1].u.arr,i); s+=d*d; }
        return kc_val_float(s/n);
    }
    case BUILTIN_CROSS_ENT: { /* 교차엔트로피(P*, P) */
        if(argc<2||args[0].type!=VAL_ARRAY||args[1].type!=VAL_ARRAY||!ARR0||!args[1].u.arr) return kc_val_float(0);
        int n=ARR0->len; double s=0;
        for(int i=0;i<n;i++){ double ps=arr_dbl(ARR0,i), p=arr_dbl(args[1].u.arr,i); if(p>1e-15) s-=ps*log(p); }
        return kc_val_float(s);
    }
    case BUILTIN_SOFTMAX: { /* 소프트맥스(배열) */
        NEED_ARR(1);
        int n=ARR0->len; if(n==0) return args[0];
        double mx=arr_dbl(ARR0,0);
        for(int i=1;i<n;i++) if(arr_dbl(ARR0,i)>mx) mx=arr_dbl(ARR0,i);
        double s=0; double *tmp=(double*)xmalloc(n*sizeof(double));
        for(int i=0;i<n;i++){tmp[i]=exp(arr_dbl(ARR0,i)-mx);s+=tmp[i];}
        KcArray *ra=new_arr();
        for(int i=0;i<n;i++){KcVal v=kc_val_float(tmp[i]/s);arr_push(ra,v);}
        free(tmp);
        KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
    }
    case BUILTIN_POS_ENC: { /* 위치인코딩(위치, 차원수) */
        if(argc<2) return kc_val_null();
        int64_t pos=(args[0].type==VAL_INT)?args[0].u.ival:(int64_t)args[0].u.fval;
        int64_t dim=(args[1].type==VAL_INT)?args[1].u.ival:(int64_t)args[1].u.fval;
        KcArray *ra=new_arr();
        for(int64_t i=0;i<dim;i++){
            double v=(i%2==0)?sin((double)pos/pow(10000.0,(double)i/dim))
                             :cos((double)pos/pow(10000.0,(double)(i-1)/dim));
            KcVal kv=kc_val_float(v); arr_push(ra,kv);
        }
        KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
    }
    case BUILTIN_GEOM_SUM: { /* 등비수열합(a, r) */
        if(argc<2) return kc_val_float(0);
        double a=ARG_DBL(0), r=ARG_DBL(1);
        if(fabs(r)>=1.0) return kc_val_float(0); /* 발산 */
        return kc_val_float(a/(1.0-r));
    }
    case BUILTIN_ARITH_SUM: { /* 등차수열합(a, d, n) */
        if(argc<3) return kc_val_float(0);
        double a=ARG_DBL(0), d=ARG_DBL(1); int64_t n=(int64_t)ARG_DBL(2);
        return kc_val_float((double)n/2.0*(2.0*a+(double)(n-1)*d));
    }
    case BUILTIN_RECUR_GEOM: { /* 점화식값(a1, r, n) */
        if(argc<3) return kc_val_float(0);
        double a=ARG_DBL(0), r=ARG_DBL(1); int64_t n=(int64_t)ARG_DBL(2);
        return kc_val_float(a*pow(r,(double)(n-1)));
    }

    /* ── 통계 함수 ────────────────────────────────────────────── */
    case BUILTIN_MEAN: {
        NEED_ARR(1);
        int n=ARR0->len; if(n==0) return kc_val_float(0);
        double s=0; for(int i=0;i<n;i++) s+=arr_dbl(ARR0,i);
        return kc_val_float(s/n);
    }
    case BUILTIN_STDEV: {
        NEED_ARR(1);
        int n=ARR0->len; if(n<2) return kc_val_float(0);
        int samp=(argc>=2)?(int)ARG_DBL(1):0;
        double mu=0; for(int i=0;i<n;i++) mu+=arr_dbl(ARR0,i); mu/=n;
        double sq=0; for(int i=0;i<n;i++){double d=arr_dbl(ARR0,i)-mu;sq+=d*d;}
        return kc_val_float(sqrt(sq/(samp?n-1:n)));
    }
    case BUILTIN_SUM: {
        NEED_ARR(1);
        double s=0; for(int i=0;i<ARR0->len;i++) s+=arr_dbl(ARR0,i);
        return kc_val_float(s);
    }
    case BUILTIN_VARIANCE: {
        NEED_ARR(1);
        int n=ARR0->len; if(n<2) return kc_val_float(0);
        int samp=(argc>=2)?(int)ARG_DBL(1):0;
        double mu=0; for(int i=0;i<n;i++) mu+=arr_dbl(ARR0,i); mu/=n;
        double sq=0; for(int i=0;i<n;i++){double d=arr_dbl(ARR0,i)-mu;sq+=d*d;}
        return kc_val_float(sq/(samp?n-1:n));
    }
    case BUILTIN_MEDIAN: {
        NEED_ARR(1);
        int n=ARR0->len; if(n==0) return kc_val_float(0);
        double *tmp=(double*)xmalloc(n*sizeof(double));
        for(int i=0;i<n;i++) tmp[i]=arr_dbl(ARR0,i);
        qsort(tmp,(size_t)n,sizeof(double),cmp_dbl_asc);
        double med=(n%2==0)?(tmp[n/2-1]+tmp[n/2])/2.0:tmp[n/2];
        free(tmp); return kc_val_float(med);
    }
    case BUILTIN_MODE: {
        NEED_ARR(1);
        int n=ARR0->len; if(n==0) return kc_val_float(0);
        double *tmp=(double*)xmalloc(n*sizeof(double));
        for(int i=0;i<n;i++) tmp[i]=arr_dbl(ARR0,i);
        qsort(tmp,(size_t)n,sizeof(double),cmp_dbl_asc);
        double mode=tmp[0]; int mx=1, cnt=1;
        for(int i=1;i<n;i++){
            if(tmp[i]==tmp[i-1]){cnt++;if(cnt>mx){mx=cnt;mode=tmp[i];}}
            else cnt=1;
        }
        free(tmp); return kc_val_float(mode);
    }
    case BUILTIN_CUMSUM: {
        NEED_ARR(1);
        KcArray *ra=new_arr(); double acc=0;
        for(int i=0;i<ARR0->len;i++){acc+=arr_dbl(ARR0,i);KcVal v=kc_val_float(acc);arr_push(ra,v);}
        KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
    }
    case BUILTIN_COVARIANCE: {
        if(argc<2||args[0].type!=VAL_ARRAY||args[1].type!=VAL_ARRAY||!ARR0||!args[1].u.arr) return kc_val_float(0);
        int n=ARR0->len; if(n!=args[1].u.arr->len||n<2) return kc_val_float(0);
        int samp=(argc>=3)?(int)ARG_DBL(2):0;
        double m1=0,m2=0;
        for(int i=0;i<n;i++){m1+=arr_dbl(ARR0,i);m2+=arr_dbl(args[1].u.arr,i);}
        m1/=n; m2/=n; double cov=0;
        for(int i=0;i<n;i++) cov+=(arr_dbl(ARR0,i)-m1)*(arr_dbl(args[1].u.arr,i)-m2);
        return kc_val_float(cov/(samp?n-1:n));
    }
    case BUILTIN_CORRELATION: {
        if(argc<2||args[0].type!=VAL_ARRAY||args[1].type!=VAL_ARRAY||!ARR0||!args[1].u.arr) return kc_val_float(0);
        int n=ARR0->len; if(n!=args[1].u.arr->len||n<2) return kc_val_float(0);
        double m1=0,m2=0;
        for(int i=0;i<n;i++){m1+=arr_dbl(ARR0,i);m2+=arr_dbl(args[1].u.arr,i);}
        m1/=n; m2/=n; double cov=0,s1=0,s2=0;
        for(int i=0;i<n;i++){
            double d1=arr_dbl(ARR0,i)-m1, d2=arr_dbl(args[1].u.arr,i)-m2;
            cov+=d1*d2; s1+=d1*d1; s2+=d2*d2;
        }
        double denom=sqrt(s1*s2);
        return kc_val_float(denom==0?0:cov/denom);
    }
    case BUILTIN_NORMALIZE: {
        NEED_ARR(1);
        int n=ARR0->len; if(n==0) return args[0];
        double mn=arr_dbl(ARR0,0),mx=arr_dbl(ARR0,0);
        for(int i=1;i<n;i++){double v=arr_dbl(ARR0,i);if(v<mn)mn=v;if(v>mx)mx=v;}
        double rng=mx-mn;
        KcArray *ra=new_arr();
        for(int i=0;i<n;i++){double v=(rng==0)?0:(arr_dbl(ARR0,i)-mn)/rng;KcVal kv=kc_val_float(v);arr_push(ra,kv);}
        KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
    }
    case BUILTIN_STANDARDIZE: {
        NEED_ARR(1);
        int n=ARR0->len; if(n<2) return args[0];
        double mu=0; for(int i=0;i<n;i++) mu+=arr_dbl(ARR0,i); mu/=n;
        double sq=0; for(int i=0;i<n;i++){double d=arr_dbl(ARR0,i)-mu;sq+=d*d;}
        double sigma=sqrt(sq/n); if(sigma==0) return args[0];
        KcArray *ra=new_arr();
        for(int i=0;i<n;i++){KcVal v=kc_val_float((arr_dbl(ARR0,i)-mu)/sigma);arr_push(ra,v);}
        KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
    }

    /* ── 관계심리 ─────────────────────────────────────────────── */
    case BUILTIN_ATTRACTION: {
        /* 호감도(T,S,C,R) 평균형 / 호감도(T,S,C,R,α,β,γ,δ) 가중합형
           호감도(배열) / 호감도(배열, 가중치배열) */
        if(argc==1&&args[0].type==VAL_ARRAY&&ARR0&&ARR0->len>=4){
            double s=0; for(int i=0;i<4;i++) s+=arr_dbl(ARR0,i);
            return kc_val_float(s/4.0);
        }
        if(argc==2&&args[0].type==VAL_ARRAY&&args[1].type==VAL_ARRAY&&ARR0&&args[1].u.arr&&ARR0->len>=4&&args[1].u.arr->len>=4){
            double s=0; for(int i=0;i<4;i++) s+=arr_dbl(ARR0,i)*arr_dbl(args[1].u.arr,i);
            return kc_val_float(s);
        }
        if(argc>=4&&args[0].type!=VAL_ARRAY){
            if(argc>=8){ double s=0; for(int i=0;i<4;i++) s+=ARG_DBL(i)*ARG_DBL(4+i); return kc_val_float(s); }
            double s=0; for(int i=0;i<4;i++) s+=ARG_DBL(i);
            return kc_val_float(s/4.0);
        }
        return kc_val_float(0);
    }

    /* ── 파일 함수 17종 ──────────────────────────────────────── */
    case BUILTIN_FILE_OPEN: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_null();
        FILE *f=fopen(args[0].u.sval, args[1].u.sval);
        if(!f) return kc_val_null();
        char buf[64]; snprintf(buf,63,"%p",(void*)f);
        return kc_val_str(buf);
    }
    case BUILTIN_FILE_CLOSE: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_null();
        FILE *f=NULL; sscanf(args[0].u.sval,"%p",(void**)&f);
        if(f) fclose(f);
        return kc_val_null();
    }
    case BUILTIN_FILE_READ: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        FILE *f=NULL; sscanf(args[0].u.sval,"%p",(void**)&f);
        if(!f) return kc_val_str("");
        char buf[4096]; size_t n=fread(buf,1,4095,f); buf[n]='\0';
        return kc_val_str(buf);
    }
    case BUILTIN_FILE_READLINE: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        FILE *f=NULL; sscanf(args[0].u.sval,"%p",(void**)&f);
        if(!f) return kc_val_str("");
        char buf[4096];
        if(fgets(buf,4096,f)) return kc_val_str(buf);
        return kc_val_str("");
    }
    case BUILTIN_FILE_WRITE: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_bool(0);
        FILE *f=NULL; sscanf(args[0].u.sval,"%p",(void**)&f);
        if(!f) return kc_val_bool(0);
        return kc_val_bool(fputs(args[1].u.sval,f)>=0);
    }
    case BUILTIN_FILE_WRITELN: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_bool(0);
        FILE *f=NULL; sscanf(args[0].u.sval,"%p",(void**)&f);
        if(!f) return kc_val_bool(0);
        return kc_val_bool(fprintf(f,"%s\n",args[1].u.sval)>=0);
    }
    case BUILTIN_FILE_EXISTS: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_bool(0);
        struct stat st; return kc_val_bool(stat(args[0].u.sval,&st)==0);
    }
    case BUILTIN_FILE_SIZE: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_int(-1);
        struct stat st; if(stat(args[0].u.sval,&st)!=0) return kc_val_int(-1);
        return kc_val_int((int64_t)st.st_size);
    }
    case BUILTIN_FILE_LIST: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_null();
        DIR *d=opendir(args[0].u.sval); if(!d) return kc_val_null();
        KcArray *ra=new_arr(); struct dirent *ent;
        while((ent=readdir(d))){
            if(strcmp(ent->d_name,".")==0||strcmp(ent->d_name,"..")==0) continue;
            KcVal v=kc_val_str(ent->d_name); arr_push(ra,v);
        }
        closedir(d);
        KcVal r; r.type=VAL_ARRAY; r.u.arr=ra; return r;
    }
    case BUILTIN_FILE_NAME: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        char *tmp=xstrdup(args[0].u.sval);
        char *b=basename(tmp); KcVal r=kc_val_str(b); free(tmp); return r;
    }
    case BUILTIN_FILE_EXT: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        char *tmp=xstrdup(args[0].u.sval);
        char *b=basename(tmp); char *dot=strrchr(b,'.');
        KcVal r=kc_val_str(dot?dot:""); free(tmp); return r;
    }
    case BUILTIN_DIR_MAKE: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_bool(0);
        return kc_val_bool(mkdir(args[0].u.sval,0755)==0);
    }
    case BUILTIN_FILE_DELETE: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_bool(0);
        return kc_val_bool(remove(args[0].u.sval)==0);
    }
    case BUILTIN_FILE_COPY: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_bool(0);
        FILE *src=fopen(args[0].u.sval,"rb"); if(!src) return kc_val_bool(0);
        FILE *dst=fopen(args[1].u.sval,"wb"); if(!dst){fclose(src);return kc_val_bool(0);}
        char buf[4096]; size_t n;
        while((n=fread(buf,1,4096,src))>0) fwrite(buf,1,n,dst);
        fclose(src); fclose(dst); return kc_val_bool(1);
    }
    case BUILTIN_FILE_MOVE: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_bool(0);
        return kc_val_bool(rename(args[0].u.sval,args[1].u.sval)==0);
    }
    case BUILTIN_FILE_READALL: {
        if(argc<1||args[0].type!=VAL_STR) return kc_val_str("");
        FILE *f=fopen(args[0].u.sval,"r"); if(!f) return kc_val_str("");
        fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
        char *buf=(char*)xmalloc((size_t)sz+1);
        size_t n=fread(buf,1,(size_t)sz,f); buf[n]='\0'; fclose(f);
        KcVal rv=kc_val_str(buf); free(buf); return rv;
    }
    case BUILTIN_FILE_WRITEALL: {
        if(argc<2||args[0].type!=VAL_STR||args[1].type!=VAL_STR) return kc_val_bool(0);
        FILE *f=fopen(args[0].u.sval,"w"); if(!f) return kc_val_bool(0);
        int ok=(fputs(args[1].u.sval,f)>=0); fclose(f);
        return kc_val_bool(ok);
    }

    /* ── 텐서 13종 ───────────────────────────────── */
#define T0 (argc>0 && args[0].type==VAL_TENSOR ? args[0].u.tensor : NULL)
#define T1 (argc>1 && args[1].type==VAL_TENSOR ? args[1].u.tensor : NULL)

    /* 텐서생성(데이터배열, 형태배열) */
    case BUILTIN_TENSOR_CREATE: {
        if (argc<2 || args[0].type!=VAL_ARRAY || args[1].type!=VAL_ARRAY) {
            fprintf(stderr,"[KVM] 텐서생성: 데이터배열, 형태배열 필요\n");
            return kc_val_null();
        }
        KcArray *da=args[0].u.arr, *sa=args[1].u.arr;
        int dlen=da?da->len:0, ndim=sa?sa->len:0;
        double *dbuf=dlen>0?(double*)xmalloc(sizeof(double)*(size_t)dlen):NULL;
        for(int i=0;i<dlen;i++){
            KcVal *v=&da->items[i];
            dbuf[i]=(v->type==VAL_INT)?(double)v->u.ival:(v->type==VAL_FLOAT)?v->u.fval:0.0;
        }
        int64_t *shape=ndim>0?(int64_t*)xmalloc(sizeof(int64_t)*(size_t)ndim):NULL;
        for(int i=0;i<ndim;i++){
            KcVal *v=&sa->items[i];
            shape[i]=(v->type==VAL_INT)?v->u.ival:(int64_t)v->u.fval;
        }
        KcTensor *t=kc_tensor_from_flat(dbuf,(int64_t)dlen,shape,ndim);
        free(dbuf); free(shape);
        return t?kc_val_tensor(t):kc_val_null();
    }

    /* 텐서형태(t) -> 글자 */
    case BUILTIN_TENSOR_SHAPE: {
        if(!T0) return kc_val_str("[]");
        char *s=kc_tensor_shape_str(T0);
        KcVal r=kc_val_str(s?s:"[]"); free(s); return r;
    }

    /* 텐서크기(t) -> 정수 */
    case BUILTIN_TENSOR_NUMEL:
        return kc_val_int(T0?kc_tensor_numel(T0):0);

    /* 텐서차원(t) -> 정수 */
    case BUILTIN_TENSOR_NDIM:
        return kc_val_int(T0?kc_tensor_ndim(T0):0);

    /* 텐서합(t) -> 실수 */
    case BUILTIN_TENSOR_SUM:
        return kc_val_float(T0?kc_tensor_sum(T0):0.0);

    /* 텐서평균(t) -> 실수 */
    case BUILTIN_TENSOR_MEAN:
        return kc_val_float(T0?kc_tensor_mean(T0):0.0);

    /* 텐서최대(t) -> 실수 */
    case BUILTIN_TENSOR_MAX:
        return kc_val_float(T0?kc_tensor_max(T0):0.0);

    /* 텐서최소(t) -> 실수 */
    case BUILTIN_TENSOR_MIN:
        return kc_val_float(T0?kc_tensor_min(T0):0.0);

    /* 텐서행렬곱(a, b) -> 텐서 */
    case BUILTIN_TENSOR_MATMUL: {
        KcTensor *r=kc_tensor_matmul(T0,T1);
        return r?kc_val_tensor(r):kc_val_null();
    }

    /* 텐서전치(t) -> 텐서 */
    case BUILTIN_TENSOR_TRANSPOSE: {
        KcTensor *r=kc_tensor_transpose(T0);
        return r?kc_val_tensor(r):kc_val_null();
    }

    /* 텐서변형(t, 형태배열) -> 텐서 */
    case BUILTIN_TENSOR_RESHAPE: {
        if(!T0||argc<2||args[1].type!=VAL_ARRAY) return kc_val_null();
        KcArray *sa=args[1].u.arr;
        int ndim=sa?sa->len:0;
        int64_t *shape=ndim>0?(int64_t*)xmalloc(sizeof(int64_t)*(size_t)ndim):NULL;
        for(int i=0;i<ndim;i++){
            KcVal *v=&sa->items[i];
            shape[i]=(v->type==VAL_INT)?v->u.ival:(int64_t)v->u.fval;
        }
        KcTensor *r=kc_tensor_reshape(T0,shape,ndim);
        free(shape);
        return r?kc_val_tensor(r):kc_val_null();
    }

    /* 텐서평탄화(t) -> 텐서 */
    case BUILTIN_TENSOR_FLATTEN: {
        KcTensor *r=kc_tensor_flatten(T0);
        return r?kc_val_tensor(r):kc_val_null();
    }

    /* 텐서복사(t) -> 텐서 */
    case BUILTIN_TENSOR_COPY: {
        KcTensor *r=kc_tensor_copy(T0);
        return r?kc_val_tensor(r):kc_val_null();
    }

#undef T0
#undef T1

    /* ── 자동미분 2종 ───────────────────────────────── */

    /* 역전파(텐서) */
    case BUILTIN_BACKWARD: {
        if(argc<1||args[0].type!=VAL_TENSOR){
            fprintf(stderr,"[KVM] 역전파: 텐서 인자 필요\n");
            return kc_val_null();
        }
        kc_autograd_backward(args[0].u.tensor);
        return kc_val_null();
    }

    /* 기울기초기화(텐서) */
    case BUILTIN_ZERO_GRAD: {
        if(argc<1||args[0].type!=VAL_TENSOR){
            fprintf(stderr,"[KVM] 기울기초기화: 텐서 인자 필요\n");
            return kc_val_null();
        }
        kc_autograd_zero_grad(args[0].u.tensor);
        return kc_val_null();
    }

    /* ── MCP (stub) ────────────────────────────────────────── */
    case BUILTIN_MCP_ERROR:
        fprintf(stderr, "[KVM] MCP오류\n");
        return kc_val_null();

    default:
        return kc_val_null();
    }

#undef ARG_DBL
#undef ARR0
#undef NEED_ARR
}

/* ================================================================
 *  VM 초기화 / 해제
 * ================================================================ */
void kc_vm_init(KcVM *vm)
{
    memset(vm, 0, sizeof(KcVM));
}

void kc_vm_free(KcVM *vm)
{
    /* 스택 값 해제 */
    for (int i = 0; i < vm->stack_top; i++)
        kc_val_free(&vm->stack[i]);
    /* 전역 값 해제 */
    for (int i = 0; i < vm->global_count; i++)
        kc_val_free(&vm->globals[i].val);
    vm->stack_top    = 0;
    vm->frame_count  = 0;
    vm->global_count = 0;
}

/* ================================================================
 *  디버그: 스택 덤프
 * ================================================================ */
void kc_vm_dump_stack(const KcVM *vm)
{
    printf("[스택 (%d)]: ", vm->stack_top);
    for (int i = 0; i < vm->stack_top; i++) {
        printf("| ");
        kc_val_print(&vm->stack[i]);
        printf(" ");
    }
    printf("|\n");
}

/* ================================================================
 *  메인 실행 루프
 * ================================================================ */
int kc_vm_run(KcVM *vm, KcModule *mod)
{
    vm->module = mod;
    vm->had_error = 0;

    if (vm->frame_count >= KVM_FRAME_MAX) {
        VM_ERROR(vm, "호출 스택 초과");
        return -1;
    }

    KcFrame *frame = &vm->frames[vm->frame_count++];
    memset(frame, 0, sizeof(KcFrame));
    frame->chunk       = mod->main_chunk;
    frame->ip          = 0;
    frame->try_catch_ip = -1;
    frame->stack_base  = vm->stack_top;

    #define FRAME  (vm->frames[vm->frame_count - 1])
    #define CHUNK  (FRAME.chunk)
    #define IP     (FRAME.ip)

    while (!vm->had_error) {
        if (IP >= CHUNK->code_len) break;

        KcInstr *ins = &CHUNK->code[IP++];
        uint8_t  op  = ins->op;
        int32_t  arg = ins->arg;

        switch ((KcOpCode)op) {

        case OP_NOP: break;

        /* ── 상수 로드 ─────────────────────────────────────── */
        case OP_LOAD_CONST: {
            KcVal v = const_to_val(vm, CHUNK, arg);
            if (vm->had_error) goto vm_error;
            PUSH(vm, v);
            break;
        }
        case OP_PUSH_INT:   PUSH(vm, kc_val_int(arg)); break;
        case OP_PUSH_BOOL:  PUSH(vm, kc_val_bool(arg)); break;
        case OP_PUSH_NULL:  PUSH(vm, kc_val_null()); break;

        /* ── 스택 조작 ─────────────────────────────────────── */
        case OP_POP:  { KcVal v = POP(vm); kc_val_free(&v); break; }
        case OP_DUP:  { KcVal dup_v = vm->stack[vm->stack_top - 1]; PUSH(vm, dup_v); break; }
        case OP_SWAP: {
            KcVal a = POP(vm), b = POP(vm);
            PUSH(vm, a); PUSH(vm, b);
            break;
        }

        /* ── 지역 변수 ─────────────────────────────────────── */
        case OP_LOAD_LOCAL: {
            if (arg < 0 || arg >= KVM_LOCALS_MAX) {
                VM_ERROR(vm, "지역 변수 인덱스 범위 초과: %d", arg);
                goto vm_error;
            }
            PUSH(vm, FRAME.locals[arg]);
            break;
        }
        case OP_STORE_LOCAL: {
            if (arg < 0 || arg >= KVM_LOCALS_MAX) {
                VM_ERROR(vm, "지역 변수 인덱스 범위 초과: %d", arg);
                goto vm_error;
            }
            kc_val_free(&FRAME.locals[arg]);
            FRAME.locals[arg] = PEEK(vm, 0); /* 대입 후 스택 유지 */
            break;
        }

        /* ── 전역 변수 ─────────────────────────────────────── */
        case OP_LOAD_GLOBAL: {
            KcVal name_v = const_to_val(vm, CHUNK, arg);
            const char *name = (name_v.type == VAL_STR) ? name_v.u.sval : "";
            KcVal *gv = global_get(vm, name);
            PUSH(vm, gv ? *gv : kc_val_null());
            kc_val_free(&name_v);
            break;
        }
        case OP_STORE_GLOBAL: {
            KcVal name_v = const_to_val(vm, CHUNK, arg);
            const char *name = (name_v.type == VAL_STR) ? name_v.u.sval : "";
            global_set(vm, name, PEEK(vm, 0));
            kc_val_free(&name_v);
            break;
        }

        /* ── 산술 ───────────────────────────────────────────── */
        case OP_ADD: {
            KcVal b = POP(vm), a = POP(vm);
            PUSH(vm, kc_val_add(vm, a, b));
            kc_val_free(&a); kc_val_free(&b);
            break;
        }
        case OP_SUB: {
            KcVal b = POP(vm), a = POP(vm);
            double fa = (a.type==VAL_INT)?(double)a.u.ival:a.u.fval;
            double fb = (b.type==VAL_INT)?(double)b.u.ival:b.u.fval;
            if (a.type==VAL_INT && b.type==VAL_INT)
                PUSH(vm, kc_val_int(a.u.ival - b.u.ival));
            else
                PUSH(vm, kc_val_float(fa - fb));
            break;
        }
        case OP_MUL: {
            KcVal b = POP(vm), a = POP(vm);
            if (a.type==VAL_INT && b.type==VAL_INT)
                PUSH(vm, kc_val_int(a.u.ival * b.u.ival));
            else {
                double fa=(a.type==VAL_INT)?(double)a.u.ival:a.u.fval;
                double fb=(b.type==VAL_INT)?(double)b.u.ival:b.u.fval;
                PUSH(vm, kc_val_float(fa * fb));
            }
            break;
        }
        case OP_DIV: {
            KcVal b = POP(vm), a = POP(vm);
            double fa=(a.type==VAL_INT)?(double)a.u.ival:a.u.fval;
            double fb=(b.type==VAL_INT)?(double)b.u.ival:b.u.fval;
            if (fb == 0.0) { VM_ERROR(vm, "0으로 나누기"); goto vm_error; }
            PUSH(vm, kc_val_float(fa / fb));
            break;
        }
        case OP_MOD: {
            KcVal b = POP(vm), a = POP(vm);
            if (a.type==VAL_INT && b.type==VAL_INT) {
                if (b.u.ival == 0) { VM_ERROR(vm, "0으로 나머지"); goto vm_error; }
                PUSH(vm, kc_val_int(a.u.ival % b.u.ival));
            } else {
                double fa=(a.type==VAL_INT)?(double)a.u.ival:a.u.fval;
                double fb=(b.type==VAL_INT)?(double)b.u.ival:b.u.fval;
                PUSH(vm, kc_val_float(fmod(fa, fb)));
            }
            break;
        }
        case OP_POW: {
            KcVal b = POP(vm), a = POP(vm);
            double fa=(a.type==VAL_INT)?(double)a.u.ival:a.u.fval;
            double fb=(b.type==VAL_INT)?(double)b.u.ival:b.u.fval;
            PUSH(vm, kc_val_float(pow(fa, fb)));
            break;
        }
        case OP_NEG: {
            KcVal a = POP(vm);
            if (a.type==VAL_INT) PUSH(vm, kc_val_int(-a.u.ival));
            else PUSH(vm, kc_val_float(-a.u.fval));
            break;
        }

        /* ── 비교 ───────────────────────────────────────────── */
        case OP_EQ: { KcVal b=POP(vm),a=POP(vm); PUSH(vm,kc_val_eq(a,b)); break; }
        case OP_NE: {
            KcVal b=POP(vm),a=POP(vm);
            KcVal eq = kc_val_eq(a,b);
            PUSH(vm, kc_val_bool(!eq.u.bval));
            break;
        }
        case OP_LT: {
            KcVal b=POP(vm), a=POP(vm);
            double fa=(a.type==VAL_INT)?(double)a.u.ival:a.u.fval;
            double fb=(b.type==VAL_INT)?(double)b.u.ival:b.u.fval;
            PUSH(vm, kc_val_bool(fa < fb));
            break;
        }
        case OP_LE: {
            KcVal b=POP(vm), a=POP(vm);
            double fa=(a.type==VAL_INT)?(double)a.u.ival:a.u.fval;
            double fb=(b.type==VAL_INT)?(double)b.u.ival:b.u.fval;
            PUSH(vm, kc_val_bool(fa <= fb));
            break;
        }
        case OP_GT: {
            KcVal b=POP(vm), a=POP(vm);
            double fa=(a.type==VAL_INT)?(double)a.u.ival:a.u.fval;
            double fb=(b.type==VAL_INT)?(double)b.u.ival:b.u.fval;
            PUSH(vm, kc_val_bool(fa > fb));
            break;
        }
        case OP_GE: {
            KcVal b=POP(vm), a=POP(vm);
            double fa=(a.type==VAL_INT)?(double)a.u.ival:a.u.fval;
            double fb=(b.type==VAL_INT)?(double)b.u.ival:b.u.fval;
            PUSH(vm, kc_val_bool(fa >= fb));
            break;
        }

        /* ── 논리 ───────────────────────────────────────────── */
        case OP_AND: {
            KcVal b=POP(vm), a=POP(vm);
            PUSH(vm, kc_val_bool(kc_val_truthy(&a) && kc_val_truthy(&b)));
            break;
        }
        case OP_OR: {
            KcVal b=POP(vm), a=POP(vm);
            PUSH(vm, kc_val_bool(kc_val_truthy(&a) || kc_val_truthy(&b)));
            break;
        }
        case OP_NOT: {
            KcVal a=POP(vm);
            PUSH(vm, kc_val_bool(!kc_val_truthy(&a)));
            break;
        }

        /* ── 제어 흐름 ──────────────────────────────────────── */
        case OP_JUMP:
            IP = arg;
            break;
        case OP_JUMP_BACK:
            IP = arg;
            break;
        case OP_JUMP_FALSE: {
            KcVal cond = POP(vm);
            if (!kc_val_truthy(&cond)) IP = arg;
            kc_val_free(&cond);
            break;
        }
        case OP_JUMP_TRUE: {
            KcVal cond = POP(vm);
            if (kc_val_truthy(&cond)) IP = arg;
            kc_val_free(&cond);
            break;
        }

        /* ── 함수 호출 ──────────────────────────────────────── */
        case OP_CALL: {
            int argc  = arg;
            /* 스택: [함수, arg0, arg1, ..., argN] */
            KcVal func_val = vm->stack[vm->stack_top - argc - 1];

            KcChunk *callee = NULL;
            if (func_val.type == VAL_FUNC)    callee = func_val.u.func;
            else if (func_val.type == VAL_CLOSURE) callee = func_val.u.closure->chunk;

            if (!callee) {
                VM_ERROR(vm, "호출할 수 없는 값");
                goto vm_error;
            }
            if (vm->frame_count >= KVM_FRAME_MAX) {
                VM_ERROR(vm, "호출 스택 초과");
                goto vm_error;
            }

            /* 새 프레임 설정 */
            KcFrame *new_frame = &vm->frames[vm->frame_count++];
            memset(new_frame, 0, sizeof(KcFrame));
            new_frame->chunk       = callee;
            new_frame->ip          = 0;
            new_frame->try_catch_ip = -1;
            new_frame->stack_base  = vm->stack_top;

            /* 인수 → 지역 변수 슬롯 복사 */
            int base = vm->stack_top - argc;
            for (int i = 0; i < argc && i < KVM_LOCALS_MAX; i++)
                new_frame->locals[i] = vm->stack[base + i];
            new_frame->local_count = argc;

            /* 스택에서 함수+인수 제거 */
            for (int i = 0; i < argc + 1; i++)
                vm->stack_top--;

            break;
        }

        case OP_CALL_BUILTIN: {
            /* 호출 전에 인수들이 이미 스택에 push된 상태 */
            /* 내장 함수는 인수 개수를 ID에서 유추 불가 → 스택 문맥 사용 */
            /* 간소 구현: 0~4개 인수 허용 */
            KcVal args[8];
            int   argc_b = 0;
            /* 내장 함수 호출 시 인수는 이미 push된 상태 */
            /* 인수 개수를 알기 어려우므로 스택 고정 슬롯 방식 사용 */
            /* → 실제 구현: 컴파일러가 인수 개수를 arg 상위 16비트에 인코딩 */
            /* 간소화: arg = (builtin_id & 0xFF) | (argc << 8) */
            int bid   = arg & 0xFF;
            argc_b    = (arg >> 8) & 0xFF;
            for (int i = argc_b - 1; i >= 0; i--)
                args[i] = POP(vm);
            KcVal result = run_builtin(vm, bid, args, argc_b);
            PUSH(vm, result);
            for (int i = 0; i < argc_b; i++) kc_val_free(&args[i]);
            break;
        }

        case OP_RETURN: {
            KcVal ret = POP(vm);
            vm->frame_count--;
            /* 스택 정리 */
            while (vm->stack_top > FRAME.stack_base)
                { KcVal v = POP(vm); kc_val_free(&v); }
            PUSH(vm, ret);
            if (vm->frame_count == 0) goto vm_done;
            break;
        }
        case OP_RETURN_VOID: {
            vm->frame_count--;
            while (vm->stack_top > FRAME.stack_base)
                { KcVal v = POP(vm); kc_val_free(&v); }
            PUSH(vm, kc_val_null());
            if (vm->frame_count == 0) goto vm_done;
            break;
        }

        /* ── 배열 ───────────────────────────────────────────── */
        case OP_MAKE_ARRAY: {
            KcArray *arr = (KcArray *)xmalloc(sizeof(KcArray));
            arr->cap = arg + 1;
            arr->len = arg;
            arr->ref_count = 1;
            arr->items = (KcVal *)xmalloc(arr->cap * sizeof(KcVal));
            for (int i = arg - 1; i >= 0; i--)
                arr->items[i] = POP(vm);
            KcVal r; r.type = VAL_ARRAY; r.u.arr = arr;
            PUSH(vm, r);
            break;
        }
        case OP_ARRAY_LEN: {
            KcVal a = PEEK(vm, 0);
            if (a.type == VAL_ARRAY && a.u.arr)
                PUSH(vm, kc_val_int((int64_t)a.u.arr->len));
            else if (a.type == VAL_STR && a.u.sval)
                PUSH(vm, kc_val_int((int64_t)strlen(a.u.sval)));
            else
                PUSH(vm, kc_val_int(0));
            break;
        }
        case OP_ARRAY_GET: {
            KcVal idx = POP(vm), arr = POP(vm);
            if (arr.type == VAL_ARRAY && arr.u.arr) {
                int64_t i = (idx.type==VAL_INT) ? idx.u.ival : (int64_t)idx.u.fval;
                if (i < 0) i += arr.u.arr->len;
                if (i >= 0 && i < arr.u.arr->len)
                    PUSH(vm, arr.u.arr->items[(int)i]);
                else
                    PUSH(vm, kc_val_null());
            } else {
                PUSH(vm, kc_val_null());
            }
            break;
        }
        case OP_ARRAY_SET: {
            KcVal idx = POP(vm), arr = POP(vm), val = POP(vm);
            if (arr.type == VAL_ARRAY && arr.u.arr) {
                int64_t i = (idx.type==VAL_INT) ? idx.u.ival : (int64_t)idx.u.fval;
                if (i >= 0 && i < arr.u.arr->len) {
                    kc_val_free(&arr.u.arr->items[(int)i]);
                    arr.u.arr->items[(int)i] = val;
                }
            }
            break;
        }

        /* ── 사전 ───────────────────────────────────────────── */
        case OP_MAKE_DICT: {
            KcDict *dict = (KcDict *)xmalloc(sizeof(KcDict));
            dict->len = arg;
            dict->cap = arg + 1;
            dict->ref_count = 1;
            dict->entries = (KcDictEntry *)xmalloc(dict->cap * sizeof(KcDictEntry));
            for (int i = arg - 1; i >= 0; i--) {
                KcVal v = POP(vm);
                KcVal k = POP(vm);
                dict->entries[i].key = (k.type==VAL_STR) ? xstrdup(k.u.sval) : xstrdup("?");
                dict->entries[i].val = v;
                kc_val_free(&k);
            }
            KcVal r; r.type = VAL_DICT; r.u.dict = dict;
            PUSH(vm, r);
            break;
        }

        /* ── 출력 (단축) ────────────────────────────────────── */
        case OP_PRINT: {
            KcVal v = POP(vm);
            kc_val_print(&v);
            KC_PRINTF("\n");
            kc_val_free(&v);
            break;
        }
        case OP_PRINT_RAW: {
            KcVal v = POP(vm);
            kc_val_print(&v);
            kc_val_free(&v);
            break;
        }
        case OP_INPUT: {
            char buf[1024];
            if (fgets(buf, sizeof(buf), stdin)) {
                size_t l = strlen(buf);
                if (l > 0 && buf[l-1] == '\n') buf[l-1] = '\0';
                PUSH(vm, kc_val_str(buf));
            } else {
                PUSH(vm, kc_val_str(""));
            }
            break;
        }

        /* ── 클로저 생성 ────────────────────────────────────── */
        case OP_MAKE_CLOSURE: {
            KcVal cv = const_to_val(vm, CHUNK, arg);
            if (cv.type == VAL_FUNC) {
                KcClosure *cl = (KcClosure *)xmalloc(sizeof(KcClosure));
                cl->chunk = cv.u.func;
                cl->upval_count = 0;
                cl->ref_count = 1;
                KcVal r; r.type = VAL_CLOSURE; r.u.closure = cl;
                PUSH(vm, r);
            } else {
                PUSH(vm, cv);
            }
            break;
        }

        /* ── 예외 ───────────────────────────────────────────── */
        case OP_TRY_BEGIN:
            FRAME.try_catch_ip = arg;
            break;
        case OP_TRY_END:
            FRAME.try_catch_ip = -1;
            break;
        case OP_RAISE: {
            KcVal msg = POP(vm);
            fprintf(stderr, "[오류] ");
            kc_val_print(&msg);
            fprintf(stderr, "\n");
            kc_val_free(&msg);
            if (FRAME.try_catch_ip >= 0) {
                IP = FRAME.try_catch_ip;
                FRAME.try_catch_ip = -1;
            } else {
                VM_ERROR(vm, "처리되지 않은 오류");
                goto vm_error;
            }
            break;
        }

        /* ── 형변환 ─────────────────────────────────────────── */
        case OP_TO_INT: {
            KcVal a = POP(vm);
            KcVal r = run_builtin(vm, BUILTIN_INT, &a, 1);
            PUSH(vm, r);
            kc_val_free(&a);
            break;
        }
        case OP_TO_FLOAT: {
            KcVal a = POP(vm);
            KcVal r = run_builtin(vm, BUILTIN_FLOAT, &a, 1);
            PUSH(vm, r);
            kc_val_free(&a);
            break;
        }
        case OP_TO_STR: {
            KcVal a = POP(vm);
            KcVal r = run_builtin(vm, BUILTIN_STR, &a, 1);
            PUSH(vm, r);
            kc_val_free(&a);
            break;
        }
        case OP_TO_BOOL: {
            KcVal a = POP(vm);
            PUSH(vm, kc_val_bool(kc_val_truthy(&a)));
            kc_val_free(&a);
            break;
        }

        case OP_HALT:
            goto vm_done;

        default:
            VM_ERROR(vm, "알 수 없는 opcode: 0x%02X", op);
            goto vm_error;
        } /* switch */
    } /* while */

vm_done:
    vm->frame_count = 0;
    return vm->had_error ? -1 : 0;

vm_error:
    fprintf(stderr, "[KVM 오류] %s\n", vm->error_msg);
    vm->frame_count = 0;
    return -1;

    #undef FRAME
    #undef CHUNK
    #undef IP
}
