TEST_PROGS := ptrace-gpr

include ../../lib.mk

all: $(TEST_PROGS)

CFLAGS += -m64 -I../../../../../usr/include

$(TEST_PROGS): ../harness.c ../utils.c ../lib/reg.S ptrace.h

clean:
	rm -f $(TEST_PROGS) *.o
