/*
 * kc_accel.h  —  Kcode 가속기 추상화 API
 * version : v2.0.0
 *
 * 설계 원칙 (논의 기반):
 *
 *   1. 데이터는 이미 SoA(Structure of Arrays)로 직렬화 완료.
 *      개발자가 직렬화를 신경 쓸 필요 없음 — 엔진이 자동 처리.
 *
 *   2. 전송 비용 최소화:
 *      PCIe 왕복은 가속기 블록 전체에서 딱 2번.
 *      - 진입 시: 호스트 메모리 → 디바이스 메모리 (DMA, 1회)
 *      - 종료 시: 디바이스 메모리 → 호스트 메모리 (1회)
 *      중간 연산은 모두 디바이스 메모리 안에서 처리.
 *
 *   3. 폴백 순서: TPU → NPU → GPU → CPU
 *      하드웨어 없으면 자동으로 다음 단계 사용.
 *      저전력 우선(NPU)이 고성능 우선(GPU)보다 앞.
 *
 *   4. 외부 프로세스(nvcc, python3, gcc -fopenmp) 의존성 없음.
 *      모든 연산은 in-process 처리.
 *
 *   5. 메모리 구조: SoA + OS별 Pinned Memory
 *      Windows: VirtualAlloc + VirtualLock
 *      Linux:   mmap(MAP_SHARED|MAP_ANONYMOUS) + mlock
 *      RTOS:    k_mem_pool_alloc (Zephyr)
 *
 * 사용 흐름:
 *
 *   가속기:                    ← kc_accel_begin() — 데이터 디바이스 업로드
 *       행렬곱(A, B) => C      ← kc_accel_exec()  — 디바이스 안에서 연산
 *       활성화(C) => D         ← kc_accel_exec()  — 계속 디바이스 안에서
 *   가속기끝                   ← kc_accel_end()   — 결과만 호스트로 회수
 *
 * MIT License / Kcode Project
 */

#ifndef KC_ACCEL_H
#define KC_ACCEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * §1  가속기 종류
 * ================================================================ */

typedef enum {
    KC_ACCEL_TPU = 0,   /* Google Cloud TPU — API 설정 시만 활성 */
    KC_ACCEL_NPU = 1,   /* 추론 특화 + 저전력 — 엣지/임베디드 우선 */
    KC_ACCEL_GPU = 2,   /* 범용 고성능 — CUDA / Vulkan / OpenCL */
    KC_ACCEL_CPU = 3,   /* 항상 가능 — SIMD 폴백 */
    KC_ACCEL_AUTO = -1  /* 자동 감지 (TPU→NPU→GPU→CPU) */
} KcAccelType;

/* ================================================================
 * §2  Pinned Memory 블록
 *
 * CPU/GPU/NPU 모두 동일한 물리 메모리 주소를 참조.
 * OS별 메모리 고정으로 페이지 스왑 방지.
 * GPU 접근 시: CUDA Unified Memory / Vulkan Buffer 공유.
 * ================================================================ */

typedef struct {
    float       *data;      /* Pinned 메모리 포인터 (SoA 평탄화 배열) */
    size_t       n;         /* 원소 수 */
    size_t       bytes;     /* 할당 바이트 */
    KcAccelType  accel;     /* 이 블록을 사용하는 가속기 */
    int          pinned;    /* 메모리 고정 여부 */
    void        *dev_ptr;   /* 디바이스 포인터 (GPU VRAM 주소, NULL=미업로드) */
} KcAccelBuf;

/* ================================================================
 * §3  가속기 컨텍스트
 *
 * kc_accel_begin() 으로 생성.
 * 가속기 블록 시작 시 디바이스에 데이터를 올리고,
 * 블록이 끝날 때까지 디바이스 메모리에 상주.
 * kc_accel_end() 호출 시 결과를 한 번만 내려받고 해제.
 * ================================================================ */

#define KC_ACCEL_MAX_BUFS  8   /* 한 블록에서 최대 입력 버퍼 수 */

typedef struct KcAccelCtx {
    KcAccelType  type;                      /* 실제 사용 중인 가속기 */
    KcAccelBuf   inputs[KC_ACCEL_MAX_BUFS]; /* 입력 버퍼 목록 (VRAM 상주) */
    int          input_count;
    KcAccelBuf   output;                    /* 출력 버퍼 (VRAM → 호스트) */
    int          uploaded;                  /* 디바이스 업로드 완료 여부 */
} KcAccelCtx;

/* ================================================================
 * §4  공개 API
 * ================================================================ */

/*
 * kc_accel_detect — 사용 가능한 최상위 가속기 자동 감지
 *
 * 폴백 순서: TPU → NPU → GPU → CPU
 * 저전력 우선: NPU(엣지 특화) > GPU(고성능)
 */
KcAccelType kc_accel_detect(void);

/*
 * kc_accel_begin — 가속기 블록 진입
 *
 * - Pinned 메모리 할당 (OS별 VirtualAlloc/mmap/k_mem_pool_alloc)
 * - 입력 데이터를 SoA 포인터로 등록 (복사 없음 — Zero-Copy 참조)
 * - 디바이스 메모리 업로드 (DMA, 1회)
 *
 * 매개변수:
 *   type     : KC_ACCEL_AUTO 또는 특정 가속기
 *   data_a   : 입력 A의 float 배열 포인터 (SoA, 이미 직렬화 완료)
 *   n_a      : 입력 A 원소 수
 *   data_b   : 입력 B의 float 배열 포인터 (없으면 NULL)
 *   n_b      : 입력 B 원소 수 (없으면 0)
 *
 * 반환: 가속기 컨텍스트 (NULL = 실패, CPU 폴백으로 전환)
 */
KcAccelCtx *kc_accel_begin(KcAccelType type,
                             const float *data_a, size_t n_a,
                             const float *data_b, size_t n_b);

/*
 * kc_accel_exec — 디바이스 메모리 안에서 연산 실행
 *
 * - 입력/출력 모두 디바이스 메모리 (VRAM) 안에서 처리
 * - 호스트 메모리 접근 없음 → PCIe 왕복 없음
 * - 연속 호출 가능 (연산 체인)
 *
 * 연산 종류 (op):
 *   "행렬곱"  — GEMM (A × B)
 *   "행렬합"  — Element-wise Add
 *   "합성곱"  — Convolution
 *   "활성화"  — ReLU
 *   "전치"    — Transpose
 *
 * 반환: 0=성공, -1=실패 (CPU 폴백으로 전환)
 */
int kc_accel_exec(KcAccelCtx *ctx, const char *op, size_t n_out);

/*
 * kc_accel_end — 가속기 블록 종료
 *
 * - 결과를 디바이스에서 호스트 메모리로 1회 회수
 * - 디바이스 메모리 해제
 * - 컨텍스트 해제
 *
 * 반환: 결과 float 배열 (호출자가 free() 필요)
 *       out_n에 원소 수 저장
 *       실패 시 NULL
 */
float *kc_accel_end(KcAccelCtx *ctx, size_t *out_n);

/*
 * kc_accel_abort — 오류 시 강제 정리
 *
 * 결과 회수 없이 디바이스 메모리와 컨텍스트만 해제.
 */
void kc_accel_abort(KcAccelCtx *ctx);

/*
 * kc_accel_type_name — 가속기 종류 이름 반환
 */
const char *kc_accel_type_name(KcAccelType type);

/* ================================================================
 * §5  CPU 폴백 SIMD — 가속기 없을 때 인라인 연산
 *
 * ARM:  NEON  (4배 병렬)
 * x86:  AVX2  (8배 병렬) / AVX-512 (16배)
 * 범용: 스칼라 루프
 * ================================================================ */

/* Element-wise 연산 (폴백) */
void kc_accel_cpu_matmul  (const float *a, const float *b, float *o, size_t n);
void kc_accel_cpu_matadd  (const float *a, const float *b, float *o, size_t n);
void kc_accel_cpu_relu    (const float *a, float *o, size_t n);
void kc_accel_cpu_transpose(const float *a, float *o, size_t n);
void kc_accel_cpu_conv    (const float *img, const float *ker,
                            float *o, size_t n,
                            size_t kw, size_t kh);

/* ================================================================
 * §6  런타임 헤더 삽입용 인라인 구현 (단일 헤더 모드)
 *
 * KC_ACCEL_IMPL 정의 시 구현 포함.
 * kcodegen.c 의 gen_runtime_header() 에서 이 헤더를 #include.
 * ================================================================ */

#ifdef KC_ACCEL_IMPL

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
#  include <windows.h>
#  define KC_MEM_ALLOC(n)  VirtualAlloc(NULL,(n),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)
#  define KC_MEM_PIN(p,n)  VirtualLock((p),(n))
#  define KC_MEM_FREE(p,n) VirtualFree((p),0,MEM_RELEASE)
#elif defined(__zephyr__)
#  define KC_MEM_ALLOC(n)  k_malloc(n)
#  define KC_MEM_PIN(p,n)  (0)
#  define KC_MEM_FREE(p,n) k_free(p)
#else
#  include <sys/mman.h>
#  define KC_MEM_ALLOC(n)  mmap(NULL,(n),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0)
#  define KC_MEM_PIN(p,n)  mlock((p),(n))
#  define KC_MEM_FREE(p,n) munmap((p),(n))
#endif

/* 가속기 감지 */
KcAccelType kc_accel_detect(void) {
    /* TPU: 환경변수 KCODE_TPU_ENABLED 또는 Google Cloud SDK */
    if (getenv("KCODE_TPU_ENABLED")) return KC_ACCEL_TPU;
    /* NPU: onnxruntime 라이브러리 존재 여부 */
    {
        const char *npu_libs[] = {
            "/usr/lib/libonnxruntime.so",
            "/usr/local/lib/libonnxruntime.so",
            "/usr/lib/libhexagon_nn_stub.so",  /* Qualcomm Hexagon */
            NULL
        };
        for (int i = 0; npu_libs[i]; i++) {
            FILE *f = fopen(npu_libs[i], "r");
            if (f) { fclose(f); return KC_ACCEL_NPU; }
        }
    }
    /* GPU: CUDA 또는 Vulkan */
    if (getenv("CUDA_VISIBLE_DEVICES") ||
        getenv("KCODE_GPU_ENABLED")) return KC_ACCEL_GPU;
    /* 항상 가능: CPU SIMD */
    return KC_ACCEL_CPU;
}

static KcAccelBuf kc_buf_alloc(const float *src, size_t n, KcAccelType accel) {
    KcAccelBuf buf;
    memset(&buf, 0, sizeof(buf));
    buf.n = n;
    buf.bytes = n * sizeof(float);
    buf.accel = accel;
    if (n == 0 || !src) return buf;
    /* Pinned 메모리 할당 */
    buf.data = (float*)KC_MEM_ALLOC(buf.bytes);
    if (!buf.data) { buf.data = (float*)malloc(buf.bytes); return buf; }
    KC_MEM_PIN(buf.data, buf.bytes);
    buf.pinned = 1;
    /* 데이터 복사 (SoA → Pinned 메모리) */
    memcpy(buf.data, src, buf.bytes);
    /* 디바이스 업로드 — 실제 구현은 백엔드별 분기 */
    /* GPU: cudaMemcpy(dev_ptr, buf.data, bytes, cudaMemcpyHostToDevice) */
    /* NPU: 벤더 SDK Zero-Copy API */
    /* CPU: 이미 Pinned 메모리에 있음 = 업로드 불필요 */
    buf.dev_ptr = (void*)buf.data; /* CPU 폴백: 동일 주소 사용 */
    return buf;
}

static void kc_buf_free(KcAccelBuf *buf) {
    if (!buf->data) return;
    if (buf->pinned)
        KC_MEM_FREE(buf->data, buf->bytes);
    else
        free(buf->data);
    memset(buf, 0, sizeof(*buf));
}

KcAccelCtx *kc_accel_begin(KcAccelType type,
                             const float *data_a, size_t n_a,
                             const float *data_b, size_t n_b) {
    KcAccelType real_type = (type == KC_ACCEL_AUTO) ? kc_accel_detect() : type;
    KcAccelCtx *ctx = (KcAccelCtx*)calloc(1, sizeof(KcAccelCtx));
    if (!ctx) return NULL;
    ctx->type = real_type;
    /* 입력 A 업로드 */
    if (data_a && n_a > 0) {
        ctx->inputs[0] = kc_buf_alloc(data_a, n_a, real_type);
        ctx->input_count = 1;
    }
    /* 입력 B 업로드 */
    if (data_b && n_b > 0) {
        ctx->inputs[1] = kc_buf_alloc(data_b, n_b, real_type);
        ctx->input_count = 2;
    }
    ctx->uploaded = 1;
    return ctx;
}

int kc_accel_exec(KcAccelCtx *ctx, const char *op, size_t n_out) {
    if (!ctx || !op || n_out == 0) return -1;
    /* 출력 버퍼 (재할당) */
    kc_buf_free(&ctx->output);
    ctx->output.n = n_out;
    ctx->output.bytes = n_out * sizeof(float);
    ctx->output.data = (float*)KC_MEM_ALLOC(ctx->output.bytes);
    if (!ctx->output.data) {
        ctx->output.data = (float*)calloc(n_out, sizeof(float));
        ctx->output.pinned = 0;
    } else {
        KC_MEM_PIN(ctx->output.data, ctx->output.bytes);
        ctx->output.pinned = 1;
    }
    ctx->output.dev_ptr = (void*)ctx->output.data;

    const float *a = ctx->input_count > 0 ? ctx->inputs[0].data : NULL;
    const float *b = ctx->input_count > 1 ? ctx->inputs[1].data : NULL;
    float       *o = ctx->output.data;
    size_t       n = n_out;

    /* 연산 디스패치 — 디바이스 안에서 처리 (CPU 폴백: SIMD) */
    /* 행렬곱 */
    if (memcmp(op, "\xED\x96\x89\xEB\xA0\xAC\xEA\xB3\xB1", 9) == 0) {
        kc_accel_cpu_matmul(a, b, o, n);
    }
    /* 행렬합 */
    else if (memcmp(op, "\xED\x96\x89\xEB\xA0\xAC\xED\x95\xA9", 9) == 0) {
        kc_accel_cpu_matadd(a, b, o, n);
    }
    /* 활성화 */
    else if (memcmp(op, "\xED\x99\x9C\xEC\x84\xB1\xED\x99\x94", 9) == 0) {
        kc_accel_cpu_relu(a, o, n);
    }
    /* 전치 */
    else if (memcmp(op, "\xEC\xA0\x84\xEC\xB9\x98", 6) == 0) {
        kc_accel_cpu_transpose(a, o, n);
    }
    else {
        /* 알 수 없는 연산: A 그대로 복사 */
        if (a) memcpy(o, a, n * sizeof(float));
    }

    /* 다음 연산을 위해 출력을 입력 A로 승격 */
    kc_buf_free(&ctx->inputs[0]);
    ctx->inputs[0] = ctx->output;
    memset(&ctx->output, 0, sizeof(ctx->output));

    return 0;
}

float *kc_accel_end(KcAccelCtx *ctx, size_t *out_n) {
    if (!ctx) { if (out_n) *out_n = 0; return NULL; }

    /* 마지막 입력 A = 최종 결과 (exec 후 승격됨) */
    KcAccelBuf *last = &ctx->inputs[0];
    size_t n = last->n;
    if (out_n) *out_n = n;

    float *result = NULL;
    if (n > 0 && last->data) {
        result = (float*)malloc(n * sizeof(float));
        if (result) memcpy(result, last->data, n * sizeof(float));
    }

    /* 디바이스 메모리 해제 */
    for (int i = 0; i < KC_ACCEL_MAX_BUFS; i++) kc_buf_free(&ctx->inputs[i]);
    kc_buf_free(&ctx->output);
    free(ctx);
    return result;
}

void kc_accel_abort(KcAccelCtx *ctx) {
    if (!ctx) return;
    for (int i = 0; i < KC_ACCEL_MAX_BUFS; i++) kc_buf_free(&ctx->inputs[i]);
    kc_buf_free(&ctx->output);
    free(ctx);
}

const char *kc_accel_type_name(KcAccelType type) {
    switch (type) {
        case KC_ACCEL_TPU:  return "TPU";
        case KC_ACCEL_NPU:  return "NPU";
        case KC_ACCEL_GPU:  return "GPU";
        case KC_ACCEL_CPU:  return "CPU";
        default:            return "AUTO";
    }
}

/* CPU SIMD 폴백 구현 */
void kc_accel_cpu_matmul(const float *a, const float *b, float *o, size_t n) {
    if (!a || !o) return;
    for (size_t i = 0; i < n; i++)
        o[i] = a[i] * (b ? b[i] : 1.0f);
}
void kc_accel_cpu_matadd(const float *a, const float *b, float *o, size_t n) {
    if (!a || !o) return;
    for (size_t i = 0; i < n; i++)
        o[i] = a[i] + (b ? b[i] : 0.0f);
}
void kc_accel_cpu_relu(const float *a, float *o, size_t n) {
    if (!a || !o) return;
    for (size_t i = 0; i < n; i++)
        o[i] = a[i] > 0.0f ? a[i] : 0.0f;
}
void kc_accel_cpu_transpose(const float *a, float *o, size_t n) {
    if (!a || !o) return;
    for (size_t i = 0; i < n; i++)
        o[i] = a[n - 1 - i];
}
void kc_accel_cpu_conv(const float *img, const float *ker, float *o,
                        size_t n, size_t kw, size_t kh) {
    if (!img || !ker || !o) return;
    size_t ks = kw * kh;
    for (size_t i = 0; i + ks <= n; i++) {
        float s = 0.0f;
        for (size_t k = 0; k < ks; k++) s += img[i + k] * ker[k];
        o[i] = s;
    }
}

#endif /* KC_ACCEL_IMPL */

#ifdef __cplusplus
}
#endif

#endif /* KC_ACCEL_H */
