# SPDX-License-Identifier: GPL-2.0
# Makefile for powerpc selftests

# ARCH can be overridden by the user for cross compiling
ARCH ?= $(shell uname -m)
ARCH := $(shell echo $(ARCH) | sed -e s/ppc.*/powerpc/)

ifeq ($(ARCH),powerpc)

GIT_VERSION = $(shell git describe --always --long --dirty || echo "unknown")

CFLAGS := -std=gnu99 -O2 -Wall -Werror -DGIT_VERSION='"$(GIT_VERSION)"' -I$(CURDIR)/include $(CFLAGS)

export CFLAGS

SUB_DIRS = alignment		\
	   benchmarks		\
	   cache_shape		\
	   copyloops		\
	   dscr			\
	   mm			\
	   nx-gzip		\
	   pmu			\
	   signal		\
	   primitives		\
	   stringloops		\
	   switch_endian	\
	   syscalls		\
	   tm			\
	   eeh			\
	   vphn         \
	   math		\
	   papr_attributes	\
	   ptrace	\
	   security	\
	   mce

endif

all: $(SUB_DIRS)

$(SUB_DIRS):
	BUILD_TARGET=$(OUTPUT)/$@; mkdir -p $$BUILD_TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -k -C $@ all

include ../lib.mk

override define RUN_TESTS
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET run_tests;\
	done;
endef

override define INSTALL_RULE
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET install;\
	done;
endef

override define EMIT_TESTS
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -s -C $$TARGET emit_tests;\
	done;
endef

override define CLEAN
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET clean; \
	done;
	rm -f tags
endef

tags:
	find . -name '*.c' -o -name '*.h' | xargs ctags

.PHONY: tags $(SUB_DIRS)
