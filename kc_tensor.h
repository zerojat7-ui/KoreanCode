/*
 * kc_tensor.h  —  Kcode 텐서(Tensor) 자료형 헤더
 * version : v14.0.1
 *
 * v11.0.0:
 *   - KcTensor 구조체 (N차원, GC 통합)
 *   - 생성 함수 4종 : kc_tensor_create / kc_tensor_zeros / kc_tensor_ones / kc_tensor_rand
 *   - 변형 함수 3종 : kc_tensor_reshape / kc_tensor_transpose / kc_tensor_flatten
 *   - 원소별 연산 4종 : kc_tensor_add / kc_tensor_sub / kc_tensor_mul / kc_tensor_div
 *   - 행렬곱 1종     : kc_tensor_matmul
 *   - 축 연산 2종    : kc_tensor_sum_axis / kc_tensor_mean_axis
 *   - 브로드캐스팅 지원 (strides 기반)
 *   - GC 참조 카운트 통합
 *
 * MIT License
 * zerojat7
 */

#ifndef KC_TENSOR_H
#define KC_TENSOR_H

#include <stdint.h>
#include <stddef.h>

/* ================================================================
 *  KcTensor 구조체
 * ================================================================ */
typedef struct KcTensor {
    double   *data;          /* 데이터 버퍼 (1D 연속 배열, C 순서) */
    int64_t  *shape;         /* 형태 배열 [d0, d1, ...] */
    int64_t  *strides;       /* 스트라이드 배열 (브로드캐스팅용, 원소 단위) */
    int       ndim;          /* 차원 수 */
    int64_t   numel;         /* 전체 원소 수 (shape 곱) */
    int       requires_grad; /* 자동미분 추적 여부 (v12.0.0 연동) */
    double   *grad;          /* 기울기 버퍼 (v12.0.0 연동, NULL 가능) */
    void     *grad_fn;       /* 역전파 함수 포인터 (v12.0.0 연동, NULL 가능) */
    int       ref_count;     /* GC 참조 카운트 */
} KcTensor;

/* ================================================================
 *  생성 / 해제 함수
 * ================================================================ */

/*
 * kc_tensor_create — 데이터 배열 + 형태 배열로 텐서 생성
 *   data_in  : 초기값 배열 (numel 개, 복사됨). NULL 이면 0으로 초기화.
 *   shape_in : 형태 배열 (ndim 개)
 *   ndim     : 차원 수
 * 반환값: 힙 할당된 KcTensor*, 실패 시 NULL
 */
KcTensor *kc_tensor_create(const double *data_in,
                            const int64_t *shape_in,
                            int ndim);

/*
 * kc_tensor_zeros — 모든 원소 0으로 초기화된 텐서
 */
KcTensor *kc_tensor_zeros(const int64_t *shape_in, int ndim);

/*
 * kc_tensor_ones — 모든 원소 1로 초기화된 텐서
 */
KcTensor *kc_tensor_ones(const int64_t *shape_in, int ndim);

/*
 * kc_tensor_rand — [0,1) 균일 난수로 초기화된 텐서
 */
KcTensor *kc_tensor_rand(const int64_t *shape_in, int ndim);

/*
 * kc_tensor_free — 텐서 메모리 해제 (ref_count 0일 때 호출)
 */
void kc_tensor_free(KcTensor *t);

/*
 * kc_tensor_retain / kc_tensor_release — GC 참조 카운트 관리
 */
void kc_tensor_retain(KcTensor *t);
void kc_tensor_release(KcTensor *t);

/* ================================================================
 *  변형 함수
 * ================================================================ */

/*
 * kc_tensor_reshape — 형태 변환 (numel 동일해야 함)
 * 새 텐서 반환 (데이터 공유 아님 — 복사)
 */
KcTensor *kc_tensor_reshape(const KcTensor *t,
                             const int64_t *new_shape,
                             int new_ndim);

/*
 * kc_tensor_transpose — 전치 (2D만 지원, 고차원은 축 역순)
 */
KcTensor *kc_tensor_transpose(const KcTensor *t);

/*
 * kc_tensor_flatten — 1D 텐서로 변환 (reshape([numel]))
 */
KcTensor *kc_tensor_flatten(const KcTensor *t);

/* ================================================================
 *  원소별 연산 (브로드캐스팅 자동)
 * ================================================================ */
KcTensor *kc_tensor_add(const KcTensor *a, const KcTensor *b);
KcTensor *kc_tensor_sub(const KcTensor *a, const KcTensor *b);
KcTensor *kc_tensor_mul(const KcTensor *a, const KcTensor *b);
KcTensor *kc_tensor_div(const KcTensor *a, const KcTensor *b);

/* ================================================================
 *  행렬곱
 * ================================================================ */
/*
 * kc_tensor_matmul — 행렬곱 A @ B
 * A: (M, K), B: (K, N) → 결과: (M, N)
 * 현재 2D만 지원.
 */
KcTensor *kc_tensor_matmul(const KcTensor *a, const KcTensor *b);

/* ================================================================
 *  축(axis) 연산
 * ================================================================ */
/*
 * kc_tensor_sum_axis  — 특정 축 방향 합산
 * kc_tensor_mean_axis — 특정 축 방향 평균
 * axis < 0 이면 전체 합산/평균 (스칼라 텐서 반환)
 */
KcTensor *kc_tensor_sum_axis(const KcTensor *t, int axis);
KcTensor *kc_tensor_mean_axis(const KcTensor *t, int axis);

/* ================================================================
 *  유틸리티
 * ================================================================ */

/*
 * kc_tensor_print — 텐서 내용 출력 (디버깅용)
 */
void kc_tensor_print(const KcTensor *t);

/*
 * kc_tensor_shape_str — "[ d0 x d1 x ... ]" 형태 문자열 반환
 * 반환값: 힙 할당 — 호출자가 free
 */
char *kc_tensor_shape_str(const KcTensor *t);

/*
 * kc_compute_strides — C 순서(row-major) 스트라이드 계산
 * (strides_out 은 ndim 크기의 사전 할당된 배열)
 */
void kc_compute_strides(const int64_t *shape, int ndim, int64_t *strides_out);

/*
 * kc_tensor_copy — 텐서 딥 복사
 */
KcTensor *kc_tensor_copy(const KcTensor *t);

/* ================================================================
 *  autograd 연동 헬퍼 (v14.0.1 — kc_autograd.c 대응)
 * ================================================================ */

/*
 * kc_tensor_sum — 전체 원소 합산 (스칼라 반환)
 */
double kc_tensor_sum(const KcTensor *t);

/*
 * kc_tensor_mean — 전체 원소 평균 (스칼라 반환)
 */
double kc_tensor_mean(const KcTensor *t);

/*
 * kc_tensor_from_data — 외부 버퍼 포인터 + 형태 배열로 텐서 생성
 * data_ptr : 복사할 데이터 포인터 (ndim=1, numel=1 이면 &scalar 전달)
 * shape    : 형태 배열 (ndim 개 원소)
 * ndim     : 차원 수
 */
KcTensor *kc_tensor_from_data(const double *data_ptr,
                               const int64_t *shape,
                               int ndim);

/* §ACC 호환성 — kc_vm.c 참조 함수 선언 (v27.0.0) */
KcTensor *kc_tensor_from_flat(const double *data, int64_t n,
                               const int64_t *shape, int ndim);
int64_t   kc_tensor_numel(const KcTensor *t);
int       kc_tensor_ndim(const KcTensor *t);
double    kc_tensor_max(const KcTensor *t);
double    kc_tensor_min(const KcTensor *t);

#endif /* KC_TENSOR_H */
