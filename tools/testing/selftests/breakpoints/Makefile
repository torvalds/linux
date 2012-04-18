# Taken from perf makefile
uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/)
ifeq ($(ARCH),i386)
        ARCH := x86
endif
ifeq ($(ARCH),x86_64)
	ARCH := x86
endif


all:
ifeq ($(ARCH),x86)
	gcc breakpoint_test.c -o breakpoint_test
else
	echo "Not an x86 target, can't build breakpoints selftests"
endif

run_tests:
	./breakpoint_test

clean:
	rm -fr breakpoint_test
