# =============================================================================
#  Kcode v30.0.0 — Makefile
#  빌드 타겟:
#    make              — 인터프리터 풀빌드 (kcode)
#    make interp       — 인터프리터 빌드 (kcode)
#    make gen          — C 코드 생성기 빌드 (kcode_gen)
#    make kvm          — 바이트코드 VM 빌드 (kbc)
#    make server       — HTTP 서버 빌드 (kserver_bin)
#    make alltest      — 전체 통합 테스트 빌드 & 실행
#    make test         — 렉서 단위 테스트
#    make all          — 전체 빌드 (interp + gen + kvm + server)
#    make llvm-check   — LLVM 설치 여부 확인
#    make clean        — 빌드 결과물 삭제
#
#  LLVM 연동:
#    make LLVM=1       — LLVM 백엔드 포함 빌드 (llvm-config 필요)
#
#  MIT License / zerojat7
# =============================================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -g
LDFLAGS  = -lm

# =============================================================================
#  레이어 0 — 코어 (렉서 + 파서)
# =============================================================================
LAYER0_SRC = klexer.c kparser.c

# =============================================================================
#  레이어 1 — 기본 런타임 (GC + 텐서 + 자동미분 + MCP)
# =============================================================================
LAYER1_SRC = kgc.c kc_tensor.c kc_autograd.c kc_mcp.c

# =============================================================================
#  레이어 2 — Concept Identity (추적·상태로그·벡터·학습·메트릭·연산정의)
# =============================================================================
LAYER2_SRC = kc_trace.c kc_state_log.c \
             kc_vec.c kc_vec_recon.c \
             kc_learn.c kc_metric.c \
             kc_op_def.c

# =============================================================================
#  레이어 3 — 온톨로지 + 명칭 레이어 + CKR
# =============================================================================
LAYER3_SRC = kc_ontology.c kc_ont_query.c kc_ont_import.c \
             kc_ont_server.c kc_ont_remote.c kc_ont_label.c \
             kc_ckr.c kc_ckr_cache.c

# =============================================================================
#  레이어 4 — KACP + 지식 뱅크
# =============================================================================
LAYER4_SRC = kacp.c \
             kc_kbank.c kc_kbank_proof.c kc_kbank_merge.c

# =============================================================================
#  레이어 5 — DNA 뉴런
# =============================================================================
LAYER5_SRC = kc_dna_neuron.c kc_dna_ont.c \
             kc_neuron.c kc_neuron_persist.c

# =============================================================================
#  레이어 6 — 안전·계약·서버 스폰·사망 인증
# =============================================================================
LAYER6_SRC = kc_contract_check.c kc_death_cert.c kc_server_spawn.c

# =============================================================================
#  인터프리터 타겟 (전체 레이어 통합)
# =============================================================================
INTERP_LAYERS = $(LAYER0_SRC) $(LAYER1_SRC) $(LAYER2_SRC) \
                $(LAYER3_SRC) $(LAYER4_SRC) $(LAYER5_SRC) $(LAYER6_SRC)

INTERP_SRC    = $(INTERP_LAYERS) kinterp.c kinterp_main.c
INTERP_BIN    = kcode

INTERP_HEADERS = klexer.h kparser.h kinterp.h kgc.h \
                 kc_tensor.h kc_autograd.h kc_mcp.h \
                 kc_trace.h kc_state_log.h \
                 kc_vec.h kc_vec_recon.h \
                 kc_learn.h kc_metric.h kc_op_def.h \
                 kc_ontology.h kc_ont_query.h kc_ont_import.h \
                 kc_ont_server.h kc_ont_remote.h kc_ontology_label_addon.h \
                 kc_ckr.h kc_ckr_cache.h \
                 kacp.h kc_kbank.h kc_kbank_proof.h kc_kbank_merge.h \
                 kc_dna_neuron.h kc_dna_ont.h \
                 kc_neuron.h kc_neuron_persist.h \
                 kc_contract_check.h kc_death_cert.h kc_server_spawn.h

# =============================================================================
#  C 코드 생성기 타겟
# =============================================================================
GEN_SRC  = $(LAYER0_SRC) \
           kcodegen.c kcodegen_core.c kcodegen_expr.c \
           kcodegen_stmt.c kcodegen_func.c kcodegen_main.c
GEN_BIN  = kcode_gen

# =============================================================================
#  KVM 바이트코드 VM 타겟
# =============================================================================
KVM_SRC  = $(LAYER0_SRC) kc_bytecode.c kc_bcgen.c kc_vm.c kbc_main.c
KVM_BIN  = kbc

# =============================================================================
#  HTTP 서버 타겟
# =============================================================================
SRV_SRC      = $(INTERP_LAYERS) kinterp.c kserver.c
SRV_BIN      = kserver_bin
SRV_LDFLAGS  = $(LDFLAGS)
ifeq ($(shell uname), Linux)
    SRV_LDFLAGS += -lpthread
endif

# =============================================================================
#  렉서 단위 테스트
# =============================================================================
TEST_SRC = klexer.c ktest_lexer.c
TEST_BIN = ktest_lexer

# =============================================================================
#  통합 테스트 타겟 (v30.0.0 신규)
# =============================================================================
ONT_TEST_SRC    = $(INTERP_LAYERS) kinterp.c kc_ont_test.c
CI_TEST_SRC     = $(INTERP_LAYERS) kinterp.c kc_ci_test.c
KBANK_TEST_SRC  = $(INTERP_LAYERS) kinterp.c kc_kbank_test.c
LABEL_TEST_SRC  = $(INTERP_LAYERS) kinterp.c kc_label_test.c
NEURON_TEST_SRC = $(INTERP_LAYERS) kinterp.c kc_neuron_test.c

# =============================================================================
#  LLVM 옵션 (make LLVM=1 로 활성화)
# =============================================================================
LLVM ?= 0

ifeq ($(LLVM), 1)
  LLVM_CONFIG := $(shell command -v llvm-config 2>/dev/null)
  ifeq ($(LLVM_CONFIG),)
    $(warning [경고] llvm-config 를 찾을 수 없습니다. LLVM 없이 빌드합니다.)
    LLVM := 0
  else
    LLVM_CFLAGS  := $(shell llvm-config --cflags)
    LLVM_LDFLAGS := $(shell llvm-config --ldflags --libs core analysis native bitwriter)
    CFLAGS       += $(LLVM_CFLAGS) -DKCODE_LLVM=1
    LDFLAGS      += $(LLVM_LDFLAGS)
    $(info [정보] LLVM 연동 활성화: $(shell llvm-config --version))
  endif
endif

LLVM_GEN_SRC = $(LAYER0_SRC) \
               kcodegen.c kcodegen_core.c kcodegen_expr.c \
               kcodegen_stmt.c kcodegen_func.c \
               kcodegen_llvm.c kcodegen_llvm_expr.c \
               kcodegen_llvm_stmt.c kcodegen_llvm_builtin.c \
               kcodegen_llvm_util.c kcodegen_llvm_main.c
LLVM_BIN = kcode_llvm

# =============================================================================
#  기본 타겟
# =============================================================================
.PHONY: all interp gen kvm server test alltest llvm-check clean help

all: interp gen kvm server
	@echo ""
	@echo "========================================="
	@echo "  Kcode v30.0.0 빌드 완료"
	@echo "    인터프리터  : ./$(INTERP_BIN)"
	@echo "    C 생성기    : ./$(GEN_BIN)"
	@echo "    바이트코드VM: ./$(KVM_BIN)"
	@echo "    HTTP 서버   : ./$(SRV_BIN)"
	@echo "========================================="

# --- 인터프리터 (풀 레이어) --------------------------------------------------
interp: $(INTERP_BIN)

$(INTERP_BIN): $(INTERP_SRC) $(INTERP_HEADERS)
	$(CC) $(CFLAGS) -o $@ $(INTERP_SRC) $(LDFLAGS)
	@echo "[빌드 완료] 인터프리터 → $@"

# --- C 코드 생성기 ------------------------------------------------------------
gen: $(GEN_BIN)

$(GEN_BIN): $(GEN_SRC) klexer.h kparser.h kcodegen.h
	$(CC) $(CFLAGS) -o $@ $(GEN_SRC) $(LDFLAGS)
	@echo "[빌드 완료] C 코드 생성기 → $@"

# --- KVM 바이트코드 VM --------------------------------------------------------
kvm: $(KVM_BIN)

$(KVM_BIN): $(KVM_SRC) klexer.h kparser.h kc_bytecode.h kc_bcgen.h kc_vm.h
	$(CC) $(CFLAGS) -o $@ $(KVM_SRC) $(LDFLAGS)
	@echo "[빌드 완료] 바이트코드 VM → $@"

# --- HTTP 서버 ----------------------------------------------------------------
server: $(SRV_BIN)

$(SRV_BIN): $(SRV_SRC) $(INTERP_HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SRV_SRC) $(SRV_LDFLAGS)
	@echo "[빌드 완료] HTTP 서버 → $@"

# --- LLVM 생성기 (make LLVM=1 시 활성화) --------------------------------------
ifeq ($(LLVM), 1)
llvm: $(LLVM_BIN)

$(LLVM_BIN): $(LLVM_GEN_SRC) klexer.h kparser.h kcodegen.h kcodegen_llvm.h
	$(CC) $(CFLAGS) -o $@ $(LLVM_GEN_SRC) $(LDFLAGS)
	@echo "[빌드 완료] LLVM 생성기 → $@"
endif

# --- 렉서 단위 테스트 ---------------------------------------------------------
test: $(TEST_BIN)
	@echo ""
	@echo "=== Kcode 렉서 단위 테스트 실행 ==="
	-./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) klexer.h
	@sed 's|#include "../src/klexer.h"|#include "klexer.h"|g' ktest_lexer.c > _ktest_lexer_patched.c
	$(CC) $(CFLAGS) -o $@ klexer.c _ktest_lexer_patched.c $(LDFLAGS)
	@rm -f _ktest_lexer_patched.c
	@echo "[빌드 완료] 렉서 테스트 → $@"

# --- 통합 테스트 (v30.0.0 신규) ----------------------------------------------
alltest: test ktest_ont ktest_ci ktest_kbank ktest_label ktest_neuron
	@echo ""
	@echo "=== 전체 통합 테스트 실행 ==="
	-./ktest_lexer
	-./ktest_ont
	-./ktest_ci
	-./ktest_kbank
	-./ktest_label
	-./ktest_neuron
	@echo "=== 통합 테스트 완료 ==="

ktest_ont: $(ONT_TEST_SRC) $(INTERP_HEADERS)
	$(CC) $(CFLAGS) -o $@ $(ONT_TEST_SRC) $(SRV_LDFLAGS)
	@echo "[빌드 완료] 온톨로지 테스트 → $@"

ktest_ci: $(CI_TEST_SRC) $(INTERP_HEADERS)
	$(CC) $(CFLAGS) -o $@ $(CI_TEST_SRC) $(SRV_LDFLAGS)
	@echo "[빌드 완료] CI 테스트 → $@"

ktest_kbank: $(KBANK_TEST_SRC) $(INTERP_HEADERS)
	$(CC) $(CFLAGS) -o $@ $(KBANK_TEST_SRC) $(SRV_LDFLAGS)
	@echo "[빌드 완료] 지식뱅크 테스트 → $@"

ktest_label: $(LABEL_TEST_SRC) $(INTERP_HEADERS)
	$(CC) $(CFLAGS) -o $@ $(LABEL_TEST_SRC) $(SRV_LDFLAGS)
	@echo "[빌드 완료] 명칭 레이어 테스트 → $@"

ktest_neuron: $(NEURON_TEST_SRC) $(INTERP_HEADERS)
	$(CC) $(CFLAGS) -o $@ $(NEURON_TEST_SRC) $(SRV_LDFLAGS)
	@echo "[빌드 완료] 뉴런 테스트 → $@"

# =============================================================================
#  LLVM 설치 확인
# =============================================================================
llvm-check:
	@echo "--- LLVM 환경 확인 ---"
	@if command -v llvm-config >/dev/null 2>&1; then \
		echo "[OK] llvm-config 발견: $$(llvm-config --version)"; \
		echo "     경로  : $$(which llvm-config)"; \
		echo "  → LLVM 연동 빌드 방법:  make LLVM=1"; \
	else \
		echo "[없음] llvm-config 를 찾을 수 없습니다."; \
		echo "  설치 방법:"; \
		echo "    Ubuntu/Debian : sudo apt install llvm-dev"; \
		echo "    macOS (brew)  : brew install llvm"; \
	fi

# =============================================================================
#  정리
# =============================================================================
clean:
	rm -f $(INTERP_BIN) $(GEN_BIN) $(KVM_BIN) $(SRV_BIN) \
	      $(LLVM_BIN) $(TEST_BIN) \
	      ktest_ont ktest_ci ktest_kbank ktest_label ktest_neuron \
	      *.kbc _ktest_lexer_patched.c
	@echo "[정리 완료]"

# =============================================================================
#  도움말
# =============================================================================
help:
	@echo "Kcode v30.0.0 빌드 도움말"
	@echo ""
	@echo "  make              인터프리터 풀빌드 (kcode, 전체 레이어)"
	@echo "  make interp       인터프리터 빌드 (kcode)"
	@echo "  make gen          C 코드 생성기 빌드 (kcode_gen)"
	@echo "  make kvm          바이트코드 VM 빌드 (kbc)"
	@echo "  make server       HTTP 서버 빌드 (kserver_bin)"
	@echo "  make test         렉서 단위 테스트"
	@echo "  make alltest      전체 통합 테스트 (6종)"
	@echo "  make all          전체 빌드 (interp+gen+kvm+server)"
	@echo "  make llvm-check   LLVM 설치 여부 확인"
	@echo "  make LLVM=1       LLVM 백엔드 포함 빌드 (kcode_llvm)"
	@echo "  make clean        빌드 결과물 삭제"
	@echo "  make help         이 도움말"
	@echo ""
	@echo "  레이어 구성:"
	@echo "    L0 렉서+파서"
	@echo "    L1 GC + 텐서 + 자동미분 + MCP"
	@echo "    L2 Concept Identity (kc_trace/state_log/vec/vec_recon/learn/metric/op_def)"
	@echo "    L3 온톨로지 + 명칭 + CKR (8종)"
	@echo "    L4 KACP + 지식뱅크 (4종)"
	@echo "    L5 DNA 뉴런 (4종)"
	@echo "    L6 계약 + 안전 + 서버스폰 (3종)"
	@echo ""
	@echo "  KVM 사용 예:"
	@echo "    ./kbc compile 소스.han      → 소스.kbc 생성"
	@echo "    ./kbc run     소스.kbc      → 바이트코드 실행"
	@echo "    ./kbc exec    소스.han      → 컴파일+즉시실행"
	@echo "    ./kbc dump    소스.kbc      → 디스어셈블"
