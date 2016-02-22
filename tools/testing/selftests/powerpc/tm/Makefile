TEST_PROGS := tm-resched-dscr tm-syscall tm-signal-msr-resv tm-signal-stack tm-vmxcopy

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c ../utils.c

tm-syscall: tm-syscall-asm.S
tm-syscall: CFLAGS += -mhtm -I../../../../../usr/include

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
