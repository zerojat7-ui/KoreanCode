/*
 * kc_label_test.c — §15-2 명칭 레이어 통합 테스트
 * Kcode v22.11.1
 *
 * 테스트 범위:
 *   Layer 1  — KcNodeLabel 초기화 / altLabel CRUD / 완전일치 검색
 *   Layer 2  — 트라이그램 유사도 (Jaccard ≥ 70%) / 제안 패킷 생성
 *   Layer 3  — KcAliasMap CRUD 5종 (GLOBAL/LOCAL/PERSONAL scope)
 *   E2E      — Layer 1 → 2 → 3 전체 흐름 + 다국어/구어체 시나리오
 *
 * TC 목록 (총 14종):
 *   TC-01  KcNodeLabel 초기화
 *   TC-02  altLabel 추가 (정상)
 *   TC-03  altLabel 추가 초과 (KC_ONT_ERR_FULL)
 *   TC-04  altLabel 완전일치 검색 (hit)
 *   TC-05  altLabel 완전일치 검색 (miss)
 *   TC-06  트라이그램 유사도 — 동일 문자열 1.0
 *   TC-07  트라이그램 유사도 — 완전 다른 문자열 0.0
 *   TC-08  Layer 2 유사도 제안 (≥70% → suggest)
 *   TC-09  Layer 2 altLabel 수락 흐름
 *   TC-10  KcAliasMap add + count (GLOBAL)
 *   TC-11  KcAliasMap 중복 차단 (GLOBAL)
 *   TC-12  KcAliasMap PERSONAL scope — owner_id 구분
 *   TC-13  KcAliasMap remove → 비활성화
 *   TC-14  다국어/구어체 E2E — 영어 altLabel 등록 → 유사도 검색 → AliasMap 연동
 *
 * 빌드:
 *   gcc -std=c11 -Wall -Wextra -o kc_label_test \
 *       kc_ontology.c kc_ont_label.c kc_kbank.c \
 *       kc_state_log.c kc_vec.c kc_tensor.c kc_mcp.c \
 *       kc_label_test.c -lm
 *
 * MIT License / Kcode Project
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "kc_ontology.h"
#include "kc_kbank.h"

/* ================================================================
 * 테스트 헬퍼
 * ================================================================ */

static int g_pass = 0;
static int g_fail = 0;

#define TC_ASSERT(id, cond, msg) \
    do { \
        if (cond) { \
            printf("[PASS] TC-%02d %s\n", (id), (msg)); \
            g_pass++; \
        } else { \
            printf("[FAIL] TC-%02d %s  (line %d)\n", (id), (msg), __LINE__); \
            g_fail++; \
        } \
    } while(0)

/* ================================================================
 * TC-01  KcNodeLabel 초기화
 * ================================================================ */
static void tc_01(void)
{
    KcNodeLabel lbl;
    kc_ont_label_init(&lbl, "자동차", "ko");

    TC_ASSERT(1,
        strncmp(lbl.pref_label, "자동차", KC_ONT_LABEL_TEXT_LEN) == 0 &&
        lbl.alt_count == 0 &&
        strncmp(lbl.pref_lang, "ko", KC_ONT_LABEL_LANG_LEN) == 0,
        "KcNodeLabel 초기화 — pref_label/alt_count/pref_lang 검증");
}

/* ================================================================
 * TC-02  altLabel 추가 (정상)
 * ================================================================ */
static void tc_02(void)
{
    KcNodeLabel lbl;
    kc_ont_label_init(&lbl, "자동차", "ko");

    int r1 = kc_ont_label_add_alt(&lbl, "car",   "en");
    int r2 = kc_ont_label_add_alt(&lbl, "車",     "ja");
    int r3 = kc_ont_label_add_alt(&lbl, "voiture","fr");

    TC_ASSERT(2,
        r1 == KC_ONT_OK && r2 == KC_ONT_OK && r3 == KC_ONT_OK &&
        lbl.alt_count == 3,
        "altLabel 추가 정상 — 3종 (en/ja/fr)");
}

/* ================================================================
 * TC-03  altLabel 추가 초과 (KC_ONT_ERR_FULL)
 * ================================================================ */
static void tc_03(void)
{
    KcNodeLabel lbl;
    kc_ont_label_init(&lbl, "테스트", "ko");

    /* KC_ONT_LABEL_ALT_MAX(8) 채우기 */
    const char *alts[] = {"a","b","c","d","e","f","g","h"};
    for (int i = 0; i < KC_ONT_LABEL_ALT_MAX; i++) {
        kc_ont_label_add_alt(&lbl, alts[i], "en");
    }
    int r = kc_ont_label_add_alt(&lbl, "overflow", "en");

    TC_ASSERT(3,
        r == KC_ONT_ERR_FULL && lbl.alt_count == KC_ONT_LABEL_ALT_MAX,
        "altLabel 초과 → KC_ONT_ERR_FULL");
}

/* ================================================================
 * TC-04  altLabel 완전일치 검색 (hit)
 * ================================================================ */
static void tc_04(void)
{
    KcOntology *ont = kc_ont_create("test");
    kc_ont_add_class(ont, "스마트폰", NULL);
    KcOntClass *cls = kc_ont_get_class(ont, "스마트폰");

    kc_ont_label_init(&cls->label, "스마트폰", "ko");
    kc_ont_label_add_alt(&cls->label, "smartphone", "en");
    kc_ont_label_add_alt(&cls->label, "携帯電話",   "ja");

    KcOntClass *found = kc_ont_label_find_by_alt(ont, "smartphone");

    TC_ASSERT(4,
        found != NULL &&
        strncmp(found->name, "스마트폰", KC_ONT_NAME_LEN) == 0,
        "altLabel 완전일치 검색 hit — 'smartphone' → '스마트폰'");

    kc_ont_destroy(ont);
}

/* ================================================================
 * TC-05  altLabel 완전일치 검색 (miss)
 * ================================================================ */
static void tc_05(void)
{
    KcOntology *ont = kc_ont_create("test");
    kc_ont_add_class(ont, "노트북", NULL);
    KcOntClass *cls = kc_ont_get_class(ont, "노트북");
    kc_ont_label_init(&cls->label, "노트북", "ko");
    kc_ont_label_add_alt(&cls->label, "laptop", "en");

    KcOntClass *found = kc_ont_label_find_by_alt(ont, "tablet");

    TC_ASSERT(5,
        found == NULL,
        "altLabel 완전일치 검색 miss — 'tablet' 없음 → NULL");

    kc_ont_destroy(ont);
}

/* ================================================================
 * TC-06  트라이그램 유사도 — 동일 문자열 1.0
 * ================================================================ */
static void tc_06(void)
{
    float sim = kc_ont_label_similarity("스마트폰", "스마트폰");

    TC_ASSERT(6,
        fabsf(sim - 1.0f) < 1e-4f,
        "트라이그램 유사도 동일 문자열 → 1.0");
}

/* ================================================================
 * TC-07  트라이그램 유사도 — 완전 다른 문자열
 * ================================================================ */
static void tc_07(void)
{
    float sim = kc_ont_label_similarity("가나다", "xyz");

    TC_ASSERT(7,
        sim < KC_ONT_LABEL_SIM_THRESH,
        "트라이그램 유사도 완전 다른 문자열 → 임계값 미만");
}

/* ================================================================
 * TC-08  Layer 2 유사도 제안 (≥70% → suggest 발생)
 * ================================================================ */
static void tc_08(void)
{
    KcOntology *ont = kc_ont_create("test");
    kc_ont_add_class(ont, "스마트폰", NULL);
    KcOntClass *cls = kc_ont_get_class(ont, "스마트폰");
    kc_ont_label_init(&cls->label, "스마트폰", "ko");
    /* "스마트폰" 과 "스마트 폰" 은 한글 바이트 3-gram 상 높은 유사도 */
    kc_ont_label_add_alt(&cls->label, "스마트폰단말기", "ko");

    KcLabelSuggest sg;
    memset(&sg, 0, sizeof(sg));
    /* pref_label 직접 유사도 검사 — "스마트폰" vs "스마트폰단말기" */
    int r = kc_ont_label_suggest(ont, "스마트폰", &sg);

    /* pref_label 완전일치 → 유사도 1.0 → suggest 반환 */
    TC_ASSERT(8,
        r == 1 && sg.similarity >= KC_ONT_LABEL_SIM_THRESH,
        "Layer 2 유사도 제안 — 임계값 이상 suggest 반환");

    kc_ont_destroy(ont);
}

/* ================================================================
 * TC-09  Layer 2 altLabel 수락 흐름
 * ================================================================ */
static void tc_09(void)
{
    KcOntology *ont = kc_ont_create("test");
    kc_ont_add_class(ont, "자동차", NULL);
    KcOntClass *cls = kc_ont_get_class(ont, "자동차");
    kc_ont_label_init(&cls->label, "자동차", "ko");
    kc_ont_label_add_alt(&cls->label, "car", "en");

    KcLabelSuggest sg;
    memset(&sg, 0, sizeof(sg));
    /* "자동차" 는 pref_label 완전일치 → suggest */
    kc_ont_label_suggest(ont, "자동차", &sg);
    sg.accept_as_alt = 1;

    int r = kc_ont_label_accept_alt(ont, &sg, "ko");

    TC_ASSERT(9,
        r == KC_ONT_OK,
        "Layer 2 altLabel 수락 — accept_as_alt=1 후 kc_ont_label_accept_alt OK");

    kc_ont_destroy(ont);
}

/* ================================================================
 * TC-10  KcAliasMap add + count (GLOBAL)
 * ================================================================ */
static void tc_10(void)
{
    KcAliasMap map;
    memset(&map, 0, sizeof(map));

    uint8_t lid[KC_ALIAS_LINK_ID_LEN] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10
    };

    KcKbankErr r1 = kc_alias_add(&map, lid, "자동차",    KC_ALIAS_SCOPE_GLOBAL, NULL);
    KcKbankErr r2 = kc_alias_add(&map, lid, "car",       KC_ALIAS_SCOPE_GLOBAL, NULL);
    KcKbankErr r3 = kc_alias_add(&map, lid, "automobile",KC_ALIAS_SCOPE_GLOBAL, NULL);

    int cnt = kc_alias_count(&map, KC_ALIAS_SCOPE_GLOBAL);

    TC_ASSERT(10,
        r1 == KC_KBANK_OK && r2 == KC_KBANK_OK && r3 == KC_KBANK_OK &&
        cnt == 3,
        "KcAliasMap add 3종 + count(GLOBAL) == 3");
}

/* ================================================================
 * TC-11  KcAliasMap 중복 차단 (GLOBAL)
 * ================================================================ */
static void tc_11(void)
{
    KcAliasMap map;
    memset(&map, 0, sizeof(map));

    uint8_t lid[KC_ALIAS_LINK_ID_LEN] = {0xAA};

    kc_alias_add(&map, lid, "스마트폰", KC_ALIAS_SCOPE_GLOBAL, NULL);
    KcKbankErr r = kc_alias_add(&map, lid, "스마트폰", KC_ALIAS_SCOPE_GLOBAL, NULL);

    TC_ASSERT(11,
        r == KC_KBANK_ERR_FULL,
        "KcAliasMap GLOBAL 중복 alias 차단 → KC_KBANK_ERR_FULL");
}

/* ================================================================
 * TC-12  KcAliasMap PERSONAL scope — owner_id 구분
 * ================================================================ */
static void tc_12(void)
{
    KcAliasMap map;
    memset(&map, 0, sizeof(map));

    uint8_t lid[KC_ALIAS_LINK_ID_LEN] = {0xBB};

    /* 같은 alias 라도 owner_id 다르면 허용 */
    KcKbankErr r1 = kc_alias_add(&map, lid, "내폰", KC_ALIAS_SCOPE_PERSONAL, "user_A");
    KcKbankErr r2 = kc_alias_add(&map, lid, "내폰", KC_ALIAS_SCOPE_PERSONAL, "user_B");
    /* 같은 owner_id + alias → 중복 차단 */
    KcKbankErr r3 = kc_alias_add(&map, lid, "내폰", KC_ALIAS_SCOPE_PERSONAL, "user_A");

    TC_ASSERT(12,
        r1 == KC_KBANK_OK &&
        r2 == KC_KBANK_OK &&
        r3 == KC_KBANK_ERR_FULL,
        "KcAliasMap PERSONAL — owner_id 구분 허용 + 동일 owner 중복 차단");
}

/* ================================================================
 * TC-13  KcAliasMap remove → 비활성화
 * ================================================================ */
static void tc_13(void)
{
    KcAliasMap map;
    memset(&map, 0, sizeof(map));

    uint8_t lid[KC_ALIAS_LINK_ID_LEN] = {0xCC};
    kc_alias_add(&map, lid, "노트북", KC_ALIAS_SCOPE_LOCAL, NULL);

    int before = kc_alias_count(&map, KC_ALIAS_SCOPE_LOCAL);
    KcKbankErr r = kc_alias_remove(&map, "노트북", KC_ALIAS_SCOPE_LOCAL, NULL);
    int after  = kc_alias_count(&map, KC_ALIAS_SCOPE_LOCAL);

    TC_ASSERT(13,
        before == 1 && r == KC_KBANK_OK && after == 0,
        "KcAliasMap remove → active=0 비활성화, count 감소");
}

/* ================================================================
 * TC-14  다국어/구어체 E2E
 *         영어 altLabel 등록 → 유사도 검색 → AliasMap 연동
 * ================================================================ */
static void tc_14(void)
{
    /* [1] 온톨로지 개념 등록 + altLabel 설정 */
    KcOntology *ont = kc_ont_create("test");
    kc_ont_add_class(ont, "전기자동차", NULL);
    KcOntClass *cls = kc_ont_get_class(ont, "전기자동차");
    kc_ont_label_init(&cls->label, "전기자동차", "ko");
    kc_ont_label_add_alt(&cls->label, "electric vehicle", "en");
    kc_ont_label_add_alt(&cls->label, "EV",               "en");
    kc_ont_label_add_alt(&cls->label, "전기차",            "ko");  /* 구어체 */

    /* [2] Layer 1 — 구어체 완전일치 검색 */
    KcOntClass *found = kc_ont_label_find_by_alt(ont, "전기차");
    int layer1_ok = (found != NULL &&
                     strncmp(found->name, "전기자동차", KC_ONT_NAME_LEN) == 0);

    /* [3] Layer 2 — 영어 유사도 제안 */
    KcLabelSuggest sg;
    memset(&sg, 0, sizeof(sg));
    /* "전기자동차" pref 완전일치 → suggest */
    int suggest_r = kc_ont_label_suggest(ont, "전기자동차", &sg);

    /* [4] Layer 3 — AliasMap 에 link_id 연결 */
    KcAliasMap amap;
    memset(&amap, 0, sizeof(amap));

    uint8_t fake_lid[KC_ALIAS_LINK_ID_LEN];
    memset(fake_lid, 0xDE, KC_ALIAS_LINK_ID_LEN);

    kc_alias_add(&amap, fake_lid, "전기자동차",     KC_ALIAS_SCOPE_GLOBAL,   NULL);
    kc_alias_add(&amap, fake_lid, "electric vehicle",KC_ALIAS_SCOPE_GLOBAL,  NULL);
    kc_alias_add(&amap, fake_lid, "전기차",          KC_ALIAS_SCOPE_PERSONAL, "user_ko");

    int cnt_global   = kc_alias_count(&amap, KC_ALIAS_SCOPE_GLOBAL);
    int cnt_personal = kc_alias_count(&amap, KC_ALIAS_SCOPE_PERSONAL);

    /* 결과 검증: Layer 1 hit + Layer 2 suggest + Layer 3 AliasMap 정합 */
    int ok = layer1_ok &&
             suggest_r == 1 &&
             cnt_global == 2 &&
             cnt_personal == 1;

    TC_ASSERT(14,
        ok,
        "다국어/구어체 E2E — Layer1 hit + Layer2 suggest + Layer3 AliasMap 연동");

    kc_ont_destroy(ont);
}

/* ================================================================
 * main
 * ================================================================ */
int main(void)
{
    printf("=== kc_label_test v22.11.1 — 명칭 레이어 통합 테스트 ===\n\n");

    tc_01();
    tc_02();
    tc_03();
    tc_04();
    tc_05();
    tc_06();
    tc_07();
    tc_08();
    tc_09();
    tc_10();
    tc_11();
    tc_12();
    tc_13();
    tc_14();

    printf("\n--- 결과: PASS %d / FAIL %d / TOTAL %d ---\n",
           g_pass, g_fail, g_pass + g_fail);

    return (g_fail == 0) ? 0 : 1;
}
