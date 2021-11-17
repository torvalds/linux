# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020 ARM Limited

# preserve CC value from top level Makefile
ifeq ($(CC),cc)
CC := $(CROSS_COMPILE)gcc
endif

CFLAGS += -std=gnu99 -I. -pthread
LDFLAGS += -pthread
SRCS := $(filter-out mte_common_util.c,$(wildcard *.c))
PROGS := $(patsubst %.c,%,$(SRCS))

#Add mte compiler option
CFLAGS += -march=armv8.5-a+memtag

#check if the compiler works well
mte_cc_support := $(shell if ($(CC) $(CFLAGS) -E -x c /dev/null -o /dev/null 2>&1) then echo "1"; fi)

ifeq ($(mte_cc_support),1)
# Generated binaries to be installed by top KSFT script
TEST_GEN_PROGS := $(PROGS)

# Get Kernel headers installed and use them.
KSFT_KHDR_INSTALL := 1
else
    $(warning compiler "$(CC)" does not support the ARMv8.5 MTE extension.)
    $(warning test program "mte" will not be created.)
endif

# Include KSFT lib.mk.
include ../../lib.mk

ifeq ($(mte_cc_support),1)
$(TEST_GEN_PROGS): mte_common_util.c mte_helper.S
endif
