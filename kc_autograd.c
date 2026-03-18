/*
 * kc_autograd.c  —  Kcode 자동미분(Autograd) 엔진 구현
 * version : v13.0.0
 *
 * MIT License / zerojat7
 */

#include "kc_autograd.h"
#include "kc_tensor.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ================================================================
 *  내부 유틸리티
 * ================================================================ */

void kc_tensor_ensure_grad(KcTensor *t)
{
    if (!t) return;
    if (!t->grad) {
        t->grad = (double *)calloc((size_t)t->numel, sizeof(double));
    }
}

void kc_tensor_accum_grad(KcTensor *t, const double *delta, int64_t numel)
{
    if (!t || !t->requires_grad) return;
    kc_tensor_ensure_grad(t);
    int64_t n = (numel < t->numel) ? numel : t->numel;
    for (int64_t i = 0; i < n; i++) {
        t->grad[i] += delta[i];
    }
}

/* ================================================================
 *  GradFn 생성 / 해제
 * ================================================================ */

GradFn *kc_gradfn_new(void)
{
    GradFn *fn = (GradFn *)calloc(1, sizeof(GradFn));
    return fn;
}

void kc_gradfn_free(GradFn *fn)
{
    if (!fn) return;
    /* saved 텐서는 원본이 관리하므로 포인터만 NULL 처리 */
    for (int i = 0; i < KC_GRADFN_MAX_SAVED; i++) fn->saved[i] = NULL;
    free(fn);
}

/* ================================================================
 *  역전파 함수 10종 구현
 * ================================================================ */

/* ── 덧셈 ─────────────────────────────────────────────────────── */
static void backward_add(GradFn *self, const double *grad_out, int64_t numel)
{
    /* da += grad_out, db += grad_out */
    if (self->input_tensors[0]) kc_tensor_accum_grad(self->input_tensors[0], grad_out, numel);
    if (self->input_tensors[1]) kc_tensor_accum_grad(self->input_tensors[1], grad_out, numel);
}

KcTensor *kc_ag_add(KcTensor *a, KcTensor *b)
{
    KcTensor *z = kc_tensor_add(a, b);
    if (!z) return NULL;
    int need_grad = (a && a->requires_grad) || (b && b->requires_grad);
    if (!need_grad) return z;

    z->requires_grad = 1;
    GradFn *fn = kc_gradfn_new();
    fn->backward = backward_add;
    fn->input_tensors[0] = a;
    fn->input_tensors[1] = b;
    fn->n_inputs = 2;
    /* 연결: 이전 grad_fn */
    if (a && a->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)a->grad_fn; }
    if (b && b->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)b->grad_fn; }
    z->grad_fn = fn;
    return z;
}

/* ── 곱셈 ─────────────────────────────────────────────────────── */
static void backward_mul(GradFn *self, const double *grad_out, int64_t numel)
{
    KcTensor *a = self->saved[0];
    KcTensor *b = self->saved[1];
    /* da += grad * b->data */
    if (self->input_tensors[0] && b) {
        double *da = (double *)malloc((size_t)numel * sizeof(double));
        if (da) {
            for (int64_t i = 0; i < numel; i++) da[i] = grad_out[i] * b->data[i];
            kc_tensor_accum_grad(self->input_tensors[0], da, numel);
            free(da);
        }
    }
    /* db += grad * a->data */
    if (self->input_tensors[1] && a) {
        double *db = (double *)malloc((size_t)numel * sizeof(double));
        if (db) {
            for (int64_t i = 0; i < numel; i++) db[i] = grad_out[i] * a->data[i];
            kc_tensor_accum_grad(self->input_tensors[1], db, numel);
            free(db);
        }
    }
}

KcTensor *kc_ag_mul(KcTensor *a, KcTensor *b)
{
    KcTensor *z = kc_tensor_mul(a, b);
    if (!z) return NULL;
    int need_grad = (a && a->requires_grad) || (b && b->requires_grad);
    if (!need_grad) return z;

    z->requires_grad = 1;
    GradFn *fn = kc_gradfn_new();
    fn->backward = backward_mul;
    /* 입력값 저장 */
    fn->saved[0] = kc_tensor_copy(a);
    fn->saved[1] = kc_tensor_copy(b);
    fn->n_saved  = 2;
    fn->input_tensors[0] = a;
    fn->input_tensors[1] = b;
    fn->n_inputs = 2;
    if (a && a->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)a->grad_fn; }
    if (b && b->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)b->grad_fn; }
    z->grad_fn = fn;
    return z;
}

/* ── 행렬곱 ───────────────────────────────────────────────────── */
static void backward_matmul(GradFn *self, const double *grad_out, int64_t numel)
{
    KcTensor *a  = self->saved[0];  /* [m, k] */
    KcTensor *b  = self->saved[1];  /* [k, n] */
    KcTensor *ta = self->input_tensors[0];
    KcTensor *tb = self->input_tensors[1];

    if (!a || !b) return;

    int64_t m = a->shape[0], k = a->shape[1], n = b->shape[1];
    /* G: [m, n] — grad_out */

    /* dA = G @ Bᵀ  → [m, n] @ [n, k] = [m, k] */
    if (ta && ta->requires_grad) {
        double *dA = (double *)calloc((size_t)(m * k), sizeof(double));
        if (dA) {
            for (int64_t i = 0; i < m; i++)
                for (int64_t j = 0; j < k; j++)
                    for (int64_t l = 0; l < n; l++)
                        dA[i * k + j] += grad_out[i * n + l] * b->data[j * n + l];
            kc_tensor_accum_grad(ta, dA, m * k);
            free(dA);
        }
    }
    /* dB = Aᵀ @ G  → [k, m] @ [m, n] = [k, n] */
    if (tb && tb->requires_grad) {
        double *dB = (double *)calloc((size_t)(k * n), sizeof(double));
        if (dB) {
            for (int64_t i = 0; i < k; i++)
                for (int64_t j = 0; j < n; j++)
                    for (int64_t l = 0; l < m; l++)
                        dB[i * n + j] += a->data[l * k + i] * grad_out[l * n + j];
            kc_tensor_accum_grad(tb, dB, k * n);
            free(dB);
        }
    }
    (void)numel;
}

KcTensor *kc_ag_matmul(KcTensor *a, KcTensor *b)
{
    KcTensor *z = kc_tensor_matmul(a, b);
    if (!z) return NULL;
    int need_grad = (a && a->requires_grad) || (b && b->requires_grad);
    if (!need_grad) return z;

    z->requires_grad = 1;
    GradFn *fn = kc_gradfn_new();
    fn->backward = backward_matmul;
    fn->saved[0] = kc_tensor_copy(a);
    fn->saved[1] = kc_tensor_copy(b);
    fn->n_saved  = 2;
    fn->input_tensors[0] = a;
    fn->input_tensors[1] = b;
    fn->n_inputs = 2;
    if (a && a->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)a->grad_fn; }
    if (b && b->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)b->grad_fn; }
    z->grad_fn = fn;
    return z;
}

/* ── 렐루 ─────────────────────────────────────────────────────── */
static void backward_relu(GradFn *self, const double *grad_out, int64_t numel)
{
    KcTensor *a = self->saved[0];
    if (!a || !self->input_tensors[0]) return;
    double *da = (double *)malloc((size_t)numel * sizeof(double));
    if (!da) return;
    for (int64_t i = 0; i < numel; i++)
        da[i] = (a->data[i] > 0.0) ? grad_out[i] : 0.0;
    kc_tensor_accum_grad(self->input_tensors[0], da, numel);
    free(da);
}

KcTensor *kc_ag_relu(KcTensor *a)
{
    if (!a) return NULL;
    KcTensor *z = kc_tensor_copy(a);
    for (int64_t i = 0; i < z->numel; i++)
        if (z->data[i] < 0.0) z->data[i] = 0.0;

    if (!a->requires_grad) return z;
    z->requires_grad = 1;
    GradFn *fn = kc_gradfn_new();
    fn->backward = backward_relu;
    fn->saved[0] = kc_tensor_copy(a);
    fn->n_saved  = 1;
    fn->input_tensors[0] = a;
    fn->n_inputs = 1;
    if (a->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)a->grad_fn; }
    z->grad_fn = fn;
    return z;
}

/* ── 시그모이드 ───────────────────────────────────────────────── */
static void backward_sigmoid(GradFn *self, const double *grad_out, int64_t numel)
{
    KcTensor *z_saved = self->saved[0]; /* 순전파 출력 z = σ(a) */
    if (!z_saved || !self->input_tensors[0]) return;
    double *da = (double *)malloc((size_t)numel * sizeof(double));
    if (!da) return;
    for (int64_t i = 0; i < numel; i++) {
        double s = z_saved->data[i];
        da[i] = grad_out[i] * s * (1.0 - s);
    }
    kc_tensor_accum_grad(self->input_tensors[0], da, numel);
    free(da);
}

KcTensor *kc_ag_sigmoid(KcTensor *a)
{
    if (!a) return NULL;
    KcTensor *z = kc_tensor_copy(a);
    for (int64_t i = 0; i < z->numel; i++)
        z->data[i] = 1.0 / (1.0 + exp(-z->data[i]));

    if (!a->requires_grad) return z;
    z->requires_grad = 1;
    GradFn *fn = kc_gradfn_new();
    fn->backward = backward_sigmoid;
    fn->saved[0] = kc_tensor_copy(z); /* 출력값 저장 */
    fn->n_saved  = 1;
    fn->input_tensors[0] = a;
    fn->n_inputs = 1;
    if (a->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)a->grad_fn; }
    z->grad_fn = fn;
    return z;
}

/* ── 쌍곡탄젠트 ───────────────────────────────────────────────── */
static void backward_tanh(GradFn *self, const double *grad_out, int64_t numel)
{
    KcTensor *z_saved = self->saved[0]; /* z = tanh(a) */
    if (!z_saved || !self->input_tensors[0]) return;
    double *da = (double *)malloc((size_t)numel * sizeof(double));
    if (!da) return;
    for (int64_t i = 0; i < numel; i++) {
        double t = z_saved->data[i];
        da[i] = grad_out[i] * (1.0 - t * t);
    }
    kc_tensor_accum_grad(self->input_tensors[0], da, numel);
    free(da);
}

KcTensor *kc_ag_tanh_op(KcTensor *a)
{
    if (!a) return NULL;
    KcTensor *z = kc_tensor_copy(a);
    for (int64_t i = 0; i < z->numel; i++)
        z->data[i] = tanh(z->data[i]);

    if (!a->requires_grad) return z;
    z->requires_grad = 1;
    GradFn *fn = kc_gradfn_new();
    fn->backward = backward_tanh;
    fn->saved[0] = kc_tensor_copy(z);
    fn->n_saved  = 1;
    fn->input_tensors[0] = a;
    fn->n_inputs = 1;
    if (a->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)a->grad_fn; }
    z->grad_fn = fn;
    return z;
}

/* ── 자연로그 ─────────────────────────────────────────────────── */
static void backward_log(GradFn *self, const double *grad_out, int64_t numel)
{
    KcTensor *a_saved = self->saved[0];
    if (!a_saved || !self->input_tensors[0]) return;
    double *da = (double *)malloc((size_t)numel * sizeof(double));
    if (!da) return;
    for (int64_t i = 0; i < numel; i++) {
        double v = a_saved->data[i];
        da[i] = (v != 0.0) ? grad_out[i] / v : 0.0;
    }
    kc_tensor_accum_grad(self->input_tensors[0], da, numel);
    free(da);
}

KcTensor *kc_ag_log(KcTensor *a)
{
    if (!a) return NULL;
    KcTensor *z = kc_tensor_copy(a);
    for (int64_t i = 0; i < z->numel; i++)
        z->data[i] = log(z->data[i]);

    if (!a->requires_grad) return z;
    z->requires_grad = 1;
    GradFn *fn = kc_gradfn_new();
    fn->backward = backward_log;
    fn->saved[0] = kc_tensor_copy(a);
    fn->n_saved  = 1;
    fn->input_tensors[0] = a;
    fn->n_inputs = 1;
    if (a->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)a->grad_fn; }
    z->grad_fn = fn;
    return z;
}

/* ── 합산 ─────────────────────────────────────────────────────── */
static void backward_sum(GradFn *self, const double *grad_out, int64_t numel)
{
    KcTensor *ta = self->input_tensors[0];
    if (!ta || !ta->requires_grad) return;
    /* scalar grad_out[0] 브로드캐스트 */
    double g = grad_out[0];
    double *da = (double *)malloc((size_t)ta->numel * sizeof(double));
    if (!da) return;
    for (int64_t i = 0; i < ta->numel; i++) da[i] = g;
    kc_tensor_accum_grad(ta, da, ta->numel);
    free(da);
    (void)numel;
}

KcTensor *kc_ag_sum(KcTensor *a)
{
    if (!a) return NULL;
    double s = kc_tensor_sum(a);
    int64_t shape1[1] = {1};
    KcTensor *z = kc_tensor_from_data(&s, shape1, 1);
    if (!z) return NULL;

    if (!a->requires_grad) return z;
    z->requires_grad = 1;
    GradFn *fn = kc_gradfn_new();
    fn->backward = backward_sum;
    fn->input_tensors[0] = a;
    fn->n_inputs = 1;
    if (a->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)a->grad_fn; }
    z->grad_fn = fn;
    return z;
}

/* ── 평균 ─────────────────────────────────────────────────────── */
static void backward_mean(GradFn *self, const double *grad_out, int64_t numel)
{
    KcTensor *ta = self->input_tensors[0];
    if (!ta || !ta->requires_grad) return;
    double g = grad_out[0] / (double)ta->numel;
    double *da = (double *)malloc((size_t)ta->numel * sizeof(double));
    if (!da) return;
    for (int64_t i = 0; i < ta->numel; i++) da[i] = g;
    kc_tensor_accum_grad(ta, da, ta->numel);
    free(da);
    (void)numel;
}

KcTensor *kc_ag_mean(KcTensor *a)
{
    if (!a) return NULL;
    double m = kc_tensor_mean(a);
    int64_t shape1[1] = {1};
    KcTensor *z = kc_tensor_from_data(&m, shape1, 1);
    if (!z) return NULL;

    if (!a->requires_grad) return z;
    z->requires_grad = 1;
    GradFn *fn = kc_gradfn_new();
    fn->backward = backward_mean;
    fn->input_tensors[0] = a;
    fn->n_inputs = 1;
    if (a->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)a->grad_fn; }
    z->grad_fn = fn;
    return z;
}

/* ── 제곱 ─────────────────────────────────────────────────────── */
static void backward_pow2(GradFn *self, const double *grad_out, int64_t numel)
{
    KcTensor *a_saved = self->saved[0];
    if (!a_saved || !self->input_tensors[0]) return;
    double *da = (double *)malloc((size_t)numel * sizeof(double));
    if (!da) return;
    for (int64_t i = 0; i < numel; i++)
        da[i] = grad_out[i] * 2.0 * a_saved->data[i];
    kc_tensor_accum_grad(self->input_tensors[0], da, numel);
    free(da);
}

KcTensor *kc_ag_pow2(KcTensor *a)
{
    if (!a) return NULL;
    KcTensor *z = kc_tensor_copy(a);
    for (int64_t i = 0; i < z->numel; i++)
        z->data[i] = z->data[i] * z->data[i];

    if (!a->requires_grad) return z;
    z->requires_grad = 1;
    GradFn *fn = kc_gradfn_new();
    fn->backward = backward_pow2;
    fn->saved[0] = kc_tensor_copy(a);
    fn->n_saved  = 1;
    fn->input_tensors[0] = a;
    fn->n_inputs = 1;
    if (a->grad_fn) { fn->next_fns[fn->n_next++] = (GradFn *)a->grad_fn; }
    z->grad_fn = fn;
    return z;
}

/* ================================================================
 *  역전파 실행 — DFS 위상 정렬
 * ================================================================ */

/* 방문 플래그 리셋 (DFS 전처리) */
static void reset_visited(GradFn *fn)
{
    if (!fn || fn->visited == 0) return;
    fn->visited = 0;
    for (int i = 0; i < fn->n_next; i++)
        reset_visited(fn->next_fns[i]);
}

/* DFS 역전파 */
static void run_backward(GradFn *fn, KcTensor *output)
{
    if (!fn) return;
    if (fn->visited) return;
    fn->visited = 1;

    if (!output || !output->grad) return;

    /* 현재 노드의 backward 호출 */
    if (fn->backward) {
        fn->backward(fn, output->grad, output->numel);
    }

    /* saved 텐서의 grad를 이용해 다음 노드로 전파
     * — 다음 노드가 처리할 출력 텐서는 현재의 input_tensors */
    for (int i = 0; i < fn->n_next && i < fn->n_inputs; i++) {
        KcTensor *inp = fn->input_tensors[i];
        if (inp && inp->grad_fn) {
            run_backward((GradFn *)inp->grad_fn, inp);
        }
    }
}

void kc_autograd_backward(KcTensor *root)
{
    if (!root) return;
    /* 루트 grad = 1.0 초기화 */
    kc_tensor_ensure_grad(root);
    for (int64_t i = 0; i < root->numel; i++) root->grad[i] = 1.0;

    if (!root->grad_fn) return;

    GradFn *fn = (GradFn *)root->grad_fn;
    reset_visited(fn);
    run_backward(fn, root);
}

void kc_autograd_zero_grad(KcTensor *t)
{
    if (!t || !t->grad) return;
    memset(t->grad, 0, (size_t)t->numel * sizeof(double));
}
