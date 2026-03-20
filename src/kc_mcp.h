/*
 * kc_mcp.h  —  Kcode MCP(Model Context Protocol) 런타임 헤더
 * version : v14.0.0
 *
 * Kcode 언어로 MCP 서버를 작성할 수 있는 런타임 기반.
 * 전송: stdio (JSON-RPC 2.0)
 * 계약 시스템 연동: 헌법→서버메타 / 법률→핸들러 / 법령→입력검증 / 법위반→오류응답
 *
 * ▶ 지원 MCP 핸들러 3종:
 *   - KcMcpTool     : tools/call       — 도구 호출
 *   - KcMcpResource : resources/read   — 자원 접근
 *   - KcMcpPrompt   : prompts/get      — 프롬프트 반환
 *
 * ▶ 내장 미니 JSON 파서/직렬화기 포함 (외부 라이브러리 불필요)
 *
 * MIT License / zerojat7
 */

#ifndef KCODE_MCP_H
#define KCODE_MCP_H

#include <stdio.h>
#include <stdint.h>

/* ================================================================
 *  미니 JSON 값 타입
 * ================================================================ */

typedef enum {
    KC_JSON_NULL,
    KC_JSON_BOOL,
    KC_JSON_INT,
    KC_JSON_FLOAT,
    KC_JSON_STRING,
    KC_JSON_ARRAY,
    KC_JSON_OBJECT
} KcJsonType;

typedef struct KcJson KcJson;

struct KcJson {
    KcJsonType type;
    union {
        int         bval;
        int64_t     ival;
        double      fval;
        char       *sval;   /* KC_JSON_STRING — malloc 할당 */
        struct {
            KcJson **items;
            int      count;
            int      cap;
        } arr;              /* KC_JSON_ARRAY */
        struct {
            char   **keys;
            KcJson **vals;
            int      count;
            int      cap;
        } obj;              /* KC_JSON_OBJECT */
    } as;
};

/* JSON 생성 */
KcJson *kc_json_null(void);
KcJson *kc_json_bool(int b);
KcJson *kc_json_int(int64_t v);
KcJson *kc_json_float(double v);
KcJson *kc_json_string(const char *s);
KcJson *kc_json_array(void);
KcJson *kc_json_object(void);

/* JSON 조작 */
void    kc_json_array_push(KcJson *arr, KcJson *val);
void    kc_json_object_set(KcJson *obj, const char *key, KcJson *val);
KcJson *kc_json_object_get(KcJson *obj, const char *key);

/* JSON 파싱 / 직렬화 */
KcJson *kc_json_parse(const char *src);         /* 문자열 → KcJson* */
char   *kc_json_stringify(const KcJson *val);   /* KcJson* → malloc 문자열 */
void    kc_json_free(KcJson *val);              /* 재귀 해제 */

/* ================================================================
 *  MCP 핸들러 콜백 타입
 *
 *  params : tools/call 의 arguments 객체 (KC_JSON_OBJECT)
 *  반환   : content 배열에 넣을 KcJson* (text 블록)
 *           NULL 반환 시 내부 오류로 처리
 * ================================================================ */
typedef KcJson *(*KcMcpHandlerFn)(KcJson *params, void *userdata);

/* ================================================================
 *  입력 파라미터 스키마 — JSON Schema 자동 생성용
 * ================================================================ */

#define KC_MCP_PARAM_MAX 16

typedef enum {
    KC_MCP_PARAM_STRING,
    KC_MCP_PARAM_INTEGER,
    KC_MCP_PARAM_FLOAT,
    KC_MCP_PARAM_BOOLEAN,
    KC_MCP_PARAM_OBJECT,
    KC_MCP_PARAM_ARRAY
} KcMcpParamType;

typedef struct {
    const char     *name;
    KcMcpParamType  type;
    int             required;   /* 1 = 필수 */
    const char     *description;
} KcMcpParam;

/* ================================================================
 *  MCP 도구 (Tool)
 * ================================================================ */
typedef struct {
    const char      *name;
    const char      *description;
    KcMcpParam       params[KC_MCP_PARAM_MAX];
    int              param_count;
    KcMcpHandlerFn   handler;
    void            *userdata;
} KcMcpTool;

/* ================================================================
 *  MCP 자원 (Resource)
 * ================================================================ */
typedef struct {
    const char      *name;
    const char      *uri;
    const char      *mime_type;   /* 예: "application/json" */
    const char      *description;
    KcMcpHandlerFn   handler;     /* params = NULL, 반환 = 자원 내용 */
    void            *userdata;
} KcMcpResource;

/* ================================================================
 *  MCP 프롬프트 (Prompt)
 * ================================================================ */
typedef struct {
    const char      *name;
    const char      *description;
    KcMcpParam       params[KC_MCP_PARAM_MAX];
    int              param_count;
    KcMcpHandlerFn   handler;
    void            *userdata;
} KcMcpPrompt;

/* ================================================================
 *  MCP 서버 (Server)
 * ================================================================ */

#define KC_MCP_HANDLER_MAX 64

typedef struct {
    /* 서버 메타 */
    const char   *name;
    const char   *version;
    const char   *description;

    /* 핸들러 등록 테이블 */
    KcMcpTool     tools[KC_MCP_HANDLER_MAX];
    int           tool_count;

    KcMcpResource resources[KC_MCP_HANDLER_MAX];
    int           resource_count;

    KcMcpPrompt   prompts[KC_MCP_HANDLER_MAX];
    int           prompt_count;

    /* 입출력 스트림 (기본: stdin/stdout) */
    FILE         *in;
    FILE         *out;
} KcMcpServer;

/* ================================================================
 *  서버 공개 API
 * ================================================================ */

/*
 * kc_mcp_server_init() — 서버 초기화
 */
void kc_mcp_server_init(KcMcpServer *srv,
                         const char *name,
                         const char *version,
                         const char *description);

/*
 * kc_mcp_server_add_tool() — 도구 등록
 */
void kc_mcp_server_add_tool(KcMcpServer *srv, KcMcpTool tool);

/*
 * kc_mcp_server_add_resource() — 자원 등록
 */
void kc_mcp_server_add_resource(KcMcpServer *srv, KcMcpResource res);

/*
 * kc_mcp_server_add_prompt() — 프롬프트 등록
 */
void kc_mcp_server_add_prompt(KcMcpServer *srv, KcMcpPrompt prompt);

/*
 * kc_mcp_server_run() — stdio JSON-RPC 메인 루프 (블로킹)
 *   stdin에서 JSON-RPC 요청을 읽고 stdout으로 응답.
 *   EOF 또는 오류 시 반환.
 */
void kc_mcp_server_run(KcMcpServer *srv);

/*
 * kc_mcp_error_response() — JSON-RPC 오류 응답 생성
 *   Kcode: MCP오류(코드, 메시지)
 */
KcJson *kc_mcp_error_response(int64_t id, int code, const char *message);

/*
 * kc_mcp_text_content() — text 타입 content 블록 생성
 *   도구/프롬프트 핸들러 반환값 편의 함수
 */
KcJson *kc_mcp_text_content(const char *text);

#endif /* KCODE_MCP_H */
