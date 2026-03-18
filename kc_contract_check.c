/*
 * kc_contract_check.c  —  Kcode 계약 시스템 강제 검증 구현
 * version : v30.0.0
 *
 * AST(NODE_PROGRAM)를 순회하여 계약 노드 4종 중 하나 이상 존재 여부 확인.
 * MCP 전용 파일은 예외 처리.
 *
 * MIT License / Kcode Project
 */

#include "kc_contract_check.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * §1  내부 헬퍼
 * ================================================================ */

/* MCP 관련 최상위 노드 여부 */
static int is_mcp_node(NodeType type) {
    return (type == NODE_MCP_SERVER   ||
            type == NODE_MCP_TOOL     ||
            type == NODE_MCP_RESOURCE ||
            type == NODE_MCP_PROMPT);
}

/* 계약 노드 여부 */
static int is_contract_node(NodeType type) {
    return (type == NODE_CONSTITUTION ||
            type == NODE_STATUTE      ||
            type == NODE_REGULATION   ||
            type == NODE_CONTRACT);
}

/* ================================================================
 * §2  AST 재귀 순회 — 계약 노드 카운트
 * ================================================================ */
static void scan_node(const Node *node, KcContractCheckInfo *out) {
    if (!node) return;

    switch (node->type) {
        case NODE_CONSTITUTION: out->constitution_count++; break;
        case NODE_STATUTE:      out->statute_count++;      break;
        case NODE_REGULATION:   out->regulation_count++;   break;
        case NODE_CONTRACT:     out->contract_count++;     break;
        default: break;
    }

    for (int i = 0; i < node->child_count; i++)
        scan_node(node->children[i], out);
}

/* ================================================================
 * §3  공개 API 구현
 * ================================================================ */
KcContractCheckResult kc_contract_check(const Node *ast,
                                         KcContractCheckInfo *info) {
    KcContractCheckInfo local;
    KcContractCheckInfo *out = info ? info : &local;
    memset(out, 0, sizeof(KcContractCheckInfo));

    /* ── 빈 소스 ──────────────────────────────────────────── */
    if (!ast || ast->child_count == 0) {
        out->result = KC_CONTRACT_EMPTY_SOURCE;
        snprintf(out->error_msg, sizeof(out->error_msg),
            "[계약 오류] 소스가 비어 있습니다.\n"
            "\n"
            "  Kcode 파일에는 반드시 계약 시스템이 있어야 합니다.\n"
            "  최소 요건: 헌법, 법률, 규정, 법령/법위반 중 하나.");
        return out->result;
    }

    out->total_nodes = ast->child_count;

    /* ── 최상위 노드 분류 (MCP 전용 여부 판별) ──────────────── */
    int has_mcp     = 0;
    int has_non_mcp = 0;

    for (int i = 0; i < ast->child_count; i++) {
        const Node *child = ast->children[i];
        if (!child) continue;
        if (is_mcp_node(child->type))
            has_mcp = 1;
        else
            has_non_mcp = 1;
    }

    /* ── MCP 전용 파일 → 예외 허용 ──────────────────────────── */
    if (has_mcp && !has_non_mcp) {
        out->mcp_only = 1;
        out->result   = KC_CONTRACT_MCP_EXEMPT;
        return out->result;
    }

    /* ── 전체 AST 순회: 계약 노드 탐색 ──────────────────────── */
    scan_node(ast, out);

    int total_contract = out->constitution_count
                       + out->statute_count
                       + out->regulation_count
                       + out->contract_count;

    /* ── 계약 없음 → 컴파일 거부 ─────────────────────────────── */
    if (total_contract == 0) {
        out->result = KC_CONTRACT_MISSING;
        snprintf(out->error_msg, sizeof(out->error_msg),
            "[계약 오류] 계약 시스템이 없습니다.\n"
            "\n"
            "  Kcode 파일에는 반드시 다음 중 하나 이상이 있어야 합니다:\n"
            "\n"
            "    헌법 이름:          -- 전역 최상위 계약\n"
            "    법률 이름:          -- 파일 범위 계약\n"
            "    규정 객체이름:      -- 객체 범위 계약\n"
            "    함수에 법령/법위반  -- 함수 단위 계약\n"
            "\n"
            "  예시:\n"
            "    헌법 안전정책:\n"
            "        법령:  입력값 >= 0\n"
            "        법위반: 오류발생(\"음수 입력 금지\")\n"
            "    헌법끝\n"
            "\n"
            "  ※ MCP 전용 파일(MCP서버 블록만 있는 경우)은 예외입니다.");
        return out->result;
    }

    /* ── 통과 ─────────────────────────────────────────────── */
    out->result = KC_CONTRACT_OK;
    return out->result;
}

/* ================================================================
 * §4  오류 출력
 * ================================================================ */
void kc_contract_check_print_error(const KcContractCheckInfo *info,
                                    const char *filename) {
    if (!info) return;
    if (info->result == KC_CONTRACT_OK ||
        info->result == KC_CONTRACT_MCP_EXEMPT) return;

    fprintf(stderr, "\n");
    if (filename)
        fprintf(stderr, "파일: %s\n", filename);
    fprintf(stderr, "%s\n\n", info->error_msg);
}
