# SPDX-License-Identifier: GPL-2.0-only
# Makefile for kexec tests

ARCH ?= $(shell uname -m 2>/dev/null || echo not)
ARCH_PROCESSED := $(shell echo $(ARCH) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

ifeq ($(ARCH_PROCESSED),$(filter $(ARCH_PROCESSED),x86 ppc64le))
TEST_PROGS := test_kexec_load.sh test_kexec_file_load.sh
TEST_FILES := kexec_common_lib.sh

include ../../../scripts/Makefile.arch

ifeq ($(IS_64_BIT)$(ARCH_PROCESSED),1x86)
TEST_PROGS += test_kexec_jump.sh
TEST_GEN_PROGS := test_kexec_jump
endif

include ../lib.mk

endif
