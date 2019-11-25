# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2019 ARM Limited

# Additional include paths needed by kselftest.h and local headers
CFLAGS += -D_GNU_SOURCE -std=gnu99 -I.

SRCS := $(filter-out testcases/testcases.c,$(wildcard testcases/*.c))
PROGS := $(patsubst %.c,%,$(SRCS))

# Generated binaries to be installed by top KSFT script
TEST_GEN_PROGS := $(notdir $(PROGS))

# Get Kernel headers installed and use them.
KSFT_KHDR_INSTALL := 1

# Including KSFT lib.mk here will also mangle the TEST_GEN_PROGS list
# to account for any OUTPUT target-dirs optionally provided by
# the toplevel makefile
include ../../lib.mk

$(TEST_GEN_PROGS): $(PROGS)
	cp $(PROGS) $(OUTPUT)/

clean:
	$(CLEAN)
	rm -f $(PROGS)

# Common test-unit targets to build common-layout test-cases executables
# Needs secondary expansion to properly include the testcase c-file in pre-reqs
.SECONDEXPANSION:
$(PROGS): test_signals.c test_signals_utils.c testcases/testcases.c signals.S $$@.c test_signals.h test_signals_utils.h testcases/testcases.h
	$(CC) $(CFLAGS) $^ -o $@
