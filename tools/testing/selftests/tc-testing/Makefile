# SPDX-License-Identifier: GPL-2.0
include ../../../scripts/Makefile.include

top_srcdir = $(abspath ../../../..)
APIDIR := $(top_scrdir)/include/uapi
TEST_GEN_FILES = action.o

include ../lib.mk

PROBE := $(shell $(LLC) -march=bpf -mcpu=probe -filetype=null /dev/null 2>&1)

ifeq ($(PROBE),)
  CPU ?= probe
else
  CPU ?= generic
endif

CLANG_SYS_INCLUDES := $(shell $(CLANG) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')

CLANG_FLAGS = -I. -I$(APIDIR) \
	      $(CLANG_SYS_INCLUDES) \
	      -Wno-compare-distinct-pointer-types

$(OUTPUT)/%.o: %.c
	$(CLANG) $(CLANG_FLAGS) \
		 -O2 -target bpf -emit-llvm -c $< -o - |      \
	$(LLC) -march=bpf -mcpu=$(CPU) $(LLC_FLAGS) -filetype=obj -o $@

TEST_PROGS += ./tdc.sh
TEST_FILES := tdc*.py Tdc*.py plugins plugin-lib tc-tests
