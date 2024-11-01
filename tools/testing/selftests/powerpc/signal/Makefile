# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := signal signal_tm sigfuz sigreturn_vdso sig_sc_double_restart
TEST_GEN_PROGS += sigreturn_kernel
TEST_GEN_PROGS += sigreturn_unaligned

CFLAGS += -maltivec
$(OUTPUT)/signal_tm: CFLAGS += -mhtm
$(OUTPUT)/sigfuz: CFLAGS += -pthread -m64

TEST_FILES := settings

top_srcdir = ../../../../..
include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c ../utils.c signal.S
