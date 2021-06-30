# SPDX-License-Identifier: GPL-2.0

CFLAGS += -Wall -O2

src_test := $(wildcard *_test.c)

TEST_GEN_PROGS := $(src_test:.c=)

TEST_GEN_PROGS_EXTENDED := true

KSFT_KHDR_INSTALL := 1
OVERRIDE_TARGETS := 1
include ../lib.mk

khdr_dir = $(top_srcdir)/usr/include

$(khdr_dir)/linux/landlock.h: khdr
	@:

$(OUTPUT)/true: true.c
	$(LINK.c) $< $(LDLIBS) -o $@ -static

$(OUTPUT)/%_test: %_test.c $(khdr_dir)/linux/landlock.h ../kselftest_harness.h common.h
	$(LINK.c) $< $(LDLIBS) -o $@ -lcap -I$(khdr_dir)
