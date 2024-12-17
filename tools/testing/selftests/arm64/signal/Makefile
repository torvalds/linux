# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2019 ARM Limited

# Additional include paths needed by kselftest.h and local headers
CFLAGS += -std=gnu99 -I.

SRCS := $(filter-out testcases/testcases.c,$(wildcard testcases/*.c))
PROGS := $(patsubst %.c,%,$(SRCS))

# Generated binaries to be installed by top KSFT script
TEST_GEN_PROGS := $(notdir $(PROGS))

# Get Kernel headers installed and use them.

# Including KSFT lib.mk here will also mangle the TEST_GEN_PROGS list
# to account for any OUTPUT target-dirs optionally provided by
# the toplevel makefile
include ../../lib.mk

$(TEST_GEN_PROGS): $(PROGS)
	cp $(PROGS) $(OUTPUT)/

# Common test-unit targets to build common-layout test-cases executables
# Needs secondary expansion to properly include the testcase c-file in pre-reqs
COMMON_SOURCES := test_signals.c test_signals_utils.c testcases/testcases.c \
	signals.S sve_helpers.c
COMMON_HEADERS := test_signals.h test_signals_utils.h testcases/testcases.h

.SECONDEXPANSION:
$(PROGS): $$@.c ${COMMON_SOURCES} ${COMMON_HEADERS}
	$(CC) $(CFLAGS) ${@}.c ${COMMON_SOURCES} -o $@
