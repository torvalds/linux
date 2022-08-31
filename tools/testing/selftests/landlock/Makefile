# SPDX-License-Identifier: GPL-2.0

CFLAGS += -Wall -O2 $(KHDR_INCLUDES)

src_test := $(wildcard *_test.c)

TEST_GEN_PROGS := $(src_test:.c=)

TEST_GEN_PROGS_EXTENDED := true

OVERRIDE_TARGETS := 1
include ../lib.mk

$(OUTPUT)/true: true.c
	$(LINK.c) $< $(LDLIBS) -o $@ -static

$(OUTPUT)/%_test: %_test.c ../kselftest_harness.h common.h
	$(LINK.c) $< $(LDLIBS) -o $@ -lcap
