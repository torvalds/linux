# SPDX-License-Identifier: GPL-2.0
CFLAGS += -m64

TEST_GEN_PROGS :=

top_srcdir = ../../../../../..
include ../../../lib.mk

$(TEST_GEN_PROGS): ../../harness.c ../../utils.c ../event.c ../lib.c ../sampling_tests/misc.h ../sampling_tests/misc.c
