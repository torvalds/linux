CFLAGS := $(CFLAGS) -Wall -D_GNU_SOURCE
LDLIBS := $(LDLIBS) -lm

ifeq (,$(filter $(ARCH),x86))
TEST_GEN_FILES := msr aperf
endif

TEST_PROGS := run.sh

include ../lib.mk

$(TEST_GEN_FILES): $(HEADERS)
