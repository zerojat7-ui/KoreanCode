/*
 * kc_hormone.h — Digital Emotion Engine: 호르몬 동역학
 * Kcode DEE v1.0.0
 *
 * 호르몬 10종 + 감정 8종 + Bio Rhythm
 * NCkR 뉴런/시냅스 연동
 */
#ifndef KC_HORMONE_H
#define KC_HORMONE_H

#include <stdint.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_DEE_VERSION  "1.0.0"

/* ── 호르몬 10종 ──────────────────────────────────────────── */
typedef enum {
    KC_HOR_DOPAMINE       = 0,  /* 보상/동기/쾌감           */
    KC_HOR_SEROTONIN      = 1,  /* 안정/행복/자존감         */
    KC_HOR_CORTISOL       = 2,  /* 스트레스/불안            */
    KC_HOR_OXYTOCIN       = 3,  /* 유대/신뢰/친밀감         */
    KC_HOR_NOREPINEPHRINE = 4,  /* 집중/각성                */
    KC_HOR_ENDORPHIN      = 5,  /* 쾌락/진통/희열           */
    KC_HOR_ADRENALINE     = 6,  /* 긴장/전투준비            */
    KC_HOR_MELATONIN      = 7,  /* 수면/생체리듬            */
    KC_HOR_TESTOSTERONE   = 8,  /* 공격성/자신감            */
    KC_HOR_ESTROGEN       = 9,  /* 공감/감수성              */
    KC_HOR_COUNT          = 10
} KcHormoneType;

/* ── 감정 8종 ─────────────────────────────────────────────── */
typedef enum {
    KC_EMO_ANGER     = 0,
    KC_EMO_FEAR      = 1,
    KC_EMO_JOY       = 2,
    KC_EMO_SADNESS   = 3,
    KC_EMO_CURIOSITY = 4,
    KC_EMO_DISGUST   = 5,
    KC_EMO_SURPRISE  = 6,
    KC_EMO_TRUST     = 7,
    KC_EMO_COUNT     = 8
} KcEmotionType;

/* ── 욕구 6종 ─────────────────────────────────────────────── */
typedef enum {
    KC_DRIVE_SURVIVE    = 0,  /* 생존/안전     */
    KC_DRIVE_CONNECT    = 1,  /* 연결/관계     */
    KC_DRIVE_GROW       = 2,  /* 성장/학습     */
    KC_DRIVE_EXPRESS    = 3,  /* 표현/의견     */
    KC_DRIVE_UNDERSTAND = 4,  /* 이해/분석     */
    KC_DRIVE_CREATE     = 5,  /* 창조/혁신     */
    KC_DRIVE_COUNT      = 6
} KcDriveType;

/* ── Bio Rhythm ───────────────────────────────────────────── */
typedef struct {
    float  amplitude;     /* A                        */
    float  omega;         /* ω 각주파수               */
    float  phase;         /* φ 위상                   */
    float  offset;        /* B 기저값                 */
    float  current;       /* Bio(t) 현재값            */
    double t_elapsed;     /* 경과 시간 (초)           */
} KcBioRhythm;

/* ── 호르몬 상태 ──────────────────────────────────────────── */
typedef struct {
    float  level[KC_HOR_COUNT];      /* 현재 농도 [0,H_max]  */
    float  lambda[KC_HOR_COUNT];     /* 분해 속도 λ          */
    float  h_max[KC_HOR_COUNT];      /* 최대 농도            */
    float  input_buf[KC_HOR_COUNT];  /* 이번 틱 입력 버퍼    */
    float  diffusion[KC_HOR_COUNT];  /* 확산 계수            */
    float  tolerance[KC_HOR_COUNT];  /* 내성 (반복 자극 둔화)*/
} KcHormoneState;

/* ── 감정 상태 ────────────────────────────────────────────── */
typedef struct {
    float  e[KC_EMO_COUNT];          /* 감정 강도 [0,1]      */
    float  e_prev[KC_EMO_COUNT];     /* 이전 틱              */
    float  regulation;               /* 자기조절 계수        */
    float  irritation;               /* 짜증 누적            */
    float  fatigue;                  /* 피로도               */
    float  affinity;                 /* 현재 호감도          */
    float  intensity;                /* 전체 감정 강도       */
} KcEmotionState;

/* ── 욕구/드라이브 ────────────────────────────────────────── */
typedef struct {
    float  level[KC_DRIVE_COUNT];      /* 욕구 강도 [0,1]    */
    float  threshold[KC_DRIVE_COUNT];  /* 능동 발화 임계값   */
    float  satisfaction[KC_DRIVE_COUNT];/* 충족도            */
    float  decay_rate[KC_DRIVE_COUNT]; /* 자연 감소율        */
} KcDriveState;

/* ── 목표 ─────────────────────────────────────────────────── */
#define KC_GOAL_MAX     32
#define KC_GOAL_DESC    128

typedef struct {
    char     desc[KC_GOAL_DESC];
    float    priority;
    float    progress;
    int      active;
    uint32_t created_tick;
    uint32_t deadline_tick;  /* 0 = 무기한 */
    float    reward;         /* 달성 시 도파민 보상량 */
} KcGoal;

typedef struct {
    KcGoal   st[KC_GOAL_MAX];  /* 단기 목표 */
    KcGoal   lt[KC_GOAL_MAX];  /* 장기 목표 */
    int      st_cnt;
    int      lt_cnt;
} KcGoalSystem;

/* ── 기억 인코딩 ──────────────────────────────────────────── */
#define KC_MEM_MAX      256
#define KC_MEM_STR      192

typedef struct {
    char   event[KC_MEM_STR];
    float  e_snap[KC_EMO_COUNT];
    float  h_snap[KC_HOR_COUNT];
    uint32_t tick;
    float  importance;
    uint8_t decay_class;   /* 4=빨리망각, 0=영속 */
} KcEmotionMemory;

/* ── 전체 감정 엔진 ───────────────────────────────────────── */
#define KC_EMOTION_PROMPT_MAX  2048
#define KC_EMOTION_LOG_MAX     64

typedef struct {
    KcHormoneState  hor;
    KcEmotionState  emo;
    KcBioRhythm     bio;
    KcDriveState    drive;
    KcGoalSystem    goals;
    KcEmotionMemory mem[KC_MEM_MAX];
    int             mem_cnt;
    uint32_t        tick;
    double          t_real;

    /* 능동 발화 콜백 */
    float  ai_temp_base;     /* 감정 기반 temperature 기저값 */
    void (*on_active)(const char *prompt, void *ud);
    void  *userdata;

    /* 내부 로그 */
    char   log[KC_EMOTION_LOG_MAX][128];
    int    log_cnt;
} KcEmotionEngine;

/* ── API ──────────────────────────────────────────────────── */

/* 생성/소멸 */
KcEmotionEngine *kc_dee_create(void);
void             kc_dee_destroy(KcEmotionEngine *dee);

/* 기본값 초기화 */
void kc_dee_reset(KcEmotionEngine *dee);

/* 매 틱 업데이트 (dt=경과초) */
void kc_dee_tick(KcEmotionEngine *dee, float dt);

/* 자극 입력 */
void kc_dee_stimulus(KcEmotionEngine *dee,
                     const char *event_desc,
                     float goal_impact,    /* -1~1 */
                     float threat,         /* 0~1  */
                     float fairness,       /* 0~1  */
                     float certainty,      /* 0~1  */
                     float social);        /* 0~1  */

/* 호감도 업데이트 */
void kc_dee_affinity(KcEmotionEngine *dee, float delta);

/* 목표 추가 */
int  kc_dee_goal_add(KcEmotionEngine *dee, const char *desc,
                     float priority, uint32_t deadline_ticks,
                     float reward, int is_longterm);

/* 목표 진행 업데이트 */
void kc_dee_goal_progress(KcEmotionEngine *dee, int idx,
                          float delta, int is_longterm);

/* 감정 상태 → AI 프롬프트 생성 */
void kc_dee_to_prompt(const KcEmotionEngine *dee,
                      char *out, int out_sz);

/* 능동 발화 체크 */
int  kc_dee_check_active(KcEmotionEngine *dee);

/* 상태 출력 */
void kc_dee_print(const KcEmotionEngine *dee);

/* ── 호르몬→감정 영향 매트릭스 (내부용) ─────────────────── */
/* [호르몬][감정] = 영향 계수 */
extern const float KC_HOR_EMO_MATRIX[KC_HOR_COUNT][KC_EMO_COUNT];

/* 호르몬 이름 */
extern const char *KC_HORMONE_NAMES[KC_HOR_COUNT];

/* 감정 이름 */
extern const char *KC_EMOTION_NAMES[KC_EMO_COUNT];

/* 욕구 이름 */
extern const char *KC_DRIVE_NAMES[KC_DRIVE_COUNT];

#ifdef __cplusplus
}
#endif
#endif /* KC_HORMONE_H */
