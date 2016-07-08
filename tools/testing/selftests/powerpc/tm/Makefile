TEST_PROGS := tm-resched-dscr tm-syscall tm-signal-msr-resv tm-signal-stack tm-vmxcopy tm-fork tm-tar tm-tmspr

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c ../utils.c

tm-syscall: tm-syscall-asm.S
tm-syscall: CFLAGS += -mhtm -I../../../../../usr/include
tm-tmspr: CFLAGS += -pthread

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
