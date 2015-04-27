CFLAGS += -I$(CURDIR)

TEST_PROGS := load_unaligned_zeropad

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
