# Taken from perf makefile
uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

ifeq ($(ARCH),x86)
TEST_GEN_PROGS := breakpoint_test
endif
ifeq ($(ARCH),aarch64)
TEST_GEN_PROGS := breakpoint_test_arm64
endif

TEST_GEN_PROGS += step_after_suspend_test

include ../lib.mk

