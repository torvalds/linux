# SPDX-License-Identifier: GPL-2.0
CFLAGS += -m64

TEST_GEN_PROGS := mmcr0_exceptionbits_test mmcr0_cc56run_test mmcr0_pmccext_test

top_srcdir = ../../../../../..
include ../../../lib.mk

$(TEST_GEN_PROGS): ../../harness.c ../../utils.c ../event.c ../lib.c misc.c misc.h ../loop.S
