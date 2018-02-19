# SPDX-License-Identifier: GPL-2.0
SIGNAL_CONTEXT_CHK_TESTS := tm-signal-context-chk-gpr tm-signal-context-chk-fpu \
	tm-signal-context-chk-vmx tm-signal-context-chk-vsx

TEST_GEN_PROGS := tm-resched-dscr tm-syscall tm-signal-msr-resv tm-signal-stack \
	tm-vmxcopy tm-fork tm-tar tm-tmspr tm-vmx-unavail tm-unavailable \
	$(SIGNAL_CONTEXT_CHK_TESTS)

include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c ../utils.c

CFLAGS += -mhtm

$(OUTPUT)/tm-syscall: tm-syscall-asm.S
$(OUTPUT)/tm-syscall: CFLAGS += -I../../../../../usr/include
$(OUTPUT)/tm-tmspr: CFLAGS += -pthread
$(OUTPUT)/tm-vmx-unavail: CFLAGS += -pthread -m64
$(OUTPUT)/tm-resched-dscr: ../pmu/lib.o
$(OUTPUT)/tm-unavailable: CFLAGS += -O0 -pthread -m64 -Wno-error=uninitialized -mvsx

SIGNAL_CONTEXT_CHK_TESTS := $(patsubst %,$(OUTPUT)/%,$(SIGNAL_CONTEXT_CHK_TESTS))
$(SIGNAL_CONTEXT_CHK_TESTS): tm-signal.S
$(SIGNAL_CONTEXT_CHK_TESTS): CFLAGS += -mhtm -m64 -mvsx
