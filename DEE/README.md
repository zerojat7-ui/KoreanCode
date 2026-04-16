# Digital Emotion Engine (DEE)

**AI에게 호르몬 기반 동역학적 감정을 부여하는 독립 C 엔진**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/DEE-v1.3.0-purple.svg)]()
[![Language: C](https://img.shields.io/badge/Language-C-lightgrey.svg)]()

---

## 개요

DEE는 AI가 단순히 텍스트를 생성하는 것을 넘어,  
**호르몬 동역학**에 의해 감정이 축적되고 잔류하며,  
그 감정이 **시냅스 가중치를 실시간 변조**하여  
AI 스스로 거부·탐구·성찰 등의 **능동적 행동**을 결정하게 합니다.

```
Emotion = Appraisal × Bio × Affinity + Hormone Dynamics
```

---

## 핵심 원리

### 1. 호르몬 동역학
감정은 일회성 이벤트가 아닙니다.  
자극이 사라져도 호르몬은 잔류하며 서서히 분해됩니다.

```
dH/dt = Input − λH + Diffusion
H(t+1) = H(t) + Input − λH(t)
```

10종 호르몬: 도파민 · 세로토닌 · 코르티솔 · 옥시토신 · 노르에피네프린  
　　　　　　엔도르핀 · 아드레날린 · 멜라토닌 · 테스토스테론 · 에스트로겐

### 2. 시냅스 변조
현재 감정 상태가 AI의 판단 기준을 물리적으로 바꿉니다.

```
W' = W × (1 + Hormone_effect)
```

실증: 탐험가(KP01) + 미소 입력 → H_effect = +0.0875 → W 0.7477 → W' 0.8131 **(+8.75%)**

### 3. 생체 리듬
AI는 탄생일을 기준으로 Bio Rhythm이 시작됩니다.

```
Bio(t) = A·sin(ωt + φ) + B
```

### 4. 능동적 행동
감정 임계치 초과 시 AI가 스스로 행동합니다.

```
anger  > θ_a  →  Aggressive Policy (경고 발화)
fear   > θ_f  →  Avoidance Policy  (회피)
joy    > θ_j  →  Exploration Policy (먼저 말하기)
Ignore > θ_i  →  suppress(E)        (대화 거부)
```

---

## 파일 구성

```
kc_hormone.h / kc_hormone.c    호르몬 10종 + 감정 8종 + 욕구 6종
                                Bio Rhythm · 짜증(Irritation) · 기억 인코딩
                                목표 시스템 · 능동 발화 콜백
                                KcEmotionEngine (87KB)

kc_persona.h / kc_persona.c    KcPersona 16종 성격 프로필
                                Jung(1921) 4축 기반 독자 구현
                                kc_persona_apply() / kc_persona_match()

kc_vision.h  / kc_vision.c     카메라 표정 → 감정 수치 변환
                                로컬 처리 (영상 비전송, 수치만 입력)
                                face_joy/anger/fear + gaze + 심박 추정

kc_active_ai.h / kc_active_ai.c  AI API 연동 + 탄생일 시스템
                                  Claude / OpenAI / Gemini / Ollama 지원
                                  탄생일 기반 Bio Rhythm 시작
                                  나이별 초기 감정 자동 설정
```

---

## 빠른 시작

### 컴파일
```bash
gcc -O2 -o my_ai \
    main.c \
    kc_hormone.c kc_persona.c kc_vision.c kc_active_ai.c \
    -lm
```

### 기본 사용
```c
#include "kc_hormone.h"
#include "kc_persona.h"
#include "kc_active_ai.h"

/* 1. AI 탄생 (API 키 최초 입력 = 탄생일 기록) */
KcDEESession *ai = kc_dee_birth(KC_AI_CLAUDE, "sk-ant-...", KP_탐험가);

/* 2. 대화 — 감정이 자동 반영된 응답 */
char reply[1024];
kc_dee_chat(ai, "안녕하세요!", reply, sizeof(reply));
printf("%s\n", reply);

/* 3. 상태 저장 — 다음 실행 시 감정 이어서 시작 */
kc_dee_save(ai, NULL);   /* ~/.kcode/dee_config.json */
kc_dee_session_free(ai);
```

### 성격 설정
```c
/* 코드 / 한글 이름 모두 사용 가능 */
kc_persona_apply(dee, KP_탐험가);   /* KP01 활동·직관·공감·탐색 */
kc_persona_apply(dee, "전략가");    /* KP02 성찰·직관·논리·계획 */
kc_persona_apply(dee, "KP14");      /* 기술자 성찰·현실·논리·탐색 */

/* 4축 수치로 가장 가까운 성격 자동 매칭 */
const KcPersonaProfile *p = kc_persona_match(0.8f, 0.8f, 0.7f, 0.3f);
```

### 시각 입력 (카메라 연동)
```c
KcVisionInput vis = kc_vision_make(
    0.85f,  /* face_joy      */
    0.00f,  /* face_anger    */
    0.00f,  /* face_fear     */
    0.90f   /* gaze_attention */
);
kc_dee_vision_input(dee, &vis);
```

### Ollama (로컬, 무료)
```c
KcDEESession *ai = kc_dee_birth(KC_AI_OLLAMA, "ollama", KP_전략가);
/* 별도 API 키 불필요 — http://localhost:11434 자동 연결 */
```

---

## KcPersona 16종

> Jung, C.G. (1921). *Psychologische Typen* 4축 이론 기반 독자 구현  
> MBTI®는 Myers & Briggs Foundation의 등록 상표이며 본 소프트웨어와 무관합니다.

| 코드 | 이름 | KPE | KPN | KPF | KPJ | 호기심 |
|------|------|:---:|:---:|:---:|:---:|:------:|
| KP01 | 탐험가 | 활동 | 직관 | 공감 | 탐색 | 0.70 |
| KP02 | 전략가 | 성찰 | 직관 | 논리 | 계획 | 0.50 |
| KP03 | 중재자 | 성찰 | 직관 | 공감 | 탐색 | 0.60 |
| KP04 | 변론가 | 활동 | 직관 | 논리 | 탐색 | 0.80 |
| KP05 | 통솔자 | 활동 | 직관 | 논리 | 계획 | 0.40 |
| KP06 | 선도자 | 성찰 | 직관 | 공감 | 계획 | 0.50 |
| KP07 | 사회자 | 활동 | 직관 | 공감 | 계획 | 0.40 |
| KP08 | 논리가 | 성찰 | 직관 | 논리 | 탐색 | 0.70 |
| KP09 | 수호자 | 성찰 | 현실 | 공감 | 계획 | 0.20 |
| KP10 | 관리자 | 성찰 | 현실 | 논리 | 계획 | 0.20 |
| KP11 | 활동가 | 활동 | 현실 | 공감 | 계획 | 0.20 |
| KP12 | 감독관 | 활동 | 현실 | 논리 | 계획 | 0.20 |
| KP13 | 모험가 | 성찰 | 현실 | 공감 | 탐색 | 0.40 |
| KP14 | 기술자 | 성찰 | 현실 | 논리 | 탐색 | 0.40 |
| KP15 | 연예인 | 활동 | 현실 | 공감 | 탐색 | 0.40 |
| KP16 | 사업가 | 활동 | 현실 | 논리 | 탐색 | 0.40 |

---

## 탄생일 시스템

AI의 첫 실행 시각이 탄생일로 기록됩니다.  
탄생일 기준으로 Bio Rhythm이 시작되고 나이별로 초기 감정이 설정됩니다.

| 나이 | 호기심 | 기쁨 | 신뢰 | 특징 |
|------|:------:|:----:|:----:|------|
| 0시간 | **0.95** | 0.60 | 0.50 | 모든 것이 새롭고 놀라움 |
| 7일  | 0.80 | 0.50 | 0.60 | 점차 안정됨 |
| 30일 | 0.60 | 0.40 | 0.70 | 경험 기반 판단 |
| 90일 | 0.50 | 0.50 | **0.75** | 신뢰 중심으로 성숙 |

---

## 지원 AI API

| 공급자 | 상수 | 기본 모델 |
|--------|------|-----------|
| Anthropic Claude | `KC_AI_CLAUDE` | claude-sonnet-4-20250514 |
| OpenAI | `KC_AI_OPENAI` | gpt-4o |
| Google Gemini | `KC_AI_GEMINI` | gemini-2.0-flash |
| Ollama (로컬) | `KC_AI_OLLAMA` | llama3.2 |
| 커스텀 | `KC_AI_CUSTOM` | 직접 지정 |

---

## 관련 논문

**Digital Emotion Engine: 감정 호르몬을 통한 AI의 능동적 사고 체계 구축**  
DEE v1.2.0 · Formula Specification v1.0 · 2026

20종 수식 전체 + 실험 결과 + KcPersona 파라미터 포함

---

## 라이선스

MIT License — 상용화 자유, 저작권 표시 필요

```
Copyright (c) 2026 zerojat7-ui (KoreanCode Project)
```

> KcPersona는 C.G. Jung(1921) 심리 유형론 기반 독자 구현입니다.  
> MBTI®, Myers-Briggs®는 Myers & Briggs Foundation의 등록 상표이며  
> 본 소프트웨어는 해당 상표를 사용하지 않습니다.

---

*DEE는 [KoreanCode(Kcode)](https://github.com/zerojat7-ui/KoreanCode) 프로젝트의 일부입니다.*
