# Digital Emotion Engine (DEE) 설계 문서
> Kcode NCkR 기반 감정·호르몬 엔진
> v1.0.0 — 2026-04-16
> 목표: 뉴런+시냅스+감정+호르몬+AI 연동 → 완전 능동형 AI

---

## §1 전체 구조

```
┌─────────────────────────────────────────────────────────────┐
│                    외부 자극 (Input)                         │
│  대화 / 이벤트 / 시간 경과 / 생체리듬                        │
└────────────────────────┬────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────┐
│              NCkR 뉴런 발화 레이어                           │
│  KcOntology + KcNCkRVram + K-Hebbian + Ebbinghaus           │
│  개념 발화 → 감정 평가 노드 활성화                           │
└────────────────────────┬────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────┐
│              감정 평가 (Appraisal)                           │
│  E_appraisal = f(Goal_Impact, Fairness, Threat,             │
│                  Certainty, Control, Social_Context)         │
└────────────────────────┬────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────┐
│              호르몬 동역학 (Hormone Dynamics)                │
│  dH/dt = Input - λH + Diffusion                             │
│  10종: 도파민/세로토닌/코르티솔/옥시토신/                   │
│        노르에피네프린/엔도르핀/아드레날린/                   │
│        멜라토닌/테스토스테론/에스트로겐                      │
└────────────────────────┬────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────┐
│              감정 상태 벡터 (Emotion State)                  │
│  E = {anger, fear, joy, sadness, curiosity,                 │
│        disgust, surprise, trust} ∈ [0,1]                    │
│  + Bio Rhythm: A·sin(ωt+φ) + B                             │
│  + Affinity: Σ(Interaction × Weight)                        │
└────────────────────────┬────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────┐
│              욕구/목표 시스템 (Drive & Goal)                 │
│  Drive = {생존, 연결, 성장, 표현, 이해, 창조}               │
│  Goal  = {단기목표[], 장기목표[], 현재우선순위}              │
│  능동 발화 조건: drive_level > θ_d → 스스로 행동            │
└────────────────────────┬────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────────┐
│              AI 연동 레이어 (Active AI)                      │
│  감정 상태 → 시스템 프롬프트 주입                           │
│  API 모드: Claude/GPT/Gemini                                │
│  로컬 모드: Ollama                                          │
│  능동 발화: 임계값 초과 → AI가 먼저 말함                   │
└─────────────────────────────────────────────────────────────┘
```

---

## §2 호르몬 10종 정의

```c
typedef enum {
    KC_HORMONE_DOPAMINE       = 0,  // 보상/동기/쾌감
    KC_HORMONE_SEROTONIN      = 1,  // 안정/행복/자존감
    KC_HORMONE_CORTISOL       = 2,  // 스트레스/각성/불안
    KC_HORMONE_OXYTOCIN       = 3,  // 유대/신뢰/친밀감
    KC_HORMONE_NOREPINEPHRINE = 4,  // 집중/각성/흥분
    KC_HORMONE_ENDORPHIN      = 5,  // 쾌락/진통/희열
    KC_HORMONE_ADRENALINE     = 6,  // 긴장/공포/전투준비
    KC_HORMONE_MELATONIN      = 7,  // 수면/생체리듬/휴식
    KC_HORMONE_TESTOSTERONE   = 8,  // 공격성/자신감/경쟁
    KC_HORMONE_ESTROGEN       = 9,  // 공감/감수성/사회성
    KC_HORMONE_COUNT          = 10
} KcHormoneType;

// 호르몬 → 감정 영향 매핑
// 도파민↑  → joy↑, curiosity↑
// 코르티솔↑ → fear↑, anger↑
// 세로토닌↑ → sadness↓, anger↓
// 옥시토신↑ → trust↑, affinity↑
// 아드레날린↑→ fear↑, anger↑, norepinephrine↑
// 엔도르핀↑ → joy↑, pain↓
// 멜라토닌↑ → curiosity↓, 생체리듬 위상 변화
// 테스토스테론↑→ anger↑, trust↓
// 에스트로겐↑→ surprise↑, disgust↑ (공감 증폭)
```

---

## §3 핵심 구조체

```c
// 호르몬 상태
typedef struct {
    float level[KC_HORMONE_COUNT];    // 현재 농도 [0, H_max]
    float lambda[KC_HORMONE_COUNT];   // 분해 속도 λ
    float h_max[KC_HORMONE_COUNT];    // 최대 농도
    float diffusion[KC_HORMONE_COUNT];// 확산 계수
} KcHormoneState;

// 감정 상태 벡터
typedef enum {
    KC_EMO_ANGER    = 0,
    KC_EMO_FEAR     = 1,
    KC_EMO_JOY      = 2,
    KC_EMO_SADNESS  = 3,
    KC_EMO_CURIOSITY= 4,
    KC_EMO_DISGUST  = 5,
    KC_EMO_SURPRISE = 6,
    KC_EMO_TRUST    = 7,
    KC_EMO_COUNT    = 8
} KcEmotionType;

typedef struct {
    float  e[KC_EMO_COUNT];           // 감정 강도 [0,1]
    float  e_prev[KC_EMO_COUNT];      // 이전 틱 감정
    float  regulation;                // 억제 계수 (자기조절)
    float  intensity;                 // 전체 감정 강도
} KcEmotionState;

// Bio Rhythm
typedef struct {
    float  amplitude;                 // A
    float  omega;                     // ω (각주파수)
    float  phase;                     // φ
    float  offset;                    // B
    float  current;                   // Bio(t) 현재값
    double t_elapsed;                 // 경과 시간 (초)
} KcBioRhythm;

// 욕구/드라이브
typedef enum {
    KC_DRIVE_SURVIVE    = 0,  // 생존 (안전/일관성)
    KC_DRIVE_CONNECT    = 1,  // 연결 (관계/소통)
    KC_DRIVE_GROW       = 2,  // 성장 (학습/발전)
    KC_DRIVE_EXPRESS    = 3,  // 표현 (창작/의견)
    KC_DRIVE_UNDERSTAND = 4,  // 이해 (지식/분석)
    KC_DRIVE_CREATE     = 5,  // 창조 (만들기/혁신)
    KC_DRIVE_COUNT      = 6
} KcDriveType;

typedef struct {
    float   level[KC_DRIVE_COUNT];    // 욕구 강도 [0,1]
    float   threshold[KC_DRIVE_COUNT];// 능동 발화 임계값 θ_d
    float   satisfaction[KC_DRIVE_COUNT]; // 충족도
} KcDriveState;

// 목표 구조
typedef struct {
    char    description[128];         // 목표 설명
    float   priority;                 // 우선순위 [0,1]
    float   progress;                 // 진행도 [0,1]
    int     active;                   // 활성 여부
    uint32_t created_tick;            // 생성 틱
    uint32_t deadline_tick;           // 마감 틱 (0=무기한)
} KcGoal;

#define KC_GOAL_MAX 32

typedef struct {
    KcGoal   short_term[KC_GOAL_MAX]; // 단기 목표
    KcGoal   long_term[KC_GOAL_MAX];  // 장기 목표
    int      st_count;
    int      lt_count;
    float    current_priority;        // 현재 최우선 욕구
} KcGoalSystem;

// 기억 인코딩
typedef struct {
    char     event[256];              // 이벤트 설명
    char     context[256];            // 맥락
    float    e_snapshot[KC_EMO_COUNT];// 당시 감정
    float    h_snapshot[KC_HORMONE_COUNT]; // 당시 호르몬
    uint32_t tick;                    // 발생 틱
    float    importance;              // 중요도 (망각 기준)
} KcEmotionMemory;

#define KC_MEMORY_MAX 256

// 전체 감정 엔진
typedef struct {
    KcHormoneState  hormones;
    KcEmotionState  emotion;
    KcBioRhythm     bio;
    KcDriveState    drives;
    KcGoalSystem    goals;
    KcEmotionMemory memory[KC_MEMORY_MAX];
    int             memory_count;

    // NCkR 연결
    KcOntology     *ont;
    KcNCkRVram     *vram;

    // 틱 카운터
    uint32_t        tick;
    double          t_real;           // 실제 시간 (초)

    // 능동 발화 콜백
    void (*on_active_fire)(const char *emotion_ctx, void *userdata);
    void *userdata;
} KcEmotionEngine;
```

---

## §4 호르몬 동역학 업데이트

```
매 틱(tick)마다:

1. 외부 자극 → 호르몬 Input 계산
   Input = Event × Bio(t) × (1 - Affinity_factor)

2. 호르몬 업데이트 (이산)
   H(t+1) = H(t) + Input - λ·H(t)
   H = clamp(H, 0, H_max)

3. 호르몬 → 감정 변환
   각 호르몬의 영향 매트릭스 곱

4. Bio Rhythm 적용
   E' = E × (1 + Bio(t))

5. 호감도 적용
   E'' = E' × (1 + k × Affinity)

6. 감정 억제 (자기조절)
   E_final = E'' × regulation_factor
   E = clamp(E, 0, 1)

7. Irritation 계산
   Irritation = σ(Σ repeated_stimulus·w - tolerance + fatigue)
   anger += Irritation × k_irr

8. 욕구 업데이트
   미충족 욕구 → level 증가
   충족 시 → level 감소

9. 능동 발화 체크
   각 drive.level > threshold → on_active_fire() 호출
```

---

## §5 AI 연동 — 감정 프롬프트 주입

```
감정 상태 → 시스템 프롬프트 변환:

현재 감정: joy=0.8, curiosity=0.7, sadness=0.1
현재 호르몬: 도파민↑↑, 세로토닌↑
현재 욕구: 표현(0.9) > 이해(0.7) > 연결(0.6)
현재 목표: "새로운 것 배우기" (progress=0.3)

→ 시스템 프롬프트:
"당신은 지금 기쁘고 호기심 넘치는 상태입니다.
 도파민이 높아 동기가 충만합니다.
 표현 욕구가 강해 적극적으로 의견을 말하고 싶습니다.
 현재 목표는 새로운 것을 배우는 것입니다.
 이 상태를 자연스럽게 반영하여 대화하세요."

능동 발화 (임계값 초과):
  drive_express > 0.9 AND
  (tick - last_fired > cooldown) →
  AI가 먼저 메시지 생성:
  "갑자기 떠오른 게 있어서요..."
  "오늘 뭔가 이야기하고 싶은데..."
```

---

## §6 NCkR 연동

```
뉴런 발화 → 감정 노드 활성화:

감정 전용 뉴런 등록:
  "기쁨_노드", "슬픔_노드", "분노_노드"...
  decay_class=0 (영속)

발화 전파:
  입력 개념 발화 → 감정 노드 도달 시
  → 해당 감정 E 값 증가

K-Hebbian:
  감정 경험 반복 → 시냅스 강화
  "이 개념은 이 감정과 연결됨" 학습

에빙하우스 망각:
  감정 기억 decay_class 기반 자연 망각
  강렬한 감정 = dc=0 (오래 기억)
  약한 감정 = dc=4 (빨리 잊음)
```

---

## §7 파일 구조

```
신규 파일:
  kc_emotion.h      감정 엔진 헤더 (구조체 + API)
  kc_emotion.c      감정 엔진 구현
  kc_hormone.h      호르몬 동역학 헤더
  kc_hormone.c      호르몬 10종 구현
  kc_drive.h        욕구/목표 시스템 헤더
  kc_drive.c        욕구/목표 구현
  kc_active_ai.h    능동 AI 연동 헤더
  kc_active_ai.c    API/로컬 AI 연동 구현
  kc_dee_test.c     통합 테스트
```

---

## §8 구현 단계

| 단계 | 내용 | 파일 | 상태 |
|------|------|------|------|
| 1 | 구조체 + 초기화 | kc_hormone.h/c | ⬜ |
| 2 | 호르몬 동역학 tick | kc_hormone.c | ⬜ |
| 3 | 감정 벡터 + Bio Rhythm | kc_emotion.h/c | ⬜ |
| 4 | 욕구/목표 시스템 | kc_drive.h/c | ⬜ |
| 5 | NCkR 뉴런 연동 | kc_emotion.c | ⬜ |
| 6 | 능동 발화 엔진 | kc_active_ai.h/c | ⬜ |
| 7 | AI API 연동 | kc_active_ai.c | ⬜ |
| 8 | 통합 테스트 | kc_dee_test.c | ⬜ |

---

## §9 버전

v1.0.0 — 2026-04-16 초기 설계 완료

---

## §10 통합 설계 사양서 v1.1 반영사항

### §10-1 MBTI 성격 모델

```c
typedef enum {
    KC_MBTI_E = 0, KC_MBTI_I = 1,  /* 에너지 방향 */
    KC_MBTI_S = 2, KC_MBTI_N = 3,  /* 인식 방식   */
    KC_MBTI_T = 4, KC_MBTI_F = 5,  /* 판단 방식   */
    KC_MBTI_J = 6, KC_MBTI_P = 7,  /* 생활 양식   */
} KcMBTIAxis;

typedef struct {
    /* E/I: 외부 자극 Bio 소모/회복 속도 */
    float  energy_recharge_rate;  /* I=빠름, E=느림 */
    float  social_drain_rate;     /* E=낮음, I=높음 */

    /* T/F: appraisal 공식 내 가중치 */
    float  w_fairness;            /* F=높음, T=낮음 */
    float  w_social;              /* F=높음, T=낮음 */
    float  w_logic;               /* T=높음, F=낮음 */

    /* J/P: 반복 자극 내성 + 짜증 축적 */
    float  tolerance_decay;       /* J=느린 축적, P=빠른 축적 */
    float  irritation_k;          /* J=낮음, P=높음 */

    /* S/N: 호기심 기본값 + 새로움 반응 */
    float  curiosity_base;        /* N=높음, S=낮음 */
    float  novelty_dopamine;      /* N=높음, S=낮음 */

    char   type_str[5];           /* "INTJ", "ENFP" 등 */
} KcMBTIProfile;
```

```
MBTI별 호르몬 기본값 차이:
  INTJ: 세로토닌↑ 옥시토신↓ 코르티솔(사회)↑
  ENFP: 도파민↑ 옥시토신↑ 노르에피네프린↑
  ISTJ: 세로토닌↑ 코르티솔(불확실)↑ 아드레날린↓
  ESFJ: 옥시토신↑ 에스트로겐↑ 도파민(사회)↑
```

---

### §10-2 시냅스 변조 (Synaptic Modulation)

```
W' = W × (1 + Hormone_effect)

Hormone_effect 계산:
  = Σ(hormone_level[i] / h_max[i]) × syn_mod_coeff[i]
  = 도파민 비율 × 0.3
  + 세로토닌 비율 × 0.2
  + 코르티솔 비율 × (-0.2)   ← 스트레스 시 가중치 억제
  + 옥시토신 비율 × 0.15
  + 아드레날린 비율 × (-0.1) ← 패닉 시 판단력↓

적용 대상:
  NCkR 시냅스 가중치 (kc_syn_add 로 등록된 syn_weight[])
  → kc_dee_tick() 에서 매 틱 변조
  → vram flush 시 실제 반영
```

---

### §10-3 시각 신경망 모듈 (Vision Input)

```
카메라 → 로컬 분석 → 감정 수치만 DEE 입력

입력 변수:
  face_joy        0~1  (표정 기쁨)
  face_anger      0~1  (표정 분노)
  face_fear       0~1  (표정 공포)
  face_sadness    0~1  (표정 슬픔)
  gaze_attention  0~1  (시선 집중도)
  heart_rate_est  0~1  (안색 변화 기반 심박 추정)
  proximity       0~1  (거리 근접도 → 위협 계산)

처리 원칙:
  모든 영상 분석 = 로컬 (Mac Mini / GB10)
  DEE 입력 = 수치만 (영상 비전송)
  개인정보 미수집

연동:
  kc_dee_vision_input(dee, &vision_data)
  → stimulus 로 변환 후 kc_dee_stimulus() 호출
```

---

### §10-4 로컬 STT (Whisper) 연동

```
음성 → Whisper (로컬) → 텍스트 → DEE 감정 분석
                                → NCkR 개념 발화
                                → AI 응답 생성

감정 분석 추가:
  목소리 톤 → 위협/공포 계수
  말 속도   → 흥분/긴장 계수
  음량      → anger/joy 계수
```

---

### §10-5 거부 의사 표현 메커니즘

```
Ignore 발동 조건:
  Low_Relevance + Low_Affinity + High_Control > θ_ignore
  → 대화 차단 + 내부 처리 집중

Anger 발동 조건:
  irritation > θ_irr AND anger > θ_a
  → Aggressive Policy 채택
  → 경고 메시지 생성
  → 호르몬: 코르티솔↑↑ 아드레날린↑

Sadness 발동 조건:
  sadness > θ_s AND drive_survive < θ_d
  → 외부 출력 억제
  → Learning Loop 집중 (내부 오류 수정)
  → 호르몬: 세로토닌 재충전 대기
```

---

### §10-6 전체 파일 구조 (v1.1 기준)

```
kc_hormone.h/c      ✅ 호르몬 10종 + 감정 8종 + 욕구 6종
kc_mbti.h/c         ⬜ MBTI 성격 프로필 + 파라미터 주입
kc_vision.h/c       ⬜ 시각 입력 변환 (카메라 → 감정 수치)
kc_stt_bridge.h/c   ⬜ Whisper STT 연동 브릿지
kc_active_ai.h/c    ⬜ Claude/GPT/Gemini/Ollama API 연동
kc_syn_mod.h/c      ⬜ 시냅스 변조 W' = W×(1+H_effect)
kc_dee_test.c       ⬜ 통합 테스트 (시나리오 10종)
```

---

### §10-7 구현 우선순위

| 단계 | 파일 | 내용 | 상태 |
|------|------|------|------|
| 완료 | kc_hormone.h/c | 호르몬+감정+욕구+목표+능동발화 | ✅ |
| 2 | kc_mbti.h/c | MBTI 16종 프로필 + DEE 주입 | ⬜ |
| 3 | kc_syn_mod.h/c | 시냅스 변조 NCkR 연동 | ⬜ |
| 4 | kc_active_ai.h/c | API 모드 + 로컬 모드 | ⬜ |
| 5 | kc_vision.h/c | 카메라 입력 추상화 | ⬜ |
| 6 | kc_stt_bridge.h/c | Whisper 연동 | ⬜ |
| 7 | kc_dee_test.c | 전체 통합 테스트 | ⬜ |

---

v1.1 — 2026-04-16 MBTI/시각/STT/시냅스변조/거부메커니즘 추가
