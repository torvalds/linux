# Makefile for mount selftests.

all: unprivileged-remount-test

unprivileged-remount-test: unprivileged-remount-test.c
	gcc -Wall -O2 unprivileged-remount-test.c -o unprivileged-remount-test

# Allow specific tests to be selected.
test_unprivileged_remount: unprivileged-remount-test
	@if [ -f /proc/self/uid_map ] ; then ./unprivileged-remount-test ; fi

run_tests: all test_unprivileged_remount

clean:
	rm -f unprivileged-remount-test

.PHONY: all test_unprivileged_remount
