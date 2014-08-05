# Makefile for sysctl selftests.
# Expects kernel.sysctl_writes_strict=1.

# No binaries, but make sure arg-less "make" doesn't trigger "run_tests".
all:

# Allow specific tests to be selected.
test_num:
	@/bin/sh ./run_numerictests

test_string:
	@/bin/sh ./run_stringtests

run_tests: all test_num test_string

# Nothing to clean up.
clean:

.PHONY: all run_tests clean test_num test_string
