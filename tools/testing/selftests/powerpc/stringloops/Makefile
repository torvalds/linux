# The loops are all 64-bit code
CFLAGS += -m64
CFLAGS += -I$(CURDIR)

TEST_GEN_PROGS := memcmp
EXTRA_SOURCES := memcmp_64.S ../harness.c

include ../../lib.mk

$(TEST_GEN_PROGS): $(EXTRA_SOURCES)
