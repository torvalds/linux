include ../../../../../build/Build.include

UNAME_M := $(shell uname -m)

ifneq ($(UNAME_M),s390x)
nothing:
.PHONY: all clean run_tests install
.SILENT:
else

TEST_GEN_PROGS := test_uvdevice

top_srcdir ?= ../../../../../..
khdr_dir = $(top_srcdir)/usr/include
LINUX_TOOL_ARCH_INCLUDE = $(top_srcdir)/tools/arch/$(ARCH)/include

CFLAGS += -Wall -Werror -static -I$(khdr_dir) -I$(LINUX_TOOL_ARCH_INCLUDE)

include ../../../lib.mk

endif
