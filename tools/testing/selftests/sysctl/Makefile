# Makefile for sysctl selftests.
# Expects kernel.sysctl_writes_strict=1.

# No binaries, but make sure arg-less "make" doesn't trigger "run_tests".
all:

TEST_PROGS := run_numerictests run_stringtests
TEST_FILES := common_tests

include ../lib.mk

# Nothing to clean up.
clean:
