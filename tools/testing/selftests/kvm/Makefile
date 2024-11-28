# SPDX-License-Identifier: GPL-2.0-only
top_srcdir = ../../../..
include $(top_srcdir)/scripts/subarch.include
ARCH            ?= $(SUBARCH)

ifeq ($(ARCH),$(filter $(ARCH),arm64 s390 riscv x86 x86_64))
ifeq ($(ARCH),x86)
	ARCH_DIR := x86_64
else ifeq ($(ARCH),arm64)
	ARCH_DIR := aarch64
else ifeq ($(ARCH),s390)
	ARCH_DIR := s390x
else
	ARCH_DIR := $(ARCH)
endif

include Makefile.kvm
else
# Empty targets for unsupported architectures
all:
clean:
endif
