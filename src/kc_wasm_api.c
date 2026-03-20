/*
 * kc_wasm_api.c — Kcode WebAssembly 브릿지 구현
 * version : v16.5.1
 *
 * Emscripten으로 컴파일되는 JS 노출 함수 5종과
 * 내부 출력 캡처 버퍼를 구현한다.
 *
 * 컴파일:
 *   emcmake cmake .. -DBUILD_WASM=ON
 *   emmake make kcode_vm
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "klexer.h"
#include "kparser.h"
#include "kc_bcgen.h"
#include "kc_bytecode.h"
#include "kc_vm.h"
#include "kc_wasm_api.h"

/* ================================================================
 *  버전
 * ================================================================ */
#define KC_WASM_VERSION "v16.5.1"

/* ================================================================
 *  내부 출력 캡처 버퍼
 * ================================================================ */
#define WASM_OUTPUT_BUF_SIZE (65536)

static char  g_output_buf[WASM_OUTPUT_BUF_SIZE];
static int   g_output_pos = 0;

void kc_wasm_output_reset(void) {
    g_output_pos    = 0;
    g_output_buf[0] = '\0';
}

void kc_wasm_output_append(const char *str) {
    if (!str) return;
    int len       = (int)strlen(str);
    int remaining = WASM_OUTPUT_BUF_SIZE - g_output_pos - 1;
    if (remaining <= 0) return;
    if (len > remaining) len = remaining;
    memcpy(g_output_buf + g_output_pos, str, (size_t)len);
    g_output_pos += len;
    g_output_buf[g_output_pos] = '\0';
}

const char *kc_wasm_output_get(void) {
    return g_output_buf;
}

/* ================================================================
 *  반환 JSON 버퍼 (함수 호출마다 재사용)
 * ================================================================ */
static char *g_result_buf = NULL;

static char *result_set(const char *json) {
    free(g_result_buf);
    g_result_buf = strdup(json ? json : "{}");
    return g_result_buf;
}

/* ================================================================
 *  JSON 문자열 이스케이프 헬퍼
 * ================================================================ */
static void json_escape_append(char *dst, int *pos, int cap, const char *src) {
    for (; src && *src; src++) {
        unsigned char c = (unsigned char)*src;
        if (*pos >= cap - 8) break;
        switch (c) {
            case '"':  dst[(*pos)++]='\\'; dst[(*pos)++]='"';  break;
            case '\\': dst[(*pos)++]='\\'; dst[(*pos)++]='\\'; break;
            case '\n': dst[(*pos)++]='\\'; dst[(*pos)++]='n';  break;
            case '\r': dst[(*pos)++]='\\'; dst[(*pos)++]='r';  break;
            case '\t': dst[(*pos)++]='\\'; dst[(*pos)++]='t';  break;
            default:
                if (c < 0x20) {
                    *pos += snprintf(dst+*pos, (size_t)(cap-*pos), "\\u%04x", c);
                } else {
                    dst[(*pos)++] = (char)c;
                }
        }
    }
    dst[*pos] = '\0';
}

/* JSON 문자열 이스케이프하여 malloc 반환 */
static char *json_escape_str(const char *src) {
    if (!src) return strdup("");
    size_t slen = strlen(src);
    size_t cap  = slen * 6 + 8;
    char *buf   = (char *)malloc(cap);
    if (!buf) return strdup("");
    int pos = 0;
    json_escape_append(buf, &pos, (int)cap, src);
    return buf;
}

/* ================================================================
 *  공개 API
 * ================================================================ */

WASM_EXPORT const char *kc_wasm_version(void) {
    return KC_WASM_VERSION;
}

WASM_EXPORT void kc_wasm_free(char *ptr) {
    free(ptr);
}

/* ================================================================
 *  kc_wasm_run(source)
 *  소스 → 렉싱 → 파싱 → 바이트코드 생성 → KVM 실행
 * ================================================================ */
WASM_EXPORT char *kc_wasm_run(const char *source) {
    kc_wasm_output_reset();

    if (!source || !*source) {
        return result_set("{\"success\":true,\"output\":\"\",\"error\":\"\"}");
    }

    /* 1. 렉싱 + 파싱 */
    Lexer lx;
    lexer_init(&lx, source, strlen(source));
    Parser par;
    parser_init(&par, &lx);
    Node *ast = parser_parse(&par);

    if (par.had_error || !ast) {
        if (ast) node_free(ast);
        return result_set("{\"success\":false,\"output\":\"\","
                          "\"error\":\"파싱 오류가 발생했습니다\"}");
    }

    /* 2. 바이트코드 생성 */
    KcModule *mod = kc_bcgen_compile(ast, "<wasm>");
    node_free(ast);

    if (!mod) {
        return result_set("{\"success\":false,\"output\":\"\","
                          "\"error\":\"바이트코드 생성 오류\"}");
    }

    /* 3. KVM 실행 */
    KcVM vm;
    kc_vm_init(&vm);
    int run_ok = kc_vm_run(&vm, mod);
    int had_err  = vm.had_error;
    char errmsg[512];
    strncpy(errmsg, vm.error_msg, sizeof(errmsg) - 1);
    errmsg[sizeof(errmsg)-1] = '\0';
    kc_vm_free(&vm);
    kc_module_free(mod);

    const char *output = kc_wasm_output_get();
    char *esc_out = json_escape_str(output);
    char *esc_err = json_escape_str(had_err ? errmsg : "");

    char *json = (char *)malloc(strlen(esc_out) + strlen(esc_err) + 128);
    if (run_ok == 0 && !had_err) {
        sprintf(json, "{\"success\":true,\"output\":\"%s\",\"error\":\"\"}", esc_out);
    } else {
        sprintf(json, "{\"success\":false,\"output\":\"%s\",\"error\":\"%s\"}",
                esc_out, esc_err);
    }
    free(esc_out); free(esc_err);
    char *r = result_set(json);
    free(json);
    return r;
}

/* ================================================================
 *  kc_wasm_compile(source)
 *  소스 → 바이트코드 .kbc 파일 생성 → Base64 반환
 *  (직렬화: kc_module_write로 임시 파일에 쓴 뒤 읽기)
 *  WASM FILESYSTEM=0 환경에서는 base64 미지원으로 간략 반환
 * ================================================================ */
WASM_EXPORT char *kc_wasm_compile(const char *source) {
    if (!source || !*source) {
        return result_set("{\"success\":false,\"bytecode_b64\":\"\","
                          "\"errors\":[\"빈 소스코드\"]}");
    }

    Lexer lx;
    lexer_init(&lx, source, strlen(source));
    Parser par;
    parser_init(&par, &lx);
    Node *ast = parser_parse(&par);

    if (par.had_error || !ast) {
        if (ast) node_free(ast);
        return result_set("{\"success\":false,\"bytecode_b64\":\"\","
                          "\"errors\":[\"파싱 오류\"]}");
    }

    KcModule *mod = kc_bcgen_compile(ast, "<wasm>");
    node_free(ast);

    if (!mod) {
        return result_set("{\"success\":false,\"bytecode_b64\":\"\","
                          "\"errors\":[\"컴파일 오류\"]}");
    }

    /* WASM 환경에서 파일 직렬화 대신 성공 여부만 반환 */
    kc_module_free(mod);
    return result_set("{\"success\":true,\"bytecode_b64\":\"\","
                      "\"errors\":[]}");
}

/* ================================================================
 *  kc_wasm_exec_bc(bytecode_b64)
 *  WASM FILESYSTEM=0 환경에서는 직렬화 미지원 — 오류 반환
 * ================================================================ */
WASM_EXPORT char *kc_wasm_exec_bc(const char *bytecode_b64) {
    (void)bytecode_b64;
    return result_set("{\"success\":false,\"output\":\"\","
                      "\"error\":\"WASM 환경에서 바이트코드 직렬화는 미지원입니다. kc_wasm_run()을 사용하세요.\"}");
}

/* ================================================================
 *  kc_wasm_lex(source)
 *  IDE 토큰 하이라이팅용 토큰 목록 JSON 반환
 * ================================================================ */
WASM_EXPORT char *kc_wasm_lex(const char *source) {
    if (!source) return result_set("{\"tokens\":[]}");

    size_t cap = 65536;
    char *buf  = (char *)malloc(cap);
    if (!buf) return result_set("{\"tokens\":[]}");

    int pos = 0;

#define BAPPEND(s) do { \
    const char *_s = (s); size_t _l = strlen(_s); \
    if ((size_t)pos + _l + 2 >= cap) { cap *= 2; buf = (char *)realloc(buf, cap); } \
    memcpy(buf + pos, _s, _l); pos += (int)_l; buf[pos] = '\0'; \
} while(0)

    BAPPEND("{\"tokens\":[");

    Lexer lx;
    lexer_init(&lx, source, strlen(source));
    int first = 1;
    for (;;) {
        Token tok = lexer_next(&lx);
        if (tok.type == TOK_EOF || tok.type == TOK_ERROR) break;

        char val[256];
        token_to_str(&tok, val, sizeof(val));
        char *esc = json_escape_str(val);

        char entry[512];
        snprintf(entry, sizeof(entry),
                 "%s{\"type\":\"%s\",\"value\":\"%s\",\"line\":%d,\"col\":%d}",
                 first ? "" : ",",
                 token_type_name(tok.type), esc, tok.line, tok.col);
        free(esc);
        first = 0;
        BAPPEND(entry);
    }
    BAPPEND("]}");

#undef BAPPEND

    char *r = result_set(buf);
    free(buf);
    return r;
}

/* ================================================================
 *  kc_wasm_parse(source)
 *  IDE 구문 분석용 — 파싱 성공/실패 + 에러 수 반환
 * ================================================================ */
WASM_EXPORT char *kc_wasm_parse(const char *source) {
    if (!source || !*source) {
        return result_set("{\"success\":true,\"error_count\":0,\"errors\":[]}");
    }

    Lexer lx;
    lexer_init(&lx, source, strlen(source));
    Parser par;
    parser_init(&par, &lx);
    Node *ast = parser_parse(&par);

    char json[256];
    if (!par.had_error && ast) {
        node_free(ast);
        snprintf(json, sizeof(json),
                 "{\"success\":true,\"error_count\":0,\"errors\":[]}");
    } else {
        if (ast) node_free(ast);
        snprintf(json, sizeof(json),
                 "{\"success\":false,\"error_count\":%d,\"errors\":[\"파싱 오류\"]}",
                 par.error_count > 0 ? par.error_count : 1);
    }
    return result_set(json);
}
