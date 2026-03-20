/*
 * kc_mcp.c  —  Kcode MCP(Model Context Protocol) 런타임 구현
 * version : v14.0.0
 *
 * MIT License / zerojat7
 */

#define _POSIX_C_SOURCE 200809L  /* strdup, getline, ssize_t */
#include "kc_mcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ================================================================
 *  미니 JSON 파서
 * ================================================================ */

/* ── 생성 ── */
KcJson *kc_json_null(void) {
    KcJson *j = calloc(1, sizeof(KcJson));
    if (j) j->type = KC_JSON_NULL;
    return j;
}
KcJson *kc_json_bool(int b) {
    KcJson *j = calloc(1, sizeof(KcJson));
    if (!j) return NULL;
    j->type = KC_JSON_BOOL; j->as.bval = b ? 1 : 0;
    return j;
}
KcJson *kc_json_int(int64_t v) {
    KcJson *j = calloc(1, sizeof(KcJson));
    if (!j) return NULL;
    j->type = KC_JSON_INT; j->as.ival = v;
    return j;
}
KcJson *kc_json_float(double v) {
    KcJson *j = calloc(1, sizeof(KcJson));
    if (!j) return NULL;
    j->type = KC_JSON_FLOAT; j->as.fval = v;
    return j;
}
KcJson *kc_json_string(const char *s) {
    KcJson *j = calloc(1, sizeof(KcJson));
    if (!j) return NULL;
    j->type = KC_JSON_STRING;
    j->as.sval = s ? strdup(s) : strdup("");
    return j;
}
KcJson *kc_json_array(void) {
    KcJson *j = calloc(1, sizeof(KcJson));
    if (j) j->type = KC_JSON_ARRAY;
    return j;
}
KcJson *kc_json_object(void) {
    KcJson *j = calloc(1, sizeof(KcJson));
    if (j) j->type = KC_JSON_OBJECT;
    return j;
}

/* ── 조작 ── */
void kc_json_array_push(KcJson *arr, KcJson *val) {
    if (!arr || arr->type != KC_JSON_ARRAY || !val) return;
    if (arr->as.arr.count >= arr->as.arr.cap) {
        int newcap = arr->as.arr.cap ? arr->as.arr.cap * 2 : 8;
        arr->as.arr.items = realloc(arr->as.arr.items,
                                    sizeof(KcJson*) * (size_t)newcap);
        arr->as.arr.cap = newcap;
    }
    arr->as.arr.items[arr->as.arr.count++] = val;
}

void kc_json_object_set(KcJson *obj, const char *key, KcJson *val) {
    if (!obj || obj->type != KC_JSON_OBJECT || !key || !val) return;
    /* 기존 키 업데이트 */
    for (int i = 0; i < obj->as.obj.count; i++) {
        if (strcmp(obj->as.obj.keys[i], key) == 0) {
            kc_json_free(obj->as.obj.vals[i]);
            obj->as.obj.vals[i] = val;
            return;
        }
    }
    /* 새 키 추가 */
    if (obj->as.obj.count >= obj->as.obj.cap) {
        int newcap = obj->as.obj.cap ? obj->as.obj.cap * 2 : 8;
        obj->as.obj.keys = realloc(obj->as.obj.keys,
                                    sizeof(char*) * (size_t)newcap);
        obj->as.obj.vals = realloc(obj->as.obj.vals,
                                    sizeof(KcJson*) * (size_t)newcap);
        obj->as.obj.cap = newcap;
    }
    obj->as.obj.keys[obj->as.obj.count] = strdup(key);
    obj->as.obj.vals[obj->as.obj.count] = val;
    obj->as.obj.count++;
}

KcJson *kc_json_object_get(KcJson *obj, const char *key) {
    if (!obj || obj->type != KC_JSON_OBJECT || !key) return NULL;
    for (int i = 0; i < obj->as.obj.count; i++)
        if (strcmp(obj->as.obj.keys[i], key) == 0)
            return obj->as.obj.vals[i];
    return NULL;
}

/* ── 해제 ── */
void kc_json_free(KcJson *val) {
    if (!val) return;
    switch (val->type) {
    case KC_JSON_STRING:
        free(val->as.sval);
        break;
    case KC_JSON_ARRAY:
        for (int i = 0; i < val->as.arr.count; i++)
            kc_json_free(val->as.arr.items[i]);
        free(val->as.arr.items);
        break;
    case KC_JSON_OBJECT:
        for (int i = 0; i < val->as.obj.count; i++) {
            free(val->as.obj.keys[i]);
            kc_json_free(val->as.obj.vals[i]);
        }
        free(val->as.obj.keys);
        free(val->as.obj.vals);
        break;
    default: break;
    }
    free(val);
}

/* ── 파서 내부 ── */
typedef struct { const char *p; } Parser;

static void skip_ws(Parser *p) {
    while (*p->p && isspace((unsigned char)*p->p)) p->p++;
}

static KcJson *parse_value(Parser *p);

static char *parse_string_raw(Parser *p) {
    if (*p->p != '"') return NULL;
    p->p++;
    size_t cap = 64, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    while (*p->p && *p->p != '"') {
        if (*p->p == '\\') {
            p->p++;
            char c = 0;
            switch (*p->p) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            default:  c = *p->p; break;
            }
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = c;
        } else {
            /* UTF-8 다중 바이트 그대로 복사 */
            unsigned char ch = (unsigned char)*p->p;
            int bytes = 1;
            if      (ch >= 0xF0) bytes = 4;
            else if (ch >= 0xE0) bytes = 3;
            else if (ch >= 0xC0) bytes = 2;
            if (len + bytes + 1 >= cap) { cap = (cap + bytes) * 2; buf = realloc(buf, cap); }
            for (int i = 0; i < bytes && *p->p; i++) buf[len++] = *p->p++;
            continue;
        }
        p->p++;
    }
    if (*p->p == '"') p->p++;
    buf[len] = '\0';
    return buf;
}

static KcJson *parse_string(Parser *p) {
    char *s = parse_string_raw(p);
    if (!s) return NULL;
    KcJson *j = kc_json_string(s);
    free(s);
    return j;
}

static KcJson *parse_number(Parser *p) {
    char buf[64]; int i = 0;
    int is_float = 0;
    if (*p->p == '-') buf[i++] = *p->p++;
    while (*p->p && isdigit((unsigned char)*p->p)) buf[i++] = *p->p++;
    if (*p->p == '.') { is_float = 1; buf[i++] = *p->p++; }
    while (*p->p && isdigit((unsigned char)*p->p)) buf[i++] = *p->p++;
    if (*p->p == 'e' || *p->p == 'E') {
        is_float = 1; buf[i++] = *p->p++;
        if (*p->p == '+' || *p->p == '-') buf[i++] = *p->p++;
        while (*p->p && isdigit((unsigned char)*p->p)) buf[i++] = *p->p++;
    }
    buf[i] = '\0';
    if (is_float) return kc_json_float(atof(buf));
    return kc_json_int((int64_t)atoll(buf));
}

static KcJson *parse_array(Parser *p) {
    p->p++; /* '[' */
    KcJson *arr = kc_json_array();
    skip_ws(p);
    if (*p->p == ']') { p->p++; return arr; }
    while (*p->p) {
        skip_ws(p);
        KcJson *v = parse_value(p);
        if (v) kc_json_array_push(arr, v);
        skip_ws(p);
        if (*p->p == ',') { p->p++; continue; }
        if (*p->p == ']') { p->p++; break; }
        break;
    }
    return arr;
}

static KcJson *parse_object(Parser *p) {
    p->p++; /* '{' */
    KcJson *obj = kc_json_object();
    skip_ws(p);
    if (*p->p == '}') { p->p++; return obj; }
    while (*p->p) {
        skip_ws(p);
        char *key = parse_string_raw(p);
        if (!key) break;
        skip_ws(p);
        if (*p->p == ':') p->p++;
        skip_ws(p);
        KcJson *val = parse_value(p);
        if (val) kc_json_object_set(obj, key, val);
        free(key);
        skip_ws(p);
        if (*p->p == ',') { p->p++; continue; }
        if (*p->p == '}') { p->p++; break; }
        break;
    }
    return obj;
}

static KcJson *parse_value(Parser *p) {
    skip_ws(p);
    if (!*p->p) return NULL;
    if (*p->p == '"') return parse_string(p);
    if (*p->p == '[') return parse_array(p);
    if (*p->p == '{') return parse_object(p);
    if (*p->p == '-' || isdigit((unsigned char)*p->p)) return parse_number(p);
    if (strncmp(p->p, "true",  4) == 0) { p->p += 4; return kc_json_bool(1); }
    if (strncmp(p->p, "false", 5) == 0) { p->p += 5; return kc_json_bool(0); }
    if (strncmp(p->p, "null",  4) == 0) { p->p += 4; return kc_json_null(); }
    return NULL;
}

KcJson *kc_json_parse(const char *src) {
    if (!src) return NULL;
    Parser p; p.p = src;
    return parse_value(&p);
}

/* ── 직렬화 ── */
typedef struct { char *buf; size_t len; size_t cap; } Buf;

static void buf_push(Buf *b, const char *s) {
    size_t slen = strlen(s);
    while (b->len + slen + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->buf = realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, s, slen);
    b->len += slen;
    b->buf[b->len] = '\0';
}

static void stringify_rec(Buf *b, const KcJson *val) {
    if (!val) { buf_push(b, "null"); return; }
    char tmp[64];
    switch (val->type) {
    case KC_JSON_NULL:   buf_push(b, "null"); break;
    case KC_JSON_BOOL:   buf_push(b, val->as.bval ? "true" : "false"); break;
    case KC_JSON_INT:
        snprintf(tmp, sizeof(tmp), "%lld", (long long)val->as.ival);
        buf_push(b, tmp); break;
    case KC_JSON_FLOAT:
        snprintf(tmp, sizeof(tmp), "%g", val->as.fval);
        buf_push(b, tmp); break;
    case KC_JSON_STRING: {
        buf_push(b, "\"");
        const char *s = val->as.sval;
        while (s && *s) {
            if      (*s == '"')  buf_push(b, "\\\"");
            else if (*s == '\\') buf_push(b, "\\\\");
            else if (*s == '\n') buf_push(b, "\\n");
            else if (*s == '\r') buf_push(b, "\\r");
            else if (*s == '\t') buf_push(b, "\\t");
            else { char cs[2] = {*s, 0}; buf_push(b, cs); }
            s++;
        }
        buf_push(b, "\""); break;
    }
    case KC_JSON_ARRAY:
        buf_push(b, "[");
        for (int i = 0; i < val->as.arr.count; i++) {
            if (i > 0) buf_push(b, ",");
            stringify_rec(b, val->as.arr.items[i]);
        }
        buf_push(b, "]"); break;
    case KC_JSON_OBJECT:
        buf_push(b, "{");
        for (int i = 0; i < val->as.obj.count; i++) {
            if (i > 0) buf_push(b, ",");
            buf_push(b, "\"");
            buf_push(b, val->as.obj.keys[i]);
            buf_push(b, "\":");
            stringify_rec(b, val->as.obj.vals[i]);
        }
        buf_push(b, "}"); break;
    }
}

char *kc_json_stringify(const KcJson *val) {
    Buf b = {NULL, 0, 0};
    stringify_rec(&b, val);
    return b.buf ? b.buf : strdup("null");
}

/* ================================================================
 *  MCP 편의 함수
 * ================================================================ */

KcJson *kc_mcp_text_content(const char *text) {
    KcJson *block = kc_json_object();
    kc_json_object_set(block, "type", kc_json_string("text"));
    kc_json_object_set(block, "text", kc_json_string(text ? text : ""));
    return block;
}

KcJson *kc_mcp_error_response(int64_t id, int code, const char *message) {
    KcJson *resp = kc_json_object();
    kc_json_object_set(resp, "jsonrpc", kc_json_string("2.0"));
    kc_json_object_set(resp, "id",      kc_json_int(id));
    KcJson *err = kc_json_object();
    kc_json_object_set(err, "code",    kc_json_int(code));
    kc_json_object_set(err, "message", kc_json_string(message ? message : "error"));
    kc_json_object_set(resp, "error",  err);
    return resp;
}

/* ================================================================
 *  서버 초기화 / 등록
 * ================================================================ */

void kc_mcp_server_init(KcMcpServer *srv,
                         const char *name,
                         const char *version,
                         const char *description) {
    if (!srv) return;
    memset(srv, 0, sizeof(KcMcpServer));
    srv->name        = name;
    srv->version     = version;
    srv->description = description;
    srv->in          = stdin;
    srv->out         = stdout;
}

void kc_mcp_server_add_tool(KcMcpServer *srv, KcMcpTool tool) {
    if (!srv || srv->tool_count >= KC_MCP_HANDLER_MAX) return;
    srv->tools[srv->tool_count++] = tool;
}

void kc_mcp_server_add_resource(KcMcpServer *srv, KcMcpResource res) {
    if (!srv || srv->resource_count >= KC_MCP_HANDLER_MAX) return;
    srv->resources[srv->resource_count++] = res;
}

void kc_mcp_server_add_prompt(KcMcpServer *srv, KcMcpPrompt prompt) {
    if (!srv || srv->prompt_count >= KC_MCP_HANDLER_MAX) return;
    srv->prompts[srv->prompt_count++] = prompt;
}

/* ================================================================
 *  JSON Schema 생성 (입력 파라미터 → inputSchema)
 * ================================================================ */

static const char *param_type_str(KcMcpParamType t) {
    switch (t) {
    case KC_MCP_PARAM_STRING:  return "string";
    case KC_MCP_PARAM_INTEGER: return "integer";
    case KC_MCP_PARAM_FLOAT:   return "number";
    case KC_MCP_PARAM_BOOLEAN: return "boolean";
    case KC_MCP_PARAM_OBJECT:  return "object";
    case KC_MCP_PARAM_ARRAY:   return "array";
    default:                   return "string";
    }
}

static KcJson *build_input_schema(const KcMcpParam *params, int count) {
    KcJson *schema = kc_json_object();
    kc_json_object_set(schema, "type", kc_json_string("object"));
    KcJson *props = kc_json_object();
    KcJson *req   = kc_json_array();
    for (int i = 0; i < count; i++) {
        KcJson *prop = kc_json_object();
        kc_json_object_set(prop, "type",
                           kc_json_string(param_type_str(params[i].type)));
        if (params[i].description)
            kc_json_object_set(prop, "description",
                               kc_json_string(params[i].description));
        kc_json_object_set(props, params[i].name, prop);
        if (params[i].required)
            kc_json_array_push(req, kc_json_string(params[i].name));
    }
    kc_json_object_set(schema, "properties", props);
    kc_json_object_set(schema, "required",   req);
    return schema;
}

/* ================================================================
 *  JSON-RPC 2.0 요청 처리
 * ================================================================ */

/* initialize */
static KcJson *handle_initialize(KcMcpServer *srv, KcJson *id) {
    KcJson *resp = kc_json_object();
    kc_json_object_set(resp, "jsonrpc", kc_json_string("2.0"));
    kc_json_object_set(resp, "id",      id ? id : kc_json_null());

    KcJson *result = kc_json_object();

    /* protocolVersion */
    kc_json_object_set(result, "protocolVersion", kc_json_string("2024-11-05"));

    /* serverInfo */
    KcJson *info = kc_json_object();
    kc_json_object_set(info, "name",    kc_json_string(srv->name    ? srv->name    : "kcode-mcp"));
    kc_json_object_set(info, "version", kc_json_string(srv->version ? srv->version : "1.0.0"));
    kc_json_object_set(result, "serverInfo", info);

    /* capabilities */
    KcJson *caps = kc_json_object();
    if (srv->tool_count > 0) {
        KcJson *tc = kc_json_object();
        kc_json_object_set(tc, "listChanged", kc_json_bool(0));
        kc_json_object_set(caps, "tools", tc);
    }
    if (srv->resource_count > 0) {
        KcJson *rc = kc_json_object();
        kc_json_object_set(rc, "listChanged", kc_json_bool(0));
        kc_json_object_set(caps, "resources", rc);
    }
    if (srv->prompt_count > 0) {
        KcJson *pc = kc_json_object();
        kc_json_object_set(pc, "listChanged", kc_json_bool(0));
        kc_json_object_set(caps, "prompts", pc);
    }
    kc_json_object_set(result, "capabilities", caps);
    kc_json_object_set(resp, "result", result);
    return resp;
}

/* tools/list */
static KcJson *handle_tools_list(KcMcpServer *srv, KcJson *id) {
    KcJson *resp = kc_json_object();
    kc_json_object_set(resp, "jsonrpc", kc_json_string("2.0"));
    kc_json_object_set(resp, "id",      id ? id : kc_json_null());
    KcJson *result = kc_json_object();
    KcJson *tools  = kc_json_array();
    for (int i = 0; i < srv->tool_count; i++) {
        KcMcpTool *t = &srv->tools[i];
        KcJson *tj = kc_json_object();
        kc_json_object_set(tj, "name",        kc_json_string(t->name));
        kc_json_object_set(tj, "description", kc_json_string(t->description ? t->description : ""));
        kc_json_object_set(tj, "inputSchema", build_input_schema(t->params, t->param_count));
        kc_json_array_push(tools, tj);
    }
    kc_json_object_set(result, "tools", tools);
    kc_json_object_set(resp, "result", result);
    return resp;
}

/* tools/call */
static KcJson *handle_tools_call(KcMcpServer *srv, KcJson *id, KcJson *params) {
    KcJson *name_j = kc_json_object_get(params, "name");
    const char *name = (name_j && name_j->type == KC_JSON_STRING)
                       ? name_j->as.sval : NULL;
    KcJson *args = kc_json_object_get(params, "arguments");

    for (int i = 0; i < srv->tool_count; i++) {
        if (name && strcmp(srv->tools[i].name, name) == 0) {
            KcJson *content = srv->tools[i].handler
                ? srv->tools[i].handler(args, srv->tools[i].userdata)
                : kc_mcp_text_content("(핸들러 없음)");
            KcJson *resp = kc_json_object();
            kc_json_object_set(resp, "jsonrpc", kc_json_string("2.0"));
            kc_json_object_set(resp, "id",      id ? id : kc_json_null());
            KcJson *result = kc_json_object();
            KcJson *arr    = kc_json_array();
            if (content) kc_json_array_push(arr, content);
            kc_json_object_set(result, "content", arr);
            kc_json_object_set(result, "isError", kc_json_bool(0));
            kc_json_object_set(resp, "result", result);
            return resp;
        }
    }
    int64_t rid = (id && id->type == KC_JSON_INT) ? id->as.ival : 0;
    return kc_mcp_error_response(rid, -32601, "도구를 찾을 수 없습니다");
}

/* resources/list */
static KcJson *handle_resources_list(KcMcpServer *srv, KcJson *id) {
    KcJson *resp = kc_json_object();
    kc_json_object_set(resp, "jsonrpc", kc_json_string("2.0"));
    kc_json_object_set(resp, "id",      id ? id : kc_json_null());
    KcJson *result = kc_json_object();
    KcJson *list   = kc_json_array();
    for (int i = 0; i < srv->resource_count; i++) {
        KcMcpResource *r = &srv->resources[i];
        KcJson *rj = kc_json_object();
        kc_json_object_set(rj, "uri",         kc_json_string(r->uri ? r->uri : ""));
        kc_json_object_set(rj, "name",        kc_json_string(r->name ? r->name : ""));
        kc_json_object_set(rj, "description", kc_json_string(r->description ? r->description : ""));
        kc_json_object_set(rj, "mimeType",    kc_json_string(r->mime_type ? r->mime_type : "text/plain"));
        kc_json_array_push(list, rj);
    }
    kc_json_object_set(result, "resources", list);
    kc_json_object_set(resp, "result", result);
    return resp;
}

/* resources/read */
static KcJson *handle_resources_read(KcMcpServer *srv, KcJson *id, KcJson *params) {
    KcJson *uri_j = kc_json_object_get(params, "uri");
    const char *uri = (uri_j && uri_j->type == KC_JSON_STRING) ? uri_j->as.sval : NULL;
    for (int i = 0; i < srv->resource_count; i++) {
        if (uri && strcmp(srv->resources[i].uri, uri) == 0) {
            KcJson *content = srv->resources[i].handler
                ? srv->resources[i].handler(NULL, srv->resources[i].userdata)
                : kc_mcp_text_content("");
            KcJson *resp = kc_json_object();
            kc_json_object_set(resp, "jsonrpc", kc_json_string("2.0"));
            kc_json_object_set(resp, "id",      id ? id : kc_json_null());
            KcJson *result = kc_json_object();
            KcJson *arr    = kc_json_array();
            if (content) {
                kc_json_object_set(content, "uri",      kc_json_string(uri));
                kc_json_object_set(content, "mimeType", kc_json_string(
                    srv->resources[i].mime_type ? srv->resources[i].mime_type : "text/plain"));
                kc_json_array_push(arr, content);
            }
            kc_json_object_set(result, "contents", arr);
            kc_json_object_set(resp, "result", result);
            return resp;
        }
    }
    int64_t rid = (id && id->type == KC_JSON_INT) ? id->as.ival : 0;
    return kc_mcp_error_response(rid, -32601, "자원을 찾을 수 없습니다");
}

/* prompts/list */
static KcJson *handle_prompts_list(KcMcpServer *srv, KcJson *id) {
    KcJson *resp = kc_json_object();
    kc_json_object_set(resp, "jsonrpc", kc_json_string("2.0"));
    kc_json_object_set(resp, "id",      id ? id : kc_json_null());
    KcJson *result = kc_json_object();
    KcJson *list   = kc_json_array();
    for (int i = 0; i < srv->prompt_count; i++) {
        KcMcpPrompt *pt = &srv->prompts[i];
        KcJson *pj = kc_json_object();
        kc_json_object_set(pj, "name",        kc_json_string(pt->name));
        kc_json_object_set(pj, "description", kc_json_string(pt->description ? pt->description : ""));
        KcJson *args = kc_json_array();
        for (int k = 0; k < pt->param_count; k++) {
            KcJson *aj = kc_json_object();
            kc_json_object_set(aj, "name",     kc_json_string(pt->params[k].name));
            kc_json_object_set(aj, "required", kc_json_bool(pt->params[k].required));
            if (pt->params[k].description)
                kc_json_object_set(aj, "description", kc_json_string(pt->params[k].description));
            kc_json_array_push(args, aj);
        }
        kc_json_object_set(pj, "arguments", args);
        kc_json_array_push(list, pj);
    }
    kc_json_object_set(result, "prompts", list);
    kc_json_object_set(resp, "result", result);
    return resp;
}

/* prompts/get */
static KcJson *handle_prompts_get(KcMcpServer *srv, KcJson *id, KcJson *params) {
    KcJson *name_j = kc_json_object_get(params, "name");
    const char *name = (name_j && name_j->type == KC_JSON_STRING) ? name_j->as.sval : NULL;
    KcJson *args = kc_json_object_get(params, "arguments");
    for (int i = 0; i < srv->prompt_count; i++) {
        if (name && strcmp(srv->prompts[i].name, name) == 0) {
            KcJson *text = srv->prompts[i].handler
                ? srv->prompts[i].handler(args, srv->prompts[i].userdata)
                : kc_mcp_text_content("");
            KcJson *resp = kc_json_object();
            kc_json_object_set(resp, "jsonrpc", kc_json_string("2.0"));
            kc_json_object_set(resp, "id",      id ? id : kc_json_null());
            KcJson *result   = kc_json_object();
            KcJson *messages = kc_json_array();
            KcJson *msg      = kc_json_object();
            kc_json_object_set(msg, "role", kc_json_string("user"));
            KcJson *content  = kc_json_object();
            kc_json_object_set(content, "type", kc_json_string("text"));
            const char *tval = (text && text->type == KC_JSON_OBJECT)
                ? (kc_json_object_get(text, "text")
                   ? kc_json_object_get(text, "text")->as.sval : "") : "";
            kc_json_object_set(content, "text", kc_json_string(tval));
            kc_json_object_set(msg, "content", content);
            kc_json_array_push(messages, msg);
            kc_json_free(text);
            kc_json_object_set(result, "messages", messages);
            kc_json_object_set(resp, "result", result);
            return resp;
        }
    }
    int64_t rid = (id && id->type == KC_JSON_INT) ? id->as.ival : 0;
    return kc_mcp_error_response(rid, -32601, "프롬프트를 찾을 수 없습니다");
}

/* ================================================================
 *  메인 루프
 * ================================================================ */

void kc_mcp_server_run(KcMcpServer *srv) {
    if (!srv) return;

    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    /* stderr로 준비 완료 알림 (디버그용) */
    fprintf(stderr, "[kcode-mcp] 서버 시작: %s v%s\n",
            srv->name ? srv->name : "kcode",
            srv->version ? srv->version : "1.0.0");
    fflush(stderr);

    while ((len = getline(&line, &cap, srv->in)) != -1) {
        /* 빈 줄 무시 */
        if (len <= 1) continue;

        KcJson *req = kc_json_parse(line);
        if (!req) continue;

        KcJson *method_j = kc_json_object_get(req, "method");
        KcJson *id_j     = kc_json_object_get(req, "id");
        KcJson *params_j = kc_json_object_get(req, "params");
        const char *method = (method_j && method_j->type == KC_JSON_STRING)
                             ? method_j->as.sval : "";

        KcJson *resp = NULL;

        if (strcmp(method, "initialize") == 0) {
            resp = handle_initialize(srv, id_j);
        } else if (strcmp(method, "notifications/initialized") == 0) {
            /* 알림 — 응답 없음 */
        } else if (strcmp(method, "tools/list") == 0) {
            resp = handle_tools_list(srv, id_j);
        } else if (strcmp(method, "tools/call") == 0) {
            resp = handle_tools_call(srv, id_j, params_j ? params_j : kc_json_object());
        } else if (strcmp(method, "resources/list") == 0) {
            resp = handle_resources_list(srv, id_j);
        } else if (strcmp(method, "resources/read") == 0) {
            resp = handle_resources_read(srv, id_j, params_j ? params_j : kc_json_object());
        } else if (strcmp(method, "prompts/list") == 0) {
            resp = handle_prompts_list(srv, id_j);
        } else if (strcmp(method, "prompts/get") == 0) {
            resp = handle_prompts_get(srv, id_j, params_j ? params_j : kc_json_object());
        } else if (strcmp(method, "ping") == 0) {
            resp = kc_json_object();
            kc_json_object_set(resp, "jsonrpc", kc_json_string("2.0"));
            kc_json_object_set(resp, "id",      id_j ? id_j : kc_json_null());
            kc_json_object_set(resp, "result",  kc_json_object());
        } else {
            /* 알 수 없는 메서드 */
            int64_t rid = (id_j && id_j->type == KC_JSON_INT) ? id_j->as.ival : 0;
            resp = kc_mcp_error_response(rid, -32601, "지원하지 않는 메서드");
        }

        if (resp) {
            char *out = kc_json_stringify(resp);
            fprintf(srv->out, "%s\n", out);
            fflush(srv->out);
            free(out);
            kc_json_free(resp);
        }

        kc_json_free(req);
    }

    free(line);
}
