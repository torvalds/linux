TEST_PROGS := test-vphn

CFLAGS += -m64

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS)
