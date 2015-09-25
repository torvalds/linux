TEST_PROGS := gettimeofday

CFLAGS += -O2

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
