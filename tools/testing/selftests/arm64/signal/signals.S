/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 ARM Limited */

#include <asm/unistd.h>

.section        .rodata, "a"
call_fmt:
	.asciz "Calling sigreturn with fake sigframe sized:%zd at SP @%08lX\n"

.text

.globl fake_sigreturn

/*	fake_sigreturn	x0:&sigframe,  x1:sigframe_size,  x2:misalign_bytes */
fake_sigreturn:
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp

	mov	x20, x0
	mov	x21, x1
	mov	x22, x2

	/* create space on the stack for fake sigframe 16 bytes-aligned */
	add	x0, x21, x22
	add	x0, x0, #15
	bic	x0, x0, #15 /* round_up(sigframe_size + misalign_bytes, 16) */
	sub	sp, sp, x0
	add	x23, sp, x22 /* new sigframe base with misaligment if any */

	ldr	x0, =call_fmt
	mov	x1, x21
	mov	x2, x23
	bl	printf

	/* memcpy the provided content, while still keeping SP aligned */
	mov	x0, x23
	mov	x1, x20
	mov	x2, x21
	bl	memcpy

	/*
	 * Here saving a last minute SP to current->token acts as a marker:
	 * if we got here, we are successfully faking a sigreturn; in other
	 * words we are sure no bad fatal signal has been raised till now
	 * for unrelated reasons, so we should consider the possibly observed
	 * fatal signal like SEGV coming from Kernel restore_sigframe() and
	 * triggered as expected from our test-case.
	 * For simplicity this assumes that current field 'token' is laid out
	 * as first in struct tdescr
	 */
	ldr	x0, current
	str	x23, [x0]
	/* finally move SP to misaligned address...if any requested */
	mov	sp, x23

	mov	x8, #__NR_rt_sigreturn
	svc	#0

	/*
	 * Above sigreturn should not return...looping here leads to a timeout
	 * and ensure proper and clean test failure, instead of jumping around
	 * on a potentially corrupted stack.
	 */
	b	.
