/*
 * kparser.h  —  Kcode 한글 프로그래밍 언어 파서 헤더
 * version : v22.0.0
 *
 * v22.0.0 변경 (Concept Identity 단계 9 — kparser AST 노드 추가):
 *   - NODE_SEMANTIC_INFER  — 의미추론 블록 (Layer 2: 추론 전략 실행)
 *   - NODE_VECTORIZE       — 벡터화 블록  (Layer 3: 온톨로지→텐서 변환)
 *   - NODE_SEM_RECON       — 의미복원 블록(Layer 9: 벡터→의미 역변환)
 *   - NODE_REPRO_LABEL     — 재생산라벨 블록(Layer 6: 계보ID 자동 기록)
 *
 * v14.0.1 변경:
 *   - NODE_MCP_SERVER / NODE_MCP_TOOL / NODE_MCP_RESOURCE / NODE_MCP_PROMPT 추가
 *     (kparser.c v14.0.0 에서 사용하던 미선언 노드 보완)
 *
 * v11.0.0 변경:
 *   - NODE_TENSOR_LIT 추가 — 텐서 리터럴 / 생성 호출
 *     op    = TOK_KW_TENSOR | TOK_KW_ZERO_TENSOR |
 *             TOK_KW_ONE_TENSOR | TOK_KW_RAND_TENSOR
 *     child[0] = 데이터 배열 표현식 (텐서()만 해당, 아니면 없음)
 *     child[last] = 형태 배열 표현식
 *
 * v6.1.0 변경:
 *   - #포함 파일명 파싱 추가 (꺽쇠/따옴표/식별자)
 *   - NODE_GPU_BLOCK sval("GPU"/"NPU"/"CPU") + NODE_GPU_OP 추가 (v8.2.0 동기화)
 *
 * v6.0.0 변경:
 *   - NODE_SIGNAL_HANDLER  추가 — 신호받기 블록
 *   - NODE_SIGNAL_CTRL     추가 — 신호무시/신호기본/신호보내기
 *   - NODE_ISR_HANDLER     추가 — 간섭 블록 (하드웨어 ISR)
 *   - NODE_ISR_CTRL        추가 — 간섭잠금/간섭허용
 *   - NODE_EVENT_HANDLER   추가 — 행사등록 블록
 *   - NODE_EVENT_CTRL      추가 — 행사시작/중단/발생/해제
 *
 * v1.4.0 변경사항:
 *   - NODE_CONSTITUTION (헌법) 추가 — 전역 최상위 계약
 *   - NODE_STATUTE (법률) 추가 — 현재 파일 계약
 *   - NODE_REGULATION (규정) 추가 — 객체 범위 계약 블록
 *   - NODE_CONTRACT 파싱 방식 블록 형태로 변경
 *   - 파일 내장 함수 노드 처리 추가
 *
 * 렉서(Lexer)가 생성한 토큰 스트림을 받아
 * 재귀 하강(Recursive Descent) 방식으로
 * AST(Abstract Syntax Tree)를 생성한다.
 *
 * MIT License
 * zerojat7
 */

#ifndef KCODE_PARSER_H
#define KCODE_PARSER_H

#include "klexer.h"
#include <stdint.h>
#include <stddef.h>

/* ================================================================
 *  AST 노드 종류
 * ================================================================ */
typedef enum {

    /* ── 최상위 ─────────────────────────────────────────── */
    NODE_PROGRAM,       /* 소스 전체                        */

    /* ── 구문(Statement) ─────────────────────────────────── */
    NODE_BLOCK,         /* 들여쓰기 블록 { stmt* }          */
    NODE_VAR_DECL,      /* [자료형] 이름 [= 값]             */
    NODE_CONST_DECL,    /* 고정 [자료형] 이름 = 값          */
    NODE_EXPR_STMT,     /* 표현식 구문                       */

    NODE_IF,            /* 만약 조건: 블록
                           child[0] = 조건
                           child[1] = then 블록
                           child[2] = elif/else (선택)      */
    NODE_ELIF,          /* 아니면 만약 조건: 블록
                           child[0] = 조건
                           child[1] = then 블록
                           child[2] = 다음 elif/else(선택)  */
    NODE_ELSE,          /* 아니면: 블록
                           child[0] = else 블록             */

    NODE_WHILE,         /* 동안 조건: 블록
                           child[0] = 조건
                           child[1] = 블록                  */
    NODE_FOR_RANGE,     /* 반복 var 부터 시작 까지 끝: 블록
                           sval    = 변수 이름
                           child[0] = 시작
                           child[1] = 끝
                           child[2] = 블록                  */
    NODE_FOR_EACH,      /* 각각 var 안에 목록: 블록
                           sval    = 변수 이름
                           child[0] = 목록 표현식
                           child[1] = 블록                  */

    NODE_SWITCH,        /* 선택 값: 케이스*
                           child[0]   = 대상 표현식
                           child[1..] = NODE_CASE / NODE_DEFAULT */
    NODE_CASE,          /* 경우 값: 블록
                           child[0] = 값
                           child[1] = 블록                  */
    NODE_DEFAULT,       /* 그외: 블록
                           child[0] = 블록                  */

    NODE_FUNC_DECL,     /* 함수 이름(매개변수): 블록
                           sval      = 함수 이름
                           child[0..n-1] = 매개변수(NODE_PARAM)
                           child[n]  = 블록                 */
    NODE_VOID_DECL,     /* 정의 이름(매개변수): 블록 (동일) */
    NODE_PARAM,         /* 매개변수
                           sval  = 이름
                           dtype = 자료형 토큰
                           child[0] = 기본값(선택)          */

    NODE_RETURN,        /* 반환 [값]
                           child[0] = 값(선택)              */
    NODE_BREAK,         /* 멈춤                             */
    NODE_CONTINUE,      /* 건너뜀                           */
    NODE_GOTO,          /* 이동 이름   sval = 이름          */
    NODE_LABEL,         /* 이름:       sval = 이름          */

    NODE_TRY,           /* 시도/실패시/항상
                           child[0] = 시도 블록
                           child[1] = 실패시 블록
                           child[2] = 항상 블록(선택)       */

    NODE_IMPORT,        /* 가짐 모듈 [로부터 이름,...]
                           sval = 모듈 이름
                           child[0..] = NODE_IDENT (from 이름들) */

    NODE_CLASS_DECL,    /* 객체 이름 [이어받기 부모]: 블록
                           sval  = 클래스 이름
                           child[0] = NODE_IDENT 부모(선택)
                           child[last] = 블록               */

    NODE_RAISE,         /* 오류(메시지)
                           child[0] = 메시지 표현식         */

    NODE_PP_STMT,       /* 전처리기 구문
                           op   = 전처리기 토큰 종류
                           sval = 이름/값                   */

    NODE_GPU_BLOCK,     /* 가속기 [종류]: ... 가속기끝  (v9.0.0)
                           sval     = "GPU" | "NPU" | "CPU" | NULL(자동)
                           val.ival = 반환 child 인덱스 (-1=없음)
                           child[0..n-1] = NODE_GPU_OP (내장 연산, 선택)
                           child[last]   = NODE_BLOCK (블록 본문)       */

    NODE_GPU_OP,        /* 가속기 내장 연산  (v9.0.0)
                           sval    = "행렬곱"|"행렬합"|"합성곱"|"활성화"|"전치"
                           val.ival= 반환 child 인덱스 (-1=없음)
                           child[0..k-1] = 입력 인수 (NODE_IDENT)
                           child[k]      = 반환 변수 (NODE_IDENT, => 뒤) */

    /* ── 스크립트 블록 ──────────────────────────────────── */
    NODE_SCRIPT_PYTHON, /* 파이썬: ... 파이썬끝
                           sval      = 스크립트 원문 코드
                           child[0..n-1] = 전달 변수 (NODE_IDENT)
                           child[n]  = 반환 변수 (NODE_IDENT, 선택) */
    NODE_SCRIPT_JAVA,   /* 자바: ... 자바끝  (동일 구조) */
    NODE_SCRIPT_JS,     /* 자바스크립트: ... 자바스크립트끝 (동일 구조) */

    /* ── 계약 시스템 ─────────────────────────────────────── */
    NODE_CONTRACT,      /* 법령 / 법위반 선언 (블록 방식 v4.2.0)
                           op      = TOK_KW_BEOPRYEONG (법령)
                                   | TOK_KW_BEOPWIBAN  (법위반)
                           sval    = 대상 함수명
                           child[0]= 조건 표현식 (NODE_SANCTION 참조)
                           child[1]= 제재 노드 (NODE_SANCTION)        */
    NODE_CONSTITUTION,  /* 헌법 선언 — 전역 최상위 계약 (단일 라인)
                           child[0]= 조건 표현식
                           child[1]= 제재 노드 (NODE_SANCTION)        */
    NODE_STATUTE,       /* 법률 선언 — 현재 파일 전체 계약 (단일 라인)
                           child[0]= 조건 표현식
                           child[1]= 제재 노드 (NODE_SANCTION)        */
    NODE_REGULATION,    /* 규정 선언 — 특정 객체 전체 메서드 계약 (블록)
                           sval    = 대상 객체명
                           child[0]= 조건 표현식
                           child[1]= 제재 노드 (NODE_SANCTION)        */

    /* ── 인터럽트 시스템 (v6.0.0) ────────────────────────────── */

    NODE_SIGNAL_HANDLER, /* 신호받기 [신호명]:
                            op      = 신호 TokenType (TOK_KW_SIG_INT 등)
                            sval    = 신호 이름 문자열
                            child[0]= 핸들러 블록                    */

    NODE_SIGNAL_CTRL,    /* 신호무시/신호기본/신호보내기
                            op      = TOK_KW_SINHOMUSI |
                                      TOK_KW_SINHOGIBON |
                                      TOK_KW_SINHOBONEGI
                            sval    = 신호 이름 문자열
                            child[0]= 대상 PID 표현식 (보내기만)     */

    NODE_ISR_HANDLER,    /* 간섭 [벡터명]:
                            op      = 벡터 TokenType (TOK_KW_IRQ_* 등)
                            sval    = 벡터 이름 문자열
                            child[0]= ISR 블록                       */

    NODE_ISR_CTRL,       /* 간섭잠금 / 간섭허용
                            op      = TOK_KW_GANSEOB_JAMGEUM |
                                      TOK_KW_GANSEOB_HEOYONG          */

    NODE_EVENT_HANDLER,  /* 행사등록 [이름] 처리[(매개변수...)]:
                            sval    = 이벤트 이름 문자열
                            child[0..n-2] = NODE_PARAM (선택)
                            child[last]   = 핸들러 블록              */

    NODE_EVENT_CTRL,     /* 행사시작 / 행사중단 / 행사발생 / 행사해제
                            op      = TOK_KW_HAENGSA_START |
                                      TOK_KW_HAENGSA_STOP  |
                                      TOK_KW_HAENGSA_EMIT  |
                                      TOK_KW_HAENGSA_OFF
                            child[0]= 이벤트 이름 표현식 (발생/해제) */
    NODE_SANCTION,      /* 제재 정보
                           op      = 제재 토큰 (TOK_KW_GYEONGGO 등)
                           sval    = 회귀 복원지점명 / 대체 함수명 (해당 시)
                           child[0]= 대체값 표현식 또는 대체함수 IDENT
                                     (대체 제재일 때만, 그 외 없음)   */
    NODE_CHECKPOINT,    /* 복원지점 선언
                           sval    = 지점 이름                        */

    /* ── 표현식(Expression) ──────────────────────────────── */
    NODE_INT_LIT,       /* 정수 리터럴   val.ival           */
    NODE_FLOAT_LIT,     /* 실수 리터럴   val.fval           */
    NODE_STRING_LIT,    /* 문자열 리터럴 sval               */
    NODE_CHAR_LIT,      /* 글자 리터럴   val.ival(코드포인트)*/
    NODE_BOOL_LIT,      /* 참/거짓       val.bval           */
    NODE_NULL_LIT,      /* 없음                             */

    NODE_ARRAY_LIT,     /* [값, 값, ...]  child[0..] = 값  */
    NODE_TENSOR_LIT,    /* 텐서/영텐서/일텐서/무작위텐서 생성  (v11.0.0)
                           op      = TOK_KW_TENSOR      | TOK_KW_ZERO_TENSOR |
                                     TOK_KW_ONE_TENSOR  | TOK_KW_RAND_TENSOR
                           텐서(data, shape):
                             child[0] = 데이터 배열 표현식
                             child[1] = 형태 배열 표현식
                           영텐서/일텐서/무작위텐서(shape):
                             child[0] = 형태 배열 표현식               */
    NODE_DICT_LIT,      /* {키:값, ...}   child[0..] = DICT_ENTRY */
    NODE_DICT_ENTRY,    /* 키: 값
                           child[0] = 키
                           child[1] = 값                    */

    NODE_IDENT,         /* 식별자  sval = 이름             */

    NODE_BINARY,        /* 이항 연산
                           op       = 연산자 토큰
                           child[0] = 왼쪽
                           child[1] = 오른쪽               */
    NODE_UNARY,         /* 단항 연산
                           op       = 연산자 토큰
                           child[0] = 피연산자              */
    NODE_ASSIGN,        /* 대입
                           op       = 대입 연산자 토큰
                           child[0] = 좌변 (이름/멤버/인덱스)
                           child[1] = 우변                  */

    NODE_CALL,          /* 함수 호출
                           child[0]    = 함수 표현식
                           child[1..] = 인수               */
    NODE_INDEX,         /* 인덱스 접근
                           child[0] = 배열
                           child[1] = 인덱스               */
    NODE_MEMBER,        /* 멤버 접근
                           child[0] = 객체
                           sval     = 멤버 이름             */

    NODE_LAMBDA,        /* (매개변수) => 표현식
                           child[0..n-1] = NODE_PARAM
                           child[n]      = 표현식          */

    /* MCP 시스템 (v14.0.1) */
    NODE_MCP_SERVER,    /* MCP서버 블록
                           sval      = 서버 이름
                           child[*]  = NODE_MCP_TOOL / NODE_MCP_RESOURCE / NODE_MCP_PROMPT */
    NODE_MCP_TOOL,      /* MCP도구 핸들러
                           sval      = 도구 이름
                           child[*]  = 블록 내 구문          */
    NODE_MCP_RESOURCE,  /* MCP자원 핸들러
                           sval      = 자원 이름
                           child[*]  = 블록 내 구문          */
    NODE_MCP_PROMPT,    /* MCP프롬프트 핸들러
                           sval      = 프롬프트 이름
                           child[*]  = 블록 내 구문          */

    /* ── 산업/임베디드 (v16.0.0) ──────────────────────── */
    NODE_TIMER_BLOCK,   /* 타이머 블록
                           sval      = 주기 문자열 ("100ms" 등)
                           child[*]  = 블록 내 구문          */
    NODE_ROS2_BLOCK,    /* ROS2 노드 블록
                           sval      = 노드 이름
                           child[*]  = 블록 내 구문          */

    /* ── 안전 규격 (v17.0.0) ──────────────────────────── */
    NODE_WATCHDOG_BLOCK,  /* 워치독 타이머 블록
                             ival      = 타임아웃(ms)
                             sval      = 타임아웃 문자열
                             child[*]  = 블록 내 구문              */
    NODE_FAULT_TOL_BLOCK, /* 결함허용 N중 블록
                             ival      = 중복수(N)
                             child[0..N-1] = 각 복제 블록          */

    /* ── 온디바이스 AI (v18.0.0) ───────────────────────── */
    NODE_AI_MODEL_BLOCK,  /* AI모델 블록
                             sval      = 모델 이름
                             child[*]  = 블록 내 구문
                             val.ival  = 0=ONNX, 1=TinyML          */
    NODE_TINYML_BLOCK,    /* TinyML 경량 모델 블록
                             sval      = 모델 이름
                             child[*]  = 블록 내 구문              */
    NODE_FEDERATED_BLOCK, /* 연합학습 블록
                             child[*]  = 블록 내 구문              */

    /* ── 온톨로지 시스템 (v18.5.0 4회차) ──────────── */
    NODE_ONT_BLOCK,       /* 온톨로지 블록
                             sval      = 모드+URL 조합: "내장"|"대여"|"접속@URL"
                             val.ival  = 0=내장, 1=대여, 2=접속
                             child[*]  = 블록 내 구문              */
    NODE_ONT_CONCEPT,     /* 개념 정의 블록
                             sval      = 개념(클래스) 이름
                             child[0]  = 부모 개념 (NODE_IDENT, 없으면 NULL)
                             child[1..] = NODE_ONT_PROP 목록       */
    NODE_ONT_PROP,        /* 속성 선언
                             sval      = 속성 이름
                             val.ival  = 타입 토큰 (TOK_KW_JEONGSU 등)
                             op        = TOK_KW_ONT_SENSITIVE 등   */
    NODE_ONT_RELATE,      /* 관계 정의
                             sval      = 관계 이름
                             child[0]  = from 개념 (NODE_IDENT)
                             child[1]  = to   개념 (NODE_IDENT)    */
    NODE_ONT_QUERY,       /* 질의 실행
                             sval      = 질의 문자열
                             child[0]  = 결과 변수 (NODE_IDENT, 선택) */
    NODE_ONT_INFER,       /* 추론 실행 블록 (body = child[*])     */

    /* ── Concept Identity / Vector Space (v22.0.0) ──── */
    NODE_SEMANTIC_INFER,  /* 의미추론 블록 (Layer 2 — 추론 전략 실행)
                             sval      = 추론 이름 (선택)
                             child[*]  = 설정 구문 목록            */
    NODE_VECTORIZE,       /* 벡터화 블록  (Layer 3 — 온톨로지→텐서)
                             sval      = 벡터화 이름 (선택)
                             child[*]  = 설정 구문 목록            */
    NODE_SEM_RECON,       /* 의미복원 블록(Layer 9 — 벡터→의미)
                             sval      = 복원 이름 (선택)
                             child[*]  = 설정 구문 목록            */
    NODE_REPRO_LABEL,     /* 재생산라벨 블록(Layer 6 — 계보ID 자동 기록)
                             sval      = 메모 (선택)
                             child[*]  = 설정 구문 목록            */

    /* ── 지식 뱅크 노드 (Stage 24) ── */
    NODE_KBANK,           /* 지식뱅크 블록 선언
                             sval      = 뱅크 이름
                             child[*]  = 속성 구문 목록            */
    NODE_KBANK_LOAD,      /* 지식불러오기 블록
                             sval      = .kbank 파일 경로
                             child[0]  = 선택: 액터 ID 식         */
    NODE_KBANK_COMPARE,   /* 지식비교 블록
                             child[0]  = 비교 대상 뱅크 이름 식
                             child[1]  = 정책 식 (선택)            */
    NODE_REPRO_LABEL_DECL,/* 재생산라벨선언 (인라인, 블록 없음)
                             sval      = 라이선스/메모 문자열       */
    NODE_KBANK_PROOF,     /* 지식증거출력 블록
                             sval      = 출력 경로 (선택)
                             child[*]  = 설정 구문 목록            */

    NODE_COUNT

} NodeType;

/* ================================================================
 *  AST 노드 구조체
 * ================================================================ */
typedef struct Node {
    NodeType type;
    int      line;
    int      col;

    /* 자식 노드 배열 (동적 할당) */
    struct Node **children;
    int           child_count;
    int           child_cap;

    /* 리터럴 값 */
    union {
        int64_t  ival;   /* INT_LIT, CHAR_LIT                */
        double   fval;   /* FLOAT_LIT                        */
        int      bval;   /* BOOL_LIT  (1=참, 0=거짓)         */
    } val;

    /* 문자열 (식별자명, 문자열 리터럴, 레이블명, 모듈명 등) */
    char *sval;          /* malloc 할당 — node_free 에서 해제 */

    /* 연산자 / 전처리기 토큰 종류 */
    TokenType op;

    /* 자료형 키워드 (VAR_DECL, CONST_DECL, PARAM) */
    TokenType dtype;     /* TOK_KW_JEONGSU 등, 없으면 TOK_EOF */

} Node;

/* ================================================================
 *  파서 상태 구조체
 * ================================================================ */
#define KCODE_PARSER_MAX_ERRORS  64

typedef struct {
    Lexer  *lexer;
    Token   current;    /* 현재 (아직 소비 안 한) 토큰         */
    Token   previous;   /* 방금 소비한 토큰                    */

    int     had_error;
    int     panic_mode; /* 오류 복구 모드 — 다음 구문까지 건너뜀 */

    char    errors[KCODE_PARSER_MAX_ERRORS][256];
    int     error_count;
} Parser;

/* ================================================================
 *  공개 API
 * ================================================================ */

/* 노드 생성 */
Node *node_new(NodeType type, int line, int col);

/* 자식 노드 추가 */
void  node_add_child(Node *parent, Node *child);

/* 노드 및 하위 트리 전체 해제 */
void  node_free(Node *node);

/* 파서 초기화 */
void  parser_init(Parser *p, Lexer *lx);

/* 소스 전체 파싱 → 루트 NODE_PROGRAM 반환
   오류가 있어도 최대한 파싱을 계속한다. NULL 반환 시 치명적 오류. */
Node *parser_parse(Parser *p);

/* AST 트리 출력 (디버깅용) */
void  ast_print(const Node *node, int depth);

/* 노드 종류 이름 문자열 반환 */
const char *node_type_name(NodeType t);

#endif /* KCODE_PARSER_H */
