# The loops are all 64-bit code
CFLAGS += -m64
CFLAGS += -I$(CURDIR)

TEST_PROGS := memcmp
EXTRA_SOURCES := memcmp_64.S ../harness.c

all: $(TEST_PROGS)

$(TEST_PROGS): $(EXTRA_SOURCES)

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
