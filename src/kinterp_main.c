/*
 * kinterp_main.c  —  Kcode 인터프리터 드라이버
 * version : v30.0.0  (메인 버전 상속)
 *
 * v30.0.0 변경:
 *   - run_source() — kc_contract_check() 검증 패스 삽입
 *   - repl_exec_chunk() — REPL 모드 계약 검증 삽입 (⬜ REPL은 선택적 경고만)
 *
 * 변경 (v23.0.0):
 *   - REPL 모드 추가 — 인수 없이 실행 시 대화형 셸 진입
 *   - --test 옵션 추가 — 내장 테스트 실행
 *   - --repl 옵션 추가 — 명시적 REPL 진입
 *   - 멀티라인 입력 지원 — 콜론(:)으로 끝나는 줄 이후 자동 누적
 *   - REPL 내 오류 후 세션 유지 (인터프리터 상태 보존)
 *   - 변수/함수 세션 내 영속 — 한 번 선언하면 계속 사용 가능
 *
 * 빌드:
 *   gcc -std=c11 -Wall -Wextra -lm -o kcode \
 *       klexer.c kparser.c kinterp.c kinterp_main.c
 *
 * 실행:
 *   ./kcode               → REPL 모드 (대화형)
 *   ./kcode --repl        → REPL 모드 (명시적)
 *   ./kcode --test        → 내장 테스트 모드
 *   ./kcode [파일.han]    → 파일 실행 모드
 *
 * REPL 명령:
 *   종료() 또는 Ctrl+D   → 종료
 *   도움말()             → 도움말 출력
 *   초기화()             → 세션 변수 전체 초기화
 *
 * MIT License
 * zerojat7
 */

#include "klexer.h"
#include "kparser.h"
#include "kinterp.h"
#include "kc_contract_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  상수
 * ================================================================ */

#define REPL_LINE_BUF    4096     /* 단일 입력 줄 최대 길이 */
#define REPL_ACCUM_BUF   65536    /* 멀티라인 누적 버퍼 최대 */
#define REPL_HISTORY_MAX 128      /* 입력 히스토리 최대 항목 수 */

/* ================================================================
 *  파일 전체 읽기
 * ================================================================ */
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "파일을 열 수 없습니다: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "메모리 할당 실패\n");
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)size, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

/* ================================================================
 *  소스 실행 (테스트용 레이블 포함)
 * ================================================================ */
static int run_source(const char *src, const char *label) {
    printf("─────────────────────────────────\n");
    printf("[%s]\n", label);
    printf("─────────────────────────────────\n");

    Lexer lx;
    lexer_init(&lx, src, strlen(src));

    Parser parser;
    parser_init(&parser, &lx);
    Node *ast = parser_parse(&parser);

    if (parser.had_error) {
        for (int i = 0; i < parser.error_count; i++)
            fprintf(stderr, "[파서 오류] %s\n", parser.errors[i]);
        node_free(ast);
        return 1;
    }

    /* ★ 계약 시스템 강제 검증 패스 (v30.0.0) */
    KcContractCheckInfo cc_info;
    KcContractCheckResult cc = kc_contract_check(ast, &cc_info);
    if (cc == KC_CONTRACT_MISSING || cc == KC_CONTRACT_EMPTY_SOURCE) {
        kc_contract_check_print_error(&cc_info, NULL);
        node_free(ast);
        return 1;
    }
    /* ★ 검증 종료 */

    Interp interp;
    interp_init(&interp);
    interp_run(&interp, ast);

    int had_error = interp.had_error;
    interp_free(&interp);
    node_free(ast);
    printf("\n");
    return had_error;
}

/* ================================================================
 *  REPL — 도움말 출력
 * ================================================================ */
static void repl_print_help(void) {
    printf("\n");
    printf("┌──────────────────────────────────────────┐\n");
    printf("│  Kcode REPL  도움말                       │\n");
    printf("├──────────────────────────────────────────┤\n");
    printf("│  종료()          REPL 종료                │\n");
    printf("│  도움말()        이 도움말 출력            │\n");
    printf("│  초기화()        세션 변수 전체 초기화      │\n");
    printf("│                                          │\n");
    printf("│  코드 입력 후 Enter → 즉시 실행           │\n");
    printf("│  콜론(:)으로 끝나는 줄 → 멀티라인 모드    │\n");
    printf("│  멀티라인 모드에서 빈 줄 → 실행           │\n");
    printf("│                                          │\n");
    printf("│  예시:                                   │\n");
    printf("│    >>> 출력(\"안녕하세요\")                 │\n");
    printf("│    >>> 정수 가 = 10                      │\n");
    printf("│    >>> 출력(가 * 2)                      │\n");
    printf("│    >>> 함수 더하기(정수 가, 정수 나):      │\n");
    printf("│    ...     반환 가 + 나                  │\n");
    printf("│    ...                                   │\n");
    printf("│    >>> 출력(더하기(3, 7))                 │\n");
    printf("└──────────────────────────────────────────┘\n");
    printf("\n");
}

/* ================================================================
 *  REPL — 줄 끝 공백/개행 제거
 * ================================================================ */
static size_t repl_trim_newline(char *buf) {
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
        buf[--len] = '\0';
    return len;
}

/* ================================================================
 *  REPL — 줄이 콜론으로 끝나는지 확인 (멀티라인 트리거)
 * ================================================================ */
static int repl_ends_with_colon(const char *buf, size_t len) {
    if (len == 0) return 0;
    /* 줄 끝 공백 무시 */
    size_t i = len;
    while (i > 0 && (buf[i-1] == ' ' || buf[i-1] == '\t')) i--;
    return (i > 0 && buf[i-1] == ':');
}

/* ================================================================
 *  REPL — 한 청크 실행 (누적된 소스 파싱 + 실행)
 *  interp 는 호출자가 소유 — 세션 상태 보존
 * ================================================================ */
static void repl_exec_chunk(Interp *interp,
                             const char *src, size_t src_len) {
    if (src_len == 0) return;

    Lexer lx;
    lexer_init(&lx, src, src_len);

    Parser parser;
    parser_init(&parser, &lx);
    Node *ast = parser_parse(&parser);

    if (parser.had_error) {
        for (int i = 0; i < parser.error_count; i++)
            fprintf(stderr, "[오류] %s\n", parser.errors[i]);
        node_free(ast);
        return;
    }

    /* ★ 계약 검증 (REPL — 경고만, 세션 유지) */
    KcContractCheckInfo cc_info;
    KcContractCheckResult cc = kc_contract_check(ast, &cc_info);
    if (cc == KC_CONTRACT_MISSING || cc == KC_CONTRACT_EMPTY_SOURCE) {
        fprintf(stderr, "\n[계약 경고] REPL 입력에 계약 시스템이 없습니다.\n"
                        "  실행은 계속되지만, 파일 모드에서는 컴파일이 거부됩니다.\n\n");
    }
    /* ★ 검증 종료 */

    interp_run(interp, ast);

    /* REPL: 오류 후에도 세션 유지 */
    if (interp->had_error)
        interp->had_error = 0;

    node_free(ast);
}

/* ================================================================
 *  REPL 메인 루프
 * ================================================================ */
static void run_repl(void) {
    printf("\n");
    printf("  ██╗  ██╗ ██████╗ ██████╗ ██████╗ ███████╗\n");
    printf("  ██║ ██╔╝██╔════╝██╔═══██╗██╔══██╗██╔════╝\n");
    printf("  █████╔╝ ██║     ██║   ██║██║  ██║█████╗  \n");
    printf("  ██╔═██╗ ██║     ██║   ██║██║  ██║██╔══╝  \n");
    printf("  ██║  ██╗╚██████╗╚██████╔╝██████╔╝███████╗\n");
    printf("  ╚═╝  ╚═╝ ╚═════╝ ╚═════╝ ╚═════╝ ╚══════╝\n");
    printf("\n");
    printf("  Kcode v23.0.0 — 한글 프로그래밍 언어 REPL\n");
    printf("  종료: 종료()  |  도움말: 도움말()\n");
    printf("  ─────────────────────────────────────────\n\n");

    Interp interp;
    interp_init(&interp);

    char  line_buf[REPL_LINE_BUF];
    char  accum[REPL_ACCUM_BUF];
    int   accum_len  = 0;
    int   multiline  = 0;    /* 멀티라인 누적 모드 여부 */
    int   indent_depth = 0;  /* 현재 들여쓰기 깊이 추적 */

    for (;;) {
        /* ── 프롬프트 출력 ── */
        if (multiline)
            printf("... ");
        else
            printf(">>> ");
        fflush(stdout);

        /* ── 입력 읽기 ── */
        if (!fgets(line_buf, sizeof(line_buf), stdin)) {
            /* EOF (Ctrl+D) */
            printf("\n종료합니다.\n");
            break;
        }

        size_t len = repl_trim_newline(line_buf);

        /* ── 특수 명령 처리 ── */
        if (!multiline) {
            /* 종료 */
            if (strcmp(line_buf, "종료()") == 0 ||
                strcmp(line_buf, "exit")    == 0 ||
                strcmp(line_buf, "exit()")  == 0 ||
                strcmp(line_buf, "quit")    == 0) {
                printf("종료합니다.\n");
                break;
            }

            /* 도움말 */
            if (strcmp(line_buf, "도움말()") == 0 ||
                strcmp(line_buf, "help")      == 0) {
                repl_print_help();
                continue;
            }

            /* 세션 초기화 */
            if (strcmp(line_buf, "초기화()") == 0 ||
                strcmp(line_buf, "reset")     == 0) {
                interp_free(&interp);
                interp_init(&interp);
                accum_len   = 0;
                accum[0]    = '\0';
                multiline   = 0;
                indent_depth = 0;
                printf("[세션 초기화 완료 — 모든 변수/함수 삭제]\n");
                continue;
            }

            /* 빈 줄 무시 */
            if (len == 0) continue;
        }

        /* ── 멀티라인: 빈 줄 = 실행 트리거 ── */
        if (multiline && len == 0) {
            multiline    = 0;
            indent_depth = 0;

            /* 누적된 소스 실행 */
            accum[accum_len] = '\0';
            repl_exec_chunk(&interp, accum, (size_t)accum_len);

            /* 누적 버퍼 초기화 */
            accum_len = 0;
            accum[0]  = '\0';
            continue;
        }

        /* ── 누적 버퍼에 줄 추가 ── */
        int written = snprintf(accum + accum_len,
                               (size_t)(REPL_ACCUM_BUF - accum_len - 1),
                               "%s\n", line_buf);
        if (written > 0) accum_len += written;

        /* ── 콜론 감지 → 멀티라인 모드 진입 ── */
        if (repl_ends_with_colon(line_buf, len)) {
            multiline = 1;
            indent_depth++;
            continue;
        }

        /* ── 싱글라인 즉시 실행 ── */
        if (!multiline) {
            accum[accum_len] = '\0';
            repl_exec_chunk(&interp, accum, (size_t)accum_len);
            accum_len    = 0;
            accum[0]     = '\0';
            indent_depth = 0;
        }
    }

    interp_free(&interp);
}

/* ================================================================
 *  내장 테스트 케이스
 * ================================================================ */
static void run_builtin_tests(void) {
    printf("══════════════════════════════════════\n");
    printf("  Kcode v23.0.0 — 인터프리터 내장 테스트\n");
    printf("══════════════════════════════════════\n\n");

    /* 1. 기본 출력 */
    run_source(
        "출력(\"안녕하세요, Kcode!\")\n",
        "기본 출력"
    );

    /* 2. 변수 및 산술 */
    run_source(
        "정수 가 = 10\n"
        "정수 나 = 3\n"
        "출력(가 + 나)\n"
        "출력(가 - 나)\n"
        "출력(가 * 나)\n"
        "출력(가 / 나)\n"
        "출력(가 % 나)\n"
        "출력(가 ** 나)\n",
        "산술 연산"
    );

    /* 3. 조건문 */
    run_source(
        "정수 점수 = 85\n"
        "만약 점수 >= 90:\n"
        "    출력(\"A등급\")\n"
        "아니면 만약 점수 >= 80:\n"
        "    출력(\"B등급\")\n"
        "아니면:\n"
        "    출력(\"C등급\")\n",
        "만약/아니면"
    );

    /* 4. 동안 반복 */
    run_source(
        "정수 합 = 0\n"
        "정수 i = 1\n"
        "동안 i <= 10:\n"
        "    합 += i\n"
        "    i += 1\n"
        "출력(합)\n",
        "동안 (1~10 합계)"
    );

    /* 5. 반복 (범위) */
    run_source(
        "반복 i 부터 1 까지 5:\n"
        "    출력(i)\n",
        "반복 (부터/까지)"
    );

    /* 6. 배열 */
    run_source(
        "배열 과일 = [\"사과\", \"바나나\", \"포도\"]\n"
        "출력(과일[0])\n"
        "출력(과일.길이)\n"
        "각각 f 안에 과일:\n"
        "    출력(f)\n",
        "배열 및 각각"
    );

    /* 7. 함수 */
    run_source(
        "함수 더하기(정수 가, 정수 나):\n"
        "    반환 가 + 나\n"
        "\n"
        "정의 인사(문자 이름):\n"
        "    출력(\"안녕하세요, \" + 이름 + \"!\")\n"
        "\n"
        "출력(더하기(3, 7))\n"
        "인사(\"Kcode\")\n",
        "함수/정의"
    );

    /* 8. 재귀 — 팩토리얼 */
    run_source(
        "함수 팩토리얼(정수 n):\n"
        "    만약 n <= 1:\n"
        "        반환 1\n"
        "    반환 n * 팩토리얼(n - 1)\n"
        "\n"
        "출력(팩토리얼(10))\n",
        "재귀 (팩토리얼 10)"
    );

    /* 9. 내장 함수 */
    run_source(
        "출력(길이(\"안녕하세요\"))\n"
        "출력(절댓값(-42))\n"
        "출력(최대(3, 7, 2, 9, 1))\n"
        "출력(최소(3, 7, 2, 9, 1))\n"
        "출력(제곱근(144.0))\n",
        "내장 함수"
    );

    /* 10. 람다 */
    run_source(
        "함수형 두배 = (x) => x * 2\n"
        "출력(두배(5))\n",
        "람다"
    );

    /* 11. 선택 */
    run_source(
        "정수 값 = 2\n"
        "선택 값:\n"
        "    경우 1:\n"
        "        출력(\"하나\")\n"
        "    경우 2:\n"
        "        출력(\"둘\")\n"
        "    그외:\n"
        "        출력(\"기타\")\n",
        "선택/경우/그외"
    );

    /* 12. 시도/실패시 */
    run_source(
        "시도:\n"
        "    오류(\"테스트 오류입니다\")\n"
        "실패시:\n"
        "    출력(\"오류를 잡았습니다: \" + 오류)\n",
        "시도/실패시"
    );

    /* 13. 범위 + 각각 */
    run_source(
        "배열 숫자 = 범위(1, 6)\n"
        "각각 n 안에 숫자:\n"
        "    출력(n * n)\n",
        "범위 + 각각 (제곱)"
    );

    printf("══════════════════════════════════════\n");
    printf("  테스트 완료\n");
    printf("══════════════════════════════════════\n");
}

/* ================================================================
 *  main
 * ================================================================ */
int main(int argc, char *argv[]) {

    /* 인수 없음 → REPL (기본) */
    if (argc == 1) {
        run_repl();
        return 0;
    }

    /* --repl : 명시적 REPL */
    if (argc == 2 && strcmp(argv[1], "--repl") == 0) {
        run_repl();
        return 0;
    }

    /* --test : 내장 테스트 */
    if (argc == 2 && strcmp(argv[1], "--test") == 0) {
        run_builtin_tests();
        return 0;
    }

    /* --help */
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 ||
                      strcmp(argv[1], "-h")      == 0)) {
        printf("사용법:\n");
        printf("  kcode                파일 없이 실행 → REPL 모드\n");
        printf("  kcode --repl         REPL 모드 (명시적)\n");
        printf("  kcode --test         내장 테스트 실행\n");
        printf("  kcode <파일.han>     파일 실행 모드\n");
        printf("  kcode --help         이 도움말\n");
        return 0;
    }

    /* 파일 모드 */
    char *src = read_file(argv[1]);
    if (!src) return 1;

    Lexer lx;
    lexer_init(&lx, src, strlen(src));

    Parser parser;
    parser_init(&parser, &lx);
    Node *ast = parser_parse(&parser);

    if (parser.had_error) {
        for (int i = 0; i < parser.error_count; i++)
            fprintf(stderr, "%s\n", parser.errors[i]);
        node_free(ast);
        free(src);
        return 1;
    }

    Interp interp;
    interp_init(&interp);
    interp_run(&interp, ast);
    int had_error = interp.had_error;

    interp_free(&interp);
    node_free(ast);
    free(src);
    return had_error ? 1 : 0;
}
