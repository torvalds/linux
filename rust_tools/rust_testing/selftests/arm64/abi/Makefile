# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 ARM Limited

TEST_GEN_PROGS := hwcap ptrace syscall-abi tpidr2

include ../../lib.mk

$(OUTPUT)/syscall-abi: syscall-abi.c syscall-abi-asm.S

# Build with nolibc since TPIDR2 is intended to be actively managed by
# libc and we're trying to test the functionality that it depends on here.
$(OUTPUT)/tpidr2: tpidr2.c
	$(CC) -fno-asynchronous-unwind-tables -fno-ident -s -Os -nostdlib \
		-static -include ../../../../include/nolibc/nolibc.h \
		-I../.. -ffreestanding -Wall $^ -o $@ -lgcc
