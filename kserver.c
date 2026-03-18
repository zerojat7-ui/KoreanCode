#define _POSIX_C_SOURCE 200809L
/*

- kserver.c  -  Kcode 로컬 HTTP 서버
- version : v22.3.0  (메인 버전 상속)
- 
- 웹 IDE(GitHub Pages)와 로컬 컴파일러(kcode_llvm / kcode_gen)를
- 연결하는 경량 HTTP 서버. 외부 라이브러리 없이 POSIX 소켓만 사용.
- 
- 실행:
- ./kserver                        기본 포트 8080
- ./kserver 3000                   포트 지정
- ./kserver 8080 -llvm             LLVM 컴파일러 사용 (기본)
- ./kserver 8080 -cgen             C 코드 생성기 사용
- ./kserver --with-db              kdbserver 프로세스 생성
- ./kserver --with-ont             kont_server 프로세스 생성
- ./kserver --with-db-ont          kdbont_server 프로세스 생성
- ./kserver --with-all             세 서버 모두 생성
- ./kserver --with-all --detach    하위 서버를 데몬으로 분리
- 
- 엔드포인트:
- POST /compile              소스 컴파일 → JSON 응답
- GET  /health               서버 상태 확인
- GET  /version              컴파일러 버전 반환
- OPTIONS *                  CORS preflight 처리
- 
- 요청 형식 (POST /compile):
- Content-Type: application/json
- {"action":"compile", "source":"…한글 Kcode 소스…"}
- 
- 응답 형식:
- {"success":true, "ir_text":"…", "sourcemap":[…],
- "errors":[], "stats":{…}}
- 
- CORS:
- GitHub Pages 등 외부 도메인에서 호출 가능하도록
- Access-Control-Allow-Origin: * 헤더를 모든 응답에 포함.
- 
- 컴파일러 연동:
- kcode_llvm -ide 또는 kcode_gen -ide 를 자식 프로세스로 실행.
- stdin → JSON 요청 전달, stdout ← JSON 응답 수신.
- 
- 빌드:
- gcc -O2 -o kserver kserver.c
- (POSIX 소켓 / POSIX 파이프 사용 - 추가 라이브러리 불필요)
- 
- MIT License
- zerojat7
  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

/* ── 지식 뱅크 REST API (Stage 23) ── */
#include "kc_kbank.h"
#include "kc_kbank_proof.h"
#include "kc_kbank_merge.h"
#include <netinet/in.h>
#include <arpa/inet.h>

#include "kc_server_spawn.h"

/* ================================================================

- 설정 상수
- ================================================================ */
  #define SERVER_VERSION      "22.3.0"
  #define DEFAULT_PORT        8080
  #define BACKLOG             16          /* 동시 대기 연결 수       */
  #define RECV_BUF_SIZE       (256*1024)  /* 요청 수신 버퍼 256KB    */
  #define SEND_BUF_SIZE       (4*1024*1024) /* 응답 송신 버퍼 4MB    */
  #define COMPILER_LLVM       "kcode_llvm"
  #define COMPILER_CGEN       "kcode_gen"
  #define COMPILER_ARGS       " -ide"    /* IDE 서버 모드 인수      */

/* ================================================================

- 전역 상태
- ================================================================ */
  static int   g_running     = 1;
  static int   g_port        = DEFAULT_PORT;
  static int   g_use_llvm    = 1;        /* 1=LLVM, 0=C코드생성기   */
  static char  g_compiler[256];          /* 실제 사용할 컴파일러 경로 */

/* 하위 서버 옵션 */
  static int          g_with_db     = 0;  /* --with-db     */
  static int          g_with_ont    = 0;  /* --with-ont    */
  static int          g_with_dbont  = 0;  /* --with-db-ont */
  static int          g_detach      = 0;  /* --detach      */
  static KcSpawnCtx   g_spawn_ctx;        /* 하위 서버 컨텍스트 */

/* ================================================================

- 시그널 핸들러 - Ctrl+C 로 서버 종료
- ================================================================ */
  static void handle_sigint(int sig) {
  (void)sig;
  g_running = 0;
  fprintf(stderr, "\n[Kcode 서버] 종료 중…\n");
  /* 하위 서버 종료 (detach 아닌 것만) */
  kc_spawn_stop_all(&g_spawn_ctx);
  }

/* ================================================================

- 헬퍼: 문자열 앞뒤 공백 제거 (in-place)
- ================================================================ */
  static void trim(char *s) {
  /* 앞 공백 */
  char *p = s;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if (p != s) memmove(s, p, strlen(p) + 1);
  /* 뒤 공백 */
  int len = (int)strlen(s);
  while (len > 0 &&
  (s[len-1] == ' ' || s[len-1] == '\t' ||
  s[len-1] == '\r' || s[len-1] == '\n')) {
  s[--len] = '\0';
  }
  }

/* ================================================================

- 헬퍼: 대소문자 무관 문자열 검색
- ================================================================ */
  static const char *stristr(const char *hay, const char *needle) {
  if (!*needle) return hay;
  for (; *hay; hay++) {
  const char *h = hay, *n = needle;
  while (*h && *n &&
  (*h | 32) == (*n | 32)) { h++; n++; }
  if (!*n) return hay;
  }
  return NULL;
  }

/* ================================================================

- HTTP 요청 파싱 결과
- ================================================================ */
  typedef struct {
  char method[16];        /* GET / POST / OPTIONS        */
  char path[256];         /* /compile / /health 등       */
  char content_type[128]; /* Content-Type 헤더 값        */
  int  content_length;    /* Content-Length 값           */
  char *body;             /* 요청 본문 (포인터, 해제 불필요) */
  int  body_len;
  } HttpRequest;

/* ================================================================

- HTTP 요청 파싱
- buf : 수신된 전체 데이터
- len : 버퍼 길이
- req : 결과를 채울 구조체
- 반환: 0=성공, -1=파싱 실패
- ================================================================ */
  static int parse_request(char *buf, int len, HttpRequest *req) {
  memset(req, 0, sizeof(HttpRequest));
  
  /* 요청 라인: METHOD PATH HTTP/x.x\r\n */
  char *line_end = strstr(buf, "\r\n");
  if (!line_end) return -1;
  *line_end = '\0';
  
  char *tok = strtok(buf, " ");
  if (!tok) return -1;
  strncpy(req->method, tok, sizeof(req->method) - 1);
  
  tok = strtok(NULL, " ");
  if (!tok) return -1;
  strncpy(req->path, tok, sizeof(req->path) - 1);
  
  /* 헤더 파싱 */
  char *p = line_end + 2;  /* \r\n 다음 */
  while (p < buf + len) {
  char *hdr_end = strstr(p, "\r\n");
  if (!hdr_end) break;
  if (hdr_end == p) { p += 2; break; } /* 빈 줄 = 헤더 끝 */
  *hdr_end = '\0';

   char *colon = strchr(p, ':');
   if (colon) {
       *colon = '\0';
       char *val = colon + 1;
       while (*val == ' ') val++;
  
       if (stristr(p, "content-type"))
           strncpy(req->content_type, val,
                   sizeof(req->content_type) - 1);
       else if (stristr(p, "content-length"))
           req->content_length = atoi(val);
   }
   p = hdr_end + 2;

  }
  
  /* 본문 */
  req->body     = p;
  req->body_len = (int)(buf + len - p);
  if (req->body_len < 0) req->body_len = 0;
  
  return 0;
  }

/* ================================================================

- CORS + 공통 응답 헤더 전송
- ================================================================ */
  static void send_headers(int fd, int status,
  const char *status_text,
  const char *content_type,
  int body_len) {
  char hdr[512];
  snprintf(hdr, sizeof(hdr),
  "HTTP/1.1 %d %s\r\n"
  "Content-Type: %s; charset=utf-8\r\n"
  "Content-Length: %d\r\n"
  "Access-Control-Allow-Origin: *\r\n"
  "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
  "Access-Control-Allow-Headers: Content-Type\r\n"
  "Connection: close\r\n"
  "\r\n",
  status, status_text, content_type, body_len);
  write(fd, hdr, strlen(hdr));
  }

/* ================================================================

- JSON 응답 전송 헬퍼
- ================================================================ */
  static void send_json(int fd, int status,
  const char *status_text,
  const char *json) {
  int len = (int)strlen(json);
  send_headers(fd, status, status_text, "application/json", len);
  write(fd, json, len);
  }

/* ================================================================

- 컴파일러 실행 - popen으로 kcode_llvm -ide 호출
- 
- json_in  : 컴파일러 stdin에 보낼 JSON 문자열
- 반환     : 컴파일러 stdout 결과 (힙 할당, 호출자가 free)
- ```
          실패 시 NULL

- ================================================================ */
  static char *run_compiler(const char *json_in) {
  /* 임시 파일에 JSON 입력 저장 */
  char tmp_in[64];
  snprintf(tmp_in, sizeof(tmp_in),
  "/tmp/*kcode_srv_in*%d.json", (int)getpid());
  
  FILE *fin = fopen(tmp_in, "w");
  if (!fin) return NULL;
  fputs(json_in, fin);
  fclose(fin);
  
  /* 명령 구성: kcode_llvm -ide < tmp_in */
  char cmd[512];
  snprintf(cmd, sizeof(cmd),
  "%s%s < \"%s\" 2>/dev/null",
  g_compiler, COMPILER_ARGS, tmp_in);
  
  /* 실행 및 출력 읽기 */
  FILE *fp = popen(cmd, "r");
  remove(tmp_in);
  if (!fp) return NULL;
  
  size_t cap = 256 * 1024;
  size_t len = 0;
  char  *buf = (char*)malloc(cap);
  if (!buf) { pclose(fp); return NULL; }
  
  int c;
  while ((c = fgetc(fp)) != EOF) {
  if (len + 1 >= cap) {
  cap *= 2;
  char *nbuf = (char*)realloc(buf, cap);
  if (!nbuf) { free(buf); pclose(fp); return NULL; }
  buf = nbuf;
  }
  buf[len++] = (char)c;
  }
  buf[len] = '\0';
  pclose(fp);
  
  return buf;
  }

/* ================================================================

- 핸들러: OPTIONS (CORS preflight)
- ================================================================ */
  static void handle_options(int fd) {
  send_headers(fd, 204, "No Content", "text/plain", 0);
  }

/* ================================================================

- 핸들러: GET /health
- ================================================================ */
  static void handle_health(int fd) {
  char json[256];
  snprintf(json, sizeof(json),
  "{\"status\":\"ok\","
  "\"server\":\"Kcode Server\","
  "\"version\":\"%s\","
  "\"compiler\":\"%s\","
  "\"port\":%d}",
  SERVER_VERSION,
  g_use_llvm ? "llvm" : "cgen",
  g_port);
  send_json(fd, 200, "OK", json);
  }

/* ================================================================

- 핸들러: GET /version
- ================================================================ */
  static void handle_version(int fd) {
  char json[128];
  snprintf(json, sizeof(json),
  "{\"version\":\"%s\","
  "\"compiler\":\"%s\"}",
  SERVER_VERSION,
  g_use_llvm ? COMPILER_LLVM : COMPILER_CGEN);
  send_json(fd, 200, "OK", json);
  }

/* ================================================================

- 핸들러: POST /compile
- ================================================================ */
  static void handle_compile(int fd, HttpRequest *req) {
  /* 본문 없음 */
  if (!req->body || req->body_len == 0) {
  send_json(fd, 400, "Bad Request",
  "{\"success\":false,\"errors\":[{\"line\":0,\"col\":0,""\"msg\":\"요청 본문이 없습니다\"}]}");
  return;
  }
  
  /* 본문을 null 종료 문자열로 만들기 */
  char *body = (char*)malloc(req->body_len + 1);
  if (!body) {
  send_json(fd, 500, "Internal Server Error",
  "{\"success\":false,\"errors\":[{\"line\":0,\"col\":0,""\"msg\":\"메모리 부족\"}]}");
  return;
  }
  memcpy(body, req->body, req->body_len);
  body[req->body_len] = '\0';
  
  /* action 필드 확인 */
  if (!strstr(body, "\"compile\"")) {
  free(body);
  send_json(fd, 400, "Bad Request",
  "{\"success\":false,\"errors\":[{\"line\":0,\"col\":0,""\"msg\":\"action 필드가 없거나 compile 이 아닙니다\"}]}");
  return;
  }
  
  /* 컴파일러 실행 */
  char *result = run_compiler(body);
  free(body);
  
  if (!result) {
  send_json(fd, 500, "Internal Server Error",
  "{\"success\":false,\"errors\":[{\"line\":0,\"col\":0,""\"msg\":\"컴파일러 실행 실패 - kcode_llvm 이 설치되어 있는지 확인하세요\"}]}");
  return;
  }
  
  /* 컴파일러 출력을 그대로 응답 */
  send_json(fd, 200, "OK", result);
  free(result);
  }

/* ================================================================
 * 지식 뱅크 REST API — Stage 23
 *
 * 엔드포인트 목록:
 *   GET  /kbank/stats          — 뱅크 통계 JSON
 *   GET  /kbank/meta           — 뱅크 메타 전체 JSON
 *   GET  /kbank/audit          — 감사 로그 JSON
 *   GET  /kbank/lineage        — 계보/조상 체인 JSON
 *   GET  /kbank/models         — 등록 모델 목록 JSON
 *   POST /kbank/save           — 뱅크 저장  {"path":".kbank", "actor":"id"}
 *   POST /kbank/load           — 뱅크 로드  {"path":".kbank", "actor":"id"}
 *   POST /kbank/proof          — 소유 증거 출력  {"base_path":"...", "actor":"id"}
 *   POST /kbank/rollback       — 롤백 실행  {"steps":1}
 *   GET  /kbank/integrity      — SHA-256 서명 검증 결과
 * ================================================================ */

/* 전방 선언 */
static void handle_method_not_allowed(int fd);

/* 뱅크 인스턴스: 프로세스 전역 단일 인스턴스 (g_kbank)
 * ================================================================ */

/* 전역 뱅크 컨텍스트 — 서버 기동 시 초기화 */
static KcKbank g_kbank;
static int     g_kbank_ready = 0;  /* 0 = 미초기화, 1 = 사용 가능 */
static char    g_kbank_path[KC_KBANK_PATH_LEN] = "";  /* 현재 로드된 파일 경로 */

/* JSON 에서 문자열 값 추출 (간단한 "key":"value" 파싱) */
static int json_str_val(const char *json, const char *key,
                         char *out, size_t out_sz)
{
    if (!json || !key || !out) return 0;
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == ':' || *p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_sz)
        out[i++] = *p++;
    out[i] = '\0';
    return (int)i;
}

/* 뱅크 미초기화 시 오류 응답 */
static void kbank_not_ready(int fd) {
    send_json(fd, 503, "Service Unavailable",
              "{\"error\":\"지식 뱅크 미초기화 — POST /kbank/load 로 먼저 로드하세요\"}");
}

/* ── GET /kbank/stats ── */
static void handle_kbank_stats(int fd) {
    if (!g_kbank_ready) { kbank_not_ready(fd); return; }
    char buf[2048];
    kc_kbank_stats_json(&g_kbank, buf, sizeof(buf));
    send_json(fd, 200, "OK", buf);
}

/* ── GET /kbank/meta ── */
static void handle_kbank_meta(int fd) {
    if (!g_kbank_ready) { kbank_not_ready(fd); return; }
    char *buf = (char *)malloc(16384);
    if (!buf) { send_json(fd, 500, "Internal Server Error",
                          "{\"error\":\"메모리 부족\"}"); return; }
    kc_kbank_to_json(&g_kbank, buf, 16384);
    send_json(fd, 200, "OK", buf);
    free(buf);
}

/* ── GET /kbank/audit ── */
static void handle_kbank_audit(int fd) {
    if (!g_kbank_ready) { kbank_not_ready(fd); return; }
    char *buf = (char *)malloc(8192);
    if (!buf) { send_json(fd, 500, "Internal Server Error",
                          "{\"error\":\"메모리 부족\"}"); return; }
    int n = 0;
    n += snprintf(buf + n, 8192 - n, "{\"audit_count\":%d,\"entries\":[",
                  g_kbank.audit_count);
    for (int i = 0; i < g_kbank.audit_count && n < 8000; i++) {
        const KcKbankAuditEntry *e = &g_kbank.audit_log[i];
        char ts[32]; struct tm *tm = localtime(&e->ts);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
        n += snprintf(buf + n, 8192 - n,
                      "%s{\"ts\":\"%s\",\"actor\":\"%s\","
                      "\"action\":\"%s\",\"detail\":\"%s\"}",
                      i > 0 ? "," : "", ts,
                      e->actor_id, e->action, e->detail);
    }
    n += snprintf(buf + n, 8192 - n, "]}");
    send_json(fd, 200, "OK", buf);
    free(buf);
}

/* ── GET /kbank/lineage ── */
static void handle_kbank_lineage(int fd) {
    if (!g_kbank_ready) { kbank_not_ready(fd); return; }
    char buf[4096]; int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n,
                  "{\"lineage_id\":\"%s\",\"parent\":\"%s\","
                  "\"license\":\"%s\",\"ancestor_count\":%d,\"ancestors\":[",
                  g_kbank.label.lineage_id,
                  g_kbank.label.parent_lineage_id,
                  g_kbank.label.license,
                  g_kbank.label.ancestor_count);
    for (int i = 0; i < g_kbank.label.ancestor_count; i++)
        n += snprintf(buf + n, sizeof(buf) - n, "%s\"%s\"",
                      i > 0 ? "," : "",
                      g_kbank.label.ancestors[i]);
    snprintf(buf + n, sizeof(buf) - n, "]}");
    send_json(fd, 200, "OK", buf);
}

/* ── GET /kbank/models ── */
static void handle_kbank_models(int fd) {
    if (!g_kbank_ready) { kbank_not_ready(fd); return; }
    char buf[4096]; int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n,
                  "{\"model_count\":%d,\"models\":[",
                  g_kbank.assets.model_count);
    for (int i = 0; i < g_kbank.assets.model_count; i++) {
        const KcKbankModelMeta *m = &g_kbank.assets.models[i];
        char ts[32]; struct tm *tm = localtime(&m->saved_at);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
        n += snprintf(buf + n, sizeof(buf) - n,
                      "%s{\"tag\":\"%s\",\"path\":\"%s\","
                      "\"sig\":\"%s\",\"saved_at\":\"%s\"}",
                      i > 0 ? "," : "",
                      m->tag, m->path, m->sig, ts);
    }
    snprintf(buf + n, sizeof(buf) - n, "]}");
    send_json(fd, 200, "OK", buf);
}

/* ── GET /kbank/integrity ── */
static void handle_kbank_integrity(int fd) {
    if (!g_kbank_ready) { kbank_not_ready(fd); return; }
    if (g_kbank_path[0] == '\0') {
        send_json(fd, 400, "Bad Request",
                  "{\"valid\":false,\"error\":\"파일 경로 없음\"}");
        return;
    }
    KcKbankErr ve = kc_kbank_verify_sig(g_kbank_path, g_kbank.root_sig);
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"valid\":%s,\"path\":\"%s\",\"sig\":\"%.16s...\"}",
             ve == KC_KBANK_OK ? "true" : "false",
             g_kbank_path, g_kbank.root_sig);
    send_json(fd, 200, "OK", buf);
}

/* ── POST /kbank/save ── */
static void handle_kbank_save(int fd, HttpRequest *req) {
    if (!g_kbank_ready) { kbank_not_ready(fd); return; }
    char path[KC_KBANK_PATH_LEN] = "";
    char actor[KC_KBANK_ID_LEN]  = "server";
    json_str_val(req->body, "path",  path,  sizeof(path));
    json_str_val(req->body, "actor", actor, sizeof(actor));
    if (path[0] == '\0') snprintf(path, sizeof(path), "%s", g_kbank_path);
    KcKbankErr e = kc_kbank_save(&g_kbank, path, actor);
    if (e == KC_KBANK_OK) {
        snprintf(g_kbank_path, sizeof(g_kbank_path), "%s", path);
        char ok[256];
        snprintf(ok, sizeof(ok), "{\"result\":\"저장 완료\",\"path\":\"%s\"}", path);
        send_json(fd, 200, "OK", ok);
    } else {
        char err[256];
        snprintf(err, sizeof(err), "{\"error\":\"%s\"}", kc_kbank_err_str(e));
        send_json(fd, 500, "Internal Server Error", err);
    }
}

/* ── POST /kbank/load ── */
static void handle_kbank_load(int fd, HttpRequest *req) {
    char path[KC_KBANK_PATH_LEN] = "";
    char actor[KC_KBANK_ID_LEN]  = "server";
    json_str_val(req->body, "path",  path,  sizeof(path));
    json_str_val(req->body, "actor", actor, sizeof(actor));
    if (path[0] == '\0') {
        send_json(fd, 400, "Bad Request",
                  "{\"error\":\"path 필드 필수\"}");
        return;
    }
    /* kc_kbank_load: 새 KcKbank* 반환, 실패 시 NULL */
    KcKbank *kb = kc_kbank_load(path, actor);
    if (kb) {
        g_kbank       = *kb;
        free(kb);
        g_kbank_ready = 1;
        snprintf(g_kbank_path, sizeof(g_kbank_path), "%s", path);
        char ok[256];
        snprintf(ok, sizeof(ok),
                 "{\"result\":\"로드 완료\",\"bank\":\"%s\",\"path\":\"%s\"}",
                 g_kbank.name, path);
        send_json(fd, 200, "OK", ok);
    } else {
        send_json(fd, 500, "Internal Server Error",
                  "{\"error\":\"뱅크 로드 실패 — 파일 없음 또는 서명 불일치\"}");
    }
}

/* ── POST /kbank/proof ── */
static void handle_kbank_proof(int fd, HttpRequest *req) {
    if (!g_kbank_ready) { kbank_not_ready(fd); return; }
    char base_path[KC_KBANK_PATH_LEN] = "/tmp/kbank_proof";
    char actor[KC_KBANK_ID_LEN]       = "server";
    json_str_val(req->body, "base_path", base_path, sizeof(base_path));
    json_str_val(req->body, "actor",     actor,     sizeof(actor));
    KcKbankErr e = kc_proof_export(&g_kbank, actor,
                                    g_kbank_path[0] ? g_kbank_path : NULL,
                                    base_path);
    if (e == KC_KBANK_OK) {
        char ok[512];
        snprintf(ok, sizeof(ok),
                 "{\"result\":\"증거 출력 완료\","
                 "\"json\":\"%s.proof.json\","
                 "\"txt\":\"%s.proof.txt\"}",
                 base_path, base_path);
        send_json(fd, 200, "OK", ok);
    } else {
        char err[256];
        snprintf(err, sizeof(err), "{\"error\":\"%s\"}", kc_kbank_err_str(e));
        send_json(fd, 500, "Internal Server Error", err);
    }
}

/* ── POST /kbank/rollback ── */
static void handle_kbank_rollback(int fd, HttpRequest *req) {
    if (!g_kbank_ready) { kbank_not_ready(fd); return; }
    char steps_str[16] = "1";
    json_str_val(req->body, "steps", steps_str, sizeof(steps_str));
    int steps = atoi(steps_str);
    if (steps <= 0) steps = 1;
    KcKbankErr e = kc_kbank_rollback(&g_kbank, steps);
    if (e == KC_KBANK_OK) {
        char ok[128];
        snprintf(ok, sizeof(ok), "{\"result\":\"롤백 완료\",\"steps\":%d}", steps);
        send_json(fd, 200, "OK", ok);
    } else {
        char err[256];
        snprintf(err, sizeof(err), "{\"error\":\"%s\"}", kc_kbank_err_str(e));
        send_json(fd, 400, "Bad Request", err);
    }
}

/* ── kbank 라우팅 통합 ── */
static int handle_kbank_route(int fd, HttpRequest *req) {
    const char *p = req->path;
    /* /kbank/* 이 아니면 처리하지 않음 */
    if (strncmp(p, "/kbank/", 7) != 0 && strcmp(p, "/kbank") != 0)
        return 0;

    const char *sub = p + 7; /* "/kbank/" 이후 */

    if (strcmp(req->method, "GET") == 0) {
        if (strcmp(sub, "stats")     == 0) { handle_kbank_stats(fd);     return 1; }
        if (strcmp(sub, "meta")      == 0) { handle_kbank_meta(fd);      return 1; }
        if (strcmp(sub, "audit")     == 0) { handle_kbank_audit(fd);     return 1; }
        if (strcmp(sub, "lineage")   == 0) { handle_kbank_lineage(fd);   return 1; }
        if (strcmp(sub, "models")    == 0) { handle_kbank_models(fd);    return 1; }
        if (strcmp(sub, "integrity") == 0) { handle_kbank_integrity(fd); return 1; }
        send_json(fd, 404, "Not Found",
                  "{\"error\":\"알 수 없는 kbank 엔드포인트\","
                  "\"available\":["
                  "\"/kbank/stats\",\"/kbank/meta\",\"/kbank/audit\","
                  "\"/kbank/lineage\",\"/kbank/models\",\"/kbank/integrity\"]}");
        return 1;
    }
    if (strcmp(req->method, "POST") == 0) {
        if (strcmp(sub, "save")     == 0) { handle_kbank_save(fd, req);     return 1; }
        if (strcmp(sub, "load")     == 0) { handle_kbank_load(fd, req);     return 1; }
        if (strcmp(sub, "proof")    == 0) { handle_kbank_proof(fd, req);    return 1; }
        if (strcmp(sub, "rollback") == 0) { handle_kbank_rollback(fd, req); return 1; }
        send_json(fd, 404, "Not Found",
                  "{\"error\":\"알 수 없는 kbank POST 엔드포인트\","
                  "\"available\":["
                  "\"/kbank/save\",\"/kbank/load\","
                  "\"/kbank/proof\",\"/kbank/rollback\"]}");
        return 1;
    }
    handle_method_not_allowed(fd);
    return 1;
}

/* ================================================================
 * 404 핸들러
- ================================================================ */
  static void handle_not_found(int fd) {
  send_json(fd, 404, "Not Found",
  "{\"error\":\"Not Found\",""\"available\":[\"/compile\",\"/health\",\"/version\"]}");
  }

/* ================================================================

- 405 핸들러
- ================================================================ */
  static void handle_method_not_allowed(int fd) {
  send_json(fd, 405, "Method Not Allowed",
  "{\"error\":\"Method Not Allowed\"}");
  }

/* ================================================================

- 클라이언트 연결 처리
- ================================================================ */
  static void handle_client(int client_fd) {
  char *buf = (char*)malloc(RECV_BUF_SIZE);
  if (!buf) { close(client_fd); return; }
  
  /* 데이터 수신 */
  int total = 0;
  int n;
  while (total < RECV_BUF_SIZE - 1) {
  n = (int)recv(client_fd, buf + total,
  RECV_BUF_SIZE - 1 - total, 0);
  if (n <= 0) break;
  total += n;

   /* 헤더 끝(\r\n\r\n) 확인 후 Content-Length 만큼 더 읽기 */
   buf[total] = '\0';
   char *hdr_end = strstr(buf, "\r\n\r\n");
   if (hdr_end) {
       /* Content-Length 파싱 */
       const char *cl_hdr = stristr(buf, "content-length:");
       if (cl_hdr) {
           int cl = atoi(cl_hdr + 15);
           int body_received = total - (int)(hdr_end + 4 - buf);
           if (body_received >= cl) break; /* 본문 다 받음 */
       } else {
           break; /* Content-Length 없으면 헤더만 있음 */
       }
   }

  }
  buf[total] = '\0';
  
  if (total == 0) {
  free(buf);
  close(client_fd);
  return;
  }
  
  /* 요청 파싱 */
  HttpRequest req;
  if (parse_request(buf, total, &req) != 0) {
  send_json(client_fd, 400, "Bad Request",
  "{\"error\":\"잘못된 HTTP 요청\"}");
  free(buf);
  close(client_fd);
  return;
  }
  
  /* 로그 출력 */
  fprintf(stderr, "[요청] %s %s\n", req.method, req.path);
  
  /* 라우팅 */
  if (strcmp(req.method, "OPTIONS") == 0) {
  handle_options(client_fd);
  } else if (strcmp(req.path, "/health") == 0) {
  if (strcmp(req.method, "GET") == 0)
  handle_health(client_fd);
  else
  handle_method_not_allowed(client_fd);
  } else if (strcmp(req.path, "/version") == 0) {
  if (strcmp(req.method, "GET") == 0)
  handle_version(client_fd);
  else
  handle_method_not_allowed(client_fd);
  } else if (strcmp(req.path, "/compile") == 0) {
  if (strcmp(req.method, "POST") == 0)
  handle_compile(client_fd, &req);
  else
  handle_method_not_allowed(client_fd);
  } else if (handle_kbank_route(client_fd, &req)) {
  /* /kbank/* 처리 완료 */
  } else {
  handle_not_found(client_fd);
  }
  
  free(buf);
  close(client_fd);
  }

/* ================================================================

- 사용법 출력
- ================================================================ */
  static void print_usage(const char *prog) {
  fprintf(stderr,
  "사용법:\n"
  "  %s                        기본 포트(%d)로 시작\n"
  "  %s <포트>                  포트 지정\n"
  "  %s <포트> -llvm            LLVM 컴파일러 사용 (기본)\n"
  "  %s <포트> -cgen            C 코드 생성기 사용\n"
  "\n"
  "하위 서버 옵션 (독립 프로세스 생성):\n"
  "  %s --with-db               kdbserver    (일반 DB, 포트 5432)\n"
  "  %s --with-ont              kont_server  (온톨로지 DB, 포트 8765)\n"
  "  %s --with-db-ont           kdbont_server(통합 DB, 포트 9000)\n"
  "  %s --with-all              세 서버 모두 생성\n"
  "  %s --with-all --detach     하위 서버를 데몬으로 분리\n"
  "\n"
  "엔드포인트:\n"
  "  POST /compile              소스 컴파일\n"
  "  GET  /health               서버 상태 (하위 서버 정보 포함)\n"
  "  GET  /version              버전 정보\n"
  "\n"
  "예시:\n"
  "  %s 8080 --with-all\n"
  "  curl http://localhost:8080/health\n",
  prog, DEFAULT_PORT,
  prog, prog, prog,
  prog, prog, prog, prog, prog,
  prog);
  }

/* ================================================================

- main
- ================================================================ */
  int main(int argc, char *argv[]) {
  
  /* 인수 파싱 */
  for (int i = 1; i < argc; i++) {
  if (strcmp(argv[i], "-llvm") == 0) {
  g_use_llvm = 1;
  } else if (strcmp(argv[i], "-cgen") == 0) {
  g_use_llvm = 0;
  } else if (strcmp(argv[i], "--with-db") == 0) {
  g_with_db = 1;
  } else if (strcmp(argv[i], "--with-ont") == 0) {
  g_with_ont = 1;
  } else if (strcmp(argv[i], "--with-db-ont") == 0) {
  g_with_dbont = 1;
  } else if (strcmp(argv[i], "--with-all") == 0) {
  g_with_db = g_with_ont = g_with_dbont = 1;
  } else if (strcmp(argv[i], "--detach") == 0) {
  g_detach = 1;
  } else if (strcmp(argv[i], "-h") == 0 ||
  strcmp(argv[i], "-help") == 0 ||
  strcmp(argv[i], "--help") == 0) {
  print_usage(argv[0]);
  return 0;
  } else {
  int p = atoi(argv[i]);
  if (p > 0 && p < 65536)
  g_port = p;
  else {
  fprintf(stderr, "[경고] 알 수 없는 인수: %s\n", argv[i]);
  }
  }
  }
  
  /* 컴파일러 경로 결정 */
  snprintf(g_compiler, sizeof(g_compiler),
  "./%s", g_use_llvm ? COMPILER_LLVM : COMPILER_CGEN);

  /* 하위 서버 프로세스 생성 (--with-* 옵션) */
  kc_spawn_init(&g_spawn_ctx);
  if (g_with_db)
  kc_spawn_server(&g_spawn_ctx, KC_SPAWN_DB,    "./kdbserver",     0, g_detach);
  if (g_with_ont)
  kc_spawn_server(&g_spawn_ctx, KC_SPAWN_ONT,   "./kont_server",   0, g_detach);
  if (g_with_dbont)
  kc_spawn_server(&g_spawn_ctx, KC_SPAWN_DBONT, "./kdbont_server", 0, g_detach);
  if (g_with_db || g_with_ont || g_with_dbont)
  kc_spawn_print_status(&g_spawn_ctx);

  /* 시그널 등록 */
  signal(SIGINT,  handle_sigint);
  signal(SIGTERM, handle_sigint);
  signal(SIGPIPE, SIG_IGN);   /* 클라이언트 끊김 무시 */
  signal(SIGCHLD, SIG_IGN);   /* 좀비 프로세스 방지  */
  
  /* 서버 소켓 생성 */
  int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (srv_fd < 0) {
  perror("[오류] socket 생성 실패");
  return 1;
  }
  
  /* SO_REUSEADDR - 재시작 시 포트 즉시 재사용 */
  int opt = 1;
  setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  
  /* 바인드 */
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1만 */
  addr.sin_port        = htons((uint16_t)g_port);
  
  if (bind(srv_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
  perror("[오류] bind 실패");
  close(srv_fd);
  return 1;
  }
  
  /* 리슨 */
  if (listen(srv_fd, BACKLOG) < 0) {
  perror("[오류] listen 실패");
  close(srv_fd);
  return 1;
  }
  
  /* 시작 메시지 */
  fprintf(stderr,
  "\n"
  "══════════════════════════════════════════════\n"
  "  Kcode 로컬 서버 v%s\n"
  "══════════════════════════════════════════════\n"
  "  주소    : http://localhost:%d\n"
  "  컴파일러 : %s\n"
  "  종료    : Ctrl+C\n"
  "══════════════════════════════════════════════\n\n",
  SERVER_VERSION, g_port, g_compiler);
  
  /* 메인 루프 */
  while (g_running) {
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  
  
   int client_fd = accept(srv_fd,
                          (struct sockaddr*)&client_addr,
                          &client_len);
   if (client_fd < 0) {
       if (g_running) perror("[경고] accept 실패");
       continue;
   }
  
   /* 클라이언트 IP 로그 */
   char ip[INET_ADDRSTRLEN];
   inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
   fprintf(stderr, "[연결] %s:%d\n", ip,
           ntohs(client_addr.sin_port));
  
   /* 클라이언트 처리 (단일 프로세스, 순차 처리)
    * 컴파일 요청은 보통 짧으므로 fork 없이 처리.
    * 동시 요청이 많으면 fork 방식으로 전환 가능. */
   handle_client(client_fd);

   /* 하위 프로세스 상태 갱신 (좀비 정리) */
   kc_spawn_check(&g_spawn_ctx);

  }
  
  close(srv_fd);
  kc_spawn_destroy(&g_spawn_ctx);
  fprintf(stderr, "[Kcode 서버] 정상 종료\n");
  return 0;
  }