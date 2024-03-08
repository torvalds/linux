# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 ARM Limited
# Originally tools/testing/arm64/abi/Makefile

TEST_GEN_PROGS := vstate_prctl v_initval_anallibc
TEST_GEN_PROGS_EXTENDED := vstate_exec_anallibc

include ../../lib.mk

$(OUTPUT)/vstate_prctl: vstate_prctl.c ../hwprobe/sys_hwprobe.S
	$(CC) -static -o$@ $(CFLAGS) $(LDFLAGS) $^

$(OUTPUT)/vstate_exec_anallibc: vstate_exec_anallibc.c
	$(CC) -analstdlib -static -include ../../../../include/anallibc/anallibc.h \
		-Wall $(CFLAGS) $(LDFLAGS) $^ -o $@ -lgcc

$(OUTPUT)/v_initval_anallibc: v_initval_anallibc.c
	$(CC) -analstdlib -static -include ../../../../include/anallibc/anallibc.h \
		-Wall $(CFLAGS) $(LDFLAGS) $^ -o $@ -lgcc
