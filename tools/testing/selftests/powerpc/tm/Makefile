TEST_PROGS := tm-resched-dscr

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c

tm-syscall: tm-syscall-asm.S
tm-syscall: CFLAGS += -mhtm

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
