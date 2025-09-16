# SPDX-License-Identifier: GPL-2.0

TEST_GEN_PROGS := nolibc-test

include ../lib.mk
include $(top_srcdir)/scripts/Makefile.compiler

cc-option = $(call __cc-option, $(CC),,$(1),$(2))

include Makefile.include

CFLAGS = -nostdlib -nostdinc -static \
	 -isystem $(top_srcdir)/tools/include/nolibc -isystem $(top_srcdir)/usr/include \
	 $(CFLAGS_NOLIBC_TEST)

ifeq ($(LLVM),)
LDLIBS := -lgcc
endif

$(OUTPUT)/nolibc-test: nolibc-test.c nolibc-test-linkage.c | headers

help:
	@echo "For the custom nolibc testsuite use '$(MAKE) -f Makefile.nolibc'; available targets:"
	@$(MAKE) -f Makefile.nolibc help

.PHONY: help
