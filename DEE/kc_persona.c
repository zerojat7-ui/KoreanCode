/*
 * kc_persona.c — KcPersona 성격 프로필 구현
 * Kcode DEE v1.3.0
 *
 * Jung(1921) 4축 이론 기반 독자 구현
 * Reference: Jung, C.G. (1921). Psychologische Typen.
 */
#include "kc_persona.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ── 런타임 초기화 함수 ──────────────────────────────── */
static void _init(KcPersonaProfile *p,
    const char *code, const char *name,
    float kpe, float kpn, float kpf, float kpj,
    float rr, float dr, float wl, float we,
    float td, float ik, float cb, float nd,
    float b0,float b1,float b2,float b3,float b4,
    float b5,float b6,float b7,float b8,float b9)
{
    strncpy(p->code, code, 4); p->code[4]=0;
    strncpy(p->name, name, 15); p->name[15]=0;
    p->axis.kpe=kpe; p->axis.kpn=kpn;
    p->axis.kpf=kpf; p->axis.kpj=kpj;
    p->energy_recharge_rate=rr; p->social_drain_rate=dr;
    p->w_logic=wl; p->w_empathy=we;
    p->tolerance_decay=td; p->irritation_k=ik;
    p->curiosity_base=cb; p->novelty_dopamine=nd;
    p->hor_bias[0]=b0; p->hor_bias[1]=b1; p->hor_bias[2]=b2;
    p->hor_bias[3]=b3; p->hor_bias[4]=b4; p->hor_bias[5]=b5;
    p->hor_bias[6]=b6; p->hor_bias[7]=b7; p->hor_bias[8]=b8;
    p->hor_bias[9]=b9;
}

KcPersonaProfile KC_PERSONA_PROFILES[16];
static int _ready = 0;

static void _ensure(void) {
    if (_ready) return; _ready=1;
    /*           code    name      kpe  kpn  kpf  kpj   rr    dr    wl    we   td     ik    cb    nd    H편향(도파/세로/코르/옥시/노르/엔도/아드/멜라/테스/에스) */
    _init(&KC_PERSONA_PROFILES[ 0],"KP01","탐험가",0.8f,0.8f,0.7f,0.3f,0.2f,0.1f,0.30f,0.70f,0.04f,0.08f,0.70f,0.90f,+0.4f,+0.1f,-0.1f,+0.4f,+0.1f,+0.1f,-0.1f,-0.2f,+0.2f,+0.3f);
    _init(&KC_PERSONA_PROFILES[ 1],"KP02","전략가",0.2f,0.8f,0.2f,0.8f,0.8f,0.5f,0.90f,0.30f,0.01f,0.05f,0.50f,0.70f,-0.1f,+0.3f,-0.1f,-0.2f,-0.1f,+0.1f,-0.2f,+0.3f,-0.1f,-0.2f);
    _init(&KC_PERSONA_PROFILES[ 2],"KP03","중재자",0.2f,0.8f,0.8f,0.3f,0.8f,0.4f,0.30f,0.80f,0.02f,0.06f,0.60f,0.70f,+0.1f,+0.2f,-0.1f,+0.3f,-0.1f,+0.3f,-0.2f,+0.2f,+0.1f,+0.4f);
    _init(&KC_PERSONA_PROFILES[ 3],"KP04","변론가",0.8f,0.8f,0.3f,0.3f,0.2f,0.1f,0.70f,0.40f,0.03f,0.07f,0.80f,0.90f,+0.3f,+0.1f,-0.1f,+0.1f,+0.2f,+0.1f,-0.1f,-0.1f,+0.1f,+0.1f);
    _init(&KC_PERSONA_PROFILES[ 4],"KP05","통솔자",0.8f,0.8f,0.2f,0.8f,0.2f,0.1f,0.90f,0.30f,0.01f,0.06f,0.40f,0.60f,+0.2f,+0.1f,+0.1f,+0.1f,+0.2f,-0.1f,+0.1f,-0.1f,+0.1f,-0.1f);
    _init(&KC_PERSONA_PROFILES[ 5],"KP06","선도자",0.3f,0.8f,0.7f,0.7f,0.7f,0.4f,0.50f,0.70f,0.01f,0.04f,0.50f,0.60f,+0.1f,+0.3f,-0.1f,+0.4f,-0.1f,+0.2f,-0.2f,+0.2f,-0.1f,+0.3f);
    _init(&KC_PERSONA_PROFILES[ 6],"KP07","사회자",0.8f,0.8f,0.8f,0.7f,0.2f,0.1f,0.40f,0.80f,0.01f,0.05f,0.40f,0.50f,+0.2f,+0.2f,-0.1f,+0.5f,-0.1f,+0.2f,-0.1f,-0.1f,+0.1f,+0.4f);
    _init(&KC_PERSONA_PROFILES[ 7],"KP08","논리가",0.2f,0.8f,0.2f,0.3f,0.8f,0.4f,0.80f,0.40f,0.02f,0.04f,0.70f,0.80f,+0.2f,+0.1f,-0.1f,-0.2f,+0.1f,+0.2f,-0.2f,+0.1f,+0.1f,-0.1f);
    _init(&KC_PERSONA_PROFILES[ 8],"KP09","수호자",0.2f,0.2f,0.7f,0.8f,0.8f,0.4f,0.50f,0.60f,0.01f,0.03f,0.20f,0.20f,-0.1f,+0.3f,-0.1f,+0.4f,-0.2f,+0.1f,-0.2f,+0.2f,-0.2f,+0.2f);
    _init(&KC_PERSONA_PROFILES[ 9],"KP10","관리자",0.2f,0.2f,0.2f,0.8f,0.8f,0.5f,0.80f,0.40f,0.01f,0.03f,0.20f,0.30f,-0.1f,+0.4f,+0.1f,+0.1f,-0.2f,-0.1f,-0.2f,+0.3f,-0.2f,-0.1f);
    _init(&KC_PERSONA_PROFILES[10],"KP11","활동가",0.8f,0.2f,0.7f,0.7f,0.2f,0.1f,0.30f,0.70f,0.01f,0.04f,0.20f,0.30f,+0.1f,+0.2f,-0.1f,+0.5f,-0.1f,+0.1f,-0.1f,-0.1f, 0.0f,+0.3f);
    _init(&KC_PERSONA_PROFILES[11],"KP12","감독관",0.8f,0.2f,0.2f,0.8f,0.2f,0.1f,0.80f,0.30f,0.01f,0.04f,0.20f,0.30f, 0.0f,+0.2f,+0.1f,+0.2f,-0.1f,-0.1f,-0.1f, 0.0f,-0.1f,-0.1f);
    _init(&KC_PERSONA_PROFILES[12],"KP13","모험가",0.2f,0.2f,0.7f,0.3f,0.8f,0.4f,0.30f,0.60f,0.03f,0.07f,0.40f,0.40f,+0.1f,+0.1f,-0.1f,+0.2f,-0.1f,+0.2f,-0.1f,+0.1f, 0.0f,+0.2f);
    _init(&KC_PERSONA_PROFILES[13],"KP14","기술자",0.2f,0.2f,0.2f,0.3f,0.9f,0.5f,0.70f,0.30f,0.03f,0.06f,0.40f,0.50f, 0.0f,+0.1f,-0.1f,-0.1f,+0.1f,+0.1f,-0.1f,+0.1f,+0.1f,-0.1f);
    _init(&KC_PERSONA_PROFILES[14],"KP15","연예인",0.8f,0.2f,0.8f,0.3f,0.2f,0.1f,0.20f,0.70f,0.04f,0.09f,0.40f,0.50f,+0.4f,+0.1f,-0.1f,+0.4f, 0.0f,+0.2f, 0.0f,-0.2f,+0.1f,+0.2f);
    _init(&KC_PERSONA_PROFILES[15],"KP16","사업가",0.8f,0.2f,0.3f,0.3f,0.2f,0.1f,0.60f,0.40f,0.04f,0.09f,0.40f,0.60f,+0.3f,+0.1f,-0.1f,+0.1f,+0.1f,+0.1f,+0.1f,-0.1f,+0.1f, 0.0f);
}

/* ── 검색 ─────────────────────────────────────────────── */
const KcPersonaProfile *kc_persona_find(const char *q) {
    _ensure();
    if (!q) return NULL;
    for (int i=0;i<16;i++) {
        if (strcmp(KC_PERSONA_PROFILES[i].code, q)==0) return &KC_PERSONA_PROFILES[i];
        if (strcmp(KC_PERSONA_PROFILES[i].name, q)==0) return &KC_PERSONA_PROFILES[i];
    }
    return NULL;
}

/* ── 4축 수치로 가장 가까운 타입 매칭 ─────────────────── */
const KcPersonaProfile *kc_persona_match(float kpe, float kpn,
                                          float kpf, float kpj) {
    _ensure();
    int   best=0;
    float best_d=1e9f;
    for (int i=0;i<16;i++) {
        float de=kpe-KC_PERSONA_PROFILES[i].axis.kpe;
        float dn=kpn-KC_PERSONA_PROFILES[i].axis.kpn;
        float df=kpf-KC_PERSONA_PROFILES[i].axis.kpf;
        float dj=kpj-KC_PERSONA_PROFILES[i].axis.kpj;
        float d=sqrtf(de*de+dn*dn+df*df+dj*dj);
        if (d<best_d){best_d=d;best=i;}
    }
    return &KC_PERSONA_PROFILES[best];
}

/* ── DEE 에 성격 적용 ─────────────────────────────────── */
void kc_persona_apply(KcEmotionEngine *dee, const char *q) {
    _ensure();
    if (!dee||!q) return;
    const KcPersonaProfile *p = kc_persona_find(q);
    if (!p){ fprintf(stderr,"[KcPersona] 알 수 없는 타입: %s\n",q); return; }

    /* 호르몬 편향 적용 */
    for (int i=0;i<KC_HOR_COUNT;i++){
        dee->hor.level[i] *= (1.0f + p->hor_bias[i]*0.5f);
        if (dee->hor.level[i]<0.05f) dee->hor.level[i]=0.05f;
        if (dee->hor.level[i]>dee->hor.h_max[i]) dee->hor.level[i]=dee->hor.h_max[i];
    }

    /* 욕구 임계값 조정 */
    dee->drive.threshold[KC_DRIVE_CONNECT]    = p->axis.kpe < 0.5f ? 0.85f : 0.60f;
    dee->drive.threshold[KC_DRIVE_UNDERSTAND] = p->curiosity_base > 0.5f ? 0.60f : 0.80f;

    /* 감정 초기값 */
    dee->emo.e[KC_EMO_CURIOSITY] = p->curiosity_base;
    dee->emo.regulation = p->w_logic > 0.6f ? 0.85f : 0.70f;

    /* Bio Rhythm: 활동형(KPE↑) = 활발한 리듬, 성찰형(KPE↓) = 안정적 리듬 */
    dee->bio.amplitude = 0.10f + p->axis.kpe * 0.20f;
    dee->bio.omega     = p->axis.kpe > 0.5f
                       ? (2.0f*3.14159f/43200.0f)   /* 12h 주기 — 활동형 */
                       : (2.0f*3.14159f/86400.0f);  /* 24h 주기 — 성찰형 */

    printf("[KcPersona] %s (%s) 적용 | "
           "KPE=%.1f KPN=%.1f KPF=%.1f KPJ=%.1f | "
           "호기심=%.2f 논리=%.2f\n",
           p->name, p->code,
           p->axis.kpe, p->axis.kpn,
           p->axis.kpf, p->axis.kpj,
           p->curiosity_base, p->w_logic);
}

/* ── 전체 목록 출력 ───────────────────────────────────── */
void kc_persona_print_all(void) {
    _ensure();
    printf("\n KcPersona 16종 (Kcode DEE v1.3.0)\n");
    printf(" %-6s %-8s  KPE  KPN  KPF  KPJ  호기심  논리  공감\n",
           "코드","이름");
    printf(" %-6s %-8s  ---  ---  ---  ---  ------  ----  ----\n","------","--------");
    for (int i=0;i<16;i++) {
        KcPersonaProfile *p=&KC_PERSONA_PROFILES[i];
        printf(" %-6s %-8s  %.1f  %.1f  %.1f  %.1f  %.2f    %.2f  %.2f\n",
               p->code, p->name,
               p->axis.kpe, p->axis.kpn,
               p->axis.kpf, p->axis.kpj,
               p->curiosity_base,
               p->w_logic, p->w_empathy);
    }
}
