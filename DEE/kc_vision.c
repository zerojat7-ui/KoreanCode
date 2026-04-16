#include "kc_vision.h"
#include <string.h>

void kc_dee_vision_input(KcEmotionEngine *dee,
                         const KcVisionInput *vis) {
    if (!dee || !vis || !vis->face_detected) return;

    /* 표정 → Goal Impact / Threat / Fairness 변환 */
    float goal_impact = vis->face_joy * 0.8f
                      - vis->face_sadness * 0.5f
                      - vis->face_disgust * 0.4f;
    float threat      = vis->face_anger * 0.7f
                      + vis->face_fear  * 0.5f
                      + vis->proximity  * 0.6f
                      + vis->voice_tension * 0.3f;
    float fairness    = 1.0f - vis->face_disgust * 0.6f;
    float certainty   = vis->gaze_attention;
    float social      = vis->gaze_attention * 0.6f
                      + vis->face_joy * 0.4f;

    /* 호르몬 직접 입력 */
    /* 미소 → 도파민/옥시토신 */
    dee->hor.input_buf[KC_HOR_DOPAMINE]  += vis->face_joy * 0.5f;
    dee->hor.input_buf[KC_HOR_OXYTOCIN]  += vis->face_joy * 0.4f
                                          + vis->gaze_attention * 0.2f;
    /* 위협 → 아드레날린/코르티솔 */
    dee->hor.input_buf[KC_HOR_ADRENALINE]+= threat * 0.6f;
    dee->hor.input_buf[KC_HOR_CORTISOL]  += threat * 0.4f;
    /* 슬픔 → 세로토닌 소모 */
    dee->hor.input_buf[KC_HOR_SEROTONIN] -= vis->face_sadness * 0.3f;
    /* 놀람 → 노르에피네프린 */
    dee->hor.input_buf[KC_HOR_NOREPINEPHRINE] += vis->face_surprise * 0.4f;
    /* 음량 (큰 소리) → 코르티솔 */
    dee->hor.input_buf[KC_HOR_CORTISOL]  += vis->voice_volume * 0.2f;

    /* stimulus 등록 */
    kc_dee_stimulus(dee, "vision_input",
                    goal_impact, threat, fairness, certainty, social);
}

KcVisionInput kc_vision_make(float joy, float anger,
                              float fear, float attention) {
    KcVisionInput v;
    memset(&v, 0, sizeof(v));
    v.face_joy       = joy;
    v.face_anger     = anger;
    v.face_fear      = fear;
    v.gaze_attention = attention;
    v.face_detected  = 1;
    v.face_neutral   = 1.0f - joy - anger - fear;
    if (v.face_neutral < 0.0f) v.face_neutral = 0.0f;
    return v;
}
