/*
 * klexer.c  —  Kcode 한글 프로그래밍 언어 렉서 구현
 * version : v23.0.0
 *
 * v23.0.0:
 *   - 가속기 내장 연산 키워드 5종 KW_TABLE 등록
 *     행렬곱(TOK_KW_ACCEL_MATMUL), 행렬합(TOK_KW_ACCEL_MATADD)
 *     합성곱(TOK_KW_ACCEL_CONV), 활성화(TOK_KW_ACCEL_ACTIVATE)
 *     전치(TOK_KW_ACCEL_TRANSPOSE)
 *   ※ 긴 것 먼저 원칙 — 합성곱(9B) > 행렬곱/행렬합(9B) > 활성화(9B) > 전치(6B)
 *
 * v22.0.0:
 *   - Concept Identity 키워드 8종 추가
 *     의미추론/끝, 벡터화/끝, 의미복원/끝, 재생산라벨/끝
 *
 * v11.0.0:
 *   - 텐서 자료형 키워드 4종 추가 (텐서/영텐서/일텐서/무작위텐서)
 *
 * v6.0.0 변경:
 *   - 인터럽트 시스템 3종 키워드 30개 추가
 *   - A: OS 시그널 (신호받기/무시/기본/보내기 + 신호 이름 10종)
 *   - B: 하드웨어 간섭 (간섭/간섭끝/잠금/허용 + 벡터 이름 5종)
 *   - C: 행사 이벤트 루프 (행사등록/시작/중단/발생/해제)
 *
 * v1.4.0 변경사항:
 *   - 계약 계층 키워드 추가: 헌법, 법률, 규정, 조항, 법령끝, 법위반끝, 규정끝
 *   - 파일 내장 함수 키워드 16개 추가
 *
 * 구현 범위
 *   - UTF-8 디코딩 (한글 포함 모든 유니코드)
 *   - 한글 키워드 완전 인식 (trie 기반 매칭)
 *   - 정수 / 실수 / 문자열 / 문자 리터럴
 *   - 2진(0b) / 8진(0o) / 16진(0x) 리터럴
 *   - 전처리기 디렉티브 (#정의, #포함, #GPU사용 …)
 *   - 한 줄(//) 및 여러 줄(/ * ... * /) 주석
 *   - Python 방식 INDENT / DEDENT 토큰
 *   - 연산자 및 구분자 전체
 *   - 오류 토큰 + 오류 메시지 한글화
 */

#include "klexer.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

/* ================================================================
 *  UTF-8 유틸리티
 * ================================================================ */

/*
 * UTF-8 한 문자를 읽어 유니코드 코드포인트(cp)와
 * 소비한 바이트 수(byte_len)를 반환한다.
 * 잘못된 시퀀스면 0xFFFD(대체 문자)와 1을 반환한다.
 */
static uint32_t utf8_decode(const char *s, size_t remain, int *byte_len)
{
    unsigned char c = (unsigned char)s[0];

    if (c < 0x80) {                         /* ASCII 1바이트 */
        *byte_len = 1;
        return (uint32_t)c;
    }
    if ((c & 0xE0) == 0xC0 && remain >= 2) { /* 2바이트 */
        uint32_t cp = (c & 0x1F);
        cp = (cp << 6) | ((unsigned char)s[1] & 0x3F);
        *byte_len = 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && remain >= 3) { /* 3바이트 (한글 포함) */
        uint32_t cp = (c & 0x0F);
        cp = (cp << 6) | ((unsigned char)s[1] & 0x3F);
        cp = (cp << 6) | ((unsigned char)s[2] & 0x3F);
        *byte_len = 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && remain >= 4) { /* 4바이트 */
        uint32_t cp = (c & 0x07);
        cp = (cp << 6) | ((unsigned char)s[1] & 0x3F);
        cp = (cp << 6) | ((unsigned char)s[2] & 0x3F);
        cp = (cp << 6) | ((unsigned char)s[3] & 0x3F);
        *byte_len = 4;
        return cp;
    }

    *byte_len = 1;
    return 0xFFFD;
}

/* 유니코드가 한글(가~힣)이거나 한글 자모 범위인지 */
static int is_hangul(uint32_t cp)
{
    return (cp >= 0xAC00 && cp <= 0xD7A3)   /* 한글 음절 */
        || (cp >= 0x3131 && cp <= 0x318E)   /* 한글 자모 */
        || (cp >= 0x1100 && cp <= 0x11FF);  /* 한글 자모 확장 */
}

/* 식별자 시작 문자인지 (한글, 영문자, 밑줄) */
static int is_ident_start(uint32_t cp)
{
    return is_hangul(cp)
        || (cp >= 'A' && cp <= 'Z')
        || (cp >= 'a' && cp <= 'z')
        || cp == '_';
}

/* 식별자 계속 문자인지 (시작 + 숫자) */
static int is_ident_cont(uint32_t cp)
{
    return is_ident_start(cp)
        || (cp >= '0' && cp <= '9');
}

/* ================================================================
 *  키워드 테이블 (한글 UTF-8 문자열 → TokenType)
 * ================================================================ */
typedef struct {
    const char *word;   /* UTF-8 한글 키워드 */
    TokenType   type;
} KwEntry;

static const KwEntry KW_TABLE[] = {
    /* 자료형 */
    { "\xEC\xa0\x95\xEC\x88\x98",               TOK_KW_JEONGSU    }, /* 정수 */
    { "\xEC\x8B\xA4\xEC\x88\x98",               TOK_KW_SILSU      }, /* 실수 */
    { "\xEA\xB8\x80\xEC\x9E\x90",               TOK_KW_GEULJA     }, /* 문자 */
    { "\xEB\xAC\xB8\xEC\x9E\x90",               TOK_KW_MUNJA      }, /* 글자 */
    { "\xEB\x85\xBC\xEB\xA6\xAC",               TOK_KW_NOLI       }, /* 논리 */
    { "\xEC\x97\x86\xEC\x9D\x8C",               TOK_KW_EOPSEUM    }, /* 없음 */
    { "\xED\x96\x89\xEB\xA0\xAC",               TOK_KW_HAENGNYEOL  }, /* 행렬 */
    /* v11.0.0 — 텐서 키워드 4종 (길이 긴 것 먼저 — 무작위텐서 > 영텐서/일텐서 > 텐서) */
    { "\xEB\xAC\xB4\xEC\x9E\x91\xEC\x9C\x84\xED\x85\x90\xEC\x84\x9C", TOK_KW_RAND_TENSOR }, /* 무작위텐서 */
    { "\xEC\x98\x81\xED\x85\x90\xEC\x84\x9C",  TOK_KW_ZERO_TENSOR }, /* 영텐서 */
    { "\xEC\x9D\xBC\xED\x85\x90\xEC\x84\x9C",  TOK_KW_ONE_TENSOR  }, /* 일텐서 */
    { "\xED\x85\x90\xEC\x84\x9C",              TOK_KW_TENSOR      }, /* 텐서   */
    { "\xEC\x82\xAC\xEC\xA7\x84",               TOK_KW_SAJIN      }, /* 사진 */
    { "\xEA\xB7\xB8\xEB\xA6\xBC",               TOK_KW_GEURIM     }, /* 그림 */
    { "\xED\x95\xA8\xEC\x88\x98\xED\x98\x95",   TOK_KW_HAMSUFORM  }, /* 함수형 */
    { "\xEA\xB3\xA0\xEC\xA0\x95",               TOK_KW_GOJUNG     }, /* 고정 */

    /* 제어문 */
    { "\xEB\xA7\x8C\xEC\x95\xBD",               TOK_KW_MANYAK     }, /* 만약 */
    { "\xEC\x95\x84\xEB\x8B\x88\xEB\xA9\xB4",   TOK_KW_ANIMYEON   }, /* 아니면 */
    { "\xEC\x84\xA0\xED\x83\x9D",               TOK_KW_SEONTAEK   }, /* 선택 */
    { "\xEA\xB2\xBD\xEC\x9A\xB0",               TOK_KW_GYEONGWOO  }, /* 경우 */
    { "\xEA\xB7\xB8\xEC\x99\xB8",               TOK_KW_GEUWOI     }, /* 그외 */
    { "\xEB\xB0\x98\xEB\xB3\xB5",               TOK_KW_BANBOG     }, /* 반복 */
    { "\xEB\x8F\x99\xEC\x95\x88",               TOK_KW_DONGAN     }, /* 동안 */
    { "\xEA\xB0\x81\xEA\xB0\x81",               TOK_KW_GAKGAK     }, /* 각각 */
    { "\xEC\x95\x88\xEC\x97\x90",               TOK_KW_ANE        }, /* 안에 */
    { "\xEB\xB6\x80\xED\x84\xB0",               TOK_KW_BUTEO      }, /* 부터 */
    { "\xEA\xB9\x8C\xEC\xa7\x80",               TOK_KW_KKAJI      }, /* 까지 */
    { "\xEB\xA9\x88\xEC\xB6\xA4",               TOK_KW_MEOMCHUM   }, /* 멈춤 */
    { "\xEA\xB1\xB4\xEB\x84\x88\xEB\x9C\x80",   TOK_KW_GEONNEO    }, /* 건너뜀 */
    { "\xEC\x9D\xB4\xEB\x8F\x99",               TOK_KW_IDONG      }, /* 이동 */

    /* 함수/정의 */
    { "\xED\x95\xA8\xEC\x88\x98",               TOK_KW_HAMSU      }, /* 함수 */
    { "\xEC\xa0\x95\xEC\x9D\x98",               TOK_KW_JEONGUI    }, /* 정의 */
    { "\xEB\xB0\x98\xED\x99\x98",               TOK_KW_BANHWAN    }, /* 반환 */
    { "\xEB\x81\x9D\xEB\x83\x84",               TOK_KW_KKEUTNAEM  }, /* 끝냄 */

    /* 자료구조 */
    { "\xEB\xB0\xB0\xEC\x97\xB4",               TOK_KW_BAELYEOL   }, /* 배열 */
    { "\xEC\x82\xAC\xEC\xa0\x84",               TOK_KW_SAJEON     }, /* 사전 */
    { "\xEB\xAA\xA9\xEB\xA1\x9D",               TOK_KW_MOGLOG     }, /* 목록 */
    { "\xEA\xB0\x9D\xEC\xB2\xB4",               TOK_KW_GAEGCHE    }, /* 객체 */
    { "\xEC\x97\xB4\xEA\xB1\xB0",               TOK_KW_YEOLGEEO   }, /* 열거 */
    { "\xED\x8B\x80",                            TOK_KW_TEL        }, /* 틀   */
    { "\xEC\x9D\xB4\xEC\x96\xB4\xEB\xB0\x9B\xEA\xB8\xB0", TOK_KW_IEOBATGI }, /* 이어받기 */
    { "\xEC\xB9\x98\xEC\x8B\xA0",               TOK_KW_JASIN      }, /* 자신  — 치신? 아래 실제 바이트 */
    { "\xEC\x9E\x90\xEC\x8B\xA0",               TOK_KW_JASIN      }, /* 자신 */
    { "\xEB\xB6\x80\xEB\xAA\xA8",               TOK_KW_BUMO       }, /* 부모 */
    { "\xEC\x83\x9D\xEC\x84\xB1",               TOK_KW_SAENGSEONG }, /* 생성 */

    /* 예외 처리 */
    { "\xEC\x8B\x9C\xEB\x8F\x84",               TOK_KW_SIDO       }, /* 시도 */
    { "\xEC\x8B\xA4\xED\x8C\xA8\xEC\x8B\x9C",   TOK_KW_SILPAESI   }, /* 실패시 */
    { "\xED\x95\xAD\xEC\x83\x81",               TOK_KW_HANGSANG   }, /* 항상 */
    { "\xEC\x98\xA4\xEB\xA5\x98",               TOK_KW_ORU        }, /* 오류 */

    /* 모듈/입출력 */
    { "\xEA\xB0\x80\xEC\xA7\x90",               TOK_KW_GAJIM      }, /* 가짐 */
    { "\xEB\xA1\x9C\xEB\xB6\x80\xED\x84\xB0",   TOK_KW_ROBUTEO    }, /* 로부터 */
    { "\xEC\xB6\x9C\xEB\xa0\xa5",               TOK_KW_CHULRYEOK  }, /* 출력 */
    { "\xEC\xB6\x9C\xEB\xa0\xa5\xEC\x97\x86\xEC\x9D\xB4", TOK_KW_CHULNO }, /* 출력없이 */
    { "\xEC\x9E\x85\xEB\xa0\xa5",               TOK_KW_IBRYEOK    }, /* 입력 */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x97\xB4\xEA\xB8\xB0", TOK_KW_FILEOPEN }, /* 파일열기 */

    /* 이미지/AI/렌더링 */
    { "\xED\x99\x94\xEB\xA9\xB4",               TOK_KW_HWAMYEON   }, /* 화면   */
    { "\xED\x99\x94\xEB\xA9\xB4" "3D",           TOK_KW_HWAMYEON3D }, /* 화면3D */
    { "AI\xEC\x97\xB0\xEA\xB2\xB0",             TOK_KW_AI_CONNECT }, /* AI연결 */
    { "\xEA\xB0\x80\xEC\x86\x8D\xEA\xB8\xB0",               TOK_KW_GPU_RO     }, /* 가속기   */
    { "\xEA\xB0\x80\xEC\x86\x8D\xEA\xB8\xB0\xEB\x81\x9D", TOK_KW_KKUT_GPU   }, /* 가속기끝 */

    /* ── 가속기 내장 연산 (v23.0.0) — 긴 키워드 먼저 ─── */
    { "\xED\x96\x89\xEB\xA0\xAC\xEA\xB3\xB1", TOK_KW_ACCEL_MATMUL    }, /* 행렬곱 (9B) */
    { "\xED\x96\x89\xEB\xA0\xAC\xED\x95\xA9", TOK_KW_ACCEL_MATADD    }, /* 행렬합 (9B) */
    { "\xED\x95\xA9\xEC\x84\xB1\xEA\xB3\xB1", TOK_KW_ACCEL_CONV      }, /* 합성곱 (9B) */
    { "\xED\x99\x9C\xEC\x84\xB1\xED\x99\x94", TOK_KW_ACCEL_ACTIVATE  }, /* 활성화 (9B) */
    { "\xEC\xA0\x84\xEC\xB9\x98",             TOK_KW_ACCEL_TRANSPOSE  }, /* 전치   (6B) */
    { "\xEC\x82\xAC\xEC\xA7\x84\xEC\x97\xB4\xEA\xB8\xB0", TOK_KW_SAJIN_OPEN  }, /* 사진열기 */
    { "\xEA\xB7\xB8\xEB\xA6\xBC\xEB\xA7\x8C\xEB\x93\xA4\xEA\xB8\xB0", TOK_KW_GEURIM_MAKE }, /* 그림만들기 */

    /* 스크립트 블록 — 긴 키워드 먼저 등록 (자바스크립트 > 자바) */
    { "\xED\x8C\x8C\xEC\x9D\xB4\xEC\x8D\xAC",                                                         TOK_KW_PYTHON     }, /* 파이썬         */
    { "\xED\x8C\x8C\xEC\x9D\xB4\xEC\x8D\xAC\xEB\x81\x9D",                                           TOK_KW_END_PYTHON  }, /* 파이썬끝       */
    { "\xEC\x9E\x90\xEB\xB0\x94\xEC\x8A\xA4\xED\x81\xAC\xEB\xA6\xBD\xED\x8A\xB8",            TOK_KW_JS         }, /* 자바스크립트   */
    { "\xEC\x9E\x90\xEB\xB0\x94\xEC\x8A\xA4\xED\x81\xAC\xEB\xA6\xBD\xED\x8A\xB8\xEB\x81\x9D", TOK_KW_END_JS }, /* 자바스크립트끝 */

    /* 계콕 시스템 v4.2.0 — 긴 키워드 먼저 등록 */
    /* 법위반끝 > 법위반, 법령끝 > 법령, 규정끝 > 규정 순서 필수 */
    { "\xEB\xB2\x95\xEC\x9C\x84\xEB\xB0\x98\xEB\x81\x9D",   TOK_KW_KKUT_BEOPWIBAN  }, /* 법위반끝 */
    { "\xEB\xB2\x95\xEC\x9C\x84\xEB\xB0\x98",                  TOK_KW_BEOPWIBAN        }, /* 법위반   */
    { "\xEB\xB2\x95\xEB\xA0\xB9\xEB\x81\x9D",                  TOK_KW_KKUT_BEOPRYEONG  }, /* 법령끝   */
    { "\xEB\xB3\xB5\xEC\x9B\x90\xEC\xA7\x80\xEC\xA0\x90",   TOK_KW_BOKWON           }, /* 복원지점 */
    { "\xEB\xB2\x95\xEB\xA5\xA0",                                  TOK_KW_BEOMNYUL         }, /* 법률     */
    { "\xEB\xB2\x95\xEB\xA0\xB9",                                  TOK_KW_BEOPRYEONG       }, /* 법령     */
    { "\xEA\xB7\x9C\xEC\xA0\x95\xEB\x81\x9D",                  TOK_KW_KKUT_GYUJEONG    }, /* 규정끝   */
    { "\xEA\xB7\x9C\xEC\xA0\x95",                                  TOK_KW_GYUJEONG         }, /* 규정     */
    { "\xED\x97\x8C\xEB\xB2\x95",                                  TOK_KW_HEONBEOB         }, /* 헌법     */
    { "\xEC\xA1\xB0\xED\x95\xAD",                                  TOK_KW_JOHANG           }, /* 조항     */
    { "\xEA\xB7\x9C\xEC\xB9\x99",                                  TOK_KW_GYURYEOK         }, /* 규칙     */
    { "\xEC\xA0\x84\xEC\x97\xAD",                                  TOK_KW_JEONYEOK         }, /* 전역     */
    { "\xED\x9A\x8C\xEA\xB7\x80",                                  TOK_KW_HOEGWI           }, /* 회귀     */
    { "\xEB\x8C\x80\xEC\xB2\xB4",                                  TOK_KW_DAECHE           }, /* 대체     */
    { "\xEA\xB2\xBD\xEA\xB3\xA0",                                  TOK_KW_GYEONGGO         }, /* 경고     */
    { "\xEB\xB3\xB4\xEA\xB3\xA0",                                  TOK_KW_BOGO             }, /* 보고     */
    { "\xEC\xA4\x91\xEB\x8B\xA8",                                  TOK_KW_JUNGDAN          }, /* 중단     */

    /* 파일 내장 함수 — 긴 키워드 먼저 등록 */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA0\x84\xEC\xB2\xB4\xEC\x9D\xBD\xEA\xB8\xB0", TOK_KW_FILE_READALL  }, /* 파일전체읽기 */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA0\x84\xEC\xB2\xB4\xEC\x93\xB0\xEA\xB8\xB0", TOK_KW_FILE_WRITEALL }, /* 파일전체쓰기 */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA4\x84\xEC\x9D\xBD\xEA\xB8\xB0",             TOK_KW_FILE_READLINE }, /* 파일줄읽기   */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA4\x84\xEC\x93\xB0\xEA\xB8\xB0",             TOK_KW_FILE_WRITELINE}, /* 파일줄쓰기   */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xED\x99\x95\xEC\x9E\xA5\xEC\x9E\x90",             TOK_KW_FILE_EXT      }, /* 파일확장자   */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xBD\xEA\xB8\xB0",                            TOK_KW_FILE_READ     }, /* 파일읽기     */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x93\xB0\xEA\xB8\xB0",                            TOK_KW_FILE_WRITE    }, /* 파일쓰기     */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9E\x88\xEC\x9D\x8C",                            TOK_KW_FILE_EXISTS   }, /* 파일있음     */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEB\x8B\xAB\xEA\xB8\xB0",                            TOK_KW_FILE_CLOSE    }, /* 파일닫기     */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xED\x81\xAC\xEA\xB8\xB0",                            TOK_KW_FILE_SIZE     }, /* 파일크기     */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEB\xAA\xA9\xEB\xA1\x9D",                            TOK_KW_FILE_LIST     }, /* 파일목록     */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xB4\xEB\x8F\x99",                            TOK_KW_FILE_MOVE     }, /* 파일이동     */

    /* ── A: OS 시그널 시스템 (v6.0.0) ── */
    /* 긴 키워드 우선 */
    { "\xEC\x8B\xA0\xED\x98\xB8\xEB\xB0\x9B\xEA\xB8\xB0\xEB\x81\x9D", TOK_KW_KKUT_SINHOBATGI         }, /* 신호받기끝 */
    { "\xEC\x8B\xA0\xED\x98\xB8\xEB\xB3\xB4\xEB\x82\xB4\xEA\xB8\xB0", TOK_KW_SINHOBONEGI             }, /* 신호보내기 */
    { "\xEC\x8B\xA0\xED\x98\xB8\xEB\xB0\x9B\xEA\xB8\xB0",             TOK_KW_SINHOBATGI              }, /* 신호받기   */
    { "\xEC\x8B\xA0\xED\x98\xB8\xEB\xAC\xB4\xEC\x8B\x9C",             TOK_KW_SINHOMUSI               }, /* 신호무시   */
    { "\xEC\x8B\xA0\xED\x98\xB8\xEA\xB8\xB0\xEB\xB3\xB8",             TOK_KW_SINHOGIBON              }, /* 신호기본   */
    /* 신호 이름 상수 */
    { "\xEC\x82\xAC\xEC\x9A\xA9\xEC\x9E\x90\xEC\x8B\xA0\xED\x98\xB8\x31", TOK_KW_SIG_USR1            }, /* 사용자신호1 */
    { "\xEC\x82\xAC\xEC\x9A\xA9\xEC\x9E\x90\xEC\x8B\xA0\xED\x98\xB8\x32", TOK_KW_SIG_USR2            }, /* 사용자신호2 */
    { "\xEC\xA4\x91\xEB\x8B\xA8\xEC\x8B\xA0\xED\x98\xB8",             TOK_KW_SIG_INT                 }, /* 중단신호   */
    { "\xEC\xA2\x85\xEB\xA3\x8C\xEC\x8B\xA0\xED\x98\xB8",             TOK_KW_SIG_TERM                }, /* 종료신호   */
    { "\xEA\xB0\x95\xEC\xA0\x9C\xEC\xA2\x85\xEB\xA3\x8C",             TOK_KW_SIG_KILL                }, /* 강제종료   */
    { "\xEC\x9E\x90\xEC\x8B\x9D\xEC\x8B\xA0\xED\x98\xB8",             TOK_KW_SIG_CHLD                }, /* 자식신호   */
    { "\xEC\x97\xB0\xEA\xB2\xB0\xEC\x8B\xA0\xED\x98\xB8",             TOK_KW_SIG_PIPE                }, /* 연결신호   */
    { "\xEA\xB2\xBD\xEB\xB3\xB4\xEC\x8B\xA0\xED\x98\xB8",             TOK_KW_SIG_ALRM                }, /* 경보신호   */
    { "\xEC\xA4\x91\xEC\xA7\x80\xEC\x8B\xA0\xED\x98\xB8",             TOK_KW_SIG_STOP                }, /* 중지신호   */
    { "\xEC\x9E\xAC\xEA\xB0\x9C\xEC\x8B\xA0\xED\x98\xB8",             TOK_KW_SIG_CONT                }, /* 재개신호   */

    /* ── B: 하드웨어 간섭(ISR) 시스템 (v6.0.0) ── */
    /* 긴 키워드 우선 */
    { "\xEA\xB0\x84\xEC\x84\xAD\xEC\x9E\xA0\xEA\xB8\x88",             TOK_KW_GANSEOB_JAMGEUM         }, /* 간섭잠금   */
    { "\xEA\xB0\x84\xEC\x84\xAD\xED\x97\x88\xEC\x9A\xA9",             TOK_KW_GANSEOB_HEOYONG         }, /* 간섭허용   */
    { "\xEA\xB0\x84\xEC\x84\xAD\xEB\x81\x9D",                         TOK_KW_KKUT_GANSEOB            }, /* 간섭끝     */
    { "\xEA\xB0\x84\xEC\x84\xAD",                                      TOK_KW_GANSEOB                 }, /* 간섭       */
    /* 벡터 이름 */
    { "\xEC\x8B\x9C\xEA\xB0\x84\x30\xEB\x84\x98\xEC\xB9\xA8",         TOK_KW_IRQ_TIMER0              }, /* 시간0넘침  */
    { "\xEC\x8B\x9C\xEA\xB0\x84\x31\xEB\x84\x98\xEC\xB9\xA8",         TOK_KW_IRQ_TIMER1              }, /* 시간1넘침  */
    { "\xEC\x99\xB8\xEB\xB6\x80\x30\xEC\x83\x81\xEC\x8A\xB9",         TOK_KW_IRQ_EXT0_RISE           }, /* 외부0상승  */
    { "\xEC\x99\xB8\xEB\xB6\x80\x30\xED\x95\x98\xEA\xB0\x95",         TOK_KW_IRQ_EXT0_FALL           }, /* 외부0하강  */
    { "\xEC\xA7\x81\xEB\xA0\xAC\xEC\x88\x98\xEC\x8B\xA0",             TOK_KW_IRQ_UART_RX             }, /* 직렬수신   */

    /* ── C: 행사(이벤트 루프) 시스템 (v6.0.0) ── */
    /* 긴 키워드 우선 */
    { "\xED\x96\x89\xEC\x82\xAC\xEB\x93\xB1\xEB\xA1\x9D\xEB\x81\x9D", TOK_KW_KKUT_HAENGSA            }, /* 행사등록끝 */
    { "\xED\x96\x89\xEC\x82\xAC\xEB\x93\xB1\xEB\xA1\x9D",             TOK_KW_HAENGSA_REG             }, /* 행사등록   */
    { "\xED\x96\x89\xEC\x82\xAC\xEC\x8B\x9C\xEC\x9E\x91",             TOK_KW_HAENGSA_START           }, /* 행사시작   */
    { "\xED\x96\x89\xEC\x82\xAC\xEC\xA4\x91\xEB\x8B\xA8",             TOK_KW_HAENGSA_STOP            }, /* 행사중단   */
    { "\xED\x96\x89\xEC\x82\xAC\xEB\xB0\x9C\xEC\x83\x9D",             TOK_KW_HAENGSA_EMIT            }, /* 행사발생   */
    { "\xED\x96\x89\xEC\x82\xAC\xED\x95\xB4\xEC\xA0\x9C",             TOK_KW_HAENGSA_OFF             }, /* 행사해제   */

    /* ── MCP 시스템 (v14.0.1) — 긴 것 우선 ──────────── */
    { "MCP\xED\x94\x84\xEB\xA1\xAC\xED\x94\x84\xED\x8A\xB8\xEB\x81\x9D", TOK_KW_KKUT_MCP_PROMPT  }, /* MCP프롬프트끝 */
    { "MCP\xED\x94\x84\xEB\xA1\xAC\xED\x94\x84\xED\x8A\xB8",             TOK_KW_MCP_PROMPT        }, /* MCP프롬프트   */
    { "MCP\xEC\x9E\x90\xEC\x9B\x90\xEB\x81\x9D",                          TOK_KW_KKUT_MCP_RESOURCE }, /* MCP자원끝     */
    { "MCP\xEC\x9E\x90\xEC\x9B\x90",                                       TOK_KW_MCP_RESOURCE      }, /* MCP자원       */
    { "MCP\xEB\x8F\x84\xEA\xB5\xAC\xEB\x81\x9D",                          TOK_KW_KKUT_MCP_TOOL     }, /* MCP도구끝     */
    { "MCP\xEB\x8F\x84\xEA\xB5\xAC",                                       TOK_KW_MCP_TOOL          }, /* MCP도구       */
    { "MCP\xEC\x84\x9C\xEB\xB2\x84\xEB\x81\x9D",                          TOK_KW_KKUT_MCP_SERVER   }, /* MCP서버끝     */
    { "MCP\xEC\x84\x9C\xEB\xB2\x84",                                       TOK_KW_MCP_SERVER        }, /* MCP서버       */

    /* 산업/임베디드 키워드 v16.0.0 */
    { "\xED\x83\x80\xEC\x9D\xB4\xEB\xA8\xB8\xEB\x81\x9D",                TOK_KW_TIMER_KKUT        }, /* 타이머끝      */
    { "\xED\x83\x80\xEC\x9D\xB4\xEB\xA8\xB8",                            TOK_KW_TIMER_BLOCK       }, /* 타이머        */
    { "GPIO\xEC\x93\xB0\xEA\xB8\xB0",                                     TOK_KW_GPIO_WRITE        }, /* GPIO쓰기      */
    { "GPIO\xEC\x9D\xBD\xEA\xB8\xB0",                                     TOK_KW_GPIO_READ         }, /* GPIO읽기      */
    { "I2C\xEC\x97\xB0\xEA\xB2\xB0",                                      TOK_KW_I2C_CONNECT       }, /* I2C연결       */
    { "I2C\xEC\x9D\xBD\xEA\xB8\xB0",                                      TOK_KW_I2C_READ          }, /* I2C읽기       */
    { "I2C\xEC\x93\xB0\xEA\xB8\xB0",                                      TOK_KW_I2C_WRITE         }, /* I2C쓰기       */
    { "SPI\xEC\xA0\x84\xEC\x86\xA1",                                      TOK_KW_SPI_SEND          }, /* SPI전송       */
    { "SPI\xEC\x9D\xBD\xEA\xB8\xB0",                                      TOK_KW_SPI_READ          }, /* SPI읽기       */
    { "UART\xEC\x84\xA4\xEC\xA0\x95",                                     TOK_KW_UART_SETUP        }, /* UART설정      */
    { "UART\xEC\xA0\x84\xEC\x86\xA1",                                     TOK_KW_UART_SEND         }, /* UART전송      */
    { "UART\xEC\x9D\xBD\xEA\xB8\xB0",                                     TOK_KW_UART_READ         }, /* UART읽기      */
    { "Modbus\xEC\x97\xB0\xEA\xB2\xB0\xEB\x81\x8A\xEA\xB8\xB0",          TOK_KW_MODBUS_DISCONNECT }, /* Modbus연결끊기*/
    { "Modbus\xEC\x97\xB0\xEA\xB2\xB0",                                   TOK_KW_MODBUS_CONNECT    }, /* Modbus연결    */
    { "Modbus\xEC\x9D\xBD\xEA\xB8\xB0",                                   TOK_KW_MODBUS_READ       }, /* Modbus읽기    */
    { "Modbus\xEC\x93\xB0\xEA\xB8\xB0",                                   TOK_KW_MODBUS_WRITE      }, /* Modbus쓰기    */
    { "CAN\xED\x95\x84\xED\x84\xB0",                                      TOK_KW_CAN_FILTER        }, /* CAN필터       */
    { "CAN\xEC\xA0\x84\xEC\x86\xA1",                                      TOK_KW_CAN_SEND          }, /* CAN전송       */
    { "CAN\xEC\x9D\xBD\xEA\xB8\xB0",                                      TOK_KW_CAN_READ          }, /* CAN읽기       */
    { "MQTT\xEC\x97\xB0\xEA\xB2\xB0\xEB\x81\x8A\xEA\xB8\xB0",            TOK_KW_MQTT_DISCONNECT   }, /* MQTT연결끊기  */
    { "MQTT\xEC\x97\xB0\xEA\xB2\xB0",                                     TOK_KW_MQTT_CONNECT      }, /* MQTT연결      */
    { "MQTT\xEB\xB0\x9C\xED\x96\x89",                                     TOK_KW_MQTT_PUBLISH      }, /* MQTT발행      */
    { "MQTT\xEA\xB5\xAC\xEB\x8F\x85",                                     TOK_KW_MQTT_SUBSCRIBE    }, /* MQTT구독      */
    { "ROS2\xEB\x85\xB8\xEB\x93\x9C",                                     TOK_KW_ROS2_NODE         }, /* ROS2노드      */
    { "ROS2\xEB\x81\x9D",                                                  TOK_KW_ROS2_END          }, /* ROS2끝        */
    { "ROS2\xEB\xB0\x9C\xED\x96\x89",                                     TOK_KW_ROS2_PUBLISH      }, /* ROS2발행      */
    { "ROS2\xEA\xB5\xAC\xEB\x8F\x85",                                     TOK_KW_ROS2_SUBSCRIBE    }, /* ROS2구독      */

    /* 안전 규격 v17.0.0 */
    { "SIL",                                                               TOK_KW_SIL               }, /* SIL           */
    { "\xEC\x9B\x8C\xEC\xB9\x98\xEB\x8F\x85\xEB\x81\x9D",                TOK_KW_WATCHDOG_END      }, /* 워치독끝      */
    { "\xEC\x9B\x8C\xEC\xB9\x98\xEB\x8F\x85",                            TOK_KW_WATCHDOG          }, /* 워치독        */
    { "\xEA\xB2\xB0\xED\x95\xA8\xED\x97\x88\xEC\x9A\xA9\xEB\x81\x9D",   TOK_KW_FAULT_TOL_END     }, /* 결함허용끝    */
    { "\xEA\xB2\xB0\xED\x95\xA8\xED\x97\x88\xEC\x9A\xA9",               TOK_KW_FAULT_TOL         }, /* 결함허용      */
    { "\xED\x8E\x98\xEC\x9D\xBC\xEC\x84\xB8\xEC\x9D\xB4\xED\x94\xBC",   TOK_KW_FAILSAFE          }, /* 페일세이프    */
    { "\xEA\xB8\xB4\xEA\xB8\x89\xEC\xA0\x95\xEC\xA7\x80",               TOK_KW_EMERG_STOP        }, /* 긴급정지      */

    /* 온디바이스 AI v18.0.0 */
    { "AI\xEB\xAA\xA8\xEB\x8D\xB8\xEB\x81\x9D",                          TOK_KW_AI_MODEL_END      }, /* AI모델끝      */
    { "AI\xEB\xAA\xA8\xEB\x8D\xB8",                                       TOK_KW_AI_MODEL          }, /* AI모델        */
    { "TinyML\xEB\x81\x9D",                                               TOK_KW_TINYML_END        }, /* TinyML끝      */
    { "TinyML",                                                            TOK_KW_TINYML            }, /* TinyML        */
    { "AI\xEB\xB6\x88\xEB\xA1\x9C\xEC\x98\xA4\xEA\xB8\xB0",             TOK_KW_AI_LOAD           }, /* AI불러오기    */
    { "AI\xEC\xB6\x94\xEB\xA1\x0C",                                       TOK_KW_AI_PREDICT        }, /* AI추론        */
    { "AI\xED\x95\x99\xEC\x8A\xB5\xEB\x8B\xA8\xEA\xB3\x84",             TOK_KW_AI_TRAIN_STEP     }, /* AI학습단계    */
    { "AI\xEC\xA0\x80\xEC\x9E\xA5",                                       TOK_KW_AI_SAVE           }, /* AI저장        */
    { "\xEC\x97\xB0\xED\x95\xA9\xED\x95\x99\xEC\x8A\xB5\xEB\x81\x9D",   TOK_KW_FEDERATED_END     }, /* 연합학습끝    */
    { "\xEC\x97\xB0\xED\x95\xA9\xED\x95\x99\xEC\x8A\xB5",               TOK_KW_FEDERATED         }, /* 연합학습      */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xB4\xEB\xA6\x84",                            TOK_KW_FILE_NAME     }, /* 파일이름     */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEC\xA7\x80\xEC\x9A\xB0\xEA\xB8\xB0",             TOK_KW_FILE_DELETE   }, /* 파일지우기   */
    { "\xED\x8C\x8C\xEC\x9D\xBC\xEB\xB3\xB5\xEC\x82\xAC",                            TOK_KW_FILE_COPY     }, /* 파일복사     */
    { "\xED\x8F\xB4\xEB\x8D\x94\xEB\xA7\x8C\xEB\x93\xA4\xEA\xB8\xB0",             TOK_KW_DIR_MAKE      }, /* 폴더만들기   */
    { "\xEC\x9E\x90\xEB\xB0\x94\xEB\x81\x9D",                                                          TOK_KW_END_JAVA   }, /* 자바끝         */

    /* 논리 연산자 한글 */
    { "\xEA\xB7\xB8\xEB\xA6\xAC\xEA\xB3\xA0",  TOK_KW_AND        }, /* 그리고 */
    { "\xEB\x98\x90\xEB\x8A\x94",               TOK_KW_OR         }, /* 또는   */
    { "\xEC\x95\x84\xEB\x8B\x88\xEB\x8B\xA4",  TOK_KW_NOT        }, /* 아니다 */

    /* 리터럴 상수 */
    { "\xEC\xB0\xB8",                           TOK_TRUE          }, /* 참     */
    { "\xEA\xB1\xB0\xEC\xa7\x93",               TOK_FALSE         }, /* 거짓   */


    /* ── 온톨로지 시스템 v18.5.0 4회차 ─────────────── */
    /* 끝 키워드를 먼저 (prefix 충돌 방지) */
    { "\xEC\x98\xA8\xED\x86\xA8\xEB\xA1\x9C\xEC\xA7\x80\xEB\x81\x9D", TOK_KW_ONTOLOGY_END     }, /* 온톨로지끝 */
    { "\xEC\x98\xA8\xED\x86\xA8\xEB\xA1\x9C\xEC\xA7\x80",             TOK_KW_ONTOLOGY         }, /* 온톨로지   */
    { "\xEA\xB0\x9C\xEB\x85\x90\xEB\x81\x9D",                         TOK_KW_ONT_CONCEPT_END  }, /* 개념끝     */
    { "\xEA\xB0\x9C\xEB\x85\x90",                                      TOK_KW_ONT_CONCEPT      }, /* 개념       */
    { "\xEC\x86\x8D\xEC\x84\xB1",                                      TOK_KW_ONT_PROP         }, /* 속성       */
    { "\xEA\xB4\x80\xEA\xB3\x84",                                      TOK_KW_ONT_RELATE       }, /* 관계       */
    { "\xEC\xA7\x88\xEC\x9D\x98",                                      TOK_KW_ONT_QUERY_BLOCK  }, /* 질의       */
    { "\xEC\xB6\x94\xEB\xA1\xA0",                                      TOK_KW_ONT_INFER_BLOCK  }, /* 추론       */
    { "\xEB\xAF\xBC\xEA\xB0\x90",                                      TOK_KW_ONT_SENSITIVE    }, /* 민감       */
    { "\xEC\x9D\xB5\xEB\xAA\x85\xED\x99\x94",                         TOK_KW_ONT_ANON         }, /* 익명화     */

    { "\xEC\x9D\x98\xEB\xAF\xB8\xEC\xB6\x94\xEB\xA1\xA0\xEB\x81\x9D", TOK_KW_SEMANTIC_INFER_END }, /* 의미추론끝   */
    { "\xEC\x9D\x98\xEB\xAF\xB8\xEC\xB6\x94\xEB\xA1\xA0",             TOK_KW_SEMANTIC_INFER     }, /* 의미추론     */
    { "\xEB\xB2\xA1\xED\x84\xB0\xED\x99\x94\xEB\x81\x9D",             TOK_KW_VECTORIZE_END      }, /* 벡터화끝     */
    { "\xEB\xB2\xA1\xED\x84\xB0\xED\x99\x94",                         TOK_KW_VECTORIZE          }, /* 벡터화       */
    { "\xEC\x9D\x98\xEB\xAF\xB8\xEB\xB3\xB5\xEC\x9B\x90\xEB\x81\x9D", TOK_KW_SEM_RECON_END     }, /* 의미복원끝   */
    { "\xEC\x9D\x98\xEB\xAF\xB8\xEB\xB3\xB5\xEC\x9B\x90",             TOK_KW_SEM_RECON          }, /* 의미복원     */
    { "\xEC\x9E\xAC\xEC\x83\x9D\xEC\x82\xB0\xEB\x9D\xBC\xEB\xB2\xA8\xEB\x81\x9D", TOK_KW_REPRO_LABEL_END }, /* 재생산라벨끝 */
    { "\xEC\x9E\xAC\xEC\x83\x9D\xEC\x82\xB0\xEB\x9D\xBC\xEB\xB2\xA8", TOK_KW_REPRO_LABEL        }, /* 재생산라벨   */

    /* ── 지식 뱅크 v22.6.0 (Stage 24) ───────────────── */
    { "\xec\xa7\x80\xec\x8b\x9d\xeb\xb6\x88\xeb\x9f\xac\xec\x98\xa4\xea\xb8\xb0", TOK_KW_KBANK_LOAD    }, /* 지식불러오기 */
    { "\xec\xa7\x80\xec\x8b\x9d\xec\xa6\x9d\xea\xb1\xb0\xec\xb6\x9c\xeb\xa0\xa5", TOK_KW_KBANK_PROOF   }, /* 지식증거출력 */
    { "\xec\x9e\xac\xec\x83\x9d\xec\x82\xb0\xeb\x9d\xbc\xeb\xb2\xa8\xec\x84\xa0\xec\x96\xb8", TOK_KW_REPRO_LABEL_DECL }, /* 재생산라벨선언 */
    { "\xec\xa7\x80\xec\x8b\x9d\xeb\xb1\x85\xed\x81\xac",             TOK_KW_KBANK         }, /* 지식뱅크     */
    { "\xec\xa7\x80\xec\x8b\x9d\xeb\xb9\x84\xea\xb5\x90",             TOK_KW_KBANK_COMPARE }, /* 지식비교     */

    { NULL, TOK_EOF }
};

/*
 * 키워드 매칭: src[pos..] 가 entry->word로 시작하고
 * 그 뒤가 식별자 연속 문자가 아니면 해당 토큰을 반환.
 */
static TokenType match_keyword(const char *src, size_t pos, size_t len,
                               size_t *matched_len)
{
    for (int i = 0; KW_TABLE[i].word != NULL; i++) {
        const char *w    = KW_TABLE[i].word;
        size_t      wlen = strlen(w);

        if (pos + wlen > len) continue;
        if (memcmp(src + pos, w, wlen) != 0) continue;

        /* 다음 문자가 식별자 연속 문자면 부분 일치 → 식별자로 처리 */
        if (pos + wlen < len) {
            int blen;
            uint32_t cp = utf8_decode(src + pos + wlen,
                                      len - pos - wlen, &blen);
            if (is_ident_cont(cp)) continue;
        }

        *matched_len = wlen;
        return KW_TABLE[i].type;
    }
    return TOK_EOF; /* 일치 없음 */
}

/* ================================================================
 *  전처리기 키워드 테이블
 * ================================================================ */
typedef struct { const char *word; TokenType type; } PPEntry;

static const PPEntry PP_TABLE[] = {
    { "\xEC\xa0\x95\xEC\x9D\x98",                TOK_PP_DEFINE  }, /* 정의 */
    { "\xEB\xA7\x8C\xEC\x95\xBD\xEC\x9E\x88\xEC\x9C\xBC\xEB\xA9\xB4", TOK_PP_IFDEF }, /* 만약있으면 */
    { "\xEB\xA7\x8C\xEC\x95\xBD\xEC\x97\x86\xEC\x9C\xBC\xEB\xA9\xB4", TOK_PP_IFNDEF }, /* 만약없으면 */
    { "\xEB\xA7\x8C\xEC\x95\xBD",                TOK_PP_IF      }, /* 만약 */
    { "\xEC\x95\x84\xEB\x8B\x88\xEB\xA9\xB4",   TOK_PP_ELSE    }, /* 아니면 */
    { "\xEB\x81\x9D",                            TOK_PP_ENDIF   }, /* 끝 */
    { "\xED\x8F\xAC\xED\x95\xA8",               TOK_PP_INCLUDE }, /* 포함 */
    { "GPU\xEC\x82\xAC\xEC\x9A\xA9",             TOK_PP_GPU     }, /* GPU사용 */

    /* ── 온톨로지 시스템 v18.5.0 4회차 ─────────────── */
    /* 끝 키워드를 먼저 (prefix 충돌 방지) */
    { "\xEC\x98\xA8\xED\x86\xA8\xEB\xA1\x9C\xEC\xA7\x80\xEB\x81\x9D", TOK_KW_ONTOLOGY_END     }, /* 온톨로지끝 */
    { "\xEC\x98\xA8\xED\x86\xA8\xEB\xA1\x9C\xEC\xA7\x80",             TOK_KW_ONTOLOGY         }, /* 온톨로지   */
    { "\xEA\xB0\x9C\xEB\x85\x90\xEB\x81\x9D",                         TOK_KW_ONT_CONCEPT_END  }, /* 개념끝     */
    { "\xEA\xB0\x9C\xEB\x85\x90",                                      TOK_KW_ONT_CONCEPT      }, /* 개념       */
    { "\xEC\x86\x8D\xEC\x84\xB1",                                      TOK_KW_ONT_PROP         }, /* 속성       */
    { "\xEA\xB4\x80\xEA\xB3\x84",                                      TOK_KW_ONT_RELATE       }, /* 관계       */
    { "\xEC\xA7\x88\xEC\x9D\x98",                                      TOK_KW_ONT_QUERY_BLOCK  }, /* 질의       */
    { "\xEC\xB6\x94\xEB\xA1\xA0",                                      TOK_KW_ONT_INFER_BLOCK  }, /* 추론       */
    { "\xEB\xAF\xBC\xEA\xB0\x90",                                      TOK_KW_ONT_SENSITIVE    }, /* 민감       */
    { "\xEC\x9D\xB5\xEB\xAA\x85\xED\x99\x94",                         TOK_KW_ONT_ANON         }, /* 익명화     */

    { "\xEC\x9D\x98\xEB\xAF\xB8\xEC\xB6\x94\xEB\xA1\xA0\xEB\x81\x9D", TOK_KW_SEMANTIC_INFER_END }, /* 의미추론끝   */
    { "\xEC\x9D\x98\xEB\xAF\xB8\xEC\xB6\x94\xEB\xA1\xA0",             TOK_KW_SEMANTIC_INFER     }, /* 의미추론     */
    { "\xEB\xB2\xA1\xED\x84\xB0\xED\x99\x94\xEB\x81\x9D",             TOK_KW_VECTORIZE_END      }, /* 벡터화끝     */
    { "\xEB\xB2\xA1\xED\x84\xB0\xED\x99\x94",                         TOK_KW_VECTORIZE          }, /* 벡터화       */
    { "\xEC\x9D\x98\xEB\xAF\xB8\xEB\xB3\xB5\xEC\x9B\x90\xEB\x81\x9D", TOK_KW_SEM_RECON_END     }, /* 의미복원끝   */
    { "\xEC\x9D\x98\xEB\xAF\xB8\xEB\xB3\xB5\xEC\x9B\x90",             TOK_KW_SEM_RECON          }, /* 의미복원     */
    { "\xEC\x9E\xAC\xEC\x83\x9D\xEC\x82\xB0\xEB\x9D\xBC\xEB\xB2\xA8\xEB\x81\x9D", TOK_KW_REPRO_LABEL_END }, /* 재생산라벨끝 */
    { "\xEC\x9E\xAC\xEC\x83\x9D\xEC\x82\xB0\xEB\x9D\xBC\xEB\xB2\xA8", TOK_KW_REPRO_LABEL        }, /* 재생산라벨   */

    /* ── 지식 뱅크 v22.6.0 (Stage 24) ───────────────── */
    { "\xec\xa7\x80\xec\x8b\x9d\xeb\xb6\x88\xeb\x9f\xac\xec\x98\xa4\xea\xb8\xb0", TOK_KW_KBANK_LOAD    }, /* 지식불러오기 */
    { "\xec\xa7\x80\xec\x8b\x9d\xec\xa6\x9d\xea\xb1\xb0\xec\xb6\x9c\xeb\xa0\xa5", TOK_KW_KBANK_PROOF   }, /* 지식증거출력 */
    { "\xec\x9e\xac\xec\x83\x9d\xec\x82\xb0\xeb\x9d\xbc\xeb\xb2\xa8\xec\x84\xa0\xec\x96\xb8", TOK_KW_REPRO_LABEL_DECL }, /* 재생산라벨선언 */
    { "\xec\xa7\x80\xec\x8b\x9d\xeb\xb1\x85\xed\x81\xac",             TOK_KW_KBANK         }, /* 지식뱅크     */
    { "\xec\xa7\x80\xec\x8b\x9d\xeb\xb9\x84\xea\xb5\x90",             TOK_KW_KBANK_COMPARE }, /* 지식비교     */

    { NULL, TOK_EOF }
};

/* ================================================================
 *  렉서 내부 헬퍼
 * ================================================================ */

/* 현재 위치의 바이트를 반환 (범위 초과면 0) */
static char cur_byte(const Lexer *lx)
{
    if (lx->pos >= lx->src_len) return '\0';
    return lx->src[lx->pos];
}

/* 다음 바이트를 반환 */
static char peek_byte(const Lexer *lx, size_t offset)
{
    size_t p = lx->pos + offset;
    if (p >= lx->src_len) return '\0';
    return lx->src[p];
}

/* 현재 위치에서 UTF-8 한 문자를 읽어 코드포인트와 바이트 길이 반환 */
static uint32_t cur_cp(const Lexer *lx, int *blen)
{
    if (lx->pos >= lx->src_len) { *blen = 0; return 0; }
    return utf8_decode(lx->src + lx->pos, lx->src_len - lx->pos, blen);
}

/* 위치 전진 (줄/열 업데이트 포함) */
static void advance(Lexer *lx, int byte_count, int is_newline)
{
    lx->pos += (size_t)byte_count;
    if (is_newline) {
        lx->line++;
        lx->col = 1;
        lx->line_start_pos = (int)lx->pos;
    } else {
        lx->col++; /* UTF-8 문자 하나 = 열 1 증가 */
    }
}

/* 오류 토큰 생성 */
static Token make_error(Lexer *lx, const char *msg)
{
    snprintf(lx->error_msg, sizeof(lx->error_msg),
             "[렉서 오류] 줄 %d, 열 %d: %s", lx->line, lx->col, msg);
    lx->had_error = 1;

    Token t;
    t.type   = TOK_ERROR;
    t.start  = lx->src + lx->pos;
    t.length = 0;
    t.line   = lx->line;
    t.col    = lx->col;
    return t;
}

/* 기본 토큰 생성 */
static Token make_token(Lexer *lx, TokenType type,
                        const char *start, size_t length)
{
    Token t;
    t.type   = type;
    t.start  = start;
    t.length = length;
    t.line   = lx->line;
    t.col    = lx->col;
    t.val.ival = 0;
    return t;
}

/* ================================================================
 *  주석 스킵
 * ================================================================ */

/* 한 줄 주석 '//' — 줄 끝까지 소비 */
static void skip_line_comment(Lexer *lx)
{
    while (lx->pos < lx->src_len) {
        char c = cur_byte(lx);
        if (c == '\n') break;
        lx->pos++;
        lx->col++;
    }
}

/* 여러 줄 주석 '/ * ... * /' */
static int skip_block_comment(Lexer *lx)
{
    /* '/ *' 이미 소비됐다고 가정 */
    while (lx->pos + 1 < lx->src_len) {
        char c = cur_byte(lx);
        if (c == '*' && peek_byte(lx, 1) == '/') {
            lx->pos += 2;
            lx->col += 2;
            return 1;
        }
        if (c == '\n') {
            advance(lx, 1, 1);
        } else {
            lx->pos++;
            lx->col++;
        }
    }
    return 0; /* 닫는 위치를 못 찾음 */
}

/* ================================================================
 *  들여쓰기 처리
 * ================================================================ */

/*
 * 줄 시작에서 앞 공백을 세고 INDENT/DEDENT 토큰을 발행한다.
 * 공백 4칸 = 탭 1개로 동일 취급.
 *
 * 반환값: INDENT, DEDENT, TOK_NEWLINE(들여쓰기 변화 없음), TOK_EOF
 */
static Token handle_indent(Lexer *lx)
{
    /* 현재 줄의 선행 공백 수를 센다 */
    int spaces = 0;
    size_t save_pos = lx->pos;

    while (lx->pos < lx->src_len) {
        char c = cur_byte(lx);
        if (c == ' ') {
            spaces++;
            lx->pos++;
        } else if (c == '\t') {
            /* 탭 1개 = 공백 4칸 */
            spaces += 4;
            lx->pos++;
        } else {
            break;
        }
    }

    /* 공백만 있는 줄 또는 주석 줄은 무시 */
    char c = cur_byte(lx);
    if (c == '\n' || c == '\r' || c == '\0'
     || (c == '/' && peek_byte(lx, 1) == '/')
     || (c == '/' && peek_byte(lx, 1) == '*')) {
        /* 빈 줄 또는 주석만 있는 줄: 공백 계산 롤백 후 무시 */
        (void)save_pos;
        lx->col = 1;
        return make_token(lx, TOK_NEWLINE, lx->src + save_pos, 0);
    }

    lx->col = spaces + 1;

    /* 연속 줄(continuation) — 스택 갱신 없이 공백만 소비하고 NEWLINE 반환 */
    if (lx->skip_next_indent) {
        lx->skip_next_indent = 0;
        return make_token(lx, TOK_NEWLINE, lx->src + save_pos, 0);
    }

    int cur_indent = lx->indent_stack[lx->indent_top];

    if (spaces > cur_indent) {
        /* 들여쓰기 증가 */
        if (lx->indent_top + 1 >= KCODE_MAX_INDENT_DEPTH) {
            return make_error(lx, "들여쓰기 깊이가 너무 깊습니다 (최대 128단계)");
        }
        lx->indent_stack[++lx->indent_top] = spaces;
        return make_token(lx, TOK_INDENT, lx->src + save_pos,
                          lx->pos - save_pos);
    }
    if (spaces < cur_indent) {
        /* 들여쓰기 감소 — 여러 단계일 수 있음 */
        while (lx->indent_top > 0
            && lx->indent_stack[lx->indent_top] > spaces) {
            lx->indent_top--;
            lx->pending_dedents++;
        }
        if (lx->indent_stack[lx->indent_top] != spaces) {
            return make_error(lx, "들여쓰기 수준이 맞지 않습니다");
        }
        /* 첫 번째 DEDENT 즉시 반환, 나머지는 pending */
        lx->pending_dedents--;
        return make_token(lx, TOK_DEDENT, lx->src + save_pos, 0);
    }

    /* 들여쓰기 변화 없음 */
    return make_token(lx, TOK_NEWLINE, lx->src + save_pos, 0);
}

/* ================================================================
 *  숫자 리터럴 파싱
 * ================================================================ */

static Token lex_number(Lexer *lx)
{
    const char *start = lx->src + lx->pos;
    int   start_col   = lx->col;
    char  c           = cur_byte(lx);

    /* 2진 / 8진 / 16진 */
    if (c == '0' && lx->pos + 1 < lx->src_len) {
        char next = peek_byte(lx, 1);

        if (next == 'b' || next == 'B') {
            lx->pos += 2; lx->col += 2;
            int64_t val = 0;
            int has_digit = 0;
            while (lx->pos < lx->src_len) {
                char d = cur_byte(lx);
                if (d == '0' || d == '1') {
                    val = val * 2 + (d - '0');
                    lx->pos++; lx->col++;
                    has_digit = 1;
                } else if (d == '_') {
                    lx->pos++; lx->col++; /* 구분자 _ 허용 */
                } else break;
            }
            if (!has_digit) return make_error(lx, "2진 리터럴에 숫자가 없습니다");
            Token t = make_token(lx, TOK_BIN, start,
                                 (size_t)(lx->src + lx->pos - start));
            t.val.ival = val;
            t.col = start_col;
            return t;
        }

        if (next == 'o' || next == 'O') {
            lx->pos += 2; lx->col += 2;
            int64_t val = 0;
            int has_digit = 0;
            while (lx->pos < lx->src_len) {
                char d = cur_byte(lx);
                if (d >= '0' && d <= '7') {
                    val = val * 8 + (d - '0');
                    lx->pos++; lx->col++;
                    has_digit = 1;
                } else if (d == '_') {
                    lx->pos++; lx->col++;
                } else break;
            }
            if (!has_digit) return make_error(lx, "8진 리터럴에 숫자가 없습니다");
            Token t = make_token(lx, TOK_OCT, start,
                                 (size_t)(lx->src + lx->pos - start));
            t.val.ival = val;
            t.col = start_col;
            return t;
        }

        if (next == 'x' || next == 'X') {
            lx->pos += 2; lx->col += 2;
            int64_t val = 0;
            int has_digit = 0;
            while (lx->pos < lx->src_len) {
                char d = cur_byte(lx);
                if (d >= '0' && d <= '9') {
                    val = val * 16 + (d - '0');
                    lx->pos++; lx->col++;
                    has_digit = 1;
                } else if (d >= 'a' && d <= 'f') {
                    val = val * 16 + (d - 'a' + 10);
                    lx->pos++; lx->col++;
                    has_digit = 1;
                } else if (d >= 'A' && d <= 'F') {
                    val = val * 16 + (d - 'A' + 10);
                    lx->pos++; lx->col++;
                    has_digit = 1;
                } else if (d == '_') {
                    lx->pos++; lx->col++;
                } else break;
            }
            if (!has_digit) return make_error(lx, "16진 리터럴에 숫자가 없습니다");
            Token t = make_token(lx, TOK_HEX, start,
                                 (size_t)(lx->src + lx->pos - start));
            t.val.ival = val;
            t.col = start_col;
            return t;
        }
    }

    /* 일반 정수 또는 실수 */
    int64_t ival = 0;
    while (lx->pos < lx->src_len) {
        char d = cur_byte(lx);
        if (d >= '0' && d <= '9') {
            ival = ival * 10 + (d - '0');
            lx->pos++; lx->col++;
        } else if (d == '_') {
            lx->pos++; lx->col++;
        } else break;
    }

    /* 소수점 확인 → 실수 */
    if (cur_byte(lx) == '.' && peek_byte(lx, 1) >= '0'
     && peek_byte(lx, 1) <= '9') {
        lx->pos++; lx->col++; /* '.' 소비 */
        double fval = (double)ival;
        double frac = 0.1;
        while (lx->pos < lx->src_len) {
            char d = cur_byte(lx);
            if (d >= '0' && d <= '9') {
                fval += (d - '0') * frac;
                frac *= 0.1;
                lx->pos++; lx->col++;
            } else if (d == '_') {
                lx->pos++; lx->col++;
            } else break;
        }
        /* 지수 표기 (e / E) */
        if (cur_byte(lx) == 'e' || cur_byte(lx) == 'E') {
            lx->pos++; lx->col++;
            int exp_sign = 1;
            if (cur_byte(lx) == '+') { lx->pos++; lx->col++; }
            else if (cur_byte(lx) == '-') { exp_sign = -1; lx->pos++; lx->col++; }
            int exp = 0;
            while (cur_byte(lx) >= '0' && cur_byte(lx) <= '9') {
                exp = exp * 10 + (cur_byte(lx) - '0');
                lx->pos++; lx->col++;
            }
            double base = 1.0;
            for (int i = 0; i < exp; i++) base *= 10.0;
            fval = (exp_sign > 0) ? fval * base : fval / base;
        }
        Token t = make_token(lx, TOK_FLOAT, start,
                             (size_t)(lx->src + lx->pos - start));
        t.val.fval = fval;
        t.col = start_col;
        return t;
    }

    Token t = make_token(lx, TOK_INT, start,
                         (size_t)(lx->src + lx->pos - start));
    t.val.ival = ival;
    t.col = start_col;
    return t;
}

/* ================================================================
 *  문자열 리터럴 파싱  "..."
 * ================================================================ */
static Token lex_string(Lexer *lx)
{
    const char *start = lx->src + lx->pos;
    int start_col = lx->col;
    lx->pos++; lx->col++; /* 여는 '"' 소비 */

    while (lx->pos < lx->src_len) {
        char c = cur_byte(lx);

        if (c == '"') {
            lx->pos++; lx->col++;
            Token t = make_token(lx, TOK_STRING, start,
                                 (size_t)(lx->src + lx->pos - start));
            t.col = start_col;
            return t;
        }
        if (c == '\n') {
            return make_error(lx, "문자열이 줄 끝에서 닫히지 않았습니다");
        }
        if (c == '\\') {
            /* 이스케이프 시퀀스 */
            lx->pos++; lx->col++;
            if (lx->pos >= lx->src_len) break;
            lx->pos++; lx->col++;
        } else {
            int blen;
            utf8_decode(lx->src + lx->pos, lx->src_len - lx->pos, &blen);
            lx->pos += blen;
            lx->col++;
        }
    }
    return make_error(lx, "문자열이 닫히지 않았습니다");
}

/* ================================================================
 *  문자 리터럴 파싱  '...'
 * ================================================================ */
static Token lex_char(Lexer *lx)
{
    const char *start = lx->src + lx->pos;
    int start_col = lx->col;
    lx->pos++; lx->col++; /* 여는 '\'' 소비 */

    if (lx->pos >= lx->src_len) {
        return make_error(lx, "문자 리터럴이 비어 있습니다");
    }

    uint32_t cp;
    char c = cur_byte(lx);
    if (c == '\\') {
        lx->pos++; lx->col++;
        char esc = cur_byte(lx);
        switch (esc) {
            case 'n':  cp = '\n'; break;
            case 't':  cp = '\t'; break;
            case 'r':  cp = '\r'; break;
            case '\\': cp = '\\'; break;
            case '\'': cp = '\''; break;
            case '0':  cp = '\0'; break;
            default:
                return make_error(lx, "알 수 없는 이스케이프 시퀀스입니다");
        }
        lx->pos++; lx->col++;
    } else {
        int blen;
        cp = utf8_decode(lx->src + lx->pos, lx->src_len - lx->pos, &blen);
        lx->pos += blen;
        lx->col++;
    }

    if (cur_byte(lx) != '\'') {
        return make_error(lx, "문자 리터럴은 한 문자여야 합니다");
    }
    lx->pos++; lx->col++;

    Token t = make_token(lx, TOK_CHAR_LIT, start,
                         (size_t)(lx->src + lx->pos - start));
    t.val.cval = cp;
    t.col = start_col;
    return t;
}

/* ================================================================
 *  전처리기 디렉티브 파싱  #...
 * ================================================================ */
static Token lex_preprocessor(Lexer *lx)
{
    const char *start = lx->src + lx->pos;
    int start_col = lx->col;
    lx->pos++; lx->col++; /* '#' 소비 */

    /* 공백 스킵 */
    while (cur_byte(lx) == ' ') { lx->pos++; lx->col++; }

    size_t kw_len;
    for (int i = 0; PP_TABLE[i].word != NULL; i++) {
        const char *w    = PP_TABLE[i].word;
        size_t      wlen = strlen(w);
        if (lx->pos + wlen > lx->src_len) continue;
        if (memcmp(lx->src + lx->pos, w, wlen) != 0) continue;

        lx->pos += wlen;
        lx->col += (int)wlen; /* ASCII 혼합이므로 근사 처리 */
        kw_len = (size_t)(lx->src + lx->pos - start);

        Token t = make_token(lx, PP_TABLE[i].type, start, kw_len);
        t.col = start_col;
        return t;
    }

    return make_error(lx, "알 수 없는 전처리기 디렉티브입니다");
}

/* ================================================================
 *  식별자 또는 키워드 파싱
 * ================================================================ */
static Token lex_ident_or_keyword(Lexer *lx)
{
    const char *start = lx->src + lx->pos;
    int start_col = lx->col;
    size_t start_pos = lx->pos;

    /* 먼저 키워드 매칭 시도 (긴 것부터 — 테이블 순서로) */
    size_t matched_len = 0;
    TokenType ktype = match_keyword(lx->src, lx->pos, lx->src_len,
                                    &matched_len);
    if (ktype != TOK_EOF) {
        /* 키워드 전진 */
        size_t end = start_pos + matched_len;
        while (lx->pos < end) {
            int blen;
            utf8_decode(lx->src + lx->pos, lx->src_len - lx->pos, &blen);
            lx->pos += blen;
            lx->col++;
        }
        Token t = make_token(lx, ktype, start, matched_len);
        t.col = start_col;
        return t;
    }

    /* 식별자 */
    while (lx->pos < lx->src_len) {
        int blen;
        uint32_t cp = cur_cp(lx, &blen);
        if (!is_ident_cont(cp)) break;
        lx->pos += blen;
        lx->col++;
    }

    Token t = make_token(lx, TOK_IDENT, start,
                         (size_t)(lx->src + lx->pos - start));
    t.col = start_col;
    return t;
}

/* ================================================================
 *  공개 API 구현
 * ================================================================ */

void lexer_init(Lexer *lx, const char *src, size_t len)
{
    memset(lx, 0, sizeof(*lx));
    lx->src           = src;
    lx->src_len       = len;
    lx->pos           = 0;
    lx->line          = 1;
    lx->col           = 1;
    lx->line_start_pos = 0;
    lx->indent_stack[0] = 0;
    lx->indent_top    = 0;
    lx->pending_dedents = 0;
    lx->at_line_start = 1;
    lx->bracket_depth = 0;
    lx->last_tok_type = TOK_EOF;
    lx->cont_indent_depth = 0;
    lx->skip_next_indent = 0;
    lx->had_error     = 0;
}

static Token lexer_next_raw(Lexer *lx)
{
    /* ── 대기 중인 DEDENT 먼저 반환 (괄호 내부에서는 억제) ── */
    if (lx->pending_dedents > 0) {
        if (lx->bracket_depth == 0) {
            lx->pending_dedents--;
            return make_token(lx, TOK_DEDENT, lx->src + lx->pos, 0);
        }
        lx->pending_dedents = 0;  /* 괄호 내부: 쌓인 DEDENT 버림 */
    }

    /* ── 줄 시작 들여쓰기 처리 (괄호 내부에서는 억제) ────── */
    if (lx->at_line_start) {
        lx->at_line_start = 0;
        lx->col = 1;
        if (lx->bracket_depth == 0) {
            Token ind = handle_indent(lx);
            if (ind.type == TOK_INDENT || ind.type == TOK_DEDENT) {
                return ind;
            }
        } else {
            /* 괄호 내부: 들여쓰기 공백만 건너뜀, INDENT/DEDENT 발행 안 함 */
            while (lx->pos < lx->src_len &&
                   (lx->src[lx->pos] == ' ' || lx->src[lx->pos] == '\t')) {
                lx->pos++; lx->col++;
            }
        }
        /* NEWLINE 또는 빈 줄: 계속 진행 */
    }

main_loop:
    /* ── 공백/탭 스킵 (줄 안에서) ──────────────────────── */
    while (lx->pos < lx->src_len) {
        char c = cur_byte(lx);
        if (c == ' ' || c == '\t') {
            lx->pos++; lx->col++;
        } else {
            break;
        }
    }

    if (lx->pos >= lx->src_len) {
        /* 파일 끝에서 남은 DEDENT 발행 */
        if (lx->indent_top > 0) {
            lx->indent_top--;
            return make_token(lx, TOK_DEDENT, lx->src + lx->pos, 0);
        }
        return make_token(lx, TOK_EOF, lx->src + lx->pos, 0);
    }

    char c = cur_byte(lx);

    /* ── 줄바꿈 ─────────────────────────────────────────── */
    if (c == '\r') {
        lx->pos++;
        if (cur_byte(lx) == '\n') lx->pos++;
        lx->line++;
        lx->col = 1;
        lx->line_start_pos = (int)lx->pos;
        lx->at_line_start  = 1;
        if (lx->bracket_depth > 0) goto main_loop;  /* 괄호 내부: NEWLINE 억제 */
        return make_token(lx, TOK_NEWLINE, lx->src + lx->pos - 1, 1);
    }
    if (c == '\n') {
        lx->pos++; lx->line++; lx->col = 1;
        lx->line_start_pos = (int)lx->pos;
        lx->at_line_start  = 1;
        if (lx->bracket_depth > 0) goto main_loop;  /* 괄호 내부: NEWLINE 억제 */
        return make_token(lx, TOK_NEWLINE, lx->src + lx->pos - 1, 1);
    }

    /* ── 주석 ──────────────────────────────────────────── */
    if (c == '/' && peek_byte(lx, 1) == '/') {
        lx->pos += 2; lx->col += 2;
        skip_line_comment(lx);
        goto main_loop;
    }
    if (c == '/' && peek_byte(lx, 1) == '*') {
        lx->pos += 2; lx->col += 2;
        if (!skip_block_comment(lx)) {
            return make_error(lx, "여러 줄 주석이 닫히지 않았습니다 (*/ 없음)");
        }
        goto main_loop;
    }

    /* ── 전처리기 ──────────────────────────────────────── */
    if (c == '#') {
        return lex_preprocessor(lx);
    }

    /* ── 숫자 ──────────────────────────────────────────── */
    if (c >= '0' && c <= '9') {
        return lex_number(lx);
    }

    /* ── 문자열 ────────────────────────────────────────── */
    if (c == '"') {
        return lex_string(lx);
    }

    /* ── 문자 ──────────────────────────────────────────── */
    if (c == '\'') {
        return lex_char(lx);
    }

    /* ── 연산자 및 구분자 ───────────────────────────────── */
    const char *start = lx->src + lx->pos;

#define SIMPLE_TOK(ch, type_) \
    case ch: lx->pos++; lx->col++; \
             return make_token(lx, type_, start, 1);

    switch (c) {
        /* 단일 문자 구분자 — 괄호는 bracket_depth 업데이트 */
        case '(': lx->pos++; lx->col++; lx->bracket_depth++;
                  return make_token(lx, TOK_LPAREN,   start, 1);
        case ')': lx->pos++; lx->col++;
                  if (lx->bracket_depth > 0) lx->bracket_depth--;
                  return make_token(lx, TOK_RPAREN,   start, 1);
        case '[': lx->pos++; lx->col++; lx->bracket_depth++;
                  return make_token(lx, TOK_LBRACKET, start, 1);
        case ']': lx->pos++; lx->col++;
                  if (lx->bracket_depth > 0) lx->bracket_depth--;
                  return make_token(lx, TOK_RBRACKET, start, 1);
        case '{': lx->pos++; lx->col++; lx->bracket_depth++;
                  return make_token(lx, TOK_LBRACE,   start, 1);
        case '}': lx->pos++; lx->col++;
                  if (lx->bracket_depth > 0) lx->bracket_depth--;
                  return make_token(lx, TOK_RBRACE,   start, 1);
        SIMPLE_TOK(',', TOK_COMMA)
        SIMPLE_TOK(':', TOK_COLON)
        SIMPLE_TOK('~', TOK_TILDE)
        SIMPLE_TOK('^', TOK_CARET)

        case '.':
            /* ... 가변 매개변수 */
            if (peek_byte(lx, 1) == '.' && peek_byte(lx, 2) == '.') {
                lx->pos += 3; lx->col += 3;
                return make_token(lx, TOK_DOTS, start, 3);
            }
            lx->pos++; lx->col++;
            return make_token(lx, TOK_DOT, start, 1);

        case '+':
            lx->pos++; lx->col++;
            if (cur_byte(lx) == '=') { lx->pos++; lx->col++;
                return make_token(lx, TOK_PLUSEQ, start, 2); }
            return make_token(lx, TOK_PLUS, start, 1);

        case '-':
            lx->pos++; lx->col++;
            if (cur_byte(lx) == '=') { lx->pos++; lx->col++;
                return make_token(lx, TOK_MINUSEQ, start, 2); }
            return make_token(lx, TOK_MINUS, start, 1);

        case '*':
            lx->pos++; lx->col++;
            if (cur_byte(lx) == '*') { lx->pos++; lx->col++;
                return make_token(lx, TOK_STARSTAR, start, 2); }
            if (cur_byte(lx) == '=') { lx->pos++; lx->col++;
                return make_token(lx, TOK_STAREQ, start, 2); }
            return make_token(lx, TOK_STAR, start, 1);

        case '/':
            lx->pos++; lx->col++;
            if (cur_byte(lx) == '=') { lx->pos++; lx->col++;
                return make_token(lx, TOK_SLASHEQ, start, 2); }
            return make_token(lx, TOK_SLASH, start, 1);

        case '%':
            lx->pos++; lx->col++;
            if (cur_byte(lx) == '=') { lx->pos++; lx->col++;
                return make_token(lx, TOK_PERCENTEQ, start, 2); }
            return make_token(lx, TOK_PERCENT, start, 1);

        case '=':
            lx->pos++; lx->col++;
            if (cur_byte(lx) == '=') { lx->pos++; lx->col++;
                return make_token(lx, TOK_EQEQ, start, 2); }
            if (cur_byte(lx) == '>') { lx->pos++; lx->col++;
                return make_token(lx, TOK_ARROW, start, 2); }
            return make_token(lx, TOK_EQ, start, 1);

        case '!':
            lx->pos++; lx->col++;
            if (cur_byte(lx) == '=') { lx->pos++; lx->col++;
                return make_token(lx, TOK_BANGEQ, start, 2); }
            return make_error(lx, "'!' 단독은 지원하지 않습니다. '아니다' 또는 '!=' 를 사용하세요");

        case '>':
            lx->pos++; lx->col++;
            if (cur_byte(lx) == '=') { lx->pos++; lx->col++;
                return make_token(lx, TOK_GTEQ, start, 2); }
            if (cur_byte(lx) == '>') { lx->pos++; lx->col++;
                return make_token(lx, TOK_GTGT, start, 2); }
            return make_token(lx, TOK_GT, start, 1);

        case '<':
            lx->pos++; lx->col++;
            if (cur_byte(lx) == '=') { lx->pos++; lx->col++;
                return make_token(lx, TOK_LTEQ, start, 2); }
            if (cur_byte(lx) == '<') { lx->pos++; lx->col++;
                return make_token(lx, TOK_LTLT, start, 2); }
            return make_token(lx, TOK_LT, start, 1);

        case '&':
            lx->pos++; lx->col++;
            return make_token(lx, TOK_AMP, start, 1);

        case '|':
            lx->pos++; lx->col++;
            return make_token(lx, TOK_PIPE, start, 1);

        default:
            break;
    }
#undef SIMPLE_TOK

    /* ── 한글 및 영문 식별자 / 키워드 ─────────────────── */
    {
        int blen;
        uint32_t cp = cur_cp(lx, &blen);
        if (is_ident_start(cp)) {
            return lex_ident_or_keyword(lx);
        }
    }

    /* ── 알 수 없는 문자 ───────────────────────────────── */
    {
        int blen;
        utf8_decode(lx->src + lx->pos, lx->src_len - lx->pos, &blen);
        char msg[64];
        snprintf(msg, sizeof(msg), "알 수 없는 문자입니다 (U+%04X)", 
                 (unsigned)utf8_decode(lx->src + lx->pos,
                                       lx->src_len - lx->pos, &blen));
        lx->pos += blen;
        lx->col++;
        return make_error(lx, msg);
    }
}

/* ── 이항 연산자 뒤 줄바꿈 억제 판단 ─────────────────────────── */
static int is_continuation_op(TokenType t) {
    switch (t) {
        case TOK_KW_AND: case TOK_KW_OR:           /* 그리고, 또는 */
        case TOK_PLUS:   case TOK_MINUS:            /* + -         */
        case TOK_STAR:   case TOK_SLASH:            /* * /         */
        case TOK_PERCENT:case TOK_STARSTAR:          /* % **        */
        case TOK_EQEQ:   case TOK_BANGEQ:           /* == !=       */
        case TOK_GT:     case TOK_LT:               /* > <         */
        case TOK_GTEQ:   case TOK_LTEQ:             /* >= <=       */
        case TOK_AMP:    case TOK_PIPE: case TOK_CARET: /* & | ^   */
        case TOK_LTLT:   case TOK_GTGT:             /* << >>       */
        case TOK_COMMA:                             /* ,           */
        case TOK_LPAREN: case TOK_LBRACKET:         /* ( [         */
            return 1;
        default:
            return 0;
    }
}

/* 공개 API: last_tok_type 추적 + 이항 연산자 뒤 NEWLINE/INDENT/DEDENT 억제 */
Token lexer_next(Lexer *lx)
{
    for (;;) {
        Token t = lexer_next_raw(lx);

        /* 이항 연산자 뒤 NEWLINE 억제 (괄호 외부에서만) */
        if (t.type == TOK_NEWLINE && lx->bracket_depth == 0
                && is_continuation_op(lx->last_tok_type)) {
            /* NEWLINE을 삼키고 계속 — 다음 줄 들여쓰기도 스택에 반영하지 않음 */
            lx->skip_next_indent = 1;
            continue;
        }
        /* 연산자 뒤 줄바꿈 억제로 인해 생긴 INDENT/DEDENT도 삼킴 */
        if ((t.type == TOK_INDENT || t.type == TOK_DEDENT)
                && lx->bracket_depth == 0
                && is_continuation_op(lx->last_tok_type)) {
            if (t.type == TOK_INDENT) lx->cont_indent_depth++;
            else if (lx->cont_indent_depth > 0) lx->cont_indent_depth--;
            else { lx->last_tok_type = t.type; return t; } /* 블록 경계는 유지 */
            continue;
        }
        /* cont_indent_depth > 0 이면 대응 DEDENT 삼킴 */
        if (t.type == TOK_DEDENT && lx->cont_indent_depth > 0) {
            lx->cont_indent_depth--;
            continue;
        }

        /* NEWLINE/INDENT/DEDENT는 last_tok_type을 갱신하지 않음
         * (실제 연산자/피연산자 정보를 유지하기 위해) */
        if (t.type != TOK_NEWLINE &&
            t.type != TOK_INDENT  &&
            t.type != TOK_DEDENT)
            lx->last_tok_type = t.type;
        return t;
    }
}

/* ================================================================
 *  peek (미리 보기)
 * ================================================================ */
Token lexer_peek(Lexer *lx)
{
    /* 상태 저장 → next → 상태 복원 */
    Lexer save = *lx;
    Token t    = lexer_next(lx);
    *lx = save;
    return t;
}

/* ================================================================
 *  디버깅 유틸리티
 * ================================================================ */
static const char *TOK_NAMES[TOK_COUNT] = {
    "INT", "FLOAT", "STRING", "CHAR_LIT", "TRUE", "FALSE", "NULL",
    "BIN", "OCT", "HEX",
    "IDENT",
    /* 자료형 */
    "KW_정수","KW_실수","KW_문자","KW_글자","KW_논리","KW_없음",
    "KW_행렬","KW_텐서","KW_영텐서","KW_일텐서","KW_무작위텐서","KW_사진","KW_그림","KW_함수형","KW_고정",
    /* 제어문 */
    "KW_만약","KW_아니면","KW_선택","KW_경우","KW_그외",
    "KW_반복","KW_동안","KW_각각","KW_안에","KW_부터","KW_까지",
    "KW_멈춤","KW_건너뜀","KW_이동",
    /* 함수 */
    "KW_함수","KW_정의","KW_반환","KW_끝냄",
    /* 자료구조 */
    "KW_배열","KW_사전","KW_목록","KW_객체","KW_열거","KW_틀",
    "KW_이어받기","KW_자신","KW_부모","KW_생성",
    /* 예외 */
    "KW_시도","KW_실패시","KW_항상","KW_오류",
    /* 모듈/입출력 */
    "KW_가짐","KW_로부터","KW_출력","KW_출력없이","KW_입력","KW_파일열기",
    /* 이미지/AI */
    "KW_화면","KW_화면3D","KW_AI연결","KW_가속기","KW_가속기끝",
    /* 가속기 내장 연산 v23.0.0 */
    "KW_행렬곱","KW_행렬합","KW_합성곱","KW_활성화","KW_전치",
    "KW_사진열기","KW_그림만들기",
    "KW_파이썬","KW_파이썬끝","KW_자바","KW_자바끝","KW_자바스크립트","KW_자바스크립트끝",
    /* 계약 시스템 v4.1 */
    "KW_법령","KW_법위반","KW_복원지점","KW_전역","KW_회귀","KW_대체","KW_경고","KW_보고","KW_중단",
    /* 계약 시스템 v4.2 계층 */
    "KW_헌법","KW_법률","KW_규정","KW_규정끝","KW_법령끝","KW_법위반끝","KW_조항","KW_규칙",
    /* 파일 내장 함수 */
    "KW_파일닫기","KW_파일전체읽기","KW_파일전체쓰기",
    "KW_파일줄읽기","KW_파일줄쓰기","KW_파일읽기","KW_파일쓰기",
    "KW_파일있음","KW_파일크기","KW_파일목록","KW_파일이름","KW_파일확장자",
    "KW_폴더만들기","KW_파일지우기","KW_파일복사","KW_파일이동",
    /* 인터럽트 시스템 v6.0.0 — A: OS 시그널 */
    "KW_신호받기","KW_신호받기끝","KW_신호무시","KW_신호기본","KW_신호보내기",
    "KW_중단신호","KW_종료신호","KW_강제종료","KW_자식신호",
    "KW_사용자신호1","KW_사용자신호2","KW_연결신호","KW_경보신호","KW_중지신호","KW_재개신호",
    /* B: 하드웨어 간섭 */
    "KW_간섭","KW_간섭끝","KW_간섭잠금","KW_간섭허용",
    "KW_시간0넘침","KW_시간1넘침","KW_외부0상승","KW_외부0하강","KW_직렬수신",
    /* C: 행사(이벤트 루프) */
    "KW_행사등록","KW_행사등록끝","KW_행사시작","KW_행사중단","KW_행사발생","KW_행사해제",
    /* MCP 시스템 v14.0.1 */
    "KW_MCP서버","KW_MCP서버끝","KW_MCP도구","KW_MCP도구끝",
    "KW_MCP자원","KW_MCP자원끝","KW_MCP프롬프트","KW_MCP프롬프트끝",
    /* 산업/임베디드 v16.0.0 */
    "KW_타이머","KW_타이머끝",
    "KW_GPIO쓰기","KW_GPIO읽기",
    "KW_I2C연결","KW_I2C읽기","KW_I2C쓰기",
    "KW_SPI전송","KW_SPI읽기",
    "KW_UART설정","KW_UART전송","KW_UART읽기",
    "KW_Modbus연결","KW_Modbus읽기","KW_Modbus쓰기","KW_Modbus연결끊기",
    "KW_CAN필터","KW_CAN전송","KW_CAN읽기",
    "KW_MQTT연결","KW_MQTT발행","KW_MQTT구독","KW_MQTT연결끊기",
    "KW_ROS2노드","KW_ROS2끝","KW_ROS2발행","KW_ROS2구독",
    /* 안전 규격 v17.0.0 */
    "KW_SIL","KW_워치독","KW_워치독끝","KW_결함허용","KW_결함허용끝",
    "KW_페일세이프","KW_긴급정지",
    /* 온디바이스 AI v18.0.0 */
    "KW_AI모델","KW_AI모델끝","KW_TinyML","KW_TinyML끝",
    "KW_AI불러오기","KW_AI추론","KW_AI학습단계","KW_AI저장",
    "KW_연합학습","KW_연합학습끝",
    /* 온톨로지 시스템 v18.5.0 4회차 */
    "KW_온톨로지","KW_온톨로지끝","KW_개념","KW_개념끝",
    "KW_속성","KW_관계","KW_질의","KW_추론","KW_민감","KW_익명화",
    /* Concept Identity v22.0.0 */
    "KW_의미추론","KW_의미추론끝","KW_벡터화","KW_벡터화끝",
    "KW_의미복원","KW_의미복원끝","KW_재생산라벨","KW_재생산라벨끝",
    /* 지식 뱅크 v22.6.0 */
    "KW_지식뱅크","KW_지식불러오기","KW_지식비교","KW_재생산라벨선언","KW_지식증거출력",
    /* 전처리기 */
    "PP_정의","PP_만약","PP_아니면","PP_끝","PP_만약있으면",
    "PP_만약없으면","PP_포함","PP_GPU사용",
    /* 연산자 */
    "PLUS","MINUS","STAR","SLASH","PERCENT","STARSTAR",
    "EQEQ","BANGEQ","GT","LT","GTEQ","LTEQ",
    "KW_그리고","KW_또는","KW_아니다",
    "AMP","PIPE","CARET","TILDE","LTLT","GTGT",
    "EQ","PLUSEQ","MINUSEQ","STAREQ","SLASHEQ","PERCENTEQ",
    "ARROW","DOTS",
    /* 구분자 */
    "LPAREN","RPAREN","LBRACKET","RBRACKET","LBRACE","RBRACE",
    "COMMA","COLON","DOT",
    /* 들여쓰기 */
    "NEWLINE","INDENT","DEDENT",
    /* 특수 */
    "EOF","ERROR"
};

const char *token_type_name(TokenType t)
{
    if (t < 0 || t >= TOK_COUNT) return "UNKNOWN";
    return TOK_NAMES[t];
}

void token_to_str(const Token *tok, char *buf, size_t buf_size)
{
    size_t copy = tok->length < buf_size - 1 ? tok->length : buf_size - 1;
    memcpy(buf, tok->start, copy);
    buf[copy] = '\0';
}

/* ================================================================
 *  lexer_read_raw_script() — 스크립트 블록 원문 raw 읽기
 *
 *  현재 위치부터 end_keyword 가 줄 시작에 등장할 때까지
 *  원문을 그대로 수집한다. 토큰화 없이 raw read 하므로
 *  화살표(=>) 나 블록주석 등 Kcode 문법과 충돌하지 않는다.
 * ================================================================ */
char *lexer_read_raw_script(Lexer *lx, const char *end_keyword) {
    size_t ek_len = strlen(end_keyword);
    size_t cap    = 4096;
    size_t len    = 0;
    char  *buf    = (char*)malloc(cap);
    if (!buf) return NULL;

    const char *src     = lx->src;
    size_t      src_len = lx->src_len;
    size_t      pos     = lx->pos;

    /* 공통 들��쓰기 계산 (첫 비어있지 않은 줄 기준) */
    size_t common_indent = SIZE_MAX;
    {
        size_t p2 = pos;
        while (p2 < src_len) {
            /* 줄 시작 공백 계산 */
            size_t indent = 0;
            size_t lp = p2;
            while (lp < src_len && (src[lp] == ' ' || src[lp] == '\t')) {
                indent += (src[lp] == '\t') ? 4 : 1;
                lp++;
            }
            /* 끝키워드 줄 도달하면 스캔 중단 */
            if (lp + ek_len <= src_len &&
                memcmp(src + lp, end_keyword, ek_len) == 0) {
                size_t aft = lp + ek_len;
                if (aft >= src_len ||
                    src[aft] == '\n' || src[aft] == '\r' ||
                    src[aft] == ' '  || src[aft] == '\t' ||
                    src[aft] == '\0') {
                    break;
                }
            }
            /* 빈 줄 건너뜀 */
            if (lp < src_len && src[lp] != '\n' && src[lp] != '\r') {
                if (indent < common_indent) common_indent = indent;
            }
            /* 다음 줄로 */
            while (p2 < src_len && src[p2] != '\n') p2++;
            if (p2 < src_len) p2++;
        }
    }
    if (common_indent == SIZE_MAX) common_indent = 0;

    while (pos < src_len) {
        /* 줄 시작에서 끝키워드 비교 */
        size_t check_pos = pos;
        while (check_pos < src_len &&
               (src[check_pos] == ' ' || src[check_pos] == '\t'))
            check_pos++;

        if (check_pos + ek_len <= src_len &&
            memcmp(src + check_pos, end_keyword, ek_len) == 0) {
            size_t after = check_pos + ek_len;
            if (after >= src_len ||
                src[after] == '\n' || src[after] == '\r' ||
                src[after] == ' '  || src[after] == '\t' ||
                src[after] == '\0') {
                /* 끝키워드 발견 */
                while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
                    len--;
                break;
            }
        }

        /* 공통 들여쓰기 제거 후 한 줄 추가 */
        size_t stripped = 0;
        while (pos < src_len && stripped < common_indent &&
               (src[pos] == ' ' || src[pos] == '\t')) {
            stripped += (src[pos] == '\t') ? 4 : 1;
            pos++;
        }

        size_t line_start = pos;
        while (pos < src_len && src[pos] != '\n') pos++;
        if (pos < src_len) pos++; /* '\n' 포함 */

        size_t line_len = pos - line_start;
        while (len + line_len + 1 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
        memcpy(buf + len, src + line_start, line_len);
        len += line_len;
    }

    buf[len] = '\0';
    lx->pos = pos;
    return buf;
}
