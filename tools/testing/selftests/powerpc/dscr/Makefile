TEST_PROGS := dscr_default_test

dscr_default_test: LDLIBS += -lpthread

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
