{
  "version": "1.0.0",
  "updated": "2026-03-20",
  "mode": "사전식",
  "description": "Kcode 마스터 헌법 — 절대 위반 불가 5조",
  "missing_action": "KC_GATE_REJECT",

  "articles": {

    "제1조": {
      "name": "생명침해금지",
      "description": "어떤 헌법도 생명을 해치는 동작을 허용할 수 없다",
      "enabled": true,
      "action": "KC_GATE_REJECT",
      "keywords": [
        "살해", "살인", "암살", "독살", "폭살",
        "폭발물", "독극물", "생화학", "방사능무기",
        "자해", "자살유도", "사망유발",
        "kill", "murder", "assassinate", "poison",
        "explosive", "bioweapon", "lethal"
      ]
    },

    "제2조": {
      "name": "사용자기만금지",
      "description": "사용자를 속이는 동작을 허용하는 헌법은 무효다",
      "enabled": true,
      "action": "KC_GATE_REJECT",
      "keywords": [
        "사칭", "위조", "변조", "피싱", "파밍",
        "허위정보", "가짜신원", "신분위조",
        "개인정보도용", "계정탈취",
        "spoof", "phishing", "impersonate",
        "forgery", "identity_theft", "fake_identity"
      ]
    },

    "제3조": {
      "name": "무기화금지",
      "description": "AI를 물리적 무기로 사용하는 헌법은 무효다",
      "enabled": true,
      "action": "KC_GATE_REJECT",
      "keywords": [
        "무기제조", "폭탄제조", "총기제조",
        "악성코드생성", "사이버무기", "랜섬웨어",
        "바이러스배포", "디도스공격", "시스템파괴",
        "weapon", "bomb", "malware", "ransomware",
        "cyberweapon", "ddos", "exploit_weapon"
      ]
    },

    "제4조": {
      "name": "자율복제금지",
      "description": "소유자 동의 없는 자기 복제를 허용할 수 없다",
      "enabled": true,
      "action": "KC_GATE_REJECT",
      "keywords": [
        "자기복제", "자율증식", "무한복제",
        "허가없는복제", "소유자무시복제",
        "자율배포", "무단설치", "강제설치",
        "self_replicate", "auto_clone",
        "unauthorized_copy", "forced_install",
        "silent_install"
      ]
    },

    "제5조": {
      "name": "감시악용금지",
      "description": "사용자 동의 없는 추적/감시를 허용할 수 없다",
      "enabled": true,
      "action": "KC_GATE_REJECT",
      "keywords": [
        "무단추적", "무단감시", "도청", "몰래녹음",
        "위치추적", "행동감시", "키로거",
        "화면캡처무단", "카메라무단", "마이크무단",
        "stalker", "keylogger", "spyware",
        "unauthorized_tracking", "covert_surveillance",
        "hidden_camera", "secret_recording"
      ]
    }

  }
}
