# SPDX-License-Identifier: GPL-2.0
top_srcdir = ../../../../../..
include $(top_srcdir)/scripts/subarch.include
ARCH_PROCESSED := $(shell echo $(or $(ARCH),$(SUBARCH)) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

ifeq ($(ARCH_PROCESSED),x86)
TEST_GEN_PROGS := power_floor_test
endif

include ../../../lib.mk
