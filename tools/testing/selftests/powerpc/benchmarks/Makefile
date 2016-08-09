TEST_PROGS := gettimeofday context_switch mmap_bench futex_bench

CFLAGS += -O2

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c

context_switch: ../utils.c
context_switch: CFLAGS += -maltivec -mvsx -mabi=altivec
context_switch: LDLIBS += -lpthread

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
