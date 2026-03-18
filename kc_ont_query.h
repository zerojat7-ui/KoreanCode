/*
 * kc_ont_query.h — K엔진 질의 실행기 헤더
 * Kcode v18.5.0
 */

#ifndef KC_ONT_QUERY_H
#define KC_ONT_QUERY_H

#include "kc_ontology.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 최대 컬럼 수 */
#define KC_ONT_QR_MAX_COLS 32

/* ================================================================
 * 질의 결과 구조체
 * ================================================================ */

typedef struct {
    char  *columns[KC_ONT_QR_MAX_COLS]; /* 컬럼명 배열 */
    int    col_count;

    char ***rows;       /* rows[행][열] — 동적 할당 */
    int    row_count;

    char  *error;       /* NULL = 정상, 아니면 오류 메시지 */
} KcOntQueryResult;

/* ================================================================
 * API
 * ================================================================ */

/* 결과 생성 / 해제 */
KcOntQueryResult *kc_ont_qr_create(void);
void              kc_ont_qr_destroy(KcOntQueryResult *r);

/* 결과 JSON 직렬화 (호출자가 free() 책임) */
char             *kc_ont_qr_to_json(const KcOntQueryResult *r);

/*
 * 한글 질의 실행
 * 예:
 *   선택 ?센서 ?값
 *   조건:
 *       ?센서 는 온도센서
 *       ?센서.측정값 > 70
 *   정렬: ?센서.측정값 내림차순
 *   제한: 10
 */
KcOntQueryResult *kc_ont_query(KcOntology *onto,
                                const char *query_str);

/*
 * SPARQL 1.1 서브셋 실행
 * 예:
 *   SELECT ?s ?v WHERE {
 *       ?s rdf:type :온도센서 .
 *       ?s :측정값 ?v .
 *       FILTER(?v > 70)
 *   } ORDER BY DESC(?v) LIMIT 10
 */
KcOntQueryResult *kc_ont_sparql(KcOntology *onto,
                                 const char *sparql_str);

#ifdef __cplusplus
}
#endif

#endif /* KC_ONT_QUERY_H */
