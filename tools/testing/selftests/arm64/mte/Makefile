# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020 ARM Limited

CFLAGS += -std=gnu99 -I. -pthread
LDFLAGS += -pthread
SRCS := $(filter-out mte_common_util.c,$(wildcard *.c))
PROGS := $(patsubst %.c,%,$(SRCS))

ifeq ($(LLVM),)
# For GCC check that the toolchain has MTE support.

# preserve CC value from top level Makefile
ifeq ($(CC),cc)
CC := $(CROSS_COMPILE)gcc
endif

#check if the compiler works well
mte_cc_support := $(shell if ($(CC) $(CFLAGS) -march=armv8.5-a+memtag -E -x c /dev/null -o /dev/null 2>&1) then echo "1"; fi)

else

# All supported clang versions also support MTE.
mte_cc_support := 1

endif

ifeq ($(mte_cc_support),1)
# Generated binaries to be installed by top KSFT script
TEST_GEN_PROGS := $(PROGS)

else
    $(warning compiler "$(CC)" does not support the ARMv8.5 MTE extension.)
    $(warning test program "mte" will not be created.)
endif

# Include KSFT lib.mk.
include ../../lib.mk

ifeq ($(mte_cc_support),1)
$(TEST_GEN_PROGS): mte_common_util.c mte_helper.S
endif
