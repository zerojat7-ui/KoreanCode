/*
 * kc_ont_import.h — K엔진 외부 포맷 임포트 헤더
 * Kcode v19.0.0
 */

#ifndef KC_ONT_IMPORT_H
#define KC_ONT_IMPORT_H

#include "kc_ontology.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CSV 임포트
 * format:
 *   [개념] / [속성] / [인스턴스] 섹션 구조
 */
int kc_ont_import_csv(KcOntology *onto, const char *csv_str);

/*
 * JSON-LD 임포트
 * {"@context":{...}, "@graph":[{...},...]}
 */
int kc_ont_import_jsonld(KcOntology *onto, const char *jsonld_str);

/*
 * 내부 JSON 임포트 (kc_ont_to_json 역직렬화)
 */
int kc_ont_import_json(KcOntology *onto, const char *json_str);

/*
 * 파일에서 임포트
 * format: "csv" | "jsonld" | "json"
 */
int kc_ont_import_file(KcOntology *onto, const char *path,
                        const char *format);

/*
 * 문자열에서 임포트
 * format: "csv" | "jsonld" | "json" | "turtle" | "owl"
 */
int kc_ont_import_string(KcOntology *onto, const char *str,
                          const char *format);

/* ================================================================
 * Turtle (TTL) 임포트
 * W3C RDF 1.1 Turtle 서브셋 파서
 * @prefix 선언 + 삼중 트리플(subject predicate object .) 지원
 * ================================================================ */
int kc_ont_import_turtle(KcOntology *onto, const char *ttl_str);

/* ================================================================
 * OWL/XML 임포트
 * OWL 2 XML Serialization 경량 파서
 * Class / ObjectProperty / DataProperty / Individual 지원
 * ================================================================ */
int kc_ont_import_owl(KcOntology *onto, const char *owl_xml_str);

#ifdef __cplusplus
}
#endif

#endif /* KC_ONT_IMPORT_H */
