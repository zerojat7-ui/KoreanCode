/*

- kcodegen_llvm_main.c  -  Kcode LLVM 코드 생성기 드라이버
- version : v30.0.0  (메인 버전 상속)
-
- v30.0.0 변경:
-   - compile_source() — kc_contract_check() 검증 패스 삽입
- 
- 실행 모드:
- ./kcode_llvm                          내장 테스트
- ./kcode_llvm <파일.han>               LLVM IR stdout 출력
- ./kcode_llvm <파일.han> -o <out.ll>   IR 파일 저장
- ./kcode_llvm <파일.han> -b            IR 생성 + clang 빌드 (네이티브 실행파일)
- ./kcode_llvm <파일.han> -b -o <out>   빌드 후 실행파일 이름 지정
- ./kcode_llvm <파일.han> -bc          LLVM 비트코드(.bc) 저장
- ./kcode_llvm <파일.han> -json        IDE 연동 JSON 출력 (stdout)
- ./kcode_llvm <파일.han> -unopt       최적화 전 IR 출력
- ./kcode_llvm -ide                    IDE 서버 모드 (stdin JSON 수신)
- 
- IDE 연동 프로토콜 (-ide 모드):
- stdin  ← {"action":"compile", "source":"…han 소스…"}
- stdout → {
- ```
  "success": true|false,

- ```
  "ir_text": "LLVM IR (최적화 후)",

- ```
  "ir_text_unopt": "LLVM IR (최적화 전)",

- ```
  "sourcemap": [{"han_line":N, "han_col":N, "ir_line":N}, ...],

- ```
  "errors":   [{"line":N, "col":N, "msg":"..."}, ...],

- ```
  "stats":    {"func_count":N, "var_count":N, "ir_line_count":N}

- }
- 
- 빌드:
- make LLVM=1
- 또는:
- gcc $(llvm-config -cflags) -DKCODE_LLVM \
- ```
    -o kcode_llvm \

- ```
    klexer.c kparser.c kcodegen_llvm.c kcodegen_llvm_main.c \

- ```
    $(llvm-config --ldflags --libs core analysis native bitwriter)

- 
- MIT License
- zerojat7
  */

#ifdef KCODE_LLVM

#include "klexer.h"
#include "kparser.h"
#include "kcodegen_llvm.h"
#include "kc_contract_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ================================================================

- 파일 읽기
- ================================================================ */
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

- stdin 전체 읽기 (IDE 서버 모드용)
- ================================================================ */
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

- 소스 → LLVMCodegenResult 파이프라인
- 렉서 → 파서 → AST → LLVM IR 변환
- ================================================================ */
  static LLVMCodegenResult *compile_source(const char *src,
  const char *module_name) {
  /* 렉서 초기화 */
  Lexer lx;
  lexer_init(&lx, src, strlen(src));
  
  /* 파서 초기화 및 파싱 */
  Parser parser;
  parser_init(&parser, &lx);
  Node *ast = parser_parse(&parser);
  
  /* 파서 오류 처리 - LLVMCodegenResult 오류로 변환 */
  if (parser.had_error) {
  LLVMCodegenResult *r = (LLVMCodegenResult*)calloc(1,
  sizeof(LLVMCodegenResult));
  r->had_error = 1;
  for (int i = 0;
  i < parser.error_count && i < LLVM_CODEGEN_MAX_ERRORS; i++) {
  LLVMCodegenError *e = &r->errors[r->error_count++];
  e->line = 0;
  e->col  = 0;
  snprintf(e->msg, sizeof(e->msg),
  "[파서] %s", parser.errors[i]);
  }
  node_free(ast);
  return r;
  }
  
  /* ★ 계약 시스템 강제 검증 패스 (v30.0.0) */
  KcContractCheckInfo cc_info;
  KcContractCheckResult cc = kc_contract_check(ast, &cc_info);
  if (cc == KC_CONTRACT_MISSING || cc == KC_CONTRACT_EMPTY_SOURCE) {
    kc_contract_check_print_error(&cc_info, module_name);
    LLVMCodegenResult *r2 = (LLVMCodegenResult*)calloc(1,
        sizeof(LLVMCodegenResult));
    r2->had_error = 1;
    LLVMCodegenError *e = &r2->errors[r2->error_count++];
    e->line = 0; e->col = 0;
    snprintf(e->msg, sizeof(e->msg), "%s", cc_info.error_msg);
    node_free(ast);
    return r2;
  }
  /* ★ 검증 종료 */

  /* LLVM IR 생성 */
  LLVMCodegenResult *r = llvm_codegen_run(ast, module_name);
  node_free(ast);
  return r;
  }

/* ================================================================

- JSON 문자열에서 "source" 필드 추출 (경량 파서)
- IDE에서 {"action":"compile","source":"…"} 를 보낼 때 사용
- ================================================================ */
  static char *extract_json_source(const char *json) {
  const char *key = ""source"";
  const char *p   = strstr(json, key);
  if (!p) return NULL;
  p += strlen(key);
  while (*p == ' ' || *p == ':') p++;
  if (*p != '"') return NULL;
  p++; /* 여는 따옴표 건너뜀 */
  
  size_t cap = 64 * 1024;
  char  *out = (char*)malloc(cap);
  if (!out) return NULL;
  size_t len = 0;
  
  while (*p) {
  if (len + 4 >= cap) {
  cap *= 2;
  out = (char*)realloc(out, cap);
  if (!out) return NULL;
  }
  if (*p == '\\' && *(p + 1)) {
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

- IDE 서버 모드
- stdin으로 JSON 요청을 받고 stdout으로 JSON 응답을 보낸다.
- 
- 요청: {"action":"compile", "source":"…한글 Kcode 소스…"}
- 응답: llvm_codegen_to_json() 포맷 참조
- ================================================================ */
  static void ide_server_mode(void) {
  fprintf(stderr, "[Kcode LLVM] IDE 서버 모드 시작\n");
  fprintf(stderr, "stdin으로 JSON 요청을 보내세요.\n");
  fprintf(stderr, "형식: {"action":"compile","source":"…"}\n\n");
  
  char *json = read_stdin();
  if (!json) {
  fprintf(stdout,
  "{"success":false,"errors":[{"line":0,"col":0,"
  ""msg":"stdin 읽기 실패"}]}\n");
  return;
  }
  
  /* action 확인 */
  if (!strstr(json, ""compile"")) {
  fprintf(stdout,
  "{"success":false,"errors":[{"line":0,"col":0,"
  ""msg":"알 수 없는 action"}]}\n");
  free(json);
  return;
  }
  
  char *src = extract_json_source(json);
  free(json);
  
  if (!src) {
  fprintf(stdout,
  "{"success":false,"errors":[{"line":0,"col":0,"
  ""msg":"source 필드를 찾을 수 없습니다"}]}\n");
  return;
  }
  
  LLVMCodegenResult *r = compile_source(src, "ide_input");
  free(src);
  
  llvm_codegen_to_json(r, stdout);
  llvm_codegen_result_free(r);
  }

/* ================================================================

- 내장 테스트 - 기본 동작 확인용
- ================================================================ */
  static void run_builtin_test(void) {
  printf("══════════════════════════════════════════════\n");
  printf("  Kcode v3.2.0 - LLVM IR 코드 생성기 내장 테스트\n");
  printf("══════════════════════════════════════════════\n\n");
  
  /* 테스트 소스 (UTF-8 한글):
  - 함수 더하기(정수 가, 정수 나):
  - ```
      반환 가 + 나

  - 
  - 정수 결과 = 더하기(3, 7)
  - 출력(결과)
  - 
  - 반복 i 부터 1 까지 5:
  - ```
      출력(i)

  */
  const char *src =
  "\xED\x95\xA8\xEC\x88\x98 "
  "\xEB\x8D\x94\xED\x95\x98\xEA\xB8\xB0"
  "(\xEC\xa0\x95\xEC\x88\x98 \xEA\xB0\x80"
  ", \xEC\xa0\x95\xEC\x88\x98 \xEB\x82\x98):\n"
  "    \xEB\xB0\x98\xED\x99\x98 "
  "\xEA\xB0\x80 + \xEB\x82\x98\n\n"
  "\xEC\xa0\x95\xEC\x88\x98 "
  "\xEA\xB2\xB0\xEA\xB3\xBC = "
  "\xEB\x8D\x94\xED\x95\x98\xEA\xB8\xB0(3, 7)\n"
  "\xEC\xB6\x9C\xEB\xa0\xa5("
  "\xEA\xB2\xB0\xEA\xB3\xBC)\n"
  "\xEB\xB0\x98\xEB\xB3\xB5 i "
  "\xEB\xB6\x80\xED\x84\xB0 1 "
  "\xEA\xB9\x8C\xEC\xa7\x80 5:\n"
  "    \xEC\xB6\x9C\xEB\xa0\xa5(i)\n";
  
  printf("[테스트 소스 (한글)]\n");
  printf("--------------\n");
  printf("%s\n", src);
  
  LLVMCodegenResult *r = compile_source(src, "test_module");
  
  if (r->had_error) {
  printf("[오류 목록]\n");
  for (int i = 0; i < r->error_count; i++) {
  printf("  줄 %d, 열 %d: %s\n",
  r->errors[i].line,
  r->errors[i].col,
  r->errors[i].msg);
  }
  } else {
  printf("[생성된 LLVM IR (최적화 후)]\n");
  printf("--------------\n");
  if (r->ir_text) printf("%s\n", r->ir_text);

   if (r->ir_text_unopt) {
       printf("[생성된 LLVM IR (최적화 전)]\n");
       printf("----------------------------\n");
       printf("%s\n", r->ir_text_unopt);
   }
  
   printf("[통계]\n");
   printf("  함수: %d개 / 변수: %d개 / IR 라인: %d줄\n",
          r->func_count, r->var_count, r->ir_line_count);
   printf("  소스맵 엔트리: %d개\n", r->sourcemap_count);

  }
  
  llvm_codegen_result_free(r);
  
  printf("\n══════════════════════════════════════════════\n");
  printf("  완료\n");
  printf("══════════════════════════════════════════════\n");
  }

/* ================================================================

- 사용법 출력
- ================================================================ */
  static void print_usage(const char *prog) {
  fprintf(stderr,
  "사용법:\n"
  "  %s                              내장 테스트\n"
  "  %s <파일.han>                   LLVM IR stdout 출력\n"
  "  %s <파일.han> -o <out.ll>       IR 파일 저장\n"
  "  %s <파일.han> -b                IR 생성 + clang 빌드\n"
  "  %s <파일.han> -b -o <out>       빌드 후 실행파일 이름 지정\n"
  "  %s <파일.han> -bc              LLVM 비트코드(.bc) 저장\n"
  "  %s <파일.han> -json            IDE 연동 JSON 출력\n"
  "  %s <파일.han> -unopt           최적화 전 IR 출력\n"
  "  %s -ide                        IDE 서버 모드 (stdin JSON)\n",
  prog, prog, prog, prog, prog,
  prog, prog, prog, prog);
  }

/* ================================================================

- clang으로 .ll → 네이티브 실행파일 빌드
- 반환: 0=성공, 비0=실패
- ================================================================ */
  static int build_with_clang(const char *ll_path, const char *exec_path) {
  char cmd[1024];
  snprintf(cmd, sizeof(cmd),
  "clang -O2 -o "%s" "%s" 2>&1",
  exec_path, ll_path);
  fprintf(stderr, "[빌드] clang: %s\n", cmd);
  return system(cmd);
  }

/* ================================================================

- .han 파일명에서 확장자를 제거한 기본 이름 반환
- 예) "hello.han" → "hello"
- out_buf 에 결과를 쓰고 out_buf 를 반환한다.
- ================================================================ */
  static char *strip_extension(const char *path, char *out_buf, size_t buf_size) {
  snprintf(out_buf, buf_size, "%s", path);
  char *dot = strrchr(out_buf, '.');
  if (dot) *dot = '\0';
  return out_buf;
  }

/* ================================================================

- main
- ================================================================ */
  int main(int argc, char *argv[]) {
  
  /* 인수 없음 → 내장 테스트 */
  if (argc == 1) {
  run_builtin_test();
  return 0;
  }
  
  /* IDE 서버 모드 */
  if (argc == 2 && strcmp(argv[1], "-ide") == 0) {
  ide_server_mode();
  return 0;
  }
  
  /* 도움말 */
  if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-help") == 0) {
  print_usage(argv[0]);
  return 0;
  }
  
  /* 파일 경로 */
  const char *han_path = argv[1];
  
  /* 옵션 파싱 */
  int         build     = 0;
  int         json_out  = 0;
  int         bitcode   = 0;
  int         unopt_out = 0;
  const char *out_path  = NULL;
  
  for (int i = 2; i < argc; i++) {
  if (strcmp(argv[i], "-b") == 0) {
  build = 1;
  } else if (strcmp(argv[i], "-json") == 0) {
  json_out = 1;
  } else if (strcmp(argv[i], "-bc") == 0) {
  bitcode = 1;
  } else if (strcmp(argv[i], "-unopt") == 0) {
  unopt_out = 1;
  } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
  out_path = argv[++i];
  } else {
  fprintf(stderr, "[경고] 알 수 없는 옵션: %s\n", argv[i]);
  }
  }
  
  /* 소스 읽기 */
  char *src = read_file(han_path);
  if (!src) return 1;
  
  /* 모듈 이름 = 파일명 */
  char module_name[256];
  strip_extension(han_path, module_name, sizeof(module_name));
  /* 경로에서 파일명만 추출 */
  char *slash = strrchr(module_name, '/');
  const char *mod = slash ? slash + 1 : module_name;
  
  /* 컴파일 */
  LLVMCodegenResult *r = compile_source(src, mod);
  free(src);
  
  /* JSON 모드 */
  if (json_out) {
  llvm_codegen_to_json(r, stdout);
  int ret = r->had_error ? 1 : 0;
  llvm_codegen_result_free(r);
  return ret;
  }
  
  /* 오류 출력 */
  if (r->had_error) {
  for (int i = 0; i < r->error_count; i++) {
  fprintf(stderr, "[오류] 줄 %d, 열 %d: %s\n",
  r->errors[i].line,
  r->errors[i].col,
  r->errors[i].msg);
  }
  llvm_codegen_result_free(r);
  return 1;
  }
  
  /* ── 비트코드 저장 모드 ── */
  if (bitcode) {
  char bc_path[256];
  if (out_path) {
  snprintf(bc_path, sizeof(bc_path), "%s", out_path);
  } else {
  strip_extension(han_path, bc_path, sizeof(bc_path));
  strncat(bc_path, ".bc", sizeof(bc_path) - strlen(bc_path) - 1);
  }
  /* llvm_codegen_to_bitcode 는 내부적으로 LLVMModuleRef 를 사용.
  * 여기서는 IR 텍스트를 임시 .ll 파일로 저장한 뒤 llvm-as 를 호출한다. */
  char tmp_ll[256];
  snprintf(tmp_ll, sizeof(tmp_ll), "/tmp/*kcode_llvm*%d.ll", (int)getpid());
  if (llvm_codegen_to_file(r, tmp_ll) != 0) {
  fprintf(stderr, "[오류] IR 파일 저장 실패: %s\n", tmp_ll);
  llvm_codegen_result_free(r);
  return 1;
  }
  char cmd[512];
  snprintf(cmd, sizeof(cmd),
  "llvm-as "%s" -o "%s" 2>&1", tmp_ll, bc_path);
  int ret = system(cmd);
  remove(tmp_ll);
  if (ret != 0) {
  fprintf(stderr, "[오류] llvm-as 실패 (코드: %d)\n", ret);
  llvm_codegen_result_free(r);
  return 1;
  }
  fprintf(stderr, "[완료] 비트코드 저장: %s\n", bc_path);
  llvm_codegen_result_free(r);
  return 0;
  }
  
  /* ── 빌드 모드 (.ll → clang → 실행파일) ── */
  if (build) {
  char tmp_ll[256];
  snprintf(tmp_ll, sizeof(tmp_ll), "/tmp/*kcode_llvm*%d.ll", (int)getpid());

   if (llvm_codegen_to_file(r, tmp_ll) != 0) {
       fprintf(stderr, "[오류] IR 파일 저장 실패: %s\n", tmp_ll);
       llvm_codegen_result_free(r);
       return 1;
   }
  
   /* 출력 경로 결정 */
   char exec_path[256];
   if (out_path) {
       snprintf(exec_path, sizeof(exec_path), "%s", out_path);
   } else {
       strip_extension(han_path, exec_path, sizeof(exec_path));
   }
  
   int ret = build_with_clang(tmp_ll, exec_path);
   remove(tmp_ll);
  
   if (ret != 0) {
       fprintf(stderr, "[오류] clang 빌드 실패 (코드: %d)\n", ret);
       llvm_codegen_result_free(r);
       return 1;
   }
   fprintf(stderr, "[완료] 실행파일 생성: %s\n", exec_path);
   llvm_codegen_result_free(r);
   return 0;

  }
  
  /* ── IR 파일 저장 모드 ── */
  if (out_path) {
  /* -unopt 이면 최적화 전 IR 저장 */
  const char *ir = (unopt_out && r->ir_text_unopt)
  ? r->ir_text_unopt : r->ir_text;
  FILE *f = fopen(out_path, "w");
  if (!f) {
  fprintf(stderr, "[오류] 파일 쓰기 실패: %s\n", out_path);
  llvm_codegen_result_free(r);
  return 1;
  }
  fputs(ir, f);
  fclose(f);
  fprintf(stderr, "[완료] IR 저장: %s (%d 라인)\n",
  out_path, r->ir_line_count);
  llvm_codegen_result_free(r);
  return 0;
  }
  
  /* ── 기본: IR stdout 출력 ── */
  if (unopt_out && r->ir_text_unopt) {
  fputs(r->ir_text_unopt, stdout);
  } else if (r->ir_text) {
  fputs(r->ir_text, stdout);
  }
  
  llvm_codegen_result_free(r);
  return 0;
  }

#else /* KCODE_LLVM 미정의 시 */

#include <stdio.h>
int main(void) {
fprintf(stderr,
"[오류] LLVM 빌드가 필요합니다.\n"
"  make LLVM=1  또는\n"
"  gcc -DKCODE_LLVM $(llvm-config -cflags) … 으로 빌드하세요.\n");
return 1;
}

#endif /* KCODE_LLVM */