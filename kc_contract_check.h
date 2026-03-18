/*
 * kc_contract_check.h  —  Kcode 계약 시스템 강제 검증 헤더
 * version : v30.0.0
 *
 * 컴파일 파이프라인에서 파서 직후, 코드 생성 직전에 실행되는
 * 시맨틱 검증 패스.
 *
 * 설계 원칙:
 *   계약 시스템(헌법/법률/규정/법령/법위반)이 없으면 컴파일 거부.
 *   단, MCP 전용 파일(NODE_MCP_SERVER/TOOL/RESOURCE/PROMPT만 있는 파일)은 예외.
 *
 * 적용 대상 (4개 드라이버 동일):
 *   kcodegen_main.c      — C 코드 생성기
 *   kinterp_main.c       — 인터프리터 / REPL
 *   kcodegen_llvm_main.c — LLVM IR 생성기
 *   kbc_main.c           — 바이트코드 VM
 *
 * 참조: kcode_contract_enforce_design.md
 * MIT License / Kcode Project
 */

#ifndef KC_CONTRACT_CHECK_H
#define KC_CONTRACT_CHECK_H

#include "kparser.h"

/* ================================================================
 * §1  검증 결과 코드
 * ================================================================ */
typedef enum {
    KC_CONTRACT_OK,              /* 계약 존재 — 코드 생성 진행          */
    KC_CONTRACT_MISSING,         /* 계약 없음 — 컴파일 거부             */
    KC_CONTRACT_MCP_EXEMPT,      /* MCP 전용 파일 — 예외 허용           */
    KC_CONTRACT_EMPTY_SOURCE,    /* 빈 소스 — 컴파일 거부               */
} KcContractCheckResult;

/* ================================================================
 * §2  검증 상세 정보
 * ================================================================ */
typedef struct {
    KcContractCheckResult result;

    /* 발견된 계약 노드 수 */
    int constitution_count;  /* 헌법  — 전역 최상위 계약  */
    int statute_count;       /* 법률  — 파일 범위 계약    */
    int regulation_count;    /* 규정  — 객체 범위 계약    */
    int contract_count;      /* 법령/법위반 — 함수 계약   */

    /* 파일 구성 정보 */
    int total_nodes;         /* 최상위 노드 수             */
    int mcp_only;            /* 1 = MCP 전용 파일          */

    /* 오류 메시지 (MISSING / EMPTY_SOURCE 시 사용) */
    char error_msg[512];
} KcContractCheckInfo;

/* ================================================================
 * §3  공개 API
 * ================================================================ */

/*
 * kc_contract_check()
 *   AST를 순회하여 계약 시스템 존재 여부를 검증한다.
 *
 *   ast  : parser_parse() 가 반환한 NODE_PROGRAM 루트
 *   info : 검증 상세 결과 출력 구조체 (NULL 가능)
 *
 *   반환값:
 *     KC_CONTRACT_OK         → 코드 생성 진행
 *     KC_CONTRACT_MCP_EXEMPT → 코드 생성 진행 (MCP 예외)
 *     KC_CONTRACT_MISSING    → 컴파일 거부
 *     KC_CONTRACT_EMPTY_SOURCE → 컴파일 거부
 */
KcContractCheckResult kc_contract_check(const Node *ast,
                                         KcContractCheckInfo *info);

/*
 * kc_contract_check_print_error()
 *   검증 실패 시 표준 오류 메시지를 stderr에 출력한다.
 *   result == OK 또는 MCP_EXEMPT 면 아무것도 출력하지 않는다.
 *
 *   filename : 소스 파일 이름 (NULL 가능 — 출력 생략)
 */
void kc_contract_check_print_error(const KcContractCheckInfo *info,
                                    const char *filename);

#endif /* KC_CONTRACT_CHECK_H */
