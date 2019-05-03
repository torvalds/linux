# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := signal signal_tm

CFLAGS += -maltivec
$(OUTPUT)/signal_tm: CFLAGS += -mhtm

top_srcdir = ../../../../..
include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c ../utils.c signal.S
