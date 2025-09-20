# SPDX-License-Identifier: GPL-2.0
# Makefile for powerpc selftests

# ARCH can be overridden by the user for cross compiling
ARCH ?= $(shell uname -m)
ARCH := $(shell echo $(ARCH) | sed -e s/ppc.*/powerpc/)

ifeq ($(ARCH),powerpc)

SUB_DIRS = alignment		\
	   benchmarks		\
	   cache_shape		\
	   copyloops		\
	   dexcr		\
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
	   papr_vpd		\
	   papr_sysparm		\
	   ptrace	\
	   security	\
	   mce

endif

all: $(SUB_DIRS)

$(SUB_DIRS):
	BUILD_TARGET=$(OUTPUT)/$@; mkdir -p $$BUILD_TARGET; $(MAKE) OUTPUT=$$BUILD_TARGET -k -C $@ all

include ../lib.mk
include ./flags.mk

override define RUN_TESTS
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET run_tests;\
	done;
endef

override define INSTALL_RULE
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET INSTALL_PATH=$$INSTALL_PATH/$$TARGET -C $$TARGET install;\
	done;
endef

emit_tests:
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET COLLECTION=$(COLLECTION)/$$TARGET -s -C $$TARGET $@;\
	done;

override define CLEAN
	+@for TARGET in $(SUB_DIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET clean; \
	done;
	rm -f tags
endef

tags:
	find . -name '*.c' -o -name '*.h' | xargs ctags

.PHONY: tags $(SUB_DIRS) emit_tests
