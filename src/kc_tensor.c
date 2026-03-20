/*
 * kc_tensor.c  —  Kcode 텐서(Tensor) 자료형 구현
 * version : v14.0.1
 *
 * v11.0.0:
 *   - KcTensor 생성/변형/연산/축 연산 전체 구현
 *   - 브로드캐스팅: 두 텐서 형태가 다를 때 자동 확장
 *   - GC 참조 카운트 통합 (retain / release)
 *   - 행렬곱 2D (M×K @ K×N → M×N)
 *   - 축 연산 sum_axis / mean_axis
 *
 * MIT License
 * zerojat7
 */

#include "kc_tensor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* ================================================================
 *  내부 헬퍼
 * ================================================================ */

/* numel = shape 원소 곱 */
static int64_t compute_numel(const int64_t *shape, int ndim) {
    int64_t n = 1;
    for (int i = 0; i < ndim; i++) n *= shape[i];
    return n;
}

/* C 순서(row-major) 스트라이드 계산 */
void kc_compute_strides(const int64_t *shape, int ndim, int64_t *strides_out) {
    if (ndim == 0) return;
    strides_out[ndim - 1] = 1;
    for (int i = ndim - 2; i >= 0; i--) {
        strides_out[i] = strides_out[i + 1] * shape[i + 1];
    }
}

/* 브로드캐스팅 가능 여부 확인 + 결과 형태 계산
 * ndim_out, shape_out 은 최대 32 차원
 * 반환값: 1=가능, 0=불가
 */
#define KC_MAX_NDIM 32
static int broadcast_check(const KcTensor *a, const KcTensor *b,
                            int *ndim_out, int64_t *shape_out) {
    int na = a->ndim;
    int nb = b->ndim;
    int nout = (na > nb) ? na : nb;
    *ndim_out = nout;
    for (int i = 0; i < nout; i++) {
        int ai = na - nout + i;
        int bi = nb - nout + i;
        int64_t da = (ai >= 0) ? a->shape[ai] : 1;
        int64_t db = (bi >= 0) ? b->shape[bi] : 1;
        if (da == db) {
            shape_out[i] = da;
        } else if (da == 1) {
            shape_out[i] = db;
        } else if (db == 1) {
            shape_out[i] = da;
        } else {
            return 0; /* 브로드캐스팅 불가 */
        }
    }
    return 1;
}

/* 선형 인덱스 → 다차원 인덱스 배열로 변환 */
static void linear_to_multi(int64_t idx, const int64_t *shape, int ndim,
                             int64_t *multi_out) {
    for (int i = ndim - 1; i >= 0; i--) {
        multi_out[i] = idx % shape[i];
        idx /= shape[i];
    }
}

/* 다차원 인덱스 + 브로드캐스팅 오프셋 → 원본 텐서 선형 인덱스 */
static int64_t broadcast_offset(const KcTensor *t, const int64_t *multi,
                                 int result_ndim) {
    int offset_dim = result_ndim - t->ndim;
    int64_t off = 0;
    for (int i = 0; i < t->ndim; i++) {
        int64_t idx = multi[offset_dim + i];
        if (t->shape[i] == 1) idx = 0; /* 브로드캐스팅 */
        off += idx * t->strides[i];
    }
    return off;
}

/* ================================================================
 *  생성 / 해제
 * ================================================================ */

KcTensor *kc_tensor_create(const double *data_in,
                            const int64_t *shape_in,
                            int ndim) {
    if (ndim <= 0 || ndim > KC_MAX_NDIM || !shape_in) return NULL;

    KcTensor *t = (KcTensor *)calloc(1, sizeof(KcTensor));
    if (!t) return NULL;

    t->ndim = ndim;
    t->shape = (int64_t *)malloc(sizeof(int64_t) * (size_t)ndim);
    t->strides = (int64_t *)malloc(sizeof(int64_t) * (size_t)ndim);
    if (!t->shape || !t->strides) { kc_tensor_free(t); return NULL; }

    for (int i = 0; i < ndim; i++) t->shape[i] = shape_in[i];
    t->numel = compute_numel(shape_in, ndim);
    kc_compute_strides(shape_in, ndim, t->strides);

    t->data = (double *)calloc((size_t)t->numel, sizeof(double));
    if (!t->data) { kc_tensor_free(t); return NULL; }

    if (data_in) {
        memcpy(t->data, data_in, sizeof(double) * (size_t)t->numel);
    }

    t->ref_count = 1;
    t->requires_grad = 0;
    t->grad = NULL;
    t->grad_fn = NULL;
    return t;
}

KcTensor *kc_tensor_zeros(const int64_t *shape_in, int ndim) {
    return kc_tensor_create(NULL, shape_in, ndim);
}

KcTensor *kc_tensor_ones(const int64_t *shape_in, int ndim) {
    KcTensor *t = kc_tensor_zeros(shape_in, ndim);
    if (!t) return NULL;
    for (int64_t i = 0; i < t->numel; i++) t->data[i] = 1.0;
    return t;
}

KcTensor *kc_tensor_rand(const int64_t *shape_in, int ndim) {
    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }

    KcTensor *t = kc_tensor_zeros(shape_in, ndim);
    if (!t) return NULL;
    for (int64_t i = 0; i < t->numel; i++) {
        t->data[i] = (double)rand() / ((double)RAND_MAX + 1.0);
    }
    return t;
}

void kc_tensor_free(KcTensor *t) {
    if (!t) return;
    free(t->data);
    free(t->shape);
    free(t->strides);
    free(t->grad);
    /* grad_fn 은 v12.0.0 에서 관리 */
    free(t);
}

void kc_tensor_retain(KcTensor *t) {
    if (t) t->ref_count++;
}

void kc_tensor_release(KcTensor *t) {
    if (!t) return;
    t->ref_count--;
    if (t->ref_count <= 0) kc_tensor_free(t);
}

/* ================================================================
 *  딥 복사
 * ================================================================ */

KcTensor *kc_tensor_copy(const KcTensor *t) {
    if (!t) return NULL;
    return kc_tensor_create(t->data, t->shape, t->ndim);
}

/* ================================================================
 *  변형 함수
 * ================================================================ */

KcTensor *kc_tensor_reshape(const KcTensor *t,
                             const int64_t *new_shape,
                             int new_ndim) {
    if (!t || !new_shape || new_ndim <= 0) return NULL;
    int64_t new_numel = compute_numel(new_shape, new_ndim);
    if (new_numel != t->numel) {
        fprintf(stderr, "[텐서] 모양바꾸기 오류: 원소 수 불일치 (%lld != %lld)\n",
                (long long)t->numel, (long long)new_numel);
        return NULL;
    }
    return kc_tensor_create(t->data, new_shape, new_ndim);
}

KcTensor *kc_tensor_transpose(const KcTensor *t) {
    if (!t) return NULL;

    /* 형태 역순 */
    int64_t new_shape[KC_MAX_NDIM];
    for (int i = 0; i < t->ndim; i++) {
        new_shape[i] = t->shape[t->ndim - 1 - i];
    }

    KcTensor *out = kc_tensor_zeros(new_shape, t->ndim);
    if (!out) return NULL;

    /* 각 원소 복사 (2D 특수화 + 일반 N차원) */
    if (t->ndim == 2) {
        int64_t R = t->shape[0], C = t->shape[1];
        for (int64_t r = 0; r < R; r++) {
            for (int64_t c = 0; c < C; c++) {
                out->data[c * R + r] = t->data[r * C + c];
            }
        }
    } else {
        int64_t multi[KC_MAX_NDIM];
        int64_t orig_multi[KC_MAX_NDIM];
        for (int64_t idx = 0; idx < t->numel; idx++) {
            linear_to_multi(idx, new_shape, t->ndim, multi);
            for (int i = 0; i < t->ndim; i++) {
                orig_multi[i] = multi[t->ndim - 1 - i];
            }
            int64_t src = 0;
            for (int i = 0; i < t->ndim; i++) {
                src += orig_multi[i] * t->strides[i];
            }
            out->data[idx] = t->data[src];
        }
    }
    return out;
}

KcTensor *kc_tensor_flatten(const KcTensor *t) {
    if (!t) return NULL;
    int64_t shape1d[1] = { t->numel };
    return kc_tensor_reshape(t, shape1d, 1);
}

/* ================================================================
 *  원소별 연산 (브로드캐스팅)
 * ================================================================ */

typedef double (*ElemOp)(double a, double b);

static double op_add(double a, double b) { return a + b; }
static double op_sub(double a, double b) { return a - b; }
static double op_mul(double a, double b) { return a * b; }
static double op_div(double a, double b) { return a / b; }

static KcTensor *elemwise(const KcTensor *a, const KcTensor *b, ElemOp op) {
    if (!a || !b) return NULL;

    int64_t out_shape[KC_MAX_NDIM];
    int out_ndim = 0;
    if (!broadcast_check(a, b, &out_ndim, out_shape)) {
        fprintf(stderr, "[텐서] 브로드캐스팅 오류: 형태 불일치\n");
        return NULL;
    }

    KcTensor *out = kc_tensor_zeros(out_shape, out_ndim);
    if (!out) return NULL;

    int64_t multi[KC_MAX_NDIM];
    for (int64_t idx = 0; idx < out->numel; idx++) {
        linear_to_multi(idx, out_shape, out_ndim, multi);
        double va = a->data[broadcast_offset(a, multi, out_ndim)];
        double vb = b->data[broadcast_offset(b, multi, out_ndim)];
        out->data[idx] = op(va, vb);
    }
    return out;
}

KcTensor *kc_tensor_add(const KcTensor *a, const KcTensor *b) { return elemwise(a, b, op_add); }
KcTensor *kc_tensor_sub(const KcTensor *a, const KcTensor *b) { return elemwise(a, b, op_sub); }
KcTensor *kc_tensor_mul(const KcTensor *a, const KcTensor *b) { return elemwise(a, b, op_mul); }
KcTensor *kc_tensor_div(const KcTensor *a, const KcTensor *b) { return elemwise(a, b, op_div); }

/* ================================================================
 *  행렬곱 (2D: M×K @ K×N → M×N)
 * ================================================================ */

KcTensor *kc_tensor_matmul(const KcTensor *a, const KcTensor *b) {
    if (!a || !b) return NULL;
    if (a->ndim != 2 || b->ndim != 2) {
        fprintf(stderr, "[텐서] 행렬곱 오류: 2D 텐서만 지원 (a=%dD, b=%dD)\n",
                a->ndim, b->ndim);
        return NULL;
    }
    int64_t M = a->shape[0];
    int64_t K = a->shape[1];
    int64_t K2 = b->shape[0];
    int64_t N = b->shape[1];
    if (K != K2) {
        fprintf(stderr, "[텐서] 행렬곱 오류: 내부 차원 불일치 (%lld != %lld)\n",
                (long long)K, (long long)K2);
        return NULL;
    }
    int64_t out_shape[2] = { M, N };
    KcTensor *out = kc_tensor_zeros(out_shape, 2);
    if (!out) return NULL;

    for (int64_t m = 0; m < M; m++) {
        for (int64_t n = 0; n < N; n++) {
            double sum = 0.0;
            for (int64_t k = 0; k < K; k++) {
                sum += a->data[m * K + k] * b->data[k * N + n];
            }
            out->data[m * N + n] = sum;
        }
    }
    return out;
}

/* ================================================================
 *  축 연산
 * ================================================================ */

KcTensor *kc_tensor_sum_axis(const KcTensor *t, int axis) {
    if (!t) return NULL;

    /* axis < 0: 전체 합산 → 스칼라 텐서 */
    if (axis < 0) {
        double total = 0.0;
        for (int64_t i = 0; i < t->numel; i++) total += t->data[i];
        int64_t s1[1] = { 1 };
        KcTensor *out = kc_tensor_zeros(s1, 1);
        if (out) out->data[0] = total;
        return out;
    }

    if (axis >= t->ndim) {
        fprintf(stderr, "[텐서] 합산 오류: axis=%d >= ndim=%d\n", axis, t->ndim);
        return NULL;
    }

    /* 결과 형태: axis 차원 제거 */
    int new_ndim = t->ndim - 1;
    if (new_ndim == 0) {
        int64_t s1[1] = { 1 };
        KcTensor *out = kc_tensor_zeros(s1, 1);
        if (out) {
            double total = 0.0;
            for (int64_t i = 0; i < t->numel; i++) total += t->data[i];
            out->data[0] = total;
        }
        return out;
    }

    int64_t new_shape[KC_MAX_NDIM];
    int j = 0;
    for (int i = 0; i < t->ndim; i++) {
        if (i != axis) new_shape[j++] = t->shape[i];
    }

    KcTensor *out = kc_tensor_zeros(new_shape, new_ndim);
    if (!out) return NULL;

    int64_t multi[KC_MAX_NDIM];
    for (int64_t idx = 0; idx < t->numel; idx++) {
        linear_to_multi(idx, t->shape, t->ndim, multi);
        /* 결과 인덱스: axis 제거 */
        int64_t out_idx = 0;
        int64_t stride = 1;
        for (int i = t->ndim - 1; i >= 0; i--) {
            if (i == axis) continue;
            out_idx += multi[i] * stride;
            stride *= (i < axis ? t->shape[i] :
                       (i > axis ? new_shape[i - 1] : 1));
        }
        /* 간단한 선형 인덱스 재계산 */
        int64_t oi = 0;
        int64_t st = 1;
        for (int i = new_ndim - 1; i >= 0; i--) {
            int orig_i = (i < axis) ? i : i + 1;
            oi += multi[orig_i] * st;
            st *= new_shape[i];
        }
        out->data[oi] += t->data[idx];
    }
    return out;
}

KcTensor *kc_tensor_mean_axis(const KcTensor *t, int axis) {
    KcTensor *sum = kc_tensor_sum_axis(t, axis);
    if (!sum) return NULL;

    int64_t divisor = (axis < 0) ? t->numel :
                      ((axis < t->ndim) ? t->shape[axis] : 1);
    if (divisor <= 0) divisor = 1;

    for (int64_t i = 0; i < sum->numel; i++) {
        sum->data[i] /= (double)divisor;
    }
    return sum;
}

/* ================================================================
 *  유틸리티
 * ================================================================ */

void kc_tensor_print(const KcTensor *t) {
    if (!t) { printf("텐서(없음)\n"); return; }
    char *shp = kc_tensor_shape_str(t);
    printf("텐서 형태=%s 원소수=%lld\n",
           shp ? shp : "?", (long long)t->numel);
    free(shp);

    /* 최대 16개만 출력 */
    int64_t show = (t->numel < 16) ? t->numel : 16;
    printf("[");
    for (int64_t i = 0; i < show; i++) {
        printf("%.4g", t->data[i]);
        if (i < show - 1) printf(", ");
    }
    if (t->numel > 16) printf(", ...");
    printf("]\n");
}

char *kc_tensor_shape_str(const KcTensor *t) {
    if (!t) return NULL;
    /* 최대 "[ 2 x 3 x 4 x ... ]" 형태, 각 숫자 최대 20자 */
    size_t buf_size = (size_t)(t->ndim * 22 + 8);
    char *buf = (char *)malloc(buf_size);
    if (!buf) return NULL;
    buf[0] = '[';
    buf[1] = ' ';
    buf[2] = '\0';
    for (int i = 0; i < t->ndim; i++) {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%lld", (long long)t->shape[i]);
        strncat(buf, tmp, buf_size - strlen(buf) - 1);
        if (i < t->ndim - 1) strncat(buf, " x ", buf_size - strlen(buf) - 1);
    }
    strncat(buf, " ]", buf_size - strlen(buf) - 1);
    return buf;
}

/* ================================================================
 *  autograd 연동 헬퍼 구현 (v14.0.1)
 * ================================================================ */

/* 전체 원소 합산 → 스칼라 반환 */
double kc_tensor_sum(const KcTensor *t) {
    if (!t || t->numel == 0) return 0.0;
    double s = 0.0;
    for (int64_t i = 0; i < t->numel; i++) s += t->data[i];
    return s;
}

/* 전체 원소 평균 → 스칼라 반환 */
double kc_tensor_mean(const KcTensor *t) {
    if (!t || t->numel == 0) return 0.0;
    return kc_tensor_sum(t) / (double)t->numel;
}

/* 외부 버퍼 포인터 + 형태 배열로 텐서 생성 */
KcTensor *kc_tensor_from_data(const double *data_ptr,
                               const int64_t *shape,
                               int ndim) {
    if (!data_ptr || !shape || ndim <= 0) return NULL;
    return kc_tensor_create(data_ptr, shape, ndim);
}

/* §ACC 호환성 스텁 — kc_vm.c / kc_interp.c 참조 */
#include "kc_tensor.h"

int64_t kc_tensor_numel(const KcTensor *t) {
    if (!t) return 0;
    return t->numel;
}
int kc_tensor_ndim(const KcTensor *t) {
    if (!t) return 0;
    return (int)t->ndim;
}
double kc_tensor_max(const KcTensor *t) {
    if (!t || t->numel == 0) return 0.0;
    double m = t->data[0];
    for (int64_t i = 1; i < t->numel; i++)
        if (t->data[i] > m) m = t->data[i];
    return m;
}
double kc_tensor_min(const KcTensor *t) {
    if (!t || t->numel == 0) return 0.0;
    double m = t->data[0];
    for (int64_t i = 1; i < t->numel; i++)
        if (t->data[i] < m) m = t->data[i];
    return m;
}
KcTensor *kc_tensor_from_flat(const double *data, int64_t n,
                               const int64_t *shape, int ndim) {
    if (!data || n <= 0) return NULL;
    if (!shape || ndim <= 0) {
        int64_t s[1] = { n };
        double *buf = (double *)malloc(sizeof(double) * (size_t)n);
        if (!buf) return NULL;
        for (int64_t i = 0; i < n; i++) buf[i] = data[i];
        KcTensor *t = kc_tensor_create(buf, s, 1);
        free(buf);
        return t;
    }
    double *buf = (double *)malloc(sizeof(double) * (size_t)n);
    if (!buf) return NULL;
    for (int64_t i = 0; i < n; i++) buf[i] = data[i];
    KcTensor *t = kc_tensor_create(buf, shape, ndim);
    free(buf);
    return t;
}
