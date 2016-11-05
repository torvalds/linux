SIGNAL_CONTEXT_CHK_TESTS := tm-signal-context-chk-gpr tm-signal-context-chk-fpu \
	tm-signal-context-chk-vmx tm-signal-context-chk-vsx

TEST_PROGS := tm-resched-dscr tm-syscall tm-signal-msr-resv tm-signal-stack \
	tm-vmxcopy tm-fork tm-tar tm-tmspr $(SIGNAL_CONTEXT_CHK_TESTS)

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c ../utils.c

CFLAGS += -mhtm

tm-syscall: tm-syscall-asm.S
tm-syscall: CFLAGS += -I../../../../../usr/include
tm-tmspr: CFLAGS += -pthread

$(SIGNAL_CONTEXT_CHK_TESTS): tm-signal.S
$(SIGNAL_CONTEXT_CHK_TESTS): CFLAGS += -mhtm -m64 -mvsx

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
