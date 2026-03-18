/*
 * kcodegen_main.c  —  Kcode C 코드 생성기 드라이버
 * version : v30.0.0  (메인 버전 상속)
 *
 * v30.0.0 변경:
 *   - compile_source() — kc_contract_check() 검증 패스 삽입
 *     계약 없는 소스 컴파일 거부 (KC_CONTRACT_MISSING / KC_CONTRACT_EMPTY_SOURCE)
 *
 * 실행 모드:
 *   ./kcode_gen file.han              → C 소스 출력 (stdout)
 *   ./kcode_gen file.han -o out.c     → C 소스 파일 저장
 *   ./kcode_gen file.han -b           → 빌드 (gcc로 실행파일 생성)
 *   ./kcode_gen file.han -b -o out    → 실행파일 이름 지정
 *   ./kcode_gen file.han --json       → IDE 연동 JSON 출력 (stdout)
 *   ./kcode_gen --ide                 → IDE 서버 모드 (stdin JSON 수신)
 *
 * IDE 연동 프로토콜 (--ide 모드):
 *   stdin  ← {"action":"compile", "source":"...han 소스..."}
 *   stdout → {"success":true, "c_source":"...", "sourcemap":[...], "errors":[], "stats":{...}}
 *
 * MIT License
 * zerojat7
 */

#include "klexer.h"
#include "kparser.h"
#include "kcodegen.h"
#include "kc_contract_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ================================================================
 *  파일 읽기
 * ================================================================ */
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[오류] 파일을 열 수 없습니다: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = (char*)malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

/* ================================================================
 *  stdin 전체 읽기 (IDE 모드용)
 * ================================================================ */
static char *read_stdin(void) {
    size_t cap = 64 * 1024;
    size_t len = 0;
    char  *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char*)realloc(buf, cap);
            if (!buf) return NULL;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return buf;
}

/* ================================================================
 *  소스 → CodegenResult 파이프라인
 * ================================================================ */
static CodegenResult *compile_source(const char *src) {
    Lexer lx;
    lexer_init(&lx, src, strlen(src));

    Parser parser;
    parser_init(&parser, &lx);
    Node *ast = parser_parse(&parser);

    if (parser.had_error) {
        /* 파서 오류를 CodegenResult 오류로 변환 */
        CodegenResult *r = (CodegenResult*)calloc(1, sizeof(CodegenResult));
        r->had_error = 1;
        for (int i = 0; i < parser.error_count && i < CODEGEN_MAX_ERRORS; i++) {
            CodegenError *e = &r->errors[r->error_count++];
            e->line = 0; e->col = 0;
            snprintf(e->msg, sizeof(e->msg), "[파서] %s", parser.errors[i]);
        }
        node_free(ast);
        return r;
    }

    /* ★ 계약 시스템 강제 검증 패스 (v30.0.0) */
    KcContractCheckInfo cc_info;
    KcContractCheckResult cc = kc_contract_check(ast, &cc_info);
    if (cc == KC_CONTRACT_MISSING || cc == KC_CONTRACT_EMPTY_SOURCE) {
        kc_contract_check_print_error(&cc_info, NULL);
        CodegenResult *r2 = (CodegenResult*)calloc(1, sizeof(CodegenResult));
        r2->had_error = 1;
        CodegenError *e = &r2->errors[r2->error_count++];
        e->line = 0; e->col = 0;
        snprintf(e->msg, sizeof(e->msg), "%s", cc_info.error_msg);
        node_free(ast);
        return r2;
    }
    /* ★ 검증 종료 */

    CodegenResult *r = codegen_run(ast);
    node_free(ast);
    return r;
}

/* ================================================================
 *  JSON 문자열에서 "source" 필드 추출 (경량 파서)
 *  IDE에서 {"action":"compile","source":"..."}를 보낼 때 사용
 * ================================================================ */
static char *extract_json_source(const char *json) {
    /* "source": 를 찾음 */
    const char *key = "\"source\"";
    const char *p   = strstr(json, key);
    if (!p) return NULL;
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == ' ') p++;
    if (*p != '"') return NULL;
    p++; /* 여는 따옴표 건너뜀 */

    /* 이스케이프 처리하며 문자열 복사 */
    size_t cap = 64 * 1024;
    char  *out = (char*)malloc(cap);
    size_t len = 0;

    while (*p && *p != '\0') {
        if (len + 4 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
        if (*p == '\\' && *(p+1)) {
            p++;
            switch (*p) {
                case 'n':  out[len++] = '\n'; break;
                case 't':  out[len++] = '\t'; break;
                case 'r':  out[len++] = '\r'; break;
                case '"':  out[len++] = '"';  break;
                case '\\': out[len++] = '\\'; break;
                default:   out[len++] = *p;   break;
            }
            p++;
        } else if (*p == '"') {
            break; /* 닫는 따옴표 */
        } else {
            out[len++] = *p++;
        }
    }
    out[len] = '\0';
    return out;
}

/* ================================================================
 *  IDE 서버 모드
 *  stdin으로 JSON 요청을 받고, stdout으로 JSON 응답을 보낸다.
 *
 *  요청 형식:
 *    {"action": "compile", "source": "...한글 Kcode 소스..."}
 *
 *  응답 형식: codegen_to_json() 참조
 * ================================================================ */
static void ide_server_mode(void) {
    fprintf(stderr, "[Kcode 컴파일러] IDE 서버 모드 시작\n");
    fprintf(stderr, "stdin으로 JSON 요청을 보내세요.\n");
    fprintf(stderr, "형식: {\"action\":\"compile\",\"source\":\"...\"}\n\n");

    char *json = read_stdin();
    if (!json) {
        fprintf(stdout, "{\"success\":false,\"errors\":[{\"line\":0,\"col\":0,\"msg\":\"stdin 읽기 실패\"}]}\n");
        return;
    }

    /* action 확인 */
    if (!strstr(json, "\"compile\"")) {
        fprintf(stdout, "{\"success\":false,\"errors\":[{\"line\":0,\"col\":0,\"msg\":\"알 수 없는 action\"}]}\n");
        free(json);
        return;
    }

    char *src = extract_json_source(json);
    free(json);

    if (!src) {
        fprintf(stdout, "{\"success\":false,\"errors\":[{\"line\":0,\"col\":0,\"msg\":\"source 필드를 찾을 수 없습니다\"}]}\n");
        return;
    }

    CodegenResult *r = compile_source(src);
    free(src);

    codegen_to_json(r, stdout);
    codegen_result_free(r);
}

/* ================================================================
 *  내장 테스트
 * ================================================================ */
static void run_builtin_test(void) {
    printf("══════════════════════════════════════════════\n");
    printf("  Kcode v1.0.0 — C 코드 생성기 내장 테스트\n");
    printf("══════════════════════════════════════════════\n\n");

    const char *src =
        /* 함수 선언 */
        "\xED\x95\xA8\xEC\x88\x98 \xEB\x8D\x94\xED\x95\x98\xEA\xB8\xB0"
        "(\xEC\xa0\x95\xEC\x88\x98 \xEA\xB0\x80, \xEC\xa0\x95\xEC\x88\x98 \xEB\x82\x98):\n"
        "    \xEB\xB0\x98\xED\x99\x98 \xEA\xB0\x80 + \xEB\x82\x98\n\n"
        /* 전역 구문 */
        "\xEC\xa0\x95\xEC\x88\x98 \xEA\xB2\xB0\xEA\xB3\xBC = \xEB\x8D\x94\xED\x95\x98\xEA\xB8\xB0(3, 7)\n"
        "\xEC\xB6\x9C\xEB\xa0\xa5(\xEA\xB2\xB0\xEA\xB3\xBC)\n"
        /* 반복 */
        "\xEB\xB0\x98\xEB\xB3\xB5 i \xEB\xB6\x80\xED\x84\xB0 1 \xEA\xB9\x8C\xEC\xa7\x80 5:\n"
        "    \xEC\xB6\x9C\xEB\xa0\xa5(i)\n";

    printf("[테스트 소스 (한글)]\n");
    printf("----------------------------\n");
    printf("%s\n", src);

    CodegenResult *r = compile_source(src);

    printf("[생성된 C 코드]\n");
    printf("----------------------------\n");
    if (r->c_source) printf("%s\n", r->c_source);

    if (r->had_error) {
        printf("[오류 목록]\n");
        for (int i = 0; i < r->error_count; i++) {
            printf("  줄 %d: %s\n", r->errors[i].line, r->errors[i].msg);
        }
    } else {
        printf("[통계]\n");
        printf("  함수: %d개 / 변수: %d개 / C 라인: %d줄\n",
               r->func_count, r->var_count, r->line_count);
        printf("  소스맵 엔트리: %d개\n", r->sourcemap_count);
    }

    codegen_result_free(r);

    printf("\n══════════════════════════════════════════════\n");
    printf("  완료\n");
    printf("══════════════════════════════════════════════\n");
}

/* ================================================================
 *  사용법 출력
 * ================================================================ */
static void print_usage(const char *prog) {
    fprintf(stderr,
        "사용법:\n"
        "  %s                          내장 테스트\n"
        "  %s <파일.han>               C 소스 stdout 출력\n"
        "  %s <파일.han> -o <out.c>    C 소스 파일 저장\n"
        "  %s <파일.han> -b            C 소스 생성 + gcc 빌드\n"
        "  %s <파일.han> -b -o <out>   빌드 후 실행파일 이름 지정\n"
        "  %s <파일.han> --json        IDE 연동 JSON 출력\n"
        "  %s --ide                    IDE 서버 모드 (stdin JSON)\n",
        prog, prog, prog, prog, prog, prog, prog);
}

/* ================================================================
 *  main
 * ================================================================ */
int main(int argc, char *argv[]) {

    /* 인수 없음 → 내장 테스트 */
    if (argc == 1) {
        run_builtin_test();
        return 0;
    }

    /* IDE 서버 모드 */
    if (argc == 2 && strcmp(argv[1], "--ide") == 0) {
        ide_server_mode();
        return 0;
    }

    /* 파일 경로 필수 */
    const char *han_path = argv[1];

    /* 옵션 파싱 */
    int build    = 0;
    int json_out = 0;
    const char *out_path = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0) {
            build = 1;
        } else if (strcmp(argv[i], "--json") == 0) {
            json_out = 1;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else {
            fprintf(stderr, "[경고] 알 수 없는 옵션: %s\n", argv[i]);
        }
    }

    /* 소스 읽기 */
    char *src = read_file(han_path);
    if (!src) return 1;

    /* 컴파일 */
    CodegenResult *r = compile_source(src);
    free(src);

    /* JSON 모드 */
    if (json_out) {
        codegen_to_json(r, stdout);
        int ret = r->had_error ? 1 : 0;
        codegen_result_free(r);
        return ret;
    }

    /* 오류 출력 */
    if (r->had_error) {
        for (int i = 0; i < r->error_count; i++) {
            fprintf(stderr, "[오류] 줄 %d, 열 %d: %s\n",
                    r->errors[i].line, r->errors[i].col, r->errors[i].msg);
        }
        codegen_result_free(r);
        return 1;
    }

    /* C 소스 출력/저장 */
    if (out_path && !build) {
        /* -o out.c 만 (빌드 없음) */
        FILE *f = fopen(out_path, "w");
        if (!f) {
            fprintf(stderr, "[오류] 파일 쓰기 실패: %s\n", out_path);
            codegen_result_free(r); return 1;
        }
        fwrite(r->c_source, 1, r->c_source_len, f);
        fclose(f);
        fprintf(stderr, "[완료] C 소스 저장: %s (%d 라인)\n",
                out_path, r->line_count);
    } else if (build) {
        /* 빌드 모드: 임시 .c 파일 → gcc */
        char tmp_c[256];
        snprintf(tmp_c, sizeof(tmp_c), "/tmp/_kcode_gen_%d.c", (int)getpid());

        FILE *f = fopen(tmp_c, "w");
        if (!f) {
            fprintf(stderr, "[오류] 임시 파일 생성 실패\n");
            codegen_result_free(r); return 1;
        }
        fwrite(r->c_source, 1, r->c_source_len, f);
        fclose(f);

        /* 출력 경로 결정 */
        char exec_path[256];
        if (out_path) {
            snprintf(exec_path, sizeof(exec_path), "%s", out_path);
        } else {
            /* 파일명에서 확장자 제거 */
            snprintf(exec_path, sizeof(exec_path), "%s", han_path);
            char *dot = strrchr(exec_path, '.');
            if (dot) *dot = '\0';
        }

        fprintf(stderr, "[빌드] %s → %s\n", tmp_c, exec_path);
        int ret = codegen_compile(tmp_c, exec_path);
        remove(tmp_c);

        if (ret != 0) {
            fprintf(stderr, "[오류] gcc 빌드 실패 (코드: %d)\n", ret);
            codegen_result_free(r); return 1;
        }
        fprintf(stderr, "[완료] 실행파일 생성: %s\n", exec_path);
    } else {
        /* 기본: C 소스 stdout 출력 */
        fwrite(r->c_source, 1, r->c_source_len, stdout);
    }

    int ret = r->had_error ? 1 : 0;
    codegen_result_free(r);
    return ret;
}
