/*
 * kc_active_ai.h — DEE AI 연동 + 탄생일 기반 초기화
 * Kcode DEE v1.2.0
 *
 * 최초 API 키 입력 시점 = AI 탄생일
 * 탄생일 기준 Bio Rhythm 시작
 * 탄생 감정 상태 영구 저장 → 매 구동 시 연속성 유지
 */
#ifndef KC_ACTIVE_AI_H
#define KC_ACTIVE_AI_H

#include "kc_hormone.h"
#include "kc_persona.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── AI 제공자 ──────────────────────────────────────────── */
typedef enum {
    KC_AI_CLAUDE  = 0,
    KC_AI_OPENAI  = 1,
    KC_AI_GEMINI  = 2,
    KC_AI_OLLAMA  = 3,  /* 로컬 무료 */
    KC_AI_CUSTOM  = 4,
} KcAIProvider;

/* ── 탄생 기록 ──────────────────────────────────────────── */
typedef struct {
    int64_t  born_unix;          /* 탄생 Unix timestamp      */
    char     born_str[32];       /* "2026-04-16 14:32:05"    */
    char     persona_code[5];     /* KP01~KP16 */            /* 탄생 시 설정된 KcPersona 코드 */
    char     name[64];           /* AI 이름 (선택)           */
    float    born_emotion[KC_EMO_COUNT]; /* 탄생 감정 스냅샷 */
    float    born_hormone[KC_HOR_COUNT]; /* 탄생 호르몬 스냅샷*/
    uint32_t session_count;      /* 총 구동 횟수             */
    int64_t  last_run_unix;      /* 마지막 구동 시각         */
    float    cumulative_affinity;/* 누적 호감도              */
    char     first_words[256];   /* 탄생 시 첫 AI 응답       */
} KcBirthRecord;

/* ── 설정 파일 구조 ─────────────────────────────────────── */
#define KC_AI_CONFIG_PATH  "~/.kcode/dee_config.json"
#define KC_AI_BIRTH_PATH   "~/.kcode/dee_birth.json"
#define KC_AI_STATE_PATH   "~/.kcode/dee_state.bin"

typedef struct {
    KcAIProvider provider;
    char   api_key[256];
    char   model[64];
    char   endpoint[256];
    int    timeout_ms;
    int    max_tokens;
    float  temperature;
    int    stream;
} KcAIConfig;

/* ── AI 엔진 핵심 구조체 ────────────────────────────────── */
typedef struct {
    KcAIConfig     cfg;
    KcBirthRecord  birth;
    KcEmotionEngine *dee;

    /* 연결 상태 */
    int    connected;
    char   last_error[256];

    /* 세션 통계 */
    int    msg_count;            /* 이번 세션 메시지 수      */
    double session_start_unix;   /* 이번 세션 시작 시각      */
} KcActiveAI;

/* ════════════════════════════════════════════════════════
 * 초기화 API
 * ════════════════════════════════════════════════════════ */

/*
 * 최초 실행: API 키 입력 → 탄생일 기록 → DEE 초기화
 * 이미 탄생 기록 있으면 → 연속 상태 복원
 */
KcActiveAI *kc_ai_init(const char *api_key,
                        KcAIProvider provider,
                        const char *persona_code,
                        const char *ai_name);

/* 설정 파일에서 자동 로드 (2번째 구동부터) */
KcActiveAI *kc_ai_load(void);

/* 환경변수에서 로드 */
KcActiveAI *kc_ai_load_env(void);

/* 소멸 + 상태 저장 */
void kc_ai_destroy(KcActiveAI *ai);

/* ════════════════════════════════════════════════════════
 * 탄생일 기반 Bio Rhythm
 * ════════════════════════════════════════════════════════ */

/*
 * 탄생 시각부터 현재까지 경과 시간으로 Bio Rhythm 위상 계산
 * kc_dee_tick() 호출 전 자동 적용
 */
void kc_ai_sync_bio(KcActiveAI *ai);

/* 탄생일부터 경과 시간 (초) */
double kc_ai_age_seconds(const KcActiveAI *ai);

/* 경과 시간 문자열 "3일 4시간 12분" */
void   kc_ai_age_str(const KcActiveAI *ai, char *out, int sz);

/* ════════════════════════════════════════════════════════
 * 대화 API
 * ════════════════════════════════════════════════════════ */

/* 감정 상태 → AI 응답 */
int kc_ai_chat(KcActiveAI *ai,
               const char *user_msg,
               char *out, int out_sz);

/* 능동 발화 (감정 임계값 초과 시 AI 먼저) */
int kc_ai_active_fire(KcActiveAI *ai,
                      char *out, int out_sz);

/* ════════════════════════════════════════════════════════
 * 상태 저장/복원
 * ════════════════════════════════════════════════════════ */

/* 현재 DEE 상태 저장 (~/.kcode/dee_state.bin) */
int  kc_ai_save_state(const KcActiveAI *ai);

/* 저장된 DEE 상태 복원 */
int  kc_ai_load_state(KcActiveAI *ai);

/* 탄생 기록 출력 */
void kc_ai_print_birth(const KcActiveAI *ai);

/* 현재 상태 요약 출력 */
void kc_ai_print_status(const KcActiveAI *ai);

/* ════════════════════════════════════════════════════════
 * 내부 유틸
 * ════════════════════════════════════════════════════════ */
const char *kc_ai_provider_name(KcAIProvider p);

#ifdef __cplusplus
}
#endif
#endif /* KC_ACTIVE_AI_H */
