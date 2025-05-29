# SPDX-License-Identifier: GPL-2.0

top_srcdir = ../../../../..

CFLAGS += -Wall -Wl,--no-as-needed -O2 -g -I$(top_srcdir)/usr/include $(KHDR_INCLUDES)

TEST_PROGS := test_raw_filter.sh

TEST_GEN_FILES := test_raw_filter

include ../../lib.mk
