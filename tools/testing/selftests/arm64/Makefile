# SPDX-License-Identifier: GPL-2.0

# ARCH can be overridden by the user for cross compiling
ARCH ?= $(shell uname -m 2>/dev/null || echo not)

ifneq (,$(filter $(ARCH),aarch64 arm64))
CFLAGS += -I../../../../usr/include/
TEST_GEN_PROGS := tags_test
TEST_PROGS := run_tags_test.sh
endif

include ../lib.mk
