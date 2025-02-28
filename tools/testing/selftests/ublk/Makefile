# SPDX-License-Identifier: GPL-2.0

CFLAGS += -O3 -Wl,-no-as-needed -Wall -I $(top_srcdir)
LDLIBS += -lpthread -lm -luring

TEST_PROGS := test_null_01.sh

TEST_GEN_PROGS_EXTENDED = kublk

include ../lib.mk

$(TEST_GEN_PROGS_EXTENDED): kublk.c null.c
