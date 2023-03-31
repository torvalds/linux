# SPDX-License-Identifier: GPL-2.0-only
# Makefile for amd-pstate/ function selftests

# No binaries, but make sure arg-less "make" doesn't trigger "run_tests"
all:

ARCH ?= $(shell uname -m 2>/dev/null || echo not)
ARCH := $(shell echo $(ARCH) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

ifeq (x86,$(ARCH))
TEST_FILES += ../../../power/x86/amd_pstate_tracer/amd_pstate_trace.py
TEST_FILES += ../../../power/x86/intel_pstate_tracer/intel_pstate_tracer.py
endif

TEST_PROGS += run.sh
TEST_FILES += basic.sh tbench.sh gitsource.sh

include ../lib.mk
