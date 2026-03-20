/*
 * kc_bytecode.c  —  Kcode 바이트코드 청크/모듈 관리 + .kbc 직렬화
 * version : v16.0.0
 *
 * MIT License
 * zerojat7
 */

#include "kc_bytecode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ================================================================
 *  내부 유틸
 * ================================================================ */
static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "[KBC] out of memory\n"); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) { fprintf(stderr, "[KBC] out of memory\n"); exit(1); }
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
 *  KcChunk 생성 / 해제
 * ================================================================ */
KcChunk *kc_chunk_new(const char *name, int arity)
{
    KcChunk *c = (KcChunk *)xmalloc(sizeof(KcChunk));
    memset(c, 0, sizeof(KcChunk));
    c->name  = xstrdup(name ? name : "__main__");
    c->arity = arity;

    /* 초기 코드 버퍼 */
    c->code_cap = 64;
    c->code     = (KcInstr *)xmalloc(c->code_cap * sizeof(KcInstr));

    /* 상수 풀 */
    c->const_cap = 16;
    c->consts    = (KcConst *)xmalloc(c->const_cap * sizeof(KcConst));

    /* 지역 변수 이름 */
    c->local_names = (char **)xmalloc(KBC_MAX_LOCALS * sizeof(char *));
    memset(c->local_names, 0, KBC_MAX_LOCALS * sizeof(char *));

    /* 업밸류 */
    c->upvals = (KcUpvalDesc *)xmalloc(KBC_MAX_UPVALS * sizeof(KcUpvalDesc));
    memset(c->upvals, 0, KBC_MAX_UPVALS * sizeof(KcUpvalDesc));

    /* 서브 청크 */
    c->sub_cap   = 4;
    c->sub_chunks = (KcChunk **)xmalloc(c->sub_cap * sizeof(KcChunk *));
    c->sub_count = 0;

    return c;
}

void kc_chunk_free(KcChunk *chunk)
{
    if (!chunk) return;

    free(chunk->name);
    free(chunk->code);

    /* 상수 풀 해제 */
    for (int i = 0; i < chunk->const_len; i++) {
        if (chunk->consts[i].type == KC_CONST_STR)
            free(chunk->consts[i].u.sval);
    }
    free(chunk->consts);

    /* 지역 변수 이름 해제 */
    for (int i = 0; i < chunk->local_count; i++)
        free(chunk->local_names[i]);
    free(chunk->local_names);

    free(chunk->upvals);

    /* 서브 청크 재귀 해제 */
    for (int i = 0; i < chunk->sub_count; i++)
        kc_chunk_free(chunk->sub_chunks[i]);
    free(chunk->sub_chunks);

    free(chunk);
}

/* ================================================================
 *  명령어 추가
 * ================================================================ */
int kc_chunk_emit(KcChunk *chunk, uint8_t op, int32_t arg, uint32_t line)
{
    if (chunk->code_len >= chunk->code_cap) {
        chunk->code_cap *= 2;
        chunk->code = (KcInstr *)xrealloc(chunk->code,
                      chunk->code_cap * sizeof(KcInstr));
    }
    int idx = chunk->code_len++;
    chunk->code[idx].op   = op;
    chunk->code[idx].arg  = arg;
    chunk->code[idx].line = line;
    return idx;
}

int kc_chunk_emit_nop(KcChunk *chunk)
{
    return kc_chunk_emit(chunk, OP_NOP, 0, 0);
}

void kc_chunk_patch(KcChunk *chunk, int idx, int32_t new_arg)
{
    assert(idx >= 0 && idx < chunk->code_len);
    chunk->code[idx].arg = new_arg;
    /* NOP 자리를 실제 점프로 변경할 경우 op도 교체 가능 */
    if (chunk->code[idx].op == OP_NOP)
        chunk->code[idx].op = OP_JUMP_FALSE; /* 기본값; 호출자가 필요시 직접 변경 */
}

/* ================================================================
 *  상수 풀 추가
 * ================================================================ */
static int const_grow(KcChunk *chunk)
{
    if (chunk->const_len >= chunk->const_cap) {
        chunk->const_cap *= 2;
        chunk->consts = (KcConst *)xrealloc(chunk->consts,
                        chunk->const_cap * sizeof(KcConst));
    }
    return chunk->const_len++;
}

int kc_chunk_add_const_int(KcChunk *chunk, int64_t ival)
{
    /* 중복 검색 */
    for (int i = 0; i < chunk->const_len; i++)
        if (chunk->consts[i].type == KC_CONST_INT &&
            chunk->consts[i].u.ival == ival) return i;
    int idx = const_grow(chunk);
    chunk->consts[idx].type  = KC_CONST_INT;
    chunk->consts[idx].u.ival = ival;
    return idx;
}

int kc_chunk_add_const_float(KcChunk *chunk, double fval)
{
    for (int i = 0; i < chunk->const_len; i++)
        if (chunk->consts[i].type == KC_CONST_FLOAT &&
            chunk->consts[i].u.fval == fval) return i;
    int idx = const_grow(chunk);
    chunk->consts[idx].type   = KC_CONST_FLOAT;
    chunk->consts[idx].u.fval = fval;
    return idx;
}

int kc_chunk_add_const_str(KcChunk *chunk, const char *s)
{
    if (!s) s = "";
    for (int i = 0; i < chunk->const_len; i++)
        if (chunk->consts[i].type == KC_CONST_STR &&
            strcmp(chunk->consts[i].u.sval, s) == 0) return i;
    int idx = const_grow(chunk);
    chunk->consts[idx].type   = KC_CONST_STR;
    chunk->consts[idx].u.sval = xstrdup(s);
    return idx;
}

int kc_chunk_add_const_bool(KcChunk *chunk, int bval)
{
    bval = bval ? 1 : 0;
    for (int i = 0; i < chunk->const_len; i++)
        if (chunk->consts[i].type == KC_CONST_BOOL &&
            chunk->consts[i].u.bval == bval) return i;
    int idx = const_grow(chunk);
    chunk->consts[idx].type   = KC_CONST_BOOL;
    chunk->consts[idx].u.bval = bval;
    return idx;
}

int kc_chunk_add_const_null(KcChunk *chunk)
{
    for (int i = 0; i < chunk->const_len; i++)
        if (chunk->consts[i].type == KC_CONST_NULL) return i;
    int idx = const_grow(chunk);
    chunk->consts[idx].type = KC_CONST_NULL;
    return idx;
}

int kc_chunk_add_sub(KcChunk *chunk, KcChunk *sub)
{
    if (chunk->sub_count >= chunk->sub_cap) {
        chunk->sub_cap *= 2;
        chunk->sub_chunks = (KcChunk **)xrealloc(chunk->sub_chunks,
                            chunk->sub_cap * sizeof(KcChunk *));
    }
    int idx = chunk->sub_count++;
    chunk->sub_chunks[idx] = sub;
    /* 상수 풀에 함수 참조 등록 */
    int cidx = const_grow(chunk);
    chunk->consts[cidx].type         = KC_CONST_FUNC;
    chunk->consts[cidx].u.func_idx   = idx;
    return cidx; /* 상수 풀 인덱스 반환 */
}

/* ================================================================
 *  디스어셈블러
 * ================================================================ */
const char *kc_opcode_name(uint8_t op)
{
    switch ((KcOpCode)op) {
        case OP_NOP:          return "NOP";
        case OP_PUSH_INT:     return "PUSH_INT";
        case OP_PUSH_FLOAT:   return "PUSH_FLOAT";
        case OP_PUSH_STR:     return "PUSH_STR";
        case OP_PUSH_BOOL:    return "PUSH_BOOL";
        case OP_PUSH_NULL:    return "PUSH_NULL";
        case OP_POP:          return "POP";
        case OP_DUP:          return "DUP";
        case OP_SWAP:         return "SWAP";
        case OP_LOAD_LOCAL:   return "LOAD_LOCAL";
        case OP_STORE_LOCAL:  return "STORE_LOCAL";
        case OP_LOAD_GLOBAL:  return "LOAD_GLOBAL";
        case OP_STORE_GLOBAL: return "STORE_GLOBAL";
        case OP_LOAD_CONST:   return "LOAD_CONST";
        case OP_LOAD_UPVAL:   return "LOAD_UPVAL";
        case OP_STORE_UPVAL:  return "STORE_UPVAL";
        case OP_ADD:          return "ADD";
        case OP_SUB:          return "SUB";
        case OP_MUL:          return "MUL";
        case OP_DIV:          return "DIV";
        case OP_MOD:          return "MOD";
        case OP_POW:          return "POW";
        case OP_NEG:          return "NEG";
        case OP_MATMUL:       return "MATMUL";
        case OP_BIT_AND:      return "BIT_AND";
        case OP_BIT_OR:       return "BIT_OR";
        case OP_BIT_XOR:      return "BIT_XOR";
        case OP_BIT_NOT:      return "BIT_NOT";
        case OP_SHL:          return "SHL";
        case OP_SHR:          return "SHR";
        case OP_EQ:           return "EQ";
        case OP_NE:           return "NE";
        case OP_LT:           return "LT";
        case OP_LE:           return "LE";
        case OP_GT:           return "GT";
        case OP_GE:           return "GE";
        case OP_AND:          return "AND";
        case OP_OR:           return "OR";
        case OP_NOT:          return "NOT";
        case OP_JUMP:         return "JUMP";
        case OP_JUMP_FALSE:   return "JUMP_FALSE";
        case OP_JUMP_TRUE:    return "JUMP_TRUE";
        case OP_JUMP_BACK:    return "JUMP_BACK";
        case OP_CALL:         return "CALL";
        case OP_CALL_BUILTIN: return "CALL_BUILTIN";
        case OP_RETURN:       return "RETURN";
        case OP_RETURN_VOID:  return "RETURN_VOID";
        case OP_TAIL_CALL:    return "TAIL_CALL";
        case OP_MAKE_ARRAY:   return "MAKE_ARRAY";
        case OP_MAKE_DICT:    return "MAKE_DICT";
        case OP_ARRAY_GET:    return "ARRAY_GET";
        case OP_ARRAY_SET:    return "ARRAY_SET";
        case OP_ARRAY_LEN:    return "ARRAY_LEN";
        case OP_ARRAY_PUSH:   return "ARRAY_PUSH";
        case OP_NEW_OBJ:      return "NEW_OBJ";
        case OP_GET_FIELD:    return "GET_FIELD";
        case OP_SET_FIELD:    return "SET_FIELD";
        case OP_GET_METHOD:   return "GET_METHOD";
        case OP_MAKE_CLOSURE: return "MAKE_CLOSURE";
        case OP_CLOSE_UPVAL:  return "CLOSE_UPVAL";
        case OP_TRY_BEGIN:    return "TRY_BEGIN";
        case OP_TRY_END:      return "TRY_END";
        case OP_RAISE:        return "RAISE";
        case OP_PRINT:        return "PRINT";
        case OP_PRINT_RAW:    return "PRINT_RAW";
        case OP_INPUT:        return "INPUT";
        case OP_TO_INT:       return "TO_INT";
        case OP_TO_FLOAT:     return "TO_FLOAT";
        case OP_TO_STR:       return "TO_STR";
        case OP_TO_BOOL:      return "TO_BOOL";
        case OP_HALT:         return "HALT";
        default:              return "???";
    }
}

const char *kc_builtin_name(int id)
{
    static const char *names[] = {
        /* 0-4  기본 I/O */
        "출력","출력없이","입력","길이","범위",
        /* 5-8  형변환 */
        "정수","실수","글자","논리",
        /* 9-21 수학 */
        "제곱근","절댓값","최대","최소","사인","코사인","탄젠트",
        "로그","자연로그","지수","반올림","내림","올림",
        /* 22-24 배열 */
        "추가","배열정렬","배열뒤집기",
        /* 25-46 글자 19종 */
        "대문자","소문자","포함","공백제거","대체","글자길이",
        "자르기","분할","합치기","반복글자","역순",
        "위치","시작","끝확인","비교","제목식","한번대체",
        "앞공백제거","뒤공백제거","반복확인","분석","포맷",
        /* 47-49 AI 활성함수 */
        "시그모이드","렐루","쌍곡탄젠트",
        /* 50-56 AI/수열 */
        "평균제곱오차","교차엔트로피","소프트맥스","위치인코딩",
        "등비수열합","등차수열합","점화식값",
        /* 57-68 통계+관계 */
        "평균","표준편차","합계","분산","중앙값","최빈값",
        "누적합","공분산","상관계수","정규화","표준화","호감도",
        /* 69-85 파일 17종 */
        "파일열기","파일닫기","파일읽기","파일줄읽기","파일쓰기","파일줄쓰기",
        "파일있음","파일크기","파일목록","파일이름","파일확장자",
        "폴더만들기","파일지우기","파일복사","파일이동",
        "파일전체읽기","파일전체쓰기",
        /* 86-98 텐서 13종 */
        "텐서생성","텐서형태","텐서크기","텐서차원","텐서합","텐서평균",
        "텐서최대","텐서최소","텐서행렬곱","텐서전치","텐서변형",
        "텐서평탄화","텐서복사",
        /* 99-101 자동미분+MCP */
        "역전파","기울기초기화","MCP오류"
    };
    if (id < 0 || id >= BUILTIN_COUNT) return "알수없음";
    return names[id];
}

static void dump_const(const KcConst *c)
{
    switch (c->type) {
        case KC_CONST_INT:   printf("정수(%lld)", (long long)c->u.ival); break;
        case KC_CONST_FLOAT: printf("실수(%g)",   c->u.fval); break;
        case KC_CONST_STR:   printf("글자(\"%s\")", c->u.sval); break;
        case KC_CONST_BOOL:  printf("논리(%s)",   c->u.bval ? "참":"거짓"); break;
        case KC_CONST_NULL:  printf("없음"); break;
        case KC_CONST_FUNC:  printf("함수[%d]", c->u.func_idx); break;
    }
}

void kc_chunk_dump(const KcChunk *chunk)
{
    printf("=== 청크: %s (인수=%d, 지역=%d) ===\n",
           chunk->name, chunk->arity, chunk->local_count);
    printf("상수 풀 (%d):\n", chunk->const_len);
    for (int i = 0; i < chunk->const_len; i++) {
        printf("  [%3d] ", i);
        dump_const(&chunk->consts[i]);
        printf("\n");
    }
    printf("코드 (%d 명령어):\n", chunk->code_len);
    for (int i = 0; i < chunk->code_len; i++) {
        KcInstr *ins = &chunk->code[i];
        printf("  %04d  %-16s %5d    ; 줄 %u\n",
               i, kc_opcode_name(ins->op), ins->arg, ins->line);
    }
    /* 서브 청크 재귀 덤프 */
    for (int i = 0; i < chunk->sub_count; i++) {
        printf("\n");
        kc_chunk_dump(chunk->sub_chunks[i]);
    }
}

/* ================================================================
 *  모듈 생성 / 해제
 * ================================================================ */
KcModule *kc_module_new(const char *source_name, KcChunk *main)
{
    KcModule *m = (KcModule *)xmalloc(sizeof(KcModule));
    m->magic       = KBC_MAGIC;
    m->ver_maj     = KBC_VERSION_MAJ;
    m->ver_min     = KBC_VERSION_MIN;
    m->source_name = xstrdup(source_name ? source_name : "");
    m->main_chunk  = main;
    return m;
}

void kc_module_free(KcModule *mod)
{
    if (!mod) return;
    free(mod->source_name);
    kc_chunk_free(mod->main_chunk);
    free(mod);
}

/* ================================================================
 *  .kbc 직렬화 헬퍼
 * ================================================================ */
static void write_u8(FILE *f, uint8_t v)   { fwrite(&v, 1, 1, f); }
static void write_u16(FILE *f, uint16_t v)  { fwrite(&v, 2, 1, f); }
static void write_u32(FILE *f, uint32_t v)  { fwrite(&v, 4, 1, f); }
static void write_i32(FILE *f, int32_t v)   { fwrite(&v, 4, 1, f); }
static void write_i64(FILE *f, int64_t v)   { fwrite(&v, 8, 1, f); }
static void write_f64(FILE *f, double v)    { fwrite(&v, 8, 1, f); }

static void write_str(FILE *f, const char *s)
{
    if (!s) s = "";
    uint32_t len = (uint32_t)strlen(s);
    write_u32(f, len);
    fwrite(s, 1, len, f);
}

static void write_chunk(FILE *f, const KcChunk *c)
{
    write_str(f, c->name);
    write_i32(f, c->arity);
    write_i32(f, c->local_count);
    write_i32(f, c->upval_count);

    /* 상수 풀 */
    write_i32(f, c->const_len);
    for (int i = 0; i < c->const_len; i++) {
        const KcConst *cc = &c->consts[i];
        write_u8(f, (uint8_t)cc->type);
        switch (cc->type) {
            case KC_CONST_INT:   write_i64(f, cc->u.ival); break;
            case KC_CONST_FLOAT: write_f64(f, cc->u.fval); break;
            case KC_CONST_STR:   write_str(f, cc->u.sval); break;
            case KC_CONST_BOOL:  write_u8(f, (uint8_t)cc->u.bval); break;
            case KC_CONST_NULL:  break;
            case KC_CONST_FUNC:  write_i32(f, cc->u.func_idx); break;
        }
    }

    /* 업밸류 기술자 */
    write_i32(f, c->upval_count);
    for (int i = 0; i < c->upval_count; i++) {
        write_u8(f, c->upvals[i].is_local);
        write_u8(f, c->upvals[i].index);
    }

    /* 명령어 */
    write_i32(f, c->code_len);
    for (int i = 0; i < c->code_len; i++) {
        write_u8(f,  c->code[i].op);
        write_i32(f, c->code[i].arg);
        write_u32(f, c->code[i].line);
    }

    /* 서브 청크 */
    write_i32(f, c->sub_count);
    for (int i = 0; i < c->sub_count; i++)
        write_chunk(f, c->sub_chunks[i]);
}

int kc_module_write(const KcModule *mod, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }

    write_u32(f, mod->magic);
    write_u16(f, mod->ver_maj);
    write_u16(f, mod->ver_min);
    write_str(f, mod->source_name);
    write_chunk(f, mod->main_chunk);

    fclose(f);
    return 0;
}

/* ================================================================
 *  .kbc 역직렬화 헬퍼
 * ================================================================ */
static uint8_t  read_u8(FILE *f)  { uint8_t v;  fread(&v,1,1,f); return v; }
static uint16_t read_u16(FILE *f) { uint16_t v; fread(&v,2,1,f); return v; }
static uint32_t read_u32(FILE *f) { uint32_t v; fread(&v,4,1,f); return v; }
static int32_t  read_i32(FILE *f) { int32_t v;  fread(&v,4,1,f); return v; }
static int64_t  read_i64(FILE *f) { int64_t v;  fread(&v,8,1,f); return v; }
static double   read_f64(FILE *f) { double v;   fread(&v,8,1,f); return v; }

static char *read_str_alloc(FILE *f)
{
    uint32_t len = read_u32(f);
    char *s = (char *)xmalloc(len + 1);
    fread(s, 1, len, f);
    s[len] = '\0';
    return s;
}

static KcChunk *read_chunk(FILE *f)
{
    char *name = read_str_alloc(f);
    int arity  = read_i32(f);

    KcChunk *c = kc_chunk_new(name, arity);
    free(name);

    c->local_count = read_i32(f);
    c->upval_count = read_i32(f);

    /* 상수 풀 */
    int nconsts = read_i32(f);
    for (int i = 0; i < nconsts; i++) {
        uint8_t type = read_u8(f);
        switch ((KcConstType)type) {
            case KC_CONST_INT:   kc_chunk_add_const_int(c, read_i64(f)); break;
            case KC_CONST_FLOAT: kc_chunk_add_const_float(c, read_f64(f)); break;
            case KC_CONST_STR: {
                char *s = read_str_alloc(f);
                kc_chunk_add_const_str(c, s);
                free(s);
                break;
            }
            case KC_CONST_BOOL:  kc_chunk_add_const_bool(c, read_u8(f)); break;
            case KC_CONST_NULL:  kc_chunk_add_const_null(c); break;
            case KC_CONST_FUNC: {
                int fi = read_i32(f);
                int cidx = const_grow(c);
                c->consts[cidx].type = KC_CONST_FUNC;
                c->consts[cidx].u.func_idx = fi;
                break;
            }
        }
    }

    /* 업밸류 */
    int nupvals = read_i32(f);
    for (int i = 0; i < nupvals && i < KBC_MAX_UPVALS; i++) {
        c->upvals[i].is_local = read_u8(f);
        c->upvals[i].index    = read_u8(f);
    }

    /* 명령어 */
    int ncode = read_i32(f);
    for (int i = 0; i < ncode; i++) {
        uint8_t  op   = read_u8(f);
        int32_t  arg  = read_i32(f);
        uint32_t line = read_u32(f);
        kc_chunk_emit(c, op, arg, line);
    }

    /* 서브 청크 */
    int nsub = read_i32(f);
    for (int i = 0; i < nsub; i++) {
        KcChunk *sub = read_chunk(f);
        kc_chunk_add_sub(c, sub);
    }

    return c;
}

KcModule *kc_module_read(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }

    uint32_t magic = read_u32(f);
    if (magic != KBC_MAGIC) {
        fprintf(stderr, "[KBC] %s: 잘못된 파일 형식\n", path);
        fclose(f);
        return NULL;
    }
    uint16_t vmaj = read_u16(f);
    uint16_t vmin = read_u16(f);
    (void)vmaj; (void)vmin;

    char *sname = read_str_alloc(f);
    KcChunk *main = read_chunk(f);
    fclose(f);

    KcModule *mod = kc_module_new(sname, main);
    free(sname);
    return mod;
}
