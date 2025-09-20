# SPDX-License-Identifier: GPL-2.0
CFLAGS += -O3 -Wl,-no-as-needed -Wall -I$(top_srcdir)/usr/include
LDLIBS += -lrt -lpthread -lm

TEST_GEN_PROGS = rtctest

TEST_FILES := settings

include ../lib.mk
