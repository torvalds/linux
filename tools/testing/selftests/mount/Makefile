# Makefile for mount selftests.

all: unprivileged-remount-test

unprivileged-remount-test: unprivileged-remount-test.c
	gcc -Wall -O2 unprivileged-remount-test.c -o unprivileged-remount-test

include ../lib.mk

override RUN_TESTS := if [ -f /proc/self/uid_map ] ; then ./unprivileged-remount-test ; fi

clean:
	rm -f unprivileged-remount-test
