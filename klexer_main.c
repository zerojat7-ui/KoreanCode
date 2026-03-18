/*
 * klexer_main.c  —  Kcode 렉서 테스트 드라이버
 * version : v13.0.0
 *
 * 빌드:
 *   gcc -std=c11 -Wall -Wextra -o kcode_lexer klexer.c klexer_main.c
 *
 * 실행:
 *   ./kcode_lexer [파일.han]        // 파일 모드
 *   ./kcode_lexer                   // stdin 모드
 *
 * MIT License
 * zerojat7
 */

#include "klexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  소스 파일 전체 읽기
 * ================================================================ */
static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

/* ================================================================
 *  토큰 출력
 * ================================================================ */
static void print_token(const Token *tok) {
    char text[128];
    token_to_str(tok, text, sizeof(text));

    /* 특수 문자 시각화 */
    if (tok->type == TOK_NEWLINE) { strcpy(text, "\\n"); }
    if (tok->type == TOK_INDENT)  { strcpy(text, ">>"); }
    if (tok->type == TOK_DEDENT)  { strcpy(text, "<<"); }
    if (tok->type == TOK_EOF)     { strcpy(text, "<EOF>"); }

    printf("[%3d:%2d] %-18s '%s'",
           tok->line, tok->col,
           token_type_name(tok->type),
           text);

    /* 값 출력 */
    switch (tok->type) {
        case TOK_INT:
        case TOK_BIN:
        case TOK_OCT:
        case TOK_HEX:
            printf("  => %lld", (long long)tok->val.ival);
            break;
        case TOK_FLOAT:
            printf("  => %g", tok->val.fval);
            break;
        case TOK_CHAR_LIT:
            printf("  => U+%04X", tok->val.cval);
            break;
        default:
            break;
    }
    putchar('\n');
}

/* ================================================================
 *  내장 테스트 케이스
 * ================================================================ */
static const char *BUILTIN_TEST =
    "// Kcode 렉서 통합 테스트\n"
    "\n"
    "#정의 최대크기 100\n"
    "#GPU사용\n"
    "\n"
    "가짐 수학\n"
    "가짐 파일처리 로부터 읽기\n"
    "\n"
    "고정 정수 버전 = 7\n"
    "실수 파이값 = 3.14\n"
    "문자 인사 = \"안녕하세요\"\n"
    "논리 성인 = 참\n"
    "글자 첫글자 = '홍'\n"
    "정수 이진수 = 0b1010\n"
    "정수 팔진수 = 0o17\n"
    "정수 십육진수 = 0xFF\n"
    "\n"
    "함수 더하기(정수 가, 정수 나):\n"
    "    반환 가 + 나\n"
    "\n"
    "정의 인사출력(문자 이름 = \"손님\"):\n"
    "    출력(\"안녕하세요, \" + 이름)\n"
    "    만약 이름 == \"홍길동\":\n"
    "        출력(\"반갑습니다!\")\n"
    "    아니면:\n"
    "        출력(\"환영합니다.\")\n"
    "\n"
    "객체 사람:\n"
    "    문자 이름\n"
    "    정수 나이\n"
    "\n"
    "    함수 생성(자신, 문자 이름, 정수 나이):\n"
    "        자신.이름 = 이름\n"
    "        자신.나이 = 나이\n"
    "\n"
    "배열 숫자들 = [1, 2, 3, 4, 5]\n"
    "함수형 제곱 = (정수 x) => x * x\n"
    "\n"
    "동안 참:\n"
    "    정수 결과 = 더하기(3, 5)\n"
    "    멈춤\n"
    "\n"
    "반복 i 부터 1 까지 10:\n"
    "    출력없이(i)\n"
    "\n"
    "각각 값 안에 숫자들:\n"
    "    출력(값)\n"
    "\n"
    "시도:\n"
    "    파일열기(\"data.han\", \"읽기\")\n"
    "실패시:\n"
    "    오류(\"파일을 열 수 없습니다\")\n"
    "항상:\n"
    "    출력(\"완료\")\n"
    "\n"
    "// 이미지/AI\n"
    "사진 원본 = 사진열기(\"photo.jpg\")\n"
    "AI 모델 = AI연결(\"claude\", \"api키\")\n"
    "그림 캔버스 = 그림만들기(800, 600)\n"
    "화면3D 창 = 화면3D열기(1280, 720, \"앱\")\n"
    "\n"
    "GPU로:\n"
    "    행렬곱(A, B)\n"
    "끝GPU\n"
    "\n"
    "// 연산자 테스트\n"
    "정수 a = 10\n"
    "a += 5\n"
    "a -= 2\n"
    "a *= 3\n"
    "a **= 2\n"
    "정수 b = a % 7\n"
    "논리 c = a >= b 그리고 아니다 b == 0 또는 참\n"
    "정수 d = a & b | b ^ a\n"
    "정수 e = a << 2\n"
    "정수 f = a >> 1\n"
    "\n"
    "선택 a:\n"
    "    경우 0:\n"
    "        출력(\"영\")\n"
    "    경우 1:\n"
    "        출력(\"일\")\n"
    "    그외:\n"
    "        출력(\"기타\")\n"
;

/* ================================================================
 *  메인
 * ================================================================ */
int main(int argc, char *argv[]) {
    char   *src  = NULL;
    size_t  slen = 0;
    int     use_builtin = 0;

    if (argc >= 2) {
        src = read_file(argv[1], &slen);
        if (!src) return 1;
        printf("=== 파일: %s ===\n\n", argv[1]);
    } else {
        /* 내장 테스트 */
        src = (char *)BUILTIN_TEST;
        slen = strlen(BUILTIN_TEST);
        use_builtin = 1;
        printf("=== 내장 통합 테스트 ===\n\n");
    }

    Lexer lx;
    lexer_init(&lx, src, slen);

    int tok_count = 0;
    int err_count = 0;

    while (1) {
        Token tok = lexer_next(&lx);
        print_token(&tok);
        tok_count++;
        if (tok.type == TOK_ERROR) err_count++;
        if (tok.type == TOK_EOF)   break;
    }

    printf("\n--- 결과: 토큰 %d개", tok_count);
    if (err_count) printf(", 오류 %d개: %s", err_count, lx.error_msg);
    else           printf(", 오류 없음 ✓");
    printf(" ---\n");

    if (!use_builtin) free(src);
    return err_count > 0 ? 1 : 0;
}
