# Makefile for mount selftests.

all: unprivileged-remount-test

unprivileged-remount-test: unprivileged-remount-test.c
	gcc -Wall -O2 unprivileged-remount-test.c -o unprivileged-remount-test

include ../lib.mk

TEST_PROGS := unprivileged-remount-test
override RUN_TESTS := if [ -f /proc/self/uid_map ] ; then ./unprivileged-remount-test ; fi
override EMIT_TESTS := echo "$(RUN_TESTS)"

clean:
	rm -f unprivileged-remount-test
