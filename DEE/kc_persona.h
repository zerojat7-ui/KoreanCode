/*
 * kc_persona.h — KcPersona 성격 프로필 시스템
 * Kcode DEE v1.3.0
 *
 * Jung 4축 기반 독자 명명 시스템
 * MBTI 상표와 무관한 완전 독립 구현
 *
 * 4축 정의:
 *   KPE: Energy    활동형(E) ↔ 성찰형(I)
 *   KPN: Nature    직관형(N) ↔ 현실형(S)
 *   KPF: Function  공감형(F) ↔ 논리형(T)
 *   KPJ: Judge     계획형(J) ↔ 탐색형(P)
 */
#ifndef KC_PERSONA_H
#define KC_PERSONA_H
#include "kc_hormone.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ── 16종 타입 코드 상수 ──────────────────────────────── */
#define KP_탐험가  "KP01"   /* 활동·직관·공감·탐색 */
#define KP_전략가  "KP02"   /* 성찰·직관·논리·계획 */
#define KP_중재자  "KP03"   /* 성찰·직관·공감·탐색 */
#define KP_변론가  "KP04"   /* 활동·직관·논리·탐색 */
#define KP_통솔자  "KP05"   /* 활동·직관·논리·계획 */
#define KP_선도자  "KP06"   /* 성찰·직관·공감·계획 */
#define KP_사회자  "KP07"   /* 활동·직관·공감·계획 */
#define KP_논리가  "KP08"   /* 성찰·직관·논리·탐색 */
#define KP_수호자  "KP09"   /* 성찰·현실·공감·계획 */
#define KP_관리자  "KP10"   /* 성찰·현실·논리·계획 */
#define KP_활동가  "KP11"   /* 활동·현실·공감·계획 */
#define KP_감독관  "KP12"   /* 활동·현실·논리·계획 */
#define KP_모험가  "KP13"   /* 성찰·현실·공감·탐색 */
#define KP_기술자  "KP14"   /* 성찰·현실·논리·탐색 */
#define KP_연예인  "KP15"   /* 활동·현실·공감·탐색 */
#define KP_사업가  "KP16"   /* 활동·현실·논리·탐색 */

/* ── 4축 수치 구조체 ──────────────────────────────────── */
typedef struct {
    float kpe;  /* Energy:    0.0(성찰) ~ 1.0(활동) */
    float kpn;  /* Nature:    0.0(현실) ~ 1.0(직관) */
    float kpf;  /* Function:  0.0(논리) ~ 1.0(공감) */
    float kpj;  /* Judge:     0.0(탐색) ~ 1.0(계획) */
} KcPersonaAxis;

/* ── 성격 프로필 구조체 ───────────────────────────────── */
typedef struct {
    char          code[5];              /* "KP01" ~ "KP16"      */
    char          name[16];             /* 한글 이름             */
    KcPersonaAxis axis;                 /* 4축 수치              */

    /* 호르몬 기본 편향 */
    float         hor_bias[KC_HOR_COUNT];

    /* DEE 파라미터 */
    float         energy_recharge_rate; /* Bio 회복 속도        */
    float         social_drain_rate;    /* 사회적 에너지 소모   */
    float         w_logic;              /* 논리 가중치          */
    float         w_empathy;            /* 공감 가중치          */
    float         tolerance_decay;      /* 내성 회복 속도       */
    float         irritation_k;         /* 짜증 전환 계수       */
    float         curiosity_base;       /* 기본 호기심          */
    float         novelty_dopamine;     /* 새로움 반응          */
} KcPersonaProfile;

/* ── 16종 프로필 테이블 ───────────────────────────────── */
extern KcPersonaProfile KC_PERSONA_PROFILES[16];

/* ── API ──────────────────────────────────────────────── */

/* 코드 또는 한글 이름으로 프로필 찾기 */
/* kc_persona_find("KP01") 또는 kc_persona_find("탐험가") */
const KcPersonaProfile *kc_persona_find(const char *code_or_name);

/* DEE 에 성격 적용 */
void kc_persona_apply(KcEmotionEngine *dee, const char *code_or_name);

/* 4축 수치로 가장 가까운 타입 찾기 */
const KcPersonaProfile *kc_persona_match(float kpe, float kpn,
                                          float kpf, float kpj);

/* 성격 이름 출력 */
void kc_persona_print_all(void);

#ifdef __cplusplus
}
#endif
#endif /* KC_PERSONA_H */
