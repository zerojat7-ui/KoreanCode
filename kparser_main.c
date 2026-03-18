/*
 * kparser_main.c  —  Kcode 파서 테스트 드라이버
 * version : v13.0.0
 *
 * 빌드:
 *   gcc -std=c11 -Wall -Wextra -o kcode_parser klexer.c kparser.c kparser_main.c
 *
 * 실행:
 *   ./kcode_parser [파일.han]    파일 모드
 *   ./kcode_parser               내장 테스트 모드
 *
 * MIT License
 * zerojat7
 */

#include "klexer.h"
#include "kparser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  파일 읽기
 * ================================================================ */
static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)len;
    return buf;
}

/* ================================================================
 *  내장 테스트 소스
 * ================================================================ */
static const char *TEST_SRC =
    "// Kcode 파서 통합 테스트\n"
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
    "\n"
    "함수 더하기(정수 가, 정수 나):\n"
    "    반환 가 + 나\n"
    "\n"
    "정의 인사출력(문자 이름):\n"
    "    출력(\"안녕하세요, \" + 이름)\n"
    "    만약 이름 == \"홍길동\":\n"
    "        출력(\"반갑습니다!\")\n"
    "    아니면 만약 이름 == \"손님\":\n"
    "        출력(\"어서오세요!\")\n"
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
    "    정의 소개(자신):\n"
    "        출력(자신.이름 + \" / \" + 자신.나이)\n"
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
    "선택 버전:\n"
    "    경우 7:\n"
    "        출력(\"최신 버전\")\n"
    "    경우 6:\n"
    "        출력(\"이전 버전\")\n"
    "    그외:\n"
    "        출력(\"알 수 없는 버전\")\n"
    "\n"
    "정수 a = 10\n"
    "a += 5\n"
    "논리 c = a >= 15 그리고 아니다 a == 0 또는 참\n"
    "\n"
    "GPU로:\n"
    "    행렬곱(A, B)\n"
    "끝GPU\n"
;

/* ================================================================
 *  메인
 * ================================================================ */
int main(int argc, char *argv[]) {
    const char *src  = NULL;
    char       *heap = NULL;
    size_t      slen = 0;

    if (argc >= 2) {
        heap = read_file(argv[1], &slen);
        if (!heap) return 1;
        src = heap;
        printf("=== 파일: %s ===\n\n", argv[1]);
    } else {
        src  = TEST_SRC;
        slen = strlen(TEST_SRC);
        printf("=== 내장 통합 테스트 ===\n\n");
    }

    /* 렉서 초기화 */
    Lexer lexer;
    lexer_init(&lexer, src, slen);

    /* 파서 초기화 */
    Parser parser;
    parser_init(&parser, &lexer);

    /* 파싱 */
    Node *ast = parser_parse(&parser);

    /* 결과 출력 */
    printf("=== AST ===\n");
    ast_print(ast, 0);

    /* 오류 요약 */
    printf("\n=== 결과 ===\n");
    if (parser.error_count == 0) {
        printf("파싱 성공 ✓  (오류 없음)\n");
    } else {
        printf("파싱 오류 %d개:\n", parser.error_count);
        for (int i = 0; i < parser.error_count; i++)
            printf("  [%d] %s\n", i + 1, parser.errors[i]);
    }

    /* 노드 수 집계 */
    int node_count = 0;
    void count_nodes(const Node *n) {
        if (!n) return;
        node_count++;
        for (int i = 0; i < n->child_count; i++)
            count_nodes(n->children[i]);
    }
    count_nodes(ast);
    printf("AST 노드 수: %d\n", node_count);

    node_free(ast);
    free(heap);
    return parser.had_error ? 1 : 0;
}
