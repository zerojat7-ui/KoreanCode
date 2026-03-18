/*
 * kc_bytecode.h  —  Kcode 바이트코드 명령어 정의 + .kbc 파일 포맷
 * version : v16.0.0
 *
 * v16.0.0 변경:
 *   - KcBuiltinID 37 → 102종 확장
 *   - 기존 이름 불일치 정정 (최대/최소/배열정렬/배열뒤집기/합치기/자르기/분할/대체)
 *   - 논리(BUILTIN_BOOL) 형변환 추가
 *   - 자연로그/MSE/교차엔트로피/소프트맥스/위치인코딩/수열 함수 추가
 *   - 통계 9종 / 글자 14종 / 파일 17종 / 텐서 13종 / 자동미분 2종 / MCP 1종 추가
 *
 * v15.0.0 신규:
 *   - KcOpCode  : KVM 명령어 집합 (스택 기반)
 *   - KcInstr   : 명령어 구조체 (opcode + 피연산자)
 *   - KcChunk   : 함수/프로그램 단위 바이트코드 청크
 *   - KcModule  : .kbc 파일 단위 모듈 (복수 청크)
 *   - .kbc 파일 포맷 매직 / 버전 상수
 *
 * MIT License
 * zerojat7
 */

#ifndef KC_BYTECODE_H
#define KC_BYTECODE_H

#include <stdint.h>
#include <stddef.h>

/* ================================================================
 *  .kbc 파일 포맷 상수
 * ================================================================ */
#define KBC_MAGIC        0x4B424300u   /* "KBC\0" */
#define KBC_VERSION_MAJ  15
#define KBC_VERSION_MIN  0
#define KBC_MAX_CONSTS   65536
#define KBC_MAX_LOCALS   256
#define KBC_MAX_UPVALS   64
#define KBC_STACK_MAX    1024

/* ================================================================
 *  KVM 명령어 집합 (KcOpCode)
 * ================================================================ */
typedef enum {
    /* ── 스택 조작 ───────────────────────────────────────────── */
    OP_NOP        = 0x00, /* 아무것도 하지 않음                  */
    OP_PUSH_INT   = 0x01, /* 정수 상수 스택에 push  (arg=ival)   */
    OP_PUSH_FLOAT = 0x02, /* 실수 상수 스택에 push  (arg=const)  */
    OP_PUSH_STR   = 0x03, /* 문자열 상수 push       (arg=const)  */
    OP_PUSH_BOOL  = 0x04, /* 논리값 push            (arg=0/1)    */
    OP_PUSH_NULL  = 0x05, /* 없음(null) push                      */
    OP_POP        = 0x06, /* 스택 top 버림                        */
    OP_DUP        = 0x07, /* 스택 top 복제                        */
    OP_SWAP       = 0x08, /* 스택 top 2개 교환                    */

    /* ── 변수 로드/저장 ─────────────────────────────────────── */
    OP_LOAD_LOCAL  = 0x10, /* 지역 변수 로드  (arg=슬롯번호)      */
    OP_STORE_LOCAL = 0x11, /* 지역 변수 저장  (arg=슬롯번호)      */
    OP_LOAD_GLOBAL = 0x12, /* 전역 변수 로드  (arg=이름 const)    */
    OP_STORE_GLOBAL= 0x13, /* 전역 변수 저장  (arg=이름 const)    */
    OP_LOAD_CONST  = 0x14, /* 상수 풀 값 로드 (arg=const idx)     */
    OP_LOAD_UPVAL  = 0x15, /* 업밸류 로드     (arg=upval idx)     */
    OP_STORE_UPVAL = 0x16, /* 업밸류 저장     (arg=upval idx)     */

    /* ── 산술 연산 ───────────────────────────────────────────── */
    OP_ADD = 0x20,         /* top-1 + top                         */
    OP_SUB = 0x21,         /* top-1 - top                         */
    OP_MUL = 0x22,         /* top-1 * top                         */
    OP_DIV = 0x23,         /* top-1 / top                         */
    OP_MOD = 0x24,         /* top-1 % top                         */
    OP_POW = 0x25,         /* top-1 ** top                        */
    OP_NEG = 0x26,         /* -top (단항)                         */
    OP_MATMUL = 0x27,      /* top-1 @ top (행렬곱)                */

    /* ── 비트 연산 ───────────────────────────────────────────── */
    OP_BIT_AND = 0x28,
    OP_BIT_OR  = 0x29,
    OP_BIT_XOR = 0x2A,
    OP_BIT_NOT = 0x2B,
    OP_SHL     = 0x2C,
    OP_SHR     = 0x2D,

    /* ── 비교 연산 (결과: 논리값) ───────────────────────────── */
    OP_EQ  = 0x30,         /* top-1 == top                        */
    OP_NE  = 0x31,         /* top-1 != top                        */
    OP_LT  = 0x32,         /* top-1 <  top                        */
    OP_LE  = 0x33,         /* top-1 <= top                        */
    OP_GT  = 0x34,         /* top-1 >  top                        */
    OP_GE  = 0x35,         /* top-1 >= top                        */

    /* ── 논리 연산 ───────────────────────────────────────────── */
    OP_AND = 0x38,         /* top-1 && top (단락 평가 X, 런타임)  */
    OP_OR  = 0x39,
    OP_NOT = 0x3A,

    /* ── 제어 흐름 ───────────────────────────────────────────── */
    OP_JUMP         = 0x40, /* 무조건 점프  (arg=절대 IP)         */
    OP_JUMP_FALSE   = 0x41, /* top=거짓이면 점프                  */
    OP_JUMP_TRUE    = 0x42, /* top=참이면 점프                    */
    OP_JUMP_BACK    = 0x43, /* 뒤로 점프 (루프용)  (arg=절대 IP)  */

    /* ── 함수 호출/반환 ─────────────────────────────────────── */
    OP_CALL        = 0x50, /* 함수 호출 (arg=인수 개수)           */
    OP_CALL_BUILTIN= 0x51, /* 내장 함수 호출 (arg=함수ID)         */
    OP_RETURN      = 0x52, /* 함수 반환 (stack top = 반환값)      */
    OP_RETURN_VOID = 0x53, /* 반환값 없이 반환                    */
    OP_TAIL_CALL   = 0x54, /* 꼬리 호출 최적화                   */

    /* ── 배열 / 사전 ─────────────────────────────────────────── */
    OP_MAKE_ARRAY = 0x60, /* 배열 생성 (arg=원소 개수)           */
    OP_MAKE_DICT  = 0x61, /* 사전 생성 (arg=쌍 개수)             */
    OP_ARRAY_GET  = 0x62, /* 배열[인덱스] 로드                    */
    OP_ARRAY_SET  = 0x63, /* 배열[인덱스] = 값                    */
    OP_ARRAY_LEN  = 0x64, /* 배열 길이                           */
    OP_ARRAY_PUSH = 0x65, /* 배열 끝에 원소 추가                 */

    /* ── 객체(클래스) ────────────────────────────────────────── */
    OP_NEW_OBJ    = 0x70, /* 객체 생성 (arg=클래스 const)        */
    OP_GET_FIELD  = 0x71, /* 객체.필드 (arg=필드명 const)        */
    OP_SET_FIELD  = 0x72, /* 객체.필드 = 값                      */
    OP_GET_METHOD = 0x73, /* 메서드 바인딩                       */

    /* ── 클로저 / 람다 ───────────────────────────────────────── */
    OP_MAKE_CLOSURE = 0x78, /* 클로저 생성 (arg=함수 청크 idx)   */
    OP_CLOSE_UPVAL  = 0x79, /* 업밸류 닫기                       */

    /* ── 예외 처리 ───────────────────────────────────────────── */
    OP_TRY_BEGIN  = 0x80, /* 예외 처리 블록 시작 (arg=catch IP) */
    OP_TRY_END    = 0x81, /* 예외 처리 블록 종료                 */
    OP_RAISE      = 0x82, /* 예외 발생 (top=메시지)              */

    /* ── 내장 I/O ────────────────────────────────────────────── */
    OP_PRINT      = 0x90, /* 출력 (top 출력 + 개행)              */
    OP_PRINT_RAW  = 0x91, /* 출력없이 (개행 없음)                */
    OP_INPUT      = 0x92, /* 입력 (결과 push)                    */

    /* ── 형변환 ──────────────────────────────────────────────── */
    OP_TO_INT   = 0xA0,
    OP_TO_FLOAT = 0xA1,
    OP_TO_STR   = 0xA2,
    OP_TO_BOOL  = 0xA3,

    /* ── 종료 ────────────────────────────────────────────────── */
    OP_HALT = 0xFF          /* VM 실행 종료                        */

} KcOpCode;

/* ================================================================
 *  명령어 구조체
 * ================================================================ */
typedef struct {
    uint8_t  op;           /* KcOpCode 값 (1 바이트)              */
    int32_t  arg;          /* 피연산자 (정수/인덱스/오프셋)        */
    uint32_t line;         /* 소스 줄 번호 (디버그용)              */
} KcInstr;

/* ================================================================
 *  상수 풀 값 종류
 * ================================================================ */
typedef enum {
    KC_CONST_INT    = 0,
    KC_CONST_FLOAT  = 1,
    KC_CONST_STR    = 2,
    KC_CONST_BOOL   = 3,
    KC_CONST_NULL   = 4,
    KC_CONST_FUNC   = 5    /* 중첩 함수 청크 참조 */
} KcConstType;

/* ================================================================
 *  상수 풀 항목
 * ================================================================ */
typedef struct KcConst {
    KcConstType type;
    union {
        int64_t  ival;     /* KC_CONST_INT  */
        double   fval;     /* KC_CONST_FLOAT */
        char    *sval;     /* KC_CONST_STR  (strdup 할당) */
        int      bval;     /* KC_CONST_BOOL */
        int      func_idx; /* KC_CONST_FUNC — 청크 인덱스 */
    } u;
} KcConst;

/* ================================================================
 *  업밸류 기술자 (클로저 캡처 정보)
 * ================================================================ */
typedef struct {
    uint8_t is_local;      /* 1=직접 로컬, 0=상위 업밸류 참조    */
    uint8_t index;         /* 로컬 슬롯 또는 상위 업밸류 인덱스  */
} KcUpvalDesc;

/* ================================================================
 *  청크 (함수/프로그램 단위 바이트코드)
 * ================================================================ */
typedef struct KcChunk {
    char     *name;            /* 함수 이름 ("__main__" = 최상위)  */
    int       arity;           /* 매개변수 개수                    */
    int       local_count;     /* 지역 변수 슬롯 수                */
    int       upval_count;     /* 업밸류 수                        */

    /* 명령어 배열 */
    KcInstr  *code;
    int       code_len;
    int       code_cap;

    /* 상수 풀 */
    KcConst  *consts;
    int       const_len;
    int       const_cap;

    /* 지역 변수 이름 (디버그용) */
    char    **local_names;

    /* 업밸류 기술자 */
    KcUpvalDesc *upvals;

    /* 중첩 청크 (내부 함수 등) */
    struct KcChunk **sub_chunks;
    int              sub_count;
    int              sub_cap;
} KcChunk;

/* ================================================================
 *  모듈 (.kbc 파일 루트)
 * ================================================================ */
typedef struct {
    uint32_t  magic;           /* KBC_MAGIC                        */
    uint16_t  ver_maj;
    uint16_t  ver_min;
    char     *source_name;     /* 원본 .han 파일명                 */
    KcChunk  *main_chunk;      /* 최상위 청크                      */
} KcModule;

/* ================================================================
 *  공개 API — 청크
 * ================================================================ */
KcChunk  *kc_chunk_new(const char *name, int arity);
void      kc_chunk_free(KcChunk *chunk);

int       kc_chunk_emit(KcChunk *chunk, uint8_t op, int32_t arg, uint32_t line);
int       kc_chunk_emit_nop(KcChunk *chunk);    /* 패치용 placeholder */
void      kc_chunk_patch(KcChunk *chunk, int idx, int32_t new_arg);

int       kc_chunk_add_const_int  (KcChunk *chunk, int64_t ival);
int       kc_chunk_add_const_float(KcChunk *chunk, double  fval);
int       kc_chunk_add_const_str  (KcChunk *chunk, const char *s);
int       kc_chunk_add_const_bool (KcChunk *chunk, int bval);
int       kc_chunk_add_const_null (KcChunk *chunk);
int       kc_chunk_add_sub        (KcChunk *chunk, KcChunk *sub);

void      kc_chunk_dump(const KcChunk *chunk);  /* 디스어셈블러     */

/* ================================================================
 *  공개 API — 모듈 (직렬화/역직렬화)
 * ================================================================ */
KcModule *kc_module_new(const char *source_name, KcChunk *main);
void      kc_module_free(KcModule *mod);
int       kc_module_write(const KcModule *mod, const char *path); /* → .kbc */
KcModule *kc_module_read(const char *path);                        /* .kbc 로드 */

/* 내장 함수 ID (OP_CALL_BUILTIN arg)
 * v16.0.0: 37 → 107종 확장
 *   ─ 기존 17종 이름 불일치 정정 (최대/최소/배열정렬 등)
 *   ─ 논리(BOOL) 형변환 추가
 *   ─ 수학/AI 추가 (자연로그/MSE/소프트맥스 등)
 *   ─ 통계 9종 추가
 *   ─ 글자 함수 14종 추가 (기존 kc_bcgen 오기 함수 포함)
 *   ─ 파일 17종 추가
 *   ─ 텐서 13종 추가
 *   ─ 자동미분 2종 + MCP 1종 추가
 */
typedef enum {
    /* ── 기본 I/O ────────────────────────────────────────────── */
    BUILTIN_PRINT        = 0,   /* 출력       */
    BUILTIN_PRINT_RAW    = 1,   /* 출력없이   */
    BUILTIN_INPUT        = 2,   /* 입력       */
    BUILTIN_LEN          = 3,   /* 길이       */
    BUILTIN_RANGE        = 4,   /* 범위       */

    /* ── 형변환 ──────────────────────────────────────────────── */
    BUILTIN_INT          = 5,   /* 정수       */
    BUILTIN_FLOAT        = 6,   /* 실수       */
    BUILTIN_STR          = 7,   /* 글자       */
    BUILTIN_BOOL         = 8,   /* 논리  ★신규 */

    /* ── 수학 기초 ───────────────────────────────────────────── */
    BUILTIN_SQRT         = 9,   /* 제곱근     */
    BUILTIN_ABS          = 10,  /* 절댓값     */
    BUILTIN_MAX          = 11,  /* 최대       */
    BUILTIN_MIN          = 12,  /* 최소       */
    BUILTIN_SIN          = 13,  /* 사인       */
    BUILTIN_COS          = 14,  /* 코사인     */
    BUILTIN_TAN          = 15,  /* 탄젠트     */
    BUILTIN_LOG          = 16,  /* 로그       */
    BUILTIN_LN           = 17,  /* 자연로그 ★신규 */
    BUILTIN_EXP          = 18,  /* 지수       */
    BUILTIN_ROUND        = 19,  /* 반올림     */
    BUILTIN_FLOOR        = 20,  /* 내림       */
    BUILTIN_CEIL         = 21,  /* 올림       */

    /* ── 배열 조작 ───────────────────────────────────────────── */
    BUILTIN_APPEND       = 22,  /* 추가       */
    BUILTIN_ARR_SORT     = 23,  /* 배열정렬   */
    BUILTIN_ARR_REVERSE  = 24,  /* 배열뒤집기 */

    /* ── 글자 함수 (기존 5종 + 14종 추가 = 19종) ─────────────── */
    BUILTIN_UPPER        = 25,  /* 대문자     */
    BUILTIN_LOWER        = 26,  /* 소문자     */
    BUILTIN_CONTAINS     = 27,  /* 포함       */
    BUILTIN_STRIP        = 28,  /* 공백제거   */
    BUILTIN_REPLACE      = 29,  /* 대체       */
    BUILTIN_STRLEN       = 30,  /* 글자길이   */
    BUILTIN_STR_SUB      = 31,  /* 자르기  ★신규 */
    BUILTIN_STR_SPLIT    = 32,  /* 분할    ★신규 */
    BUILTIN_STR_JOIN     = 33,  /* 합치기  ★신규 */
    BUILTIN_STR_REPEAT   = 34,  /* 반복글자 ★신규 */
    BUILTIN_STR_REVERSE  = 35,  /* 역순    ★신규 */
    BUILTIN_STR_INDEXOF  = 36,  /* 위치    ★신규 */
    BUILTIN_STR_STARTSWITH=37,  /* 시작    ★신규 */
    BUILTIN_STR_ENDSWITH = 38,  /* 끝확인  ★신규 */
    BUILTIN_STR_COMPARE  = 39,  /* 비교    ★신규 */
    BUILTIN_STR_TITLE    = 40,  /* 제목식  ★신규 */
    BUILTIN_STR_REPLACE1 = 41,  /* 한번대체 ★신규 */
    BUILTIN_STR_LTRIM    = 42,  /* 앞공백제거 ★신규 */
    BUILTIN_STR_RTRIM    = 43,  /* 뒤공백제거 ★신규 */
    BUILTIN_STR_REGEX    = 44,  /* 반복확인 ★신규 */
    BUILTIN_STR_PARSE    = 45,  /* 분석    ★신규 */
    BUILTIN_STR_FORMAT   = 46,  /* 포맷    ★신규 */

    /* ── AI 활성함수 ─────────────────────────────────────────── */
    BUILTIN_SIGMOID      = 47,  /* 시그모이드  */
    BUILTIN_RELU         = 48,  /* 렐루        */
    BUILTIN_TANH_FN      = 49,  /* 쌍곡탄젠트  */

    /* ── AI/수열 함수 (v3.5.0) ──────────────────────────────── */
    BUILTIN_MSE          = 50,  /* 평균제곱오차    ★신규 */
    BUILTIN_CROSS_ENT    = 51,  /* 교차엔트로피    ★신규 */
    BUILTIN_SOFTMAX      = 52,  /* 소프트맥스      ★신규 */
    BUILTIN_POS_ENC      = 53,  /* 위치인코딩      ★신규 */
    BUILTIN_GEOM_SUM     = 54,  /* 등비수열합      ★신규 */
    BUILTIN_ARITH_SUM    = 55,  /* 등차수열합      ★신규 */
    BUILTIN_RECUR_GEOM   = 56,  /* 점화식값        ★신규 */

    /* ── 통계 함수 (v3.8.0) ──────────────────────────────────── */
    BUILTIN_MEAN         = 57,  /* 평균       */
    BUILTIN_STDEV        = 58,  /* 표준편차   */
    BUILTIN_SUM          = 59,  /* 합계    ★신규 */
    BUILTIN_VARIANCE     = 60,  /* 분산    ★신규 */
    BUILTIN_MEDIAN       = 61,  /* 중앙값  ★신규 */
    BUILTIN_MODE         = 62,  /* 최빈값  ★신규 */
    BUILTIN_CUMSUM       = 63,  /* 누적합  ★신규 */
    BUILTIN_COVARIANCE   = 64,  /* 공분산  ★신규 */
    BUILTIN_CORRELATION  = 65,  /* 상관계수 ★신규 */
    BUILTIN_NORMALIZE    = 66,  /* 정규화  ★신규 */
    BUILTIN_STANDARDIZE  = 67,  /* 표준화  ★신규 */

    /* ── 관계심리 함수 ───────────────────────────────────────── */
    BUILTIN_ATTRACTION   = 68,  /* 호감도  ★신규 */

    /* ── 파일 함수 17종 (v4.2.0) ─────────────────────────────── */
    BUILTIN_FILE_OPEN    = 69,  /* 파일열기        ★신규 */
    BUILTIN_FILE_CLOSE   = 70,  /* 파일닫기        ★신규 */
    BUILTIN_FILE_READ    = 71,  /* 파일읽기        ★신규 */
    BUILTIN_FILE_READLINE= 72,  /* 파일줄읽기      ★신규 */
    BUILTIN_FILE_WRITE   = 73,  /* 파일쓰기        ★신규 */
    BUILTIN_FILE_WRITELN = 74,  /* 파일줄쓰기      ★신규 */
    BUILTIN_FILE_EXISTS  = 75,  /* 파일있음        ★신규 */
    BUILTIN_FILE_SIZE    = 76,  /* 파일크기        ★신규 */
    BUILTIN_FILE_LIST    = 77,  /* 파일목록        ★신규 */
    BUILTIN_FILE_NAME    = 78,  /* 파일이름        ★신규 */
    BUILTIN_FILE_EXT     = 79,  /* 파일확장자      ★신규 */
    BUILTIN_DIR_MAKE     = 80,  /* 폴더만들기      ★신규 */
    BUILTIN_FILE_DELETE  = 81,  /* 파일지우기      ★신규 */
    BUILTIN_FILE_COPY    = 82,  /* 파일복사        ★신규 */
    BUILTIN_FILE_MOVE    = 83,  /* 파일이동        ★신규 */
    BUILTIN_FILE_READALL = 84,  /* 파일전체읽기    ★신규 */
    BUILTIN_FILE_WRITEALL= 85,  /* 파일전체쓰기    ★신규 */

    /* ── 텐서 함수 13종 (v12.0.0) ───────────────────────────── */
    BUILTIN_TENSOR_CREATE   = 86, /* 텐서생성      ★신규 */
    BUILTIN_TENSOR_SHAPE    = 87, /* 텐서형태      ★신규 */
    BUILTIN_TENSOR_NUMEL    = 88, /* 텐서크기      ★신규 */
    BUILTIN_TENSOR_NDIM     = 89, /* 텐서차원      ★신규 */
    BUILTIN_TENSOR_SUM      = 90, /* 텐서합        ★신규 */
    BUILTIN_TENSOR_MEAN     = 91, /* 텐서평균      ★신규 */
    BUILTIN_TENSOR_MAX      = 92, /* 텐서최대      ★신규 */
    BUILTIN_TENSOR_MIN      = 93, /* 텐서최소      ★신규 */
    BUILTIN_TENSOR_MATMUL   = 94, /* 텐서행렬곱    ★신규 */
    BUILTIN_TENSOR_TRANSPOSE= 95, /* 텐서전치      ★신규 */
    BUILTIN_TENSOR_RESHAPE  = 96, /* 텐서변형      ★신규 */
    BUILTIN_TENSOR_FLATTEN  = 97, /* 텐서평탄화    ★신규 */
    BUILTIN_TENSOR_COPY     = 98, /* 텐서복사      ★신규 */

    /* ── 자동미분 2종 (v13.0.0) ─────────────────────────────── */
    BUILTIN_BACKWARD     = 99,  /* 역전파          ★신규 */
    BUILTIN_ZERO_GRAD    = 100, /* 기울기초기화    ★신규 */

    /* ── MCP 1종 (v14.0.0) ───────────────────────────────────── */
    BUILTIN_MCP_ERROR    = 101, /* MCP오류         ★신규 */

    BUILTIN_COUNT        = 102  /* 총 내장 함수 수 */
} KcBuiltinID;

const char *kc_opcode_name(uint8_t op);
const char *kc_builtin_name(int id);

#endif /* KC_BYTECODE_H */
