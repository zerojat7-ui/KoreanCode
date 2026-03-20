/*
 * kc_autograd.h  —  Kcode 자동미분(Autograd) 엔진 헤더
 * version : v13.0.0
 *
 * 테이프 방식(PyTorch 스타일) 동적 연산 그래프 + 역전파 엔진.
 * kc_tensor.h (v12.0.0) 선행 필요.
 *
 * 지원 역전파 연산 10종:
 *   덧셈, 곱셈, 행렬곱, 렐루, 시그모이드, 쌍곡탄젠트,
 *   로그, 합산, 평균, 제곱
 *
 * MIT License
 * zerojat7
 */

#ifndef KCODE_AUTOGRAD_H
#define KCODE_AUTOGRAD_H

#include <stdint.h>
#include "kc_tensor.h"

#define KC_GRADFN_MAX_SAVED  4
#define KC_GRADFN_MAX_NEXT   4

/* ================================================================
 *  GradFn — 계산 그래프 단일 노드
 * ================================================================ */
typedef struct GradFn {
    void (*backward)(struct GradFn *self,
                     const double  *grad_out,
                     int64_t        numel);

    KcTensor     *saved[KC_GRADFN_MAX_SAVED];
    int           n_saved;

    struct GradFn *next_fns[KC_GRADFN_MAX_NEXT];
    int            n_next;

    KcTensor     *input_tensors[KC_GRADFN_MAX_NEXT];
    int           n_inputs;

    int           visited;
} GradFn;

/* ----------------------------------------------------------------
 *  GradFn 생성 / 해제
 * ---------------------------------------------------------------- */
GradFn *kc_gradfn_new(void);
void    kc_gradfn_free(GradFn *fn);

/* ----------------------------------------------------------------
 *  역전파 연산 10종 — 순전파 + GradFn 연결
 * ---------------------------------------------------------------- */
KcTensor *kc_ag_add    (KcTensor *a, KcTensor *b);
KcTensor *kc_ag_mul    (KcTensor *a, KcTensor *b);
KcTensor *kc_ag_matmul (KcTensor *a, KcTensor *b);
KcTensor *kc_ag_relu   (KcTensor *a);
KcTensor *kc_ag_sigmoid(KcTensor *a);
KcTensor *kc_ag_tanh_op(KcTensor *a);
KcTensor *kc_ag_log    (KcTensor *a);
KcTensor *kc_ag_sum    (KcTensor *a);
KcTensor *kc_ag_mean   (KcTensor *a);
KcTensor *kc_ag_pow2   (KcTensor *a);

/* ----------------------------------------------------------------
 *  역전파 실행 API
 * ---------------------------------------------------------------- */
void kc_autograd_backward (KcTensor *root);
void kc_autograd_zero_grad(KcTensor *t);

/* ----------------------------------------------------------------
 *  내부 유틸리티 (kc_tensor.c 연동)
 * ---------------------------------------------------------------- */
void kc_tensor_ensure_grad(KcTensor *t);
void kc_tensor_accum_grad (KcTensor *t, const double *delta, int64_t numel);

#endif /* KCODE_AUTOGRAD_H */
