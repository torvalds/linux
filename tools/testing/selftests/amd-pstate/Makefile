# SPDX-License-Identifier: GPL-2.0-only
# Makefile for amd-pstate/ function selftests

# No binaries, but make sure arg-less "make" doesn't trigger "run_tests"
all:

uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

TEST_PROGS := run.sh
TEST_FILES := basic.sh tbench.sh gitsource.sh

include ../lib.mk
