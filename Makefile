# =============================================================================
#  Kcode v31.0.0 — Makefile (공개용 / Public Build)
#
#  빌드 타겟:
#    make              — 전체 빌드 (gen + kvm + llvm-check)
#    make gen          — C 코드 생성기 빌드 (kcode_gen)
#    make kvm          — 바이트코드 VM 빌드 (kbc)
#    make test         — 렉서 단위 테스트
#    make all          — gen + kvm + test
#    make llvm-check   — LLVM 설치 여부 확인
#    make LLVM=1       — LLVM 백엔드 포함 빌드 (kcode_llvm)
#    make clean        — 빌드 결과물 삭제
#
#  참고: 인터프리터(kinterp)는 온톨로지 엔진 연동이 필요하여
#        전체 엔진 빌드에서 제외됩니다.
#        바이트코드 VM(kbc)으로 .han 파일을 실행하세요.
#
#  MIT License / zerojat7-ui
# =============================================================================

CC      = gcc
CFLAGS  = -Wall -std=c11 -g
LDFLAGS = -lm

# =============================================================================
#  레이어 0 — 코어 (렉서 + 파서)
# =============================================================================
LAYER0_SRC = klexer.c kparser.c

# =============================================================================
#  레이어 1 — 기본 런타임 (GC + 텐서 + 자동미분 + MCP)
# =============================================================================
LAYER1_SRC = kgc.c kc_tensor.c kc_autograd.c kc_mcp.c

# =============================================================================
#  C 코드 생성기 타겟
# =============================================================================
GEN_SRC = $(LAYER0_SRC) \
          kcodegen.c kcodegen_core.c kcodegen_expr.c \
          kcodegen_stmt.c kcodegen_func.c kcodegen_main.c
GEN_BIN = kcode_gen

# =============================================================================
#  KVM 바이트코드 VM 타겟
# =============================================================================
KVM_SRC = $(LAYER0_SRC) kc_bytecode.c kc_bcgen.c kgc.c kc_vm.c kbc_main.c
KVM_BIN = kbc

# =============================================================================
#  렉서 단위 테스트
# =============================================================================
TEST_SRC = klexer.c ktest_lexer.c
TEST_BIN = ktest_lexer

# =============================================================================
#  LLVM 옵션 (make LLVM=1)
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
.PHONY: all gen kvm test llvm-check clean help

all: gen kvm test
	@echo ""
	@echo "========================================="
	@echo "  Kcode v31.0.0 빌드 완료"
	@echo "    C 생성기    : ./$(GEN_BIN)"
	@echo "    바이트코드VM: ./$(KVM_BIN)"
	@echo "========================================="
	@echo "  .han 파일 실행:"
	@echo "    ./kbc exec 소스.han"
	@echo "========================================="

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

# --- LLVM 생성기 (make LLVM=1) ------------------------------------------------
ifeq ($(LLVM), 1)
llvm: $(LLVM_BIN)

$(LLVM_BIN): $(LLVM_GEN_SRC) klexer.h kparser.h kcodegen.h kcodegen_llvm.h
	$(CC) $(CFLAGS) -o $@ $(LLVM_GEN_SRC) $(LDFLAGS)
	@echo "[빌드 완료] LLVM 생성기 → $@"
endif

# --- 렉서 단위 테스트 ---------------------------------------------------------
test: $(TEST_BIN)
	@echo ""
	@echo "=== Kcode 렉서 단위 테스트 ==="
	-./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) klexer.h
	@sed 's|#include "../src/klexer.h"|#include "klexer.h"|g' ktest_lexer.c > _ktest_patched.c
	$(CC) $(CFLAGS) -o $@ klexer.c _ktest_patched.c $(LDFLAGS)
	@rm -f _ktest_patched.c
	@echo "[빌드 완료] 렉서 테스트 → $@"

# =============================================================================
#  LLVM 설치 확인
# =============================================================================
llvm-check:
	@echo "--- LLVM 환경 확인 ---"
	@if command -v llvm-config >/dev/null 2>&1; then \
		echo "[OK] llvm-config: $$(llvm-config --version)"; \
		echo "  → make LLVM=1 으로 빌드"; \
	else \
		echo "[없음] llvm-config 를 찾을 수 없습니다."; \
		echo "  Ubuntu/Debian : sudo apt install llvm-dev"; \
		echo "  macOS (brew)  : brew install llvm"; \
	fi

# =============================================================================
#  정리
# =============================================================================
clean:
	rm -f $(GEN_BIN) $(KVM_BIN) $(LLVM_BIN) $(TEST_BIN) \
	      *.kbc _ktest_patched.c
	@echo "[정리 완료]"

# =============================================================================
#  도움말
# =============================================================================
help:
	@echo "Kcode v31.0.0 빌드 도움말"
	@echo ""
	@echo "  make              전체 빌드 (gen + kvm + test)"
	@echo "  make gen          C 코드 생성기 (kcode_gen)"
	@echo "  make kvm          바이트코드 VM (kbc)"
	@echo "  make test         렉서 단위 테스트"
	@echo "  make LLVM=1       LLVM 백엔드 포함 빌드"
	@echo "  make llvm-check   LLVM 설치 확인"
	@echo "  make clean        빌드 결과물 삭제"
	@echo ""
	@echo "  .han 파일 실행:"
	@echo "    ./kbc exec   소스.han   컴파일 + 즉시 실행"
	@echo "    ./kbc compile 소스.han  → 소스.kbc 생성"
	@echo "    ./kbc run    소스.kbc   바이트코드 실행"
	@echo "    ./kbc dump   소스.kbc   디스어셈블"
