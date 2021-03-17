# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020 ARM Limited

CFLAGS += -std=gnu99 -I. -lpthread
SRCS := $(filter-out mte_common_util.c,$(wildcard *.c))
PROGS := $(patsubst %.c,%,$(SRCS))

#Add mte compiler option
ifneq ($(shell $(CC) --version 2>&1 | head -n 1 | grep gcc),)
CFLAGS += -march=armv8.5-a+memtag
endif

#check if the compiler works well
mte_cc_support := $(shell if ($(CC) $(CFLAGS) -E -x c /dev/null -o /dev/null 2>&1) then echo "1"; fi)

ifeq ($(mte_cc_support),1)
# Generated binaries to be installed by top KSFT script
TEST_GEN_PROGS := $(PROGS)

# Get Kernel headers installed and use them.
KSFT_KHDR_INSTALL := 1
endif

# Include KSFT lib.mk.
include ../../lib.mk

ifeq ($(mte_cc_support),1)
$(TEST_GEN_PROGS): mte_common_util.c mte_common_util.h mte_helper.S
endif
