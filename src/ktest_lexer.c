/*
 * ktest_lexer.c  —  Kcode 렉서 단위 테스트
 * version : v13.0.0
 *
 * 변경: UTF-8 오타 수정 — '문자'(0xEC,0xC0→0x9E), '건너뜀'(0xEB,0x9B→0x9C,0x84→0x80)
 *
 * 빌드:
 *   gcc -Wall -Wextra -std=c11 -o ktest_lexer ktest_lexer.c klexer.c
 * 실행:
 *   ./ktest_lexer
 */

#include "klexer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 *  테스트 헬퍼
 * ================================================================ */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else       { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

/* src를 렉싱해서 기대 토큰 배열과 비교 */
static void check_tokens(const char *label, const char *src,
                         const TokenType *expected, int exp_count)
{
    printf("\n[테스트] %s\n", label);
    printf("  소스: %s\n", src);

    Lexer lx;
    lexer_init(&lx, src, strlen(src));

    for (int i = 0; i < exp_count; i++) {
        Token t = lexer_next(&lx);
        char buf[128];
        token_to_str(&t, buf, sizeof(buf));

        char msg[256];
        snprintf(msg, sizeof(msg),
                 "토큰[%d] 기대=%s 실제=%s (값='%s')",
                 i, token_type_name(expected[i]),
                 token_type_name(t.type), buf);
        CHECK(t.type == expected[i], msg);
    }
    /* 마지막은 EOF여야 함 */
    Token last = lexer_next(&lx);
    char msg[64];
    snprintf(msg, sizeof(msg), "EOF로 끝남 (실제=%s)", token_type_name(last.type));
    CHECK(last.type == TOK_EOF, msg);
}

/* ================================================================
 *  개별 테스트 케이스
 * ================================================================ */

/* ── 정수 리터럴 ───────────────────────────────────────── */
static void test_integers(void)
{
    printf("\n===== 정수 리터럴 테스트 =====\n");

    /* 십진수 */
    {
        Lexer lx; lexer_init(&lx, "42", 2);
        Token t = lexer_next(&lx);
        CHECK(t.type == TOK_INT,       "42 → TOK_INT");
        CHECK(t.val.ival == 42,        "42 값 확인");
    }
    /* 음수는 '-' + 정수 두 토큰 */
    {
        Lexer lx; lexer_init(&lx, "-5", 2);
        Token t1 = lexer_next(&lx);
        Token t2 = lexer_next(&lx);
        CHECK(t1.type == TOK_MINUS,    "-5 첫 토큰 MINUS");
        CHECK(t2.type == TOK_INT,      "-5 두 번째 토큰 INT");
        CHECK(t2.val.ival == 5,        "-5 값=5");
    }
    /* 2진수 */
    {
        Lexer lx; lexer_init(&lx, "0b1010", 6);
        Token t = lexer_next(&lx);
        CHECK(t.type == TOK_BIN,       "0b1010 → TOK_BIN");
        CHECK(t.val.ival == 10,        "0b1010 값=10");
    }
    /* 8진수 */
    {
        Lexer lx; lexer_init(&lx, "0o17", 4);
        Token t = lexer_next(&lx);
        CHECK(t.type == TOK_OCT,       "0o17 → TOK_OCT");
        CHECK(t.val.ival == 15,        "0o17 값=15");
    }
    /* 16진수 */
    {
        Lexer lx; lexer_init(&lx, "0xFF", 4);
        Token t = lexer_next(&lx);
        CHECK(t.type == TOK_HEX,       "0xFF → TOK_HEX");
        CHECK(t.val.ival == 255,       "0xFF 값=255");
    }
}

/* ── 실수 리터럴 ───────────────────────────────────────── */
static void test_floats(void)
{
    printf("\n===== 실수 리터럴 테스트 =====\n");
    {
        Lexer lx; lexer_init(&lx, "3.14", 4);
        Token t = lexer_next(&lx);
        CHECK(t.type == TOK_FLOAT,     "3.14 → TOK_FLOAT");
        CHECK(t.val.fval > 3.13 && t.val.fval < 3.15, "3.14 값 근사 확인");
    }
    {
        Lexer lx; lexer_init(&lx, "1.5e2", 5);
        Token t = lexer_next(&lx);
        CHECK(t.type == TOK_FLOAT,     "1.5e2 → TOK_FLOAT");
        CHECK(t.val.fval > 149.9 && t.val.fval < 150.1, "1.5e2 값=150");
    }
}

/* ── 문자열 리터럴 ─────────────────────────────────────── */
static void test_strings(void)
{
    printf("\n===== 문자열 리터럴 테스트 =====\n");
    {
        const char *src = "\"안녕하세요\"";
        Lexer lx; lexer_init(&lx, src, strlen(src));
        Token t = lexer_next(&lx);
        CHECK(t.type == TOK_STRING,    "\"안녕하세요\" → TOK_STRING");
    }
    {
        const char *src = "\"hello\\n\"";
        Lexer lx; lexer_init(&lx, src, strlen(src));
        Token t = lexer_next(&lx);
        CHECK(t.type == TOK_STRING,    "이스케이프 포함 문자열 → TOK_STRING");
    }
}

/* ── 글자 리터럴 ───────────────────────────────────────── */
static void test_chars(void)
{
    printf("\n===== 글자 리터럴 테스트 =====\n");
    {
        const char *src = "'A'";
        Lexer lx; lexer_init(&lx, src, strlen(src));
        Token t = lexer_next(&lx);
        CHECK(t.type == TOK_CHAR_LIT,  "'A' → TOK_CHAR_LIT");
        CHECK(t.val.cval == 'A',       "'A' 값='A'");
    }
}

/* ── 한글 키워드 ───────────────────────────────────────── */
static void test_keywords(void)
{
    printf("\n===== 한글 키워드 테스트 =====\n");

    struct { const char *src; TokenType expected; const char *label; } cases[] = {
        /* 자료형 */
        { "\xEC\xa0\x95\xEC\x88\x98",                     TOK_KW_JEONGSU,    "정수" },
        { "\xEC\x8B\xA4\xEC\x88\x98",                     TOK_KW_SILSU,      "실수" },
        { "\xEB\xAC\xB8\xEC\x9E\x90",                     TOK_KW_MUNJA,      "문자" },
        { "\xEB\x85\xBC\xEB\xA6\xAC",                     TOK_KW_NOLI,       "논리" },
        /* 제어문 */
        { "\xEB\xA7\x8C\xEC\x95\xBD",                     TOK_KW_MANYAK,     "만약" },
        { "\xEC\x95\x84\xEB\x8B\x88\xEB\xA9\xB4",         TOK_KW_ANIMYEON,   "아니면" },
        { "\xEB\xB0\x98\xEB\xB3\xB5",                     TOK_KW_BANBOG,     "반복" },
        { "\xEB\x8F\x99\xEC\x95\x88",                     TOK_KW_DONGAN,     "동안" },
        { "\xEA\xB0\x81\xEA\xB0\x81",                     TOK_KW_GAKGAK,     "각각" },
        { "\xEB\xA9\x88\xEC\xB6\xA4",                     TOK_KW_MEOMCHUM,   "멈춤" },
        { "\xEA\xB1\xB4\xEB\x84\x88\xEB\x9C\x80",         TOK_KW_GEONNEO,    "건너뜀" },
        /* 함수 */
        { "\xED\x95\xA8\xEC\x88\x98",                     TOK_KW_HAMSU,      "함수" },
        { "\xEC\xa0\x95\xEC\x9D\x98",                     TOK_KW_JEONGUI,    "정의" },
        { "\xEB\xB0\x98\xED\x99\x98",                     TOK_KW_BANHWAN,    "반환" },
        /* 논리 연산자 */
        { "\xEA\xB7\xB8\xEB\xA6\xAC\xEA\xB3\xA0",        TOK_KW_AND,        "그리고" },
        { "\xEB\x98\x90\xEB\x8A\x94",                     TOK_KW_OR,         "또는" },
        /* 불리언 */
        { "\xEC\xB0\xB8",                                 TOK_TRUE,          "참" },
        { "\xEA\xB1\xB0\xEC\xa7\x93",                     TOK_FALSE,         "거짓" },
        { NULL, 0, NULL }
    };

    for (int i = 0; cases[i].src; i++) {
        Lexer lx;
        lexer_init(&lx, cases[i].src, strlen(cases[i].src));
        Token t = lexer_next(&lx);
        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' → %s (실제=%s)",
                 cases[i].label, token_type_name(cases[i].expected),
                 token_type_name(t.type));
        CHECK(t.type == cases[i].expected, msg);
    }
}

/* ── 연산자 ─────────────────────────────────────────────── */
static void test_operators(void)
{
    printf("\n===== 연산자 테스트 =====\n");

    struct { const char *src; TokenType exp; const char *label; } ops[] = {
        { "+",  TOK_PLUS,     "+" },
        { "-",  TOK_MINUS,    "-" },
        { "*",  TOK_STAR,     "*" },
        { "/",  TOK_SLASH,    "/" },
        { "%",  TOK_PERCENT,  "%" },
        { "**", TOK_STARSTAR, "**" },
        { "==", TOK_EQEQ,    "==" },
        { "!=", TOK_BANGEQ,  "!=" },
        { ">=", TOK_GTEQ,    ">=" },
        { "<=", TOK_LTEQ,    "<=" },
        { "=>", TOK_ARROW,   "=>" },
        { "...", TOK_DOTS,   "..." },
        { "+=", TOK_PLUSEQ,  "+=" },
        { "-=", TOK_MINUSEQ, "-=" },
        { "<<", TOK_LTLT,    "<<" },
        { ">>", TOK_GTGT,    ">>" },
        { NULL, 0, NULL }
    };
    for (int i = 0; ops[i].src; i++) {
        Lexer lx;
        lexer_init(&lx, ops[i].src, strlen(ops[i].src));
        Token t = lexer_next(&lx);
        char msg[64];
        snprintf(msg, sizeof(msg), "'%s' (실제=%s)", ops[i].label,
                 token_type_name(t.type));
        CHECK(t.type == ops[i].exp, msg);
    }
}

/* ── 전처리기 ───────────────────────────────────────────── */
static void test_preprocessor(void)
{
    printf("\n===== 전처리기 테스트 =====\n");

    struct { const char *src; TokenType exp; const char *label; } pp[] = {
        { "#\xEC\xa0\x95\xEC\x9D\x98",               TOK_PP_DEFINE,  "#정의" },
        { "#\xEB\xA7\x8C\xEC\x95\xBD",               TOK_PP_IF,      "#만약" },
        { "#\xEC\x95\x84\xEB\x8B\x88\xEB\xA9\xB4",   TOK_PP_ELSE,    "#아니면" },
        { "#\xEB\x81\x9D",                            TOK_PP_ENDIF,   "#끝" },
        { "#\xED\x8F\xAC\xED\x95\xA8",               TOK_PP_INCLUDE, "#포함" },
        { "#GPU\xEC\x82\xAC\xEC\x9A\xA9",            TOK_PP_GPU,     "#GPU사용" },
        { NULL, 0, NULL }
    };
    for (int i = 0; pp[i].src; i++) {
        Lexer lx;
        lexer_init(&lx, pp[i].src, strlen(pp[i].src));
        Token t = lexer_next(&lx);
        char msg[64];
        snprintf(msg, sizeof(msg), "'%s' (실제=%s)", pp[i].label,
                 token_type_name(t.type));
        CHECK(t.type == pp[i].exp, msg);
    }
}

/* ── 들여쓰기 INDENT/DEDENT ─────────────────────────────── */
static void test_indent(void)
{
    printf("\n===== 들여쓰기 INDENT/DEDENT 테스트 =====\n");

    /*
     * 만약 참:
     *     출력("안녕")
     * 출력("끝")
     *
     * 기대 토큰 흐름:
     *   KW_만약 TRUE COLON NEWLINE
     *   INDENT KW_출력 LPAREN STRING RPAREN NEWLINE
     *   DEDENT KW_출력 LPAREN STRING RPAREN NEWLINE
     *   EOF
     */
    const char *src =
        "\xEB\xA7\x8C\xEC\x95\xBD"         /* 만약 */
        " \xEC\xB0\xB8"                      /* 참  */
        ":\n"
        "    \xEC\xB6\x9C\xEB\xa0\xa5"      /* 출력 */
        "(\"hi\")\n"
        "\xEC\xB6\x9C\xEB\xa0\xa5"          /* 출력 */
        "(\"bye\")\n";

    Lexer lx;
    lexer_init(&lx, src, strlen(src));

    TokenType expected[] = {
        TOK_KW_MANYAK,      /* 만약 */
        TOK_TRUE,           /* 참   */
        TOK_COLON,          /* :    */
        TOK_NEWLINE,
        TOK_INDENT,
        TOK_KW_CHULRYEOK,   /* 출력 */
        TOK_LPAREN,
        TOK_STRING,
        TOK_RPAREN,
        TOK_NEWLINE,
        TOK_DEDENT,
        TOK_KW_CHULRYEOK,   /* 출력 */
        TOK_LPAREN,
        TOK_STRING,
        TOK_RPAREN,
        TOK_NEWLINE,
        TOK_EOF
    };
    int n = (int)(sizeof(expected) / sizeof(expected[0]));

    int ok = 1;
    for (int i = 0; i < n; i++) {
        Token t = lexer_next(&lx);
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "INDENT 테스트 토큰[%d] 기대=%s 실제=%s",
                 i, token_type_name(expected[i]),
                 token_type_name(t.type));
        if (t.type != expected[i]) {
            ok = 0;
            printf("  [FAIL] %s\n", msg);
            g_fail++;
        } else {
            printf("  [PASS] %s\n", msg);
            g_pass++;
        }
    }
    (void)ok;
}

/* ── 실제 Kcode 코드 스니펫 ─────────────────────────────── */
static void test_real_snippet(void)
{
    printf("\n===== 실제 코드 스니펫 테스트 =====\n");

    /* 정수 나이 = 25 */
    const char *src1 =
        "\xEC\xa0\x95\xEC\x88\x98"   /* 정수 */
        " \xEB\x82\x98\xEC\x9D\xB4"  /* 나이 */
        " = 25";

    TokenType exp1[] = { TOK_KW_JEONGSU, TOK_IDENT, TOK_EQ, TOK_INT };
    check_tokens("변수 선언: 정수 나이 = 25", src1, exp1, 4);

    /* 함수형 제곱 = (정수 x) => x * x */
    const char *src2 =
        "\xED\x95\xA8\xEC\x88\x98\xED\x98\x95"  /* 함수형 */
        " abc = (x) => x * x";

    TokenType exp2[] = {
        TOK_KW_HAMSUFORM, TOK_IDENT, TOK_EQ, TOK_LPAREN, TOK_IDENT,
        TOK_RPAREN, TOK_ARROW, TOK_IDENT, TOK_STAR, TOK_IDENT
    };
    check_tokens("람다 선언: 함수형 abc = (x) => x * x", src2, exp2, 10);
}

/* ── 줄/열 번호 추적 ────────────────────────────────────── */
static void test_location(void)
{
    printf("\n===== 줄/열 번호 테스트 =====\n");
    const char *src = "abc\ndef";
    Lexer lx; lexer_init(&lx, src, strlen(src));

    Token t1 = lexer_next(&lx); /* abc */
    CHECK(t1.line == 1, "첫 번째 토큰 줄=1");

    Token nl = lexer_next(&lx); /* NEWLINE */
    (void)nl;

    /* 다음 줄 시작에서 들여쓰기 처리 발생, NEWLINE 토큰 건너뜀 */
    Token t2 = lexer_next(&lx);
    /* NEWLINE이거나 IDENT일 수 있으므로 NEWLINE이면 한번 더 */
    if (t2.type == TOK_NEWLINE) t2 = lexer_next(&lx);

    CHECK(t2.line == 2, "두 번째 줄 토큰 줄=2");
}

/* ── 주석 무시 ──────────────────────────────────────────── */
static void test_comments(void)
{
    printf("\n===== 주석 테스트 =====\n");
    {
        const char *src = "123 // 이것은 주석\n456";
        Lexer lx; lexer_init(&lx, src, strlen(src));
        Token t1 = lexer_next(&lx);
        CHECK(t1.type == TOK_INT && t1.val.ival == 123, "주석 앞 정수=123");
        Token nl = lexer_next(&lx);
        (void)nl;
        Token t2 = lexer_next(&lx);
        if (t2.type == TOK_NEWLINE) t2 = lexer_next(&lx);
        CHECK(t2.type == TOK_INT && t2.val.ival == 456, "주석 다음 줄 정수=456");
    }
    {
        const char *src = "1 /* 블록\n주석 */ 2";
        Lexer lx; lexer_init(&lx, src, strlen(src));
        Token t1 = lexer_next(&lx);
        CHECK(t1.type == TOK_INT && t1.val.ival == 1, "블록주석 앞 정수=1");
        Token t2 = lexer_next(&lx);
        CHECK(t2.type == TOK_INT && t2.val.ival == 2, "블록주석 뒤 정수=2");
    }
}

/* ================================================================
 *  main
 * ================================================================ */
int main(void)
{
    printf("=================================================\n");
    printf("  Kcode 렉서 단위 테스트  (v0.7.0)\n");
    printf("=================================================\n");

    test_integers();
    test_floats();
    test_strings();
    test_chars();
    test_keywords();
    test_operators();
    test_preprocessor();
    test_indent();
    test_real_snippet();
    test_location();
    test_comments();

    printf("\n=================================================\n");
    printf("  결과: %d 통과 / %d 실패\n", g_pass, g_fail);
    printf("=================================================\n");

    return g_fail > 0 ? 1 : 0;
}
