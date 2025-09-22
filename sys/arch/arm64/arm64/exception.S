/* $OpenBSD: exception.S,v 1.17 2024/03/12 13:32:53 kettenis Exp $ */
/*-
 * Copyright (c) 2014 Andrew Turner
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
 */

#include <machine/asm.h>
#include <machine/armreg.h>

#include "assym.h"

	.text

.macro	save_registers el
.if \el == 1
	mov	x18, sp
	sub	sp, sp, #128
.endif
	sub	sp, sp, #(TF_SIZE + 16)
	stp	x28, x29, [sp, #(TF_X + 28 * 8)]
	stp	x26, x27, [sp, #(TF_X + 26 * 8)]
	stp	x24, x25, [sp, #(TF_X + 24 * 8)]
	stp	x22, x23, [sp, #(TF_X + 22 * 8)]
	stp	x20, x21, [sp, #(TF_X + 20 * 8)]
	stp	x18, x19, [sp, #(TF_X + 18 * 8)]
	stp	x16, x17, [sp, #(TF_X + 16 * 8)]
	stp	x14, x15, [sp, #(TF_X + 14 * 8)]
	stp	x12, x13, [sp, #(TF_X + 12 * 8)]
	stp	x10, x11, [sp, #(TF_X + 10 * 8)]
	stp	x8,  x9,  [sp, #(TF_X + 8  * 8)]
	stp	x6,  x7,  [sp, #(TF_X + 6  * 8)]
	stp	x4,  x5,  [sp, #(TF_X + 4  * 8)]
	stp	x2,  x3,  [sp, #(TF_X + 2  * 8)]
	stp	x0,  x1,  [sp, #(TF_X + 0  * 8)]
	mrs	x10, elr_el1
	mrs	x11, spsr_el1
.if \el == 0
	mrs	x18, sp_el0
.endif
	stp	x10, x11, [sp, #(TF_ELR)]
	stp	x18, lr, [sp, #(TF_SP)]
	stp	fp, x10, [sp, #(TF_SIZE)]
	mrs	x18, tpidr_el1
	add	fp, sp, #(TF_SIZE)
.endm

.macro	restore_registers el
.if \el == 1
	msr	daifset, #3
	/*
	 * Disable interrupts, x18 may change in the interrupt exception
	 * handler.  For EL0 exceptions, do_ast already did this.
	 */
.endif
	ldp	x18,  lr, [sp, #(TF_SP)]
	ldp	x10, x11, [sp, #(TF_ELR)]
.if \el == 0
	msr	sp_el0, x18
.endif
	msr	spsr_el1, x11
	msr	elr_el1, x10
	ldp	x0,  x1,  [sp, #(TF_X + 0  * 8)]
	ldp	x2,  x3,  [sp, #(TF_X + 2  * 8)]
	ldp	x4,  x5,  [sp, #(TF_X + 4  * 8)]
	ldp	x6,  x7,  [sp, #(TF_X + 6  * 8)]
	ldp	x8,  x9,  [sp, #(TF_X + 8  * 8)]
	ldp	x10, x11, [sp, #(TF_X + 10 * 8)]
	ldp	x12, x13, [sp, #(TF_X + 12 * 8)]
	ldp	x14, x15, [sp, #(TF_X + 14 * 8)]
	ldp	x16, x17, [sp, #(TF_X + 16 * 8)]
.if \el == 0
	/*
	 * We only restore the callee saved registers when returning to
	 * userland as they may have been updated by a system call or signal.
	 */
	ldp	x18, x19, [sp, #(TF_X + 18 * 8)]
	ldp	x20, x21, [sp, #(TF_X + 20 * 8)]
	ldp	x22, x23, [sp, #(TF_X + 22 * 8)]
	ldp	x24, x25, [sp, #(TF_X + 24 * 8)]
	ldp	x26, x27, [sp, #(TF_X + 26 * 8)]
	ldp	x28, x29, [sp, #(TF_X + 28 * 8)]
.else
	ldr	     x29, [sp, #(TF_X + 29 * 8)]
.endif
.if \el == 0
	add	sp, sp, #(TF_SIZE + 16)
.else
	mov	sp, x18
	mrs	x18, tpidr_el1
.endif
.endm

.macro	do_ast
	/* Disable interrupts */
	mrs	x19, daif
1:
	msr	daifset, #3

	/* Check for astpending */
	mrs	x18, tpidr_el1
	ldr	x1, [x18, #CI_CURPROC]
	cbz	x1, 2f
	ldr	w2, [x1, #P_ASTPENDING]
	cbz	w2, 2f

	// clear astpending before enabling interrupts.
	str	wzr, [x1, #P_ASTPENDING]

	/* Restore interrupts */
	msr	daif, x19

	/* handle the ast */
	mov	x0, sp
	bl	ast
	b	1b
2:
.endm

.macro disable_ss
	mrs	x2, mdscr_el1
	and	x2, x2, #(~DBG_MDSCR_SS)
	msr	mdscr_el1, x2
.endm

.macro allow_ss
	mrs	x2, tpidr_el1
	ldr	x2, [x2, #(CI_CURPCB)]
	ldr	w2, [x2, #(PCB_FLAGS)]
	tbz	w2, #1, 1f	/* PCB_SINGLESTEP bit */
	mrs	x2, mdscr_el1
	orr	x2, x2, #(DBG_MDSCR_SS)
	msr	mdscr_el1, x2
1:
.endm

	.globl handle_el1h_sync
	.type handle_el1h_sync,@function
handle_el1h_sync:
	save_registers 1
	mov	x0, sp
	bl	do_el1h_sync
	restore_registers 1
	eret
	dsb nsh
	isb

	.globl handle_el1h_irq
	.type handle_el1h_irq,@function
handle_el1h_irq:
	save_registers 1
	mov	x0, sp
	bl	arm_cpu_irq
	restore_registers 1
	eret
	dsb nsh
	isb

	.globl handle_el1h_fiq
	.type handle_el1h_fiq,@function
handle_el1h_fiq:
	save_registers 1
	mov	x0, sp
	bl	arm_cpu_fiq
	restore_registers 1
	eret
	dsb nsh
	isb

	.globl handle_el1h_error
	.type handle_el1h_error,@function
handle_el1h_error:
	save_registers 1
	mov	x0, sp
	bl	do_el1h_error
	brk	0xf13
	1: b 1b

.macro	return
	msr	tpidrro_el0, x18
	mrs	x18, tpidr_el1
	ldr	x18, [x18, #CI_TRAMPOLINE_VECTORS]
	msr	vbar_el1, x18
	isb
	b	tramp_return
.endm

	.globl handle_el0_sync
	.type handle_el0_sync,@function
handle_el0_sync:
	save_registers 0
	disable_ss
	mov	x0, sp
	bl	do_el0_sync
	do_ast
	allow_ss
	restore_registers 0
	return

	.globl handle_el0_irq
	.type handle_el0_irq,@function
handle_el0_irq:
	save_registers 0
	disable_ss
	mov	x0, sp
	bl	arm_cpu_irq
	do_ast
	allow_ss
	restore_registers 0
	return

	.globl handle_el0_fiq
	.type handle_el0_fiq,@function
handle_el0_fiq:
	save_registers 0
	disable_ss
	mov	x0, sp
	bl	arm_cpu_fiq
	do_ast
	allow_ss
	restore_registers 0
	return

	.globl handle_el0_error
	.type handle_el0_error,@function
handle_el0_error:
	save_registers 0
	disable_ss
	mov	x0, sp
	bl	do_el0_error
	brk	0xf23
	1: b 1b

ENTRY(syscall_return)
	do_ast
	restore_registers 0
	return

.macro	vempty
	.align 7
	brk	0xfff
	1: b	1b
.endm

.macro	vector	name
	.align 7
	b	handle_\name
.endm

	.align 11
	.globl exception_vectors
exception_vectors:
	vempty			/* Synchronous EL1t */
	vempty			/* IRQ EL1t */
	vempty			/* FIQ EL1t */
	vempty			/* Error EL1t */

	vector el1h_sync	/* Synchronous EL1h */
	vector el1h_irq		/* IRQ EL1h */
	vector el1h_fiq		/* FIQ EL1h */
	vector el1h_error	/* Error EL1h */

	vempty			/* Synchronous 64-bit EL0 */
	vempty			/* IRQ 64-bit EL0 */
	vempty			/* FIQ 64-bit EL0 */
	vempty			/* Error 64-bit EL0 */

	vempty			/* Synchronous 32-bit EL0 */
	vempty			/* IRQ 32-bit EL0 */
	vempty			/* FIQ 32-bit EL0 */
	vempty			/* Error 32-bit EL0 */
