/*
 * kgc.c  —  Kcode 가비지 컬렉터 구현
 * version : v13.0.0
 *
 * v5.0.0 변경:
 *   - 버전 kgc.h v5.0.0 과 동기화 (변경 없음, 버전만 상속)
 *
 * 구현 전략
 * ──────────────────────────────────────────────────────────────────
 *  1차: 참조 카운트 (Reference Counting)
 *       - gc_retain() / gc_release() 를 통해 힙 객체의 수명 추적
 *       - ref_count == 0 이 되면 gc_release() 내에서 즉시 해제
 *       - 대부분의 Value 수명을 커버
 *
 *  2차: 마크-스윕 사이클 컬렉터 (Mark-and-Sweep Cycle Collector)
 *       - 순환 참조(배열이 자기 자신을 원소로 갖는 등)는
 *         참조 카운트만으로 해제 불가 → 주기적 사이클 GC 필요
 *       - GcHeap.all_objects 연결 리스트를 순회하며 마크-스윕 수행
 *       - gc_collect_maybe()  : obj_count >= gc_threshold 일 때만 실행
 *       - gc_collect()        : 강제 실행
 *
 *  GC 루트 (Roots)
 *       - Interp.global 환경의 모든 EnvEntry → GC 루트
 *       - 호출 스택 프레임의 임시 Value → 스택 마킹 시 포함
 *       - 마킹은 kinterp.c 의 gc_mark_roots() 콜백으로 위임
 *
 * MIT License
 * zerojat7
 */

#define _POSIX_C_SOURCE 200809L

#include "kgc.h"
#include "kinterp.h"   /* Value, Env, EnvEntry — 루트 마킹에 필요 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 *  내부 헬퍼
 * ================================================================ */

/* GcObject 하나를 즉시 메모리에서 해제
 * — ref_count 가 0 인 것이 보장된 상태에서만 호출 */
static void gc_destroy(GcHeap *heap, GcObject *obj) {
    if (!obj) return;

    switch (obj->type) {
        case GC_STRING:
            free(obj->data.str.data);
            obj->data.str.data = NULL;
            break;

        case GC_ARRAY:
            /* 배열 원소의 GC 참조를 해제 */
            for (int i = 0; i < obj->data.arr.len; i++) {
                Value *v = &obj->data.arr.items[i];
                if (v->type == VAL_STRING || v->type == VAL_ARRAY ||
                    v->type == VAL_FUNC) {
                    if (v->gc_obj) gc_release(heap, v->gc_obj);
                }
            }
            free(obj->data.arr.items);
            obj->data.arr.items = NULL;
            break;

        case GC_CLOSURE:
            /* 클로저 환경은 Env* — env_free_gc() 로 처리
             * 여기서는 포인터만 NULL로 */
            obj->data.closure.env  = NULL;
            obj->data.closure.node = NULL;
            break;

        case GC_INSTANCE:
            /* 인스턴스 필드 환경 해제
             * fields Env의 Value들은 GC가 관리하므로
             * Env 버킷만 해제 (EnvEntry 체인 + 이름 문자열 free) */
            free(obj->data.instance.class_name);
            obj->data.instance.class_name = NULL;
            /* fields Env 자체 해제 — EnvEntry 메모리만 (Value GC release 생략:
             * 사이클 GC 맥락에서 호출되므로 Value gc_obj는 GC가 별도 수집) */
            if (obj->data.instance.fields) {
                Env *e = obj->data.instance.fields;
                for (int i = 0; i < ENV_BUCKET_SIZE; i++) {
                    EnvEntry *en = e->buckets[i];
                    while (en) {
                        EnvEntry *nx = en->next;
                        free(en->name);
                        free(en);
                        en = nx;
                    }
                }
                free(e);
                obj->data.instance.fields = NULL;
            }
            obj->data.instance.proto = NULL;
            break;
    }   /* end switch */

    heap->total_freed++;
    heap->obj_count--;
    free(obj);
}

/* ================================================================
 *  GcHeap 초기화 / 전체 해제
 * ================================================================ */
void gc_heap_init(GcHeap *heap) {
    memset(heap, 0, sizeof(GcHeap));
    heap->gc_threshold = GC_THRESHOLD_DEFAULT;
}

void gc_heap_free(GcHeap *heap) {
    /* 남아있는 모든 오브젝트를 강제 해제 */
    GcObject *cur = heap->all_objects;
    while (cur) {
        GcObject *next = cur->gc_next;
        /* ref_count 와 무관하게 직접 해제 */
        switch (cur->type) {
            case GC_STRING:
                free(cur->data.str.data);
                break;
            case GC_ARRAY:
                free(cur->data.arr.items);
                break;
            case GC_CLOSURE:
                break;
            case GC_INSTANCE:
                free(cur->data.instance.class_name);
                if (cur->data.instance.fields) {
                    Env *e = cur->data.instance.fields;
                    for (int i = 0; i < ENV_BUCKET_SIZE; i++) {
                        EnvEntry *en = e->buckets[i];
                        while (en) { EnvEntry *nx = en->next; free(en->name); free(en); en = nx; }
                    }
                    free(e);
                }
                break;
        }
        free(cur);
        cur = next;
    }
    heap->all_objects = NULL;
    heap->obj_count   = 0;
}

/* ================================================================
 *  GcObject 생성 팩토리
 * ================================================================ */

static GcObject *gc_alloc(GcHeap *heap, GcType type) {
    GcObject *obj = (GcObject*)calloc(1, sizeof(GcObject));
    obj->type      = type;
    obj->ref_count = 1;   /* 생성 직후 참조 수 = 1 */
    obj->gc_mark   = 0;
    /* 전체 오브젝트 리스트에 연결 */
    obj->gc_next       = heap->all_objects;
    heap->all_objects  = obj;
    heap->obj_count++;
    heap->total_alloc++;
    return obj;
}

GcObject *gc_new_string(GcHeap *heap, const char *s, size_t len) {
    GcObject *obj = gc_alloc(heap, GC_STRING);
    obj->data.str.data = (char*)malloc(len + 1);
    if (s) memcpy(obj->data.str.data, s, len);
    obj->data.str.data[len] = '\0';
    obj->data.str.len  = len;
    return obj;
}

GcObject *gc_new_string_take(GcHeap *heap, char *s, size_t len) {
    GcObject *obj = gc_alloc(heap, GC_STRING);
    obj->data.str.data = s;   /* 소유권 이전 */
    obj->data.str.len  = len;
    return obj;
}

GcObject *gc_new_array(GcHeap *heap) {
    GcObject *obj = gc_alloc(heap, GC_ARRAY);
    obj->data.arr.items = NULL;
    obj->data.arr.len   = 0;
    obj->data.arr.cap   = 0;
    return obj;
}

GcObject *gc_new_closure(GcHeap *heap, void *node, struct Env *env) {
    GcObject *obj = gc_alloc(heap, GC_CLOSURE);
    obj->data.closure.node = node;
    obj->data.closure.env  = env;
    return obj;
}

/* ★ 객체 인스턴스 생성 (v3.9.1) */
GcObject *gc_new_instance(GcHeap *heap, const char *class_name, struct Env *fields) {
    GcObject *obj = gc_alloc(heap, GC_INSTANCE);
    obj->data.instance.class_name = class_name ? strdup(class_name) : strdup("(알 수 없음)");
    obj->data.instance.fields     = fields;
    obj->data.instance.proto      = NULL;
    return obj;
}

/* ================================================================
 *  참조 카운트
 * ================================================================ */
void gc_retain(GcObject *obj) {
    if (!obj) return;
    obj->ref_count++;
}

void gc_release(GcHeap *heap, GcObject *obj) {
    if (!obj) return;
    obj->ref_count--;
    if (obj->ref_count <= 0) {
        /* 전체 오브젝트 리스트에서 제거 */
        GcObject **prev = &heap->all_objects;
        while (*prev && *prev != obj)
            prev = &(*prev)->gc_next;
        if (*prev) *prev = obj->gc_next;

        gc_destroy(heap, obj);
    }
}

/* ================================================================
 *  마크-스윕 사이클 컬렉터
 * ================================================================ */

/* 전방 선언: kinterp.c 에서 구현하는 루트 마킹 함수 */
extern void gc_mark_roots(GcHeap *heap);

/* GcObject 와 그것이 참조하는 모든 객체에 마크 표시 */
static void gc_mark_object(GcObject *obj) {
    if (!obj || obj->gc_mark) return;
    obj->gc_mark = 1;

    if (obj->type == GC_ARRAY) {
        for (int i = 0; i < obj->data.arr.len; i++) {
            Value *v = &obj->data.arr.items[i];
            if (v->gc_obj) gc_mark_object(v->gc_obj);
        }
    } else if (obj->type == GC_CLOSURE) {
        /* 클로저 환경의 변수들도 마크 */
        Env *env = (Env*)obj->data.closure.env;
        /* env 내부 Value들의 gc_obj 마크 — gc_mark_env() 는 inline 처리 */
        while (env) {
            for (int i = 0; i < ENV_BUCKET_SIZE; i++) {
                EnvEntry *en = env->buckets[i];
                while (en) {
                    if (en->val.gc_obj) gc_mark_object(en->val.gc_obj);
                    en = en->next;
                }
            }
            env = env->parent;
        }
    } else if (obj->type == GC_INSTANCE) {
        /* 인스턴스 필드 환경 마킹 */
        gc_mark_env(obj->data.instance.fields);
        /* 부모 인스턴스(proto) 마킹 */
        if (obj->data.instance.proto)
            gc_mark_object(obj->data.instance.proto);
    }
}

/* Value 하나에 마크 표시 (gc_obj 있으면 추적) */
void gc_mark_value(Value *v) {
    if (!v) return;
    if (v->gc_obj) gc_mark_object(v->gc_obj);
}

/* 환경 전체 마크 */
void gc_mark_env(Env *env) {
    while (env) {
        for (int i = 0; i < ENV_BUCKET_SIZE; i++) {
            EnvEntry *en = env->buckets[i];
            while (en) {
                gc_mark_value(&en->val);
                en = en->next;
            }
        }
        env = env->parent;
    }
}

void gc_collect(GcHeap *heap) {
    heap->cycle_runs++;

    /* ── 1단계: 모든 마크 초기화 ── */
    for (GcObject *obj = heap->all_objects; obj; obj = obj->gc_next)
        obj->gc_mark = 0;

    /* ── 2단계: 루트에서 도달 가능한 오브젝트 마킹 ── */
    gc_mark_roots(heap);

    /* ── 3단계: 마크되지 않은 오브젝트 수집 ── */
    GcObject **prev = &heap->all_objects;
    while (*prev) {
        GcObject *obj = *prev;
        if (!obj->gc_mark) {
            /* 도달 불가 — 순환 참조 포함 → 강제 해제 */
            *prev = obj->gc_next;
            /* gc_destroy 전에 내부 참조 해제를 건너뜀
             * (사이클 전체를 한번에 제거하는 중이므로) */
            switch (obj->type) {
                case GC_STRING:
                    free(obj->data.str.data);
                    break;
                case GC_ARRAY:
                    free(obj->data.arr.items);
                    break;
                case GC_CLOSURE:
                    break;
                case GC_INSTANCE:
                    free(obj->data.instance.class_name);
                    if (obj->data.instance.fields) {
                        Env *e = obj->data.instance.fields;
                        for (int i = 0; i < ENV_BUCKET_SIZE; i++) {
                            EnvEntry *en = e->buckets[i];
                            while (en) { EnvEntry *nx = en->next; free(en->name); free(en); en = nx; }
                        }
                        free(e);
                    }
                    break;
            }
            heap->total_freed++;
            heap->obj_count--;
            free(obj);
        } else {
            prev = &obj->gc_next;
        }
    }
}

void gc_collect_maybe(GcHeap *heap) {
    if (heap->obj_count >= heap->gc_threshold)
        gc_collect(heap);
}

/* ================================================================
 *  GC 통계
 * ================================================================ */
GcStats gc_get_stats(const GcHeap *heap) {
    GcStats s;
    s.obj_alive   = heap->obj_count;
    s.total_alloc = heap->total_alloc;
    s.total_freed = heap->total_freed;
    s.cycle_runs  = heap->cycle_runs;
    return s;
}
