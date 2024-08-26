# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 ARM Limited
# Originally tools/testing/arm64/abi/Makefile

CFLAGS += -I$(top_srcdir)/tools/include

TEST_GEN_PROGS := sigreturn

include ../../lib.mk

$(OUTPUT)/sigreturn: sigreturn.c
	$(CC) -static -o$@ $(CFLAGS) $(LDFLAGS) $^
