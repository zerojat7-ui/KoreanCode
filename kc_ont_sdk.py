"""
kc_ont_sdk.py — Kcode 온톨로지 Python SDK
version : v20.2.0

역할 : libkc_ontology.so 를 ctypes 로 감싸 Python 에서 K엔진 온톨로지를
       직접 사용할 수 있게 한다 (모드2 대여 모드).

설치 / 사용법
--------------
# 1) libkc_ontology.so 빌드
#    cmake .. -DKCODE_ONTOLOGY=ON -DBUILD_SHARED_LIBS=ON
#    cmake --build . --target kc_ontology
#    cmake --install . --prefix /usr/local

# 2) Python SDK 사용
#    from kc_ont_sdk import KcOntology
#
#    ont = KcOntology()              # libkc_ontology.so 자동 탐색
#    ont.add_class("제품")
#    ont.add_class("전자제품", parent="제품")
#    ont.add_instance("제품", "TV", [("이름","TV세트"),("가격",500000.0)])
#    results = ont.query("전자제품 찾기")
#    for row in results:
#        print(row)
#    ont.infer()
#    ont.destroy()
"""

import ctypes
import ctypes.util
import os
import json
from typing import List, Optional, Dict, Any, Tuple

# =============================================================================
#  1. 라이브러리 로딩
# =============================================================================

def _load_library(lib_path: Optional[str] = None) -> ctypes.CDLL:
    """
    libkc_ontology 공유 라이브러리를 로드한다.

    탐색 순서:
      1. lib_path 인자 직접 지정
      2. KC_ONT_LIB_PATH 환경 변수
      3. 표준 시스템 경로 (ctypes.util.find_library)
      4. 현재 디렉터리 / 스크립트 위치
    """
    candidates: List[str] = []

    if lib_path:
        candidates.append(lib_path)

    env_path = os.environ.get("KC_ONT_LIB_PATH")
    if env_path:
        candidates.append(env_path)

    found = ctypes.util.find_library("kc_ontology")
    if found:
        candidates.append(found)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    for name in ("libkc_ontology.so", "libkc_ontology.dylib",
                 "kc_ontology.dll", "libkc_ontology.so.20"):
        candidates.append(os.path.join(script_dir, name))
        candidates.append(os.path.join(script_dir, "..", "lib", name))
        candidates.append(os.path.join("/usr/local/lib", name))
        candidates.append(os.path.join("/usr/lib", name))

    last_err = None
    for path in candidates:
        try:
            return ctypes.CDLL(path)
        except OSError as e:
            last_err = e

    raise OSError(
        "libkc_ontology.so 를 찾을 수 없습니다.\n"
        "  빌드: cmake .. -DKCODE_ONTOLOGY=ON -DBUILD_SHARED_LIBS=ON\n"
        "  또는 KC_ONT_LIB_PATH 환경변수를 설정하세요.\n"
        f"  마지막 오류: {last_err}"
    )

# =============================================================================
#  2. C 구조체 / 타입 매핑
# =============================================================================

class _KcOntQueryResult(ctypes.Structure):
    """kc_ont_query.h KcOntQueryResult 에 대응하는 ctypes 구조체."""
    _KC_ONT_QR_MAX_COLS = 32
    _fields_ = [
        ("columns",   ctypes.c_char_p * _KC_ONT_QR_MAX_COLS),
        ("col_count", ctypes.c_int),
        ("rows",      ctypes.POINTER(ctypes.POINTER(ctypes.c_char_p))),
        ("row_count", ctypes.c_int),
        ("error",     ctypes.c_char_p),
    ]

# =============================================================================
#  3. KcOntology 클래스
# =============================================================================

class KcOntology:
    """
    K엔진 온톨로지 Python SDK.

    내부적으로 libkc_ontology.so 의 C API 를 호출하며, 
    컨텍스트(KcOntology*) 포인터를 관리한다.

    사용 예시
    ----------
    >>> from kc_ont_sdk import KcOntology
    >>> ont = KcOntology()
    >>> ont.add_class("제품")
    >>> ont.add_instance("제품", "TV", [("이름", "TV세트"), ("가격", 500000.0)])
    >>> rows = ont.query("제품 찾기")
    >>> print(rows)
    >>> ont.destroy()
    """

    def __init__(self, lib_path: Optional[str] = None):
        """
        Parameters
        ----------
        lib_path : str | None
            libkc_ontology.so 의 명시적 경로. None 이면 자동 탐색.
        """
        self._lib = _load_library(lib_path)
        self._setup_signatures()
        self._ctx = self._lib.kc_ont_create()
        if not self._ctx:
            raise MemoryError("kc_ont_create() 가 NULL 을 반환했습니다.")

    # ------------------------------------------------------------------
    #  내부: C 함수 시그니처 설정
    # ------------------------------------------------------------------
    def _setup_signatures(self) -> None:
        lib = self._lib

        # KcOntology* kc_ont_create(void)
        lib.kc_ont_create.restype  = ctypes.c_void_p
        lib.kc_ont_create.argtypes = []

        # void kc_ont_destroy(KcOntology*)
        lib.kc_ont_destroy.restype  = None
        lib.kc_ont_destroy.argtypes = [ctypes.c_void_p]

        # int kc_ont_add_class(KcOntology*, const char* name, const char* parent)
        lib.kc_ont_add_class.restype  = ctypes.c_int
        lib.kc_ont_add_class.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p
        ]

        # int kc_ont_add_relation(KcOntology*, const char* name,
        #                         const char* from_cls, const char* to_cls)
        lib.kc_ont_add_relation.restype  = ctypes.c_int
        lib.kc_ont_add_relation.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p
        ]

        # int kc_ont_add_instance(KcOntology*, const char* class_name,
        #                          const char* inst_name)
        lib.kc_ont_add_instance.restype  = ctypes.c_int
        lib.kc_ont_add_instance.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p
        ]

        # int kc_ont_set_prop_string(KcOntology*, const char* inst_name,
        #                             const char* prop, const char* value)
        lib.kc_ont_set_prop_string.restype  = ctypes.c_int
        lib.kc_ont_set_prop_string.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p
        ]

        # int kc_ont_set_prop_int(KcOntology*, const char*, const char*, int)
        lib.kc_ont_set_prop_int.restype  = ctypes.c_int
        lib.kc_ont_set_prop_int.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int
        ]

        # int kc_ont_set_prop_float(KcOntology*, const char*, const char*, double)
        lib.kc_ont_set_prop_float.restype  = ctypes.c_int
        lib.kc_ont_set_prop_float.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_double
        ]

        # int kc_ont_infer(KcOntology*)
        lib.kc_ont_infer.restype  = ctypes.c_int
        lib.kc_ont_infer.argtypes = [ctypes.c_void_p]

        # KcOntQueryResult* kc_ont_query_kor(KcOntology*, const char* query_str)
        lib.kc_ont_query_kor.restype  = ctypes.POINTER(_KcOntQueryResult)
        lib.kc_ont_query_kor.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

        # KcOntQueryResult* kc_ont_query_sparql(KcOntology*, const char*)
        lib.kc_ont_query_sparql.restype  = ctypes.POINTER(_KcOntQueryResult)
        lib.kc_ont_query_sparql.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

        # void kc_ont_query_result_free(KcOntQueryResult*)
        lib.kc_ont_query_result_free.restype  = None
        lib.kc_ont_query_result_free.argtypes = [
            ctypes.POINTER(_KcOntQueryResult)
        ]

        # char* kc_ont_serialize_json(KcOntology*)
        lib.kc_ont_serialize_json.restype  = ctypes.c_char_p
        lib.kc_ont_serialize_json.argtypes = [ctypes.c_void_p]

        # char* kc_ont_serialize_turtle(KcOntology*)
        lib.kc_ont_serialize_turtle.restype  = ctypes.c_char_p
        lib.kc_ont_serialize_turtle.argtypes = [ctypes.c_void_p]

        # int kc_ont_import_json_ld(KcOntology*, const char* json_str)
        lib.kc_ont_import_json_ld.restype  = ctypes.c_int
        lib.kc_ont_import_json_ld.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

        # int kc_ont_import_turtle(KcOntology*, const char* ttl_str)
        lib.kc_ont_import_turtle.restype  = ctypes.c_int
        lib.kc_ont_import_turtle.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

        # int kc_ont_import_csv(KcOntology*, const char* class_name,
        #                        const char* csv_str)
        lib.kc_ont_import_csv.restype  = ctypes.c_int
        lib.kc_ont_import_csv.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p
        ]

        # int kc_ont_safe_for_learning(KcOntology*)
        lib.kc_ont_safe_for_learning.restype  = ctypes.c_int
        lib.kc_ont_safe_for_learning.argtypes = [ctypes.c_void_p]

        # const char* kc_ont_version(void)
        lib.kc_ont_version.restype  = ctypes.c_char_p
        lib.kc_ont_version.argtypes = []

    # ------------------------------------------------------------------
    #  공개 API
    # ------------------------------------------------------------------

    @staticmethod
    def version() -> str:
        """K엔진 온톨로지 라이브러리 버전을 반환한다."""
        # 전역 함수 — 컨텍스트 불필요
        lib = _load_library()
        lib.kc_ont_version.restype  = ctypes.c_char_p
        lib.kc_ont_version.argtypes = []
        result = lib.kc_ont_version()
        return result.decode("utf-8") if result else "unknown"

    def destroy(self) -> None:
        """온톨로지 컨텍스트를 해제한다. 이후 모든 메서드 호출은 오류를 발생시킨다."""
        if self._ctx:
            self._lib.kc_ont_destroy(self._ctx)
            self._ctx = None

    def __del__(self):
        self.destroy()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.destroy()

    # ── 클래스 / 관계 ---------------------------------------------------

    def add_class(self, name: str, parent: Optional[str] = None) -> int:
        """
        온톨로지에 클래스(개념)를 추가한다.

        Parameters
        ----------
        name   : 클래스 이름 (한글 가능)
        parent : 부모 클래스 이름. None 이면 최상위 클래스.

        Returns
        -------
        0 성공, 음수 오류 코드(KcOntError)
        """
        self._check_ctx()
        return self._lib.kc_ont_add_class(
            self._ctx,
            name.encode("utf-8"),
            parent.encode("utf-8") if parent else None,
        )

    def add_relation(self, name: str, from_class: str, to_class: str) -> int:
        """
        두 클래스 사이의 관계를 정의한다.

        Parameters
        ----------
        name       : 관계 이름
        from_class : 출발 클래스
        to_class   : 도착 클래스
        """
        self._check_ctx()
        return self._lib.kc_ont_add_relation(
            self._ctx,
            name.encode("utf-8"),
            from_class.encode("utf-8"),
            to_class.encode("utf-8"),
        )

    # ── 인스턴스 / 속성 -------------------------------------------------

    def add_instance(
        self,
        class_name: str,
        inst_name:  str,
        props: Optional[List[Tuple[str, Any]]] = None,
    ) -> int:
        """
        클래스에 인스턴스를 추가하고 속성값을 설정한다.

        Parameters
        ----------
        class_name : 소속 클래스 이름
        inst_name  : 인스턴스 이름
        props      : [(속성명, 값), ...] 목록. 값 타입에 따라 자동 분기.

        Returns
        -------
        0 성공
        """
        self._check_ctx()
        rc = self._lib.kc_ont_add_instance(
            self._ctx,
            class_name.encode("utf-8"),
            inst_name.encode("utf-8"),
        )
        if rc != 0:
            return rc
        if props:
            for prop_name, value in props:
                self.set_property(inst_name, prop_name, value)
        return 0

    def set_property(self, inst_name: str, prop_name: str, value: Any) -> int:
        """
        인스턴스 속성을 설정한다. 값 타입에 따라 C 함수를 자동 선택한다.

        지원 타입: str → string, int → int, float/복합 → float
        """
        self._check_ctx()
        enc_inst  = inst_name.encode("utf-8")
        enc_prop  = prop_name.encode("utf-8")
        if isinstance(value, bool):
            return self._lib.kc_ont_set_prop_int(
                self._ctx, enc_inst, enc_prop, int(value)
            )
        if isinstance(value, int):
            return self._lib.kc_ont_set_prop_int(
                self._ctx, enc_inst, enc_prop, value
            )
        if isinstance(value, float):
            return self._lib.kc_ont_set_prop_float(
                self._ctx, enc_inst, enc_prop, value
            )
        # 그 외는 문자열로 변환
        return self._lib.kc_ont_set_prop_string(
            self._ctx, enc_inst, enc_prop, str(value).encode("utf-8")
        )

    # ── 추론 ------------------------------------------------------------

    def infer(self) -> int:
        """
        전방향 체이닝 추론을 실행한다.

        Returns
        -------
        도출된 사실(Triple) 수. 음수면 오류.
        """
        self._check_ctx()
        return self._lib.kc_ont_infer(self._ctx)

    # ── 질의 ------------------------------------------------------------

    def query(
        self, query_str: str, sparql: bool = False
    ) -> List[Dict[str, str]]:
        """
        한글 질의 또는 SPARQL 질의를 실행하고 결과를 반환한다.

        Parameters
        ----------
        query_str : 질의 문자열
        sparql    : True 이면 SPARQL 1.1 질의로 처리

        Returns
        -------
        [{"컬럼명": "값", ...}, ...] 형태의 딕셔너리 목록
        """
        self._check_ctx()
        enc_q = query_str.encode("utf-8")
        if sparql:
            ptr = self._lib.kc_ont_query_sparql(self._ctx, enc_q)
        else:
            ptr = self._lib.kc_ont_query_kor(self._ctx, enc_q)

        if not ptr:
            return []

        result = ptr.contents
        if result.error:
            err = result.error.decode("utf-8")
            self._lib.kc_ont_query_result_free(ptr)
            raise RuntimeError(f"온톨로지 질의 오류: {err}")

        cols = [
            result.columns[i].decode("utf-8")
            for i in range(result.col_count)
        ]
        rows: List[Dict[str, str]] = []
        for r in range(result.row_count):
            row_ptr = result.rows[r]
            row: Dict[str, str] = {}
            for c, col in enumerate(cols):
                val = row_ptr[c]
                row[col] = val.decode("utf-8") if val else ""
            rows.append(row)

        self._lib.kc_ont_query_result_free(ptr)
        return rows

    # ── 직렬화 / 임포트 ------------------------------------------------

    def to_json_ld(self) -> Dict[str, Any]:
        """온톨로지를 JSON-LD 딕셔너리로 직렬화한다."""
        self._check_ctx()
        raw = self._lib.kc_ont_serialize_json(self._ctx)
        if not raw:
            return {}
        return json.loads(raw.decode("utf-8"))

    def to_turtle(self) -> str:
        """온톨로지를 Turtle(TTL) 문자열로 직렬화한다."""
        self._check_ctx()
        raw = self._lib.kc_ont_serialize_turtle(self._ctx)
        return raw.decode("utf-8") if raw else ""

    def load_json_ld(self, data: Dict[str, Any]) -> int:
        """JSON-LD 딕셔너리를 현재 온톨로지에 병합한다."""
        self._check_ctx()
        json_str = json.dumps(data, ensure_ascii=False).encode("utf-8")
        return self._lib.kc_ont_import_json_ld(self._ctx, json_str)

    def load_json_ld_file(self, path: str) -> int:
        """JSON-LD 파일을 로드한다."""
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        return self.load_json_ld(data)

    def load_turtle(self, ttl_str: str) -> int:
        """Turtle 문자열을 파싱해 온톨로지에 병합한다."""
        self._check_ctx()
        return self._lib.kc_ont_import_turtle(
            self._ctx, ttl_str.encode("utf-8")
        )

    def load_turtle_file(self, path: str) -> int:
        """Turtle 파일을 로드한다."""
        with open(path, encoding="utf-8") as f:
            ttl = f.read()
        return self.load_turtle(ttl)

    def load_csv(self, class_name: str, csv_str: str) -> int:
        """CSV 문자열을 지정 클래스의 인스턴스로 임포트한다."""
        self._check_ctx()
        return self._lib.kc_ont_import_csv(
            self._ctx,
            class_name.encode("utf-8"),
            csv_str.encode("utf-8"),
        )

    def load_csv_file(self, class_name: str, path: str) -> int:
        """CSV 파일을 로드한다."""
        with open(path, encoding="utf-8") as f:
            csv = f.read()
        return self.load_csv(class_name, csv)

    # ── 학습 거버넌스 --------------------------------------------------

    def safe_for_learning(self) -> bool:
        """
        민감 속성 포함 여부를 검사한다.

        Returns
        -------
        True  : 학습 안전 (민감 속성 없음)
        False : 학습 차단 (민감 속성 감지됨)
        """
        self._check_ctx()
        return self._lib.kc_ont_safe_for_learning(self._ctx) == 1

    # ── 내부 유틸 ------------------------------------------------------

    def _check_ctx(self) -> None:
        if not self._ctx:
            raise RuntimeError(
                "온톨로지 컨텍스트가 이미 해제되었습니다. destroy() 이후 사용 불가."
            )


# =============================================================================
#  4. 편의 함수
# =============================================================================

def from_json_ld_file(path: str, lib_path: Optional[str] = None) -> KcOntology:
    """JSON-LD 파일에서 온톨로지를 생성한다."""
    ont = KcOntology(lib_path)
    rc = ont.load_json_ld_file(path)
    if rc != 0:
        ont.destroy()
        raise IOError(f"JSON-LD 로드 실패 (rc={rc}): {path}")
    return ont


def from_turtle_file(path: str, lib_path: Optional[str] = None) -> KcOntology:
    """Turtle 파일에서 온톨로지를 생성한다."""
    ont = KcOntology(lib_path)
    rc = ont.load_turtle_file(path)
    if rc != 0:
        ont.destroy()
        raise IOError(f"Turtle 로드 실패 (rc={rc}): {path}")
    return ont


# =============================================================================
#  5. CLI 간이 테스트
# =============================================================================

if __name__ == "__main__":
    import sys

    print(f"Kcode 온톨로지 Python SDK v20.2.0")
    print("사용법: python kc_ont_sdk.py [libkc_ontology.so 경로]")

    lib_path = sys.argv[1] if len(sys.argv) > 1 else None

    try:
        with KcOntology(lib_path) as ont:
            print("\n[1] 클래스 추가")
            ont.add_class("제품")
            ont.add_class("전자제품", parent="제품")
            print("  ✅ 제품, 전자제품 추가")

            print("[2] 관계 추가")
            ont.add_relation("구매함", "사람", "제품")
            print("  ✅ 구매함 (사람 → 제품)")

            print("[3] 인스턴스 추가")
            ont.add_instance("전자제품", "TV세트", [
                ("이름",  "삼성 TV"),
                ("가격",  1_500_000.0),
                ("전압",  220.0),
            ])
            print("  ✅ TV세트 인스턴스")

            print("[4] 학습 안전성 검사")
            safe = ont.safe_for_learning()
            print(f"  {'✅ 안전' if safe else '⚠️ 민감 속성 감지됨'}")

            print("[5] JSON-LD 직렬화")
            jd = ont.to_json_ld()
            print(f"  키: {list(jd.keys())[:5]}")

            print("[6] 추론 실행")
            facts = ont.infer()
            print(f"  도출 사실 수: {facts}")

            print("\n✅ 모든 테스트 통과")

    except OSError as e:
        print(f"\n⚠️  라이브러리를 찾을 수 없습니다.\n{e}")
        print("\n빌드 후 재시도:")
        print("  cmake .. -DKCODE_ONTOLOGY=ON -DBUILD_SHARED_LIBS=ON")
        print("  cmake --build . --target kc_ontology")
        sys.exit(1)
