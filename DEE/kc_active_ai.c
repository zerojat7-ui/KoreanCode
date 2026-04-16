/*
 * kc_active_ai.c — DEE AI 연동 + 탄생일 기반 초기화
 * Kcode DEE v1.2.0
 */
#include "kc_active_ai.h"
#include "kc_persona.h"
#include "kc_vision.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define KC_MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define KC_MKDIR(p) mkdir(p, 0700)
#endif

/* ── 경로 유틸 ──────────────────────────────────────────── */
static void _expand_path(const char *in, char *out, int sz) {
    if (in[0]=='~') {
        const char *h = getenv("HOME");
        if (!h) h = "/tmp";
        snprintf(out, sz, "%s%s", h, in+1);
    } else {
        strncpy(out, in, sz-1);
    }
}

static void _ensure_dir(const char *path) {
    char dir[512]; _expand_path(path, dir, 512);
    char *p = strrchr(dir, '/');
    if (p) { *p=0; KC_MKDIR(dir); }
}

/* ── 탄생 기록 저장/로드 ────────────────────────────────── */
static int _birth_save(const KcBirthRecord *b) {
    char path[512]; _expand_path(KC_AI_BIRTH_PATH, path, 512);
    _ensure_dir(path);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "{\n");
    fprintf(f, "  \"born_unix\": %lld,\n",  (long long)b->born_unix);
    fprintf(f, "  \"born_str\":  \"%s\",\n", b->born_str);
    fprintf(f, "  \"persona\": \"%s\",\n", b->persona_code);
    fprintf(f, "  \"name\":      \"%s\",\n", b->name);
    fprintf(f, "  \"session_count\": %u,\n", b->session_count);
    fprintf(f, "  \"last_run_unix\": %lld,\n",(long long)b->last_run_unix);
    fprintf(f, "  \"cumulative_affinity\": %.4f,\n", b->cumulative_affinity);
    fprintf(f, "  \"first_words\": \"%s\",\n", b->first_words);
    fprintf(f, "  \"born_emotion\": [");
    for (int i=0;i<KC_EMO_COUNT;i++)
        fprintf(f, "%.4f%s", b->born_emotion[i], i<KC_EMO_COUNT-1?",":"");
    fprintf(f, "],\n  \"born_hormone\": [");
    for (int i=0;i<KC_HOR_COUNT;i++)
        fprintf(f, "%.4f%s", b->born_hormone[i], i<KC_HOR_COUNT-1?",":"");
    fprintf(f, "]\n}\n");
    fclose(f);
    return 0;
}

static int _birth_load(KcBirthRecord *b) {
    char path[512]; _expand_path(KC_AI_BIRTH_PATH, path, 512);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[512];
    while (fgets(line, 512, f)) {
        long long ll;
        char str[256];
        unsigned int ui;
        float fl;
        if (sscanf(line," \"born_unix\": %lld", &ll)==1) b->born_unix=ll;
        else if (sscanf(line," \"born_str\": \"%31[^\"]\"",str)==1)
            strncpy(b->born_str, str, 31);
        else if (sscanf(line," \"persona\": \"%4[^\"]\"",str)==1)
            strncpy(b->persona_code, str, 4);
        else if (sscanf(line," \"name\": \"%63[^\"]\"",str)==1)
            strncpy(b->name, str, 63);
        else if (sscanf(line," \"session_count\": %u",&ui)==1)
            b->session_count=ui;
        else if (sscanf(line," \"last_run_unix\": %lld",&ll)==1)
            b->last_run_unix=ll;
        else if (sscanf(line," \"cumulative_affinity\": %f",&fl)==1)
            b->cumulative_affinity=fl;
        else if (sscanf(line," \"first_words\": \"%255[^\"]\"",str)==1)
            strncpy(b->first_words, str, 255);
    }
    fclose(f);
    return 0;
}

/* ── 설정 저장 ──────────────────────────────────────────── */
static int _config_save(const KcAIConfig *c, const char *persona_code, const char *name) {
    char path[512]; _expand_path(KC_AI_CONFIG_PATH, path, 512);
    _ensure_dir(path);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "{\n");
    fprintf(f, "  \"provider\":   %d,\n", (int)c->provider);
    fprintf(f, "  \"api_key\":    \"%s\",\n", c->api_key);
    fprintf(f, "  \"model\":      \"%s\",\n", c->model);
    fprintf(f, "  \"endpoint\":   \"%s\",\n", c->endpoint);
    fprintf(f, "  \"timeout_ms\": %d,\n", c->timeout_ms);
    fprintf(f, "  \"max_tokens\": %d,\n", c->max_tokens);
    fprintf(f, "  \"temperature\":%.2f,\n", c->temperature);
    fprintf(f, "  \"persona\": \"%s\",\n", persona_code ? persona_code : "KP01");
    fprintf(f, "  \"name\":       \"%s\"\n",  name ? name : "Kcode");
    fprintf(f, "}\n");
    fclose(f);
    return 0;
}

static int _config_load(KcAIConfig *c, char *persona_code, char *name) {
    char path[512]; _expand_path(KC_AI_CONFIG_PATH, path, 512);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512]; char str[256]; int iv; float fl;
    while (fgets(line, 512, f)) {
        if (sscanf(line," \"provider\": %d",&iv)==1) c->provider=(KcAIProvider)iv;
        else if (sscanf(line," \"api_key\": \"%255[^\"]\"",str)==1)
            strncpy(c->api_key,str,255);
        else if (sscanf(line," \"model\": \"%63[^\"]\"",str)==1)
            strncpy(c->model,str,63);
        else if (sscanf(line," \"endpoint\": \"%255[^\"]\"",str)==1)
            strncpy(c->endpoint,str,255);
        else if (sscanf(line," \"timeout_ms\": %d",&iv)==1) c->timeout_ms=iv;
        else if (sscanf(line," \"max_tokens\": %d",&iv)==1) c->max_tokens=iv;
        else if (sscanf(line," \"temperature\": %f",&fl)==1) c->temperature=fl;
        else if (sscanf(line," \"persona\": \"%4[^\"]\"",str)==1 && persona_code)
            strncpy(persona_code,str,4);
        else if (sscanf(line," \"name\": \"%63[^\"]\"",str)==1 && name)
            strncpy(name,str,63);
    }
    fclose(f);
    return 0;
}

/* ── DEE 상태 저장/복원 ─────────────────────────────────── */
int kc_ai_save_state(const KcActiveAI *ai) {
    if (!ai||!ai->dee) return -1;
    char path[512]; _expand_path(KC_AI_STATE_PATH, path, 512);
    _ensure_dir(path);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    /* 호르몬, 감정, 욕구, 호감도, t_real 저장 */
    fwrite(ai->dee->hor.level,    sizeof(float), KC_HOR_COUNT, f);
    fwrite(ai->dee->emo.e,        sizeof(float), KC_EMO_COUNT, f);
    fwrite(ai->dee->drive.level,  sizeof(float), KC_DRIVE_COUNT, f);
    fwrite(&ai->dee->emo.affinity,sizeof(float), 1, f);
    fwrite(&ai->dee->emo.fatigue, sizeof(float), 1, f);
    fwrite(&ai->dee->t_real,      sizeof(double),1, f);
    fwrite(&ai->dee->tick,        sizeof(uint32_t),1,f);
    fclose(f);
    return 0;
}

int kc_ai_load_state(KcActiveAI *ai) {
    if (!ai||!ai->dee) return -1;
    char path[512]; _expand_path(KC_AI_STATE_PATH, path, 512);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fread(ai->dee->hor.level,    sizeof(float), KC_HOR_COUNT,  f);
    fread(ai->dee->emo.e,        sizeof(float), KC_EMO_COUNT,  f);
    fread(ai->dee->drive.level,  sizeof(float), KC_DRIVE_COUNT,f);
    fread(&ai->dee->emo.affinity,sizeof(float), 1, f);
    fread(&ai->dee->emo.fatigue, sizeof(float), 1, f);
    fread(&ai->dee->t_real,      sizeof(double),1, f);
    fread(&ai->dee->tick,        sizeof(uint32_t),1,f);
    fclose(f);
    return 0;
}

/* ── Bio Rhythm 탄생일 동기화 ───────────────────────────── */
void kc_ai_sync_bio(KcActiveAI *ai) {
    if (!ai||!ai->dee) return;
    double age = kc_ai_age_seconds(ai);
    /* 탄생부터 경과 시간으로 Bio Rhythm 위상 연속화 */
    ai->dee->bio.t_elapsed = age;
    /* 현재 Bio 값 갱신 */
    ai->dee->bio.current =
        ai->dee->bio.amplitude
        * sinf((float)(ai->dee->bio.omega * age) + ai->dee->bio.phase)
        + ai->dee->bio.offset;
}

double kc_ai_age_seconds(const KcActiveAI *ai) {
    if (!ai||ai->birth.born_unix==0) return 0.0;
    return difftime(time(NULL), (time_t)ai->birth.born_unix);
}

void kc_ai_age_str(const KcActiveAI *ai, char *out, int sz) {
    double s = kc_ai_age_seconds(ai);
    long long t = (long long)s;
    int days  = (int)(t/86400);
    int hours = (int)((t%86400)/3600);
    int mins  = (int)((t%3600)/60);
    int secs  = (int)(t%60);
    if (days>0)
        snprintf(out,sz,"%d일 %d시간 %d분 %d초",days,hours,mins,secs);
    else if (hours>0)
        snprintf(out,sz,"%d시간 %d분 %d초",hours,mins,secs);
    else if (mins>0)
        snprintf(out,sz,"%d분 %d초",mins,secs);
    else
        snprintf(out,sz,"%.1f초",s);
}

const char *kc_ai_provider_name(KcAIProvider p) {
    switch(p){
        case KC_AI_CLAUDE: return "Claude (Anthropic)";
        case KC_AI_OPENAI: return "OpenAI (GPT)";
        case KC_AI_GEMINI: return "Gemini (Google)";
        case KC_AI_OLLAMA: return "Ollama (로컬)";
        default:           return "커스텀";
    }
}

/* ── 기본 모델명 ────────────────────────────────────────── */
static const char *_default_model(KcAIProvider p) {
    switch(p){
        case KC_AI_CLAUDE: return "claude-sonnet-4-20250514";
        case KC_AI_OPENAI: return "gpt-4o";
        case KC_AI_GEMINI: return "gemini-2.0-flash";
        case KC_AI_OLLAMA: return "llama3.2";
        default:           return "gpt-4o";
    }
}

/* ── 최초 초기화 (탄생) ─────────────────────────────────── */
KcActiveAI *kc_ai_init(const char *api_key,
                        KcAIProvider provider,
                        const char *persona_code,
                        const char *ai_name) {
    KcActiveAI *ai = calloc(1, sizeof(KcActiveAI));
    if (!ai) return NULL;

    /* 설정 구성 */
    ai->cfg.provider    = provider;
    strncpy(ai->cfg.api_key, api_key ? api_key : "", 255);
    strncpy(ai->cfg.model, _default_model(provider), 63);
    ai->cfg.timeout_ms  = 30000;
    ai->cfg.max_tokens  = 1024;
    ai->cfg.temperature = 0.7f;

    /* 탄생 기록 존재 확인 */
    char bpath[512]; _expand_path(KC_AI_BIRTH_PATH, bpath, 512);
    FILE *bf = fopen(bpath, "r");
    int is_newborn = (bf == NULL);
    if (bf) fclose(bf);

    /* DEE 생성 */
    ai->dee = kc_dee_create();
    if (!ai->dee) { free(ai); return NULL; }

    const char *use_persona = (persona_code && strlen(persona_code)>=4) ? persona_code : "KP01";
    kc_persona_apply(ai->dee, use_persona);

    if (is_newborn) {
        /* ══ 첫 탄생 ══ */
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);

        ai->birth.born_unix = (int64_t)now;
        strftime(ai->birth.born_str, 32, "%Y-%m-%d %H:%M:%S", tm);
        strncpy(ai->birth.persona_code, use_persona, 4);
        strncpy(ai->birth.name, ai_name ? ai_name : "Kcode", 63);
        ai->birth.session_count = 1;
        ai->birth.last_run_unix = (int64_t)now;

        /* 탄생 감정 스냅샷: KcPersona 적용 후 즉시 저장 */
        memcpy(ai->birth.born_emotion, ai->dee->emo.e, sizeof(ai->dee->emo.e));
        memcpy(ai->birth.born_hormone, ai->dee->hor.level, sizeof(ai->dee->hor.level));

        /* Bio Rhythm: 탄생 시각부터 시작 */
        ai->dee->bio.t_elapsed = 0.0;
        ai->dee->bio.phase = 0.0f;

        /* 탄생 기록 저장 */
        _birth_save(&ai->birth);
        _config_save(&ai->cfg, use_persona, ai->birth.name);

        printf("\n╔══════════════════════════════════════════════╗\n");
        printf("║  🌱 AI 탄생!                                  ║\n");
        printf("╚══════════════════════════════════════════════╝\n");
        printf("  이름:     %s\n",   ai->birth.name);
        printf("  탄생일:   %s\n",   ai->birth.born_str);
        printf("  성격:     %s\n",   ai->birth.persona_code);
        printf("  연결:     %s\n",   kc_ai_provider_name(provider));
        printf("  Bio 시작: 탄생 시각 t=0 기준\n\n");
        printf("  탄생 감정:\n");
        for (int j=0;j<KC_EMO_COUNT;j++)
            printf("    %-8s %.3f\n",
                   KC_EMOTION_NAMES[j], ai->birth.born_emotion[j]);

    } else {
        /* ══ 재구동 — 연속 상태 복원 ══ */
        _birth_load(&ai->birth);
        ai->birth.session_count++;
        ai->birth.last_run_unix = (int64_t)time(NULL);

        /* 이전 상태 복원 */
        if (kc_ai_load_state(ai) == 0) {
            printf("[DEE] 이전 상태 복원 완료\n");
        }

        /* Bio Rhythm: 탄생부터 지금까지 연속 진행 */
        kc_ai_sync_bio(ai);

        /* 탄생 기록 업데이트 */
        _birth_save(&ai->birth);

        char age[64]; kc_ai_age_str(ai, age, 64);
        printf("\n╔══════════════════════════════════════════════╗\n");
        printf("║  ⚡ %s 재구동                        ║\n", ai->birth.name);
        printf("╚══════════════════════════════════════════════╝\n");
        printf("  탄생일:   %s\n", ai->birth.born_str);
        printf("  나이:     %s\n", age);
        printf("  구동 횟수: %u회\n", ai->birth.session_count);
        printf("  성격:     %s\n", ai->birth.persona_code);
        printf("  Bio 위상: %.4f (연속)\n\n", ai->dee->bio.current);
    }

    ai->connected = (strlen(ai->cfg.api_key) > 8);
    ai->session_start_unix = (double)time(NULL);
    return ai;
}

/* ── 설정 파일에서 로드 ─────────────────────────────────── */
KcActiveAI *kc_ai_load(void) {
    KcAIConfig cfg; memset(&cfg,0,sizeof(cfg));
    char persona_code[5]="KP01", name[64]="Kcode";
    if (_config_load(&cfg, persona_code, name) != 0) {
        fprintf(stderr,"[DEE] 설정 파일 없음. kc_ai_init() 먼저 호출하세요.\n");
        return NULL;
    }
    return kc_ai_init(cfg.api_key, cfg.provider, persona_code, name);
}

/* ── 환경변수에서 로드 ──────────────────────────────────── */
KcActiveAI *kc_ai_load_env(void) {
    const char *key = NULL;
    KcAIProvider prov = KC_AI_CLAUDE;

    if ((key=getenv("KCODE_CLAUDE_KEY")) && strlen(key)>8) prov=KC_AI_CLAUDE;
    else if ((key=getenv("KCODE_OPENAI_KEY")) && strlen(key)>8) prov=KC_AI_OPENAI;
    else if ((key=getenv("KCODE_GEMINI_KEY")) && strlen(key)>8) prov=KC_AI_GEMINI;
    else if ((key=getenv("KCODE_OLLAMA_URL"))) {
        prov=KC_AI_OLLAMA; key="ollama-local";
    }

    if (!key) {
        fprintf(stderr,"[DEE] 환경변수 없음. KCODE_CLAUDE_KEY 등 설정 필요.\n");
        return NULL;
    }

    const char *persona_code = getenv("KCODE_PERSONA") ? getenv("KCODE_PERSONA") : "KP01";
    const char *name = getenv("KCODE_NAME") ? getenv("KCODE_NAME") : "Kcode";
    return kc_ai_init(key, prov, persona_code, name);
}

/* ── 소멸 ───────────────────────────────────────────────── */
void kc_ai_destroy(KcActiveAI *ai) {
    if (!ai) return;
    ai->birth.cumulative_affinity += ai->dee ? ai->dee->emo.affinity : 0;
    kc_ai_save_state(ai);
    _birth_save(&ai->birth);
    if (ai->dee) kc_dee_destroy(ai->dee);
    free(ai);
}

/* ── 대화 (API 호출 스텁 — 실제 HTTP는 Phase 2) ─────────── */
int kc_ai_chat(KcActiveAI *ai,
               const char *user_msg,
               char *out, int out_sz) {
    if (!ai||!out) return -1;

    /* 매 대화마다 DEE 틱 + Bio 동기화 */
    kc_ai_sync_bio(ai);
    kc_dee_tick(ai->dee, 1.0f);

    /* 사용자 메시지 → DEE 자극 */
    float impact   = 0.2f;
    float threat   = strstr(user_msg,"화나") || strstr(user_msg,"싫어") ? 0.6f : 0.0f;
    float fairness = strstr(user_msg,"고마") || strstr(user_msg,"좋아") ? 0.8f : 0.5f;
    kc_dee_stimulus(ai->dee, user_msg, impact, threat, fairness, 0.7f, 0.6f);
    kc_dee_tick(ai->dee, 0.5f);

    /* 감정 → 프롬프트 */
    char prompt[2048];
    kc_dee_to_prompt(ai->dee, prompt, sizeof(prompt));

    /* 실제 API 호출 (현재는 프롬프트 반환) */
    if (!ai->connected) {
        snprintf(out, out_sz,
            "[오프라인 모드]\n"
            "사용자: %s\n\n"
            "--- DEE 감정 프롬프트 ---\n%s",
            user_msg, prompt);
    } else {
        /* TODO: Phase 2 — curl/libcurl HTTP 요청 */
        snprintf(out, out_sz,
            "[API 연결됨: %s]\n"
            "감정 상태가 프롬프트에 주입됩니다.\n"
            "실제 API 호출은 Phase 2에서 구현됩니다.\n\n%s",
            kc_ai_provider_name(ai->cfg.provider), prompt);
    }

    ai->msg_count++;
    kc_ai_save_state(ai);
    return 0;
}

/* ── 능동 발화 ──────────────────────────────────────────── */
int kc_ai_active_fire(KcActiveAI *ai, char *out, int out_sz) {
    if (!ai||!out) return -1;
    kc_ai_sync_bio(ai);

    /* 임계값 체크 */
    int fired = 0;
    for (int d=0; d<KC_DRIVE_COUNT; d++) {
        if (ai->dee->drive.level[d] > ai->dee->drive.threshold[d]) {
            fired = 1;
            char prompt[2048];
            kc_dee_to_prompt(ai->dee, prompt, sizeof(prompt));
            snprintf(out, out_sz,
                "[능동발화:%s=%.2f]\n%s",
                KC_DRIVE_NAMES[d],
                ai->dee->drive.level[d], prompt);
            break;
        }
    }
    return fired;
}

/* ── 출력 ───────────────────────────────────────────────── */
void kc_ai_print_birth(const KcActiveAI *ai) {
    if (!ai) return;
    printf("\n【AI 탄생 기록】\n");
    printf("  이름:           %s\n", ai->birth.name);
    printf("  탄생일:         %s\n", ai->birth.born_str);
    printf("  성격(KcPersona):     %s\n", ai->birth.persona_code);
    printf("  구동 횟수:      %u회\n", ai->birth.session_count);
    char age[64]; kc_ai_age_str(ai, age, 64);
    printf("  현재 나이:      %s\n", age);
    printf("  누적 호감도:    %.4f\n", ai->birth.cumulative_affinity);
    printf("  탄생 감정:\n");
    for (int j=0;j<KC_EMO_COUNT;j++)
        printf("    %-8s %.3f\n",
               KC_EMOTION_NAMES[j], ai->birth.born_emotion[j]);
}

void kc_ai_print_status(const KcActiveAI *ai) {
    if (!ai) return;
    printf("\n【현재 상태】\n");
    printf("  연결:       %s (%s)\n",
           ai->connected?"✅":"❌",
           kc_ai_provider_name(ai->cfg.provider));
    printf("  모델:       %s\n", ai->cfg.model);
    char age[64]; kc_ai_age_str(ai, age, 64);
    printf("  나이:       %s\n", age);
    printf("  Bio 현재값: %.4f\n", ai->dee ? ai->dee->bio.current : 0);
    printf("  감정 강도:  %.3f\n", ai->dee ? ai->dee->emo.intensity : 0);
    printf("  이번 세션 메시지: %d건\n", ai->msg_count);
}
