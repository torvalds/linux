# SPDX-License-Identifier: GPL-2.0-only
top_srcdir = ../../../..
include $(top_srcdir)/scripts/subarch.include
ARCH            ?= $(SUBARCH)

ifeq ($(ARCH),$(filter $(ARCH),arm64 s390 riscv x86 x86_64))
# Top-level selftests allows ARCH=x86_64 :-(
ifeq ($(ARCH),x86_64)
	ARCH := x86
endif
include Makefile.kvm
else
# Empty targets for unsupported architectures
all:
clean:
endif
