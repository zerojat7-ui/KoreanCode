/*
 * kc_hormone.c — Digital Emotion Engine 구현
 * Kcode DEE v1.0.0
 */
#include "kc_hormone.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ── 이름 테이블 ──────────────────────────────────────────── */
const char *KC_HORMONE_NAMES[KC_HOR_COUNT] = {
    "도파민","세로토닌","코르티솔","옥시토신","노르에피네프린",
    "엔도르핀","아드레날린","멜라토닌","테스토스테론","에스트로겐"
};
const char *KC_EMOTION_NAMES[KC_EMO_COUNT] = {
    "분노","공포","기쁨","슬픔","호기심","혐오","놀람","신뢰"
};
const char *KC_DRIVE_NAMES[KC_DRIVE_COUNT] = {
    "생존","연결","성장","표현","이해","창조"
};

/*
 * 호르몬→감정 영향 매트릭스
 * [호르몬][감정]  양수=증가, 음수=감소
 *          ANGER  FEAR   JOY   SAD   CUR   DIS   SUR   TRUST
 */
const float KC_HOR_EMO_MATRIX[KC_HOR_COUNT][KC_EMO_COUNT] = {
/* 도파민        */ { -0.1f,  -0.1f,  +0.8f, -0.3f, +0.6f, -0.1f,  0.0f, +0.2f },
/* 세로토닌      */ { -0.5f,  -0.4f,  +0.4f, -0.5f, +0.1f, -0.2f,  0.0f, +0.3f },
/* 코르티솔      */ { +0.5f,  +0.6f,  -0.3f, +0.3f, -0.2f, +0.1f, +0.2f, -0.3f },
/* 옥시토신      */ { -0.3f,  -0.3f,  +0.3f, -0.2f,  0.0f, -0.1f,  0.0f, +0.7f },
/* 노르에피네프린*/ { +0.2f,  +0.3f,  -0.1f, -0.1f, +0.4f,  0.0f, +0.3f, -0.1f },
/* 엔도르핀      */ { -0.2f,  -0.3f,  +0.6f, -0.4f, +0.1f, -0.2f,  0.0f, +0.1f },
/* 아드레날린    */ { +0.3f,  +0.5f,  -0.1f, -0.1f,  0.0f,  0.0f, +0.4f, -0.2f },
/* 멜라토닌      */ { -0.1f,  -0.1f,  -0.1f, +0.2f, -0.3f,  0.0f, -0.1f, +0.1f },
/* 테스토스테론  */ { +0.4f,  -0.1f,  +0.1f, -0.2f, +0.2f, +0.1f, +0.1f, -0.2f },
/* 에스트로겐    */ { -0.1f,  +0.1f,  +0.2f, +0.2f, +0.1f, +0.3f, +0.3f, +0.2f },
};

/* 욕구 기본 임계값 */
static const float KC_DRIVE_THRESH_DEFAULT[KC_DRIVE_COUNT] = {
    0.85f, 0.75f, 0.70f, 0.80f, 0.70f, 0.75f
};
static const float KC_DRIVE_DECAY_DEFAULT[KC_DRIVE_COUNT] = {
    0.0002f, 0.0005f, 0.0003f, 0.0008f, 0.0004f, 0.0006f
};

/* λ (분해 속도) — 낮을수록 오래 지속 */
static const float KC_HOR_LAMBDA_DEFAULT[KC_HOR_COUNT] = {
    0.05f,  /* 도파민         빠른 분해 */
    0.02f,  /* 세로토닌       느린 분해 */
    0.03f,  /* 코르티솔       중간      */
    0.01f,  /* 옥시토신       매우 느림 */
    0.06f,  /* 노르에피네프린 빠름      */
    0.015f, /* 엔도르핀       느림      */
    0.08f,  /* 아드레날린     매우 빠름 */
    0.005f, /* 멜라토닌       극히 느림 */
    0.01f,  /* 테스토스테론   느림      */
    0.01f,  /* 에스트로겐     느림      */
};
static const float KC_HOR_MAX_DEFAULT[KC_HOR_COUNT] = {
    2.0f, 2.0f, 2.0f, 2.0f, 2.0f,
    2.0f, 2.0f, 1.5f, 1.5f, 1.5f
};

/* ── 유틸리티 ─────────────────────────────────────────────── */
static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static float sigmoidf(float x) {
    return 1.0f / (1.0f + expf(-x));
}

/* ── 생성/소멸 ────────────────────────────────────────────── */
KcEmotionEngine *kc_dee_create(void) {
    KcEmotionEngine *dee = calloc(1, sizeof(KcEmotionEngine));
    if (!dee) return NULL;
    kc_dee_reset(dee);
    return dee;
}

void kc_dee_destroy(KcEmotionEngine *dee) {
    if (dee) free(dee);
}

void kc_dee_reset(KcEmotionEngine *dee) {
    memset(dee, 0, sizeof(*dee));

    /* 호르몬 초기화 */
    for (int i = 0; i < KC_HOR_COUNT; i++) {
        dee->hor.lambda[i]    = KC_HOR_LAMBDA_DEFAULT[i];
        dee->hor.h_max[i]     = KC_HOR_MAX_DEFAULT[i];
        dee->hor.level[i]     = 0.3f; /* 기본 안정 농도 */
        dee->hor.tolerance[i] = 1.0f;
        dee->hor.diffusion[i] = 0.001f;
    }
    /* 멜라토닌: 낮에는 낮음 */
    dee->hor.level[KC_HOR_MELATONIN] = 0.1f;
    /* 세로토닌 기본 안정 */
    dee->hor.level[KC_HOR_SEROTONIN] = 0.5f;

    /* 감정 초기화 — 중립 */
    for (int i = 0; i < KC_EMO_COUNT; i++)
        dee->emo.e[i] = 0.1f;
    dee->emo.e[KC_EMO_TRUST]    = 0.4f;
    dee->emo.e[KC_EMO_CURIOSITY]= 0.3f;
    dee->emo.regulation = 0.8f;

    /* Bio Rhythm — 24h 주기 */
    dee->bio.amplitude = 0.2f;
    dee->bio.omega     = 2.0f * 3.14159f / 86400.0f;
    dee->bio.phase     = 0.0f;
    dee->bio.offset    = 0.0f;

    /* 욕구 초기화 */
    for (int i = 0; i < KC_DRIVE_COUNT; i++) {
        dee->drive.level[i]       = 0.2f;
        dee->drive.threshold[i]   = KC_DRIVE_THRESH_DEFAULT[i];
        dee->drive.satisfaction[i]= 0.5f;
        dee->drive.decay_rate[i]  = KC_DRIVE_DECAY_DEFAULT[i];
    }
}

/* ── 자극 입력 ────────────────────────────────────────────── */
void kc_dee_stimulus(KcEmotionEngine *dee,
                     const char *event_desc,
                     float goal_impact,
                     float threat,
                     float fairness,
                     float certainty,
                     float social) {
    if (!dee) return;

    /* Appraisal 계산 */
    float bio = dee->bio.current;
    float aff = dee->emo.affinity;

    /* 호르몬 Input 계산 */
    /* 코르티솔: 위협에 비례 */
    dee->hor.input_buf[KC_HOR_CORTISOL]  += threat * 0.8f * (1.0f - certainty * 0.3f);

    /* 아드레날린: 위협 + 불확실 */
    dee->hor.input_buf[KC_HOR_ADRENALINE]+= threat * (1.0f - certainty) * 0.6f;

    /* 도파민: 목표 달성/긍정 */
    if (goal_impact > 0)
        dee->hor.input_buf[KC_HOR_DOPAMINE] += goal_impact * 0.9f;

    /* 노르에피네프린: 각성 */
    dee->hor.input_buf[KC_HOR_NOREPINEPHRINE] += (threat + (1.0f - certainty)) * 0.3f;

    /* 옥시토신: 사회적 상호작용 */
    dee->hor.input_buf[KC_HOR_OXYTOCIN]  += social * (1.0f + aff * 0.3f) * 0.4f;

    /* 세로토닌: 공정 + 긍정 */
    dee->hor.input_buf[KC_HOR_SEROTONIN] += fairness * 0.3f;

    /* 엔도르핀: 강한 긍정 보상 */
    if (goal_impact > 0.7f)
        dee->hor.input_buf[KC_HOR_ENDORPHIN] += (goal_impact - 0.7f) * 0.5f;

    /* Bio Rhythm 반영 */
    for (int i = 0; i < KC_HOR_COUNT; i++)
        dee->hor.input_buf[i] *= (1.0f + bio * 0.1f);

    /* Affinity: 호르몬 입력 감쇠 (친밀하면 덜 자극) */
    dee->hor.input_buf[KC_HOR_CORTISOL]  *= (1.0f - aff * 0.2f);
    dee->hor.input_buf[KC_HOR_ADRENALINE]*= (1.0f - aff * 0.2f);

    /* 기억 인코딩 */
    if (dee->mem_cnt < KC_MEM_MAX) {
        KcEmotionMemory *m = &dee->mem[dee->mem_cnt++];
        strncpy(m->event, event_desc ? event_desc : "event", KC_MEM_STR-1);
        memcpy(m->e_snap, dee->emo.e, sizeof(dee->emo.e));
        memcpy(m->h_snap, dee->hor.level, sizeof(dee->hor.level));
        m->tick       = dee->tick;
        m->importance = threat * 0.5f + fabsf(goal_impact) * 0.5f;
        m->decay_class= m->importance > 0.7f ? 0 :
                       (m->importance > 0.4f ? 1 :
                       (m->importance > 0.2f ? 2 : 4));
    }
}

/* ── 매 틱 업데이트 ───────────────────────────────────────── */
void kc_dee_tick(KcEmotionEngine *dee, float dt) {
    if (!dee) return;
    dee->tick++;
    dee->t_real += dt;

    /* 1. Bio Rhythm 업데이트 */
    dee->bio.t_elapsed += dt;
    dee->bio.current = dee->bio.amplitude
        * sinf((float)(dee->bio.omega * dee->bio.t_elapsed) + dee->bio.phase)
        + dee->bio.offset;

    /* 2. 호르몬 업데이트: H(t+1) = H(t) + Input - λH(t) */
    for (int i = 0; i < KC_HOR_COUNT; i++) {
        float input  = dee->hor.input_buf[i] * dee->hor.tolerance[i];
        float decay  = dee->hor.lambda[i] * dee->hor.level[i];
        dee->hor.level[i] += (input - decay) * dt;
        dee->hor.level[i] += dee->hor.diffusion[i] * dt;
        dee->hor.level[i]  = clampf(dee->hor.level[i], 0.0f, dee->hor.h_max[i]);
        dee->hor.input_buf[i] = 0.0f;

        /* 내성: 반복 자극 시 감소 */
        if (input > 0.5f)
            dee->hor.tolerance[i] = clampf(dee->hor.tolerance[i] - 0.001f, 0.3f, 1.0f);
        else
            dee->hor.tolerance[i] = clampf(dee->hor.tolerance[i] + 0.0005f, 0.3f, 1.0f);
    }

    /* 3. 호르몬 → 감정 변환 */
    memcpy(dee->emo.e_prev, dee->emo.e, sizeof(dee->emo.e));
    for (int j = 0; j < KC_EMO_COUNT; j++) {
        float delta = 0.0f;
        for (int i = 0; i < KC_HOR_COUNT; i++)
            delta += KC_HOR_EMO_MATRIX[i][j]
                   * (dee->hor.level[i] / dee->hor.h_max[i]);
        dee->emo.e[j] += delta * dt * 0.1f;
    }

    /* 4. Bio Rhythm 감정 변조 */
    for (int j = 0; j < KC_EMO_COUNT; j++)
        dee->emo.e[j] *= (1.0f + dee->bio.current * 0.05f);

    /* 5. 호감도 감정 변조 */
    float k_aff = 0.15f;
    dee->emo.e[KC_EMO_JOY]   *= (1.0f + k_aff * dee->emo.affinity);
    dee->emo.e[KC_EMO_TRUST]  = clampf(dee->emo.e[KC_EMO_TRUST]
                                + dee->emo.affinity * 0.01f * dt, 0.0f, 1.0f);

    /* 6. 짜증(Irritation) */
    float irr_input = dee->emo.fatigue * 0.3f
                    + dee->hor.level[KC_HOR_CORTISOL] * 0.2f
                    - dee->hor.level[KC_HOR_SEROTONIN] * 0.2f;
    dee->emo.irritation += (sigmoidf(irr_input - 0.5f) - dee->emo.irritation) * dt * 0.1f;
    dee->emo.e[KC_EMO_ANGER] += dee->emo.irritation * 0.05f * dt;

    /* 피로도 증가/감소 */
    dee->emo.fatigue += dt * 0.00005f;
    if (dee->hor.level[KC_HOR_MELATONIN] > 0.5f)
        dee->emo.fatigue -= dt * 0.001f;
    dee->emo.fatigue = clampf(dee->emo.fatigue, 0.0f, 1.0f);

    /* 7. 자기조절 */
    for (int j = 0; j < KC_EMO_COUNT; j++)
        dee->emo.e[j] *= dee->emo.regulation;

    /* 8. clamp */
    for (int j = 0; j < KC_EMO_COUNT; j++)
        dee->emo.e[j] = clampf(dee->emo.e[j], 0.0f, 1.0f);

    /* 전체 강도 */
    float sum = 0.0f;
    for (int j = 0; j < KC_EMO_COUNT; j++) sum += dee->emo.e[j];
    dee->emo.intensity = sum / KC_EMO_COUNT;

    /* 9. 욕구 업데이트 */
    for (int d = 0; d < KC_DRIVE_COUNT; d++) {
        dee->drive.level[d] += dee->drive.decay_rate[d] * dt
                             - dee->drive.satisfaction[d] * 0.0001f * dt;
        dee->drive.level[d] = clampf(dee->drive.level[d], 0.0f, 1.0f);
        dee->drive.satisfaction[d] = clampf(dee->drive.satisfaction[d]
                                    - 0.00005f * dt, 0.0f, 1.0f);
    }
    /* 도파민 → 성장/이해 욕구 충족 */
    dee->drive.satisfaction[KC_DRIVE_GROW] +=
        dee->hor.level[KC_HOR_DOPAMINE] * 0.001f * dt;
    /* 옥시토신 → 연결 욕구 충족 */
    dee->drive.satisfaction[KC_DRIVE_CONNECT] +=
        dee->hor.level[KC_HOR_OXYTOCIN] * 0.002f * dt;
    /* 엔도르핀 → 생존 욕구 충족 */
    dee->drive.satisfaction[KC_DRIVE_SURVIVE] +=
        dee->hor.level[KC_HOR_ENDORPHIN] * 0.001f * dt;

    /* 10. 행동 트리거 체크 */
    kc_dee_check_active(dee);
}

/* ── 능동 발화 체크 ───────────────────────────────────────── */
int kc_dee_check_active(KcEmotionEngine *dee) {
    if (!dee || !dee->on_active) return 0;
    static uint32_t last_fire = 0;
    if (dee->tick - last_fire < 300) return 0; /* 쿨다운 */

    char reason[64] = {0};
    int  fired = 0;

    /* anger > θ_a → 먼저 말함 */
    if (dee->emo.e[KC_EMO_ANGER] > 0.8f) {
        snprintf(reason, 63, "anger=%.2f", dee->emo.e[KC_EMO_ANGER]);
        fired = 1;
    }
    /* curiosity > θ_c → 먼저 질문 */
    if (!fired && dee->emo.e[KC_EMO_CURIOSITY] > 0.85f) {
        snprintf(reason, 63, "curiosity=%.2f", dee->emo.e[KC_EMO_CURIOSITY]);
        fired = 1;
    }
    /* express drive > θ */
    if (!fired && dee->drive.level[KC_DRIVE_EXPRESS] > dee->drive.threshold[KC_DRIVE_EXPRESS]) {
        snprintf(reason, 63, "express_drive=%.2f", dee->drive.level[KC_DRIVE_EXPRESS]);
        fired = 1;
    }
    /* joy > 0.9 → 공유하고 싶음 */
    if (!fired && dee->emo.e[KC_EMO_JOY] > 0.9f) {
        snprintf(reason, 63, "joy=%.2f", dee->emo.e[KC_EMO_JOY]);
        fired = 1;
    }

    if (fired) {
        char prompt[KC_EMOTION_PROMPT_MAX];
        kc_dee_to_prompt(dee, prompt, KC_EMOTION_PROMPT_MAX);
        char full[KC_EMOTION_PROMPT_MAX + 128];
        snprintf(full, sizeof(full),
                 "[능동발화:%s]\n%s", reason, prompt);
        dee->on_active(full, dee->userdata);
        last_fire = dee->tick;
    }
    return fired;
}

/* ── 호감도 업데이트 ──────────────────────────────────────── */
void kc_dee_affinity(KcEmotionEngine *dee, float delta) {
    if (!dee) return;
    dee->emo.affinity = clampf(dee->emo.affinity + delta, 0.0f, 1.0f);
    dee->hor.input_buf[KC_HOR_OXYTOCIN] += delta > 0 ? delta * 0.3f : 0.0f;
}

/* ── 목표 추가 ────────────────────────────────────────────── */
int kc_dee_goal_add(KcEmotionEngine *dee, const char *desc,
                    float priority, uint32_t deadline_ticks,
                    float reward, int is_longterm) {
    if (!dee) return -1;
    KcGoal *arr = is_longterm ? dee->goals.lt : dee->goals.st;
    int    *cnt = is_longterm ? &dee->goals.lt_cnt : &dee->goals.st_cnt;
    if (*cnt >= KC_GOAL_MAX) return -1;
    KcGoal *g = &arr[(*cnt)++];
    strncpy(g->desc, desc, KC_GOAL_DESC-1);
    g->priority      = priority;
    g->progress      = 0.0f;
    g->active        = 1;
    g->created_tick  = dee->tick;
    g->deadline_tick = deadline_ticks;
    g->reward        = reward;
    return *cnt - 1;
}

void kc_dee_goal_progress(KcEmotionEngine *dee, int idx,
                          float delta, int is_longterm) {
    if (!dee) return;
    KcGoal *arr = is_longterm ? dee->goals.lt : dee->goals.st;
    int     cnt = is_longterm ? dee->goals.lt_cnt : dee->goals.st_cnt;
    if (idx < 0 || idx >= cnt) return;
    arr[idx].progress = clampf(arr[idx].progress + delta, 0.0f, 1.0f);
    if (arr[idx].progress >= 1.0f && arr[idx].active) {
        arr[idx].active = 0;
        dee->hor.input_buf[KC_HOR_DOPAMINE] += arr[idx].reward;
        dee->hor.input_buf[KC_HOR_ENDORPHIN]+= arr[idx].reward * 0.3f;
        dee->drive.satisfaction[KC_DRIVE_GROW] += 0.2f;
    }
}

/* ── 감정 상태 → AI 프롬프트 ─────────────────────────────── */
void kc_dee_to_prompt(const KcEmotionEngine *dee,
                      char *out, int out_sz) {
    if (!dee || !out) return;

    /* 지배 감정 찾기 */
    int   dom_emo = 0;
    float dom_val = dee->emo.e[0];
    for (int j = 1; j < KC_EMO_COUNT; j++) {
        if (dee->emo.e[j] > dom_val) {
            dom_val = dee->emo.e[j]; dom_emo = j;
        }
    }
    /* 지배 욕구 찾기 */
    int   dom_drv = 0;
    float dom_dlv = dee->drive.level[0];
    for (int d = 1; d < KC_DRIVE_COUNT; d++) {
        if (dee->drive.level[d] > dom_dlv) {
            dom_dlv = dee->drive.level[d]; dom_drv = d;
        }
    }
    /* 호르몬 상위 2종 */
    int h1=0, h2=1;
    for (int i=0;i<KC_HOR_COUNT;i++) {
        float n = dee->hor.level[i]/dee->hor.h_max[i];
        if (n > dee->hor.level[h1]/dee->hor.h_max[h1]) {h2=h1; h1=i;}
        else if (i!=h1 && n > dee->hor.level[h2]/dee->hor.h_max[h2]) h2=i;
    }
    /* 현재 목표 */
    const char *goal_desc = "특별한 목표 없음";
    float goal_prog = 0.0f;
    for (int i = 0; i < dee->goals.st_cnt; i++) {
        if (dee->goals.st[i].active) {
            goal_desc = dee->goals.st[i].desc;
            goal_prog = dee->goals.st[i].progress;
            break;
        }
    }

    snprintf(out, out_sz,
        "[DEE v%s | tick=%u]\n"
        "현재 감정: %s(%.2f) | 피로=%.2f | 짜증=%.2f\n"
        "호르몬: %s(%.2f) %s(%.2f)\n"
        "강한 욕구: %s(%.2f)\n"
        "현재 목표: \"%s\" (진행 %.0f%%)\n"
        "호감도: %.2f | 생체리듬: %.3f\n"
        "\n"
        "위 감정 상태를 자연스럽게 반영하여 대화하세요.\n"
        "%s욕구가 강합니다. 이 상태를 표현하세요.",
        KC_DEE_VERSION, dee->tick,
        KC_EMOTION_NAMES[dom_emo], dom_val,
        dee->emo.fatigue, dee->emo.irritation,
        KC_HORMONE_NAMES[h1], dee->hor.level[h1]/dee->hor.h_max[h1],
        KC_HORMONE_NAMES[h2], dee->hor.level[h2]/dee->hor.h_max[h2],
        KC_DRIVE_NAMES[dom_drv], dom_dlv,
        goal_desc, goal_prog * 100.0f,
        dee->emo.affinity, dee->bio.current,
        KC_DRIVE_NAMES[dom_drv]
    );
}

/* ── 상태 출력 ────────────────────────────────────────────── */
void kc_dee_print(const KcEmotionEngine *dee) {
    if (!dee) return;
    printf("\n══════════════════════════════════════════\n");
    printf("  DEE v%s | tick=%u | t=%.1fs\n",
           KC_DEE_VERSION, dee->tick, dee->t_real);
    printf("══════════════════════════════════════════\n");

    printf("\n【감정 상태】\n");
    for (int j = 0; j < KC_EMO_COUNT; j++) {
        printf("  %-12s %.3f  ", KC_EMOTION_NAMES[j], dee->emo.e[j]);
        int bars = (int)(dee->emo.e[j] * 20);
        for (int b=0;b<20;b++) printf(b<bars?"█":"░");
        printf("\n");
    }
    printf("  피로=%.3f 짜증=%.3f 호감도=%.3f\n",
           dee->emo.fatigue, dee->emo.irritation, dee->emo.affinity);

    printf("\n【호르몬 농도】\n");
    for (int i = 0; i < KC_HOR_COUNT; i++) {
        float ratio = dee->hor.level[i] / dee->hor.h_max[i];
        printf("  %-16s %.3f  ", KC_HORMONE_NAMES[i], dee->hor.level[i]);
        int bars = (int)(ratio * 20);
        for (int b=0;b<20;b++) printf(b<bars?"█":"░");
        printf("\n");
    }

    printf("\n【욕구/드라이브】\n");
    for (int d = 0; d < KC_DRIVE_COUNT; d++) {
        printf("  %-8s lv=%.3f sat=%.3f %s\n",
               KC_DRIVE_NAMES[d],
               dee->drive.level[d],
               dee->drive.satisfaction[d],
               dee->drive.level[d] > dee->drive.threshold[d] ? "★능동" : "");
    }

    printf("\n【목표】\n");
    for (int i=0; i < dee->goals.st_cnt; i++) {
        if (dee->goals.st[i].active)
            printf("  단기: %s (%.0f%%)\n",
                   dee->goals.st[i].desc,
                   dee->goals.st[i].progress*100);
    }
    for (int i=0; i < dee->goals.lt_cnt; i++) {
        if (dee->goals.lt[i].active)
            printf("  장기: %s (%.0f%%)\n",
                   dee->goals.lt[i].desc,
                   dee->goals.lt[i].progress*100);
    }
    printf("══════════════════════════════════════════\n");
}
