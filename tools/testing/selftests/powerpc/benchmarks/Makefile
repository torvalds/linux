# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := gettimeofday context_switch fork mmap_bench futex_bench null_syscall
TEST_GEN_FILES := exec_target

TEST_FILES := settings

CFLAGS += -O2

top_srcdir = ../../../../..
include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c

$(OUTPUT)/context_switch: ../utils.c
$(OUTPUT)/context_switch: CFLAGS += -maltivec -mvsx -mabi=altivec
$(OUTPUT)/context_switch: LDLIBS += -lpthread

$(OUTPUT)/fork: LDLIBS += -lpthread

$(OUTPUT)/exec_target: CFLAGS += -static -nostartfiles
