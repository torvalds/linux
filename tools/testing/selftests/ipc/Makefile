# SPDX-License-Identifier: GPL-2.0
ARCH ?= $(shell uname -m 2>/dev/null || echo not)
override ARCH := $(shell echo $(ARCH) | sed -e s/i.86/i386/)
ifeq ($(ARCH),i386)
        override ARCH := x86
	CFLAGS := -DCONFIG_X86_32 -D__i386__
endif
ifeq ($(ARCH),x86_64)
        override ARCH := x86
	CFLAGS := -DCONFIG_X86_64 -D__x86_64__
endif

CFLAGS += $(KHDR_INCLUDES)

TEST_GEN_PROGS := msgque

include ../lib.mk

