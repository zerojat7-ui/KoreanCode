/*
 * kinterp.h  —  Kcode 한글 프로그래밍 언어 인터프리터 헤더
 * version : v20.1.0
 *
 * v20.1.0 변경:
 *   - 온톨로지 3모드 블록 실행 (NODE_ONT_BLOCK/CONCEPT/PROP/RELATE/QUERY/INFER)
 *   - Interp 구조체: ont_local / ont_remote / ont_current_class / mcp_srv / mcp_exposed 추가
 *   - kc_ontology.h / kc_ont_query.h / kc_ont_remote.h / kc_mcp.h 포함
 *   - MCP 자동 노출 — 내장/대여 모드 온톨로지 도구·자원 자동 등록
 *
 * v11.0.0 변경:
 *   - VAL_TENSOR 타입 추가 (KcTensor* 직접 소유)
 *   - kc_tensor.h 포함
 *
 * v9.0.0 변경:
 *   - 가속기 NPU 완전 구현 (Python onnxruntime 스크립트 자동생성 / 5종 ONNX 연산)
 *   - GPU→NPU→CPU 폴백 체인 완성
 *
 * v8.0.0 변경:
 *   - 글자 내장 함수 21종 완전 구현 (자르기/분할/합치기/반복글자/역순/포함/위치 등)
 *
 * v7.0.0 변경:
 *   - 글자 함수 시스템 추가 (자르기/분할/합치기/포함/위치 기본 5종)
 *
 * v6.0.0 변경:
 *   - 인터럽트 시스템 3종 런타임 지원 필드 추가
 *   - SignalEntry    : OS 시그널 핸들러 레지스트리
 *   - IsrEntry       : 하드웨어 간섭 핸들러 레지스트리
 *   - EventEntry     : 행사(이벤트) 핸들러 레지스트리
 *   - Interp 구조체에 signal_handlers / isr_handlers / event_handlers 추가
 *   - isr_locked 플래그 (간섭잠금/허용)
 *   - event_running 플래그 (행사시작/중단)
 *
 * v5.0.0 변경:
 *   - NODE_CONSTITUTION / NODE_STATUTE / NODE_REGULATION eval 추가
 *   - check_contracts_layered() 추가 — 헌법→법률→규정→법령 계층 우선순위 평가
 *   - call_function() check_contracts → check_contracts_layered 연동
 *   - 파일 내장 함수 17종 register_builtins() 등록
 *     파일열기/파일닫기/파일읽기/파일줄읽기/파일쓰기/파일줄쓰기
 *     파일있음/파일크기/파일목록/파일이름/파일확장자
 *     폴더만들기/파일지우기/파일복사/파일이동
 *     파일전체읽기/파일전체쓰기
 *
 * AST(추상 구문 트리)를 직접 순회하며 실행하는
 * 트리 워킹(Tree-Walking) 인터프리터.
 *
 * v4.0.0 변경:
 *   - 법령/법위반 계약 시스템 추가 (ContractEntry, ContractRegistry)
 *   - 복원지점 스냅샷 추가 (CheckpointEntry)
 *   - Interp 에 contracts / checkpoints 필드 추가
 *   - gc_mark_roots() 에 current 스코프 마킹 추가
 *
 * v3.9.0 변경:
 *   - GC(참조 카운트 + 마크-스윕) 통합
 *   - Value 에 gc_obj(GcObject*) 필드 추가
 *   - 힙 할당 타입(STRING, ARRAY, FUNC 클로저)은
 *     모두 GcObject 를 통해 관리
 *   - Interp 에 GcHeap gc 필드 추가
 *   - val_gc_string(), val_gc_array(), val_gc_func() 추가
 *   - gc_mark_roots() 구현 (kinterp.c)
 *   - env_define/env_set/env_free 시그니처에 Interp* 추가
 *
 * MIT License
 * zerojat7
 */

#ifndef KCODE_INTERP_H
#define KCODE_INTERP_H

#include "kparser.h"
#include "kgc.h"
#include "kc_tensor.h"  /* v11.0.0 텐서 자료형 */
#include "kc_ontology.h"  /* v20.1.0 온톨로지 */
#include "kc_ont_query.h" /* v20.1.0 온톨로지 질의 */
#include "kc_ont_remote.h"/* v20.1.0 원격 온톨로지 클라이언트 */
#include "kc_mcp.h"       /* v20.1.0 MCP 자동 노출 */
#include "kc_kbank.h"        /* v22.7.0 지식 뱅크 */
#include "kc_kbank_proof.h"  /* v22.7.0 소유 증거 출력 */
#include "kc_kbank_merge.h"  /* v22.7.0 지식 병합 엔진 */
#include <stdint.h>
#include <stddef.h>

/* ================================================================
 *  런타임 값 종류
 * ================================================================ */
typedef enum {
    VAL_NULL,       /* 없음                   */
    VAL_INT,        /* 정수   (int64_t)        */
    VAL_FLOAT,      /* 실수   (double)         */
    VAL_BOOL,       /* 논리   (1=참, 0=거짓)   */
    VAL_CHAR,       /* 문자   (uint32_t UTF-32)*/
    VAL_STRING,     /* 글자   → GcObject(STR) */
    VAL_ARRAY,      /* 배열   → GcObject(ARR) */
    VAL_FUNC,       /* 함수   → GcObject(CLO) */
    VAL_OBJ,        /* 객체 인스턴스 → GcObject(INSTANCE) ★ v4.2.0 */
    VAL_BUILTIN,    /* 내장 함수 (함수 포인터)  */
    VAL_RETURN,     /* 반환 신호 (내부 전달용)  */
    VAL_BREAK,      /* 멈춤 신호               */
    VAL_CONTINUE,   /* 건너뜀 신호             */
    VAL_ERROR,      /* 오류 신호 (char* 힙)    */
    VAL_TENSOR,     /* N차원 텐서 (KcTensor*)  (v11.0.0) */
} ValueType;

/* 전방 선언 */
struct Value;
struct Env;
struct Interp;

/* 내장 함수 포인터 타입 */
typedef struct Value (*BuiltinFn)(struct Interp *interp,
                                  struct Value  *args,
                                  int            argc);

/* ================================================================
 *  런타임 값 구조체  (GC 통합)
 * ================================================================ */
typedef struct Value {
    ValueType  type;
    GcObject  *gc_obj;   /* GC 관리 힙 객체 (NULL = 스칼라 값)
                          * VAL_STRING / VAL_ARRAY / VAL_FUNC 에 사용 */
    union {
        int64_t   ival;     /* VAL_INT, VAL_CHAR                */
        double    fval;     /* VAL_FLOAT                        */
        int       bval;     /* VAL_BOOL                        */
        char     *sval;     /* VAL_ERROR only (별도 힙, GC 외부)*/
        BuiltinFn builtin;  /* VAL_BUILTIN                     */
        struct Value *retval; /* VAL_RETURN (힙 할당, GC 외부) */
        KcTensor *tensor;   /* VAL_TENSOR — 직접 소유 (v11.0.0)*/
    } as;
} Value;

/*
 * VAL_STRING 접근 편의 매크로
 */
#define VAL_STR_DATA(v)  ((v)->gc_obj->data.str.data)
#define VAL_STR_LEN(v)   ((v)->gc_obj->data.str.len)

/*
 * VAL_ARRAY 접근 편의 매크로
 */
#define VAL_ARR_ITEMS(v) ((v)->gc_obj->data.arr.items)
#define VAL_ARR_LEN(v)   ((v)->gc_obj->data.arr.len)
#define VAL_ARR_CAP(v)   ((v)->gc_obj->data.arr.cap)

/* ================================================================
 *  환경(스코프) — 변수 저장소
 * ================================================================ */
#define ENV_BUCKET_SIZE  64

typedef struct EnvEntry {
    char           *name;   /* 변수 이름 (strdup)   */
    Value           val;    /* 값                   */
    struct EnvEntry *next;  /* 해시 체인             */
} EnvEntry;

typedef struct Env {
    EnvEntry    *buckets[ENV_BUCKET_SIZE];
    struct Env  *parent;    /* 상위 스코프 (NULL = 전역) */
} Env;

/* ================================================================
 *  객체 시스템 구조체 (v4.2.0)
 * ================================================================ */

/* 클래스 레지스트리 항목 */
typedef struct ClassDef {
    char  *name;         /* 클래스 이름 (strdup)        */
    char  *parent_name;  /* 부모 클래스 이름 (strdup, NULL 가능) */
    Node  *node;         /* AST 노드 포인터 (수명: AST 따름) */
} ClassDef;

/* ================================================================
 *  계약 시스템 구조체
 * ================================================================ */

/* 계약 레지스트리 항목 */
typedef struct ContractEntry {
    TokenType  kind;     /* TOK_KW_BEOPRYEONG(법령) | TOK_KW_BEOPWIBAN(법위반) */
    char      *scope;    /* 범위 이름 ("전역" 포함, strdup) */
    Node      *cond;     /* 조건 AST 노드 (포인터만 — AST 수명 따름) */
    TokenType  sanction; /* 제재 토큰 종류 */
    char      *alt_name; /* 회귀 복원지점명 / 대체 함수명 (strdup, NULL 가능) */
    Node      *alt_node; /* 대체 값 표현식 노드 (NULL 가능) */
} ContractEntry;

/* 복원지점 — 전역 Env 스냅샷 */
typedef struct CheckpointEntry {
    char  *name;         /* 지점 이름 (strdup) */
    Env   *snapshot;     /* 전역 환경 deep-copy */
} CheckpointEntry;

/* ================================================================
 *  인터프리터 상태 구조체  (GC 통합)
 * ================================================================ */
#define INTERP_MAX_CALL_DEPTH  512

typedef struct Interp {
    Env    *global;          /* 전역 환경                         */
    Env    *current;         /* 현재 활성 환경                    */
    int     call_depth;      /* 현재 호출 깊이 (재귀 방지)         */
    char    error_msg[512];  /* 런타임 오류 메시지                */
    int     had_error;       /* 오류 발생 여부                    */
    GcHeap  gc;              /* ★ 가비지 컬렉터 힙 상태          */

    /* ★ 계약 시스템 */
    ContractEntry   *contracts;        /* 계약 레지스트리 */
    int              contract_count;
    int              contract_cap;
    CheckpointEntry *checkpoints;      /* 복원지점 목록   */
    int              checkpoint_count;
    int              checkpoint_cap;

    /* ★ 객체 시스템 (v4.2.0) */
    ClassDef        *classes;          /* 클래스 레지스트리 */
    int              class_count;
    int              class_cap;

    /* ★ 인터럽트 시스템 (v6.0.0) */

    /* A: OS 시그널 핸들러 레지스트리 */
#define INTERP_MAX_SIGNALS 16
    struct {
        int   signum;    /* SIGINT 등 POSIX 번호 */
        Node *handler;   /* 핸들러 블록 AST (NULL = 무시/기본) */
        int   mode;      /* 0=핸들러, 1=무시(SIG_IGN), 2=기본(SIG_DFL) */
    } signal_handlers[INTERP_MAX_SIGNALS];
    int signal_handler_count;

    /* B: 하드웨어 간섭(ISR) 핸들러 레지스트리 */
#define INTERP_MAX_ISR 16
    struct {
        TokenType vec;   /* TOK_KW_IRQ_TIMER0 등 */
        char     *name;  /* 벡터 이름 문자열 (strdup) */
        Node     *handler; /* ISR 블록 AST */
    } isr_handlers[INTERP_MAX_ISR];
    int  isr_handler_count;
    int  isr_locked;     /* 1=간섭잠금(cli), 0=간섭허용(sei) */

    /* C: 행사(이벤트 루프) 핸들러 레지스트리 */
#define INTERP_MAX_EVENTS 64
    struct {
        char *name;      /* 이벤트 이름 (strdup) */
        Node *handler;   /* 핸들러 블록 AST */
        Node *params;    /* 매개변수 블록 (선택) */
    } event_handlers[INTERP_MAX_EVENTS];
    int  event_handler_count;
    int  event_running;  /* 1=행사시작 루프 중 */

    /* ★ 온톨로지 시스템 (v20.1.0) */
    KcOntology   *ont_local;          /* 모드 0(내장) / 1(대여) 로컬 온톨로지 */
    KcOntRemote  *ont_remote;         /* 모드 2(접속) 원격 클라이언트 */
    KcOntClass   *ont_current_class;  /* 속성 처리 중인 현재 클래스 (내부용) */
    KcMcpServer   mcp_srv;            /* MCP 자동 노출 서버 인스턴스 */
    int           mcp_exposed;        /* MCP 자동 노출 완료 여부 (1=완료) */

    /* ★ 지식 뱅크 시스템 (v22.7.0) */
    KcKbank      *kbank_current;      /* 현재 활성 지식 뱅크 컨텍스트 */
} Interp;

/* ================================================================
 *  Value 편의 생성 함수
 * ================================================================ */
Value val_null(void);
Value val_int(int64_t v);
Value val_float(double v);
Value val_bool(int v);
Value val_char(uint32_t cp);

/* GC 관리 문자열 생성 */
Value val_gc_string(struct Interp *interp, const char *s);
Value val_gc_string_take(struct Interp *interp, char *s);

/* 하위 호환 — VAL_ERROR 등 GC 외부 임시 문자열 */
Value val_string(const char *s);
Value val_string_take(char *s);

/* GC 관리 배열/함수 생성 */
Value val_gc_array(struct Interp *interp);
Value val_gc_func(struct Interp *interp, Node *node, Env *closure);

/* GC 관리 객체 인스턴스 생성 (v4.2.0) */
Value val_gc_instance(struct Interp *interp, const char *class_name, Env *fields);

Value val_builtin(BuiltinFn fn);
Value val_return(Value inner);
Value val_break(void);
Value val_continue(void);
Value val_error(const char *fmt, ...);

/* 텐서 값 생성 (v11.0.0) */
Value val_tensor(KcTensor *t);

/* ================================================================
 *  Value 유틸리티
 * ================================================================ */
int   val_is_signal(const Value *v);
int   val_is_truthy(const Value *v);
char *val_to_string(const Value *v);

/* GC retain + 공유 복사 (힙 타입), 스칼라는 그냥 복사 */
Value val_clone(struct Interp *interp, const Value *v);

/* gc_release (힙 타입) 또는 free (오류/반환 신호) */
void  val_release(struct Interp *interp, Value *v);

int   val_equal(const Value *a, const Value *b);

/* GC 루트 마킹 (kgc.c 의 gc_collect 에서 호출됨) */
void gc_mark_roots(GcHeap *heap);

/* ================================================================
 *  환경 API
 * ================================================================ */
Env  *env_new(Env *parent);
void  env_free(Env *e, struct Interp *interp);
void  env_define(Env *e, const char *name, Value val, struct Interp *interp);
Value *env_get(Env *e, const char *name);
int   env_set(Env *e, const char *name, Value val, struct Interp *interp);

/* ================================================================
 *  배열 헬퍼 (GC 통합)
 * ================================================================ */
void gc_array_push(struct Interp *interp, GcObject *arr_obj, Value item);

/* ================================================================
 *  객체 인스턴스 헬퍼 API (v4.2.0)
 * ================================================================ */

/* 인스턴스에서 필드/메서드 값 검색 (상속 체인 탐색) */
Value *instance_get_member(GcObject *inst_obj, const char *name);

/* 인스턴스 필드/메서드 값 설정 */
void   instance_set_member(struct Interp *interp, GcObject *inst_obj,
                            const char *name, Value val);

/* ================================================================
 *  인터프리터 API
 * ================================================================ */
void  interp_init(Interp *interp);
void  interp_run(Interp *interp, Node *program);
Value interp_eval(Interp *interp, Node *node);
void  interp_free(Interp *interp);
void  interp_gc_stats(Interp *interp);  /* GC 통계 출력 */

#endif /* KCODE_INTERP_H */
