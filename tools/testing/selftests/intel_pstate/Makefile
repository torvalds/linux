CFLAGS := $(CFLAGS) -Wall -D_GNU_SOURCE
LDLIBS := $(LDLIBS) -lm

TEST_GEN_FILES := msr aperf

TEST_PROGS := run.sh

include ../lib.mk

$(TEST_GEN_FILES): $(HEADERS)
