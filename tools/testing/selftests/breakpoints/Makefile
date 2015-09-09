# Taken from perf makefile
uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

ifeq ($(ARCH),x86)
TEST_PROGS := breakpoint_test
endif

all:

include ../lib.mk

clean:
	rm -fr breakpoint_test
