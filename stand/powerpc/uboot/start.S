/*-
 * Copyright (c) 2007 Semihalf, Rafal Jaworowski <raj@semihalf.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <machine/asm.h>

/*
 * Entry point to the loader that U-Boot passes control to.
 */
	.text
	.globl	_start
_start:
	/* Hint where to look for the API signature */
	lis	%r11, uboot_address@ha
	addi	%r11, %r11, uboot_address@l
	stw	%r1, 0(%r11)
	/* Save U-Boot's r14 */
	lis	%r11, saved_regs@ha
	addi	%r11, %r11, saved_regs@l
	stw	%r14, 0(%r11)
	/* Disable interrupts */
	mfmsr	%r11
	andi.	%r11, %r11, ~0x8000@l
	mtmsr	%r11
	b	main

/*
 * syscall()
 */
ENTRY(syscall)
	stwu	%r1, -16(%r1)
	mflr	%r0
	stw	%r14, 8(%r1)
	stw	%r0, 20(%r1)
	/* Restore U-Boot's r14 */
	lis	%r11, saved_regs@ha
	addi	%r11, %r11, saved_regs@l
	lwz	%r14, 0(%r11)
	/* Enable interrupts */
	mfmsr	%r11
	ori	%r11, %r11, 0x8000@l
	mtmsr	%r11
	/* Call into U-Boot */
	lis	%r11, syscall_ptr@ha
	addi	%r11, %r11, syscall_ptr@l
	lwz	%r11, 0(%r11)
	mtctr	%r11
	bctrl
	/* Disable interrupts */
	mfmsr	%r11
	andi.	%r11, %r11, ~0x8000@l
	mtmsr	%r11
	/* Epilogue */
	lwz	%r11, 0(%r1)
	lwz	%r0, 4(%r11)
	mtlr	%r0
	lwz	%r14, 8(%r1)
	mr	%r1, %r11
	blr

/*
 * Data section
 */
	.data
GLOBAL(syscall_ptr)
	.long	0
GLOBAL(saved_regs)
	.long	0	/* R14 */
GLOBAL(uboot_address)
	.long	0
