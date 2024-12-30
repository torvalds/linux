# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 ARM Limited
# Originally tools/testing/arm64/abi/Makefile

TEST_GEN_PROGS := v_initval vstate_prctl
TEST_GEN_PROGS_EXTENDED := vstate_exec_nolibc v_exec_initval_nolibc

include ../../lib.mk

$(OUTPUT)/sys_hwprobe.o: ../hwprobe/sys_hwprobe.S
	$(CC) -static -c -o$@ $(CFLAGS) $^

$(OUTPUT)/v_helpers.o: v_helpers.c
	$(CC) -static -c -o$@ $(CFLAGS) $^

$(OUTPUT)/vstate_prctl: vstate_prctl.c $(OUTPUT)/sys_hwprobe.o $(OUTPUT)/v_helpers.o
	$(CC) -static -o$@ $(CFLAGS) $(LDFLAGS) $^

$(OUTPUT)/vstate_exec_nolibc: vstate_exec_nolibc.c
	$(CC) -nostdlib -static -include ../../../../include/nolibc/nolibc.h \
		-Wall $(CFLAGS) $(LDFLAGS) $^ -o $@ -lgcc

$(OUTPUT)/v_initval: v_initval.c $(OUTPUT)/sys_hwprobe.o $(OUTPUT)/v_helpers.o
	$(CC) -static -o$@ $(CFLAGS) $(LDFLAGS) $^

$(OUTPUT)/v_exec_initval_nolibc: v_exec_initval_nolibc.c
	$(CC) -nostdlib -static -include ../../../../include/nolibc/nolibc.h \
		-Wall $(CFLAGS) $(LDFLAGS) $^ -o $@ -lgcc
