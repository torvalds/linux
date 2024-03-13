# SPDX-License-Identifier: GPL-2.0
CFLAGS := $(CFLAGS) -Wall -D_GNU_SOURCE
LDLIBS += -lm

ARCH ?= $(shell uname -m 2>/dev/null || echo not)
ARCH_PROCESSED := $(shell echo $(ARCH) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

ifeq (x86,$(ARCH_PROCESSED))
TEST_GEN_FILES := msr aperf
endif

TEST_PROGS := run.sh

include ../lib.mk

$(TEST_GEN_FILES): $(HEADERS)
