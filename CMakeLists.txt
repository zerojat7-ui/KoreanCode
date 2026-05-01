# =============================================================================
#  CMakeLists.txt  —  Kcode 한글 프로그래밍 언어 빌드 시스템
#  version : v30.0.0  (메인 버전 상속)
#
#  빌드 방법:
#    mkdir build && cd build
#    cmake ..                              # 기본 빌드 (인터프리터 + C생성기)
#    cmake .. -DKCODE_LLVM=ON              # LLVM 백엔드 포함
#    cmake .. -DKCODE_SERVER=ON            # HTTP 서버 포함
#    cmake .. -DKCODE_ONTOLOGY=ON          # 온톨로지 공유 라이브러리 빌드
#    cmake .. -DKCODE_CI=ON               # Concept Identity 레이어 포함
#    cmake .. -DKCODE_KBANK=ON            # KACP + 지식뱅크 레이어 포함
#    cmake .. -DKCODE_DNA=ON             # DNA 뉴런 레이어 포함
#    cmake .. -DKCODE_FULL=ON            # 전체 레이어 (CI+KBANK+DNA+ONT) 포함
#    cmake .. -DBUILD_SHARED_LIBS=ON       # 공유 라이브러리(.so/.dll) 빌드
#    cmake .. -DCMAKE_BUILD_TYPE=Release   # 릴리즈 빌드
#    cmake --build .
#    cmake --install . --prefix /usr/local
#
#  CMake 최소 버전 : 3.16
#  지원 플랫폼     : Linux, macOS, Windows (MinGW / MSVC)
#  MIT License / zerojat7
# =============================================================================

cmake_minimum_required(VERSION 3.16)

project(Kcode
    VERSION       30.0.0
    DESCRIPTION   "한글 프로그래밍 언어 — Kcode"
    HOMEPAGE_URL  "https://github.com/zerojat7/kcode"
    LANGUAGES     C
)

# =============================================================================
#  전역 C 표준
# =============================================================================
set(CMAKE_C_STANDARD          11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS        OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "빌드 타입 선택 (Debug/Release/RelWithDebInfo/MinSizeRel)" FORCE)
endif()

# =============================================================================
#  빌드 옵션
# =============================================================================
option(KCODE_LLVM        "LLVM IR 백엔드 포함 빌드"                    OFF)
option(KCODE_SERVER      "로컬 HTTP 서버(kserver) 빌드"                 ON)
option(KCODE_ONTOLOGY    "온톨로지 시스템 공유 라이브러리 빌드"           OFF)
option(KCODE_CI          "Concept Identity 레이어 포함"                  ON)
option(KCODE_KBANK       "KACP + 지식뱅크 레이어 포함"                   ON)
option(KCODE_DNA         "DNA 뉴런 레이어 포함"                          ON)
option(KCODE_FULL        "전체 레이어 빌드 (CI+KBANK+DNA+ONT)"           OFF)
option(KCODE_TESTS       "단위 테스트 빌드 및 CTest 등록"                ON)
option(BUILD_SHARED_LIBS "공유 라이브러리(.so/.dll)로 빌드"              OFF)

# KCODE_FULL → 모든 레이어 강제 활성화
if(KCODE_FULL)
    set(KCODE_CI      ON)
    set(KCODE_KBANK   ON)
    set(KCODE_DNA     ON)
    set(KCODE_ONTOLOGY ON)
endif()

# =============================================================================
#  소스 경로 자동 감지
# =============================================================================
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

if(EXISTS "${ROOT_DIR}/src/lexer/klexer.c")
    set(LEXER_DIR   "${ROOT_DIR}/src/lexer")
    set(PARSER_DIR  "${ROOT_DIR}/src/parser")
    set(INTERP_DIR  "${ROOT_DIR}/src/interpreter")
    set(CODEGEN_DIR "${ROOT_DIR}/src/codegen")
    set(SERVER_DIR  "${ROOT_DIR}/src/server")
    set(TEST_DIR    "${ROOT_DIR}/tests/lexer_test")
    set(KCODE_LAYOUT "modular")
elseif(EXISTS "${ROOT_DIR}/src/klexer.c")
    set(LEXER_DIR   "${ROOT_DIR}/src")
    set(PARSER_DIR  "${ROOT_DIR}/src")
    set(INTERP_DIR  "${ROOT_DIR}/src")
    set(CODEGEN_DIR "${ROOT_DIR}/src")
    set(SERVER_DIR  "${ROOT_DIR}/src")
    set(TEST_DIR    "${ROOT_DIR}/tests")
    set(KCODE_LAYOUT "src-flat")
else()
    set(LEXER_DIR   "${ROOT_DIR}")
    set(PARSER_DIR  "${ROOT_DIR}")
    set(INTERP_DIR  "${ROOT_DIR}")
    set(CODEGEN_DIR "${ROOT_DIR}")
    set(SERVER_DIR  "${ROOT_DIR}")
    set(TEST_DIR    "${ROOT_DIR}")
    set(KCODE_LAYOUT "root-flat")
endif()

message(STATUS "[Kcode] 소스 레이아웃: ${KCODE_LAYOUT}")

# include 경로 목록
set(KCODE_INCLUDE_DIRS
    "${LEXER_DIR}"
    "${PARSER_DIR}"
    "${INTERP_DIR}"
    "${CODEGEN_DIR}"
    "${ROOT_DIR}"
)
list(REMOVE_DUPLICATES KCODE_INCLUDE_DIRS)

# =============================================================================
#  레이어 0 — 코어 (렉서 + 파서)
# =============================================================================
set(LAYER0_SOURCES
    "${LEXER_DIR}/klexer.c"
    "${PARSER_DIR}/kparser.c"
)

# =============================================================================
#  레이어 1 — 기본 런타임 (GC + 텐서 + 자동미분 + MCP)
# =============================================================================
set(LAYER1_SOURCES
    "${ROOT_DIR}/kgc.c"
    "${ROOT_DIR}/kc_tensor.c"
    "${ROOT_DIR}/kc_autograd.c"
    "${ROOT_DIR}/kc_mcp.c"
)

# =============================================================================
#  레이어 2 — Concept Identity (v30.0.0, -DKCODE_CI=ON)
# =============================================================================
set(LAYER2_SOURCES "")
if(KCODE_CI)
    set(LAYER2_SOURCES
        "${ROOT_DIR}/kc_trace.c"
        "${ROOT_DIR}/kc_state_log.c"
        "${ROOT_DIR}/kc_vec.c"
        "${ROOT_DIR}/kc_vec_recon.c"
        "${ROOT_DIR}/kc_learn.c"
        "${ROOT_DIR}/kc_metric.c"
        "${ROOT_DIR}/kc_op_def.c"
    )
    message(STATUS "[Kcode] Concept Identity 레이어 : ON (7 소스)")
endif()

# =============================================================================
#  레이어 3 — 온톨로지 + 명칭 + CKR  (별도 라이브러리 또는 인라인)
# =============================================================================
set(LAYER3_SOURCES
    "${ROOT_DIR}/kc_ontology.c"
    "${ROOT_DIR}/kc_ont_query.c"
    "${ROOT_DIR}/kc_ont_import.c"
    "${ROOT_DIR}/kc_ont_server.c"
    "${ROOT_DIR}/kc_ont_remote.c"
    "${ROOT_DIR}/kc_ont_label.c"
    "${ROOT_DIR}/kc_ckr.c"
    "${ROOT_DIR}/kc_ckr_cache.c"
)

# =============================================================================
#  레이어 4 — KACP + 지식뱅크 (v30.0.0, -DKCODE_KBANK=ON)
# =============================================================================
set(LAYER4_SOURCES "")
if(KCODE_KBANK)
    set(LAYER4_SOURCES
        "${ROOT_DIR}/kacp.c"
        "${ROOT_DIR}/kc_kbank.c"
        "${ROOT_DIR}/kc_kbank_proof.c"
        "${ROOT_DIR}/kc_kbank_merge.c"
    )
    message(STATUS "[Kcode] KACP + 지식뱅크 레이어 : ON (4 소스)")
endif()

# =============================================================================
#  레이어 5 — DNA 뉴런 (v30.0.0, -DKCODE_DNA=ON)
# =============================================================================
set(LAYER5_SOURCES "")
if(KCODE_DNA)
    set(LAYER5_SOURCES
        "${ROOT_DIR}/kc_dna_neuron.c"
        "${ROOT_DIR}/kc_dna_ont.c"
        "${ROOT_DIR}/kc_neuron.c"
        "${ROOT_DIR}/kc_neuron_persist.c"
    )
    message(STATUS "[Kcode] DNA 뉴런 레이어        : ON (4 소스)")
endif()

# =============================================================================
#  레이어 6 — 안전·계약·서버 스폰·사망 인증 (항상 포함)
# =============================================================================
set(LAYER6_SOURCES
    "${ROOT_DIR}/kc_contract_check.c"
    "${ROOT_DIR}/kc_death_cert.c"
    "${ROOT_DIR}/kc_server_spawn.c"
)

# =============================================================================
#  핵심 라이브러리 소스 (전체 레이어 통합)
# =============================================================================
set(KCODE_CORE_SOURCES
    ${LAYER0_SOURCES}
    ${LAYER1_SOURCES}
    ${LAYER2_SOURCES}
    ${LAYER4_SOURCES}
    ${LAYER5_SOURCES}
    ${LAYER6_SOURCES}
    "${INTERP_DIR}/kinterp.c"
)

# =============================================================================
#  핵심 라이브러리: libkcode
# =============================================================================
add_library(libkcode ${KCODE_CORE_SOURCES})

set_target_properties(libkcode PROPERTIES
    OUTPUT_NAME               kcode
    VERSION                   "${PROJECT_VERSION}"
    SOVERSION                 "${PROJECT_VERSION_MAJOR}"
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET       hidden
)

target_include_directories(libkcode
    PUBLIC
        "$<BUILD_INTERFACE:${LEXER_DIR}>"
        "$<BUILD_INTERFACE:${PARSER_DIR}>"
        "$<BUILD_INTERFACE:${INTERP_DIR}>"
        "$<BUILD_INTERFACE:${CODEGEN_DIR}>"
        "$<BUILD_INTERFACE:${ROOT_DIR}>"
        "$<INSTALL_INTERFACE:include/kcode>"
)

target_compile_options(libkcode PRIVATE
    "$<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
    "$<$<C_COMPILER_ID:MSVC>:/W4;/utf-8>"
    "$<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-O2>"
    "$<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2>"
)

find_library(MATH_LIB m)
if(MATH_LIB)
    target_link_libraries(libkcode PUBLIC "${MATH_LIB}")
endif()

# CI 레이어 컴파일 정의
if(KCODE_CI)
    target_compile_definitions(libkcode PUBLIC KCODE_CI=1)
endif()
if(KCODE_KBANK)
    target_compile_definitions(libkcode PUBLIC KCODE_KBANK=1)
endif()
if(KCODE_DNA)
    target_compile_definitions(libkcode PUBLIC KCODE_DNA=1)
endif()

if(WIN32 AND BUILD_SHARED_LIBS)
    target_compile_definitions(libkcode
        PRIVATE   KCODE_BUILD_DLL
        INTERFACE KCODE_USE_DLL
    )
endif()

# =============================================================================
#  LLVM 백엔드 (-DKCODE_LLVM=ON)
# =============================================================================
if(KCODE_LLVM)
    find_package(LLVM REQUIRED CONFIG)
    message(STATUS "[Kcode] LLVM 버전: ${LLVM_PACKAGE_VERSION}")

    llvm_map_components_to_libnames(LLVM_LIBS
        core analysis native bitwriter support)

    # LLVM 분리 소스 (v30.0.0: 4개 분리 파일 추가)
    set(LLVM_CODEGEN_SOURCES
        "${CODEGEN_DIR}/kcodegen.c"
        "${CODEGEN_DIR}/kcodegen_core.c"
        "${CODEGEN_DIR}/kcodegen_expr.c"
        "${CODEGEN_DIR}/kcodegen_stmt.c"
        "${CODEGEN_DIR}/kcodegen_func.c"
        "${CODEGEN_DIR}/kcodegen_llvm.c"
        "${CODEGEN_DIR}/kcodegen_llvm_expr.c"
        "${CODEGEN_DIR}/kcodegen_llvm_stmt.c"
        "${CODEGEN_DIR}/kcodegen_llvm_builtin.c"
        "${CODEGEN_DIR}/kcodegen_llvm_util.c"
    )

    add_library(libkcode_llvm ${LLVM_CODEGEN_SOURCES})
    set_target_properties(libkcode_llvm PROPERTIES
        OUTPUT_NAME               kcode_llvm_engine
        VERSION                   "${PROJECT_VERSION}"
        SOVERSION                 "${PROJECT_VERSION_MAJOR}"
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(libkcode_llvm
        PUBLIC
            "$<BUILD_INTERFACE:${CODEGEN_DIR}>"
            "$<BUILD_INTERFACE:${ROOT_DIR}>"
            "$<INSTALL_INTERFACE:include/kcode>"
            "${LLVM_INCLUDE_DIRS}"
    )
    target_compile_definitions(libkcode_llvm PUBLIC
        KCODE_LLVM=1 "${LLVM_DEFINITIONS}")
    target_link_libraries(libkcode_llvm PUBLIC libkcode ${LLVM_LIBS})
endif()

# =============================================================================
#  실행파일
# =============================================================================

# kcode — 인터프리터
add_executable(kcode "${INTERP_DIR}/kinterp_main.c")
target_link_libraries(kcode PRIVATE libkcode)
target_include_directories(kcode PRIVATE ${KCODE_INCLUDE_DIRS})

# kcode_gen — C 코드 생성기
#add_executable(kcode_gen "${CODEGEN_DIR}/kcodegen_main.c")
#target_link_libraries(kcode_gen PRIVATE libkcode)
#target_include_directories(kcode_gen PRIVATE ${KCODE_INCLUDE_DIRS})
#target_sources(kcode_gen PRIVATE
#    "${CODEGEN_DIR}/kcodegen.c"
#    "${CODEGEN_DIR}/kcodegen_core.c"
#    "${CODEGEN_DIR}/kcodegen_expr.c"
#    "${CODEGEN_DIR}/kcodegen_stmt.c"
#    "${CODEGEN_DIR}/kcodegen_func.c"
#)

# kcode_llvm — LLVM 드라이버
if(KCODE_LLVM)
    add_executable(kcode_llvm "${CODEGEN_DIR}/kcodegen_llvm_main.c")
    target_link_libraries(kcode_llvm PRIVATE libkcode_llvm libkcode)
    target_include_directories(kcode_llvm PRIVATE ${KCODE_INCLUDE_DIRS})
    target_compile_definitions(kcode_llvm PRIVATE KCODE_LLVM=1)
endif()

# kbc — 바이트코드 VM
add_executable(kbc
    "${LEXER_DIR}/klexer.c"
    "${PARSER_DIR}/kparser.c"
    "${ROOT_DIR}/kc_bytecode.c"
    "${ROOT_DIR}/kc_bcgen.c"
    "${ROOT_DIR}/kgc.c"
    "${ROOT_DIR}/kc_vm.c"
    "${ROOT_DIR}/kbc_main.c"
)
target_include_directories(kbc PRIVATE ${KCODE_INCLUDE_DIRS})
target_compile_options(kbc PRIVATE
    "$<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra>")
if(MATH_LIB)
    target_link_libraries(kbc PRIVATE "${MATH_LIB}")
endif()

# kserver — HTTP 서버
if(KCODE_SERVER)
    add_executable(kserver_bin
        "${SERVER_DIR}/kserver.c"
    )
    target_link_libraries(kserver_bin PRIVATE libkcode)
    target_include_directories(kserver_bin PRIVATE ${KCODE_INCLUDE_DIRS})
    if(UNIX)
        target_link_libraries(kserver_bin PRIVATE pthread)
    endif()
    if(WIN32)
        target_link_libraries(kserver_bin PRIVATE ws2_32)
    endif()
    message(STATUS "[Kcode] HTTP 서버 빌드 : ON  → kserver_bin")
endif()

# =============================================================================
#  온톨로지 공유 라이브러리: libkc_ontology (-DKCODE_ONTOLOGY=ON)
# =============================================================================
if(KCODE_ONTOLOGY)

    set(KC_ONT_SOURCES ${LAYER3_SOURCES})

    set(KC_ONT_HEADERS
        "${ROOT_DIR}/kc_ontology.h"
        "${ROOT_DIR}/kc_ont_query.h"
        "${ROOT_DIR}/kc_ont_import.h"
        "${ROOT_DIR}/kc_ont_server.h"
        "${ROOT_DIR}/kc_ont_remote.h"
        "${ROOT_DIR}/kc_ont_label.h"
        "${ROOT_DIR}/kc_ckr.h"
        "${ROOT_DIR}/kc_ckr_cache.h"
        "${ROOT_DIR}/kc_ontology_label_addon.h"
    )

    add_library(kc_ontology ${KC_ONT_SOURCES})
    set_target_properties(kc_ontology PROPERTIES
        OUTPUT_NAME               kc_ontology
        VERSION                   "${PROJECT_VERSION}"
        SOVERSION                 "${PROJECT_VERSION_MAJOR}"
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET       default
    )
    target_include_directories(kc_ontology
        PUBLIC
            "$<BUILD_INTERFACE:${ROOT_DIR}>"
            "$<INSTALL_INTERFACE:include/kcode>"
    )
    target_compile_options(kc_ontology PRIVATE
        "$<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra>"
        "$<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-O2>"
        "$<$<C_COMPILER_ID:MSVC>:/W4;/utf-8>"
    )
    target_link_libraries(kc_ontology PUBLIC libkcode)
    if(UNIX)
        target_link_libraries(kc_ontology PRIVATE pthread)
    endif()
    if(WIN32)
        target_link_libraries(kc_ontology PRIVATE ws2_32)
    endif()
    if(MATH_LIB)
        target_link_libraries(kc_ontology PRIVATE "${MATH_LIB}")
    endif()
    if(WIN32 AND BUILD_SHARED_LIBS)
        target_compile_definitions(kc_ontology
            PRIVATE   KC_ONT_BUILD_DLL
            INTERFACE KC_ONT_USE_DLL)
    endif()

    install(TARGETS kc_ontology
        EXPORT      KcodeTargets
        LIBRARY     DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE     DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME     DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
    install(FILES ${KC_ONT_HEADERS}
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kcode")

    if(EXISTS "${ROOT_DIR}/kc_ont_sdk.py")
        install(FILES "${ROOT_DIR}/kc_ont_sdk.py"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/kcode/sdk/python")
    endif()
    if(EXISTS "${ROOT_DIR}/kc_ont_sdk.js")
        install(FILES "${ROOT_DIR}/kc_ont_sdk.js"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/kcode/sdk/nodejs")
    endif()

    message(STATUS "[Kcode] 온톨로지 라이브러리 : ON  → libkc_ontology")

endif()

# =============================================================================
#  테스트 빌드 (v30.0.0 — 통합 테스트 6종 추가)
# =============================================================================
if(KCODE_TESTS)
    include(CTest)

    # 렉서 단위 테스트
    add_executable(ktest_lexer
        "${LEXER_DIR}/klexer.c"
        "${TEST_DIR}/ktest_lexer.c"
    )
    target_include_directories(ktest_lexer PRIVATE "${LEXER_DIR}" "${ROOT_DIR}")
    if(MATH_LIB)
        target_link_libraries(ktest_lexer PRIVATE "${MATH_LIB}")
    endif()
    add_test(NAME lexer_test COMMAND ktest_lexer)

    # 헬퍼: 통합 테스트용 공통 소스 (libkcode 인라인 통합)
    set(FULL_TEST_LINK libkcode)
    if(UNIX)
        list(APPEND FULL_TEST_LINK pthread)
    endif()
    if(MATH_LIB)
        list(APPEND FULL_TEST_LINK "${MATH_LIB}")
    endif()

    # 온톨로지 통합 테스트
    add_executable(ktest_ont "${ROOT_DIR}/kc_ont_test.c" ${LAYER3_SOURCES})
    target_link_libraries(ktest_ont PRIVATE ${FULL_TEST_LINK})
    target_include_directories(ktest_ont PRIVATE ${KCODE_INCLUDE_DIRS})
    add_test(NAME ont_test COMMAND ktest_ont)

    # Concept Identity 통합 테스트
    if(KCODE_CI)
        add_executable(ktest_ci "${ROOT_DIR}/kc_ci_test.c")
        target_link_libraries(ktest_ci PRIVATE ${FULL_TEST_LINK})
        target_include_directories(ktest_ci PRIVATE ${KCODE_INCLUDE_DIRS})
        add_test(NAME ci_test COMMAND ktest_ci)
    endif()

    # 지식뱅크 통합 테스트
    if(KCODE_KBANK)
        add_executable(ktest_kbank "${ROOT_DIR}/kc_kbank_test.c")
        target_link_libraries(ktest_kbank PRIVATE ${FULL_TEST_LINK})
        target_include_directories(ktest_kbank PRIVATE ${KCODE_INCLUDE_DIRS})
        add_test(NAME kbank_test COMMAND ktest_kbank)
    endif()

    # 명칭 레이어 통합 테스트
    add_executable(ktest_label "${ROOT_DIR}/kc_label_test.c" ${LAYER3_SOURCES})
    target_link_libraries(ktest_label PRIVATE ${FULL_TEST_LINK})
    target_include_directories(ktest_label PRIVATE ${KCODE_INCLUDE_DIRS})
    add_test(NAME label_test COMMAND ktest_label)

    # 뉴런 통합 테스트
    if(KCODE_DNA)
        add_executable(ktest_neuron "${ROOT_DIR}/kc_neuron_test.c")
        target_link_libraries(ktest_neuron PRIVATE ${FULL_TEST_LINK})
        target_include_directories(ktest_neuron PRIVATE ${KCODE_INCLUDE_DIRS})
        add_test(NAME neuron_test COMMAND ktest_neuron)
    endif()

    message(STATUS "[Kcode] 단위 테스트 : ON  (6종)")
endif()

# =============================================================================
#  WebAssembly 빌드 타겟 (v16.5.0 ~)
# =============================================================================
option(BUILD_WASM      "WebAssembly 빌드 활성화 (Emscripten 필요)" OFF)
option(BUILD_WASM_FULL "풀 엔진 WASM 빌드"                         OFF)

if(BUILD_WASM)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "WASM 빌드는 Emscripten으로 실행해야 합니다.\n"
            "  emcmake cmake .. -DBUILD_WASM=ON\n"
            "  emmake make kcode_vm")
    endif()

    add_executable(kcode_vm
        "${LEXER_DIR}/klexer.c"
        "${PARSER_DIR}/kparser.c"
        "${ROOT_DIR}/kc_bytecode.c"
        "${ROOT_DIR}/kc_bcgen.c"
        "${ROOT_DIR}/kgc.c"
        "${ROOT_DIR}/kc_vm.c"
        "${ROOT_DIR}/kc_wasm_api.c"
    )
    target_compile_definitions(kcode_vm PRIVATE KCODE_WASM_BUILD=1)
    target_compile_options(kcode_vm PRIVATE -O2)
    target_include_directories(kcode_vm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(kcode_vm PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS "\
            -s WASM=1 \
            -s EXPORTED_FUNCTIONS='[\"_kc_wasm_run\",\"_kc_wasm_compile\",\"_kc_wasm_exec_bc\",\"_kc_wasm_lex\",\"_kc_wasm_parse\",\"_kc_wasm_version\",\"_kc_wasm_free\"]' \
            -s EXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\"]' \
            -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 \
            -s EXPORT_NAME='KcodeVM' -s FILESYSTEM=0 --closure 1"
    )
    message(STATUS "  WASM KVM   : ON  → kcode_vm.wasm + kcode_vm.js")

    if(BUILD_WASM_FULL)
        add_executable(kcode_full
            "${LEXER_DIR}/klexer.c"
            "${PARSER_DIR}/kparser.c"
            "${ROOT_DIR}/kc_bytecode.c"
            "${ROOT_DIR}/kc_bcgen.c"
            "${ROOT_DIR}/kgc.c"
            "${ROOT_DIR}/kc_vm.c"
            "${INTERP_DIR}/kinterp.c"
            "${ROOT_DIR}/kc_tensor.c"
            "${ROOT_DIR}/kc_autograd.c"
            "${ROOT_DIR}/kc_mcp.c"
            "${ROOT_DIR}/kc_wasm_api.c"
        )
        target_compile_definitions(kcode_full PRIVATE KCODE_WASM_BUILD=1)
        target_compile_options(kcode_full PRIVATE -O2)
        target_include_directories(kcode_full PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        set_target_properties(kcode_full PROPERTIES
            SUFFIX ".js"
            LINK_FLAGS "\
                -s WASM=1 \
                -s EXPORTED_FUNCTIONS='[\"_kc_wasm_run\",\"_kc_wasm_compile\",\"_kc_wasm_exec_bc\",\"_kc_wasm_lex\",\"_kc_wasm_parse\",\"_kc_wasm_version\",\"_kc_wasm_free\"]' \
                -s EXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\"]' \
                -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 \
                -s EXPORT_NAME='KcodeEngine' -s FILESYSTEM=0 --closure 1"
        )
        message(STATUS "  WASM Full  : ON  → kcode_full.wasm + kcode_full.js")
    endif()
endif()

# =============================================================================
#  설치 규칙
# =============================================================================
include(GNUInstallDirs)

install(TARGETS kcode kcode_gen kbc
    EXPORT      KcodeTargets
    RUNTIME     DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES
    "${LEXER_DIR}/klexer.h"
    "${PARSER_DIR}/kparser.h"
    "${INTERP_DIR}/kinterp.h"
    "${CODEGEN_DIR}/kcodegen.h"
    "${ROOT_DIR}/kgc.h"
    "${ROOT_DIR}/kc_tensor.h"
    "${ROOT_DIR}/kc_autograd.h"
    "${ROOT_DIR}/kc_mcp.h"
    "${ROOT_DIR}/kc_trace.h"
    "${ROOT_DIR}/kc_state_log.h"
    "${ROOT_DIR}/kc_vec.h"
    "${ROOT_DIR}/kc_vec_recon.h"
    "${ROOT_DIR}/kc_learn.h"
    "${ROOT_DIR}/kc_metric.h"
    "${ROOT_DIR}/kc_op_def.h"
    "${ROOT_DIR}/kacp.h"
    "${ROOT_DIR}/kc_kbank.h"
    "${ROOT_DIR}/kc_kbank_proof.h"
    "${ROOT_DIR}/kc_kbank_merge.h"
    "${ROOT_DIR}/kc_dna_neuron.h"
    "${ROOT_DIR}/kc_dna_ont.h"
    "${ROOT_DIR}/kc_neuron.h"
    "${ROOT_DIR}/kc_neuron_persist.h"
    "${ROOT_DIR}/kc_contract_check.h"
    "${ROOT_DIR}/kc_death_cert.h"
    "${ROOT_DIR}/kc_server_spawn.h"
    "${ROOT_DIR}/kc_bytecode.h"
    "${ROOT_DIR}/kc_bcgen.h"
    "${ROOT_DIR}/kc_vm.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kcode"
)

# =============================================================================
#  CMake 패키지 설정
# =============================================================================
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/KcodeConfigVersion.cmake"
    VERSION       "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

# =============================================================================
#  빌드 요약
# =============================================================================
message(STATUS "")
message(STATUS "======================================")
message(STATUS "  Kcode v${PROJECT_VERSION} 빌드 설정")
message(STATUS "  플랫폼        : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  컴파일러      : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  빌드 타입     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  라이브러리    : BUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}")
message(STATUS "  LLVM 백엔드   : ${KCODE_LLVM}")
message(STATUS "  HTTP 서버     : ${KCODE_SERVER}")
message(STATUS "  온톨로지 SDK  : ${KCODE_ONTOLOGY}")
message(STATUS "  CI 레이어     : ${KCODE_CI}")
message(STATUS "  KACP+지식뱅크 : ${KCODE_KBANK}")
message(STATUS "  DNA 뉴런      : ${KCODE_DNA}")
message(STATUS "  단위 테스트   : ${KCODE_TESTS}")
message(STATUS "  레이아웃      : ${KCODE_LAYOUT}")
message(STATUS "  설치 경로     : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "======================================")
message(STATUS "")
