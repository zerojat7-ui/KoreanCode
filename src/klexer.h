/*
 * klexer.h  —  Kcode 한글 프로그래밍 언어 렉서 헤더
 * version : v23.0.0
 *
 * v23.0.0 변경 (가속기 재설계 v2.0 — 내장 연산 토큰 정식 등록):
 *   - TOK_KW_ACCEL_MATMUL    행렬곱  (GEMM)
 *   - TOK_KW_ACCEL_MATADD    행렬합  (Element-wise Add)
 *   - TOK_KW_ACCEL_CONV      합성곱  (Convolution)
 *   - TOK_KW_ACCEL_ACTIVATE  활성화  (ReLU/Activation)
 *   - TOK_KW_ACCEL_TRANSPOSE 전치    (Transpose)
 *
 * v22.0.0 변경 (Concept Identity 단계 9 — klexer 키워드 추가):
 *   - 의미추론 / 의미추론끝  TOK_KW_SEMANTIC_INFER / TOK_KW_SEMANTIC_INFER_END
 *   - 벡터화  / 벡터화끝    TOK_KW_VECTORIZE      / TOK_KW_VECTORIZE_END
 *   - 의미복원 / 의미복원끝  TOK_KW_SEM_RECON      / TOK_KW_SEM_RECON_END
 *   - 재생산라벨 / 재생산라벨끝 TOK_KW_REPRO_LABEL / TOK_KW_REPRO_LABEL_END
 *
 * v14.0.1 변경:
 *   - MCP 시스템 토큰 8종 추가 (kparser.c v14.0.0 누락 대응)
 *     TOK_KW_MCP_SERVER    / TOK_KW_KKUT_MCP_SERVER
 *     TOK_KW_MCP_TOOL      / TOK_KW_KKUT_MCP_TOOL
 *     TOK_KW_MCP_RESOURCE  / TOK_KW_KKUT_MCP_RESOURCE
 *     TOK_KW_MCP_PROMPT    / TOK_KW_KKUT_MCP_PROMPT
 *
 * v11.0.0 변경:
 *   - 텐서 자료형 키워드 4종 추가
 *     TOK_KW_TENSOR       텐서        (텐서 생성/타입 선언)
 *     TOK_KW_ZERO_TENSOR  영텐서      (영 텐서 생성)
 *     TOK_KW_ONE_TENSOR   일텐서      (일 텐서 생성)
 *     TOK_KW_RAND_TENSOR  무작위텐서  (랜덤 텐서 생성)
 *
 * v6.0.0 변경:
 *   - 인터럽트 시스템 3종 키워드 추가 (A/B/C)
 *   - A: OS 시그널 — 신호받기/신호무시/신호기본/신호보내기 + 신호 이름 10종
 *   - B: 하드웨어 간섭 — 간섭/간섭끝/간섭잠금/간섭허용 + 벡터 이름 5종
 *   - C: 행사(이벤트 루프) — 행사등록/행사등록끝/행사시작/행사중단/행사발생/행사해제
 *
 * v1.4.0 변경사항:
 *   - 계약 계층 토큰 7개 추가 (헌법/법률/규정/조항/법령끝/법위반끝/규정끝)
 *   - 파일 내장 함수 토큰 16개 추가
 *
 * UTF-8 한글 소스 파일(.han)을 읽어 토큰 스트림으로 변환한다.
 * Python 방식의 들여쓰기 기반 INDENT / DEDENT 토큰을 생성한다.
 */

#ifndef KCODE_LEXER_H
#define KCODE_LEXER_H

#include <stddef.h>
#include <stdint.h>

/* ================================================================
 *  토큰 종류
 * ================================================================ */
typedef enum {

    /* ── 리터럴 ──────────────────────────────────────── */
    TOK_INT,            /* 정수 리터럴        42, -5             */
    TOK_FLOAT,          /* 실수 리터럴        3.14               */
    TOK_STRING,         /* 문자열 리터럴      "안녕"             */
    TOK_CHAR_LIT,       /* 문자 리터럴        '가'               */
    TOK_TRUE,           /* 참                                    */
    TOK_FALSE,          /* 거짓                                  */
    TOK_NULL,           /* 없음                                  */
    TOK_BIN,            /* 2진 리터럴         0b1010             */
    TOK_OCT,            /* 8진 리터럴         0o17               */
    TOK_HEX,            /* 16진 리터럴        0xFF               */

    /* ── 식별자 ──────────────────────────────────────── */
    TOK_IDENT,

    /* ── 자료형 키워드 ───────────────────────────────── */
    TOK_KW_JEONGSU,     /* 정수  */
    TOK_KW_SILSU,       /* 실수  */
    TOK_KW_GEULJA,      /* 문자  */
    TOK_KW_MUNJA,       /* 글자  */
    TOK_KW_NOLI,        /* 논리  */
    TOK_KW_EOPSEUM,     /* 없음  */
    TOK_KW_HAENGNYEOL,  /* 행렬      */
    TOK_KW_TENSOR,      /* 텐서      (v11.0.0) */
    TOK_KW_ZERO_TENSOR, /* 영텐서    (v11.0.0) */
    TOK_KW_ONE_TENSOR,  /* 일텐서    (v11.0.0) */
    TOK_KW_RAND_TENSOR, /* 무작위텐서(v11.0.0) */
    TOK_KW_SAJIN,       /* 사진  */
    TOK_KW_GEURIM,      /* 그림  */
    TOK_KW_HAMSUFORM,   /* 함수형 */
    TOK_KW_GOJUNG,      /* 고정  */

    /* ── 제어문 키워드 ───────────────────────────────── */
    TOK_KW_MANYAK,      /* 만약   if              */
    TOK_KW_ANIMYEON,    /* 아니면 else/elif       */
    TOK_KW_SEONTAEK,    /* 선택   switch          */
    TOK_KW_GYEONGWOO,   /* 경우   case            */
    TOK_KW_GEUWOI,      /* 그외   default         */
    TOK_KW_BANBOG,      /* 반복   for range       */
    TOK_KW_DONGAN,      /* 동안   while           */
    TOK_KW_GAKGAK,      /* 각각   foreach         */
    TOK_KW_ANE,         /* 안에   in              */
    TOK_KW_BUTEO,       /* 부터   from            */
    TOK_KW_KKAJI,       /* 까지   to              */
    TOK_KW_MEOMCHUM,    /* 멈춤   break           */
    TOK_KW_GEONNEO,     /* 건너뜀 continue        */
    TOK_KW_IDONG,       /* 이동   goto            */

    /* ── 함수/정의 키워드 ───────────────────────────── */
    TOK_KW_HAMSU,       /* 함수   function        */
    TOK_KW_JEONGUI,     /* 정의   void function   */
    TOK_KW_BANHWAN,     /* 반환   return value    */
    TOK_KW_KKEUTNAEM,   /* 끝냄   return;         */

    /* ── 자료구조 키워드 ─────────────────────────────── */
    TOK_KW_BAELYEOL,    /* 배열     array         */
    TOK_KW_SAJEON,      /* 사전     dictionary    */
    TOK_KW_MOGLOG,      /* 목록     struct        */
    TOK_KW_GAEGCHE,     /* 객체     class         */
    TOK_KW_YEOLGEEO,    /* 열거     enum          */
    TOK_KW_TEL,         /* 틀       interface     */
    TOK_KW_IEOBATGI,    /* 이어받기 extends       */
    TOK_KW_JASIN,       /* 자신     this/self     */
    TOK_KW_BUMO,        /* 부모     super/parent  */
    TOK_KW_SAENGSEONG,  /* 생성     constructor   */

    /* ── 예외 처리 키워드 ───────────────────────────── */
    TOK_KW_SIDO,        /* 시도   try             */
    TOK_KW_SILPAESI,    /* 실패시 catch           */
    TOK_KW_HANGSANG,    /* 항상   finally         */
    TOK_KW_ORU,         /* 오류   throw/raise     */

    /* ── 모듈/입출력 키워드 ─────────────────────────── */
    TOK_KW_GAJIM,       /* 가짐     import        */
    TOK_KW_ROBUTEO,     /* 로부터   from          */
    TOK_KW_CHULRYEOK,   /* 출력     print \n      */
    TOK_KW_CHULNO,      /* 출력없이 print no \n   */
    TOK_KW_IBRYEOK,     /* 입력     input         */
    TOK_KW_FILEOPEN,    /* 파일열기 file open      */

    /* ── 이미지/렌더링/AI 키워드 ─────────────────────── */
    TOK_KW_HWAMYEON,    /* 화면     2D window     */
    TOK_KW_HWAMYEON3D,  /* 화면3D   3D window     */
    TOK_KW_AI_CONNECT,  /* AI연결   LLM connect   */
    TOK_KW_GPU_RO,      /* 가속기    GPU/NPU block    */
    TOK_KW_KKUT_GPU,    /* 가속기끝  GPU/NPU block end */

    /* ── 가속기 내장 연산 (v23.0.0 — 재설계 v2.0 등록) ─── */
    TOK_KW_ACCEL_MATMUL,    /* 행렬곱  GEMM (A×B)               */
    TOK_KW_ACCEL_MATADD,    /* 행렬합  Element-wise Add         */
    TOK_KW_ACCEL_CONV,      /* 합성곱  Convolution (Conv2D)     */
    TOK_KW_ACCEL_ACTIVATE,  /* 활성화  ReLU / Activation        */
    TOK_KW_ACCEL_TRANSPOSE, /* 전치    Transpose                 */
    TOK_KW_SAJIN_OPEN,  /* 사진열기 image open    */
    TOK_KW_GEURIM_MAKE, /* 그림만들기 canvas make */

    /* ── 스크립트 블록 키워드 ──────────────────────────── */
    TOK_KW_PYTHON,      /* 파이썬         Python 블록 시작  */
    TOK_KW_END_PYTHON,  /* 파이썬끝       Python 블록 종료  */
    TOK_KW_JAVA,        /* 자바           Java 블록 시작    */
    TOK_KW_END_JAVA,    /* 자바끝         Java 블록 종료    */
    TOK_KW_JS,          /* 자바스크립트   JS 블록 시작      */
    TOK_KW_END_JS,      /* 자바스크립트끝 JS 블록 종료      */

    /* ── 계약 시스템 (법령/법위반) ───────────────────────── */
    TOK_KW_BEOPRYEONG,  /* 법령    사전조건 선언              */
    TOK_KW_BEOPWIBAN,   /* 법위반  사후조건 선언              */
    TOK_KW_BOKWON,      /* 복원지점 회귀 기준점 저장           */
    TOK_KW_JEONYEOK,    /* 전역    모든 함수에 적용 (레거시)   */
    TOK_KW_HOEGWI,      /* 회귀    복원지점으로 상태 복원      */
    TOK_KW_DAECHE,      /* 대체    대체값/함수로 교체          */
    TOK_KW_GYEONGGO,    /* 경고    경고 출력 후 계속          */
    TOK_KW_BOGO,        /* 보고    로그 기록 후 계속          */
    TOK_KW_JUNGDAN,     /* 중단    오류 발생 후 실행 중단      */

    /* ── 계약 시스템 v4.2.0 — 계층 구조 ─────────────────── */
    TOK_KW_HEONBEOB,         /* 헌법     전역 최상위 계약 (단일 라인)  */
    TOK_KW_BEOMNYUL,         /* 법률     현재 파일 계약 (단일 라인)    */
    TOK_KW_GYUJEONG,         /* 규정     객체 범위 계약 블록 시작      */
    TOK_KW_KKUT_GYUJEONG,    /* 규정끝   규정 블록 종료               */
    TOK_KW_KKUT_BEOPRYEONG,  /* 법령끝   법령 블록 종료               */
    TOK_KW_KKUT_BEOPWIBAN,   /* 법위반끝 법위반 블록 종료             */
    TOK_KW_JOHANG,           /* 조항     블록 내 조건 선언            */

    /* ── 파일 내장 함수 ──────────────────────────────────── */
    TOK_KW_FILE_CLOSE,       /* 파일닫기    fclose 래핑              */
    TOK_KW_FILE_READALL,     /* 파일전체읽기 열기+읽기+닫기           */
    TOK_KW_FILE_WRITEALL,    /* 파일전체쓰기 열기+쓰기+닫기           */
    TOK_KW_FILE_READLINE,    /* 파일줄읽기  fgets 래핑               */
    TOK_KW_FILE_WRITELINE,   /* 파일줄쓰기  fputs+개행 래핑          */
    TOK_KW_FILE_READ,        /* 파일읽기    전체 fread 래핑           */
    TOK_KW_FILE_WRITE,       /* 파일쓰기    fputs 래핑               */
    TOK_KW_FILE_EXISTS,      /* 파일있음    access() 래핑            */
    TOK_KW_FILE_SIZE,        /* 파일크기    stat() 래핑              */
    TOK_KW_FILE_LIST,        /* 파일목록    opendir() 래핑           */
    TOK_KW_FILE_NAME,        /* 파일이름    basename() 래핑          */
    TOK_KW_FILE_EXT,         /* 파일확장자  확장자 추출              */
    TOK_KW_DIR_MAKE,         /* 폴더만들기  mkdir() 래핑             */
    TOK_KW_FILE_DELETE,      /* 파일지우기  remove() 래핑            */
    TOK_KW_FILE_COPY,        /* 파일복사    파일 복사                */
    TOK_KW_FILE_MOVE,        /* 파일이동    rename() 래핑            */

    /* ── A: OS 시그널 시스템 (v6.0.0) ──────────────────────────── */
    TOK_KW_SINHOBATGI,       /* 신호받기    signal(sig, handler)     */
    TOK_KW_KKUT_SINHOBATGI,  /* 신호받기끝  블록 종료                */
    TOK_KW_SINHOMUSI,        /* 신호무시    signal(sig, SIG_IGN)     */
    TOK_KW_SINHOGIBON,       /* 신호기본    signal(sig, SIG_DFL)     */
    TOK_KW_SINHOBONEGI,      /* 신호보내기  kill(pid, sig)           */
    /* 신호 이름 상수 */
    TOK_KW_SIG_INT,          /* 중단신호    SIGINT  (Ctrl+C)         */
    TOK_KW_SIG_TERM,         /* 종료신호    SIGTERM                  */
    TOK_KW_SIG_KILL,         /* 강제종료    SIGKILL                  */
    TOK_KW_SIG_CHLD,         /* 자식신호    SIGCHLD                  */
    TOK_KW_SIG_USR1,         /* 사용자신호1 SIGUSR1                  */
    TOK_KW_SIG_USR2,         /* 사용자신호2 SIGUSR2                  */
    TOK_KW_SIG_PIPE,         /* 연결신호    SIGPIPE                  */
    TOK_KW_SIG_ALRM,         /* 경보신호    SIGALRM                  */
    TOK_KW_SIG_STOP,         /* 중지신호    SIGSTOP                  */
    TOK_KW_SIG_CONT,         /* 재개신호    SIGCONT                  */

    /* ── B: 하드웨어 간섭(ISR) 시스템 (v6.0.0) ─────────────────── */
    TOK_KW_GANSEOB,          /* 간섭        ISR 블록 등록            */
    TOK_KW_KKUT_GANSEOB,     /* 간섭끝      ISR 블록 종료            */
    TOK_KW_GANSEOB_JAMGEUM,  /* 간섭잠금    cli() / noInterrupts()   */
    TOK_KW_GANSEOB_HEOYONG,  /* 간섭허용    sei() / interrupts()     */
    /* 인터럽트 벡터 이름 상수 */
    TOK_KW_IRQ_TIMER0,       /* 시간0넘침   TIMER0_OVF_vect          */
    TOK_KW_IRQ_TIMER1,       /* 시간1넘침   TIMER1_OVF_vect          */
    TOK_KW_IRQ_EXT0_RISE,    /* 외부0상승   INT0_vect (상승 엣지)    */
    TOK_KW_IRQ_EXT0_FALL,    /* 외부0하강   INT0_vect (하강 엣지)    */
    TOK_KW_IRQ_UART_RX,      /* 직렬수신    USART_RX_vect            */

    /* ── C: 행사(이벤트 루프) 시스템 (v6.0.0) ──────────────────── */
    TOK_KW_HAENGSA_REG,      /* 행사등록    이벤트 핸들러 등록       */
    TOK_KW_KKUT_HAENGSA,     /* 행사등록끝  핸들러 블록 종료         */
    TOK_KW_HAENGSA_START,    /* 행사시작    이벤트 루프 진입(blocking)*/
    TOK_KW_HAENGSA_STOP,     /* 행사중단    이벤트 루프 종료         */
    TOK_KW_HAENGSA_EMIT,     /* 행사발생    이벤트 수동 발생         */
    TOK_KW_HAENGSA_OFF,      /* 행사해제    핸들러 제거              */

    /* ── MCP 시스템 (v14.0.1) ──────────────────────────────────── */
    TOK_KW_MCP_SERVER,       /* MCP서버      MCP 서버 블록 시작      */
    TOK_KW_KKUT_MCP_SERVER,  /* MCP서버끝    MCP 서버 블록 종료      */
    TOK_KW_MCP_TOOL,         /* MCP도구      MCP 도구 핸들러 시작    */
    TOK_KW_KKUT_MCP_TOOL,    /* MCP도구끝    MCP 도구 핸들러 종료    */
    TOK_KW_MCP_RESOURCE,     /* MCP자원      MCP 자원 핸들러 시작    */
    TOK_KW_KKUT_MCP_RESOURCE,/* MCP자원끝    MCP 자원 핸들러 종료    */
    TOK_KW_MCP_PROMPT,       /* MCP프롬프트  MCP 프롬프트 핸들러 시작*/
    TOK_KW_KKUT_MCP_PROMPT,  /* MCP프롬프트끝MCP 프롬프트 핸들러 종료*/

    /* ── 산업/임베디드 (v16.0.0) ──────────────────────── */
    TOK_KW_TIMER_BLOCK,      /* 타이머       실시간 타이머 블록 시작 */
    TOK_KW_TIMER_KKUT,       /* 타이머끝     타이머 블록 종료        */
    TOK_KW_GPIO_WRITE,       /* GPIO쓰기     디지털 핀 출력          */
    TOK_KW_GPIO_READ,        /* GPIO읽기     디지털 핀 입력          */
    TOK_KW_I2C_CONNECT,      /* I2C연결      I2C 버스 연결           */
    TOK_KW_I2C_READ,         /* I2C읽기      I2C 데이터 읽기         */
    TOK_KW_I2C_WRITE,        /* I2C쓰기      I2C 데이터 쓰기         */
    TOK_KW_SPI_SEND,         /* SPI전송      SPI 데이터 전송         */
    TOK_KW_SPI_READ,         /* SPI읽기      SPI 데이터 읽기         */
    TOK_KW_UART_SETUP,       /* UART설정     UART 초기화             */
    TOK_KW_UART_SEND,        /* UART전송     UART 데이터 전송        */
    TOK_KW_UART_READ,        /* UART읽기     UART 데이터 읽기        */
    TOK_KW_MODBUS_CONNECT,   /* Modbus연결   Modbus TCP/RTU 연결     */
    TOK_KW_MODBUS_READ,      /* Modbus읽기   레지스터 읽기           */
    TOK_KW_MODBUS_WRITE,     /* Modbus쓰기   레지스터 쓰기           */
    TOK_KW_MODBUS_DISCONNECT,/* Modbus연결끊기 연결 해제             */
    TOK_KW_CAN_FILTER,       /* CAN필터      CAN 메시지 필터         */
    TOK_KW_CAN_SEND,         /* CAN전송      CAN 메시지 전송         */
    TOK_KW_CAN_READ,         /* CAN읽기      CAN 메시지 수신         */
    TOK_KW_MQTT_CONNECT,     /* MQTT연결     브로커 연결             */
    TOK_KW_MQTT_PUBLISH,     /* MQTT발행     토픽 발행               */
    TOK_KW_MQTT_SUBSCRIBE,   /* MQTT구독     토픽 구독               */
    TOK_KW_MQTT_DISCONNECT,  /* MQTT연결끊기 브로커 연결 해제        */
    TOK_KW_ROS2_NODE,        /* ROS2노드     ROS2 노드 블록 시작     */
    TOK_KW_ROS2_END,         /* ROS2끝       ROS2 노드 블록 종료     */
    TOK_KW_ROS2_PUBLISH,     /* ROS2발행     토픽 발행               */
    TOK_KW_ROS2_SUBSCRIBE,   /* ROS2구독     토픽 구독               */

    /* ── 안전 규격 (v17.0.0) ────────────────────────── */
    TOK_KW_SIL,              /* SIL          안전 등급 선언 (SIL=N) */
    TOK_KW_WATCHDOG,         /* 워치독       워치독 타이머 블록 시작 */
    TOK_KW_WATCHDOG_END,     /* 워치독끝     워치독 타이머 블록 종료 */
    TOK_KW_FAULT_TOL,        /* 결함허용     결함허용(N중) 블록 시작 */
    TOK_KW_FAULT_TOL_END,    /* 결함허용끝   결함허용 블록 종료      */
    TOK_KW_FAILSAFE,         /* 페일세이프   페일세이프 내장함수     */
    TOK_KW_EMERG_STOP,       /* 긴급정지     긴급정지 내장함수       */

    /* ── 온디바이스 AI (v18.0.0) ────────────────────── */
    TOK_KW_AI_MODEL,         /* AI모델       AI 모델 블록 시작       */
    TOK_KW_AI_MODEL_END,     /* AI모델끝     AI 모델 블록 종료       */
    TOK_KW_TINYML,           /* TinyML       TinyML 경량 모델 블록   */
    TOK_KW_TINYML_END,       /* TinyML끝     TinyML 블록 종료        */
    TOK_KW_AI_LOAD,          /* AI불러오기   ONNX/TinyML 모델 로드   */
    TOK_KW_AI_PREDICT,       /* AI추론       모델 추론 실행          */
    TOK_KW_AI_TRAIN_STEP,    /* AI학습단계   온디바이스 1스텝 학습   */
    TOK_KW_AI_SAVE,          /* AI저장       모델 가중치 저장        */
    TOK_KW_FEDERATED,        /* 연합학습     연합학습 블록 시작       */
    TOK_KW_FEDERATED_END,    /* 연합학습끝   연합학습 블록 종료       */

    /* ── 온톨로지 시스템 (v18.5.0 4회차) ────────────── */
    TOK_KW_ONTOLOGY,         /* 온톨로지     온톨로지 블록 시작       */
    TOK_KW_ONTOLOGY_END,     /* 온톨로지끝   온톨로지 블록 종료       */
    TOK_KW_ONT_CONCEPT,      /* 개념         클래스(개념) 정의 시작   */
    TOK_KW_ONT_CONCEPT_END,  /* 개념끝       클래스 정의 종료         */
    TOK_KW_ONT_PROP,         /* 속성         클래스 속성 선언         */
    TOK_KW_ONT_RELATE,       /* 관계         관계 타입 정의           */
    TOK_KW_ONT_QUERY_BLOCK,  /* 질의         온톨로지 질의 실행       */
    TOK_KW_ONT_INFER_BLOCK,  /* 추론         추론기 실행 블록         */
    TOK_KW_ONT_SENSITIVE,    /* 민감         속성 민감 지정           */
    TOK_KW_ONT_ANON,         /* 익명화       속성 익명화 지정         */

    /* ── Concept Identity / Vector Space (v22.0.0) ─── */
    TOK_KW_SEMANTIC_INFER,      /* 의미추론     의미 추론 블록 시작  */
    TOK_KW_SEMANTIC_INFER_END,  /* 의미추론끝   의미 추론 블록 종료  */
    TOK_KW_VECTORIZE,           /* 벡터화       벡터화 블록 시작     */
    TOK_KW_VECTORIZE_END,       /* 벡터화끝     벡터화 블록 종료     */
    TOK_KW_SEM_RECON,           /* 의미복원     의미 복원 블록 시작  */
    TOK_KW_SEM_RECON_END,       /* 의미복원끝   의미 복원 블록 종료  */
    TOK_KW_REPRO_LABEL,         /* 재생산라벨   재생산 라벨 블록 시작*/
    TOK_KW_REPRO_LABEL_END,     /* 재생산라벨끝 재생산 라벨 블록 종료*/

    /* ── 지식 뱅크 (Stage 24) ─────────────────────────── */
    TOK_KW_KBANK,               /* 지식뱅크       지식 뱅크 블록 선언  */
    TOK_KW_KBANK_LOAD,          /* 지식불러오기   뱅크 로드 블록       */
    TOK_KW_KBANK_COMPARE,       /* 지식비교       두 뱅크 비교 블록    */
    TOK_KW_REPRO_LABEL_DECL,    /* 재생산라벨선언 라벨 인라인 선언     */
    TOK_KW_KBANK_PROOF,         /* 지식증거출력   소유 증거 출력 블록  */

    /* ── 전처리기 ───────────────────────────────────── */
    TOK_PP_DEFINE,      /* #정의       */
    TOK_PP_IF,          /* #만약       */
    TOK_PP_ELSE,        /* #아니면     */
    TOK_PP_ENDIF,       /* #끝         */
    TOK_PP_IFDEF,       /* #만약있으면 */
    TOK_PP_IFNDEF,      /* #만약없으면 */
    TOK_PP_INCLUDE,     /* #포함       */
    TOK_PP_GPU,         /* #GPU사용    */

    /* ── 산술 연산자 ─────────────────────────────────── */
    TOK_PLUS,           /* +   */
    TOK_MINUS,          /* -   */
    TOK_STAR,           /* *   */
    TOK_SLASH,          /* /   */
    TOK_PERCENT,        /* %   */
    TOK_STARSTAR,       /* **  */

    /* ── 비교 연산자 ─────────────────────────────────── */
    TOK_EQEQ,           /* ==  */
    TOK_BANGEQ,         /* !=  */
    TOK_GT,             /* >   */
    TOK_LT,             /* <   */
    TOK_GTEQ,           /* >=  */
    TOK_LTEQ,           /* <=  */

    /* ── 논리 연산자 (한글) ──────────────────────────── */
    TOK_KW_AND,         /* 그리고  && */
    TOK_KW_OR,          /* 또는    || */
    TOK_KW_NOT,         /* 아니다  !  */

    /* ── 비트 연산자 ─────────────────────────────────── */
    TOK_AMP,            /* &   */
    TOK_PIPE,           /* |   */
    TOK_CARET,          /* ^   */
    TOK_TILDE,          /* ~   */
    TOK_LTLT,           /* <<  */
    TOK_GTGT,           /* >>  */

    /* ── 대입 연산자 ─────────────────────────────────── */
    TOK_EQ,             /* =   */
    TOK_PLUSEQ,         /* +=  */
    TOK_MINUSEQ,        /* -=  */
    TOK_STAREQ,         /* *=  */
    TOK_SLASHEQ,        /* /=  */
    TOK_PERCENTEQ,      /* %=  */

    /* ── 특수 기호 ───────────────────────────────────── */
    TOK_ARROW,          /* =>  람다 화살표   */
    TOK_DOTS,           /* ... 가변 매개변수 */
    TOK_LPAREN,         /* (   */
    TOK_RPAREN,         /* )   */
    TOK_LBRACKET,       /* [   */
    TOK_RBRACKET,       /* ]   */
    TOK_LBRACE,         /* {   */
    TOK_RBRACE,         /* }   */
    TOK_COMMA,          /* ,   */
    TOK_COLON,          /* :   */
    TOK_DOT,            /* .   */

    /* ── 들여쓰기 ───────────────────────────────────── */
    TOK_NEWLINE,        /* 의미 있는 줄 끝 */
    TOK_INDENT,         /* 들여쓰기 증가   */
    TOK_DEDENT,         /* 들여쓰기 감소   */

    /* ── 특수 ───────────────────────────────────────── */
    TOK_EOF,
    TOK_ERROR,

    TOK_COUNT

} TokenType;

/* ================================================================
 *  토큰 구조체
 * ================================================================ */
typedef struct {
    TokenType   type;
    const char *start;      /* 소스 내 시작 포인터 (UTF-8)      */
    size_t      length;     /* 바이트 길이                      */
    int         line;       /* 줄 번호 (1부터)                  */
    int         col;        /* 열 번호 (1부터, UTF-8 문자 단위) */

    union {
        int64_t  ival;      /* TOK_INT, TOK_BIN, TOK_OCT, TOK_HEX */
        double   fval;      /* TOK_FLOAT                        */
        uint32_t cval;      /* TOK_CHAR_LIT (유니코드 코드포인트) */
    } val;
} Token;

/* ================================================================
 *  렉서 상태 구조체
 * ================================================================ */
#define KCODE_MAX_INDENT_DEPTH  128
#define KCODE_MAX_PEEK           8

typedef struct {
    const char *src;
    size_t      src_len;
    size_t      pos;            /* 현재 읽기 위치 (바이트)      */
    int         line;
    int         col;            /* 현재 열 (UTF-8 문자 수 기준) */
    int         line_start_pos; /* 현재 줄 시작 바이트 위치     */

    /* 들여쓰기 스택 (공백 수 저장) */
    int indent_stack[KCODE_MAX_INDENT_DEPTH];
    int indent_top;

    /* 한 줄에서 연속으로 발행해야 할 DEDENT 수 */
    int pending_dedents;

    /* 줄 시작 여부 — INDENT/DEDENT 계산 트리거 */
    int at_line_start;

    /* 오류 */
    char error_msg[512];
    int  had_error;
} Lexer;

/* ================================================================
 *  공개 API
 * ================================================================ */

/* 렉서 초기화. src는 NULL 종료 UTF-8 문자열.                   */
void  lexer_init(Lexer *lx, const char *src, size_t len);

/* 다음 토큰을 소비하고 반환한다.                               */
Token lexer_next(Lexer *lx);

/* 토큰을 소비하지 않고 미리 본다.                             */
Token lexer_peek(Lexer *lx);

/* 토큰 이름 문자열 반환 (디버깅용)                            */
const char *token_type_name(TokenType t);

/* 토큰의 원본 텍스트를 buf에 복사 (null 종료)                 */
void  token_to_str(const Token *tok, char *buf, size_t buf_size);

/*
 * lexer_read_raw_script() — 스크립트 블록 원문 읽기
 *
 * 현재 렉서 위치부터 end_keyword(UTF-8)가 줄 시작에 나타날 때까지
 * 원문 텍스트를 그대로 반환한다. (토큰화 없이 raw read)
 * 반환값은 malloc 할당 — 호출자가 free 해야 한다.
 * end_keyword 자체는 소비하지 않는다.
 */
char *lexer_read_raw_script(Lexer *lx, const char *end_keyword);

#endif /* KCODE_LEXER_H */
