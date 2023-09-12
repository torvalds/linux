# SPDX-License-Identifier: GPL-2.0
ifndef CROSS_COMPILE
uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

ifeq ($(ARCH),x86)
TEST_PROGS := disable-tsc-ctxt-sw-stress-test disable-tsc-on-off-stress-test \
		disable-tsc-test set-anon-vma-name-test set-process-name
all: $(TEST_PROGS)

include ../lib.mk

endif
endif
