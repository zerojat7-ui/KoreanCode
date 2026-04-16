/*
 * kc_vision.h — 시각 신경망 입력 브릿지
 * 카메라 분석 수치 → DEE 감정 입력
 * Kcode DEE v1.1.0
 */
#ifndef KC_VISION_H
#define KC_VISION_H
#include "kc_hormone.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 시각 입력 데이터 (로컬 처리 후 수치만) */
typedef struct {
    float face_joy;       /* 표정 기쁨      0~1 */
    float face_anger;     /* 표정 분노      0~1 */
    float face_fear;      /* 표정 공포      0~1 */
    float face_sadness;   /* 표정 슬픔      0~1 */
    float face_surprise;  /* 표정 놀람      0~1 */
    float face_disgust;   /* 표정 혐오      0~1 */
    float face_neutral;   /* 무표정         0~1 */
    float gaze_attention; /* 시선 집중도    0~1 */
    float proximity;      /* 근접도 (위협)  0~1 */
    float heart_rate_est; /* 심박 추정      0~1 */
    float voice_tension;  /* 음성 긴장도    0~1 */
    float voice_volume;   /* 음량 (분노/기쁨) 0~1 */
    int   face_detected;  /* 얼굴 감지 여부 */
} KcVisionInput;

/* 시각 입력 → DEE stimulus 변환 후 주입 */
void kc_dee_vision_input(KcEmotionEngine *dee,
                         const KcVisionInput *vis);

/* 테스트용 시각 데이터 생성 */
KcVisionInput kc_vision_make(float joy, float anger,
                              float fear, float attention);

#ifdef __cplusplus
}
#endif
#endif
