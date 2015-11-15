# Makefile for powerpc selftests

# ARCH can be overridden by the user for cross compiling
ARCH ?= $(shell uname -m)
ARCH := $(shell echo $(ARCH) | sed -e s/ppc.*/powerpc/)

ifeq ($(ARCH),powerpc)

GIT_VERSION = $(shell git describe --always --long --dirty || echo "unknown")

CFLAGS := -Wall -O2 -flto -Wall -Werror -DGIT_VERSION='"$(GIT_VERSION)"' -I$(CURDIR) $(CFLAGS)

export CFLAGS

SUB_DIRS = benchmarks 		\
	   copyloops		\
	   dscr			\
	   mm			\
	   pmu			\
	   primitives		\
	   stringloops		\
	   switch_endian	\
	   syscalls		\
	   tm			\
	   vphn

endif

all: $(SUB_DIRS)

$(SUB_DIRS):
	$(MAKE) -k -C $@ all

include ../lib.mk

override define RUN_TESTS
	@for TARGET in $(SUB_DIRS); do \
		$(MAKE) -C $$TARGET run_tests; \
	done;
endef

override define INSTALL_RULE
	@for TARGET in $(SUB_DIRS); do \
		$(MAKE) -C $$TARGET install; \
	done;
endef

override define EMIT_TESTS
	@for TARGET in $(SUB_DIRS); do \
		$(MAKE) -s -C $$TARGET emit_tests; \
	done;
endef

clean:
	@for TARGET in $(SUB_DIRS); do \
		$(MAKE) -C $$TARGET clean; \
	done;
	rm -f tags

tags:
	find . -name '*.c' -o -name '*.h' | xargs ctags

.PHONY: tags $(SUB_DIRS)
