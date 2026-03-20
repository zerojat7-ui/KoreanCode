/*
 * kbc_main.c  —  Kcode 바이트코드 VM 드라이버
 * version : v30.0.0
 *
 * v30.0.0 변경:
 *   - compile_source() — kc_contract_check() 검증 패스 삽입
 *
 * 사용법:
 *   kbc compile <소스.han> [출력.kbc]   소스 → .kbc 컴파일
 *   kbc run     <파일.kbc>               .kbc 실행
 *   kbc exec    <소스.han>               컴파일 + 즉시 실행 (임시 .kbc)
 *   kbc dump    <파일.kbc>               .kbc 디스어셈블러 출력
 *
 * MIT License
 * zerojat7
 */

#include "kc_bytecode.h"
#include "kc_bcgen.h"
#include "kc_contract_check.h"
#include "kc_vm.h"
#include "klexer.h"
#include "kparser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  소스 파일 읽기
 * ================================================================ */
static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    if (out_len) *out_len = rd;
    return buf;
}

/* ================================================================
 *  소스 파일 → KcModule 컴파일
 * ================================================================ */
static KcModule *compile_source(const char *path)
{
    size_t src_len = 0;
    char *src = read_file(path, &src_len);
    if (!src) {
        fprintf(stderr, "파일을 읽을 수 없습니다: %s\n", path);
        return NULL;
    }

    Lexer  lx;
    lexer_init(&lx, src, src_len);

    Parser parser;
    parser_init(&parser, &lx);
    Node *root = parser_parse(&parser);

    free(src);

    if (!root || parser.had_error) {
        fprintf(stderr, "[kbc] 파싱 오류 (%d개)\n", parser.error_count);
        for (int i = 0; i < parser.error_count; i++)
            fprintf(stderr, "  %s\n", parser.errors[i]);
        node_free(root);
        return NULL;
    }

    /* ★ 계약 시스템 강제 검증 패스 (v30.0.0) */
    KcContractCheckInfo cc_info;
    KcContractCheckResult cc = kc_contract_check(root, &cc_info);
    if (cc == KC_CONTRACT_MISSING || cc == KC_CONTRACT_EMPTY_SOURCE) {
        kc_contract_check_print_error(&cc_info, path);
        node_free(root);
        return NULL;
    }
    /* ★ 검증 종료 */

    KcModule *mod = kc_bcgen_compile(root, path);
    node_free(root);

    if (!mod) {
        fprintf(stderr, "[kbc] 바이트코드 생성 실패\n");
        return NULL;
    }

    return mod;
}

/* ================================================================
 *  출력 경로 결정 (.han → .kbc)
 * ================================================================ */
static void make_kbc_path(const char *src, char *out, size_t sz)
{
    strncpy(out, src, sz - 5);
    out[sz - 5] = '\0';
    /* 확장자 교체 */
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    strncat(out, ".kbc", 4);
}

/* ================================================================
 *  명령어: compile
 * ================================================================ */
static int cmd_compile(const char *src_path, const char *out_path)
{
    char auto_out[512];
    if (!out_path) {
        make_kbc_path(src_path, auto_out, sizeof(auto_out));
        out_path = auto_out;
    }

    printf("[kbc] 컴파일: %s → %s\n", src_path, out_path);
    KcModule *mod = compile_source(src_path);
    if (!mod) return 1;

    int r = kc_module_write(mod, out_path);
    kc_module_free(mod);
    if (r == 0)
        printf("[kbc] 완료: %s\n", out_path);
    return r == 0 ? 0 : 1;
}

/* ================================================================
 *  명령어: run
 * ================================================================ */
static int cmd_run(const char *kbc_path)
{
    KcModule *mod = kc_module_read(kbc_path);
    if (!mod) {
        fprintf(stderr, "[kbc] .kbc 파일 로드 실패: %s\n", kbc_path);
        return 1;
    }

    KcVM vm;
    kc_vm_init(&vm);
    int r = kc_vm_run(&vm, mod);
    kc_vm_free(&vm);
    kc_module_free(mod);
    return r == 0 ? 0 : 1;
}

/* ================================================================
 *  명령어: exec (컴파일 + 즉시 실행)
 * ================================================================ */
static int cmd_exec(const char *src_path)
{
    KcModule *mod = compile_source(src_path);
    if (!mod) return 1;

    KcVM vm;
    kc_vm_init(&vm);
    int r = kc_vm_run(&vm, mod);
    kc_vm_free(&vm);
    kc_module_free(mod);
    return r == 0 ? 0 : 1;
}

/* ================================================================
 *  명령어: dump (디스어셈블러)
 * ================================================================ */
static int cmd_dump(const char *kbc_path)
{
    KcModule *mod = kc_module_read(kbc_path);
    if (!mod) {
        fprintf(stderr, "[kbc] .kbc 파일 로드 실패: %s\n", kbc_path);
        return 1;
    }

    printf("=== Kcode 바이트코드 덤프 ===\n");
    printf("소스: %s\n", mod->source_name ? mod->source_name : "?");
    printf("버전: v%d.%d\n\n", mod->ver_maj, mod->ver_min);
    kc_chunk_dump(mod->main_chunk);
    kc_module_free(mod);
    return 0;
}

/* ================================================================
 *  도움말
 * ================================================================ */
static void print_usage(const char *prog)
{
    printf("Kcode 바이트코드 VM (KVM) v15.0.0\n\n");
    printf("사용법:\n");
    printf("  %s compile <소스.han> [출력.kbc]   소스 → 바이트코드 컴파일\n", prog);
    printf("  %s run     <파일.kbc>               바이트코드 실행\n", prog);
    printf("  %s exec    <소스.han>               컴파일 후 즉시 실행\n", prog);
    printf("  %s dump    <파일.kbc>               바이트코드 디스어셈블\n", prog);
    printf("\n");
    printf("예시:\n");
    printf("  %s compile 안녕.han\n", prog);
    printf("  %s run     안녕.kbc\n", prog);
    printf("  %s exec    안녕.han\n", prog);
}

/* ================================================================
 *  main
 * ================================================================ */
int main(int argc, char *argv[])
{
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "compile") == 0) {
        const char *out = (argc >= 4) ? argv[3] : NULL;
        return cmd_compile(argv[2], out);
    }
    else if (strcmp(cmd, "run") == 0) {
        return cmd_run(argv[2]);
    }
    else if (strcmp(cmd, "exec") == 0) {
        return cmd_exec(argv[2]);
    }
    else if (strcmp(cmd, "dump") == 0) {
        return cmd_dump(argv[2]);
    }
    else {
        fprintf(stderr, "알 수 없는 명령어: %s\n\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
