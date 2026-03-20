/*
 * kgc.h  —  Kcode 가비지 컬렉터 헤더
 * version : v13.0.0
 *
 * v5.0.0 변경 (kgc.h):
 *   - GC_INSTANCE 타입 GcType enum 에 추가 (kgc.c v3.9.1 과 동기화)
 *   - GcObject union 에 instance 멤버 추가 (class_name, fields, proto)
 *   - gc_new_instance() 팩토리 함수 선언 추가
 *   - gc_mark_value() 함수 선언 추가
 *
 * 전략: 참조 카운트(Reference Counting) 주 컬렉터
 *       + 마크-스윕(Mark-and-Sweep) 보조 사이클 컬렉터
 *
 * 구조:
 *   - 힙 할당 객체(문자열·배열·클로저·인스턴스)는 모두 GcObject 헤더를 가짐
 *   - GcObject 헤더에는 ref_count, gc_mark, 전체 오브젝트 연결 리스트 포인터 포함
 *   - Value는 GcObject*를 통해 힙 객체를 참조
 *   - 참조 카운트가 0이 되면 즉시 해제 (대부분의 경우)
 *   - 주기적 마크-스윕으로 순환 참조 탐지 및 해제
 *   - GcHeap 구조체가 전체 오브젝트 목록과 GC 상태를 관리
 *
 * MIT License
 * zerojat7
 */

#ifndef KCODE_GC_H
#define KCODE_GC_H

#include <stddef.h>
#include <stdint.h>

/* ================================================================
 *  GC 오브젝트 종류
 * ================================================================ */
typedef enum {
    GC_STRING,    /* char* 문자열          */
    GC_ARRAY,     /* Value 배열            */
    GC_CLOSURE,   /* 함수 클로저           */
    GC_INSTANCE,  /* 객체 인스턴스 (v5.0.0) */
} GcType;

/* 전방 선언 */
struct GcObject;
struct GcHeap;
struct Value;
struct Env;

/* ================================================================
 *  GcObject — 모든 힙 할당 객체의 공통 헤더
 * ================================================================ */
typedef struct GcObject {
    GcType          type;       /* 오브젝트 종류                      */
    int             ref_count;  /* 참조 카운트 (0 → 즉시 해제 후보)   */
    int             gc_mark;    /* 마크-스윕용 마크 비트               */
    struct GcObject *gc_next;   /* 전체 오브젝트 연결 리스트 (GcHeap)  */

    union {
        /* GC_STRING */
        struct {
            char   *data;       /* UTF-8 문자열 (힙 할당)             */
            size_t  len;        /* 바이트 길이                        */
        } str;

        /* GC_ARRAY */
        struct {
            struct Value *items; /* Value 원소 배열                   */
            int           len;   /* 현재 원소 수                      */
            int           cap;   /* 할당된 용량                       */
        } arr;

        /* GC_CLOSURE */
        struct {
            void       *node;    /* AST 노드 포인터 (Node*)           */
            struct Env *env;     /* 클로저 캡처 환경                  */
        } closure;

        /* GC_INSTANCE (v5.0.0) */
        struct {
            char       *class_name; /* 클래스 이름 (strdup)           */
            struct Env *fields;     /* 필드/메서드 환경               */
            struct GcObject *proto; /* 부모 인스턴스 (상속 체인)       */
        } instance;
    } data;
} GcObject;

/* ================================================================
 *  GcHeap — GC 전체 상태 관리
 * ================================================================ */
#define GC_THRESHOLD_DEFAULT  256   /* 이 수 이상 오브젝트 시 사이클 GC 실행 */

typedef struct GcHeap {
    GcObject   *all_objects;    /* 전체 오브젝트 연결 리스트 (head)   */
    int         obj_count;      /* 현재 살아있는 오브젝트 수           */
    int         gc_threshold;   /* 사이클 GC 실행 임계값              */
    int         total_alloc;    /* 누적 할당 횟수 (통계용)            */
    int         total_freed;    /* 누적 해제 횟수 (통계용)            */
    int         cycle_runs;     /* 사이클 GC 실행 횟수                */
} GcHeap;

/* ================================================================
 *  GcHeap API
 * ================================================================ */

/* GcHeap 초기화 */
void gc_heap_init(GcHeap *heap);

/* GcHeap 전체 해제 (프로그램 종료 시) */
void gc_heap_free(GcHeap *heap);

/* ================================================================
 *  GcObject 생성 — 각 타입별 팩토리
 * ================================================================ */

/* 문자열 오브젝트 생성 (s를 복사함) */
GcObject *gc_new_string(GcHeap *heap, const char *s, size_t len);

/* 문자열 오브젝트 생성 (s 소유권 이전, free 안 함) */
GcObject *gc_new_string_take(GcHeap *heap, char *s, size_t len);

/* 빈 배열 오브젝트 생성 */
GcObject *gc_new_array(GcHeap *heap);

/* 클로저 오브젝트 생성 */
GcObject *gc_new_closure(GcHeap *heap, void *node, struct Env *env);

/* 객체 인스턴스 오브젝트 생성 (v5.0.0) */
GcObject *gc_new_instance(GcHeap *heap, const char *class_name, struct Env *fields);

/* ================================================================
 *  참조 카운트 조작
 * ================================================================ */

/* 참조 추가 (+1) */
void gc_retain(GcObject *obj);

/* 참조 해제 (-1), 0 되면 즉시 해제 시도 */
void gc_release(GcHeap *heap, GcObject *obj);

/* ================================================================
 *  사이클 GC (마크-스윕)
 * ================================================================ */

/* 필요 시 사이클 GC 실행 (obj_count >= gc_threshold 일 때만) */
void gc_collect_maybe(GcHeap *heap);

/* 강제로 사이클 GC 실행 */
struct Env;  /* 전방 선언 — kinterp.h 의 Env 참조 */
void gc_mark_env(struct Env *env);           /* Env 내 Value GC 마킹         */
void gc_mark_value(struct Value *v);         /* Value 하나 GC 마킹 (v5.0.0) */
void gc_collect(GcHeap *heap);

/* ================================================================
 *  GC 통계
 * ================================================================ */
typedef struct GcStats {
    int obj_alive;      /* 현재 살아있는 오브젝트 수  */
    int total_alloc;    /* 누적 할당                  */
    int total_freed;    /* 누적 해제                  */
    int cycle_runs;     /* 사이클 GC 실행 횟수         */
} GcStats;

GcStats gc_get_stats(const GcHeap *heap);

#endif /* KCODE_GC_H */
