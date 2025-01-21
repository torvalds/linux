// SPDX-License-Identifier: GPL-2.0+

/*
 * Part of fork context switch microbenchmark.
 *
 * Copyright 2018, Anton Blanchard, IBM Corp.
 */

#define _GNU_SOURCE
#include <sys/syscall.h>

void _start(void)
{
	asm volatile (
		"li %%r0, %[sys_exit];"
		"li %%r3, 0;"
		"sc;"
		:
		: [sys_exit] "i" (SYS_exit)
		/*
		 * "sc" will clobber r0, r3-r13, cr0, ctr, xer and memory.
		 * Even though sys_exit never returns, handle clobber
		 * registers.
		 */
		: "r0", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
		  "r11", "r12", "r13", "cr0", "ctr", "xer", "memory"
	);
}
