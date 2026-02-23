# SPDX-License-Identifier: GPL-2.0

TEST_GEN_PROGS := nolibc-test libc-test

include ../lib.mk
include $(top_srcdir)/scripts/Makefile.compiler

cc-option = $(call __cc-option, $(CC),,$(1),$(2))

include Makefile.include

$(OUTPUT)/nolibc-test: CFLAGS = -nostdlib -nostdinc -static \
	 -isystem $(top_srcdir)/tools/include/nolibc -isystem $(top_srcdir)/usr/include \
	 $(CFLAGS_NOLIBC_TEST)
$(OUTPUT)/nolibc-test: LDLIBS = $(if $(LLVM),,-lgcc)
$(OUTPUT)/nolibc-test: nolibc-test.c nolibc-test-linkage.c | headers

$(OUTPUT)/libc-test: nolibc-test.c nolibc-test-linkage.c
	$(call msg,CC,,$@)
	$(Q)$(LINK.c) $^ -o $@

help:
	@echo "For the custom nolibc testsuite use '$(MAKE) -f Makefile.nolibc'; available targets:"
	@$(MAKE) -f Makefile.nolibc help

.PHONY: help
