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
 *
 */

#include <machine/asm.h>
#include <machine/armreg.h>
__FBSDID("$FreeBSD$");

#include "assym.inc"

	.text

.macro	save_registers el
.if \el == 1
	mov	x18, sp
	sub	sp, sp, #128
.endif
	sub	sp, sp, #(TF_SIZE + 16)
	stp	x29, x30, [sp, #(TF_SIZE)]
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
	mrs	x12, esr_el1
.if \el == 0
	mrs	x18, sp_el0
.endif
	str	x10, [sp, #(TF_ELR)]
	stp	w11, w12, [sp, #(TF_SPSR)]
	stp	x18,  lr, [sp, #(TF_SP)]
	mrs	x18, tpidr_el1
	add	x29, sp, #(TF_SIZE)
.if \el == 0
	/* Apply the SSBD (CVE-2018-3639) workaround if needed */
	ldr	x1, [x18, #PC_SSBD]
	cbz	x1, 1f
	mov	w0, #1
	blr	x1
1:
.endif
.endm

.macro	restore_registers el
.if \el == 1
	msr	daifset, #2
	/*
	 * Disable interrupts, x18 may change in the interrupt exception
	 * handler.  For EL0 exceptions, do_ast already did this.
	 */
.endif
.if \el == 0
	/* Remove the SSBD (CVE-2018-3639) workaround if needed */
	ldr	x1, [x18, #PC_SSBD]
	cbz	x1, 1f
	mov	w0, #0
	blr	x1
1:
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
	mrs	x19, daif
	/* Make sure the IRQs are enabled before calling ast() */
	bic	x19, x19, #PSR_I
1:
	/* Disable interrupts */
	msr	daifset, #2

	/* Read the current thread flags */
	ldr	x1, [x18, #PC_CURTHREAD]	/* Load curthread */
	ldr	x2, [x1, #TD_FLAGS]

	/* Check if we have either bits set */
	mov	x3, #((TDF_ASTPENDING|TDF_NEEDRESCHED) >> 8)
	lsl	x3, x3, #8
	and	x2, x2, x3
	cbz	x2, 2f

	/* Restore interrupts */
	msr	daif, x19

	/* handle the ast */
	mov	x0, sp
	bl	_C_LABEL(ast)

	/* Re-check for new ast scheduled */
	b	1b
2:
.endm

ENTRY(handle_el1h_sync)
	save_registers 1
	ldr	x0, [x18, #PC_CURTHREAD]
	mov	x1, sp
	bl	do_el1h_sync
	restore_registers 1
	eret
END(handle_el1h_sync)

ENTRY(handle_el1h_irq)
	save_registers 1
	mov	x0, sp
	bl	intr_irq_handler
	restore_registers 1
	eret
END(handle_el1h_irq)

ENTRY(handle_el0_sync)
	save_registers 0
	ldr	x0, [x18, #PC_CURTHREAD]
	mov	x1, sp
	str	x1, [x0, #TD_FRAME]
	bl	do_el0_sync
	do_ast
	restore_registers 0
	eret
END(handle_el0_sync)

ENTRY(handle_el0_irq)
	save_registers 0
	mov	x0, sp
	bl	intr_irq_handler
	do_ast
	restore_registers 0
	eret
END(handle_el0_irq)

ENTRY(handle_serror)
	save_registers 0
	mov	x0, sp
1:	bl	do_serror
	b	1b
END(handle_serror)

ENTRY(handle_empty_exception)
	save_registers 0
	mov	x0, sp
1:	bl	unhandled_exception
	b	1b
END(handle_unhandled_exception)

.macro	vempty
	.align 7
	b	handle_empty_exception
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
	vempty			/* FIQ EL1h */
	vector serror		/* Error EL1h */

	vector el0_sync		/* Synchronous 64-bit EL0 */
	vector el0_irq		/* IRQ 64-bit EL0 */
	vempty			/* FIQ 64-bit EL0 */
	vector serror		/* Error 64-bit EL0 */

	vector el0_sync		/* Synchronous 32-bit EL0 */
	vector el0_irq		/* IRQ 32-bit EL0 */
	vempty			/* FIQ 32-bit EL0 */
	vector serror		/* Error 32-bit EL0 */

