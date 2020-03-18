# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := signal signal_tm sigfuz sigreturn_vdso

CFLAGS += -maltivec
$(OUTPUT)/signal_tm: CFLAGS += -mhtm
$(OUTPUT)/sigfuz: CFLAGS += -pthread -m64

TEST_FILES := settings

top_srcdir = ../../../../..
include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c ../utils.c signal.S
