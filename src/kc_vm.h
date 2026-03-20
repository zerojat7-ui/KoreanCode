/*
 * kc_vm.h  —  Kcode KVM 스택 가상머신 헤더
 * version : v16.0.2
 *
 * v16.0.2 변경:
 *   - VAL_TENSOR 타입 추가 — KcVal 유니온에 KcTensor* 슬롯 연동
 *   - kc_tensor.h / kc_autograd.h 의존성 추가
 *
 * v16.0.0 변경:
 *   - kc_bytecode.h v16.0.0 동기화 (BUILTIN_COUNT 102종)
 *
 * v15.0.0 신규:
 *   - KcVal     : KVM 런타임 값 (태깅 유니온)
 *   - KcFrame   : 호출 프레임 (함수 스택 프레임)
 *   - KcVM      : 가상머신 주 상태
 *   - kc_vm_*   : VM 공개 API
 *
 * MIT License
 * zerojat7
 */

#ifndef KC_VM_H
#define KC_VM_H

#include "kc_bytecode.h"
#include "kc_tensor.h"
#include "kc_autograd.h"
#include <stdint.h>

/* ================================================================
 *  KVM 런타임 값 (KcVal)
 * ================================================================ */
typedef enum {
    VAL_INT    = 0,
    VAL_FLOAT  = 1,
    VAL_STR    = 2,   /* 힙 할당 (복사 의미론) */
    VAL_BOOL   = 3,
    VAL_NULL   = 4,
    VAL_ARRAY  = 5,
    VAL_DICT   = 6,
    VAL_FUNC   = 7,   /* KcChunk* 참조 */
    VAL_CLOSURE= 8,
    VAL_OBJECT = 9,
    VAL_TENSOR = 10   /* KcTensor* — kc_tensor.h 연동 */
} KcValType;

/* 전방 선언 */
typedef struct KcArray   KcArray;
typedef struct KcDict    KcDict;
typedef struct KcClosure KcClosure;

/* 런타임 값 */
typedef struct KcVal {
    KcValType type;
    union {
        int64_t     ival;
        double      fval;
        char       *sval;    /* 힙 할당 */
        int         bval;
        KcArray    *arr;
        KcDict     *dict;
        KcChunk    *func;
        KcClosure  *closure;
        KcTensor   *tensor;  /* VAL_TENSOR — kc_tensor_free로 해제 */
    } u;
} KcVal;

/* 배열 힙 오브젝트 */
struct KcArray {
    KcVal *items;
    int    len;
    int    cap;
    int    ref_count;
};

/* 사전 항목 */
typedef struct KcDictEntry {
    char  *key;
    KcVal  val;
} KcDictEntry;

/* 사전 힙 오브젝트 */
struct KcDict {
    KcDictEntry *entries;
    int          len;
    int          cap;
    int          ref_count;
};

/* 클로저 */
struct KcClosure {
    KcChunk *chunk;
    KcVal   *upvals[KBC_MAX_UPVALS];
    int      upval_count;
    int      ref_count;
};

/* ================================================================
 *  호출 프레임
 * ================================================================ */
#define KVM_FRAME_MAX    256
#define KVM_LOCALS_MAX   KBC_MAX_LOCALS

typedef struct KcFrame {
    KcChunk  *chunk;        /* 실행 중인 청크           */
    int       ip;           /* 명령어 포인터 (인덱스)   */
    KcVal     locals[KVM_LOCALS_MAX]; /* 지역 변수 슬롯 */
    int       local_count;
    /* 예외 핸들러 스택 */
    int       try_catch_ip; /* -1 = 핸들러 없음         */
    int       stack_base;   /* 이 프레임 시작 스택 위치 */
} KcFrame;

/* ================================================================
 *  전역 변수 테이블
 * ================================================================ */
#define KVM_GLOBAL_MAX 4096

typedef struct {
    char   name[128];
    KcVal  val;
} KcGlobal;

/* ================================================================
 *  KVM 가상머신
 * ================================================================ */
typedef struct {
    /* 실행 스택 */
    KcVal    stack[KBC_STACK_MAX];
    int      stack_top;     /* 다음 push 위치 */

    /* 호출 프레임 스택 */
    KcFrame  frames[KVM_FRAME_MAX];
    int      frame_count;

    /* 전역 변수 */
    KcGlobal globals[KVM_GLOBAL_MAX];
    int      global_count;

    /* 상태 */
    int      had_error;
    char     error_msg[512];

    /* 현재 실행 모듈 */
    KcModule *module;
} KcVM;

/* ================================================================
 *  공개 API
 * ================================================================ */

/* VM 초기화 */
void kc_vm_init(KcVM *vm);

/* VM 해제 (보유 값 정리) */
void kc_vm_free(KcVM *vm);

/* 모듈 실행: 0=성공, -1=오류 */
int  kc_vm_run(KcVM *vm, KcModule *mod);

/* 디버그: VM 스택 상태 출력 */
void kc_vm_dump_stack(const KcVM *vm);

/* KcVal 유틸 */
KcVal kc_val_int(int64_t v);
KcVal kc_val_float(double v);
KcVal kc_val_str(const char *s);   /* strdup 내부 수행 */
KcVal kc_val_bool(int v);
KcVal kc_val_null(void);
KcVal kc_val_tensor(KcTensor *t);  /* VAL_TENSOR 래핑 (소유권 이전) */
void  kc_val_free(KcVal *v);
void  kc_val_print(const KcVal *v);
int   kc_val_truthy(const KcVal *v);
KcVal kc_val_add(KcVM *vm, KcVal a, KcVal b);
KcVal kc_val_eq(KcVal a, KcVal b);

#endif /* KC_VM_H */
