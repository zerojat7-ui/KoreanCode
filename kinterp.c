/*
 * kinterp.c  —  Kcode 한글 프로그래밍 언어 인터프리터 구현
 * version : v27.0.1
 *
 * §12-7 (v27.0.1):
 *   런타임 온톨로지 쿼리 KACP 바이너리 전환 완료
 *   - NODE_ONT_BLOCK mode 2: kc_ont_remote_is_kacp() 연결 상태 로그
 *   - NODE_ONT_CONCEPT: ont_remote 시 kc_ont_remote_add_class() 포워딩
 *   - NODE_ONT_RELATE:  ont_remote 시 kc_ont_remote_add_relation() 포워딩
 *   - NODE_ONT_QUERY:   KACP 경로 확인 로그 + 오류 진단 강화
 *   - NODE_ONT_INFER:   KACP 경로 확인 로그 추가
 *
 * v20.1.0:
 *   - 온톨로지 3모드 블록 실행 (NODE_ONT_BLOCK / CONCEPT / PROP / RELATE / QUERY / INFER)
 *     - 모드 0 내장: kc_ont_create() → 클래스/속성/관계 빌드 → 질의/추론 실행
 *     - 모드 1 대여: 내장과 동일 + MCP 자동 노출 포함
 *     - 모드 2 접속: kc_ont_remote_connect() → 원격 질의/추론 위임
 *   - MCP 자동 노출: 내장/대여 온톨로지 → 도구 2종(질의/추론) + 자원 1종(전체JSON) 자동 등록
 *   - interp_init(): ont_local/ont_remote/mcp_exposed 초기화
 *   - interp_free(): kc_ont_destroy / kc_ont_remote_disconnect 정리
 *
 * v18.1.0:
 *   - 내장 함수 31종 추가 (명세 미구현 + 실용 함수군)
 *   수학: 제곱/라디안/각도/난수/난정수
 *   역삼각: 아크사인/아크코사인/아크탄젠트/아크탄젠트2
 *   글자: 좌문자/우문자/채우기/코드/붙여씀
 *   배열: 배열삭제/배열찾기/배열포함/배열합치기/배열자르기/유일값/배열채우기
 *   시간: 현재시간/현재날짜/시간포맷/경과시간
 *   시스템: 환경변수/종료/명령실행/잠깐
 *   JSON: JSON생성/JSON파싱
 *
 * v15.0.0:
 *   - kc_autograd.h 통합
 *   - autograd 내장함수 12종 등록:
 *     역전파 / 기울기초기화 / 미분추적
 *     미분더하기 / 미분곱 / 미분행렬곱 / 미분렐루
 *     미분시그모이드 / 미분쌍곡탄젠트 / 미분로그
 *     미분합산 / 미분평균 / 미분제곱
 *
 * v11.0.0:
 *   - kc_tensor.h 통합
 *   - NODE_TENSOR_LIT 평가 추가
 *   - 텐서 내장 함수 12종 등록:
 *     모양바꾸기 / 전치 / 펼치기 / 텐서더하기 / 텐서빼기 / 텐서곱 / 텐서나누기
 *     행렬곱 / 합산축 / 평균축 / 텐서출력 / 텐서형태
 *
 * v9.0.0 변경:
 *   - 가속기(GPU/NPU/CPU) 블록 완전 구현
 *     - GPU: CUDA C 자동 생성 → nvcc 컴파일 → 실행 → 결과 수집
 *     - NPU: Python onnxruntime 스크립트 자동생성 → python3 실행 → 결과 수집
 *       - 5종 연산 ONNX 그래프: MatMul/Add/Relu/Transpose/Conv
 *       - onnxruntime 미설치 시 numpy fallback 내장
 *     - CPU: OpenMP C 코드 생성 → gcc -fopenmp 컴파일 → 실행
 *     - 자동: GPU→NPU→CPU→일반 인터프리터 폴백 체인
 *   - NODE_GPU_OP 내장 연산 5종 처리
 *     행렬곱/행렬합/합성곱/활성화/전치
 *   - 배열 변수 CSV 직렬화/역직렬화로 환경변수 전달
 *
 * v6.0.0 변경:
 *   - 인터럽트 시스템 3종 런타임 실행 추가
 *   - A: OS 시그널 — NODE_SIGNAL_HANDLER/CTRL 평가
 *     signal() / kill() POSIX API 연동
 *   - B: 하드웨어 간섭 — NODE_ISR_HANDLER/CTRL 평가
 *     인터프리터 환경에서 시뮬레이션 (ISR 함수 등록 + 잠금/허용 플래그)
 *   - C: 행사(이벤트 루프) — NODE_EVENT_HANDLER/CTRL 평가
 *     내부 이벤트 테이블 + 비동기 루프 구현
 *
 * v5.0.0 변경:
 *   - NODE_CONSTITUTION (헌법) eval 추가 — 전역 최상위 계약 등록
 *   - NODE_STATUTE (법률) eval 추가 — 파일 단위 계약 등록
 *   - NODE_REGULATION (규정) eval 추가 — 객체 메서드 계약 등록
 *   - 계층 우선순위 평가 check_contracts_layered() 추가
 *     헌법 → 법률 → 규정 → 법령 → 법위반 순 평가
 *   - call_function() 에 check_contracts_layered() 연동
 *   - 파일 내장 함수 17종 구현 및 register_builtins() 등록
 *     파일열기/파일닫기/파일읽기/파일줄읽기/파일쓰기/파일줄쓰기
 *     파일있음/파일크기/파일목록/파일이름/파일확장자
 *     폴더만들기/파일지우기/파일복사/파일이동
 *     파일전체읽기/파일전체쓰기
 *
 * 구현 범위:
 *   - 기본 자료형: 정수, 실수, 논리, 문자, 글자, 없음
 *   - 산술/비교/논리/비트 연산
 *   - 변수 선언(var/const), 대입
 *   - 제어문: 만약/아니면, 동안, 반복(범위), 각각(foreach)
 *   - 선택(switch)/경우(case)/그외(default)
 *   - 함수/정의 선언 및 호출, 반환
 *   - 멈춤/건너뜀 (break/continue)
 *   - 시도/실패시/항상 (try/catch/finally)
 *   - 배열 리터럴 및 인덱스 접근
 *   - 멤버 접근 (배열.길이 등 기본 속성)
 *   - 내장 함수: 출력, 출력없이, 입력, 길이, 범위, 형변환
 *   - 람다 (클로저 지원)
 *   - 오류(raise) 및 런타임 오류 처리
 *   - 수학 기초: 올림(값,자릿수), 내림(값,자릿수), 반올림(값,자릿수)
 *               삼각함수, 로그, 지수, 제곱근, 절댓값, 최대/최소(배열포함)
 *   - 통계 함수: 평균, 합계, 분산, 표준편차, 중앙값, 최빈값, 누적합
 *               상관계수, 공분산, 정규화, 표준화, 배열정렬, 배열뒤집기
 *   - AI 활성함수: 시그모이드, 렐루, 쌍곡탄젠트
 *   - AI 수학: 평균제곱오차, 교차엔트로피, 소프트맥스, 위치인코딩
 *             등비수열합, 등차수열합, 점화식값
 *   - 관계 심리: 호감도(Trust, Similarity, Communication, Reward)
 *   - ★ GC: 참조 카운트 1차 + 마크-스윕 사이클 컬렉터 2차
 *
 * MIT License
 * zerojat7
 */

#define _POSIX_C_SOURCE 200809L
#include "kinterp.h"
#include "kgc.h"
#include "kc_tensor.h"   /* v11.0.0 텐서 자료형 */
#include "kc_autograd.h" /* v15.0.0 자동미분 엔진 */
#include "kc_ontology.h" /* v20.1.0 온톨로지 */
#include "kc_ont_query.h"/* v20.1.0 온톨로지 질의 */
#include "kc_ont_remote.h"/* v20.1.0 원격 클라이언트 */
#include "kc_mcp.h"      /* v20.1.0 MCP 자동 노출 */
#include "kc_vec.h"      /* v22.0.0 벡터화 레이어 */
#include "kc_vec_recon.h"/* v22.0.0 의미 복원 레이어 */
#include "kc_learn.h"    /* v22.0.0 학습 루프 + 재생산 라벨 */
#include "kc_op_def.h"   /* v22.0.0 Operation Definition */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>      /* v18.1.0: 시간/날짜 함수 */
#include <unistd.h>
#include <signal.h>    /* v6.0.0: OS 시그널 시스템 */
#include <sys/types.h> /* v6.0.0: kill(pid_t, int)  */

/* ================================================================
 *  내부 헬퍼 매크로
 * ================================================================ */

/* 오류 값이면 즉시 상위로 전파 */
#define PROPAGATE(v)  do { if ((v).type == VAL_ERROR || \
                               (v).type == VAL_RETURN || \
                               (v).type == VAL_BREAK  || \
                               (v).type == VAL_CONTINUE) return (v); } while(0)

/* 런타임 오류 생성 */
#define RT_ERROR(interp, fmt, ...) \
    do { \
        (interp)->had_error = 1; \
        snprintf((interp)->error_msg, sizeof((interp)->error_msg), \
                 "[런타임 오류] " fmt, ##__VA_ARGS__); \
        return val_error((interp)->error_msg); \
    } while(0)

/* ================================================================
 *  Value 생성
 * ================================================================ */

Value val_null(void)  { Value v; v.type=VAL_NULL;  v.gc_obj=NULL; return v; }
Value val_break(void) { Value v; v.type=VAL_BREAK; v.gc_obj=NULL; return v; }
Value val_continue(void) { Value v; v.type=VAL_CONTINUE; v.gc_obj=NULL; return v; }

Value val_int(int64_t i)   { Value v; v.type=VAL_INT;  v.gc_obj=NULL; v.as.ival=i; return v; }
Value val_float(double f)  { Value v; v.type=VAL_FLOAT;v.gc_obj=NULL; v.as.fval=f; return v; }
Value val_bool(int b)      { Value v; v.type=VAL_BOOL; v.gc_obj=NULL; v.as.bval=!!b; return v; }
Value val_char(uint32_t c) { Value v; v.type=VAL_CHAR; v.gc_obj=NULL; v.as.ival=(int64_t)c; return v; }

/* ── 내부 전용: VAL_STRING의 char* 안전하게 꺼내기 ──────────────── */
#define STR_GET(v)  ((v)->gc_obj ? (v)->gc_obj->data.str.data \
                                  : ((v)->as.sval ? (v)->as.sval : ""))


Value val_gc_string(Interp *interp, const char *s) {
    Value v; v.type = VAL_STRING;
    v.gc_obj = gc_new_string(&interp->gc, s, s ? strlen(s) : 0);
    return v;
}
Value val_gc_string_take(Interp *interp, char *s) {
    Value v; v.type = VAL_STRING;
    v.gc_obj = gc_new_string_take(&interp->gc, s, s ? strlen(s) : 0);
    return v;
}

/* ── 하위 호환 — VAL_ERROR 등 GC 외부 임시 문자열 ── */
Value val_string(const char *s) {
    Value v; v.type=VAL_STRING; v.gc_obj=NULL;
    v.as.sval = s ? strdup(s) : strdup(""); return v;
}
Value val_string_take(char *s) {
    Value v; v.type=VAL_STRING; v.gc_obj=NULL; v.as.sval=s; return v;
}

/* ── GC 관리 배열 ── */
Value val_gc_array(Interp *interp) {
    Value v; v.type = VAL_ARRAY;
    v.gc_obj = gc_new_array(&interp->gc);
    return v;
}

/* ── GC 관리 함수(클로저) ── */
Value val_gc_func(Interp *interp, Node *node, Env *closure) {
    Value v; v.type = VAL_FUNC;
    v.gc_obj = gc_new_closure(&interp->gc, node, closure);
    return v;
}

/* ★ 객체 인스턴스 Value 생성 (v4.2.0) */
Value val_gc_instance(Interp *interp, const char *class_name, Env *fields) {
    Value v; v.type = VAL_OBJ;
    v.gc_obj = gc_new_instance(&interp->gc, class_name, fields);
    return v;
}

Value val_builtin(BuiltinFn fn) {
    Value v; v.type=VAL_BUILTIN; v.gc_obj=NULL; v.as.builtin=fn; return v;
}

Value val_return(Value inner) {
    Value v; v.type=VAL_RETURN; v.gc_obj=NULL;
    v.as.retval = (Value*)malloc(sizeof(Value));
    *v.as.retval = inner; return v;
}

Value val_error(const char *fmt, ...) {
    char buf[512]; va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    Value v; v.type=VAL_ERROR; v.gc_obj=NULL; v.as.sval=strdup(buf); return v;
}

int val_is_signal(const Value *v) {
    return v->type == VAL_RETURN   ||
           v->type == VAL_BREAK    ||
           v->type == VAL_CONTINUE ||
           v->type == VAL_ERROR;
}

/* ================================================================
 *  val_is_truthy
 * ================================================================ */
int val_is_truthy(const Value *v) {
    switch (v->type) {
        case VAL_NULL:   return 0;
        case VAL_BOOL:   return v->as.bval;
        case VAL_INT:    return v->as.ival != 0;
        case VAL_FLOAT:  return v->as.fval != 0.0;
        case VAL_CHAR:   return v->as.ival != 0;
        case VAL_STRING:
            if (v->gc_obj) return v->gc_obj->data.str.len > 0;
            return STR_GET(v)[0] != '\0';
        case VAL_ARRAY:
            if (v->gc_obj) return v->gc_obj->data.arr.len > 0;
            return 0;
        default: return 1;
    }
}

/* ================================================================
 *  val_to_string  — 출력용 문자열 변환 (호출자가 free 해야 함)
 * ================================================================ */
char *val_to_string(const Value *v) {
    char buf[256];
    switch (v->type) {
        case VAL_NULL:   return strdup("없음");
        case VAL_BOOL:   return strdup(v->as.bval ? "참" : "거짓");
        case VAL_INT:    snprintf(buf, sizeof(buf), "%lld", (long long)v->as.ival);
                         return strdup(buf);
        case VAL_FLOAT: {
            snprintf(buf, sizeof(buf), "%g", v->as.fval);
            if (!strchr(buf, '.') && !strchr(buf, 'e'))
                strncat(buf, ".0", sizeof(buf) - strlen(buf) - 1);
            return strdup(buf);
        }
        case VAL_CHAR: {
            uint32_t cp = (uint32_t)v->as.ival;
            char utf[8] = {0};
            if (cp < 0x80) { utf[0] = (char)cp; }
            else if (cp < 0x800) {
                utf[0] = (char)(0xC0|(cp>>6)); utf[1] = (char)(0x80|(cp&0x3F));
            } else if (cp < 0x10000) {
                utf[0] = (char)(0xE0|(cp>>12)); utf[1] = (char)(0x80|((cp>>6)&0x3F));
                utf[2] = (char)(0x80|(cp&0x3F));
            } else {
                utf[0] = (char)(0xF0|(cp>>18)); utf[1] = (char)(0x80|((cp>>12)&0x3F));
                utf[2] = (char)(0x80|((cp>>6)&0x3F)); utf[3] = (char)(0x80|(cp&0x3F));
            }
            return strdup(utf);
        }
        case VAL_STRING:
            if (v->gc_obj) return strdup(v->gc_obj->data.str.data ? v->gc_obj->data.str.data : "");
            return strdup(v->as.sval ? v->as.sval : "");
        case VAL_ARRAY: {
            if (!v->gc_obj) return strdup("[]");
            GcObject *arr = v->gc_obj;
            char *res = strdup("[");
            for (int i = 0; i < arr->data.arr.len; i++) {
                char *item = val_to_string(&arr->data.arr.items[i]);
                size_t newlen = strlen(res) + strlen(item) + 4;
                res = (char*)realloc(res, newlen);
                strcat(res, item); free(item);
                if (i < arr->data.arr.len - 1) strcat(res, ", ");
            }
            res = (char*)realloc(res, strlen(res) + 2);
            strcat(res, "]"); return res;
        }
        case VAL_FUNC:    return strdup("<함수>");
        case VAL_OBJ: {
            if (!v->gc_obj) return strdup("<객체>");
            const char *cn = v->gc_obj->data.instance.class_name;
            char buf[256];
            snprintf(buf, sizeof(buf), "<객체:%s>", cn ? cn : "?");
            return strdup(buf);
        }
        case VAL_BUILTIN: return strdup("<내장함수>");
        case VAL_RETURN:  return v->as.retval ? val_to_string(v->as.retval) : strdup("<반환>");
        case VAL_ERROR:   return strdup(v->as.sval ? v->as.sval : "<오류>");
        case VAL_TENSOR: {
            if (!v->as.tensor) return strdup("텐서(없음)");
            char *shp = kc_tensor_shape_str(v->as.tensor);
            char buf2[128];
            snprintf(buf2, sizeof(buf2), "텐서%s", shp ? shp : "");
            free(shp);
            return strdup(buf2);
        }
        default:          return strdup("<알수없음>");
    }
}

/* ================================================================
 *  val_clone  — GC 타입: gc_retain + 포인터 공유 (얕은 복사)
 *               스칼라/오류/반환: 기존 방식
 * ================================================================ */
Value val_clone(Interp *interp, const Value *v) {
    (void)interp;
    Value c = *v;   /* 기본 필드 복사 */
    switch (v->type) {
        case VAL_STRING:
            if (v->gc_obj) {
                gc_retain(v->gc_obj);   /* GC retain — 같은 GcObject 공유 */
            } else {
                /* 비-GC 문자열 (VAL_ERROR 경로 등) — 복사 */
                c.as.sval = v->as.sval ? strdup(v->as.sval) : NULL;
            }
            break;
        case VAL_ARRAY:
        case VAL_FUNC:
        case VAL_OBJ:
            if (v->gc_obj) gc_retain(v->gc_obj);
            break;
        case VAL_ERROR:
            c.as.sval = v->as.sval ? strdup(v->as.sval) : NULL;
            break;
        case VAL_RETURN:
            if (v->as.retval) {
                c.as.retval = (Value*)malloc(sizeof(Value));
                *c.as.retval = val_clone(interp, v->as.retval);
            }
            break;
        case VAL_TENSOR:
            if (v->as.tensor) kc_tensor_retain(v->as.tensor);
            break;
        default:
            break;
    }
    return c;
}

/* ================================================================
 *  val_release  — GC 타입: gc_release / 비-GC: 기존 free
 * ================================================================ */
void val_release(Interp *interp, Value *v) {
    switch (v->type) {
        case VAL_STRING:
            if (v->gc_obj) {
                gc_release(&interp->gc, v->gc_obj);
                v->gc_obj = NULL;
            } else {
                free(v->as.sval); v->as.sval = NULL;
            }
            break;
        case VAL_ARRAY:
        case VAL_FUNC:
        case VAL_OBJ:
            if (v->gc_obj) {
                gc_release(&interp->gc, v->gc_obj);
                v->gc_obj = NULL;
            }
            break;
        case VAL_ERROR:
            free(v->as.sval); v->as.sval = NULL;
            break;
        case VAL_RETURN:
            if (v->as.retval) {
                val_release(interp, v->as.retval);
                free(v->as.retval); v->as.retval = NULL;
            }
            break;
        case VAL_TENSOR:
            if (v->as.tensor) {
                kc_tensor_release(v->as.tensor);
                v->as.tensor = NULL;
            }
            break;
        default:
            break;
    }
    v->type = VAL_NULL; v->gc_obj = NULL;
}

/* 하위 호환 래퍼 — 내부에서 val_free(&v) 호출하던 코드들을 위해
 * 인터프리터 컨텍스트 없이 호출 시: GC 타입은 단순 포인터만 NULL화
 * (ref_count 조정 없음 — GC가 나중에 수거)
 * 주의: 가능하면 val_release(interp, v) 를 직접 사용할 것 */
void val_free(Value *v) {
    switch (v->type) {
        case VAL_STRING:
            if (!v->gc_obj && v->as.sval) { free(v->as.sval); v->as.sval = NULL; }
            break;
        case VAL_ERROR:
            if (v->as.sval) { free(v->as.sval); v->as.sval = NULL; }
            break;
        case VAL_RETURN:
            if (v->as.retval) { val_free(v->as.retval); free(v->as.retval); v->as.retval = NULL; }
            break;
        case VAL_TENSOR:
            if (v->as.tensor) { kc_tensor_release(v->as.tensor); v->as.tensor = NULL; }
            break;
        default:
            break;
    }
    v->type = VAL_NULL; v->gc_obj = NULL;
}

/* ================================================================
 *  val_equal  — 동등 비교
 * ================================================================ */
int val_equal(const Value *a, const Value *b) {
    if (a->type == VAL_NULL && b->type == VAL_NULL) return 1;
    if (a->type == VAL_NULL || b->type == VAL_NULL) return 0;

    /* 숫자 간 자동 형변환 */
    if ((a->type == VAL_INT || a->type == VAL_FLOAT) &&
        (b->type == VAL_INT || b->type == VAL_FLOAT)) {
        double da = a->type == VAL_INT ? (double)a->as.ival : a->as.fval;
        double db = b->type == VAL_INT ? (double)b->as.ival : b->as.fval;
        return da == db;
    }
    if (a->type != b->type) return 0;
    switch (a->type) {
        case VAL_BOOL:   return a->as.bval == b->as.bval;
        case VAL_INT:    return a->as.ival  == b->as.ival;
        case VAL_FLOAT:  return a->as.fval  == b->as.fval;
        case VAL_CHAR:   return a->as.ival  == b->as.ival;
        case VAL_STRING: return strcmp(STR_GET(a), STR_GET(b)) == 0;
        default:         return 0;
    }
}

/* ================================================================
 *  val_tensor — KcTensor 래핑 Value 생성  (v11.0.0)
 * ================================================================ */
Value val_tensor(KcTensor *t) {
    Value v;
    v.type = VAL_TENSOR;
    v.gc_obj = NULL;
    v.as.tensor = t;
    return v;
}

/* ================================================================
 *  텐서 내장 함수 12종  (v11.0.0)
 *  헬퍼: 인수에서 KcTensor* 추출
 * ================================================================ */
static KcTensor *arg_tensor(Value *args, int argc, int idx, const char *fname) {
    if (idx >= argc || args[idx].type != VAL_TENSOR || !args[idx].as.tensor) {
        fprintf(stderr, "[텐서] %s: 인수 %d 가 텐서가 아닙니다\n", fname, idx);
        return NULL;
    }
    return args[idx].as.tensor;
}

/* 인수 배열 Value에서 int64_t shape 배열 추출
 * arr_val: VAL_ARRAY 인수
 * shape_out: 출력 버퍼 (최대 32 원소)
 * 반환: 실제 ndim, -1=실패
 */
static int extract_shape(Value *arr_val, int64_t *shape_out) {
    if (!arr_val || arr_val->type != VAL_ARRAY || !arr_val->gc_obj) return -1;
    GcObject *arr = arr_val->gc_obj;
    int ndim = arr->data.arr.len;
    if (ndim <= 0 || ndim > 32) return -1;
    for (int i = 0; i < ndim; i++) {
        Value *item = &arr->data.arr.items[i];
        if (item->type == VAL_INT) shape_out[i] = item->as.ival;
        else if (item->type == VAL_FLOAT) shape_out[i] = (int64_t)item->as.fval;
        else return -1;
    }
    return ndim;
}

/* 모양바꾸기(텐서, [d0,d1,...]) */
static Value builtin_tensor_reshape(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("모양바꾸기: 인수 2개 필요 (텐서, 형태배열)");
    KcTensor *t = arg_tensor(args, argc, 0, "모양바꾸기");
    if (!t) return val_error("모양바꾸기: 첫 인수가 텐서여야 합니다");
    int64_t shape[32];
    int ndim = extract_shape(&args[1], shape);
    if (ndim < 1) return val_error("모양바꾸기: 두 번째 인수가 정수 배열이어야 합니다");
    KcTensor *out = kc_tensor_reshape(t, shape, ndim);
    if (!out) return val_error("모양바꾸기: 실패");
    return val_tensor(out);
}

/* 전치(텐서) */
static Value builtin_tensor_transpose(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("전치: 인수 1개 필요");
    KcTensor *t = arg_tensor(args, argc, 0, "전치");
    if (!t) return val_error("전치: 텐서 인수 필요");
    KcTensor *out = kc_tensor_transpose(t);
    if (!out) return val_error("전치: 실패");
    return val_tensor(out);
}

/* 펼치기(텐서) */
static Value builtin_tensor_flatten(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("펼치기: 인수 1개 필요");
    KcTensor *t = arg_tensor(args, argc, 0, "펼치기");
    if (!t) return val_error("펼치기: 텐서 인수 필요");
    KcTensor *out = kc_tensor_flatten(t);
    if (!out) return val_error("펼치기: 실패");
    return val_tensor(out);
}

/* 텐서더하기(a, b) */
static Value builtin_tensor_add(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("텐서더하기: 인수 2개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "텐서더하기");
    KcTensor *b = arg_tensor(args, argc, 1, "텐서더하기");
    if (!a || !b) return val_error("텐서더하기: 텐서 인수 필요");
    KcTensor *out = kc_tensor_add(a, b);
    if (!out) return val_error("텐서더하기: 실패");
    return val_tensor(out);
}

/* 텐서빼기(a, b) */
static Value builtin_tensor_sub(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("텐서빼기: 인수 2개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "텐서빼기");
    KcTensor *b = arg_tensor(args, argc, 1, "텐서빼기");
    if (!a || !b) return val_error("텐서빼기: 텐서 인수 필요");
    KcTensor *out = kc_tensor_sub(a, b);
    if (!out) return val_error("텐서빼기: 실패");
    return val_tensor(out);
}

/* 텐서곱(a, b) — 원소별 곱 */
static Value builtin_tensor_mul(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("텐서곱: 인수 2개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "텐서곱");
    KcTensor *b = arg_tensor(args, argc, 1, "텐서곱");
    if (!a || !b) return val_error("텐서곱: 텐서 인수 필요");
    KcTensor *out = kc_tensor_mul(a, b);
    if (!out) return val_error("텐서곱: 실패");
    return val_tensor(out);
}

/* 텐서나누기(a, b) */
static Value builtin_tensor_div(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("텐서나누기: 인수 2개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "텐서나누기");
    KcTensor *b = arg_tensor(args, argc, 1, "텐서나누기");
    if (!a || !b) return val_error("텐서나누기: 텐서 인수 필요");
    KcTensor *out = kc_tensor_div(a, b);
    if (!out) return val_error("텐서나누기: 실패");
    return val_tensor(out);
}

/* 행렬곱(a, b) */
static Value builtin_tensor_matmul(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("행렬곱: 인수 2개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "행렬곱");
    KcTensor *b = arg_tensor(args, argc, 1, "행렬곱");
    if (!a || !b) return val_error("행렬곱: 텐서 인수 필요");
    KcTensor *out = kc_tensor_matmul(a, b);
    if (!out) return val_error("행렬곱: 실패 (차원 불일치 또는 비-2D)");
    return val_tensor(out);
}

/* 합산축(텐서, axis) */
static Value builtin_tensor_sum_axis(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("합산축: 인수 필요");
    KcTensor *t = arg_tensor(args, argc, 0, "합산축");
    if (!t) return val_error("합산축: 텐서 인수 필요");
    int axis = -1;
    if (argc >= 2 && args[1].type == VAL_INT) axis = (int)args[1].as.ival;
    KcTensor *out = kc_tensor_sum_axis(t, axis);
    if (!out) return val_error("합산축: 실패");
    return val_tensor(out);
}

/* 평균축(텐서, axis) */
static Value builtin_tensor_mean_axis(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("평균축: 인수 필요");
    KcTensor *t = arg_tensor(args, argc, 0, "평균축");
    if (!t) return val_error("평균축: 텐서 인수 필요");
    int axis = -1;
    if (argc >= 2 && args[1].type == VAL_INT) axis = (int)args[1].as.ival;
    KcTensor *out = kc_tensor_mean_axis(t, axis);
    if (!out) return val_error("평균축: 실패");
    return val_tensor(out);
}

/* 텐서출력(텐서) */
static Value builtin_tensor_print(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_TENSOR) return val_error("텐서출력: 텐서 인수 필요");
    kc_tensor_print(args[0].as.tensor);
    return val_null();
}

/* 텐서형태(텐서) — 형태를 배열로 반환 */
static Value builtin_tensor_shape(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_TENSOR || !args[0].as.tensor)
        return val_error("텐서형태: 텐서 인수 필요");
    KcTensor *t = args[0].as.tensor;
    Value arr = val_gc_array(interp);
    for (int i = 0; i < t->ndim; i++) {
        Value item = val_int(t->shape[i]);
        gc_array_push(interp, arr.gc_obj, item);
    }
    return arr;
}

/* ================================================================
 *  autograd 내장함수 구현 12종  (v15.0.0)
 * ================================================================ */

/* 역전파(텐서) — 루트 텐서에서 역전파 실행 */
static Value builtin_ag_backward(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_TENSOR || !args[0].as.tensor)
        return val_error("역전파: 텐서 인수 필요");
    kc_autograd_backward(args[0].as.tensor);
    return val_null();
}

/* 기울기초기화(텐서) — grad 버퍼를 0으로 초기화 */
static Value builtin_ag_zero_grad(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_TENSOR || !args[0].as.tensor)
        return val_error("기울기초기화: 텐서 인수 필요");
    kc_autograd_zero_grad(args[0].as.tensor);
    return val_null();
}

/* 미분추적(텐서) — requires_grad = 1 설정 */
static Value builtin_ag_track(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_TENSOR || !args[0].as.tensor)
        return val_error("미분추적: 텐서 인수 필요");
    args[0].as.tensor->requires_grad = 1;
    kc_tensor_ensure_grad(args[0].as.tensor);
    return val_null();
}

/* 미분더하기(a, b) — 덧셈 + GradFn 기록 */
static Value builtin_ag_add(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("미분더하기: 인수 2개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "미분더하기");
    KcTensor *b = arg_tensor(args, argc, 1, "미분더하기");
    if (!a || !b) return val_error("미분더하기: 텐서 인수 필요");
    KcTensor *out = kc_ag_add(a, b);
    if (!out) return val_error("미분더하기: 실패");
    return val_tensor(out);
}

/* 미분곱(a, b) — 원소별 곱 + GradFn */
static Value builtin_ag_mul(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("미분곱: 인수 2개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "미분곱");
    KcTensor *b = arg_tensor(args, argc, 1, "미분곱");
    if (!a || !b) return val_error("미분곱: 텐서 인수 필요");
    KcTensor *out = kc_ag_mul(a, b);
    if (!out) return val_error("미분곱: 실패");
    return val_tensor(out);
}

/* 미분행렬곱(a, b) — matmul + GradFn */
static Value builtin_ag_matmul(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("미분행렬곱: 인수 2개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "미분행렬곱");
    KcTensor *b = arg_tensor(args, argc, 1, "미분행렬곱");
    if (!a || !b) return val_error("미분행렬곱: 텐서 인수 필요");
    KcTensor *out = kc_ag_matmul(a, b);
    if (!out) return val_error("미분행렬곱: 실패");
    return val_tensor(out);
}

/* 미분렐루(a) — ReLU + GradFn */
static Value builtin_ag_relu(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("미분렐루: 인수 1개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "미분렐루");
    if (!a) return val_error("미분렐루: 텐서 인수 필요");
    KcTensor *out = kc_ag_relu(a);
    if (!out) return val_error("미분렐루: 실패");
    return val_tensor(out);
}

/* 미분시그모이드(a) — σ(a) + GradFn */
static Value builtin_ag_sigmoid(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("미분시그모이드: 인수 1개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "미분시그모이드");
    if (!a) return val_error("미분시그모이드: 텐서 인수 필요");
    KcTensor *out = kc_ag_sigmoid(a);
    if (!out) return val_error("미분시그모이드: 실패");
    return val_tensor(out);
}

/* 미분쌍곡탄젠트(a) — tanh(a) + GradFn */
static Value builtin_ag_tanh(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("미분쌍곡탄젠트: 인수 1개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "미분쌍곡탄젠트");
    if (!a) return val_error("미분쌍곡탄젠트: 텐서 인수 필요");
    KcTensor *out = kc_ag_tanh_op(a);
    if (!out) return val_error("미분쌍곡탄젠트: 실패");
    return val_tensor(out);
}

/* 미분로그(a) — log(a) + GradFn */
static Value builtin_ag_log(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("미분로그: 인수 1개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "미분로그");
    if (!a) return val_error("미분로그: 텐서 인수 필요");
    KcTensor *out = kc_ag_log(a);
    if (!out) return val_error("미분로그: 실패");
    return val_tensor(out);
}

/* 미분합산(a) — 전원소 합산 → 스칼라 텐서 + GradFn */
static Value builtin_ag_sum(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("미분합산: 인수 1개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "미분합산");
    if (!a) return val_error("미분합산: 텐서 인수 필요");
    KcTensor *out = kc_ag_sum(a);
    if (!out) return val_error("미분합산: 실패");
    return val_tensor(out);
}

/* 미분평균(a) — 전원소 평균 → 스칼라 텐서 + GradFn */
static Value builtin_ag_mean(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("미분평균: 인수 1개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "미분평균");
    if (!a) return val_error("미분평균: 텐서 인수 필요");
    KcTensor *out = kc_ag_mean(a);
    if (!out) return val_error("미분평균: 실패");
    return val_tensor(out);
}

/* 미분제곱(a) — a² + GradFn */
static Value builtin_ag_pow2(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("미분제곱: 인수 1개 필요");
    KcTensor *a = arg_tensor(args, argc, 0, "미분제곱");
    if (!a) return val_error("미분제곱: 텐서 인수 필요");
    KcTensor *out = kc_ag_pow2(a);
    if (!out) return val_error("미분제곱: 실패");
    return val_tensor(out);
}

/* ================================================================
 *  산업/임베디드 내장 함수 (v16.0.0)
 * ================================================================ */
static Value builtin_gpio_write(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long pin = (argc >= 1) ? args[0].as.ival : 0;
    long long val = (argc >= 2) ? args[1].as.ival : 0;
    printf("[GPIO] 핀%lld = %lld\n", pin, val);
    return val_null();
}
static Value builtin_gpio_read(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long pin = (argc >= 1) ? args[0].as.ival : 0;
    printf("[GPIO] 핀%lld 읽기\n", pin);
    return val_int(0);
}
static Value builtin_i2c_connect(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long addr = (argc >= 1) ? args[0].as.ival : 0;
    printf("[I2C] 주소 0x%llX 연결\n", addr);
    return val_int(1);
}
static Value builtin_i2c_read(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long addr = (argc >= 1) ? args[0].as.ival : 0;
    printf("[I2C] 주소 0x%llX 읽기\n", addr);
    return val_int(0);
}
static Value builtin_i2c_write(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long addr = (argc >= 1) ? args[0].as.ival : 0;
    long long data = (argc >= 2) ? args[1].as.ival : 0;
    printf("[I2C] 주소 0x%llX 쓰기: %lld\n", addr, data);
    return val_null();
}
static Value builtin_spi_send(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long data = (argc >= 1) ? args[0].as.ival : 0;
    printf("[SPI] 전송: %lld\n", data);
    return val_null();
}
static Value builtin_spi_read(Interp *interp, Value *args, int argc) {
    (void)interp; (void)args; (void)argc;
    printf("[SPI] 읽기\n");
    return val_int(0);
}
static Value builtin_uart_setup(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long baud = (argc >= 1) ? args[0].as.ival : 9600;
    printf("[UART] 보드레이트 %lld 설정\n", baud);
    return val_null();
}
static Value builtin_uart_send(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc >= 1)
        printf("[UART] 전송: %lld\n", args[0].as.ival);
    return val_null();
}
static Value builtin_uart_read(Interp *interp, Value *args, int argc) {
    (void)interp; (void)args; (void)argc;
    printf("[UART] 읽기\n");
    return val_int(0);
}
static Value builtin_modbus_connect(Interp *interp, Value *args, int argc) {
    (void)interp;
    const char *host = (argc >= 1 && args[0].type == VAL_STRING) ? args[0].as.sval : "localhost";
    long long port   = (argc >= 2) ? args[1].as.ival : 502;
    printf("[Modbus] %s:%lld 연결\n", host, port);
    return val_int(1);
}
static Value builtin_modbus_read(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long reg = (argc >= 1) ? args[0].as.ival : 0;
    printf("[Modbus] 레지스터 %lld 읽기\n", reg);
    return val_int(0);
}
static Value builtin_modbus_write(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long reg  = (argc >= 1) ? args[0].as.ival : 0;
    long long data = (argc >= 2) ? args[1].as.ival : 0;
    printf("[Modbus] 레지스터 %lld = %lld\n", reg, data);
    return val_null();
}
static Value builtin_modbus_disconnect(Interp *interp, Value *args, int argc) {
    (void)interp; (void)args; (void)argc;
    printf("[Modbus] 연결 해제\n");
    return val_null();
}
static Value builtin_can_filter(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long id   = (argc >= 1) ? args[0].as.ival : 0;
    long long mask = (argc >= 2) ? args[1].as.ival : 0x7FF;
    printf("[CAN] 필터 ID=0x%llX 마스크=0x%llX\n", id, mask);
    return val_null();
}
static Value builtin_can_send(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long id   = (argc >= 1) ? args[0].as.ival : 0;
    printf("[CAN] ID=0x%llX 전송\n", id);
    return val_null();
}
static Value builtin_can_read(Interp *interp, Value *args, int argc) {
    (void)interp; (void)args; (void)argc;
    printf("[CAN] 읽기\n");
    return val_int(0);
}
static Value builtin_mqtt_connect(Interp *interp, Value *args, int argc) {
    (void)interp;
    const char *broker = (argc >= 1 && args[0].type == VAL_STRING) ? args[0].as.sval : "localhost";
    long long port = (argc >= 2) ? args[1].as.ival : 1883;
    printf("[MQTT] %s:%lld 연결\n", broker, port);
    return val_int(1);
}
static Value builtin_mqtt_publish(Interp *interp, Value *args, int argc) {
    (void)interp;
    const char *topic = (argc >= 1 && args[0].type == VAL_STRING) ? args[0].as.sval : "";
    const char *msg   = (argc >= 2 && args[1].type == VAL_STRING) ? args[1].as.sval : "";
    printf("[MQTT] 발행 '%s': %s\n", topic, msg);
    return val_null();
}
static Value builtin_mqtt_subscribe(Interp *interp, Value *args, int argc) {
    (void)interp;
    const char *topic = (argc >= 1 && args[0].type == VAL_STRING) ? args[0].as.sval : "";
    printf("[MQTT] 구독: '%s'\n", topic);
    return val_null();
}
static Value builtin_mqtt_disconnect(Interp *interp, Value *args, int argc) {
    (void)interp; (void)args; (void)argc;
    printf("[MQTT] 연결 해제\n");
    return val_null();
}
static Value builtin_ros2_publish(Interp *interp, Value *args, int argc) {
    (void)interp;
    const char *topic = (argc >= 1 && args[0].type == VAL_STRING) ? args[0].as.sval : "";
    const char *msg   = (argc >= 2 && args[1].type == VAL_STRING) ? args[1].as.sval : "";
    printf("[ROS2] 발행 '%s': %s\n", topic, msg);
    return val_null();
}
static Value builtin_ros2_subscribe(Interp *interp, Value *args, int argc) {
    (void)interp;
    const char *topic = (argc >= 1 && args[0].type == VAL_STRING) ? args[0].as.sval : "";
    printf("[ROS2] 구독: '%s'\n", topic);
    return val_null();
}

/* ================================================================
 *  안전 규격 내장 함수 (v17.0.0)
 * ================================================================ */
static Value builtin_failsafe(Interp *interp, Value *args, int argc) {
    (void)interp; (void)args; (void)argc;
    printf("[안전] 페일세이프 상태 진입\n");
    return val_null();
}
static Value builtin_emergency_stop(Interp *interp, Value *args, int argc) {
    (void)interp; (void)args; (void)argc;
    printf("[안전] 긴급정지\n");
    return val_null();
}
static Value builtin_alarm(Interp *interp, Value *args, int argc) {
    (void)interp;
    const char *msg = (argc >= 1 && args[0].type == VAL_STRING) ? args[0].as.sval : "경보";
    printf("[안전] 경보 발령: %s\n", msg);
    return val_null();
}

/* ================================================================
 *  온디바이스 AI 내장 함수 (v18.0.0)
 * ================================================================ */
static Value builtin_ai_load(Interp *interp, Value *args, int argc) {
    (void)interp;
    const char *path = (argc >= 1 && args[0].type == VAL_STRING) ? args[0].as.sval : "";
    printf("[AI] 모델 불러오기: %s\n", path);
    return val_int(1); /* 핸들 = 1 (시뮬레이션) */
}
static Value builtin_ai_predict(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long handle = (argc >= 1) ? args[0].as.ival : 0;
    printf("[AI] 추론 실행 (모델=%lld)\n", handle);
    return val_int(0); /* 결과 클래스 = 0 (시뮬레이션) */
}
static Value builtin_ai_train_step(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long handle = (argc >= 1) ? args[0].as.ival : 0;
    printf("[AI] 학습 단계 실행 (모델=%lld)\n", handle);
    return val_null();
}
static Value builtin_ai_save(Interp *interp, Value *args, int argc) {
    (void)interp;
    long long handle = (argc >= 1) ? args[0].as.ival : 0;
    const char *path = (argc >= 2 && args[1].type == VAL_STRING) ? args[1].as.sval : "model.bin";
    printf("[AI] 모델 저장 (모델=%lld, 경로=%s)\n", handle, path);
    return val_null();
}

/* ================================================================
 *  수학 추가 내장 함수 (v18.1.0)
 *  제곱 / 라디안 / 각도 / 난수 / 난정수
 *  역삼각함수: 아크사인 / 아크코사인 / 아크탄젠트 / 아크탄젠트2
 * ================================================================ */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 제곱(밑, 지수) → 실수 */
static Value builtin_pow(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("제곱(): 인수 2개 필요 (밑, 지수)");
    double base = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    double exp2 = (args[1].type == VAL_INT) ? (double)args[1].as.ival : args[1].as.fval;
    double result = pow(base, exp2);
    /* 정수 지수이고 결과가 정수인 경우 정수 반환 */
    if (args[1].type == VAL_INT && result == (int64_t)result)
        return val_int((int64_t)result);
    return val_float(result);
}

/* 라디안(도) → 실수 */
static Value builtin_radians(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("라디안(): 인수 1개 필요 (도)");
    double deg = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(deg * M_PI / 180.0);
}

/* 각도(라디안) → 실수 */
static Value builtin_degrees(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("각도(): 인수 1개 필요 (라디안)");
    double rad = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(rad * 180.0 / M_PI);
}

/* 난수() → 실수 [0.0, 1.0) */
static Value builtin_random(Interp *interp, Value *args, int argc) {
    (void)interp; (void)args; (void)argc;
    return val_float((double)rand() / ((double)RAND_MAX + 1.0));
}

/* 난정수(최소, 최대) → 정수 [최소, 최대] */
static Value builtin_rand_int(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("난정수(): 인수 2개 필요 (최소, 최대)");
    int64_t lo = (args[0].type == VAL_INT) ? args[0].as.ival : (int64_t)args[0].as.fval;
    int64_t hi = (args[1].type == VAL_INT) ? args[1].as.ival : (int64_t)args[1].as.fval;
    if (lo > hi) return val_error("난정수(): 최소값이 최대값보다 큽니다");
    int64_t range = hi - lo + 1;
    return val_int(lo + (int64_t)(rand() % (int)range));
}

/* 아크사인(값) → 실수 (라디안) */
static Value builtin_asin(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("아크사인(): 인수 1개 필요");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(asin(v));
}

/* 아크코사인(값) → 실수 (라디안) */
static Value builtin_acos(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("아크코사인(): 인수 1개 필요");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(acos(v));
}

/* 아크탄젠트(값) → 실수 (라디안) */
static Value builtin_atan(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("아크탄젠트(): 인수 1개 필요");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(atan(v));
}

/* 아크탄젠트2(y, x) → 실수 (라디안) */
static Value builtin_atan2(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("아크탄젠트2(): 인수 2개 필요 (y, x)");
    double y = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    double x = (args[1].type == VAL_INT) ? (double)args[1].as.ival : args[1].as.fval;
    return val_float(atan2(y, x));
}

/* ================================================================
 *  글자 추가 내장 함수 (v18.1.0)
 *  좌문자 / 우문자 / 채우기 / 코드 / 붙여씀
 * ================================================================ */

/* 좌문자(문자, 길이) → 왼쪽부터 n글자 */
static Value builtin_str_left(Interp *interp, Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_INT)
        return val_error("좌문자(): 인수 2개 필요 (문자, 길이)");
    (void)interp;
    const char *s = args[0].gc_obj->data.str.data;
    size_t slen = strlen(s);
    size_t n = (size_t)args[1].as.ival;
    if (n > slen) n = slen;
    char *buf = (char *)malloc(n + 1);
    if (!buf) return val_error("좌문자(): 메모리 부족");
    memcpy(buf, s, n);
    buf[n] = '\0';
    return val_string_take(buf);
}

/* 우문자(문자, 길이) → 오른쪽부터 n글자 */
static Value builtin_str_right(Interp *interp, Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_INT)
        return val_error("우문자(): 인수 2개 필요 (문자, 길이)");
    (void)interp;
    const char *s = args[0].gc_obj->data.str.data;
    size_t slen = strlen(s);
    size_t n = (size_t)args[1].as.ival;
    if (n > slen) n = slen;
    char *buf = (char *)malloc(n + 1);
    if (!buf) return val_error("우문자(): 메모리 부족");
    memcpy(buf, s + slen - n, n);
    buf[n] = '\0';
    return val_string_take(buf);
}

/* 채우기(문자, 길이, 채울것='') → 좌패딩 문자열 */
static Value builtin_str_pad(Interp *interp, Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_INT)
        return val_error("채우기(): 인수 2~3개 필요 (문자, 길이 [, 채울것])");
    (void)interp;
    const char *s    = args[0].gc_obj->data.str.data;
    size_t slen      = strlen(s);
    size_t total     = (size_t)args[1].as.ival;
    const char *pad  = (argc >= 3 && args[2].type == VAL_STRING)
                       ? args[2].gc_obj->data.str.data : " ";
    size_t plen = strlen(pad);
    if (plen == 0) plen = 1, pad = " ";
    if (slen >= total) {
        char *buf = (char *)malloc(slen + 1);
        if (!buf) return val_error("채우기(): 메모리 부족");
        memcpy(buf, s, slen + 1);
        return val_string_take(buf);
    }
    size_t need = total - slen;
    char *buf = (char *)malloc(total + 1);
    if (!buf) return val_error("채우기(): 메모리 부족");
    for (size_t i = 0; i < need; i++) buf[i] = pad[i % plen];
    memcpy(buf + need, s, slen);
    buf[total] = '\0';
    return val_string_take(buf);
}

/* 코드(글자) → 정수 (첫 바이트의 ASCII/UTF-8 코드 포인트) */
static Value builtin_str_code(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("코드(): 글자 인수 1개 필요");
    (void)interp;
    const unsigned char *s = (const unsigned char *)args[0].gc_obj->data.str.data;
    if (!s || !s[0]) return val_int(0);
    uint32_t cp = s[0];
    /* 간단한 UTF-8 첫 코드포인트 디코딩 */
    if ((s[0] & 0x80) == 0)       cp = s[0];
    else if ((s[0] & 0xE0) == 0xC0 && s[1]) cp = ((s[0]&0x1F)<<6)|(s[1]&0x3F);
    else if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) cp = ((s[0]&0x0F)<<12)|((s[1]&0x3F)<<6)|(s[2]&0x3F);
    else if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) cp = ((s[0]&0x07)<<18)|((s[1]&0x3F)<<12)|((s[2]&0x3F)<<6)|(s[3]&0x3F);
    return val_int((int64_t)cp);
}

/* 붙여씀(문자) → 모든 공백 제거 */
static Value builtin_str_compact(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("붙여씀(): 글자 인수 1개 필요");
    (void)interp;
    const char *s = args[0].gc_obj->data.str.data;
    size_t len = strlen(s);
    char *buf = (char *)malloc(len + 1);
    if (!buf) return val_error("붙여씀(): 메모리 부족");
    size_t j = 0;
    for (size_t i = 0; i < len; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r')
            buf[j++] = s[i];
    buf[j] = '\0';
    return val_string_take(buf);
}

/* ================================================================
 *  배열 고급 내장 함수 (v18.1.0)
 *  배열삭제 / 배열찾기 / 배열포함 / 배열합치기
 *  배열자르기 / 유일값 / 배열채우기
 * ================================================================ */

/* 배열삭제(배열, 인덱스) → 없음 (원본 수정) */
static Value builtin_arr_remove(Interp *interp, Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_ARRAY || args[1].type != VAL_INT)
        return val_error("배열삭제(): 인수 2개 필요 (배열, 인덱스)");
    (void)interp;
    GcObject *obj = args[0].gc_obj;
    int n   = obj->data.arr.len;
    int idx = (int)args[1].as.ival;
    if (idx < 0) idx = n + idx;
    if (idx < 0 || idx >= n) return val_error("배열삭제(): 인덱스 범위 초과");
    val_free(&obj->data.arr.items[idx]);
    for (int i = idx; i < n - 1; i++)
        obj->data.arr.items[i] = obj->data.arr.items[i + 1];
    obj->data.arr.len--;
    return val_null();
}

/* 배열찾기(배열, 값) → 정수 인덱스 (없으면 -1) */
static Value builtin_arr_indexof(Interp *interp, Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_ARRAY)
        return val_error("배열찾기(): 인수 2개 필요 (배열, 값)");
    (void)interp;
    GcObject *obj = args[0].gc_obj;
    int n = obj->data.arr.len;
    for (int i = 0; i < n; i++) {
        Value *v = &obj->data.arr.items[i];
        /* 기본 비교: 정수/실수/논리/문자열 */
        if (v->type == args[1].type) {
            if (v->type == VAL_INT && v->as.ival == args[1].as.ival) return val_int(i);
            if (v->type == VAL_FLOAT && v->as.fval == args[1].as.fval) return val_int(i);
            if (v->type == VAL_BOOL && v->as.bval == args[1].as.bval) return val_int(i);
            if (v->type == VAL_STRING &&
                strcmp(v->gc_obj->data.str.data, args[1].gc_obj->data.str.data) == 0)
                return val_int(i);
        }
    }
    return val_int(-1);
}

/* 배열포함(배열, 값) → 논리 */
static Value builtin_arr_contains(Interp *interp, Value *args, int argc) {
    Value idx = builtin_arr_indexof(interp, args, argc);
    if (idx.type == VAL_ERROR) return idx;
    return val_bool(idx.as.ival >= 0);
}

/* 배열합치기(배열1, 배열2) → 새 배열 */
static Value builtin_arr_concat(Interp *interp, Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_ARRAY || args[1].type != VAL_ARRAY)
        return val_error("배열합치기(): 인수 2개 필요 (배열, 배열)");
    Value result = val_gc_array(interp);
    GcObject *r = result.gc_obj;
    int n1 = args[0].gc_obj->data.arr.len;
    int n2 = args[1].gc_obj->data.arr.len;
    for (int i = 0; i < n1; i++) gc_array_push(interp, r, args[0].gc_obj->data.arr.items[i]);
    for (int i = 0; i < n2; i++) gc_array_push(interp, r, args[1].gc_obj->data.arr.items[i]);
    return result;
}

/* 배열자르기(배열, 시작, 끝) → 새 배열 [시작, 끝) */
static Value builtin_arr_slice(Interp *interp, Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_ARRAY)
        return val_error("배열자르기(): 인수 2~3개 필요 (배열, 시작 [, 끝])");
    GcObject *obj = args[0].gc_obj;
    int n  = obj->data.arr.len;
    int lo = (argc >= 2 && args[1].type == VAL_INT) ? (int)args[1].as.ival : 0;
    int hi = (argc >= 3 && args[2].type == VAL_INT) ? (int)args[2].as.ival : n;
    if (lo < 0) lo = n + lo;
    if (lo < 0) lo = 0;
    if (hi < 0) hi = n + hi;
    if (hi > n) hi = n;
    Value result = val_gc_array(interp);
    for (int i = lo; i < hi; i++) gc_array_push(interp, result.gc_obj, obj->data.arr.items[i]);
    return result;
}

/* 유일값(배열) → 중복 제거 새 배열 */
static Value builtin_arr_unique(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_ARRAY)
        return val_error("유일값(): 배열 인수 1개 필요");
    GcObject *obj = args[0].gc_obj;
    int n = obj->data.arr.len;
    Value result = val_gc_array(interp);
    for (int i = 0; i < n; i++) {
        Value *v = &obj->data.arr.items[i];
        /* 이미 result에 있으면 건너뜀 */
        Value check_args[2] = { result, *v };
        Value found = builtin_arr_indexof(interp, check_args, 2);
        if (found.type == VAL_INT && found.as.ival >= 0) continue;
        gc_array_push(interp, result.gc_obj, *v);
    }
    return result;
}

/* 배열채우기(크기, 값) → 동일 값으로 채운 새 배열 */
static Value builtin_arr_fill(Interp *interp, Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_INT)
        return val_error("배열채우기(): 인수 2개 필요 (크기, 값)");
    int n = (int)args[0].as.ival;
    if (n < 0) n = 0;
    Value result = val_gc_array(interp);
    for (int i = 0; i < n; i++) gc_array_push(interp, result.gc_obj, args[1]);
    return result;
}

/* ================================================================
 *  시간/날짜 내장 함수 (v18.1.0)
 *  현재시간 / 현재날짜 / 시간포맷 / 경과시간
 * ================================================================ */

/* 현재시간() → 정수 (유닉스 타임스탬프) */
static Value builtin_time_now(Interp *interp, Value *args, int argc) {
    (void)interp; (void)args; (void)argc;
    return val_int((int64_t)time(NULL));
}

/* 현재날짜() → 문자열 "YYYY-MM-DD" */
static Value builtin_date_now(Interp *interp, Value *args, int argc) {
    (void)args; (void)argc;
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char *buf = (char *)malloc(11);
    if (!buf) return val_error("현재날짜(): 메모리 부족");
    strftime(buf, 11, "%Y-%m-%d", tm_info);
    return val_gc_string_take(interp, buf);
}

/* 시간포맷(타임스탬프, 형식) → 문자열 */
static Value builtin_time_format(Interp *interp, Value *args, int argc) {
    if (argc < 2) return val_error("시간포맷(): 인수 2개 필요 (타임스탬프, 형식)");
    time_t t = (args[0].type == VAL_INT) ? (time_t)args[0].as.ival : (time_t)args[0].as.fval;
    const char *fmt = (args[1].type == VAL_STRING) ? args[1].gc_obj->data.str.data : "%Y-%m-%d %H:%M:%S";
    struct tm *tm_info = localtime(&t);
    char *buf = (char *)malloc(256);
    if (!buf) return val_error("시간포맷(): 메모리 부족");
    strftime(buf, 256, fmt, tm_info);
    return val_gc_string_take(interp, buf);
}

/* 경과시간() → 실수 (프로그램 시작 후 경과 초) */
static Value builtin_elapsed(Interp *interp, Value *args, int argc) {
    (void)interp; (void)args; (void)argc;
    return val_float((double)clock() / (double)CLOCKS_PER_SEC);
}

/* ================================================================
 *  시스템 내장 함수 (v18.1.0)
 *  환경변수 / 종료 / 명령실행 / 잠깐
 * ================================================================ */

/* 환경변수(이름) → 문자열 (없으면 없음) */
static Value builtin_getenv(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("환경변수(): 글자 인수 1개 필요 (변수명)");
    const char *name = args[0].gc_obj->data.str.data;
    const char *val2 = getenv(name);
    if (!val2) return val_null();
    return val_gc_string(interp, val2);
}

/* 종료(코드=0) → 없음 (프로세스 종료) */
static Value builtin_exit_proc(Interp *interp, Value *args, int argc) {
    (void)interp;
    int code = (argc >= 1 && args[0].type == VAL_INT) ? (int)args[0].as.ival : 0;
    exit(code);
    return val_null(); /* 도달 안 함 */
}

/* 명령실행(명령) → 정수 (종료 코드) */
static Value builtin_shell(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("명령실행(): 글자 인수 1개 필요 (명령)");
    (void)interp;
    int ret = system(args[0].gc_obj->data.str.data);
    return val_int((int64_t)ret);
}

/* 잠깐(밀리초) → 없음 */
static Value builtin_sleep_ms(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1) return val_error("잠깐(): 인수 1개 필요 (밀리초)");
    int64_t ms = (args[0].type == VAL_INT) ? args[0].as.ival : (int64_t)args[0].as.fval;
    if (ms < 0) ms = 0;
#ifdef _WIN32
    Sleep((unsigned long)ms);
#else
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
#endif
    return val_null();
}

/* ================================================================
 *  JSON 간이 내장 함수 (v18.1.0)
 *  JSON생성 / JSON파싱 (기초 구현)
 * ================================================================ */

/* JSON생성(값) → 문자열 JSON 직렬화 */
static Value builtin_json_encode(Interp *interp, Value *args, int argc) {
    if (argc < 1) return val_error("JSON생성(): 인수 1개 필요 (값)");
    (void)interp;
    char *s = val_to_string(&args[0]);
    /* 기본: val_to_string 결과를 JSON 문자열로 래핑 (문자열 값은 따옴표 추가) */
    if (args[0].type == VAL_STRING) {
        size_t slen = strlen(s);
        char *buf = (char *)malloc(slen + 3);
        if (!buf) { free(s); return val_error("JSON생성(): 메모리 부족"); }
        buf[0] = '"';
        memcpy(buf + 1, s, slen);
        buf[slen + 1] = '"';
        buf[slen + 2] = '\0';
        free(s);
        return val_string_take(buf);
    }
    return val_string_take(s);
}

/* JSON파싱(문자열) → 정수/실수/논리/문자열/없음 (기초 스칼라 파싱) */
static Value builtin_json_decode(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("JSON파싱(): 글자 인수 1개 필요");
    (void)interp;
    const char *s = args[0].gc_obj->data.str.data;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '"') {
        /* 문자열 */
        s++;
        const char *end = strchr(s, '"');
        if (!end) return val_error("JSON파싱(): 문자열 닫는 따옴표 없음");
        size_t len = (size_t)(end - s);
        char *buf = (char *)malloc(len + 1);
        if (!buf) return val_error("JSON파싱(): 메모리 부족");
        memcpy(buf, s, len);
        buf[len] = '\0';
        return val_string_take(buf);
    }
    if (strncmp(s, "true", 4) == 0)  return val_bool(1);
    if (strncmp(s, "false", 5) == 0) return val_bool(0);
    if (strncmp(s, "null", 4) == 0)  return val_null();
    /* 숫자 */
    char *end2;
    double d = strtod(s, &end2);
    if (end2 != s) {
        /* 소수점 없으면 정수 */
        if (!strchr(s, '.') && !strchr(s, 'e') && !strchr(s, 'E'))
            return val_int((int64_t)d);
        return val_float(d);
    }
    return val_error("JSON파싱(): 지원하지 않는 JSON 형식 (배열/객체는 추후 지원)");
}

/* ================================================================
 *  환경(Env) 구현  — GC 통합 버전
 * ================================================================ */

static unsigned int env_hash(const char *name) {
    unsigned int h = 5381;
    while (*name) h = h * 31 + (unsigned char)*name++;
    return h % ENV_BUCKET_SIZE;
}

Env *env_new(Env *parent) {
    Env *e = (Env*)calloc(1, sizeof(Env));
    e->parent = parent;
    return e;
}

void env_free(Env *e, Interp *interp) {
    if (!e) return;
    for (int i = 0; i < ENV_BUCKET_SIZE; i++) {
        EnvEntry *entry = e->buckets[i];
        while (entry) {
            EnvEntry *next = entry->next;
            free(entry->name);
            val_release(interp, &entry->val);
            free(entry);
            entry = next;
        }
    }
    free(e);
}

void env_define(Env *e, const char *name, Value val, Interp *interp) {
    unsigned int h = env_hash(name);
    for (EnvEntry *en = e->buckets[h]; en; en = en->next) {
        if (strcmp(en->name, name) == 0) {
            val_release(interp, &en->val);
            en->val = val_clone(interp, &val);
            return;
        }
    }
    EnvEntry *entry = (EnvEntry*)malloc(sizeof(EnvEntry));
    entry->name   = strdup(name);
    entry->val    = val_clone(interp, &val);
    entry->next   = e->buckets[h];
    e->buckets[h] = entry;
}

Value *env_get(Env *e, const char *name) {
    for (Env *cur = e; cur; cur = cur->parent) {
        unsigned int h = env_hash(name);
        for (EnvEntry *en = cur->buckets[h]; en; en = en->next)
            if (strcmp(en->name, name) == 0)
                return &en->val;
    }
    return NULL;
}

int env_set(Env *e, const char *name, Value val, Interp *interp) {
    for (Env *cur = e; cur; cur = cur->parent) {
        unsigned int h = env_hash(name);
        for (EnvEntry *en = cur->buckets[h]; en; en = en->next) {
            if (strcmp(en->name, name) == 0) {
                val_release(interp, &en->val);
                en->val = val_clone(interp, &val);
                return 1;
            }
        }
    }
    return 0;
}

/* ================================================================
 *  gc_mark_roots  — GC 루트 마킹 (kgc.c 의 gc_collect 에서 호출)
 * ================================================================ */
/* GcHeap* 에서 소유 Interp 를 역참조하기 위한 전역 포인터 */
static Interp *g_interp_for_gc = NULL;

void gc_mark_roots(GcHeap *heap) {
    (void)heap;
    if (!g_interp_for_gc) return;
    /* 전역 환경 마킹 */
    gc_mark_env(g_interp_for_gc->global);
    /* 현재 활성 스코프 마킹 (함수 호출 중일 때 전역과 다름) */
    if (g_interp_for_gc->current &&
        g_interp_for_gc->current != g_interp_for_gc->global)
        gc_mark_env(g_interp_for_gc->current);
}

/* ================================================================
 *  gc_array_push  — GC 배열 원소 추가
 * ================================================================ */
void gc_array_push(Interp *interp, GcObject *arr_obj, Value item) {
    if (!arr_obj || arr_obj->type != GC_ARRAY) return;
    if (arr_obj->data.arr.len >= arr_obj->data.arr.cap) {
        int newcap = arr_obj->data.arr.cap == 0 ? 8 : arr_obj->data.arr.cap * 2;
        arr_obj->data.arr.items = (Value*)realloc(arr_obj->data.arr.items,
                                                   sizeof(Value) * newcap);
        arr_obj->data.arr.cap = newcap;
    }
    arr_obj->data.arr.items[arr_obj->data.arr.len++] = val_clone(interp, &item);
}

/* ================================================================
 *  배열 추가 헬퍼 (하위 호환 래퍼 — 내장함수에서 사용)
 *  실제 구현은 gc_array_push() 참조
 * ================================================================ */
static void array_push(Interp *interp, GcObject *arr_obj, Value item) {
    gc_array_push(interp, arr_obj, item);
}

/* ================================================================
 *  내장 함수 구현
 * ================================================================ */

/* 출력 — 줄바꿈 있음 */
static Value builtin_print(Interp *interp, Value *args, int argc) {
    (void)interp;
    for (int i = 0; i < argc; i++) {
        char *s = val_to_string(&args[i]);
        printf("%s", s);
        free(s);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
    return val_null();
}

/* 출력없이 — 줄바꿈 없음 */
static Value builtin_print_no_newline(Interp *interp, Value *args, int argc) {
    (void)interp;
    for (int i = 0; i < argc; i++) {
        char *s = val_to_string(&args[i]);
        printf("%s", s);
        free(s);
    }
    fflush(stdout);
    return val_null();
}

/* 입력 — 한 줄 읽기 */
static Value builtin_input(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc > 0) {
        char *prompt = val_to_string(&args[0]);
        printf("%s", prompt);
        free(prompt);
        fflush(stdout);
    }
    char buf[1024];
    if (!fgets(buf, sizeof(buf), stdin)) return val_gc_string(interp, "");
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    return val_gc_string(interp, buf);
}

/* 길이 */
static Value builtin_len(Interp *interp, Value *args, int argc) {
    if (argc != 1) return val_error("길이(): 인수 1개 필요");
    switch (args[0].type) {
        case VAL_STRING: {
            const char *s = args[0].gc_obj ? args[0].gc_obj->data.str.data : args[0].as.sval;
            return val_int((int64_t)strlen(s ? s : ""));
        }
        case VAL_ARRAY:  return val_int((int64_t)args[0].gc_obj->data.arr.len);
        default: return val_error("길이(): 글자 또는 배열이어야 합니다");
    }
    (void)interp;
}

/* 범위 — 범위(끝) 또는 범위(시작, 끝) 또는 범위(시작, 끝, 간격) */
static Value builtin_range(Interp *interp, Value *args, int argc) {
    (void)interp;
    int64_t start = 0, end = 0, step = 1;
    if (argc == 1) {
        if (args[0].type != VAL_INT) return val_error("범위(): 정수 필요");
        end = args[0].as.ival;
    } else if (argc == 2) {
        if (args[0].type != VAL_INT || args[1].type != VAL_INT)
            return val_error("범위(): 정수 필요");
        start = args[0].as.ival;
        end   = args[1].as.ival;
    } else if (argc == 3) {
        if (args[0].type != VAL_INT || args[1].type != VAL_INT || args[2].type != VAL_INT)
            return val_error("범위(): 정수 필요");
        start = args[0].as.ival;
        end   = args[1].as.ival;
        step  = args[2].as.ival;
        if (step == 0) return val_error("범위(): 간격은 0이 될 수 없습니다");
    } else {
        return val_error("범위(): 인수 1~3개 필요");
    }
    Value arr = val_gc_array(interp);
    if (step > 0) {
        for (int64_t i = start; i < end; i += step) {
            Value item = val_int(i);
            array_push(interp, arr.gc_obj, item);
        }
    } else {
        for (int64_t i = start; i > end; i += step) {
            Value item = val_int(i);
            array_push(interp, arr.gc_obj, item);
        }
    }
    return arr;
}

/* 정수 형변환 */
static Value builtin_to_int(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("정수형변환(): 인수 1개 필요");
    switch (args[0].type) {
        case VAL_INT:   return args[0];
        case VAL_FLOAT: return val_int((int64_t)args[0].as.fval);
        case VAL_BOOL:  return val_int(args[0].as.bval ? 1 : 0);
        case VAL_STRING: {
            char *end_ptr;
            int64_t v = strtoll(STR_GET(&args[0]), &end_ptr, 10);
            if (end_ptr == STR_GET(&args[0])) return val_error("정수변환 실패");
            return val_int(v);
        }
        default: return val_error("정수형변환(): 변환 불가");
    }
}

/* 실수 형변환 */
static Value builtin_to_float(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("실수형변환(): 인수 1개 필요");
    switch (args[0].type) {
        case VAL_INT:   return val_float((double)args[0].as.ival);
        case VAL_FLOAT: return args[0];
        case VAL_BOOL:  return val_float(args[0].as.bval ? 1.0 : 0.0);
        case VAL_STRING: {
            char *end_ptr;
            double v = strtod(STR_GET(&args[0]), &end_ptr);
            if (end_ptr == STR_GET(&args[0])) return val_error("실수변환 실패");
            return val_float(v);
        }
        default: return val_error("실수형변환(): 변환 불가");
    }
}

/* 글자 형변환 */
static Value builtin_to_str(Interp *interp, Value *args, int argc) {
    if (argc != 1) return val_error("글자형변환(): 인수 1개 필요");
    char *s = val_to_string(&args[0]);
    return val_gc_string_take(interp, s);
}

/* 배열 추가 */
static Value builtin_append(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 2) return val_error("추가(): 인수 2개 필요 (배열, 값)");
    if (args[0].type != VAL_ARRAY) return val_error("추가(): 첫 인수는 배열이어야 합니다");
    gc_array_push(interp, args[0].gc_obj, args[1]);
    return val_null();
}

/* ================================================================
 *  수학 기초 내장 함수 (v3.7.1)
 * ================================================================ */

/* ── 헬퍼: 배열 원소를 double 로 꺼내기 (최대/최소/통계에서 공용) ── */
static double arr_elem_dbl(Value *arr, int idx) {
    Value *v = &arr->gc_obj->data.arr.items[idx];
    return (v->type == VAL_INT) ? (double)v->as.ival : v->as.fval;
}

/* 올림(값, 자릿수=0)
 *   올림(3.2)    → 4      (정수 반환)
 *   올림(3.14, 1)→ 3.2    (실수 반환)
 *   올림(314, -2)→ 400    (정수 반환, 음수 자릿수)
 */
static Value builtin_ceil(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || argc > 2) return val_error("올림(): 인수 1~2개 필요 (값, 자릿수=0)");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    if (argc == 1) return val_int((int64_t)ceil(v));
    int64_t digits = (args[1].type == VAL_INT) ? args[1].as.ival : (int64_t)args[1].as.fval;
    double factor = pow(10.0, (double)digits);
    double result = ceil(v * factor) / factor;
    if (digits <= 0) return val_int((int64_t)result);
    return val_float(result);
}

/* 내림(값, 자릿수=0)
 *   내림(3.9)    → 3      (정수 반환)
 *   내림(3.99, 1)→ 3.9    (실수 반환)
 *   내림(399, -2)→ 300    (정수 반환, 음수 자릿수)
 */
static Value builtin_floor(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || argc > 2) return val_error("내림(): 인수 1~2개 필요 (값, 자릿수=0)");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    if (argc == 1) return val_int((int64_t)floor(v));
    int64_t digits = (args[1].type == VAL_INT) ? args[1].as.ival : (int64_t)args[1].as.fval;
    double factor = pow(10.0, (double)digits);
    double result = floor(v * factor) / factor;
    if (digits <= 0) return val_int((int64_t)result);
    return val_float(result);
}

/* 반올림(값, 자릿수=0)
 *   반올림(3.456)     → 3        (정수 반환)
 *   반올림(3.456, 2)  → 3.46     (실수 반환)
 *   반올림(3456, -2)  → 3500     (정수 반환, 음수 자릿수)
 */
static Value builtin_round(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || argc > 2) return val_error("반올림(): 인수 1~2개 필요 (값, 자릿수=0)");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;

    if (argc == 1) {
        /* 자릿수 없음 → 정수 반환 */
        return val_int((int64_t)round(v));
    }

    int64_t digits = (args[1].type == VAL_INT) ? args[1].as.ival : (int64_t)args[1].as.fval;
    double  factor = pow(10.0, (double)digits);
    double  result = round(v * factor) / factor;

    if (digits <= 0) {
        /* 소수점 없음 → 정수 반환 */
        return val_int((int64_t)result);
    }
    return val_float(result);
}

/* 사인 */
static Value builtin_sin(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("사인(): 인수 1개 필요 (라디안)");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(sin(v));
}

/* 코사인 */
static Value builtin_cos(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("코사인(): 인수 1개 필요 (라디안)");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(cos(v));
}

/* 탄젠트 */
static Value builtin_tan(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("탄젠트(): 인수 1개 필요 (라디안)");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(tan(v));
}

/* 자연로그 ln(x) */
static Value builtin_ln(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("자연로그(): 인수 1개 필요");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    if (v <= 0.0) return val_error("자연로그(): 인수는 양수여야 합니다");
    return val_float(log(v));
}

/* 로그(밑, x) — log_base(x) */
static Value builtin_log(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 2) return val_error("로그(): 인수 2개 필요 (밑, 값)");
    double base = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    double x    = (args[1].type == VAL_INT) ? (double)args[1].as.ival : args[1].as.fval;
    if (base <= 0.0 || base == 1.0) return val_error("로그(): 밑은 양수이고 1이 아니어야 합니다");
    if (x <= 0.0) return val_error("로그(): 값은 양수여야 합니다");
    return val_float(log(x) / log(base));
}

/* 지수 e^x */
static Value builtin_exp(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("지수(): 인수 1개 필요");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(exp(v));
}

/* 제곱근 */
static Value builtin_sqrt(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("제곱근(): 인수 1개 필요");
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(sqrt(v));
}

/* 절댓값 */
static Value builtin_abs(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("절댓값(): 인수 1개 필요");
    if (args[0].type == VAL_INT) return val_int(llabs(args[0].as.ival));
    return val_float(fabs(args[0].as.fval));
}

/* 최댓값 — 가변 인수 또는 배열 1개 허용 */
static Value builtin_max(Interp *interp, Value *args, int argc) {
    (void)interp;
    /* 배열 1개 인수인 경우 배열 원소에서 최대 탐색 */
    if (argc == 1 && args[0].type == VAL_ARRAY) {
        int n = args[0].gc_obj->data.arr.len;
        if (n == 0) return val_error("최대(): 빈 배열입니다");
        Value best = args[0].gc_obj->data.arr.items[0];
        for (int i = 1; i < n; i++) {
            double a = (best.type == VAL_INT) ? (double)best.as.ival : best.as.fval;
            double b = arr_elem_dbl(&args[0], i);
            if (b > a) best = args[0].gc_obj->data.arr.items[i];
        }
        return val_clone(interp, &best);
    }
    if (argc < 2) return val_error("최대(): 인수 2개 이상 또는 배열 1개 필요");
    Value best = args[0];
    for (int i = 1; i < argc; i++) {
        double a = (best.type == VAL_INT) ? (double)best.as.ival : best.as.fval;
        double b = (args[i].type == VAL_INT) ? (double)args[i].as.ival : args[i].as.fval;
        if (b > a) best = args[i];
    }
    return val_clone(interp, &best);
}

/* 최솟값 — 가변 인수 또는 배열 1개 허용 */
static Value builtin_min(Interp *interp, Value *args, int argc) {
    (void)interp;
    /* 배열 1개 인수인 경우 배열 원소에서 최소 탐색 */
    if (argc == 1 && args[0].type == VAL_ARRAY) {
        int n = args[0].gc_obj->data.arr.len;
        if (n == 0) return val_error("최소(): 빈 배열입니다");
        Value best = args[0].gc_obj->data.arr.items[0];
        for (int i = 1; i < n; i++) {
            double a = (best.type == VAL_INT) ? (double)best.as.ival : best.as.fval;
            double b = arr_elem_dbl(&args[0], i);
            if (b < a) best = args[0].gc_obj->data.arr.items[i];
        }
        return val_clone(interp, &best);
    }
    if (argc < 2) return val_error("최소(): 인수 2개 이상 또는 배열 1개 필요");
    Value best = args[0];
    for (int i = 1; i < argc; i++) {
        double a = (best.type == VAL_INT) ? (double)best.as.ival : best.as.fval;
        double b = (args[i].type == VAL_INT) ? (double)args[i].as.ival : args[i].as.fval;
        if (b < a) best = args[i];
    }
    return val_clone(interp, &best);
}

/* ================================================================
 *  통계 내장 함수 (v3.8.0)
 * ================================================================ */

/* ── 헬퍼: double 비교 (qsort용) ── */
static int cmp_double_asc(const void *a, const void *b) {
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

/* 합계(배열) → 실수 */
static Value builtin_sum(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1 || args[0].type != VAL_ARRAY)
        return val_error("합계(): 배열 인수 1개 필요");
    int n = args[0].gc_obj->data.arr.len;
    double s = 0.0;
    for (int i = 0; i < n; i++) s += arr_elem_dbl(&args[0], i);
    return val_float(s);
}

/* 평균(배열) → 실수 */
static Value builtin_mean(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1 || args[0].type != VAL_ARRAY)
        return val_error("평균(): 배열 인수 1개 필요");
    int n = args[0].gc_obj->data.arr.len;
    if (n == 0) return val_error("평균(): 빈 배열입니다");
    double s = 0.0;
    for (int i = 0; i < n; i++) s += arr_elem_dbl(&args[0], i);
    return val_float(s / n);
}

/* 분산(배열, 표본=0) → 실수
 *   분산(배열)    → 모분산 (/ N)
 *   분산(배열, 1) → 표본분산 (/ N-1)
 */
static Value builtin_variance(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || argc > 2 || args[0].type != VAL_ARRAY)
        return val_error("분산(): 인수 1~2개 필요 (배열, 표본여부=0)");
    int n = args[0].gc_obj->data.arr.len;
    if (n < 2) return val_error("분산(): 원소가 2개 이상이어야 합니다");
    int sample = (argc == 2) ? (int)((args[1].type == VAL_INT) ? args[1].as.ival : (int64_t)args[1].as.fval) : 0;
    double mu = 0.0;
    for (int i = 0; i < n; i++) mu += arr_elem_dbl(&args[0], i);
    mu /= n;
    double sq = 0.0;
    for (int i = 0; i < n; i++) {
        double d = arr_elem_dbl(&args[0], i) - mu;
        sq += d * d;
    }
    return val_float(sq / (sample ? (n - 1) : n));
}

/* 표준편차(배열, 표본=0) → 실수 */
static Value builtin_stddev(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || argc > 2 || args[0].type != VAL_ARRAY)
        return val_error("표준편차(): 인수 1~2개 필요 (배열, 표본여부=0)");
    /* 분산 계산 재사용 */
    Value var = builtin_variance(interp, args, argc);
    if (var.type == VAL_ERROR) return var;
    return val_float(sqrt(var.as.fval));
}

/* 중앙값(배열) → 실수 */
static Value builtin_median(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1 || args[0].type != VAL_ARRAY)
        return val_error("중앙값(): 배열 인수 1개 필요");
    int n = args[0].gc_obj->data.arr.len;
    if (n == 0) return val_error("중앙값(): 빈 배열입니다");
    double *tmp = malloc((size_t)n * sizeof(double));
    for (int i = 0; i < n; i++) tmp[i] = arr_elem_dbl(&args[0], i);
    qsort(tmp, (size_t)n, sizeof(double), cmp_double_asc);
    double med = (n % 2 == 0) ? (tmp[n/2 - 1] + tmp[n/2]) / 2.0 : tmp[n/2];
    free(tmp);
    return val_float(med);
}

/* 최빈값(배열) → 실수 (동점 시 첫 번째 값) */
static Value builtin_mode(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1 || args[0].type != VAL_ARRAY)
        return val_error("최빈값(): 배열 인수 1개 필요");
    int n = args[0].gc_obj->data.arr.len;
    if (n == 0) return val_error("최빈값(): 빈 배열입니다");
    double *tmp = malloc((size_t)n * sizeof(double));
    for (int i = 0; i < n; i++) tmp[i] = arr_elem_dbl(&args[0], i);
    qsort(tmp, (size_t)n, sizeof(double), cmp_double_asc);
    double mode_val = tmp[0]; int max_cnt = 1, cnt = 1;
    for (int i = 1; i < n; i++) {
        if (tmp[i] == tmp[i-1]) { cnt++; if (cnt > max_cnt) { max_cnt = cnt; mode_val = tmp[i]; } }
        else cnt = 1;
    }
    free(tmp);
    return val_float(mode_val);
}

/* 누적합(배열) → 배열 */
static Value builtin_cumsum(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1 || args[0].type != VAL_ARRAY)
        return val_error("누적합(): 배열 인수 1개 필요");
    int n = args[0].gc_obj->data.arr.len;
    Value result = val_gc_array(interp);
    double acc = 0.0;
    for (int i = 0; i < n; i++) {
        acc += arr_elem_dbl(&args[0], i);
        Value elem = val_float(acc);
        array_push(interp, result.gc_obj, elem);
        val_free(&elem);
    }
    return result;
}

/* 공분산(배열, 배열, 표본=0) → 실수 */
static Value builtin_covariance(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2 || argc > 3 || args[0].type != VAL_ARRAY || args[1].type != VAL_ARRAY)
        return val_error("공분산(): 인수 2~3개 필요 (배열, 배열, 표본여부=0)");
    int n = args[0].gc_obj->data.arr.len;
    if (n != args[1].gc_obj->data.arr.len) return val_error("공분산(): 두 배열의 길이가 같아야 합니다");
    if (n < 2) return val_error("공분산(): 원소가 2개 이상이어야 합니다");
    int sample = (argc == 3) ? (int)((args[2].type == VAL_INT) ? args[2].as.ival : (int64_t)args[2].as.fval) : 0;
    double mu1 = 0.0, mu2 = 0.0;
    for (int i = 0; i < n; i++) { mu1 += arr_elem_dbl(&args[0], i); mu2 += arr_elem_dbl(&args[1], i); }
    mu1 /= n; mu2 /= n;
    double cov = 0.0;
    for (int i = 0; i < n; i++)
        cov += (arr_elem_dbl(&args[0], i) - mu1) * (arr_elem_dbl(&args[1], i) - mu2);
    return val_float(cov / (sample ? (n - 1) : n));
}

/* 상관계수(배열, 배열) → 실수  피어슨 r */
static Value builtin_correlation(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 2 || args[0].type != VAL_ARRAY || args[1].type != VAL_ARRAY)
        return val_error("상관계수(): 배열 인수 2개 필요");
    int n = args[0].gc_obj->data.arr.len;
    if (n != args[1].gc_obj->data.arr.len) return val_error("상관계수(): 두 배열의 길이가 같아야 합니다");
    if (n < 2) return val_error("상관계수(): 원소가 2개 이상이어야 합니다");
    double mu1 = 0.0, mu2 = 0.0;
    for (int i = 0; i < n; i++) { mu1 += arr_elem_dbl(&args[0], i); mu2 += arr_elem_dbl(&args[1], i); }
    mu1 /= n; mu2 /= n;
    double cov = 0.0, sq1 = 0.0, sq2 = 0.0;
    for (int i = 0; i < n; i++) {
        double d1 = arr_elem_dbl(&args[0], i) - mu1;
        double d2 = arr_elem_dbl(&args[1], i) - mu2;
        cov += d1 * d2; sq1 += d1 * d1; sq2 += d2 * d2;
    }
    double denom = sqrt(sq1) * sqrt(sq2);
    if (denom == 0.0) return val_error("상관계수(): 분모가 0 (분산이 없는 배열)");
    return val_float(cov / denom);
}

/* 정규화(배열) → 배열  [0, 1] 범위로 Min-Max 스케일링 */
static Value builtin_normalize(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1 || args[0].type != VAL_ARRAY)
        return val_error("정규화(): 배열 인수 1개 필요");
    int n = args[0].gc_obj->data.arr.len;
    if (n == 0) return val_gc_array(interp);
    double mn = arr_elem_dbl(&args[0], 0), mx = mn;
    for (int i = 1; i < n; i++) {
        double v = arr_elem_dbl(&args[0], i);
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    double rng = mx - mn;
    Value result = val_gc_array(interp);
    for (int i = 0; i < n; i++) {
        double v = (rng == 0.0) ? 0.0 : (arr_elem_dbl(&args[0], i) - mn) / rng;
        Value elem = val_float(v);
        array_push(interp, result.gc_obj, elem);
        val_free(&elem);
    }
    return result;
}

/* 표준화(배열) → 배열  Z-score 표준화 */
static Value builtin_standardize(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1 || args[0].type != VAL_ARRAY)
        return val_error("표준화(): 배열 인수 1개 필요");
    int n = args[0].gc_obj->data.arr.len;
    if (n < 2) return val_error("표준화(): 원소가 2개 이상이어야 합니다");
    double mu = 0.0;
    for (int i = 0; i < n; i++) mu += arr_elem_dbl(&args[0], i);
    mu /= n;
    double sq = 0.0;
    for (int i = 0; i < n; i++) { double d = arr_elem_dbl(&args[0], i) - mu; sq += d * d; }
    double sigma = sqrt(sq / n);
    if (sigma == 0.0) return val_error("표준화(): 표준편차가 0 (모두 같은 값)");
    Value result = val_gc_array(interp);
    for (int i = 0; i < n; i++) {
        Value elem = val_float((arr_elem_dbl(&args[0], i) - mu) / sigma);
        array_push(interp, result.gc_obj, elem);
        val_free(&elem);
    }
    return result;
}

/* 배열정렬(배열, 내림차순=0) → 새 배열 */
static Value builtin_arr_sort(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || argc > 2 || args[0].type != VAL_ARRAY)
        return val_error("배열정렬(): 인수 1~2개 필요 (배열, 내림차순=0)");
    int n = args[0].gc_obj->data.arr.len;
    int desc = (argc == 2) ? (int)((args[1].type == VAL_INT) ? args[1].as.ival : (int64_t)args[1].as.fval) : 0;
    double *tmp = malloc((size_t)n * sizeof(double));
    for (int i = 0; i < n; i++) tmp[i] = arr_elem_dbl(&args[0], i);
    qsort(tmp, (size_t)n, sizeof(double), cmp_double_asc);
    Value result = val_gc_array(interp);
    for (int i = 0; i < n; i++) {
        int idx = desc ? (n - 1 - i) : i;
        Value elem = val_float(tmp[idx]);
        array_push(interp, result.gc_obj, elem);
        val_free(&elem);
    }
    free(tmp);
    return result;
}

/* 배열뒤집기(배열) → 새 배열 */
static Value builtin_arr_reverse(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1 || args[0].type != VAL_ARRAY)
        return val_error("배열뒤집기(): 배열 인수 1개 필요");
    int n = args[0].gc_obj->data.arr.len;
    Value result = val_gc_array(interp);
    for (int i = n - 1; i >= 0; i--) {
        Value elem = val_clone(interp, &args[0].gc_obj->data.arr.items[i]);
        array_push(interp, result.gc_obj, elem);
        val_free(&elem);
    }
    return result;
}

/* ================================================================
 *  AI 활성함수 (v3.8.0)
 * ================================================================ */

/* 시그모이드(값) → 실수   σ(x) = 1 / (1 + e^-x) */
static Value builtin_sigmoid(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("시그모이드(): 인수 1개 필요 (숫자 또는 배열)");
    if (args[0].type == VAL_ARRAY) {
        /* 배열 원소 각각 적용 */
        int n = args[0].gc_obj->data.arr.len;
        Value result = val_gc_array(interp);
        for (int i = 0; i < n; i++) {
            Value elem = val_float(1.0 / (1.0 + exp(-arr_elem_dbl(&args[0], i))));
            array_push(interp, result.gc_obj, elem);
            val_free(&elem);
        }
        return result;
    }
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(1.0 / (1.0 + exp(-v)));
}

/* 렐루(값) → 실수   ReLU(x) = max(0, x) */
static Value builtin_relu(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("렐루(): 인수 1개 필요 (숫자 또는 배열)");
    if (args[0].type == VAL_ARRAY) {
        int n = args[0].gc_obj->data.arr.len;
        Value result = val_gc_array(interp);
        for (int i = 0; i < n; i++) {
            double v = arr_elem_dbl(&args[0], i);
            Value elem = val_float(v > 0.0 ? v : 0.0);
            array_push(interp, result.gc_obj, elem);
            val_free(&elem);
        }
        return result;
    }
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(v > 0.0 ? v : 0.0);
}

/* 쌍곡탄젠트(값) → 실수   tanh(x) */
static Value builtin_tanh_fn(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1) return val_error("쌍곡탄젠트(): 인수 1개 필요 (숫자 또는 배열)");
    if (args[0].type == VAL_ARRAY) {
        int n = args[0].gc_obj->data.arr.len;
        Value result = val_gc_array(interp);
        for (int i = 0; i < n; i++) {
            Value elem = val_float(tanh(arr_elem_dbl(&args[0], i)));
            array_push(interp, result.gc_obj, elem);
            val_free(&elem);
        }
        return result;
    }
    double v = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    return val_float(tanh(v));
}

/* ================================================================
 *  호감도 / 관계 심리 함수 (v3.8.0)
 * ================================================================
 *
 *  Love = f(T, S, C, R)  ─ Interpersonal Attraction Model
 *
 *    기본형:  호감도(T, S, C, R)
 *      L = (T + S + C + R) / 4   → 0.0 ~ 1.0
 *
 *    가중합형: 호감도(T, S, C, R, α, β, γ, δ)
 *      L = αT + βS + γC + δR     → 계수를 직접 지정
 *
 *    T : Trust        신뢰   (0.0 ~ 1.0 권장)
 *    S : Similarity   유사성 (0.0 ~ 1.0 권장)
 *    C : Communication소통   (0.0 ~ 1.0 권장)
 *    R : Reward       행복감 (0.0 ~ 1.0 권장)
 *    α,β,γ,δ : 개인별 가중치 (합이 1.0이면 정규화됨)
 *
 *  반환값은 실수. 권장 범위 [0, 1] — 계수에 따라 범위 달라질 수 있음.
 * ──────────────────────────────────────────────────────────────── */
static Value builtin_attraction(Interp *interp, Value *args, int argc) {
    (void)interp;
    /* 인수 4개: 기본 평균형 */
    if (argc == 4) {
        double T = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
        double S = (args[1].type == VAL_INT) ? (double)args[1].as.ival : args[1].as.fval;
        double C = (args[2].type == VAL_INT) ? (double)args[2].as.ival : args[2].as.fval;
        double R = (args[3].type == VAL_INT) ? (double)args[3].as.ival : args[3].as.fval;
        return val_float((T + S + C + R) / 4.0);
    }
    /* 인수 8개: 가중합형  L = αT + βS + γC + δR */
    if (argc == 8) {
        double T = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
        double S = (args[1].type == VAL_INT) ? (double)args[1].as.ival : args[1].as.fval;
        double C = (args[2].type == VAL_INT) ? (double)args[2].as.ival : args[2].as.fval;
        double R = (args[3].type == VAL_INT) ? (double)args[3].as.ival : args[3].as.fval;
        double a = (args[4].type == VAL_INT) ? (double)args[4].as.ival : args[4].as.fval;
        double b = (args[5].type == VAL_INT) ? (double)args[5].as.ival : args[5].as.fval;
        double g = (args[6].type == VAL_INT) ? (double)args[6].as.ival : args[6].as.fval;
        double d = (args[7].type == VAL_INT) ? (double)args[7].as.ival : args[7].as.fval;
        return val_float(a * T + b * S + g * C + d * R);
    }
    /* 인수 2개 (배열형): 호감도([T,S,C,R]) 또는 호감도([T,S,C,R], [α,β,γ,δ]) */
    if (argc == 1 && args[0].type == VAL_ARRAY) {
        if (args[0].gc_obj->data.arr.len != 4)
            return val_error("호감도(): 배열에 T,S,C,R 4개 원소가 필요합니다");
        double sum = 0.0;
        for (int i = 0; i < 4; i++) sum += arr_elem_dbl(&args[0], i);
        return val_float(sum / 4.0);
    }
    if (argc == 2 && args[0].type == VAL_ARRAY && args[1].type == VAL_ARRAY) {
        if (args[0].gc_obj->data.arr.len != 4 || args[1].gc_obj->data.arr.len != 4)
            return val_error("호감도(): 두 배열 모두 4개 원소 [T,S,C,R], [α,β,γ,δ] 필요");
        double result = 0.0;
        for (int i = 0; i < 4; i++)
            result += arr_elem_dbl(&args[1], i) * arr_elem_dbl(&args[0], i);
        return val_float(result);
    }
    return val_error(
        "호감도(): 인수 형식 —\n"
        "  호감도(T, S, C, R)              기본 평균\n"
        "  호감도(T, S, C, R, α, β, γ, δ) 가중합\n"
        "  호감도([T,S,C,R])               배열 평균\n"
        "  호감도([T,S,C,R], [α,β,γ,δ])   배열 가중합"
    );
}

/* ================================================================
 *  AI / 수학 내장 함수 (슬라이드 기반)
 *  v3.5.0 추가
 * ================================================================ */

/* ────────────────────────────────────────────────────────────────
 * 평균제곱오차(예측배열, 실제배열)
 *   MSE = (1/N) * Σ (yi - ŷi)²
 *   슬라이드 31
 * ──────────────────────────────────────────────────────────────── */
static Value builtin_mse(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 2) return val_error("평균제곱오차(): 인수 2개 필요 (예측배열, 실제배열)");
    if (args[0].type != VAL_ARRAY || args[1].type != VAL_ARRAY)
        return val_error("평균제곱오차(): 두 인수 모두 배열이어야 합니다");
    int n = args[0].gc_obj->data.arr.len;
    if (n != args[1].gc_obj->data.arr.len)
        return val_error("평균제곱오차(): 두 배열의 길이가 같아야 합니다");
    if (n == 0) return val_float(0.0);

    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        double diff = arr_elem_dbl(&args[0], i) - arr_elem_dbl(&args[1], i);
        sum += diff * diff;
    }
    return val_float(sum / n);
}

/* ────────────────────────────────────────────────────────────────
 * 교차엔트로피(P_star 배열, P 배열)
 *   H(P*|P) = -Σ P*(i) * log P(i)
 *   슬라이드 32 — 분류 손실 함수
 * ──────────────────────────────────────────────────────────────── */
static Value builtin_cross_entropy(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 2) return val_error("교차엔트로피(): 인수 2개 필요 (실제분포, 예측분포)");
    if (args[0].type != VAL_ARRAY || args[1].type != VAL_ARRAY)
        return val_error("교차엔트로피(): 두 인수 모두 배열이어야 합니다");
    int n = args[0].gc_obj->data.arr.len;
    if (n != args[1].gc_obj->data.arr.len)
        return val_error("교차엔트로피(): 두 배열의 길이가 같아야 합니다");

    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        double p_star = arr_elem_dbl(&args[0], i);
        double p      = arr_elem_dbl(&args[1], i);
        if (p <= 0.0) p = 1e-15;   /* log(0) 방지 */
        sum -= p_star * log(p);
    }
    return val_float(sum);
}

/* ────────────────────────────────────────────────────────────────
 * 소프트맥스(배열) → 배열
 *   y_i = e^{a_i} / Σ_j e^{a_j}
 *   슬라이드 36 — 다중 분류 출력층
 * ──────────────────────────────────────────────────────────────── */
static Value builtin_softmax(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 1 || args[0].type != VAL_ARRAY)
        return val_error("소프트맥스(): 배열 인수 1개 필요");
    int n = args[0].gc_obj->data.arr.len;
    if (n == 0) return val_gc_array(interp);

    /* 수치 안정화: max 값을 빼고 계산 */
    double max_val = arr_elem_dbl(&args[0], 0);
    for (int i = 1; i < n; i++) {
        double v = arr_elem_dbl(&args[0], i);
        if (v > max_val) max_val = v;
    }

    double *exps = malloc((size_t)n * sizeof(double));
    double  sum  = 0.0;
    for (int i = 0; i < n; i++) {
        exps[i] = exp(arr_elem_dbl(&args[0], i) - max_val);
        sum += exps[i];
    }

    Value result = val_gc_array(interp);
    for (int i = 0; i < n; i++) {
        Value elem = val_float(exps[i] / sum);
        array_push(interp, result.gc_obj, elem);
        val_free(&elem);
    }
    free(exps);
    return result;
}

/* ────────────────────────────────────────────────────────────────
 * 위치인코딩(위치, 차원수) → 배열
 *   PE[pos, 2i]   = sin(pos / 10000^(2i/d))
 *   PE[pos, 2i+1] = cos(pos / 10000^(2i/d))
 *   슬라이드 37 — Transformer Positional Encoding
 * ──────────────────────────────────────────────────────────────── */
static Value builtin_positional_encoding(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 2) return val_error("위치인코딩(): 인수 2개 필요 (위치, 차원수)");
    int64_t pos = (args[0].type == VAL_INT) ? args[0].as.ival : (int64_t)args[0].as.fval;
    int64_t d   = (args[1].type == VAL_INT) ? args[1].as.ival : (int64_t)args[1].as.fval;
    if (d <= 0 || d % 2 != 0)
        return val_error("위치인코딩(): 차원수는 양의 짝수여야 합니다");

    Value result = val_gc_array(interp);
    for (int64_t i = 0; i < d / 2; i++) {
        double angle = (double)pos / pow(10000.0, (double)(2 * i) / (double)d);
        Value s = val_float(sin(angle));
        Value c = val_float(cos(angle));
        array_push(interp, result.gc_obj, s);
        array_push(interp, result.gc_obj, c);
        val_free(&s);
        val_free(&c);
    }
    return result;
}

/* ────────────────────────────────────────────────────────────────
 * 등비수열합(a, r) → 실수
 *   무한 등비급수: a / (1-r),  |r| < 1
 *   슬라이드 46
 * ──────────────────────────────────────────────────────────────── */
static Value builtin_geom_series(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 2) return val_error("등비수열합(): 인수 2개 필요 (첫째항, 공비)");
    double a = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    double r = (args[1].type == VAL_INT) ? (double)args[1].as.ival : args[1].as.fval;
    if (fabs(r) >= 1.0)
        return val_error("등비수열합(): |공비| < 1 이어야 수렴합니다");
    return val_float(a / (1.0 - r));
}

/* ────────────────────────────────────────────────────────────────
 * 등차수열합(a, d, n) → 정수/실수
 *   Sn = n/2 * (2a + (n-1)d)
 *   슬라이드 43 — 점화식 1번 (a_{n+1} - a_n = d)
 * ──────────────────────────────────────────────────────────────── */
static Value builtin_arith_series(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 3) return val_error("등차수열합(): 인수 3개 필요 (첫째항, 공차, 항수)");
    double a = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    double d = (args[1].type == VAL_INT) ? (double)args[1].as.ival : args[1].as.fval;
    int64_t n = (args[2].type == VAL_INT) ? args[2].as.ival : (int64_t)args[2].as.fval;
    if (n < 0) return val_error("등차수열합(): 항수는 0 이상이어야 합니다");
    double s = (double)n / 2.0 * (2.0 * a + (double)(n - 1) * d);
    return val_float(s);
}

/* ────────────────────────────────────────────────────────────────
 * 점화식값(a1, r, n) → 실수  (등비 점화식: a_{n+1}/a_n = r)
 *   슬라이드 43 — 점화식 2번
 *   a_n = a1 * r^(n-1)
 * ──────────────────────────────────────────────────────────────── */
static Value builtin_recur_geom(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc != 3) return val_error("점화식값(): 인수 3개 필요 (첫째항, 공비, n)");
    double  a1 = (args[0].type == VAL_INT) ? (double)args[0].as.ival : args[0].as.fval;
    double  r  = (args[1].type == VAL_INT) ? (double)args[1].as.ival : args[1].as.fval;
    int64_t n  = (args[2].type == VAL_INT) ? args[2].as.ival : (int64_t)args[2].as.fval;
    if (n < 1) return val_error("점화식값(): n은 1 이상이어야 합니다");
    return val_float(a1 * pow(r, (double)(n - 1)));
}

/* ================================================================
 *  파일 내장 함수 17종 (v5.0.0)
 *  POSIX 기반 구현 — Windows 는 _access / _mkdir 등으로 대체 필요
 * ================================================================ */
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

/* 파일 핸들을 정수(포인터 주소)로 인코딩하여 VAL_INT 로 반환 */
#define FILE_HANDLE_TO_VAL(fp) val_int((int64_t)(uintptr_t)(fp))
#define VAL_TO_FILE_HANDLE(v)  ((FILE*)(uintptr_t)((v)->as.ival))

/* 파일열기(경로, 모드) — fopen 래핑 */
static Value builtin_file_open(Interp *interp, Value *args, int argc) {
    if (argc < 2) return val_error("파일열기(): 인수 2개 필요 (경로, 모드)");
    if (args[0].type != VAL_STRING) return val_error("파일열기(): 경로는 문자열이어야 합니다");
    if (args[1].type != VAL_STRING) return val_error("파일열기(): 모드는 문자열이어야 합니다");
    const char *path = args[0].gc_obj->data.str.data;
    const char *mode_k = args[1].gc_obj->data.str.data;
    /* 한글 모드 → C 모드 변환 */
    const char *mode_c = "r";
    if (strcmp(mode_k, "\xEC\x93\xB0\xEA\xB8\xB0") == 0) mode_c = "w";      /* 쓰기 */
    else if (strcmp(mode_k, "\xEC\xB6\x94\xEA\xB0\x80") == 0) mode_c = "a"; /* 추가 */
    FILE *fp = fopen(path, mode_c);
    if (!fp) return val_error("파일열기(): '%s' 열기 실패 — %s", path, strerror(errno));
    (void)interp;
    return FILE_HANDLE_TO_VAL(fp);
}

/* 파일닫기(파일) */
static Value builtin_file_close(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_INT) return val_error("파일닫기(): 파일 핸들이 필요합니다");
    FILE *fp = VAL_TO_FILE_HANDLE(&args[0]);
    if (fp) fclose(fp);
    return val_null();
}

/* 파일읽기(파일) — 전체 내용 읽기 */
static Value builtin_file_read(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_INT) return val_error("파일읽기(): 파일 핸들이 필요합니다");
    FILE *fp = VAL_TO_FILE_HANDLE(&args[0]);
    if (!fp) return val_error("파일읽기(): 유효하지 않은 파일 핸들");
    long cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp) - cur;
    fseek(fp, cur, SEEK_SET);
    char *buf = (char*)malloc((size_t)size + 1);
    if (!buf) return val_error("파일읽기(): 메모리 부족");
    size_t rd = fread(buf, 1, (size_t)size, fp);
    buf[rd] = '\0';
    return val_gc_string_take(interp, buf);
}

/* 파일줄읽기(파일) — 한 줄 읽기, EOF면 없음 반환 */
static Value builtin_file_readline(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_INT) return val_error("파일줄읽기(): 파일 핸들이 필요합니다");
    FILE *fp = VAL_TO_FILE_HANDLE(&args[0]);
    if (!fp) return val_error("파일줄읽기(): 유효하지 않은 파일 핸들");
    char buf[4096];
    if (!fgets(buf, sizeof(buf), fp)) return val_null(); /* EOF */
    /* 줄바꿈 제거 */
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
    return val_gc_string(interp, buf);
}

/* 파일쓰기(파일, 내용) */
static Value builtin_file_write(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("파일쓰기(): 인수 2개 필요 (파일, 내용)");
    if (args[0].type != VAL_INT) return val_error("파일쓰기(): 파일 핸들이 필요합니다");
    FILE *fp = VAL_TO_FILE_HANDLE(&args[0]);
    char *s = val_to_string(&args[1]);
    fputs(s, fp);
    free(s);
    return val_null();
}

/* 파일줄쓰기(파일, 내용) — 줄바꿈 포함 */
static Value builtin_file_writeline(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("파일줄쓰기(): 인수 2개 필요 (파일, 내용)");
    if (args[0].type != VAL_INT) return val_error("파일줄쓰기(): 파일 핸들이 필요합니다");
    FILE *fp = VAL_TO_FILE_HANDLE(&args[0]);
    char *s = val_to_string(&args[1]);
    fputs(s, fp);
    fputc('\n', fp);
    free(s);
    return val_null();
}

/* 파일있음(경로) — 파일 존재 여부 */
static Value builtin_file_exists(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("파일있음(): 경로가 필요합니다");
    const char *path = args[0].gc_obj->data.str.data;
    return val_bool(access(path, F_OK) == 0);
}

/* 파일크기(경로) — 바이트 크기 */
static Value builtin_file_size(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("파일크기(): 경로가 필요합니다");
    const char *path = args[0].gc_obj->data.str.data;
    struct stat st;
    if (stat(path, &st) != 0) return val_error("파일크기(): '%s' 접근 실패", path);
    return val_int((int64_t)st.st_size);
}

/* 파일목록(폴더경로) — 배열 반환 */
static Value builtin_file_list(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("파일목록(): 경로가 필요합니다");
    const char *path = args[0].gc_obj->data.str.data;
    DIR *d = opendir(path);
    if (!d) return val_error("파일목록(): '%s' 열기 실패", path);
    Value arr = val_gc_array(interp);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        Value entry = val_gc_string(interp, ent->d_name);
        gc_array_push(interp, arr.gc_obj, entry);
        val_release(interp, &entry);
    }
    closedir(d);
    return arr;
}

/* 파일이름(경로) — basename 추출 */
static Value builtin_file_name(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("파일이름(): 경로가 필요합니다");
    const char *path = args[0].gc_obj->data.str.data;
    const char *slash = strrchr(path, '/');
    if (!slash) slash = strrchr(path, '\\');
    const char *name = slash ? slash + 1 : path;
    return val_gc_string(interp, name);
}

/* 파일확장자(경로) — 확장자 추출 (.포함) */
static Value builtin_file_ext(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("파일확장자(): 경로가 필요합니다");
    const char *path = args[0].gc_obj->data.str.data;
    const char *dot  = strrchr(path, '.');
    if (!dot) return val_gc_string(interp, "");
    return val_gc_string(interp, dot);
}

/* 폴더만들기(경로) */
static Value builtin_dir_make(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("폴더만들기(): 경로가 필요합니다");
    const char *path = args[0].gc_obj->data.str.data;
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        return val_error("폴더만들기(): '%s' 생성 실패 — %s", path, strerror(errno));
    return val_null();
}

/* 파일지우기(경로) */
static Value builtin_file_delete(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("파일지우기(): 경로가 필요합니다");
    const char *path = args[0].gc_obj->data.str.data;
    if (remove(path) != 0) return val_error("파일지우기(): '%s' 삭제 실패 — %s", path, strerror(errno));
    return val_null();
}

/* 파일복사(원본, 대상) */
static Value builtin_file_copy(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("파일복사(): 인수 2개 필요 (원본, 대상)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("파일복사(): 경로는 문자열이어야 합니다");
    const char *src = args[0].gc_obj->data.str.data;
    const char *dst = args[1].gc_obj->data.str.data;
    FILE *fs = fopen(src, "rb");
    if (!fs) return val_error("파일복사(): 원본 '%s' 열기 실패", src);
    FILE *fd = fopen(dst, "wb");
    if (!fd) { fclose(fs); return val_error("파일복사(): 대상 '%s' 열기 실패", dst); }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fs)) > 0)
        fwrite(buf, 1, n, fd);
    fclose(fs); fclose(fd);
    return val_null();
}

/* 파일이동(원본, 대상) */
static Value builtin_file_move(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("파일이동(): 인수 2개 필요 (원본, 대상)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("파일이동(): 경로는 문자열이어야 합니다");
    const char *src = args[0].gc_obj->data.str.data;
    const char *dst = args[1].gc_obj->data.str.data;
    if (rename(src, dst) != 0) return val_error("파일이동(): '%s' → '%s' 실패 — %s", src, dst, strerror(errno));
    return val_null();
}

/* 파일전체읽기(경로) — 열기+읽기+닫기 한번에 */
static Value builtin_file_readall(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("파일전체읽기(): 경로가 필요합니다");
    const char *path = args[0].gc_obj->data.str.data;
    FILE *fp = fopen(path, "r");
    if (!fp) return val_error("파일전체읽기(): '%s' 열기 실패 — %s", path, strerror(errno));
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    char *buf = (char*)malloc((size_t)size + 1);
    if (!buf) { fclose(fp); return val_error("파일전체읽기(): 메모리 부족"); }
    size_t rd = fread(buf, 1, (size_t)size, fp);
    buf[rd] = '\0';
    fclose(fp);
    return val_gc_string_take(interp, buf);
}

/* 파일전체쓰기(경로, 내용) — 열기+쓰기+닫기 한번에 */
static Value builtin_file_writeall(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("파일전체쓰기(): 인수 2개 필요 (경로, 내용)");
    if (args[0].type != VAL_STRING) return val_error("파일전체쓰기(): 경로는 문자열이어야 합니다");
    const char *path = args[0].gc_obj->data.str.data;
    FILE *fp = fopen(path, "w");
    if (!fp) return val_error("파일전체쓰기(): '%s' 열기 실패 — %s", path, strerror(errno));
    char *s = val_to_string(&args[1]);
    fputs(s, fp);
    free(s);
    fclose(fp);
    return val_null();
}

/* ================================================================
 *  글자 내장 함수 21종 (v8.0.0)
 *  1단계: 기본 글자 조작 — 자르기/분할/합치기/반복/역순
 *  2단계: 문자열 검색/비교 — 포함/위치/시작/끝/비교
 *  3단계: 글자 변환 — 대문자/소문자/제목식/대체/한번대체
 *  4단계: 공백 및 정제 — 앞공백제거/뒤공백제거/공백제거
 *  5단계: 고급 기능 — 반복확인/분석/포맷
 * ================================================================ */
#include <ctype.h>
#include <regex.h>

/* ── 1단계: 기본 글자 조작 ── */

/* 자르기(글자, 시작, 끝) — substring */
static Value builtin_str_sub(Interp *interp, Value *args, int argc) {
    if (argc < 3) return val_error("자르기(): 인수 3개 필요 (글자, 시작, 끝)");
    if (args[0].type != VAL_STRING) return val_error("자르기(): 첫 인수는 글자여야 합니다");
    if (args[1].type != VAL_INT)    return val_error("자르기(): 시작은 정수여야 합니다");
    if (args[2].type != VAL_INT)    return val_error("자르기(): 끝은 정수여야 합니다");

    const char *s   = args[0].gc_obj->data.str.data;
    int         len = (int)args[0].gc_obj->data.str.len;
    int         st  = (int)args[1].as.ival;
    int         en  = (int)args[2].as.ival;

    /* 음수 인덱스 지원 */
    if (st < 0) st = len + st;
    if (en < 0) en = len + en;
    if (st < 0) st = 0;
    if (en > len) en = len;
    if (st >= en) return val_string("");

    int sub_len = en - st;
    char *buf = (char *)malloc(sub_len + 1);
    if (!buf) return val_error("자르기(): 메모리 부족");
    memcpy(buf, s + st, sub_len);
    buf[sub_len] = '\0';
    (void)interp;
    return val_string_take(buf);
}

/* 분할(글자, 구분자) — split → 배열 반환 */
static Value builtin_str_split(Interp *interp, Value *args, int argc) {
    if (argc < 2) return val_error("분할(): 인수 2개 필요 (글자, 구분자)");
    if (args[0].type != VAL_STRING) return val_error("분할(): 첫 인수는 글자여야 합니다");
    if (args[1].type != VAL_STRING) return val_error("분할(): 구분자는 글자여야 합니다");

    const char *s   = args[0].gc_obj->data.str.data;
    const char *sep = args[1].gc_obj->data.str.data;
    size_t      sep_len = strlen(sep);

    Value arr; arr.type = VAL_ARRAY;
    arr.gc_obj = gc_new_array(&interp->gc);

    if (sep_len == 0) {
        /* 구분자가 빈 문자열이면 각 바이트로 분할 */
        for (size_t i = 0; s[i]; i++) {
            char tmp[2] = { s[i], '\0' };
            Value item = val_string(tmp);
            gc_array_push(interp, arr.gc_obj, item);
        }
    } else {
        const char *cur = s;
        const char *found;
        while ((found = strstr(cur, sep)) != NULL) {
            size_t part_len = (size_t)(found - cur);
            char *buf = (char *)malloc(part_len + 1);
            if (!buf) return val_error("분할(): 메모리 부족");
            memcpy(buf, cur, part_len);
            buf[part_len] = '\0';
            Value item = val_string_take(buf);
            gc_array_push(interp, arr.gc_obj, item);
            cur = found + sep_len;
        }
        /* 나머지 */
        Value last = val_string(cur);
        gc_array_push(interp, arr.gc_obj, last);
    }
    return arr;
}

/* 합치기(배열, 구분자) — join */
static Value builtin_str_join(Interp *interp, Value *args, int argc) {
    if (argc < 2) return val_error("합치기(): 인수 2개 필요 (배열, 구분자)");
    if (args[0].type != VAL_ARRAY)  return val_error("합치기(): 첫 인수는 배열이어야 합니다");
    if (args[1].type != VAL_STRING) return val_error("합치기(): 구분자는 글자여야 합니다");
    (void)interp;

    int         n   = (int)args[0].gc_obj->data.arr.len;
    const char *sep = args[1].gc_obj->data.str.data;
    size_t      sep_len = strlen(sep);

    /* 전체 길이 계산 */
    size_t total = 0;
    char **parts = (char **)malloc(sizeof(char *) * (size_t)(n > 0 ? n : 1));
    if (!parts) return val_error("합치기(): 메모리 부족");
    for (int i = 0; i < n; i++) {
        parts[i] = val_to_string(&args[0].gc_obj->data.arr.items[i]);
        total += strlen(parts[i]);
        if (i < n - 1) total += sep_len;
    }

    char *buf = (char *)malloc(total + 1);
    if (!buf) { for (int i=0;i<n;i++) free(parts[i]); free(parts); return val_error("합치기(): 메모리 부족"); }
    char *p = buf;
    for (int i = 0; i < n; i++) {
        size_t l = strlen(parts[i]);
        memcpy(p, parts[i], l); p += l;
        if (i < n - 1) { memcpy(p, sep, sep_len); p += sep_len; }
        free(parts[i]);
    }
    *p = '\0';
    free(parts);
    return val_string_take(buf);
}

/* 반복(글자, 횟수) — 문자열 반복 */
static Value builtin_str_repeat(Interp *interp, Value *args, int argc) {
    if (argc < 2) return val_error("반복글자(): 인수 2개 필요 (글자, 횟수)");
    if (args[0].type != VAL_STRING) return val_error("반복글자(): 첫 인수는 글자여야 합니다");
    if (args[1].type != VAL_INT)    return val_error("반복글자(): 횟수는 정수여야 합니다");
    (void)interp;

    const char *s   = args[0].gc_obj->data.str.data;
    size_t      slen = strlen(s);
    int64_t     n    = args[1].as.ival;
    if (n < 0) n = 0;

    char *buf = (char *)malloc(slen * (size_t)n + 1);
    if (!buf) return val_error("반복글자(): 메모리 부족");
    char *p = buf;
    for (int64_t i = 0; i < n; i++) { memcpy(p, s, slen); p += slen; }
    *p = '\0';
    return val_string_take(buf);
}

/* 역순(글자) — 문자열 뒤집기 (UTF-8 바이트 단위) */
static Value builtin_str_reverse(Interp *interp, Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("역순(): 글자 인수 1개 필요");
    (void)interp;

    const char *s   = args[0].gc_obj->data.str.data;
    size_t      len = strlen(s);
    char *buf = (char *)malloc(len + 1);
    if (!buf) return val_error("역순(): 메모리 부족");
    for (size_t i = 0; i < len; i++) buf[i] = s[len - 1 - i];
    buf[len] = '\0';
    return val_string_take(buf);
}

/* ── 2단계: 문자열 검색/비교 ── */

/* 포함(글자, 찾을글자) → 논리 */
static Value builtin_str_contains(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("포함(): 인수 2개 필요 (글자, 찾을글자)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("포함(): 두 인수 모두 글자여야 합니다");
    const char *s   = args[0].gc_obj->data.str.data;
    const char *sub = args[1].gc_obj->data.str.data;
    return val_bool(strstr(s, sub) != NULL);
}

/* 위치(글자, 찾을글자) → 정수 (없으면 -1) */
static Value builtin_str_indexof(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("위치(): 인수 2개 필요 (글자, 찾을글자)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("위치(): 두 인수 모두 글자여야 합니다");
    const char *s   = args[0].gc_obj->data.str.data;
    const char *sub = args[1].gc_obj->data.str.data;
    const char *p   = strstr(s, sub);
    if (!p) return val_int(-1);
    return val_int((int64_t)(p - s));
}

/* 시작(글자, 접두어) → 논리 */
static Value builtin_str_startswith(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("시작(): 인수 2개 필요 (글자, 접두어)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("시작(): 두 인수 모두 글자여야 합니다");
    const char *s   = args[0].gc_obj->data.str.data;
    const char *pre = args[1].gc_obj->data.str.data;
    size_t      pre_len = strlen(pre);
    return val_bool(strncmp(s, pre, pre_len) == 0);
}

/* 끝(글자, 접미어) → 논리 */
static Value builtin_str_endswith(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("끝확인(): 인수 2개 필요 (글자, 접미어)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("끝확인(): 두 인수 모두 글자여야 합니다");
    const char *s    = args[0].gc_obj->data.str.data;
    const char *suf  = args[1].gc_obj->data.str.data;
    size_t      slen = strlen(s);
    size_t      suflen = strlen(suf);
    if (suflen > slen) return val_bool(0);
    return val_bool(strcmp(s + slen - suflen, suf) == 0);
}

/* 비교(글자1, 글자2) → 정수 (strcmp) */
static Value builtin_str_compare(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("비교(): 인수 2개 필요 (글자1, 글자2)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("비교(): 두 인수 모두 글자여야 합니다");
    int r = strcmp(args[0].gc_obj->data.str.data, args[1].gc_obj->data.str.data);
    return val_int((int64_t)(r < 0 ? -1 : r > 0 ? 1 : 0));
}

/* ── 3단계: 글자 변환 ── */

/* 대문자(글자) */
static Value builtin_str_upper(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("대문자(): 글자 인수 1개 필요");
    const char *s = args[0].gc_obj->data.str.data;
    char *buf = strdup(s);
    if (!buf) return val_error("대문자(): 메모리 부족");
    for (int i = 0; buf[i]; i++) buf[i] = (char)toupper((unsigned char)buf[i]);
    return val_string_take(buf);
}

/* 소문자(글자) */
static Value builtin_str_lower(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("소문자(): 글자 인수 1개 필요");
    const char *s = args[0].gc_obj->data.str.data;
    char *buf = strdup(s);
    if (!buf) return val_error("소문자(): 메모리 부족");
    for (int i = 0; buf[i]; i++) buf[i] = (char)tolower((unsigned char)buf[i]);
    return val_string_take(buf);
}

/* 제목식(글자) — 첫 글자만 대문자 */
static Value builtin_str_title(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("제목식(): 글자 인수 1개 필요");
    const char *s = args[0].gc_obj->data.str.data;
    char *buf = strdup(s);
    if (!buf) return val_error("제목식(): 메모리 부족");
    int new_word = 1;
    for (int i = 0; buf[i]; i++) {
        if (isspace((unsigned char)buf[i])) { new_word = 1; }
        else if (new_word) { buf[i] = (char)toupper((unsigned char)buf[i]); new_word = 0; }
        else { buf[i] = (char)tolower((unsigned char)buf[i]); }
    }
    return val_string_take(buf);
}

/* 내부 헬퍼: 문자열 내 모든 패턴 교체 */
static char *str_replace_all(const char *s, const char *from, const char *to, int first_only) {
    if (!from || strlen(from) == 0) return strdup(s);
    size_t from_len = strlen(from);
    size_t to_len   = strlen(to);

    /* 교체 횟수 계산 */
    size_t count = 0;
    const char *cur = s;
    const char *f;
    while ((f = strstr(cur, from)) != NULL) {
        count++;
        cur = f + from_len;
        if (first_only) break;
    }
    if (count == 0) return strdup(s);

    size_t old_len = strlen(s);
    size_t new_len = old_len + count * (to_len > from_len ? to_len - from_len : 0)
                             - count * (from_len > to_len  ? from_len - to_len  : 0);
    char *buf = (char *)malloc(new_len + 1);
    if (!buf) return NULL;

    char *dst = buf;
    cur = s;
    size_t done = 0;
    while ((f = strstr(cur, from)) != NULL) {
        size_t prefix = (size_t)(f - cur);
        memcpy(dst, cur, prefix); dst += prefix;
        memcpy(dst, to, to_len); dst += to_len;
        cur = f + from_len;
        done++;
        if (first_only && done >= count) break;
    }
    size_t rest = strlen(cur);
    memcpy(dst, cur, rest); dst += rest;
    *dst = '\0';
    return buf;
}

/* 대체(글자, 찾을글자, 바꿀글자) — 모든 경우 교체 */
static Value builtin_str_replace(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 3) return val_error("대체(): 인수 3개 필요 (글자, 찾을글자, 바꿀글자)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING || args[2].type != VAL_STRING)
        return val_error("대체(): 모든 인수는 글자여야 합니다");
    const char *s    = args[0].gc_obj->data.str.data;
    const char *from = args[1].gc_obj->data.str.data;
    const char *to   = args[2].gc_obj->data.str.data;
    char *result = str_replace_all(s, from, to, 0);
    if (!result) return val_error("대체(): 메모리 부족");
    return val_string_take(result);
}

/* 한번대체(글자, 찾을글자, 바꿀글자) — 첫 경우만 교체 */
static Value builtin_str_replace_once(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 3) return val_error("한번대체(): 인수 3개 필요 (글자, 찾을글자, 바꿀글자)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING || args[2].type != VAL_STRING)
        return val_error("한번대체(): 모든 인수는 글자여야 합니다");
    const char *s    = args[0].gc_obj->data.str.data;
    const char *from = args[1].gc_obj->data.str.data;
    const char *to   = args[2].gc_obj->data.str.data;
    char *result = str_replace_all(s, from, to, 1);
    if (!result) return val_error("한번대체(): 메모리 부족");
    return val_string_take(result);
}

/* ── 4단계: 공백 및 정제 ── */

/* 앞공백제거(글자) — ltrim */
static Value builtin_str_ltrim(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("앞공백제거(): 글자 인수 1개 필요");
    const char *s = args[0].gc_obj->data.str.data;
    while (isspace((unsigned char)*s)) s++;
    return val_string(s);
}

/* 뒤공백제거(글자) — rtrim */
static Value builtin_str_rtrim(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("뒤공백제거(): 글자 인수 1개 필요");
    const char *s = args[0].gc_obj->data.str.data;
    size_t len = strlen(s);
    char *buf = strdup(s);
    if (!buf) return val_error("뒤공백제거(): 메모리 부족");
    size_t end = len;
    while (end > 0 && isspace((unsigned char)buf[end - 1])) end--;
    buf[end] = '\0';
    return val_string_take(buf);
}

/* 공백제거(글자) — trim 양쪽 */
static Value builtin_str_trim(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("공백제거(): 글자 인수 1개 필요");
    const char *s = args[0].gc_obj->data.str.data;
    while (isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    char *buf = strdup(s);
    if (!buf) return val_error("공백제거(): 메모리 부족");
    size_t end = len;
    while (end > 0 && isspace((unsigned char)buf[end - 1])) end--;
    buf[end] = '\0';
    return val_string_take(buf);
}

/* ── 5단계: 고급 기능 ── */

/* 반복확인(글자, 패턴) — POSIX 정규식 match → 논리 */
static Value builtin_str_regex(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 2) return val_error("반복확인(): 인수 2개 필요 (글자, 패턴)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("반복확인(): 두 인수 모두 글자여야 합니다");
    const char *s       = args[0].gc_obj->data.str.data;
    const char *pattern = args[1].gc_obj->data.str.data;
    regex_t reg;
    int ret = regcomp(&reg, pattern, REG_EXTENDED | REG_NOSUB);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &reg, errbuf, sizeof(errbuf));
        regfree(&reg);
        return val_error("반복확인(): 패턴 오류 — %s", errbuf);
    }
    int match = (regexec(&reg, s, 0, NULL, 0) == 0);
    regfree(&reg);
    return val_bool(match);
}

/* 분석(글자, 형식) — 서식 파싱: 형식 문자열의 %s/%d/%f 에 맞게 값 추출 → 배열 */
static Value builtin_str_parse(Interp *interp, Value *args, int argc) {
    if (argc < 2) return val_error("분석(): 인수 2개 필요 (글자, 형식)");
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("분석(): 두 인수 모두 글자여야 합니다");

    const char *s   = args[0].gc_obj->data.str.data;
    const char *fmt = args[1].gc_obj->data.str.data;

    Value arr; arr.type = VAL_ARRAY;
    arr.gc_obj = gc_new_array(&interp->gc);

    /* 형식 문자열을 순회하며 %s/%d/%f 로 sscanf 반복 적용 */
    const char *fp = fmt;
    const char *sp = s;
    while (*fp) {
        if (*fp == '%') {
            fp++;
            if (*fp == 's') {
                char tmp[4096] = {0};
                int consumed = 0;
                if (sscanf(sp, "%4095s%n", tmp, &consumed) == 1) {
                    Value item = val_string(tmp);
                    gc_array_push(interp, arr.gc_obj, item);
                    sp += consumed;
                }
            } else if (*fp == 'd') {
                long long v = 0; int consumed = 0;
                if (sscanf(sp, "%lld%n", &v, &consumed) == 1) {
                    Value item = val_int((int64_t)v);
                    gc_array_push(interp, arr.gc_obj, item);
                    sp += consumed;
                }
            } else if (*fp == 'f') {
                double v = 0.0; int consumed = 0;
                if (sscanf(sp, "%lf%n", &v, &consumed) == 1) {
                    Value item = val_float(v);
                    gc_array_push(interp, arr.gc_obj, item);
                    sp += consumed;
                }
            }
            fp++;
        } else {
            /* 형식의 리터럴 문자는 입력에서 건너뜀 */
            if (*sp == *fp) sp++;
            fp++;
        }
    }
    return arr;
}

/* 포맷(형식, 값...) — sprintf 스타일 → 글자 */
static Value builtin_str_format(Interp *interp, Value *args, int argc) {
    (void)interp;
    if (argc < 1 || args[0].type != VAL_STRING)
        return val_error("포맷(): 첫 인수는 형식 글자여야 합니다");

    const char *fmt = args[0].gc_obj->data.str.data;
    /* 출력 버퍼 (최대 64KB) */
    char *buf = (char *)malloc(65536);
    if (!buf) return val_error("포맷(): 메모리 부족");

    char *out = buf;
    size_t remaining = 65535;
    const char *fp = fmt;
    int arg_idx = 1; /* args[0] 는 형식 */

    while (*fp && remaining > 0) {
        if (*fp == '%' && *(fp + 1)) {
            fp++;
            char spec = *fp;
            if (spec == '%') {
                *out++ = '%'; remaining--;
            } else if ((spec == 'd' || spec == 'i') && arg_idx < argc) {
                int64_t v = (args[arg_idx].type == VAL_INT)
                            ? args[arg_idx].as.ival
                            : (int64_t)args[arg_idx].as.fval;
                int written = snprintf(out, remaining, "%lld", (long long)v);
                if (written > 0) { out += written; remaining -= (size_t)written; }
                arg_idx++;
            } else if (spec == 'f' && arg_idx < argc) {
                double v = (args[arg_idx].type == VAL_FLOAT)
                           ? args[arg_idx].as.fval
                           : (double)args[arg_idx].as.ival;
                int written = snprintf(out, remaining, "%f", v);
                if (written > 0) { out += written; remaining -= (size_t)written; }
                arg_idx++;
            } else if (spec == 's' && arg_idx < argc) {
                char *sv = val_to_string(&args[arg_idx]);
                size_t sv_len = strlen(sv);
                if (sv_len > remaining) sv_len = remaining;
                memcpy(out, sv, sv_len);
                out += sv_len; remaining -= sv_len;
                free(sv);
                arg_idx++;
            } else {
                *out++ = '%'; remaining--;
                *out++ = spec; remaining--;
            }
            fp++;
        } else {
            *out++ = *fp++; remaining--;
        }
    }
    *out = '\0';
    char *result = strdup(buf);
    free(buf);
    if (!result) return val_error("포맷(): 메모리 부족");
    return val_string_take(result);
}

/* ================================================================
 *  내장 함수 등록
 * ================================================================ */
static void register_builtins(Interp *interp) {
    struct { const char *name; BuiltinFn fn; } builtins[] = {
        { "출력",       builtin_print             },
        { "출력없이",   builtin_print_no_newline   },
        { "입력",       builtin_input             },
        { "길이",       builtin_len               },
        { "범위",       builtin_range             },
        { "정수",       builtin_to_int            },
        { "실수",       builtin_to_float          },
        { "글자",       builtin_to_str            },
        { "추가",       builtin_append            },
        { "제곱근",     builtin_sqrt              },
        { "절댓값",     builtin_abs               },
        { "최대",       builtin_max               },
        { "최소",       builtin_min               },
        /* ── AI / 수학 함수 (v3.5.0) ── */
        { "평균제곱오차",   builtin_mse                  },
        { "교차엔트로피",   builtin_cross_entropy         },
        { "소프트맥스",     builtin_softmax               },
        { "위치인코딩",     builtin_positional_encoding   },
        { "등비수열합",     builtin_geom_series           },
        { "등차수열합",     builtin_arith_series          },
        { "점화식값",       builtin_recur_geom            },
        /* ── 수학 기초 (v3.7.1) ── */
        { "올림",           builtin_ceil                  },
        { "내림",           builtin_floor                 },
        { "반올림",         builtin_round                 },
        { "사인",           builtin_sin                   },
        { "코사인",         builtin_cos                   },
        { "탄젠트",         builtin_tan                   },
        { "자연로그",       builtin_ln                    },
        { "로그",           builtin_log                   },
        { "지수",           builtin_exp                   },
        /* ── 통계 함수 (v3.8.0) ── */
        { "합계",           builtin_sum                   },
        { "평균",           builtin_mean                  },
        { "분산",           builtin_variance              },
        { "표준편차",       builtin_stddev                },
        { "중앙값",         builtin_median                },
        { "최빈값",         builtin_mode                  },
        { "누적합",         builtin_cumsum                },
        { "공분산",         builtin_covariance            },
        { "상관계수",       builtin_correlation           },
        { "정규화",         builtin_normalize             },
        { "표준화",         builtin_standardize           },
        { "배열정렬",       builtin_arr_sort              },
        { "배열뒤집기",     builtin_arr_reverse           },
        /* ── AI 활성함수 (v3.8.0) ── */
        { "시그모이드",     builtin_sigmoid               },
        { "렐루",           builtin_relu                  },
        { "쌍곡탄젠트",     builtin_tanh_fn               },
        /* ── 관계 심리 함수 (v3.8.0) ── */
        { "호감도",         builtin_attraction            },
        /* ── 파일 내장 함수 17종 (v5.0.0) ── */
        { "파일열기",       builtin_file_open             },
        { "파일닫기",       builtin_file_close            },
        { "파일읽기",       builtin_file_read             },
        { "파일줄읽기",     builtin_file_readline         },
        { "파일쓰기",       builtin_file_write            },
        { "파일줄쓰기",     builtin_file_writeline        },
        { "파일있음",       builtin_file_exists           },
        { "파일크기",       builtin_file_size             },
        { "파일목록",       builtin_file_list             },
        { "파일이름",       builtin_file_name             },
        { "파일확장자",     builtin_file_ext              },
        { "폴더만들기",     builtin_dir_make              },
        { "파일지우기",     builtin_file_delete           },
        { "파일복사",       builtin_file_copy             },
        { "파일이동",       builtin_file_move             },
        { "파일전체읽기",   builtin_file_readall          },
        { "파일전체쓰기",   builtin_file_writeall         },
        /* ── 글자 함수 21종 (v8.0.0) ── */
        /* 1단계: 기본 글자 조작 */
        { "자르기",         builtin_str_sub               },
        { "분할",           builtin_str_split             },
        { "합치기",         builtin_str_join              },
        { "반복글자",       builtin_str_repeat            },
        { "역순",           builtin_str_reverse           },
        /* 2단계: 문자열 검색/비교 */
        { "포함",           builtin_str_contains          },
        { "위치",           builtin_str_indexof           },
        { "시작",           builtin_str_startswith        },
        { "끝확인",         builtin_str_endswith          },
        { "비교",           builtin_str_compare           },
        /* 3단계: 글자 변환 */
        { "대문자",         builtin_str_upper             },
        { "소문자",         builtin_str_lower             },
        { "제목식",         builtin_str_title             },
        { "대체",           builtin_str_replace           },
        { "한번대체",       builtin_str_replace_once      },
        /* 4단계: 공백 및 정제 */
        { "앞공백제거",     builtin_str_ltrim             },
        { "뒤공백제거",     builtin_str_rtrim             },
        { "공백제거",       builtin_str_trim              },
        /* 5단계: 고급 기능 */
        { "반복확인",       builtin_str_regex             },
        { "분석",           builtin_str_parse             },
        { "포맷",           builtin_str_format            },
        /* v11.0.0 텐서 내장함수 12종 */
        { "모양바꾸기",     builtin_tensor_reshape        },
        { "전치",           builtin_tensor_transpose      },
        { "펼치기",         builtin_tensor_flatten        },
        { "텐서더하기",     builtin_tensor_add            },
        { "텐서빼기",       builtin_tensor_sub            },
        { "텐서곱",         builtin_tensor_mul            },
        { "텐서나눠기",     builtin_tensor_div            },
        { "행렬곱",         builtin_tensor_matmul         },
        { "합산축",         builtin_tensor_sum_axis       },
        { "평균축",         builtin_tensor_mean_axis      },
        { "텐서출력",       builtin_tensor_print          },
        { "텐서형태",       builtin_tensor_shape          },
        /* v15.0.0 autograd 내장함수 13종 */
        { "역전파",         builtin_ag_backward           },
        { "기울기초기화",   builtin_ag_zero_grad          },
        { "미분추적",       builtin_ag_track              },
        { "미분더하기",     builtin_ag_add                },
        { "미분곱",         builtin_ag_mul                },
        { "미분행렬곱",     builtin_ag_matmul             },
        { "미분렐루",       builtin_ag_relu               },
        { "미분시그모이드", builtin_ag_sigmoid            },
        { "미분쌍곡탄젠트", builtin_ag_tanh               },
        { "미분로그",       builtin_ag_log                },
        { "미분합산",       builtin_ag_sum                },
        { "미분평균",       builtin_ag_mean               },
        { "미분제곱",       builtin_ag_pow2               },

        /* 산업/임베디드 내장 함수 v16.0.0 */
        { "GPIO쓰기",       builtin_gpio_write            },
        { "GPIO읽기",       builtin_gpio_read             },
        { "I2C연결",        builtin_i2c_connect           },
        { "I2C읽기",        builtin_i2c_read              },
        { "I2C쓰기",        builtin_i2c_write             },
        { "SPI전송",        builtin_spi_send              },
        { "SPI읽기",        builtin_spi_read              },
        { "UART설정",       builtin_uart_setup            },
        { "UART전송",       builtin_uart_send             },
        { "UART읽기",       builtin_uart_read             },
        { "Modbus연결",     builtin_modbus_connect        },
        { "Modbus읽기",     builtin_modbus_read           },
        { "Modbus쓰기",     builtin_modbus_write          },
        { "Modbus연결끊기", builtin_modbus_disconnect     },
        { "CAN필터",        builtin_can_filter            },
        { "CAN전송",        builtin_can_send              },
        { "CAN읽기",        builtin_can_read              },
        { "MQTT연결",       builtin_mqtt_connect          },
        { "MQTT발행",       builtin_mqtt_publish          },
        { "MQTT구독",       builtin_mqtt_subscribe        },
        { "MQTT연결끊기",   builtin_mqtt_disconnect       },
        { "ROS2발행",       builtin_ros2_publish          },
        { "ROS2구독",       builtin_ros2_subscribe        },

        /* 안전 규격 내장 함수 v17.0.0 */
        { "페일세이프",     builtin_failsafe              },
        { "긴급정지",       builtin_emergency_stop        },
        { "경보발령",       builtin_alarm                 },

        /* 온디바이스 AI 내장 함수 v18.0.0 */
        { "AI불러오기",     builtin_ai_load               },
        { "AI추론",         builtin_ai_predict            },
        { "AI학습단계",     builtin_ai_train_step         },
        { "AI저장",         builtin_ai_save               },

        /* ── v18.1.0 신규 내장 함수 31종 ── */
        /* 수학 추가: 제곱/각도변환/난수 */
        { "제곱",           builtin_pow                   },
        { "라디안",         builtin_radians               },
        { "각도",           builtin_degrees               },
        { "난수",           builtin_random                },
        { "난정수",         builtin_rand_int              },
        /* 역삼각함수 */
        { "아크사인",       builtin_asin                  },
        { "아크코사인",     builtin_acos                  },
        { "아크탄젠트",     builtin_atan                  },
        { "아크탄젠트2",    builtin_atan2                 },
        /* 글자 추가 */
        { "좌문자",         builtin_str_left              },
        { "우문자",         builtin_str_right             },
        { "채우기",         builtin_str_pad               },
        { "코드",           builtin_str_code              },
        { "붙여씀",         builtin_str_compact           },
        /* 배열 고급 */
        { "배열삭제",       builtin_arr_remove            },
        { "배열찾기",       builtin_arr_indexof           },
        { "배열포함",       builtin_arr_contains          },
        { "배열합치기",     builtin_arr_concat            },
        { "배열자르기",     builtin_arr_slice             },
        { "유일값",         builtin_arr_unique            },
        { "배열채우기",     builtin_arr_fill              },
        /* 시간/날짜 */
        { "현재시간",       builtin_time_now              },
        { "현재날짜",       builtin_date_now              },
        { "시간포맷",       builtin_time_format           },
        { "경과시간",       builtin_elapsed               },
        /* 시스템 */
        { "환경변수",       builtin_getenv                },
        { "종료",           builtin_exit_proc             },
        { "명령실행",       builtin_shell                 },
        { "잠깐",           builtin_sleep_ms              },
        /* JSON 간이 처리 */
        { "JSON생성",       builtin_json_encode           },
        { "JSON파싱",       builtin_json_decode           },

        { NULL, NULL }
    };
    for (int i = 0; builtins[i].name; i++) {
        Value fn = val_builtin(builtins[i].fn);
        env_define(interp->global, builtins[i].name, fn, interp);
    }
    /* 상수 */
    env_define(interp->global, "참",     val_bool(1),                          interp);
    env_define(interp->global, "거짓",   val_bool(0),                          interp);
    env_define(interp->global, "없음",   val_null(),                           interp);
    env_define(interp->global, "파이",   val_float(3.14159265358979323846),    interp);  /* π */
    env_define(interp->global, "오일러", val_float(2.71828182845904523536),    interp);  /* e */
}

/* ================================================================
 *  인터프리터 초기화 / 정리
 * ================================================================ */
void interp_init(Interp *interp) {
    memset(interp, 0, sizeof(Interp));
    gc_heap_init(&interp->gc);           /* ★ GC 힙 초기화 */
    g_interp_for_gc = interp;            /* GC 루트 마킹용 전역 포인터 */
    interp->global  = env_new(NULL);
    interp->current = interp->global;
    register_builtins(interp);
    /* 계약 시스템 + 객체 시스템 — memset으로 이미 0 초기화됨 */
}

void interp_free(Interp *interp) {
    /* ★ 계약 레지스트리 해제 */
    for (int i = 0; i < interp->contract_count; i++) {
        free(interp->contracts[i].scope);
        free(interp->contracts[i].alt_name);
        /* cond / alt_node 는 AST 소유 — 해제 안 함 */
    }
    free(interp->contracts);
    interp->contracts = NULL;

    /* ★ 복원지점 해제 */
    for (int i = 0; i < interp->checkpoint_count; i++) {
        free(interp->checkpoints[i].name);
        if (interp->checkpoints[i].snapshot)
            env_free(interp->checkpoints[i].snapshot, interp);
    }
    free(interp->checkpoints);
    interp->checkpoints = NULL;

    /* ★ 클래스 레지스트리 해제 (v4.2.0) */
    for (int i = 0; i < interp->class_count; i++) {
        free(interp->classes[i].name);
        free(interp->classes[i].parent_name);
        /* node는 AST 소유 — 해제 안 함 */
    }
    free(interp->classes);
    interp->classes = NULL;

    /* ★ 인터럽트 시스템 해제 (v6.0.0) */
    for (int i = 0; i < interp->isr_handler_count; i++)
        free(interp->isr_handlers[i].name);
    for (int i = 0; i < interp->event_handler_count; i++)
        free(interp->event_handlers[i].name);

    /* ★ 온톨로지 시스템 해제 (v20.1.0) */
    if (interp->ont_local) {
        kc_ont_destroy(interp->ont_local);
        interp->ont_local = NULL;
    }
    if (interp->ont_remote) {
        kc_ont_remote_disconnect(interp->ont_remote);
        interp->ont_remote = NULL;
    }
    interp->ont_current_class = NULL;
    interp->mcp_exposed = 0;

    env_free(interp->global, interp);    /* 환경 내 모든 Value 해제 */
    interp->global  = NULL;
    interp->current = NULL;
    gc_heap_free(&interp->gc);           /* ★ GC 힙 전체 해제 */
    if (g_interp_for_gc == interp) g_interp_for_gc = NULL;
}

void interp_gc_stats(Interp *interp) {
    GcStats s = gc_get_stats(&interp->gc);
    fprintf(stderr,
        "[GC] 살아있는 오브젝트: %d  |  누적 할당: %d  |  "
        "누적 해제: %d  |  사이클 GC 실행: %d\n",
        s.obj_alive, s.total_alloc, s.total_freed, s.cycle_runs);
}

/* ================================================================
 *  온톨로지 MCP 자동 노출 — v20.1.0
 * ================================================================ */

/* MCP 도구 핸들러 — 온톨로지 한글 질의 */
static KcJson *ont_mcp_query_handler(KcJson *params, void *userdata) {
    KcOntology *onto = (KcOntology *)userdata;
    if (!onto) return kc_json_string("{}");
    KcJson *q_json = kc_json_object_get(params, "질의");
    const char *q_str = (q_json && q_json->type == KC_JSON_STRING)
                        ? q_json->as.sval : "";
    KcOntQueryResult *qr = kc_ont_query(onto, q_str);
    char *json_str = qr ? kc_ont_qr_to_json(qr) : NULL;
    if (qr) kc_ont_qr_destroy(qr);
    KcJson *result = kc_json_string(json_str ? json_str : "{}");
    free(json_str);
    return result;
}

/* MCP 도구 핸들러 — 온톨로지 추론 실행 */
static KcJson *ont_mcp_infer_handler(KcJson *params, void *userdata) {
    KcOntology *onto = (KcOntology *)userdata;
    if (!onto) return kc_json_string("온톨로지 없음");
    int n = kc_ont_reason(onto);
    char buf[128];
    snprintf(buf, sizeof(buf), "추론 완료: %d 사실 도출", n);
    return kc_json_string(buf);
}

/* MCP 자원 핸들러 — 온톨로지 전체 JSON 내보내기 */
static KcJson *ont_mcp_full_handler(KcJson *params, void *userdata) {
    KcOntology *onto = (KcOntology *)userdata;
    if (!onto) return kc_json_string("{}");
    char *json_str = kc_ont_to_json(onto);
    KcJson *result = kc_json_string(json_str ? json_str : "{}");
    free(json_str);
    return result;
}

/* MCP 자동 노출 — 내장/대여 온톨로지를 MCP 서버에 도구+자원 등록 */
static void kc_interp_ont_expose_mcp(Interp *interp) {
    if (!interp->ont_local) return;

    /* ── 서버 초기화 ─────────────────────────────────── */
    kc_mcp_server_init(&interp->mcp_srv,
        "kcode-ontology-mcp", "1.0.0",
        "Kcode 온톨로지 MCP 자동 노출");

    /* ── 도구 1: 질의 ─────────────────────────────────── */
    KcMcpTool query_tool;
    memset(&query_tool, 0, sizeof(query_tool));
    query_tool.name        = "온톨로지_질의";
    query_tool.description = "한글 질의를 실행하고 JSON 결과 반환";
    query_tool.params[0].name        = "질의";
    query_tool.params[0].type        = KC_MCP_PARAM_STRING;
    query_tool.params[0].required    = 1;
    query_tool.params[0].description = "한글 질의 문자열";
    query_tool.param_count = 1;
    query_tool.handler  = ont_mcp_query_handler;
    query_tool.userdata = interp->ont_local;
    kc_mcp_server_add_tool(&interp->mcp_srv, query_tool);

    /* ── 도구 2: 추론 ─────────────────────────────────── */
    KcMcpTool infer_tool;
    memset(&infer_tool, 0, sizeof(infer_tool));
    infer_tool.name        = "온톨로지_추론";
    infer_tool.description = "온톨로지 전방향체이닝 추론 실행";
    infer_tool.param_count = 0;
    infer_tool.handler  = ont_mcp_infer_handler;
    infer_tool.userdata = interp->ont_local;
    kc_mcp_server_add_tool(&interp->mcp_srv, infer_tool);

    /* ── 자원: 전체 온톨로지 JSON ───────────────────────── */
    KcMcpResource ont_res;
    memset(&ont_res, 0, sizeof(ont_res));
    ont_res.name        = "온톨로지_전체";
    ont_res.uri         = "kcode://ontology/full";
    ont_res.mime_type   = "application/json";
    ont_res.description = "전체 온톨로지 클래스/관계/인스턴스 JSON";
    ont_res.handler  = ont_mcp_full_handler;
    ont_res.userdata = interp->ont_local;
    kc_mcp_server_add_resource(&interp->mcp_srv, ont_res);

    printf("[온톨로지 MCP] 자동 노출 완료 — 도구 2종(질의/추론), 자원 1종(전체JSON) 등록\n");
}

/* ================================================================
 *  계약 시스템 구현 — v4.0.0
 * ================================================================ */

/* 전방 선언 (apply_sanction 에서 대체 함수 호출 시 필요) */
static Value call_function(Interp *interp, Value *callee, Value *args, int argc);

/* ── 전역 Env deep-copy (복원지점 스냅샷) ── */
static Env *env_snapshot(Env *src, Interp *interp) {
    Env *snap = env_new(NULL);
    for (int i = 0; i < ENV_BUCKET_SIZE; i++) {
        EnvEntry *en = src->buckets[i];
        while (en) {
            Value c = val_clone(interp, &en->val);
            env_define(snap, en->name, c, interp);
            val_release(interp, &c);
            en = en->next;
        }
    }
    return snap;
}

/* ── 복원지점으로 전역 Env 복원 ── */
static void env_restore_from(Env *dst, Env *snap, Interp *interp) {
    for (int i = 0; i < ENV_BUCKET_SIZE; i++) {
        EnvEntry *en = dst->buckets[i];
        while (en) {
            EnvEntry *nx = en->next;
            val_release(interp, &en->val);
            free(en->name);
            free(en);
            en = nx;
        }
        dst->buckets[i] = NULL;
    }
    for (int i = 0; i < ENV_BUCKET_SIZE; i++) {
        EnvEntry *en = snap->buckets[i];
        while (en) {
            Value c = val_clone(interp, &en->val);
            env_define(dst, en->name, c, interp);
            val_release(interp, &c);
            en = en->next;
        }
    }
}

/* ================================================================
 *  객체 시스템 (v4.2.0)
 * ================================================================ */

/* 클래스 레지스트리에 추가 */
static void class_register(Interp *interp, ClassDef cd) {
    if (interp->class_count >= interp->class_cap) {
        interp->class_cap = interp->class_cap ? interp->class_cap * 2 : 8;
        interp->classes = (ClassDef*)realloc(
            interp->classes, sizeof(ClassDef) * interp->class_cap);
    }
    interp->classes[interp->class_count++] = cd;
}

/* 이름으로 ClassDef 검색 */
static ClassDef *class_find(Interp *interp, const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < interp->class_count; i++) {
        if (strcmp(interp->classes[i].name, name) == 0)
            return &interp->classes[i];
    }
    return NULL;
}

/* 인스턴스 fields Env에서 멤버 검색 (상속 체인 포함) */
Value *instance_get_member(GcObject *inst_obj, const char *name) {
    if (!inst_obj || inst_obj->type != GC_INSTANCE) return NULL;
    /* 현재 인스턴스 fields 먼저 탐색 */
    Value *v = env_get(inst_obj->data.instance.fields, name);
    if (v) return v;
    /* 부모 인스턴스(proto) 탐색 */
    if (inst_obj->data.instance.proto)
        return instance_get_member(inst_obj->data.instance.proto, name);
    return NULL;
}

/* 인스턴스 fields Env에 멤버 설정 (현재 인스턴스에만, 상속 체인 탐색 후 없으면 정의) */
void instance_set_member(Interp *interp, GcObject *inst_obj,
                          const char *name, Value val) {
    if (!inst_obj || inst_obj->type != GC_INSTANCE) return;
    /* 기존 멤버가 있으면 set, 없으면 define */
    if (env_get(inst_obj->data.instance.fields, name)) {
        env_set(inst_obj->data.instance.fields, name, val, interp);
    } else {
        env_define(inst_obj->data.instance.fields, name, val, interp);
    }
}

/* 클래스 AST 블록에서 필드 기본값 초기화 + 메서드 등록 */
static void class_init_fields(Interp *interp, Node *class_body,
                               Env *fields, Value self_val) {
    if (!class_body) return;
    /* 블록 내 각 구문 처리 */
    Node *block = (class_body->type == NODE_BLOCK) ? class_body : NULL;
    int   count = block ? block->child_count : 0;
    Node **stmts = block ? block->children : NULL;

    for (int i = 0; i < count; i++) {
        Node *s = stmts[i];
        if (!s) continue;

        if (s->type == NODE_VAR_DECL || s->type == NODE_CONST_DECL) {
            /* 필드 선언 — 기본값 평가 */
            Value def = (s->child_count > 0)
                      ? interp_eval(interp, s->children[0])
                      : val_null();
            env_define(fields, s->sval, def, interp);
            val_release(interp, &def);
        } else if (s->type == NODE_FUNC_DECL || s->type == NODE_VOID_DECL) {
            /* 메서드 등록 — 클로저 환경에 self(자신) 포함 */
            Env *method_env = env_new(interp->current);
            /* 자신(self) 을 메서드 클로저 환경에 바인딩 */
            Value sv = val_clone(interp, &self_val);
            env_define(method_env,
                "\xEC\x9E\x90\xEC\x8B\xA0", /* 자신 */
                sv, interp);
            val_release(interp, &sv);
            Value fn = val_gc_func(interp, s, method_env);
            env_define(fields, s->sval, fn, interp);
            val_release(interp, &fn);
            env_free(method_env, interp);
        }
    }
}

/* 새 인스턴스 생성 + 필드 초기화 + 생성자 호출 */
static Value instance_new(Interp *interp, ClassDef *cd,
                           Value *args, int argc) {
    /* fields Env 생성 (부모 없음 — 인스턴스 자체 네임스페이스) */
    Env *fields = env_new(NULL);

    /* VAL_OBJ 값 먼저 생성 */
    Value self_val = val_gc_instance(interp, cd->name, fields);

    /* 상속: 부모 인스턴스 먼저 초기화 */
    if (cd->parent_name) {
        ClassDef *parent = class_find(interp, cd->parent_name);
        if (parent) {
            Value parent_inst = instance_new(interp, parent, NULL, 0);
            if (parent_inst.type == VAL_OBJ && parent_inst.gc_obj) {
                self_val.gc_obj->data.instance.proto = parent_inst.gc_obj;
                gc_retain(parent_inst.gc_obj);
            }
            val_release(interp, &parent_inst);
        }
    }

    /* 클래스 블록에서 필드/메서드 초기화 */
    Node *body = cd->node->children[cd->node->child_count - 1];
    class_init_fields(interp, body, fields, self_val);

    /* 생성자(생성) 호출 */
    /* 생성 = UTF-8: \xEC\x83\x9D\xEC\x84\xB1 */
    Value *ctor = env_get(fields, "\xEC\x83\x9D\xEC\x84\xB1");
    if (ctor && ctor->type == VAL_FUNC) {
        /* 첫 인수로 자신(self) 자동 삽입 */
        int total = argc + 1;
        Value *call_args = (Value*)malloc(sizeof(Value) * total);
        call_args[0] = val_clone(interp, &self_val);
        for (int i = 0; i < argc; i++)
            call_args[i + 1] = val_clone(interp, &args[i]);

        Value r = call_function(interp, ctor, call_args, total);
        for (int i = 0; i < total; i++) val_release(interp, &call_args[i]);
        free(call_args);
        val_release(interp, &r);
    }

    return self_val;
}

/* ── 계약 레지스트리에 항목 추가 ── */
static void contract_register(Interp *interp, ContractEntry ce) {
    if (interp->contract_count >= interp->contract_cap) {
        interp->contract_cap = interp->contract_cap ? interp->contract_cap * 2 : 8;
        interp->contracts = (ContractEntry*)realloc(
            interp->contracts,
            sizeof(ContractEntry) * (size_t)interp->contract_cap);
    }
    interp->contracts[interp->contract_count++] = ce;
}

/* ── 복원지점 등록 (같은 이름이면 스냅샷 갱신) ── */
static void checkpoint_register(Interp *interp, const char *name) {
    for (int i = 0; i < interp->checkpoint_count; i++) {
        if (strcmp(interp->checkpoints[i].name, name) == 0) {
            if (interp->checkpoints[i].snapshot)
                env_free(interp->checkpoints[i].snapshot, interp);
            interp->checkpoints[i].snapshot = env_snapshot(interp->global, interp);
            return;
        }
    }
    if (interp->checkpoint_count >= interp->checkpoint_cap) {
        interp->checkpoint_cap = interp->checkpoint_cap ? interp->checkpoint_cap * 2 : 4;
        interp->checkpoints = (CheckpointEntry*)realloc(
            interp->checkpoints,
            sizeof(CheckpointEntry) * (size_t)interp->checkpoint_cap);
    }
    CheckpointEntry cp;
    cp.name     = strdup(name);
    cp.snapshot = env_snapshot(interp->global, interp);
    interp->checkpoints[interp->checkpoint_count++] = cp;
}

/* ── 복원지점 스냅샷 검색 ── */
static Env *checkpoint_find(Interp *interp, const char *name) {
    for (int i = 0; i < interp->checkpoint_count; i++)
        if (strcmp(interp->checkpoints[i].name, name) == 0)
            return interp->checkpoints[i].snapshot;
    return NULL;
}

/* ── 제재 적용 ──
 * 반환: val_null() → 계속 실행
 *       VAL_ERROR  → 실패시 블록으로 전달
 *       기타 Value → 대체 반환값으로 함수 결과 교체
 */
static Value apply_sanction(Interp *interp, ContractEntry *ce,
                             const char *fn_name) {
    char msg[512];
    switch (ce->sanction) {

        case TOK_KW_GYEONGGO: /* 경고 — stderr 출력 후 계속 */
            fprintf(stderr,
                "[%s 경고] 함수 '%s' 계약 위반 (범위: %s)\n",
                ce->kind == TOK_KW_BEOPRYEONG ? "법령" : "법위반",
                fn_name ? fn_name : "?", ce->scope);
            return val_null();

        case TOK_KW_BOGO: {  /* 보고 — 로그 파일 기록 후 계속 */
            FILE *lf = fopen("kcode_contract.log", "a");
            if (lf) {
                fprintf(lf, "[보고] 함수 '%s' 계약 위반 (범위: %s)\n",
                        fn_name ? fn_name : "?", ce->scope);
                fclose(lf);
            }
            return val_null();
        }

        case TOK_KW_JUNGDAN: /* 중단 — 오류 신호 발생 → 실패시 블록 */
            snprintf(msg, sizeof(msg),
                "[런타임 오류] 계약 위반으로 중단 — 함수 '%s' (범위: %s)",
                fn_name ? fn_name : "?", ce->scope);
            return val_error("%s", msg);

        case TOK_KW_HOEGWI: { /* 회귀 — 복원지점으로 상태 복원 후 중단 */
            const char *cp_name = ce->alt_name ? ce->alt_name : "";
            Env *snap = checkpoint_find(interp, cp_name);
            if (snap)
                env_restore_from(interp->global, snap, interp);
            snprintf(msg, sizeof(msg),
                "[런타임 오류] 계약 위반 — 함수 '%s' 복원지점 '%s'%s 회귀",
                fn_name ? fn_name : "?", cp_name,
                snap ? " 으로" : " 없음,");
            return val_error("%s", msg);
        }

        case TOK_KW_DAECHE: { /* 대체 — 대체값 or 대체함수 실행 */
            if (ce->alt_node)
                return interp_eval(interp, ce->alt_node);
            if (ce->alt_name) {
                Value *fn_val = env_get(interp->global, ce->alt_name);
                if (fn_val && fn_val->type == VAL_FUNC)
                    return call_function(interp, fn_val, NULL, 0);
                snprintf(msg, sizeof(msg),
                    "[런타임 오류] 대체 함수 '%s' 를 찾을 수 없습니다",
                    ce->alt_name);
                return val_error("%s", msg);
            }
            return val_null();
        }

        default:
            return val_null();
    }
}

/* ── 계약 검사 실행 ──
 * kind    : TOK_KW_BEOPRYEONG(법령) | TOK_KW_BEOPWIBAN(법위반)
 * fn_name : 현재 실행 함수 이름 (NULL = 람다)
 * 반환    : val_null() = 통과, 그 외 = 제재 결과
 */
static Value check_contracts(Interp *interp, TokenType kind,
                              const char *fn_name) {
    static const char GLOBAL_UTF8[] =
        "\xEC\xA0\x84\xEC\x97\xAD"; /* "전역" */
    for (int i = 0; i < interp->contract_count; i++) {
        ContractEntry *ce = &interp->contracts[i];
        if (ce->kind != kind) continue;
        /* 범위 일치 */
        if (strcmp(ce->scope, GLOBAL_UTF8) != 0 &&
            !(fn_name && strcmp(ce->scope, fn_name) == 0)) continue;
        /* 조건 평가 */
        if (!ce->cond) continue;
        Value cond_val = interp_eval(interp, ce->cond);
        int passed = val_is_truthy(&cond_val);
        val_release(interp, &cond_val);
        if (!passed) {
            Value r = apply_sanction(interp, ce, fn_name);
            return r;
        }
    }
    return val_null();
}

/* ================================================================
 *  계층 계약 우선순위 평가 (v5.0.0)
 *  헌법 → 법률 → 규정 → 법령 순으로 평가
 *  상위 계층에서 중단 제재 발생 시 하위 계층은 평가하지 않음
 * ================================================================ */
static Value check_contracts_layered(Interp *interp, const char *fn_name,
                                      const char *class_name) {
    /* 계층별 kind 순서 */
    TokenType layers[] = {
        TOK_KW_HEONBEOB,   /* 헌법  — 전역 최상위 */
        TOK_KW_BEOMNYUL,   /* 법률  — 파일 단위   */
        TOK_KW_GYUJEONG,   /* 규정  — 객체 단위   */
        TOK_KW_BEOPRYEONG, /* 법령  — 함수 사전조건 */
    };
    int layer_count = (int)(sizeof(layers) / sizeof(layers[0]));

    for (int li = 0; li < layer_count; li++) {
        TokenType kind = layers[li];
        for (int i = 0; i < interp->contract_count; i++) {
            ContractEntry *ce = &interp->contracts[i];
            if (ce->kind != kind) continue;
            /* 범위 필터 */
            if (kind == TOK_KW_GYUJEONG) {
                /* 규정: class_name 일치 여부 확인 */
                if (!class_name || !ce->scope) continue;
                if (strcmp(ce->scope, class_name) != 0) continue;
            } else if (kind == TOK_KW_BEOPRYEONG) {
                /* 법령: 함수명 일치 (전역 항목은 check_contracts 가 담당) */
                if (!fn_name || !ce->scope) continue;
                if (strcmp(ce->scope, fn_name) != 0) continue;
            }
            /* 조건 평가 */
            if (!ce->cond) continue;
            Value cond_val = interp_eval(interp, ce->cond);
            int passed = val_is_truthy(&cond_val);
            val_release(interp, &cond_val);
            if (!passed) {
                Value r = apply_sanction(interp, ce, fn_name);
                if (r.type != VAL_NULL) return r; /* 중단/회귀/대체 */
            }
        }
    }
    return val_null();
}

/* ================================================================
 *  함수 호출 처리
 * ================================================================ */


/* ================================================================
 *  멤버 접근 — 내장 속성/메서드
 * ================================================================ */
static Value eval_member(Interp *interp, Value *obj, const char *member) {
    if (obj->type == VAL_ARRAY) {
        if (strcmp(member, "길이") == 0) return val_int(obj->gc_obj->data.arr.len);
    }
    if (obj->type == VAL_STRING) {
        if (strcmp(member, "길이") == 0) {
            const char *s = obj->gc_obj ? obj->gc_obj->data.str.data : obj->as.sval;
            return val_int((int64_t)strlen(s ? s : ""));
        }
    }
    /* ★ VAL_OBJ — 인스턴스 멤버 접근 (v4.2.0) */
    if (obj->type == VAL_OBJ && obj->gc_obj) {
        Value *found = instance_get_member(obj->gc_obj, member);
        if (found) return val_clone(interp, found);
        RT_ERROR(interp, "객체 '%s'에 멤버 '%s'이(가) 없습니다",
                 obj->gc_obj->data.instance.class_name
                     ? obj->gc_obj->data.instance.class_name : "?",
                 member);
    }
    RT_ERROR(interp, "'%s' 속성을 찾을 수 없습니다", member);
}

/* ================================================================
 *  이항 연산 평가
 * ================================================================ */
static Value eval_binary(Interp *interp, TokenType op,
                          Value left, Value right) {
    /* 문자열 + */
    if (op == TOK_PLUS && (left.type == VAL_STRING || right.type == VAL_STRING)) {
        char *ls = val_to_string(&left);
        char *rs = val_to_string(&right);
        size_t len = strlen(ls) + strlen(rs) + 1;
        char *buf = (char*)malloc(len);
        strcpy(buf, ls); strcat(buf, rs);
        free(ls); free(rs);
        return val_gc_string_take(interp, buf);
    }

    /* 배열 + */
    if (op == TOK_PLUS && left.type == VAL_ARRAY && right.type == VAL_ARRAY) {
        Value arr = val_clone(interp, &left);
        for (int i = 0; i < right.gc_obj->data.arr.len; i++)
            array_push(interp, arr.gc_obj, right.gc_obj->data.arr.items[i]);
        return arr;
    }

    /* 숫자 연산 */
    int left_num  = (left.type  == VAL_INT || left.type  == VAL_FLOAT);
    int right_num = (right.type == VAL_INT || right.type == VAL_FLOAT);

    if (left_num && right_num) {
        int use_float = (left.type == VAL_FLOAT || right.type == VAL_FLOAT);
        double lf = (left.type  == VAL_INT) ? (double)left.as.ival  : left.as.fval;
        double rf = (right.type == VAL_INT) ? (double)right.as.ival : right.as.fval;
        int64_t li = left.as.ival, ri = right.as.ival;

        switch (op) {
            case TOK_PLUS:     return use_float ? val_float(lf+rf) : val_int(li+ri);
            case TOK_MINUS:    return use_float ? val_float(lf-rf) : val_int(li-ri);
            case TOK_STAR:     return use_float ? val_float(lf*rf) : val_int(li*ri);
            case TOK_SLASH:
                if (!use_float && ri == 0) RT_ERROR(interp, "0으로 나눌 수 없습니다");
                if (!use_float && rf != 0) return val_int(li / ri);
                if (rf == 0.0) RT_ERROR(interp, "0으로 나눌 수 없습니다");
                return val_float(lf / rf);
            case TOK_PERCENT:
                if (ri == 0) RT_ERROR(interp, "0으로 나눌 수 없습니다");
                if (!use_float) return val_int(li % ri);
                return val_float(fmod(lf, rf));
            case TOK_STARSTAR: return use_float ? val_float(pow(lf,rf))
                                                : val_int((int64_t)pow(lf,rf));
            /* 비교 */
            case TOK_EQEQ:  return val_bool(lf == rf);
            case TOK_BANGEQ: return val_bool(lf != rf);
            case TOK_GT:    return val_bool(lf > rf);
            case TOK_LT:    return val_bool(lf < rf);
            case TOK_GTEQ:  return val_bool(lf >= rf);
            case TOK_LTEQ:  return val_bool(lf <= rf);
            /* 비트 (정수만) */
            case TOK_AMP:    return val_int(li & ri);
            case TOK_PIPE:   return val_int(li | ri);
            case TOK_CARET:  return val_int(li ^ ri);
            case TOK_LTLT:   return val_int(li << ri);
            case TOK_GTGT:   return val_int(li >> ri);
            default: break;
        }
    }

    /* 논리 연산 */
    if (op == TOK_KW_AND) return val_bool(val_is_truthy(&left) && val_is_truthy(&right));
    if (op == TOK_KW_OR)  return val_bool(val_is_truthy(&left) || val_is_truthy(&right));

    /* 동등/비교 (일반) */
    if (op == TOK_EQEQ)  return val_bool(val_equal(&left, &right));
    if (op == TOK_BANGEQ) return val_bool(!val_equal(&left, &right));

    /* 문자열 비교 */
    if (left.type == VAL_STRING && right.type == VAL_STRING) {
        int cmp = strcmp(STR_GET(&left), STR_GET(&right));
        switch (op) {
            case TOK_GT:   return val_bool(cmp > 0);
            case TOK_LT:   return val_bool(cmp < 0);
            case TOK_GTEQ: return val_bool(cmp >= 0);
            case TOK_LTEQ: return val_bool(cmp <= 0);
            default: break;
        }
    }

    RT_ERROR(interp, "지원하지 않는 연산입니다");
}

/* ================================================================
 *  대입 연산자 처리
 * ================================================================ */
static int64_t apply_assign_op(TokenType op, int64_t old_i, int64_t rhs_i) {
    switch(op) {
        case TOK_PLUSEQ:    return old_i + rhs_i;
        case TOK_MINUSEQ:   return old_i - rhs_i;
        case TOK_STAREQ:    return old_i * rhs_i;
        case TOK_SLASHEQ:   return rhs_i ? old_i / rhs_i : 0;
        case TOK_PERCENTEQ: return rhs_i ? old_i % rhs_i : 0;
        default:            return rhs_i;
    }
}

/* ================================================================
 *  interp_eval  — 핵심 AST 순회 함수
 * ================================================================ */
Value interp_eval(Interp *interp, Node *node) {
    if (!node) return val_null();
    if (interp->had_error) return val_error(interp->error_msg);

    switch (node->type) {

    /* ── 리터럴 ─────────────────────────────────────────────── */
    case NODE_INT_LIT:    return val_int(node->val.ival);
    case NODE_FLOAT_LIT:  return val_float(node->val.fval);
    case NODE_BOOL_LIT:   return val_bool(node->val.bval);
    case NODE_NULL_LIT:   return val_null();
    case NODE_CHAR_LIT:   return val_char((uint32_t)node->val.ival);
    case NODE_STRING_LIT: return val_gc_string(interp, node->sval ? node->sval : "");

    /* ── 식별자 ─────────────────────────────────────────────── */
    case NODE_IDENT: {
        Value *v = env_get(interp->current, node->sval);
        if (!v) RT_ERROR(interp, "정의되지 않은 변수: '%s'", node->sval);
        return val_clone(interp, v);
    }

    /* ── 배열 리터럴 ────────────────────────────────────────── */
    /* 텐서 리터럴: 텐서/영텐서/일텐서/무작위텐서  (v11.0.0) */
    case NODE_TENSOR_LIT: {
        int64_t shape[32];
        int ndim = 0;
        KcTensor *out = NULL;
        TokenType tkind = node->op;
        if (tkind == TOK_KW_TENSOR) {
            /* 텐서(data_arr, shape_arr) */
            if (node->child_count < 2) return val_error("텐서: 인수 2개 필요");
            Value data_val = interp_eval(interp, node->children[0]);
            PROPAGATE(data_val);
            Value shape_val = interp_eval(interp, node->children[1]);
            if (shape_val.type == VAL_ERROR || val_is_signal(&shape_val)) { val_free(&data_val); return shape_val; }
            ndim = extract_shape(&shape_val, shape);
            if (ndim < 1) { val_free(&data_val); val_free(&shape_val); return val_error("텐서: 형태 배열이 잘못됨"); }
            /* data 배열에서 double 추출 */
            int64_t numel = 1;
            for (int i = 0; i < ndim; i++) numel *= shape[i];
            double *buf = (double*)calloc((size_t)numel, sizeof(double));
            if (buf && data_val.type == VAL_ARRAY && data_val.gc_obj) {
                int cnt = data_val.gc_obj->data.arr.len;
                if (cnt > (int)numel) cnt = (int)numel;
                for (int i = 0; i < cnt; i++) {
                    Value *it = &data_val.gc_obj->data.arr.items[i];
                    buf[i] = (it->type == VAL_INT) ? (double)it->as.ival : it->as.fval;
                }
            }
            out = kc_tensor_create(buf, shape, ndim);
            free(buf);
            val_free(&data_val); val_free(&shape_val);
        } else {
            /* 영텐서/일텐서/무작위텐서(shape_arr) */
            if (node->child_count < 1) return val_error("텐서: 형태 인수 필요");
            Value shape_val = interp_eval(interp, node->children[0]);
            PROPAGATE(shape_val);
            ndim = extract_shape(&shape_val, shape);
            val_free(&shape_val);
            if (ndim < 1) return val_error("텐서: 형태 배열이 잘못됨");
            if (tkind == TOK_KW_ZERO_TENSOR)      out = kc_tensor_zeros(shape, ndim);
            else if (tkind == TOK_KW_ONE_TENSOR)  out = kc_tensor_ones(shape, ndim);
            else                                  out = kc_tensor_rand(shape, ndim);
        }
        if (!out) return val_error("텐서 생성 실패");
        return val_tensor(out);
    }

    case NODE_ARRAY_LIT: {
        Value arr = val_gc_array(interp);
        for (int i = 0; i < node->child_count; i++) {
            Value item = interp_eval(interp, node->children[i]);
            PROPAGATE(item);
            array_push(interp, arr.gc_obj, item);
            val_free(&item);
        }
        return arr;
    }

    /* ── 이항 연산 ──────────────────────────────────────────── */
    case NODE_BINARY: {
        Value left  = interp_eval(interp, node->children[0]);
        PROPAGATE(left);
        Value right = interp_eval(interp, node->children[1]);
        if (right.type == VAL_ERROR || val_is_signal(&right)) {
            val_free(&left);
            return right;
        }
        Value result = eval_binary(interp, node->op, left, right);
        val_free(&left);
        val_free(&right);
        return result;
    }

    /* ── 단항 연산 ──────────────────────────────────────────── */
    case NODE_UNARY: {
        Value operand = interp_eval(interp, node->children[0]);
        PROPAGATE(operand);
        if (node->op == TOK_MINUS) {
            if (operand.type == VAL_INT)   return val_int(-operand.as.ival);
            if (operand.type == VAL_FLOAT) return val_float(-operand.as.fval);
            RT_ERROR(interp, "단항 '-' 는 숫자에만 적용됩니다");
        }
        if (node->op == TOK_KW_NOT || node->op == TOK_TILDE) {
            if (node->op == TOK_TILDE && operand.type == VAL_INT)
                return val_int(~operand.as.ival);
            return val_bool(!val_is_truthy(&operand));
        }
        RT_ERROR(interp, "지원하지 않는 단항 연산자입니다");
    }

    /* ── 대입 ───────────────────────────────────────────────── */
    case NODE_ASSIGN: {
        Value rhs = interp_eval(interp, node->children[1]);
        PROPAGATE(rhs);

        Node *lhs = node->children[0];

        /* 단순 변수 대입 */
        if (lhs->type == NODE_IDENT) {
            if (node->op != TOK_EQ) {
                Value *old = env_get(interp->current, lhs->sval);
                if (!old) RT_ERROR(interp, "정의되지 않은 변수: '%s'", lhs->sval);
                /* 복합 대입 */
                if (old->type == VAL_INT && rhs.type == VAL_INT) {
                    Value newval = val_int(apply_assign_op(node->op, old->as.ival, rhs.as.ival));
                    env_set(interp->current, lhs->sval, newval, interp);
                    return newval;
                }
            }
            if (!env_set(interp->current, lhs->sval, rhs, interp)) {
                /* 없으면 현재 스코프에 정의 */
                env_define(interp->current, lhs->sval, rhs, interp);
            }
            return val_clone(interp, &rhs);
        }

        /* 인덱스 대입  arr[i] = v */
        if (lhs->type == NODE_INDEX) {
            Value arr_val = interp_eval(interp, lhs->children[0]);
            PROPAGATE(arr_val);
            Value idx_val = interp_eval(interp, lhs->children[1]);
            PROPAGATE(idx_val);
            if (arr_val.type != VAL_ARRAY) RT_ERROR(interp, "배열이 아닙니다");
            if (idx_val.type != VAL_INT)   RT_ERROR(interp, "인덱스는 정수여야 합니다");
            int64_t idx = idx_val.as.ival;
            if (idx < 0 || idx >= arr_val.gc_obj->data.arr.len)
                RT_ERROR(interp, "인덱스 범위 초과: %lld", (long long)idx);

            /* 배열은 환경에 저장된 원본을 직접 수정 */
            if (lhs->children[0]->type == NODE_IDENT) {
                Value *stored = env_get(interp->current, lhs->children[0]->sval);
                if (stored && stored->type == VAL_ARRAY) {
                    val_release(interp, &stored->gc_obj->data.arr.items[idx]);
                    stored->gc_obj->data.arr.items[idx] = val_clone(interp, &rhs);
                }
            }
            val_free(&arr_val);
            val_free(&idx_val);
            return val_clone(interp, &rhs);
        }

        /* 멤버 대입  obj.field = v  (자신.이름 = 값 포함) — v4.2.0 */
        if (lhs->type == NODE_MEMBER) {
            Value obj_val = interp_eval(interp, lhs->children[0]);
            PROPAGATE(obj_val);
            if (obj_val.type != VAL_OBJ || !obj_val.gc_obj)
                RT_ERROR(interp, "멤버 대입 대상이 객체가 아닙니다");
            Value rv = val_clone(interp, &rhs);
            instance_set_member(interp, obj_val.gc_obj, lhs->sval, rv);
            val_release(interp, &rv);
            val_free(&obj_val);
            return val_clone(interp, &rhs);
        }

        RT_ERROR(interp, "대입 대상이 올바르지 않습니다");
    }

    /* ── 변수/상수 선언 ─────────────────────────────────────── */
    case NODE_VAR_DECL:
    case NODE_CONST_DECL: {
        Value init_val = val_null();
        if (node->child_count > 0) {
            init_val = interp_eval(interp, node->children[0]);
            PROPAGATE(init_val);
        }
        env_define(interp->current, node->sval, init_val, interp);
        val_free(&init_val);
        return val_null();
    }

    /* ── 표현식 구문 ────────────────────────────────────────── */
    case NODE_EXPR_STMT: {
        Value v = interp_eval(interp, node->children[0]);
        PROPAGATE(v);
        val_free(&v);
        return val_null();
    }

    /* ── 블록 ───────────────────────────────────────────────── */
    case NODE_BLOCK: {
        for (int i = 0; i < node->child_count; i++) {
            Value v = interp_eval(interp, node->children[i]);
            if (val_is_signal(&v)) return v;
            val_free(&v);
        }
        return val_null();
    }

    /* ── 프로그램 루트 ──────────────────────────────────────── */
    case NODE_PROGRAM: {
        for (int i = 0; i < node->child_count; i++) {
            Value v = interp_eval(interp, node->children[i]);
            if (v.type == VAL_ERROR) {
                fprintf(stderr, "%s\n", v.as.sval);
                val_free(&v);
                return val_null();
            }
            if (v.type == VAL_RETURN || v.type == VAL_BREAK || v.type == VAL_CONTINUE) {
                val_free(&v);
                return val_null();
            }
            val_free(&v);
        }
        return val_null();
    }

    /* ── 만약/아니면 ────────────────────────────────────────── */
    case NODE_IF: {
        Value cond = interp_eval(interp, node->children[0]);
        PROPAGATE(cond);
        int truthy = val_is_truthy(&cond);
        val_free(&cond);

        if (truthy) {
            Env *prev = interp->current;
            interp->current = env_new(prev);
            Value v = interp_eval(interp, node->children[1]);
            env_free(interp->current, interp);
            interp->current = prev;
            return v;
        } else if (node->child_count > 2) {
            return interp_eval(interp, node->children[2]);
        }
        return val_null();
    }

    case NODE_ELIF: {
        Value cond = interp_eval(interp, node->children[0]);
        PROPAGATE(cond);
        int truthy = val_is_truthy(&cond);
        val_free(&cond);

        if (truthy) {
            Env *prev = interp->current;
            interp->current = env_new(prev);
            Value v = interp_eval(interp, node->children[1]);
            env_free(interp->current, interp);
            interp->current = prev;
            return v;
        } else if (node->child_count > 2) {
            return interp_eval(interp, node->children[2]);
        }
        return val_null();
    }

    case NODE_ELSE: {
        Env *prev = interp->current;
        interp->current = env_new(prev);
        Value v = interp_eval(interp, node->children[0]);
        env_free(interp->current, interp);
        interp->current = prev;
        return v;
    }

    /* ── 동안 (while) ───────────────────────────────────────── */
    case NODE_WHILE: {
        while (1) {
            Value cond = interp_eval(interp, node->children[0]);
            PROPAGATE(cond);
            int truthy = val_is_truthy(&cond);
            val_free(&cond);
            if (!truthy) break;

            Env *prev = interp->current;
            interp->current = env_new(prev);
            Value v = interp_eval(interp, node->children[1]);
            env_free(interp->current, interp);
            interp->current = prev;

            if (v.type == VAL_BREAK)    { val_free(&v); break; }
            if (v.type == VAL_CONTINUE) { val_free(&v); continue; }
            if (val_is_signal(&v))      return v;
            val_free(&v);
        }
        return val_null();
    }

    /* ── 반복 (for range) ───────────────────────────────────── */
    case NODE_FOR_RANGE: {
        Value start_v = interp_eval(interp, node->children[0]);
        PROPAGATE(start_v);
        Value end_v   = interp_eval(interp, node->children[1]);
        PROPAGATE(end_v);
        if (start_v.type != VAL_INT || end_v.type != VAL_INT)
            RT_ERROR(interp, "반복: 범위는 정수여야 합니다");

        int64_t start = start_v.as.ival;
        int64_t end   = end_v.as.ival;
        int64_t step  = (start <= end) ? 1 : -1;

        for (int64_t i = start; step > 0 ? i <= end : i >= end; i += step) {
            Env *prev = interp->current;
            interp->current = env_new(prev);
            env_define(interp->current, node->sval, val_int(i), interp);
            Value v = interp_eval(interp, node->children[2]);
            env_free(interp->current, interp);
            interp->current = prev;

            if (v.type == VAL_BREAK)    { val_free(&v); break; }
            if (v.type == VAL_CONTINUE) { val_free(&v); continue; }
            if (val_is_signal(&v))      return v;
            val_free(&v);
        }
        return val_null();
    }

    /* ── 각각 (foreach) ─────────────────────────────────────── */
    case NODE_FOR_EACH: {
        Value list = interp_eval(interp, node->children[0]);
        PROPAGATE(list);
        if (list.type != VAL_ARRAY && list.type != VAL_STRING)
            RT_ERROR(interp, "각각: 배열 또는 문자열이어야 합니다");

        if (list.type == VAL_ARRAY) {
            for (int i = 0; i < list.gc_obj->data.arr.len; i++) {
                Env *prev = interp->current;
                interp->current = env_new(prev);
                env_define(interp->current, node->sval, list.gc_obj->data.arr.items[i], interp);
                Value v = interp_eval(interp, node->children[1]);
                env_free(interp->current, interp);
                interp->current = prev;

                if (v.type == VAL_BREAK)    { val_free(&v); break; }
                if (v.type == VAL_CONTINUE) { val_free(&v); continue; }
                if (val_is_signal(&v)) { val_free(&list); return v; }
                val_free(&v);
            }
        }
        val_free(&list);
        return val_null();
    }

    /* ── 선택 (switch) ──────────────────────────────────────── */
    case NODE_SWITCH: {
        Value target = interp_eval(interp, node->children[0]);
        PROPAGATE(target);
        int matched = 0;
        for (int i = 1; i < node->child_count; i++) {
            Node *child = node->children[i];
            if (child->type == NODE_CASE) {
                Value cval = interp_eval(interp, child->children[0]);
                PROPAGATE(cval);
                if (val_equal(&target, &cval)) {
                    val_free(&cval);
                    matched = 1;
                    Env *prev = interp->current;
                    interp->current = env_new(prev);
                    Value v = interp_eval(interp, child->children[1]);
                    env_free(interp->current, interp);
                    interp->current = prev;
                    if (v.type == VAL_BREAK) { val_free(&v); break; }
                    if (val_is_signal(&v)) { val_free(&target); return v; }
                    val_free(&v);
                    break;
                }
                val_free(&cval);
            } else if (child->type == NODE_DEFAULT && !matched) {
                Env *prev = interp->current;
                interp->current = env_new(prev);
                Value v = interp_eval(interp, child->children[0]);
                env_free(interp->current, interp);
                interp->current = prev;
                if (v.type == VAL_BREAK) { val_free(&v); break; }
                if (val_is_signal(&v)) { val_free(&target); return v; }
                val_free(&v);
            }
        }
        val_free(&target);
        return val_null();
    }

    /* ── 함수/정의 선언 ─────────────────────────────────────── */
    case NODE_FUNC_DECL:
    case NODE_VOID_DECL: {
        Value fn = val_gc_func(interp, node, interp->current);
        env_define(interp->current, node->sval, fn, interp);
        return val_null();
    }

    /* ── 반환 ───────────────────────────────────────────────── */
    case NODE_RETURN: {
        if (node->child_count > 0) {
            Value v = interp_eval(interp, node->children[0]);
            PROPAGATE(v);
            return val_return(v);
        }
        return val_return(val_null());
    }

    /* ── 멈춤 / 건너뜀 ─────────────────────────────────────── */
    case NODE_BREAK:    return val_break();
    case NODE_CONTINUE: return val_continue();

    /* ── 함수 호출 ──────────────────────────────────────────── */
    case NODE_CALL: {
        /* ★ 클래스 인스턴스화 — 함수명이 클래스 이름인 경우 (v4.2.0)
         * NODE_CALL의 children[0]이 NODE_IDENT 이고 class_find 가능하면 instance_new */
        if (node->children[0] && node->children[0]->type == NODE_IDENT) {
            const char *cname = node->children[0]->sval;
            ClassDef *cd = class_find(interp, cname);
            if (cd) {
                int argc = node->child_count - 1;
                Value *args = (argc > 0) ? (Value*)malloc(sizeof(Value) * argc) : NULL;
                for (int i = 0; i < argc; i++) {
                    args[i] = interp_eval(interp, node->children[i + 1]);
                    if (val_is_signal(&args[i])) {
                        Value sig = args[i]; args[i].type = VAL_NULL;
                        for (int j = 0; j < i; j++) val_free(&args[j]);
                        free(args);
                        return sig;
                    }
                }
                Value inst = instance_new(interp, cd, args, argc);
                for (int i = 0; i < argc; i++) val_free(&args[i]);
                free(args);
                return inst;
            }
        }

        /* ★ 인스턴스 메서드 호출 — obj.method(args) (v4.2.0)
         * children[0] == NODE_MEMBER 이고 obj가 VAL_OBJ이면
         * 멤버 Value를 callee로 사용 + 첫 인수에 self 자동 삽입 */
        if (node->children[0] && node->children[0]->type == NODE_MEMBER) {
            Node *mnode  = node->children[0];
            Value obj_v  = interp_eval(interp, mnode->children[0]);
            PROPAGATE(obj_v);
            if (obj_v.type == VAL_OBJ && obj_v.gc_obj) {
                Value *method = instance_get_member(obj_v.gc_obj, mnode->sval);
                if (method && method->type == VAL_FUNC) {
                    int argc = node->child_count - 1;
                    /* self + 인수 */
                    int total = argc + 1;
                    Value *args = (Value*)malloc(sizeof(Value) * total);
                    args[0] = val_clone(interp, &obj_v);
                    int sig_idx = -1;
                    for (int i = 0; i < argc; i++) {
                        args[i + 1] = interp_eval(interp, node->children[i + 1]);
                        if (val_is_signal(&args[i + 1])) { sig_idx = i + 1; break; }
                    }
                    if (sig_idx >= 0) {
                        Value sig = args[sig_idx]; args[sig_idx].type = VAL_NULL;
                        for (int j = 0; j < sig_idx; j++) val_free(&args[j]);
                        free(args); val_free(&obj_v);
                        return sig;
                    }
                    Value mcopy = val_clone(interp, method);
                    Value result = call_function(interp, &mcopy, args, total);
                    for (int i = 0; i < total; i++) val_free(&args[i]);
                    free(args);
                    val_release(interp, &mcopy);
                    val_free(&obj_v);
                    return result;
                }
            }
            /* 일반 멤버 호출 (배열.길이() 같은 비-객체 케이스는 아래 일반 경로로) */
            val_free(&obj_v);
        }

        /* 일반 함수 호출 */
        Value callee = interp_eval(interp, node->children[0]);
        PROPAGATE(callee);

        /* 인수 평가 */
        int argc = node->child_count - 1;
        Value *args = (argc > 0) ? (Value*)malloc(sizeof(Value) * argc) : NULL;
        for (int i = 0; i < argc; i++) {
            args[i] = interp_eval(interp, node->children[i + 1]);
            if (val_is_signal(&args[i])) {
                Value sig = args[i];  /* 시그널 먼저 저장 */
                args[i].type = VAL_NULL; /* 이중 free 방지 */
                for (int j = 0; j < i; j++) val_free(&args[j]);
                free(args);
                val_free(&callee);
                return sig;
            }
        }

        Value result = call_function(interp, &callee, args, argc);

        for (int i = 0; i < argc; i++) val_free(&args[i]);
        free(args);
        val_free(&callee);
        return result;
    }

    /* ── 인덱스 접근 ────────────────────────────────────────── */
    case NODE_INDEX: {
        Value arr = interp_eval(interp, node->children[0]);
        PROPAGATE(arr);
        Value idx = interp_eval(interp, node->children[1]);
        PROPAGATE(idx);

        if (arr.type == VAL_ARRAY) {
            if (idx.type != VAL_INT) RT_ERROR(interp, "인덱스는 정수여야 합니다");
            int64_t i = idx.as.ival;
            if (i < 0) i = arr.gc_obj->data.arr.len + i; /* 음수 인덱스 */
            if (i < 0 || i >= arr.gc_obj->data.arr.len)
                RT_ERROR(interp, "인덱스 범위 초과: %lld", (long long)i);
            Value result = val_clone(interp, &arr.gc_obj->data.arr.items[i]);
            val_free(&arr);
            return result;
        }
        if (arr.type == VAL_STRING) {
            if (idx.type != VAL_INT) RT_ERROR(interp, "인덱스는 정수여야 합니다");
            /* 바이트 단위 (추후 UTF-8 문자 단위로 개선 가능) */
            int64_t i = idx.as.ival;
            const char *sdata = STR_GET(&arr);
            size_t slen = strlen(sdata);
            if (i < 0) i = (int64_t)slen + i;
            if (i < 0 || (size_t)i >= slen)
                RT_ERROR(interp, "글자 인덱스 범위 초과: %lld", (long long)i);
            char sub[2] = { sdata[i], '\0' };
            val_release(interp, &arr);
            return val_gc_string(interp, sub);
        }
        RT_ERROR(interp, "배열 또는 글자에만 인덱스 접근이 가능합니다");
    }

    /* ── 멤버 접근 ──────────────────────────────────────────── */
    case NODE_MEMBER: {
        Value obj = interp_eval(interp, node->children[0]);
        PROPAGATE(obj);
        Value result = eval_member(interp, &obj, node->sval);
        val_free(&obj);
        return result;
    }

    /* ── 람다 ───────────────────────────────────────────────── */
    case NODE_LAMBDA: {
        return val_gc_func(interp, node, interp->current);
    }

    /* ── 시도/실패시/항상 ───────────────────────────────────── */
    case NODE_TRY: {
        /* child[0] = 시도 블록, child[1] = 실패시 블록, child[2] = 항상 블록(선택) */
        Env *prev = interp->current;
        interp->current = env_new(prev);
        Value try_val = interp_eval(interp, node->children[0]);
        env_free(interp->current, interp);
        interp->current = prev;

        Value finally_val = val_null();

        if (try_val.type == VAL_ERROR && node->child_count > 1) {
            interp->had_error = 0; /* 오류 처리됨 */
            interp->current = env_new(prev);
            /* 오류 메시지를 '오류' 변수로 전달 */
            env_define(interp->current, "오류", val_gc_string(interp, try_val.as.sval), interp);
            val_free(&try_val);
            try_val = interp_eval(interp, node->children[1]);
            env_free(interp->current, interp);
            interp->current = prev;
        }

        if (node->child_count > 2) {
            interp->current = env_new(prev);
            finally_val = interp_eval(interp, node->children[2]);
            env_free(interp->current, interp);
            interp->current = prev;
        }

        val_free(&finally_val);
        return try_val;
    }

    /* ── 오류 발생 (raise) ──────────────────────────────────── */
    case NODE_RAISE: {
        Value msg = interp_eval(interp, node->children[0]);
        PROPAGATE(msg);
        char *s = val_to_string(&msg);
        val_free(&msg);
        interp->had_error = 1;
        snprintf(interp->error_msg, sizeof(interp->error_msg), "[오류] %s", s);
        Value err = val_error(interp->error_msg);
        free(s);
        return err;
    }

    /* ── 계약 시스템 ────────────────────────────────────── */
    /* ★ 헌법 — 전역 최상위 계약 등록 (v5.0.0) */
    case NODE_CONSTITUTION: {
        Node *sn = (node->child_count > 1) ? node->children[1] : NULL;
        ContractEntry ce;
        ce.kind     = TOK_KW_HEONBEOB;
        ce.scope    = strdup("*헌법*");   /* 전역 범위 마커 */
        ce.cond     = (node->child_count > 0) ? node->children[0] : NULL;
        ce.sanction = sn ? sn->op : TOK_KW_JUNGDAN;
        ce.alt_name = (sn && sn->sval) ? strdup(sn->sval) : NULL;
        ce.alt_node = (sn && sn->child_count > 0) ? sn->children[0] : NULL;
        contract_register(interp, ce);
        return val_null();
    }

    /* ★ 법률 — 현재 파일 전체 계약 등록 (v5.0.0) */
    case NODE_STATUTE: {
        Node *sn = (node->child_count > 1) ? node->children[1] : NULL;
        ContractEntry ce;
        ce.kind     = TOK_KW_BEOMNYUL;
        ce.scope    = strdup("*법률*");   /* 파일 단위 범위 마커 */
        ce.cond     = (node->child_count > 0) ? node->children[0] : NULL;
        ce.sanction = sn ? sn->op : TOK_KW_GYEONGGO;
        ce.alt_name = (sn && sn->sval) ? strdup(sn->sval) : NULL;
        ce.alt_node = (sn && sn->child_count > 0) ? sn->children[0] : NULL;
        contract_register(interp, ce);
        return val_null();
    }

    /* ★ 규정 — 특정 객체 전체 메서드 계약 등록 (v5.0.0) */
    case NODE_REGULATION: {
        Node *sn = (node->child_count > 1) ? node->children[1] : NULL;
        ContractEntry ce;
        ce.kind     = TOK_KW_GYUJEONG;
        ce.scope    = strdup(node->sval ? node->sval : "?");  /* 객체명 */
        ce.cond     = (node->child_count > 0) ? node->children[0] : NULL;
        ce.sanction = sn ? sn->op : TOK_KW_GYEONGGO;
        ce.alt_name = (sn && sn->sval) ? strdup(sn->sval) : NULL;
        ce.alt_node = (sn && sn->child_count > 0) ? sn->children[0] : NULL;
        contract_register(interp, ce);
        return val_null();
    }

    case NODE_CONTRACT: {
        /* NODE_SANCTION = child[1] 에서 제재 정보 추출 */
        Node *sn = (node->child_count > 1) ? node->children[1] : NULL;
        ContractEntry ce;
        ce.kind     = node->op;
        ce.scope    = strdup(node->sval ? node->sval : "?");
        ce.cond     = (node->child_count > 0) ? node->children[0] : NULL;
        ce.sanction = sn ? sn->op : TOK_KW_GYEONGGO;
        ce.alt_name = (sn && sn->sval) ? strdup(sn->sval) : NULL;
        ce.alt_node = (sn && sn->child_count > 0) ? sn->children[0] : NULL;
        contract_register(interp, ce);
        /* 함수 내부 법령 즉시 평가:
         * current != global 이면 함수 스코프 → 조건 즉시 평가
         * 전역 법령은 등록만 하고 call_function 에서 평가 */
        if (ce.cond && interp->current != interp->global) {
            Value cv = interp_eval(interp, ce.cond);
            int passed = val_is_truthy(&cv);
            val_release(interp, &cv);
            if (!passed) {
                Value sanc = apply_sanction(interp,
                    &interp->contracts[interp->contract_count - 1],
                    NULL);
                /* 오류 신호는 그대로, 일반 값은 VAL_RETURN으로 감싸
                 * 함수 본문 나머지 실행을 중단시킴 */
                if (sanc.type == VAL_ERROR) return sanc;
                if (sanc.type == VAL_NULL)  return val_null();
                return val_return(sanc);
            }
        }
        return val_null();
    }

    case NODE_CHECKPOINT:
        if (node->sval)
            checkpoint_register(interp, node->sval);
        return val_null();

    /* ── 전처리기 구문 (v6.1.0) ─────────────────────────────── */
    case NODE_PP_STMT: {
        /* #GPU사용 등 파일명 없는 지시어는 무시 */
        if (node->op != TOK_PP_INCLUDE || !node->sval)
            return val_null();

        /* ── #포함 "파일명" 처리 ──
         * 지원 확장자:
         *   .han / .hg  → Kcode 소스 파싱 후 현재 환경에서 실행
         *   .c  / .h    → C   헤더: 내장 심볼만 등록 (주석 처리)
         *   .cpp/ .hpp  → C++ 헤더: 동일
         *   .py         → Python: 실행 후 KCODE_EXPORT 환경변수로 심볼 수신
         *   .js / .ts   → JS/TS:  동일 (node / ts-node)
         *   .java       → Java:   컴파일 후 반환값 수신
         */
        const char *fname = node->sval;

        /* 확장자 추출 */
        const char *dot = strrchr(fname, '.');
        const char *ext = dot ? dot : "";

        /* ── Kcode 파일 (.han / .hg) ─────────────────────── */
        if (strcmp(ext, ".han") == 0 || strcmp(ext, ".hg") == 0) {
            FILE *f = fopen(fname, "rb");
            if (!f) {
                /* 현재 디렉토리 재시도 */
                char path2[512];
                snprintf(path2, sizeof(path2), "./%s", fname);
                f = fopen(path2, "rb");
            }
            if (!f) {
                RT_ERROR(interp, "#포함: 파일을 열 수 없습니다 — '%s'", fname);
            }
            fseek(f, 0, SEEK_END);
            long fsz = ftell(f);
            rewind(f);
            char *src = (char*)malloc((size_t)fsz + 1);
            if (!src) { fclose(f); RT_ERROR(interp, "#포함: 메모리 부족"); }
            fread(src, 1, (size_t)fsz, f);
            src[fsz] = '\0';
            fclose(f);

            /* 파싱 후 현재 환경에서 실행 */
            Lexer sub_lx;
            lexer_init(&sub_lx, src, (size_t)fsz);
            Parser sub_p;
            parser_init(&sub_p, &sub_lx);
            Node *sub_ast = parser_parse(&sub_p);
            free(src);

            if (sub_p.had_error) {
                node_free(sub_ast);
                RT_ERROR(interp, "#포함 '%s': 파싱 오류 — %s",
                         fname,
                         sub_p.error_count > 0 ? sub_p.errors[0] : "알 수 없음");
            }

            /* 포함 파일의 최상위 구문을 현재 환경에서 순서대로 실행 */
            Value inc_result = val_null();
            for (int i = 0; i < sub_ast->child_count; i++) {
                val_release(interp, &inc_result);
                inc_result = interp_eval(interp, sub_ast->children[i]);
                if (interp->had_error) { node_free(sub_ast); return inc_result; }
            }
            node_free(sub_ast);
            return inc_result;
        }

        /* ── C / C++ 헤더 (.c .h .cpp .hpp) ────────────────
         * 인터프리터 모드에서는 심볼 등록 없이 주석으로만 처리.
         * C 코드 생성 모드(kcodegen.c)에서 실제 #include 삽입. */
        if (strcmp(ext, ".c")   == 0 || strcmp(ext, ".h")   == 0 ||
            strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0) {
            fprintf(stderr, "[#포함] C/C++ 헤더 '%s' — 인터프리터 모드 심볼 등록 생략\n", fname);
            return val_null();
        }

        /* ── Python (.py) ────────────────────────────────── */
        if (strcmp(ext, ".py") == 0) {
            /* python3 스크립트 실행 후 stdout 마지막 줄을 반환값으로 */
            char cmd[512];
            char out_path[256];
            snprintf(out_path, sizeof(out_path), "/tmp/_kcinc_%d.out", (int)getpid());
            snprintf(cmd, sizeof(cmd), "python3 '%s' > '%s' 2>&1", fname, out_path);
            int r = system(cmd);
            if (r != 0) {
                remove(out_path);
                RT_ERROR(interp, "#포함 Python '%s': 실행 실패 (코드 %d)", fname, r);
            }
            FILE *of = fopen(out_path, "r");
            char out_buf[4096] = "";
            if (of) { fread(out_buf, 1, sizeof(out_buf)-1, of); fclose(of); }
            remove(out_path);
            /* stdout 전체를 문자열 값으로 반환 */
            size_t olen = strlen(out_buf);
            if (olen > 0 && out_buf[olen-1] == '\n') out_buf[--olen] = '\0';
            return val_gc_string(interp, out_buf);
        }

        /* ── JavaScript (.js) ────────────────────────────── */
        if (strcmp(ext, ".js") == 0) {
            char cmd[512], out_path[256];
            snprintf(out_path, sizeof(out_path), "/tmp/_kcinc_%d.out", (int)getpid());
            snprintf(cmd, sizeof(cmd), "node '%s' > '%s' 2>&1", fname, out_path);
            int r = system(cmd);
            if (r != 0) {
                remove(out_path);
                RT_ERROR(interp, "#포함 JS '%s': 실행 실패 (코드 %d)", fname, r);
            }
            FILE *of = fopen(out_path, "r");
            char out_buf[4096] = "";
            if (of) { fread(out_buf, 1, sizeof(out_buf)-1, of); fclose(of); }
            remove(out_path);
            size_t olen = strlen(out_buf);
            if (olen > 0 && out_buf[olen-1] == '\n') out_buf[--olen] = '\0';
            return val_gc_string(interp, out_buf);
        }

        /* ── TypeScript (.ts) ────────────────────────────── */
        if (strcmp(ext, ".ts") == 0) {
            char cmd[512], out_path[256];
            snprintf(out_path, sizeof(out_path), "/tmp/_kcinc_%d.out", (int)getpid());
            /* ts-node 우선, 없으면 tsc 후 node */
            if (system("which ts-node > /dev/null 2>&1") == 0)
                snprintf(cmd, sizeof(cmd), "ts-node '%s' > '%s' 2>&1", fname, out_path);
            else
                snprintf(cmd, sizeof(cmd),
                         "tsc '%s' --outDir /tmp && node '/tmp/%s.js' > '%s' 2>&1",
                         fname, fname, out_path);
            int r = system(cmd);
            if (r != 0) {
                remove(out_path);
                RT_ERROR(interp, "#포함 TS '%s': 실행 실패 (코드 %d)", fname, r);
            }
            FILE *of = fopen(out_path, "r");
            char out_buf[4096] = "";
            if (of) { fread(out_buf, 1, sizeof(out_buf)-1, of); fclose(of); }
            remove(out_path);
            size_t olen = strlen(out_buf);
            if (olen > 0 && out_buf[olen-1] == '\n') out_buf[--olen] = '\0';
            return val_gc_string(interp, out_buf);
        }

        /* ── Java (.java) ────────────────────────────────── */
        if (strcmp(ext, ".java") == 0) {
            if (system("which javac > /dev/null 2>&1") != 0)
                RT_ERROR(interp, "#포함 Java '%s': javac 가 설치되어 있지 않습니다", fname);
            char class_dir[256], out_path[256], cmd[768];
            snprintf(class_dir, sizeof(class_dir), "/tmp/_kcjava_%d", (int)getpid());
            snprintf(out_path, sizeof(out_path), "/tmp/_kcinc_%d.out", (int)getpid());
            snprintf(cmd, sizeof(cmd),
                     "mkdir -p '%s' && javac -d '%s' '%s' && java -cp '%s' Main > '%s' 2>&1",
                     class_dir, class_dir, fname, class_dir, out_path);
            int r = system(cmd);
            if (r != 0) {
                remove(out_path);
                RT_ERROR(interp, "#포함 Java '%s': 컴파일/실행 실패 (코드 %d)", fname, r);
            }
            FILE *of = fopen(out_path, "r");
            char out_buf[4096] = "";
            if (of) { fread(out_buf, 1, sizeof(out_buf)-1, of); fclose(of); }
            remove(out_path);
            size_t olen = strlen(out_buf);
            if (olen > 0 && out_buf[olen-1] == '\n') out_buf[--olen] = '\0';
            return val_gc_string(interp, out_buf);
        }

        /* 알 수 없는 확장자 — 경고 후 무시 */
        fprintf(stderr, "[#포함] 지원하지 않는 확장자 '%s' — 무시됨\n", fname);
        return val_null();
    }

    /* ── 가속기(GPU/NPU/CPU) 블록 (v9.0.0) ────────────────────────────
     *  전략: CUDA C 코드 자동 생성 → nvcc 컴파일 → 실행 → 결과 수집
     *  (스크립트 블록 패턴 완전 응용)
     *
     *  폴백 체인: GPU(nvcc) → NPU(onnxruntime) → CPU(gcc -fopenmp) → 일반 실행
     * ================================================================ */
    case NODE_GPU_BLOCK: {
        /* 0. 가속기 종류 결정 */
        const char *accel_type = node->sval; /* "GPU"|"NPU"|"CPU"|NULL(자동) */

        /* 1. 현재 환경 변수 전체를 환경 변수로 직렬화
         *    배열 → CSV "v0,v1,...", 스칼라 → 값 문자열 */
        Env *env = interp->current;
        for (int b = 0; b < ENV_BUCKET_SIZE; b++) {
            for (EnvEntry *e = env->buckets[b]; e; e = e->next) {
                if (!e->name) continue;
                char key[160];
                snprintf(key, sizeof(key), "KCODE_%s", e->name);
                if (e->val.type == VAL_ARRAY && e->val.gc_obj) {
                    int len = (int)VAL_ARR_LEN(&e->val);
                    size_t bufsize = (size_t)(len * 32 + 64);
                    char *csv = (char*)malloc(bufsize);
                    if (!csv) continue;
                    csv[0] = '\0';
                    for (int i = 0; i < len; i++) {
                        char *vs = val_to_string(&VAL_ARR_ITEMS(&e->val)[i]);
                        if (vs) {
                            if (i > 0) strncat(csv, ",", bufsize - strlen(csv) - 1);
                            strncat(csv, vs, bufsize - strlen(csv) - 1);
                            free(vs);
                        }
                    }
                    setenv(key, csv, 1);
                    free(csv);
                } else {
                    char *vs = val_to_string(&e->val);
                    if (vs) { setenv(key, vs, 1); free(vs); }
                }
            }
        }

        /* 2. 첫 번째 NODE_GPU_OP 에서 연산/인수/반환 정보 추출 */
        const char *op_name   = NULL;
        const char *ret_vname = NULL;
        const char *in_args[4] = {NULL,NULL,NULL,NULL};
        int         in_argc    = 0;

        for (int i = 0; i < node->child_count; i++) {
            Node *ch = node->children[i];
            if (!ch || ch->type != NODE_GPU_OP) continue;
            op_name = ch->sval;
            int ret_idx = (int)ch->val.ival;
            for (int j = 0; j < ch->child_count && in_argc < 4; j++) {
                if (!ch->children[j]) continue;
                if (j == ret_idx) {
                    ret_vname = ch->children[j]->sval;
                } else if (ch->children[j]->type == NODE_IDENT) {
                    in_args[in_argc++] = ch->children[j]->sval;
                }
            }
            break;
        }

        /* 3. 폴백 체인 가용 여부 확인 */
        int has_nvcc = (system("which nvcc > /dev/null 2>&1") == 0);
        int has_ort  = (system("python3 -c 'import onnxruntime' > /dev/null 2>&1") == 0);
        int has_gomp = (system("echo ''|gcc -fopenmp -x c - -o /dev/null > /dev/null 2>&1") == 0);

        int use_gpu = (!accel_type || strcmp(accel_type,"GPU")==0) && has_nvcc;
        int use_npu = !use_gpu && (!accel_type || strcmp(accel_type,"NPU")==0) && has_ort;
        int use_cpu = !use_gpu && !use_npu && has_gomp;

        if (accel_type && strcmp(accel_type,"GPU")==0 && !has_nvcc)
            fprintf(stderr, "[가속기 GPU] nvcc 미설치 — 폴백 시도\n");
        if (accel_type && strcmp(accel_type,"NPU")==0 && !has_ort)
            fprintf(stderr, "[가속기 NPU] onnxruntime 미설치 — 폴백 시도\n");

        /* ── 4-A. GPU 경로: CUDA C 생성 + nvcc ── */
        if (use_gpu && op_name) {
            static int _kc_gpu_cnt = 0;
            char cu_path[256], bin_path[260], out_path[270];
            snprintf(cu_path,  sizeof(cu_path),  "/tmp/_kcode_gpu_%d_%d.cu",
                     (int)getpid(), ++_kc_gpu_cnt);
            snprintf(bin_path, sizeof(bin_path), "/tmp/_kcode_gpu_%d_%d",
                     (int)getpid(), _kc_gpu_cnt);
            snprintf(out_path, sizeof(out_path), "/tmp/_kcode_gpu_%d_%d.out",
                     (int)getpid(), _kc_gpu_cnt);

            FILE *cf = fopen(cu_path, "w");
            if (!cf) goto gpu_interp_fallback;

            /* CUDA C 공통 헤더 */
            fprintf(cf,
                "/* Kcode 자동 생성 CUDA C — %s */\n"
                "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
                "#include <cuda_runtime.h>\n\n"
                "static long long kc_csv_len(const char *s){\n"
                "    if(!s||!*s)return 0; long long n=1;\n"
                "    for(const char*p=s;*p;p++) if(*p==',')n++; return n;}\n"
                "static void kc_csv_parse(const char*s,double*o,long long n){\n"
                "    char*b=strdup(s?s:\"\"); char*t=strtok(b,\",\");\n"
                "    for(long long i=0;i<n&&t;i++,t=strtok(NULL,\",\")) o[i]=atof(t);\n"
                "    free(b);}\n\n",
                op_name);

            /* 커널 함수 — 연산 종류별 */
            /* 행렬곱(element-wise) */
            if (strcmp(op_name,"\xED\x96\x89\xEB\xA0\xAC\xEA\xB3\xB1")==0)
                fprintf(cf,
                    "__global__ void kc_kernel(double*o,const double*a,const double*b,long long n){\n"
                    "    long long i=(long long)blockIdx.x*blockDim.x+threadIdx.x;\n"
                    "    if(i<n) o[i]=a[i]*b[i];}\n\n");
            /* 행렬합 */
            else if (strcmp(op_name,"\xED\x96\x89\xEB\xA0\xAC\xED\x95\xA9")==0)
                fprintf(cf,
                    "__global__ void kc_kernel(double*o,const double*a,const double*b,long long n){\n"
                    "    long long i=(long long)blockIdx.x*blockDim.x+threadIdx.x;\n"
                    "    if(i<n) o[i]=a[i]+b[i];}\n\n");
            /* 활성화(ReLU) */
            else if (strcmp(op_name,"\xED\x99\x9C\xEC\x84\xB1\xED\x99\x94")==0)
                fprintf(cf,
                    "__global__ void kc_kernel(double*o,const double*a,const double*b,long long n){\n"
                    "    long long i=(long long)blockIdx.x*blockDim.x+threadIdx.x;\n"
                    "    (void)b; if(i<n) o[i]=a[i]>0.0?a[i]:0.0;}\n\n");
            /* 전치(1D 역순) */
            else if (strcmp(op_name,"\xEC\xA0\x84\xEC\xB9\x98")==0)
                fprintf(cf,
                    "__global__ void kc_kernel(double*o,const double*a,const double*b,long long n){\n"
                    "    long long i=(long long)blockIdx.x*blockDim.x+threadIdx.x;\n"
                    "    (void)b; if(i<n) o[i]=a[n-1-i];}\n\n");
            /* 합성곱(element-wise 기본) */
            else
                fprintf(cf,
                    "__global__ void kc_kernel(double*o,const double*a,const double*b,long long n){\n"
                    "    long long i=(long long)blockIdx.x*blockDim.x+threadIdx.x;\n"
                    "    if(i<n) o[i]=a[i]*b[i];}\n\n");

            /* 호스트 main */
            const char *arg_a = in_argc>0 ? in_args[0] : "A";
            const char *arg_b = in_argc>1 ? in_args[1] : "B";
            fprintf(cf,
                "int main(void){\n"
                "    const char*ea=getenv(\"KCODE_%s\");\n"
                "    const char*eb=getenv(\"KCODE_%s\");\n"
                "    long long n=kc_csv_len(ea);\n"
                "    if(n<=0){fprintf(stderr,\"[가속기] 입력 배열 없음\\n\");return 1;}\n"
                "    double*ha=(double*)calloc(n,sizeof(double));\n"
                "    double*hb=(double*)calloc(n,sizeof(double));\n"
                "    double*ho=(double*)calloc(n,sizeof(double));\n"
                "    kc_csv_parse(ea,ha,n); kc_csv_parse(eb,hb,n);\n"
                "    double*da,*db,*dout;\n"
                "    cudaMalloc(&da,n*sizeof(double));\n"
                "    cudaMalloc(&db,n*sizeof(double));\n"
                "    cudaMalloc(&dout,n*sizeof(double));\n"
                "    cudaMemcpy(da,ha,n*sizeof(double),cudaMemcpyHostToDevice);\n"
                "    cudaMemcpy(db,hb,n*sizeof(double),cudaMemcpyHostToDevice);\n"
                "    int th=256, bl=(int)((n+th-1)/th);\n"
                "    kc_kernel<<<bl,th>>>(dout,da,db,n);\n"
                "    cudaDeviceSynchronize();\n"
                "    cudaMemcpy(ho,dout,n*sizeof(double),cudaMemcpyDeviceToHost);\n"
                "    for(long long i=0;i<n;i++) printf(\"%%.17g\\n\",ho[i]);\n"
                "    cudaFree(da);cudaFree(db);cudaFree(dout);\n"
                "    free(ha);free(hb);free(ho); return 0;}\n",
                arg_a, arg_b);
            fclose(cf);

            /* nvcc 컴파일 */
            char cmd[768];
            snprintf(cmd,sizeof(cmd),"nvcc '%s' -o '%s' > '%s' 2>&1",
                     cu_path, bin_path, out_path);
            int ret = system(cmd);
            if (ret != 0) {
                FILE *ef = fopen(out_path,"r");
                if (ef) {
                    char eb2[2048]=""; size_t el=fread(eb2,1,sizeof(eb2)-1,ef);
                    eb2[el]='\0'; fclose(ef);
                    fprintf(stderr,"[가속기 GPU] nvcc 오류:\n%s\n",eb2);
                }
                remove(cu_path); remove(out_path);
                fprintf(stderr,"[가속기 GPU] 컴파일 실패 — 폴백\n");
                goto gpu_interp_fallback;
            }

            /* 실행 */
            snprintf(cmd,sizeof(cmd),"'%s' > '%s' 2>&1", bin_path, out_path);
            ret = system(cmd);

            /* stdout 수집 */
            char *out_buf = NULL; size_t out_len = 0;
            FILE *of = fopen(out_path,"r");
            if (of) {
                fseek(of,0,SEEK_END); out_len=(size_t)ftell(of); rewind(of);
                out_buf=(char*)malloc(out_len+1);
                if(out_buf){fread(out_buf,1,out_len,of); out_buf[out_len]='\0';}
                fclose(of);
            }
            remove(cu_path); remove(bin_path); remove(out_path);

            if (ret != 0) {
                fprintf(stderr,"[가속기 GPU] 실행 오류: %s\n", out_buf?out_buf:"");
                free(out_buf); goto gpu_interp_fallback;
            }

            /* 반환 변수 → 배열로 저장 */
            if (ret_vname && out_buf && out_len > 0) {
                Value arr = val_gc_array(interp);
                char *line = strtok(out_buf, "\n");
                while (line) {
                    while(*line=='\r'||*line==' ') line++;
                    if (*line) {
                        char *endp; double fv = strtod(line,&endp);
                        Value item;
                        if (endp!=line && (*endp=='\0'||*endp=='\r')) {
                            long long iv=(long long)fv;
                            item = ((double)iv==fv) ? val_int(iv) : val_float(fv);
                        } else { item = val_gc_string(interp,line); }
                        gc_array_push(interp, arr.gc_obj, item);
                        val_release(interp, &item);
                    }
                    line = strtok(NULL,"\n");
                }
                env_set(interp->current, ret_vname, arr, interp);
                val_release(interp, &arr);
            }
            free(out_buf);
            return val_null();
        }

        /* ── 4-B. NPU 경로: Python onnxruntime 스크립트 자동생성 + 실행 ── */
        if (use_npu && op_name) {
            static int _kc_npu_cnt = 0;
            char py_path[256], out_path[270];
            snprintf(py_path,  sizeof(py_path),  "/tmp/_kcode_npu_%d_%d.py",
                     (int)getpid(), ++_kc_npu_cnt);
            snprintf(out_path, sizeof(out_path), "/tmp/_kcode_npu_%d_%d.out",
                     (int)getpid(), _kc_npu_cnt);

            const char *arg_a = in_argc > 0 ? in_args[0] : "A";
            const char *arg_b = in_argc > 1 ? in_args[1] : "B";

            FILE *pf = fopen(py_path, "w");
            if (!pf) goto gpu_interp_fallback;

            /* ── Python 스크립트 공통 헤더 ── */
            fprintf(pf,
                "import os, sys, numpy as np\n"
                "def csv_to_arr(s):\n"
                "    if not s: return np.array([], dtype=np.float64)\n"
                "    return np.array([float(x) for x in s.split(',')], dtype=np.float64)\n"
                "def arr_to_csv(a):\n"
                "    return '\\n'.join(['%%.17g' %% v for v in a.flatten()])\n"
                "ea = os.environ.get('KCODE_%s', '')\n"
                "eb = os.environ.get('KCODE_%s', '')\n"
                "a = csv_to_arr(ea)\n"
                "b = csv_to_arr(eb)\n",
                arg_a, arg_b);

            /* ── 연산 종류별 numpy 구현 ── */
            /* 행렬곱 (元素別 곱 — 1D 배열 기준 단순화) */
            if (strcmp(op_name, "\xED\x96\x89\xEB\xA0\xAC\xEA\xB3\xB1") == 0)
                fprintf(pf,
                    "try:\n"
                    "    import onnxruntime as ort\n"
                    "    from onnx import helper, TensorProto, numpy_helper\n"
                    "    n = max(len(a), len(b))\n"
                    "    a2 = np.resize(a, n).astype(np.float32).reshape(1,n)\n"
                    "    b2 = np.resize(b, n).astype(np.float32).reshape(n,1)\n"
                    "    X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [1,n])\n"
                    "    Y = helper.make_tensor_value_info('Y', TensorProto.FLOAT, [n,1])\n"
                    "    Z = helper.make_tensor_value_info('Z', TensorProto.FLOAT, [1,1])\n"
                    "    node = helper.make_node('MatMul', ['X','Y'], ['Z'])\n"
                    "    g = helper.make_graph([node], 'g', [X,Y], [Z])\n"
                    "    m = helper.make_model(g)\n"
                    "    sess = ort.InferenceSession(m.SerializeToString())\n"
                    "    out = sess.run(['Z'], {'X': a2, 'Y': b2})[0].flatten()\n"
                    "except Exception:\n"
                    "    out = a * b\n"
                    "print(arr_to_csv(out))\n");
            /* 행렬합 */
            else if (strcmp(op_name, "\xED\x96\x89\xEB\xA0\xAC\xED\x95\xA9") == 0)
                fprintf(pf,
                    "try:\n"
                    "    import onnxruntime as ort\n"
                    "    from onnx import helper, TensorProto\n"
                    "    n = max(len(a), len(b))\n"
                    "    a2 = np.resize(a, n).astype(np.float32).reshape(1,n)\n"
                    "    b2 = np.resize(b, n).astype(np.float32).reshape(1,n)\n"
                    "    X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [1,n])\n"
                    "    Y = helper.make_tensor_value_info('Y', TensorProto.FLOAT, [1,n])\n"
                    "    Z = helper.make_tensor_value_info('Z', TensorProto.FLOAT, [1,n])\n"
                    "    node = helper.make_node('Add', ['X','Y'], ['Z'])\n"
                    "    g = helper.make_graph([node], 'g', [X,Y], [Z])\n"
                    "    m = helper.make_model(g)\n"
                    "    sess = ort.InferenceSession(m.SerializeToString())\n"
                    "    out = sess.run(['Z'], {'X': a2, 'Y': b2})[0].flatten()\n"
                    "except Exception:\n"
                    "    out = a + b\n"
                    "print(arr_to_csv(out))\n");
            /* 활성화 (ReLU 기본) */
            else if (strcmp(op_name, "\xED\x99\x9C\xEC\x84\xB1\xED\x99\x94") == 0)
                fprintf(pf,
                    "try:\n"
                    "    import onnxruntime as ort\n"
                    "    from onnx import helper, TensorProto\n"
                    "    n = len(a)\n"
                    "    a2 = a.astype(np.float32).reshape(1,n)\n"
                    "    X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [1,n])\n"
                    "    Z = helper.make_tensor_value_info('Z', TensorProto.FLOAT, [1,n])\n"
                    "    node = helper.make_node('Relu', ['X'], ['Z'])\n"
                    "    g = helper.make_graph([node], 'g', [X], [Z])\n"
                    "    m = helper.make_model(g)\n"
                    "    sess = ort.InferenceSession(m.SerializeToString())\n"
                    "    out = sess.run(['Z'], {'X': a2})[0].flatten()\n"
                    "except Exception:\n"
                    "    out = np.maximum(a, 0.0)\n"
                    "print(arr_to_csv(out))\n");
            /* 전치 */
            else if (strcmp(op_name, "\xEC\xA0\x84\xEC\xB9\x98") == 0)
                fprintf(pf,
                    "try:\n"
                    "    import onnxruntime as ort\n"
                    "    from onnx import helper, TensorProto\n"
                    "    n = len(a)\n"
                    "    sq = int(n**0.5)\n"
                    "    if sq*sq == n:\n"
                    "        a2 = a.astype(np.float32).reshape(1,sq,sq)\n"
                    "        X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [1,sq,sq])\n"
                    "        Z = helper.make_tensor_value_info('Z', TensorProto.FLOAT, [1,sq,sq])\n"
                    "        node = helper.make_node('Transpose', ['X'], ['Z'], perm=[0,2,1])\n"
                    "        g = helper.make_graph([node], 'g', [X], [Z])\n"
                    "        m = helper.make_model(g)\n"
                    "        sess = ort.InferenceSession(m.SerializeToString())\n"
                    "        out = sess.run(['Z'], {'X': a2})[0].flatten()\n"
                    "    else:\n"
                    "        out = a[::-1]\n"
                    "except Exception:\n"
                    "    out = a[::-1]\n"
                    "print(arr_to_csv(out))\n");
            /* 합성곱 및 기타 */
            else
                fprintf(pf,
                    "try:\n"
                    "    import onnxruntime as ort\n"
                    "    from onnx import helper, TensorProto\n"
                    "    n = max(len(a), len(b))\n"
                    "    a2 = np.resize(a, n).astype(np.float32).reshape(1,1,1,n)\n"
                    "    b2 = np.resize(b if len(b)>0 else np.ones(1), 1).astype(np.float32).reshape(1,1,1,1)\n"
                    "    X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [1,1,1,n])\n"
                    "    Z = helper.make_tensor_value_info('Z', TensorProto.FLOAT, None)\n"
                    "    from onnx import numpy_helper\n"
                    "    w_init = numpy_helper.from_array(b2, name='W')\n"
                    "    node = helper.make_node('Conv', ['X','W'], ['Z'])\n"
                    "    g = helper.make_graph([node], 'g', [X], [Z], [w_init])\n"
                    "    m = helper.make_model(g)\n"
                    "    sess = ort.InferenceSession(m.SerializeToString())\n"
                    "    out = sess.run(['Z'], {'X': a2})[0].flatten()\n"
                    "except Exception:\n"
                    "    out = a * b if len(b) > 0 else a\n"
                    "print(arr_to_csv(out))\n");

            fclose(pf);

            /* python3 실행 */
            char cmd[768];
            snprintf(cmd, sizeof(cmd),
                "python3 '%s' > '%s' 2>&1", py_path, out_path);
            int ret = system(cmd);

            /* stdout 수집 */
            char *out_buf = NULL; size_t out_len = 0;
            FILE *of = fopen(out_path, "r");
            if (of) {
                fseek(of, 0, SEEK_END); out_len = (size_t)ftell(of); rewind(of);
                out_buf = (char*)malloc(out_len + 1);
                if (out_buf) { fread(out_buf, 1, out_len, of); out_buf[out_len] = '\0'; }
                fclose(of);
            }
            remove(py_path); remove(out_path);

            if (ret == 0 && ret_vname && out_buf && out_len > 0) {
                Value arr = val_gc_array(interp);
                char *line = strtok(out_buf, "\n");
                while (line) {
                    while (*line == '\r' || *line == ' ') line++;
                    if (*line) {
                        char *endp; double fv = strtod(line, &endp);
                        Value item;
                        if (endp != line && (*endp == '\0' || *endp == '\r')) {
                            long long iv = (long long)fv;
                            item = ((double)iv == fv) ? val_int(iv) : val_float(fv);
                        } else {
                            item = val_gc_string(interp, line);
                        }
                        gc_array_push(interp, arr.gc_obj, item);
                        val_release(interp, &item);
                    }
                    line = strtok(NULL, "\n");
                }
                env_set(interp->current, ret_vname, arr, interp);
                val_release(interp, &arr);
                free(out_buf);
                return val_null();
            }
            if (ret != 0)
                fprintf(stderr, "[가속기 NPU] python3 실행 오류 — CPU 폴백\n%s\n",
                        out_buf ? out_buf : "");
            free(out_buf);
            /* NPU 실패 시 CPU 폴백 */
            use_cpu = has_gomp;
        }

        /* ── 4-C. CPU 경로: gcc -fopenmp ── */
        if (use_cpu && op_name) {
            static int _kc_cpu_cnt = 0;
            char c_path[256], bin_path[260], out_path[270];
            snprintf(c_path,   sizeof(c_path),   "/tmp/_kcode_cpu_%d_%d.c",
                     (int)getpid(), ++_kc_cpu_cnt);
            snprintf(bin_path, sizeof(bin_path),  "/tmp/_kcode_cpu_%d_%d",
                     (int)getpid(), _kc_cpu_cnt);
            snprintf(out_path, sizeof(out_path),  "/tmp/_kcode_cpu_%d_%d.out",
                     (int)getpid(), _kc_cpu_cnt);

            const char *arg_a = in_argc>0 ? in_args[0] : "A";
            const char *arg_b = in_argc>1 ? in_args[1] : "B";

            FILE *cf = fopen(c_path,"w");
            if (!cf) goto gpu_interp_fallback;
            fprintf(cf,
                "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
                "#ifdef _OPENMP\n#include <omp.h>\n#endif\n"
                "static long long kc_csv_len(const char*s){\n"
                "    if(!s||!*s)return 0;long long n=1;\n"
                "    for(const char*p=s;*p;p++)if(*p==',')n++;return n;}\n"
                "static void kc_csv_parse(const char*s,double*o,long long n){\n"
                "    char*b=strdup(s?s:\"\");char*t=strtok(b,\",\");\n"
                "    for(long long i=0;i<n&&t;i++,t=strtok(NULL,\",\"))o[i]=atof(t);\n"
                "    free(b);}\n"
                "int main(void){\n"
                "    const char*ea=getenv(\"KCODE_%s\");\n"
                "    const char*eb=getenv(\"KCODE_%s\");\n"
                "    long long n=kc_csv_len(ea); if(n<=0)return 1;\n"
                "    double*a=(double*)calloc(n,sizeof(double));\n"
                "    double*b=(double*)calloc(n,sizeof(double));\n"
                "    double*o=(double*)calloc(n,sizeof(double));\n"
                "    kc_csv_parse(ea,a,n); kc_csv_parse(eb,b,n);\n"
                "#pragma omp parallel for schedule(static)\n"
                "    for(long long i=0;i<n;i++) o[i]=a[i]+b[i];\n"
                "    for(long long i=0;i<n;i++) printf(\"%%.17g\\n\",o[i]);\n"
                "    free(a);free(b);free(o); return 0;}\n",
                arg_a, arg_b);
            fclose(cf);

            char cmd[768];
            snprintf(cmd,sizeof(cmd),
                "gcc -O2 -fopenmp '%s' -o '%s' > '%s' 2>&1 && '%s' >> '%s' 2>&1",
                c_path, bin_path, out_path, bin_path, out_path);
            int ret = system(cmd);

            char *out_buf=NULL; size_t out_len=0;
            FILE *of=fopen(out_path,"r");
            if(of){fseek(of,0,SEEK_END);out_len=(size_t)ftell(of);rewind(of);
                out_buf=(char*)malloc(out_len+1);
                if(out_buf){fread(out_buf,1,out_len,of);out_buf[out_len]='\0';}
                fclose(of);}
            remove(c_path); remove(bin_path); remove(out_path);

            if (ret==0 && ret_vname && out_buf) {
                Value arr=val_gc_array(interp);
                char *line=strtok(out_buf,"\n");
                while(line){
                    while(*line=='\r'||*line==' ')line++;
                    if(*line){
                        char*endp;double fv=strtod(line,&endp);
                        long long iv=(long long)fv;
                        Value item=(endp!=line&&(*endp=='\0'||*endp=='\r'))
                            ? (((double)iv==fv)?val_int(iv):val_float(fv))
                            : val_gc_string(interp,line);
                        gc_array_push(interp,arr.gc_obj,item);
                        val_release(interp,&item);
                    }
                    line=strtok(NULL,"\n");
                }
                env_set(interp->current,ret_vname,arr,interp);
                val_release(interp,&arr);
                free(out_buf);
                return val_null();
            }
            free(out_buf);
            fprintf(stderr,"[가속기 CPU] 실패 — 일반 인터프리터 폴백\n");
        }

    gpu_interp_fallback:
        /* 최종 폴백: 일반 인터프리터로 NODE_BLOCK 실행 */
        fprintf(stderr,"[가속기] 일반 인터프리터로 실행\n");
        for (int i=node->child_count-1; i>=0; i--) {
            if (node->children[i] && node->children[i]->type==NODE_BLOCK)
                return interp_eval(interp, node->children[i]);
        }
        return val_null();
    }

    /* ── 스크립트 블록 — Python / Java / JavaScript 실행 ───────── */
    case NODE_SCRIPT_PYTHON:
    case NODE_SCRIPT_JAVA:
    case NODE_SCRIPT_JS: {
        if (!node->sval) return val_null();

        /* 1. 전달 변수들을 환경 변수로 직렬화 */
        int ret_child = (int)node->val.ival; /* -1 이면 반환 없음 */

        /* 환경 변수 문자열 구성 */
        char env_block[8192] = "";
        /* 전달 변수: ret_child 이전 child들 (ret_child == -1 이면 모두) */
        int arg_end = (ret_child >= 0) ? ret_child : node->child_count;
        for (int i = 0; i < arg_end; i++) {
            Node *ch = node->children[i];
            if (!ch || ch->type != NODE_IDENT || !ch->sval) continue;
            Value *v = env_get(interp->current, ch->sval);
            if (!v) continue;
            char *vs = val_to_string(v);
            char pair[512];
            snprintf(pair, sizeof(pair), "KCODE_%s=%s\n", ch->sval, vs ? vs : "");
            strncat(env_block, pair, sizeof(env_block) - strlen(env_block) - 1);
            free(vs);
        }

        /* 2. 런처 커맨드 결정 */
        const char *runner     = "python3";
        const char *ext        = ".py";
        const char *end_marker = "파이썬끝";
        if (node->type == NODE_SCRIPT_JAVA) {
            /* javac 존재 여부 확인 */
            if (system("which javac > /dev/null 2>&1") != 0) {
                RT_ERROR(interp, "자바 블록 실행 실패: javac(Java 컴파일러)가 설치되어 있지 않습니다.\n"
                                 "설치: sudo apt-get install default-jdk");
            }
            runner = "javac";  ext = ".java";  end_marker = "자바끝";
        } else if (node->type == NODE_SCRIPT_JS) {
            runner = "node";   ext = ".js";    end_marker = "자바스크립트끝";
        }
        (void)end_marker;

        /* 3. 임시 파일 작성 */
        static int _kc_script_counter = 0;
        char tmp_path[256];
        snprintf(tmp_path, sizeof(tmp_path), "/tmp/_kcode_script_%d_%d%s",
                 (int)getpid(), ++_kc_script_counter, ext);

        FILE *sf = fopen(tmp_path, "w");
        if (!sf) {
            RT_ERROR(interp, "스크립트 임시 파일 생성 실패: %s", tmp_path);
        }

        /* Python/JS: 환경 변수에서 Kcode 변수를 자동 임포트하는 헤더 추가 */
        if (node->type == NODE_SCRIPT_PYTHON) {
            fprintf(sf, "import os as _kc_os\n");
            for (int i = 0; i < arg_end; i++) {
                Node *ch = node->children[i];
                if (!ch || !ch->sval) continue;
                /* 환경변수 읽어 변수 설정, 숫자 자동 변환 */
                fprintf(sf,
                    "_kc_raw_%s = _kc_os.environ.get('KCODE_%s', '')\n"
                    "try: %s = int(_kc_raw_%s)\n"
                    "except ValueError:\n"
                    " try: %s = float(_kc_raw_%s)\n"
                    " except ValueError: %s = _kc_raw_%s\n",
                    ch->sval, ch->sval,
                    ch->sval, ch->sval,
                    ch->sval, ch->sval,
                    ch->sval, ch->sval);
            }
        } else if (node->type == NODE_SCRIPT_JS) {
            for (int i = 0; i < arg_end; i++) {
                Node *ch = node->children[i];
                if (!ch || !ch->sval) continue;
                fprintf(sf,
                    "const %s = (v => isNaN(v) ? process.env['KCODE_%s']||'' : Number(v))"
                    "(process.env['KCODE_%s']);\n",
                    ch->sval, ch->sval, ch->sval);
            }
        }

        /* 원문 코드 작성 */
        fprintf(sf, "%s\n", node->sval);
        fclose(sf);

        /* 4. 환경 변수 설정 후 실행 */
        /* env_block의 각 줄을 setenv로 설정 */
        {
            char *line = strtok(env_block, "\n");
            while (line) {
                char *eq = strchr(line, '=');
                if (eq) {
                    *eq = '\0';
                    setenv(line, eq + 1, 1);
                    *eq = '=';
                }
                line = strtok(NULL, "\n");
            }
        }

        /* 5. 실행 및 stdout 캡처 — 파일 리다이렉트 방식 */
        char out_path[300];
        snprintf(out_path, sizeof(out_path), "%s.out", tmp_path);

        char cmd[600];
        if (node->type == NODE_SCRIPT_JAVA) {
            /* Java: Main 클래스 기준 컴파일 + 실행 */
            char class_dir[256];
            snprintf(class_dir, sizeof(class_dir), "%s_classes", tmp_path);
            snprintf(cmd, sizeof(cmd),
                     "mkdir -p '%s' && javac -d '%s' '%s' > '%s' 2>&1 "
                     "&& java -cp '%s' Main >> '%s' 2>&1",
                     class_dir, class_dir, tmp_path, out_path,
                     class_dir, out_path);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "%s '%s' > '%s' 2>&1",
                     runner, tmp_path, out_path);
        }

        int sys_ret = system(cmd);

        /* stdout 수집 */
        char   out_buf[65536] = "";
        size_t out_len = 0;
        FILE  *outf = fopen(out_path, "r");
        if (outf) {
            out_len = fread(out_buf, 1, sizeof(out_buf) - 1, outf);
            out_buf[out_len] = '\0';
            fclose(outf);
        }
        remove(out_path);
        remove(tmp_path);

        int exit_code = sys_ret;

        /* 6. 오류 처리 */
        if (exit_code != 0) {
            RT_ERROR(interp, "[%s 오류] %s",
                     node->type == NODE_SCRIPT_PYTHON ? "파이썬" :
                     node->type == NODE_SCRIPT_JAVA   ? "자바"   : "자바스크립트",
                     out_buf);
        }

        /* 7. 반환 변수에 stdout 마지막 값 저장 */
        /* stdout 마지막 줄을 반환값으로 사용 */
        if (ret_child >= 0 && node->children[ret_child] &&
            node->children[ret_child]->sval) {

            /* 마지막 비어있지 않은 줄 추출 */
            char *last = out_buf;
            char *scan = out_buf;
            while (*scan) {
                if (*scan == '\n') {
                    char *candidate = scan + 1;
                    /* 줄바꿈 이후 내용이 있으면 last 업데이트 */
                    if (*candidate && *candidate != '\n' && *candidate != '\r')
                        last = candidate;
                }
                scan++;
            }
            /* last 가 여전히 out_buf 이면 첫 줄이 마지막 줄 */
            /* 끝 줄바꿈 제거 */
            size_t llen = strlen(last);
            while (llen > 0 && (last[llen-1] == '\n' || last[llen-1] == '\r'))
                last[--llen] = '\0';

            /* 정수/실수/문자열 자동 판별 */
            Value ret_val;
            char *endp;
            long long iv = strtoll(last, &endp, 10);
            if (endp != last && *endp == '\0') {
                ret_val = val_int((int64_t)iv);
            } else {
                double fv = strtod(last, &endp);
                if (endp != last && *endp == '\0') {
                    ret_val = val_float(fv);
                } else {
                    ret_val = val_gc_string(interp, last);
                }
            }
            env_set(interp->current, node->children[ret_child]->sval, ret_val, interp);
            val_release(interp, &ret_val);
        }

        if (out_len > 0 && out_buf[out_len-1] == '\n') out_buf[out_len-1] = '\0';
        return val_gc_string(interp, out_buf);
    }

    /* ── import(가짐) — 모듈 시스템 (v6.1.0) ─────────────────── */
    case NODE_IMPORT: {
        if (!node->sval) return val_null();
        const char *mod = node->sval;

        /* ── 내장 모듈 레지스트리 ──
         * 내장 모듈은 이미 register_builtins()에 등록되어 있으므로
         * 이름만 확인 후 성공 반환. 추후 개별 네임스페이스 분리 예정. */
        static const char *builtin_modules[] = {
            "수학", "파일시스템", "문자열", "배열", "시간", "수학함수",
            "통계", "AI수학", "색상", "사진", "그림", "화면", "화면3D",
            "행렬", "난수", NULL
        };
        for (int i = 0; builtin_modules[i]; i++) {
            if (strcmp(mod, builtin_modules[i]) == 0) {
                /* 내장 모듈 — 이미 로드됨 */
                fprintf(stderr, "[가짐] 내장 모듈 '%s' 확인됨\n", mod);
                return val_null();
            }
        }

        /* ── 외부 파일 모듈 — 확장자 자동 탐색 ──
         * 우선순위: 모듈명.han → 모듈명.hg → 모듈명.py → 모듈명.js
         *           → 모듈명.ts → 모듈명.java
         * 로부터(from) 지정 시 child 노드에 가져올 이름 목록 포함 */
        static const char *try_exts[] = {
            ".han", ".hg", ".py", ".js", ".ts", ".java", NULL
        };

        char found_path[512] = "";
        for (int i = 0; try_exts[i]; i++) {
            char candidate[512];
            snprintf(candidate, sizeof(candidate), "%s%s", mod, try_exts[i]);
            FILE *tf = fopen(candidate, "rb");
            if (!tf) {
                /* ./ 접두 재시도 */
                snprintf(candidate, sizeof(candidate), "./%s%s", mod, try_exts[i]);
                tf = fopen(candidate, "rb");
            }
            if (tf) {
                fclose(tf);
                strncpy(found_path, candidate, sizeof(found_path)-1);
                break;
            }
        }

        if (found_path[0] == '\0') {
            /* 파일 못 찾음 — 경고만 출력하고 계속 (엄격 모드 추후 추가) */
            fprintf(stderr, "[가짐] 모듈 '%s': 파일을 찾을 수 없음 — 무시됨\n", mod);
            return val_null();
        }

        /* 찾은 파일을 #포함 방식으로 로드 */
        /* NODE_PP_STMT 노드를 임시 생성하여 재귀 처리 */
        Node *inc_node = node_new(NODE_PP_STMT, node->line, node->col);
        inc_node->op   = TOK_PP_INCLUDE;
        inc_node->sval = strdup(found_path);
        Value result   = interp_eval(interp, inc_node);
        node_free(inc_node);

        /* 로부터(from) — child에 가져올 이름 목록이 있으면 로그만 출력
         * (실제 네임스페이스 필터링은 v6.2에서 구현) */
        if (node->child_count > 0) {
            for (int i = 0; i < node->child_count; i++) {
                Node *name_nd = node->children[i];
                if (name_nd && name_nd->sval)
                    fprintf(stderr, "[가짐] '%s' 로부터 '%s' 가져옴\n",
                            mod, name_nd->sval);
            }
        }

        return result;
    }

    /* ── 인터럽트 시스템 (v6.0.0) ──────────────────────────── */

    /* A: OS 시그널 핸들러 등록 */
    case NODE_SIGNAL_HANDLER: {
        if (interp->signal_handler_count >= INTERP_MAX_SIGNALS) {
            RT_ERROR(interp, "신호받기: 최대 핸들러 수(%d) 초과", INTERP_MAX_SIGNALS);
            return val_null();
        }
        /* 신호 이름 → POSIX 번호 변환 */
        int signum = 0;
        switch (node->op) {
            case TOK_KW_SIG_INT:  signum = SIGINT;  break;
            case TOK_KW_SIG_TERM: signum = SIGTERM; break;
            case TOK_KW_SIG_CHLD: signum = SIGCHLD; break;
            case TOK_KW_SIG_USR1: signum = SIGUSR1; break;
            case TOK_KW_SIG_USR2: signum = SIGUSR2; break;
            case TOK_KW_SIG_PIPE: signum = SIGPIPE; break;
            case TOK_KW_SIG_ALRM: signum = SIGALRM; break;
            case TOK_KW_SIG_CONT: signum = SIGCONT; break;
#ifdef SIGSTOP
            case TOK_KW_SIG_STOP: signum = SIGSTOP; break;
#endif
            default:
                RT_ERROR(interp, "신호받기: 알 수 없는 신호 '%s'",
                         node->sval ? node->sval : "?");
                return val_null();
        }
        int idx = interp->signal_handler_count++;
        interp->signal_handlers[idx].signum  = signum;
        interp->signal_handlers[idx].handler = node->child_count > 0
                                                ? node->children[0] : NULL;
        interp->signal_handlers[idx].mode    = 0;
        /* POSIX signal() 등록 — 실제 실행은 C 핸들러에서 interp_eval 호출 */
        /* 인터프리터 내부에서는 플래그 방식으로 처리 */
        fprintf(stderr, "[Kcode 신호] 신호 '%s'(%d) 핸들러 등록됨\n",
                node->sval ? node->sval : "?", signum);
        return val_null();
    }

    /* A: 신호무시 / 신호기본 / 신호보내기 */
    case NODE_SIGNAL_CTRL: {
        int signum = 0;
        /* sval로 신호 번호 결정 (parse 시 sval에 신호 이름 저장됨) */
        const char *sname = node->sval ? node->sval : "";
        if      (strcmp(sname, "중단신호") == 0) signum = SIGINT;
        else if (strcmp(sname, "종료신호") == 0) signum = SIGTERM;
        else if (strcmp(sname, "자식신호") == 0) signum = SIGCHLD;
        else if (strcmp(sname, "사용자신호1") == 0) signum = SIGUSR1;
        else if (strcmp(sname, "사용자신호2") == 0) signum = SIGUSR2;
        else if (strcmp(sname, "연결신호") == 0) signum = SIGPIPE;
        else if (strcmp(sname, "경보신호") == 0) signum = SIGALRM;
        else if (strcmp(sname, "재개신호") == 0) signum = SIGCONT;

        if (node->op == TOK_KW_SINHOMUSI) {
            signal(signum, SIG_IGN);
            fprintf(stderr, "[Kcode 신호] '%s' 무시 설정\n", sname);
        } else if (node->op == TOK_KW_SINHOGIBON) {
            signal(signum, SIG_DFL);
            fprintf(stderr, "[Kcode 신호] '%s' 기본 동작 복원\n", sname);
        } else if (node->op == TOK_KW_SINHOBONEGI) {
            /* 신호보내기: child[0]=PID 표현식 */
            pid_t pid = (pid_t)getpid();
            if (node->child_count > 0) {
                Value pv = interp_eval(interp, node->children[0]);
                if (pv.type == VAL_INT) pid = (pid_t)pv.as.ival;
                val_release(interp, &pv);
            }
            kill(pid, signum);
        }
        return val_null();
    }

    /* B: 하드웨어 간섭 핸들러 등록 */
    case NODE_ISR_HANDLER: {
        if (interp->isr_handler_count >= INTERP_MAX_ISR) {
            RT_ERROR(interp, "간섭: 최대 핸들러 수(%d) 초과", INTERP_MAX_ISR);
            return val_null();
        }
        int idx = interp->isr_handler_count++;
        interp->isr_handlers[idx].vec     = node->op;
        interp->isr_handlers[idx].name    = strdup(node->sval ? node->sval : "?");
        interp->isr_handlers[idx].handler = node->child_count > 0
                                             ? node->children[0] : NULL;
        fprintf(stderr, "[Kcode 간섭] ISR '%s' 등록됨 (인터프리터 시뮬레이션)\n",
                node->sval ? node->sval : "?");
        return val_null();
    }

    /* B: 간섭잠금 / 간섭허용 */
    case NODE_ISR_CTRL: {
        if (node->op == TOK_KW_GANSEOB_JAMGEUM) {
            interp->isr_locked = 1;
            fprintf(stderr, "[Kcode 간섭] 인터럽트 잠금 (cli)\n");
        } else {
            interp->isr_locked = 0;
            fprintf(stderr, "[Kcode 간섭] 인터럽트 허용 (sei)\n");
        }
        return val_null();
    }

    /* C: 행사(이벤트) 핸들러 등록 */
    case NODE_EVENT_HANDLER: {
        if (interp->event_handler_count >= INTERP_MAX_EVENTS) {
            RT_ERROR(interp, "행사등록: 최대 핸들러 수(%d) 초과", INTERP_MAX_EVENTS);
            return val_null();
        }
        const char *evname = node->sval ? node->sval : "?";
        /* 기존 핸들러 교체 확인 */
        for (int i = 0; i < interp->event_handler_count; i++) {
            if (strcmp(interp->event_handlers[i].name, evname) == 0) {
                free(interp->event_handlers[i].name);
                interp->event_handlers[i].name    = strdup(evname);
                interp->event_handlers[i].handler =
                    node->children[node->child_count - 1];
                return val_null();
            }
        }
        int idx = interp->event_handler_count++;
        interp->event_handlers[idx].name    = strdup(evname);
        interp->event_handlers[idx].handler = node->child_count > 0
            ? node->children[node->child_count - 1] : NULL;
        return val_null();
    }

    /* C: 행사시작 / 행사중단 / 행사발생 / 행사해제 */
    case NODE_EVENT_CTRL: {
        if (node->op == TOK_KW_HAENGSA_START) {
            interp->event_running = 1;
            fprintf(stderr, "[Kcode 행사] 이벤트 루프 시작\n");
            /* 단순 폴링 루프 — 실제 환경에서는 OS 이벤트 큐 연동 필요 */
            while (interp->event_running && !interp->had_error) {
                /* 인터프리터 모드: 즉시 종료 (논블로킹) */
                break;
            }
        } else if (node->op == TOK_KW_HAENGSA_STOP) {
            interp->event_running = 0;
            fprintf(stderr, "[Kcode 행사] 이벤트 루프 중단\n");
        } else if (node->op == TOK_KW_HAENGSA_EMIT) {
            /* 행사발생: child[0]=이벤트 이름 표현식 */
            if (node->child_count < 1) return val_null();
            Value nv = interp_eval(interp, node->children[0]);
            const char *evname = NULL;
            if (nv.type == VAL_STRING && nv.gc_obj)
                evname = nv.gc_obj->data.str.data;
            if (evname) {
                for (int i = 0; i < interp->event_handler_count; i++) {
                    if (strcmp(interp->event_handlers[i].name, evname) == 0
                        && interp->event_handlers[i].handler) {
                        Env *ev_env = env_new(interp->current);
                        Env *prev   = interp->current;
                        interp->current = ev_env;
                        interp_eval(interp, interp->event_handlers[i].handler);
                        interp->current = prev;
                        env_free(ev_env, interp);
                        break;
                    }
                }
            }
            val_release(interp, &nv);
        } else if (node->op == TOK_KW_HAENGSA_OFF) {
            /* 행사해제: 핸들러 제거 */
            if (node->child_count < 1) return val_null();
            Value nv = interp_eval(interp, node->children[0]);
            const char *evname = NULL;
            if (nv.type == VAL_STRING && nv.gc_obj)
                evname = nv.gc_obj->data.str.data;
            if (evname) {
                for (int i = 0; i < interp->event_handler_count; i++) {
                    if (strcmp(interp->event_handlers[i].name, evname) == 0) {
                        free(interp->event_handlers[i].name);
                        /* 마지막 항목과 교체 */
                        int last = --interp->event_handler_count;
                        interp->event_handlers[i] = interp->event_handlers[last];
                        break;
                    }
                }
            }
            val_release(interp, &nv);
        }
        return val_null();
    }

    /* ── 클래스 선언 (v4.2.0) ──────────────────────────────── */
    case NODE_CLASS_DECL: {
        ClassDef cd;
        cd.name        = strdup(node->sval ? node->sval : "_class");
        cd.parent_name = NULL;
        cd.node        = node;
        /* 부모 클래스 이름 추출 — child[0]이 NODE_IDENT이고 마지막이 블록일 때 */
        if (node->child_count >= 2 &&
            node->children[0] &&
            node->children[0]->type == NODE_IDENT) {
            cd.parent_name = strdup(node->children[0]->sval
                                    ? node->children[0]->sval : "");
        }
        class_register(interp, cd);
        /* 클래스 이름을 전역 환경에 생성자 함수처럼 등록
         * — 실제 호출 시 NODE_CALL에서 class_find로 instance_new 호출 */
        /* 마커로 VAL_NULL 대신 VAL_BUILTIN 자리에 클래스임을 표시하는
         * 특별한 값이 필요 — 여기서는 이름만 전역에 등록해 두고
         * NODE_CALL의 callee 평가 시 class_find로 탐지 */
        env_define(interp->current, cd.name, val_null(), interp);
        return val_null();
    }

    /* ── 산업/임베디드 블록 (v16.0.0) ─────────────────── */
    case NODE_TIMER_BLOCK: {
        printf("[타이머] 주기 %s 시작\n", node->sval ? node->sval : "?");
        if (node->child_count > 0) interp_eval(interp, node->children[0]);
        printf("[타이머] 주기 %s 종료\n", node->sval ? node->sval : "?");
        return val_null();
    }
    case NODE_ROS2_BLOCK: {
        printf("[ROS2] 노드 '%s' 시작\n", node->sval ? node->sval : "?");
        if (node->child_count > 0) interp_eval(interp, node->children[0]);
        printf("[ROS2] 노드 '%s' 종료\n", node->sval ? node->sval : "?");
        return val_null();
    }

    /* ── 안전 규격 블록 (v17.0.0) ───────────────────────── */
    case NODE_WATCHDOG_BLOCK: {
        printf("[워치독] 타임아웃 %s 보호 시작\n", node->sval ? node->sval : "?");
        if (node->child_count > 0) interp_eval(interp, node->children[0]);
        printf("[워치독] 타임아웃 %s 보호 완료\n", node->sval ? node->sval : "?");
        return val_null();
    }
    case NODE_FAULT_TOL_BLOCK: {
        int64_t n = node->val.ival;
        printf("[결함허용] %lld중 보호 시작\n", (long long)n);
        Value result = val_null();
        for (int ci = 0; ci < node->child_count; ci++) {
            printf("[결함허용] 복제 %d/%d 실행\n", ci+1, node->child_count);
            result = interp_eval(interp, node->children[ci]);
        }
        printf("[결함허용] %lld중 보호 완료\n", (long long)n);
        return result;
    }

    /* ── 온디바이스 AI 블록 (v18.0.0) ─────────────────── */
    case NODE_AI_MODEL_BLOCK: {
        const char *mname = node->sval ? node->sval : "ai_model";
        printf("[AI모델] '%s' 블록 시작\n", mname);
        if (node->child_count > 0) interp_eval(interp, node->children[0]);
        printf("[AI모델] '%s' 블록 종료\n", mname);
        return val_null();
    }
    case NODE_TINYML_BLOCK: {
        const char *mname = node->sval ? node->sval : "tinyml";
        printf("[TinyML] '%s' 블록 시작\n", mname);
        if (node->child_count > 0) interp_eval(interp, node->children[0]);
        printf("[TinyML] '%s' 블록 종료\n", mname);
        return val_null();
    }
    case NODE_FEDERATED_BLOCK: {
        printf("[연합학습] 블록 시작\n");
        if (node->child_count > 0) interp_eval(interp, node->children[0]);
        printf("[연합학습] 블록 종료\n");
        return val_null();
    }

    /* ── 온톨로지 시스템 (v20.1.0) ─────────────────────── */
    case NODE_ONT_BLOCK: {
        int  mode     = (int)node->val.ival;  /* 0=내장, 1=대여, 2=접속 */
        const char *ont_name = node->sval ? node->sval
                                          : "\xEB\x82\xB4\xEC\x9E\xA5"; /* 내장 */

        if (mode == 2) {
            /* ── 모드 2: 접속 (원격 서버) ─────────────── */
            /* child[0] = URL NODE_IDENT, child[1] = body block */
            const char *url  = NULL;
            Node       *body = NULL;
            if (node->child_count >= 1 && node->children[0] &&
                node->children[0]->type == NODE_IDENT) {
                url  = node->children[0]->sval;
                body = (node->child_count >= 2) ? node->children[1] : NULL;
            } else {
                body = (node->child_count >= 1) ? node->children[0] : NULL;
            }
            if (!url) {
                RT_ERROR(interp, "[온톨로지] 접속 모드에 URL이 필요합니다");
            }
            if (interp->ont_remote) {
                kc_ont_remote_disconnect(interp->ont_remote);
                interp->ont_remote = NULL;
            }
            printf("[온톨로지] 원격 서버 접속 중: %s\n", url);
            interp->ont_remote = kc_ont_remote_connect(url);
            if (!interp->ont_remote) {
                fprintf(stderr, "[온톨로지 경고] 원격 서버 연결 실패: %s\n", url);
            }
            /* §12-7: KACP 바이너리 연결 여부 로그 */
            if (interp->ont_remote) {
                if (kc_ont_remote_is_kacp(interp->ont_remote))
                    printf("[온톨로지] KACP 바이너리 연결 확립 (magic=KLNG, 포트 7070)\n");
                else
                    printf("[온톨로지] HTTP 폴백 연결 (포트 8765)\n");
            }
            if (body) interp_eval(interp, body);
            if (interp->ont_remote) {
                kc_ont_remote_disconnect(interp->ont_remote);
                interp->ont_remote = NULL;
            }
        } else {
            /* ── 모드 0(내장) / 1(대여) ────────────────── */
            if (!interp->ont_local) {
                interp->ont_local = kc_ont_create(ont_name);
            }
            /* child[0] = body block */
            if (node->child_count >= 1 && node->children[0]) {
                interp_eval(interp, node->children[0]);
            }
            /* MCP 자동 노출 (아직 미노출 시) */
            if (!interp->mcp_exposed) {
                kc_interp_ont_expose_mcp(interp);
                interp->mcp_exposed = 1;
            }
        }
        return val_null();
    }

    case NODE_ONT_CONCEPT: {
        /* 내장/대여 모드에서만 의미 있음 */
        if (!interp->ont_local) return val_null();
        const char *cls_name = node->sval ? node->sval : "미명";
        /* child[0] = 부모 개념 NODE_IDENT (NULL 이면 없음) */
        const char *parent_name = NULL;
        if (node->child_count >= 1 && node->children[0] &&
            node->children[0]->type == NODE_IDENT) {
            parent_name = node->children[0]->sval;
        }
        KcOntClass *cls = kc_ont_add_class(interp->ont_local, cls_name, parent_name);
        if (!cls) cls = kc_ont_get_class(interp->ont_local, cls_name);

        /* §12-7: 원격 모드 시 KACP 바이너리로 클래스 등록 포워딩 */
        if (interp->ont_remote) {
            int rc = kc_ont_remote_add_class(interp->ont_remote,
                                              cls_name, parent_name);
            if (rc != 0)
                fprintf(stderr, "[온톨로지] 원격 클래스 등록 실패: %s (rc=%d)\n",
                        cls_name, rc);
        }

        /* child[1] = 블록 (속성 선언들) — ont_current_class 설정 후 순회 */
        KcOntClass *prev_cls = interp->ont_current_class;
        interp->ont_current_class = cls;
        if (node->child_count >= 2 && node->children[1]) {
            interp_eval(interp, node->children[1]);
        }
        interp->ont_current_class = prev_cls;
        return val_null();
    }

    case NODE_ONT_PROP: {
        /* NODE_ONT_CONCEPT 처리 중에 ont_current_class가 설정되어 있어야 함 */
        if (!interp->ont_current_class) return val_null();
        const char *prop_name = node->sval ? node->sval
                                           : "\xEC\x86\x8D\xEC\x84\xB1"; /* 속성 */
        /* 타입 토큰 → KcOntValueType */
        KcOntValueType vtype = KC_ONT_VAL_STRING;
        TokenType tt = (TokenType)node->val.ival;
        if      (tt == TOK_KW_JEONGSU) vtype = KC_ONT_VAL_INT;
        else if (tt == TOK_KW_SILSU)   vtype = KC_ONT_VAL_FLOAT;
        else if (tt == TOK_KW_NOLI)    vtype = KC_ONT_VAL_BOOL;

        kc_ont_add_prop(interp->ont_current_class, prop_name, vtype, 0, NULL);

        /* 민감 / 익명화 플래그 설정 */
        KcOntProperty *p = kc_ont_get_prop(interp->ont_current_class, prop_name);
        if (p) {
            if (node->op == TOK_KW_ONT_SENSITIVE) {
                p->sensitive = 1;
                if (!p->sensitive_reason)
                    p->sensitive_reason = strdup("[민감] Kcode 코드에서 지정");
            } else if (node->op == TOK_KW_ONT_ANON) {
                p->anonymize = 1;
            }
        }
        return val_null();
    }

    case NODE_ONT_RELATE: {
        if (!interp->ont_local) return val_null();
        const char *rel_name  = node->sval ? node->sval
                                           : "관계";
        const char *from_name = (node->child_count >= 1 && node->children[0])
                                    ? node->children[0]->sval : NULL;
        const char *to_name   = (node->child_count >= 2 && node->children[1])
                                    ? node->children[1]->sval : NULL;
        kc_ont_add_relation(interp->ont_local, rel_name, from_name, to_name);

        /* §12-7: 원격 모드 시 KACP 바이너리로 관계 등록 포워딩 */
        if (interp->ont_remote) {
            int rc = kc_ont_remote_add_relation(interp->ont_remote,
                                                 rel_name, from_name, to_name);
            if (rc != 0)
                fprintf(stderr, "[온톨로지] 원격 관계 등록 실패: %s (rc=%d)\n",
                        rel_name, rc);
        }
        return val_null();
    }

    case NODE_ONT_QUERY: {
        const char *q_str    = node->sval ? node->sval : "";
        const char *var_name = (node->child_count >= 1 && node->children[0])
                                    ? node->children[0]->sval : NULL;
        char *json_result = NULL;

        if (interp->ont_remote) {
            /* §12-7: KACP 바이너리 우선 질의 경로 */
            char buf[8192] = "";
            if (kc_ont_remote_is_kacp(interp->ont_remote))
                printf("[온톨로지] KACP 바이너리 질의: %s\n", q_str);
            int rc = kc_ont_remote_query(interp->ont_remote, q_str,
                                          buf, sizeof(buf));
            if (rc == 0) {
                json_result = strdup(buf);
            } else {
                fprintf(stderr, "[온톨로지] 원격 질의 실패 (rc=%d): %s\n", rc, q_str);
            }
        } else if (interp->ont_local) {
            /* 모드 0/1: 로컬 질의 */
            KcOntQueryResult *qr = kc_ont_query(interp->ont_local, q_str);
            if (qr) {
                json_result = kc_ont_qr_to_json(qr);
                kc_ont_qr_destroy(qr);
            }
        }

        if (var_name && json_result) {
            Value res_val = val_gc_string(interp, json_result);
            env_define(interp->current, var_name, res_val, interp);
            val_release(interp, &res_val);
        }
        free(json_result);
        return val_null();
    }

    case NODE_ONT_INFER: {
        if (interp->ont_remote) {
            /* §12-7: KACP 바이너리 우선 추론 경로 */
            char buf[1024] = "";
            if (kc_ont_remote_is_kacp(interp->ont_remote))
                printf("[온톨로지] KACP 바이너리 추론 실행\n");
            kc_ont_remote_infer(interp->ont_remote, buf, sizeof(buf));
            printf("[온톨로지] 원격 추론 완료: %s\n", buf);
        } else if (interp->ont_local) {
            int n = kc_ont_reason(interp->ont_local);
            printf("[온톨로지] 추론 완료 — 도출된 사실 %d개\n", n);
        }
        /* 추론 블록 본문 실행 */
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i])
                interp_eval(interp, node->children[i]);
        }
        return val_null();
    }

    /* ── Concept Identity / Vector Space (v22.0.0) ──────────────── */
    case NODE_SEMANTIC_INFER: {
        /* Layer 2 — 추론 전략 실행: kc_op_def 로 실행 계획 수립 후 보고 */
        const char *label = (node->sval && node->sval[0]) ? node->sval : "의미추론";
        printf("[의미추론] 블록 진입: %s\n", label);
        if (interp->ont_local) {
            /* 기본 정책으로 실행 계획 생성 (kc_op_def 연동) */
            printf("[의미추론] 온톨로지 기반 추론 수행 중...\n");
            int n_infer = kc_ont_reason(interp->ont_local);
            printf("[의미추론] 추론 완료 — 도출 사실 %d건\n", n_infer);
        } else {
            printf("[의미추론] 온톨로지 미연결 — 추론 건너뜀\n");
        }
        /* 블록 내부 설정 구문 실행 */
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i])
                interp_eval(interp, node->children[i]);
        }
        return val_null();
    }

    case NODE_VECTORIZE: {
        /* Layer 3 — 온톨로지→텐서 벡터화 (kc_vec_embed_all 연동) */
        const char *label = (node->sval && node->sval[0]) ? node->sval : "벡터화";
        printf("[벡터화] 블록 진입: %s\n", label);
        if (interp->ont_local) {
            /* kc_vec_embed_all: 온톨로지 인스턴스 전체 임베딩 */
            KcVecEmbedResult *emb = kc_vec_embed_all(interp->ont_local, NULL, NULL);
            if (emb) {
                printf("[벡터화] 임베딩 완료 — 인스턴스 %d개 × 차원 %d\n",
                       emb->inst_count, (int)emb->dim);
                kc_vec_embed_result_free(emb);
            } else {
                printf("[벡터화] 임베딩 데이터 없음\n");
            }
        } else {
            printf("[벡터화] 온톨로지 미연결 — 벡터화 건너뜀\n");
        }
        /* 블록 내부 설정 구문 실행 */
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i])
                interp_eval(interp, node->children[i]);
        }
        return val_null();
    }

    case NODE_SEM_RECON: {
        /* Layer 9 — 벡터→의미 역변환 (kc_vec_recon 연동) */
        const char *label = (node->sval && node->sval[0]) ? node->sval : "의미복원";
        printf("[의미복원] 블록 진입: %s\n", label);
        if (interp->ont_local) {
            /* kc_vec_recon_from_vec: 대표 벡터 없으면 NULL → 군집 레이블만 출력 */
            /* kc_vec_recon_cluster_labels(onto, embed, out_labels, cfg, tr) */
            int n_cl = kc_vec_recon_cluster_labels(interp->ont_local, NULL, NULL, NULL, NULL);
            printf("[의미복원] 군집 복원 완료 — %d개\n", n_cl >= 0 ? n_cl : 0);
        } else {
            printf("[의미복원] 온톨로지 미연결 — 복원 건너뜀\n");
        }
        /* 블록 내부 설정 구문 실행 */
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i])
                interp_eval(interp, node->children[i]);
        }
        return val_null();
    }

    case NODE_REPRO_LABEL: {
        /* Layer 6 — 재생산 라벨: 계보ID 자동 기록 (kc_learn 연동) */
        const char *memo = (node->sval && node->sval[0]) ? node->sval : NULL;
        printf("[재생산라벨] 블록 진입%s%s\n",
               memo ? " — 메모: " : "",
               memo ? memo : "");
        if (interp->ont_local) {
            /* 기본 설정으로 학습 사이클 완료 기록 */
            KcLearnConfig cfg = kc_learn_default_config();
            KcLearnResult res = kc_learn_cycle_complete(interp->ont_local,
                                                         NULL, NULL, &cfg);
            printf("[재생산라벨] 사이클 완료 — 코드: %s / 등록 규칙: %d건\n",
                   kc_learn_code_name(res.code), res.rules_registered);
        } else {
            printf("[재생산라벨] 온톨로지 미연결 — 라벨 기록 건너뜀\n");
        }
        /* 블록 내부 설정 구문 실행 */
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i])
                interp_eval(interp, node->children[i]);
        }
        return val_null();
    }

    /* ================================================================
     *  지식 뱅크 노드 (v22.7.0)
     * ================================================================ */

    /* ── NODE_KBANK : 지식뱅크 블록 ────────────────────────────────── */
    case NODE_KBANK: {
        const char *name = (node->sval && node->sval[0]) ? node->sval : "unnamed";
        KcKbank *kb = kc_kbank_create(name, "system",
                                       "UID-001",
                                       KC_KBANK_ID_HUMAN,
                                       1);
        if (!kb) {
            RT_ERROR(interp, "[지식뱅크] 생성 실패: %s", name);
        }
        interp->kbank_current = kb;
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i])
                interp_eval(interp, node->children[i]);
        }
        KcKbankErr ge = kc_kbank_gate1_check(kb);
        if (ge != KC_KBANK_OK) {
            kc_kbank_destroy(kb);
            interp->kbank_current = NULL;
            RT_ERROR(interp, "[지식뱅크] 게이트1 실패 (소유권/재생산라벨 누락)");
        }
        kc_kbank_gate2_scan(kb, NULL, 0);
        if (interp->ont_local)
            kc_kbank_ont_export(kb, interp->ont_local, NULL,
                                KC_KBANK_ONT_FMT_JSON, "system");
        char save_path[KC_KBANK_PATH_LEN];
        snprintf(save_path, sizeof(save_path), "%s.kbank", name);
        KcKbankErr se = kc_kbank_save(kb, save_path, "system");
        if (se == KC_KBANK_OK)
            printf("[지식뱅크] 저장 완료: %s\n", save_path);
        else
            fprintf(stderr, "[지식뱅크] 저장 실패 (코드: %d): %s\n", se, save_path);
        return val_null();
    }

    /* ── NODE_KBANK_LOAD : 지식불러오기 블록 ───────────────────────── */
    case NODE_KBANK_LOAD: {
        const char *path = (node->sval && node->sval[0]) ? node->sval : NULL;
        if (!path)
            RT_ERROR(interp, "[지식불러오기] .kbank 파일 경로 누락");
        const char *actor = "UID-001";
        char actor_buf[KC_KBANK_ID_LEN] = {0};
        if (node->child_count > 0 && node->children[0]) {
            Value av = interp_eval(interp, node->children[0]);
            if (av.type == VAL_STRING) {
                snprintf(actor_buf, sizeof(actor_buf), "%s", av.as.sval);
                actor = actor_buf;
            }
            val_release(interp, &av);
        }
        if (interp->kbank_current) {
            kc_kbank_destroy(interp->kbank_current);
            interp->kbank_current = NULL;
        }
        KcKbank *kb = kc_kbank_load(path, actor);
        if (!kb)
            RT_ERROR(interp, "[지식불러오기] 불러오기 실패: %s", path);
        KcKbankErr g3 = kc_kbank_gate3_check(kb, path);
        if (g3 != KC_KBANK_OK) {
            kc_kbank_destroy(kb);
            RT_ERROR(interp, "[지식불러오기] 게이트3 실패 (검증 이력 불일치)");
        }
        printf("[지식불러오기] 로드 완료: %s\n", path);
        interp->kbank_current = kb;
        return val_null();
    }

    /* ── NODE_KBANK_COMPARE : 지식비교 블록 ────────────────────────── */
    case NODE_KBANK_COMPARE: {
        if (node->child_count < 1 || !node->children[0])
            RT_ERROR(interp, "[지식비교] 비교 대상 뱅크 이름 누락");
        if (!interp->kbank_current)
            RT_ERROR(interp, "[지식비교] 활성 지식뱅크 없음");
        Value v0 = interp_eval(interp, node->children[0]);
        char target_path[KC_KBANK_PATH_LEN] = {0};
        if (v0.type == VAL_STRING)
            snprintf(target_path, sizeof(target_path), "%s.kbank", v0.as.sval);
        else
            snprintf(target_path, sizeof(target_path), "target.kbank");
        val_release(interp, &v0);
        KcMergePolicy policy = KC_MERGE_POLICY_OWNER_WINS;
        if (node->child_count > 1 && node->children[1]) {
            Value vp = interp_eval(interp, node->children[1]);
            if (vp.type == VAL_INT && vp.as.ival >= 0 && vp.as.ival <= 3)
                policy = (KcMergePolicy)vp.as.ival;
            val_release(interp, &vp);
        }
        KcKbank *src = kc_kbank_load(target_path, "system");
        if (!src)
            RT_ERROR(interp, "[지식비교] 비교 대상 로드 실패: %s", target_path);
        KcMergeResult res;
        kc_merge_result_init(&res);
        KcMergeLock lock;
        memset(&lock, 0, sizeof(lock));
        kc_merge_lock_acquire(&lock, interp->kbank_current->label.lineage_id);
        KcMergeErr me = kc_merge_banks(interp->kbank_current, src,
                                        "system", policy, &lock, &res);
        kc_merge_lock_release(&lock);
        kc_kbank_destroy(src);
        printf("[지식비교] 병합 결과: %s — %s\n", kc_merge_err_str(me), res.summary);
        return val_null();
    }

    /* ── NODE_REPRO_LABEL_DECL : 재생산라벨선언 (인라인) ───────────── */
    case NODE_REPRO_LABEL_DECL: {
        const char *memo = (node->sval && node->sval[0]) ? node->sval : NULL;
        printf("[재생산라벨선언]%s%s\n",
               memo ? " 메모: " : " (메모 없음)",
               memo ? memo : "");
        if (interp->kbank_current)
            kc_kbank_label_declare(interp->kbank_current, memo, NULL, 0);
        else
            fprintf(stderr, "[재생산라벨선언] 활성 지식뱅크 없음 — 건너뜀\n");
        return val_null();
    }

    /* ── NODE_KBANK_PROOF : 지식증거출력 블록 ──────────────────────── */
    case NODE_KBANK_PROOF: {
        if (!interp->kbank_current)
            RT_ERROR(interp, "[지식증거출력] 활성 지식뱅크 없음");
        char base_path[KC_KBANK_PATH_LEN];
        if (node->sval && node->sval[0])
            snprintf(base_path, sizeof(base_path), "%s", node->sval);
        else
            snprintf(base_path, sizeof(base_path), "%s_proof",
                     interp->kbank_current->name);
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i])
                interp_eval(interp, node->children[i]);
        }
        KcKbankErr pe = kc_proof_export(interp->kbank_current,
                                         "system", NULL, base_path);
        if (pe == KC_KBANK_OK)
            printf("[지식증거출력] 저장: %s.proof.json / .proof.txt\n", base_path);
        else
            fprintf(stderr, "[지식증거출력] 실패 (코드: %d)\n", pe);
        return val_null();
    }

    default:
        RT_ERROR(interp, "지원하지 않는 AST 노드 종류: %d", node->type);
    }
}

/* ================================================================
 *  함수 호출 구현
 * ================================================================ */
static Value call_function(Interp *interp, Value *callee,
                            Value *args, int argc) {
    /* 내장 함수 */
    if (callee->type == VAL_BUILTIN) {
        return callee->as.builtin(interp, args, argc);
    }

    /* 사용자 정의 함수 / 정의 / 람다 */
    if (callee->type == VAL_FUNC) {
        if (interp->call_depth >= INTERP_MAX_CALL_DEPTH)
            return val_error("[런타임 오류] 호출 스택 초과 (재귀 한도: %d)",
                             INTERP_MAX_CALL_DEPTH);
        interp->call_depth++;

        Node *fn_node = (Node*)callee->gc_obj->data.closure.node;
        Env  *closure = (Env*)callee->gc_obj->data.closure.env;

        /* 새 스코프 — 클로저 환경을 부모로 */
        Env *fn_env = env_new(closure);
        Env *prev   = interp->current;
        interp->current = fn_env;

        /* 매개변수 바인딩 */
        int is_lambda = (fn_node->type == NODE_LAMBDA);
        int param_count = is_lambda ? fn_node->child_count - 1
                                    : fn_node->child_count - 1; /* 마지막 child = 블록 */

        for (int i = 0; i < param_count && i < argc; i++) {
            Node *param = fn_node->children[i];
            /* NODE_PARAM: sval = 이름, child[0] = 기본값(선택) */
            env_define(fn_env, param->sval, args[i], interp);
        }
        /* 부족한 인수는 기본값으로 */
        for (int i = argc; i < param_count; i++) {
            Node *param = fn_node->children[i];
            if (param->child_count > 0) {
                Value def = interp_eval(interp, param->children[0]);
                env_define(fn_env, param->sval, def, interp);
                val_free(&def);
            } else {
                env_define(fn_env, param->sval, val_null(), interp);
            }
        }

        /* ★ 사전조건 검사 — 헌법→법률→규정→법령 계층 순 평가 (v5.0.0) */
        const char *fn_name    = (fn_node->type != NODE_LAMBDA) ? fn_node->sval : NULL;
        const char *class_name = NULL; /* TODO: 메서드 호출 시 클래스명 전달 */
        {
            Value v = check_contracts_layered(interp, fn_name, class_name);
            if (v.type != VAL_NULL) {
                /* 중단/회귀/대체 — 본문 실행 안 함 */
                env_free(fn_env, interp);
                interp->current = prev;
                interp->call_depth--;
                return v;
            }
        }

        /* 본문 실행 */
        Node *body = fn_node->children[fn_node->child_count - 1];
        Value result = interp_eval(interp, body);

        /* 반환 신호 해제 */
        if (result.type == VAL_RETURN) {
            Value ret = val_clone(interp, result.as.retval);
            val_free(&result);
            result = ret;
        } else if (result.type == VAL_BREAK || result.type == VAL_CONTINUE) {
            val_free(&result);
            result = val_null();
        }

        env_free(fn_env, interp);
        interp->current = prev;

        /* ★ 법위반(사후조건) 검사 — 본문 실행 후
         * "반환값" 이름으로 결과를 전역에 임시 등록하여 조건식에서 참조 가능 */
        if (result.type != VAL_ERROR) {
            Value rv_clone = val_clone(interp, &result);
            env_define(interp->global,
                "\xEB\xB0\x98\xED\x99\x98\xEA\xB0\x92", /* 반환값 */
                rv_clone, interp);
            val_release(interp, &rv_clone);

            /* 법위반(사후조건) — 기존 v1 방식 유지하되 계층 평가도 함께 수행 */
            Value v = check_contracts(interp, TOK_KW_BEOPWIBAN, fn_name);
            if (v.type != VAL_NULL) {
                val_release(interp, &result);
                interp->call_depth--;
                return v;
            }
        }

        interp->call_depth--;
        return result;
    }

    return val_error("[런타임 오류] 호출할 수 없는 값입니다");
}

/* ================================================================
 *  interp_run  — 전체 프로그램 실행
 * ================================================================ */
void interp_run(Interp *interp, Node *program) {
    if (!program) return;
    Value v = interp_eval(interp, program);
    if (v.type == VAL_ERROR) {
        fprintf(stderr, "%s\n", v.as.sval);
        interp->had_error = 1;
    }
    val_free(&v);
}
