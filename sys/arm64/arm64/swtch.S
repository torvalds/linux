/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under sponsorship from
 * the FreeBSD Foundation.
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

#include "assym.inc"
#include "opt_kstack_pages.h"
#include "opt_sched.h"

#include <machine/asm.h>

__FBSDID("$FreeBSD$");

.macro clear_step_flag pcbflags, tmp
	tbz	\pcbflags, #PCB_SINGLE_STEP_SHIFT, 999f
	mrs	\tmp, mdscr_el1
	bic	\tmp, \tmp, #1
	msr	mdscr_el1, \tmp
	isb
999:
.endm

.macro set_step_flag pcbflags, tmp
	tbz	\pcbflags, #PCB_SINGLE_STEP_SHIFT, 999f
	mrs	\tmp, mdscr_el1
	orr	\tmp, \tmp, #1
	msr	mdscr_el1, \tmp
	isb
999:
.endm

/*
 * void cpu_throw(struct thread *old, struct thread *new)
 */
ENTRY(cpu_throw)
	/* Of old == NULL skip disabling stepping */
	cbz	x0, 1f

	/* If we were single stepping, disable it */
	ldr	x4, [x0, #TD_PCB]
	ldr	w5, [x4, #PCB_FLAGS]
	clear_step_flag w5, x6
1:

#ifdef VFP
	/* Backup the new thread pointer around a call to C code */
	mov	x19, x0
	mov	x20, x1
	bl	vfp_discard
	mov	x1, x20
	mov	x0, x19
#endif

	bl	pmap_switch
	mov	x4, x0

	/* If we are single stepping, enable it */
	ldr	w5, [x4, #PCB_FLAGS]
	set_step_flag w5, x6

	/* Restore the registers */
	ldp	x5, x6, [x4, #PCB_SP]
	mov	sp, x5
	msr	tpidr_el0, x6
	ldr	x6, [x4, #PCB_TPIDRRO]
	msr	tpidrro_el0, x6
	ldp	x8, x9, [x4, #PCB_REGS + 8 * 8]
	ldp	x10, x11, [x4, #PCB_REGS + 10 * 8]
	ldp	x12, x13, [x4, #PCB_REGS + 12 * 8]
	ldp	x14, x15, [x4, #PCB_REGS + 14 * 8]
	ldp	x16, x17, [x4, #PCB_REGS + 16 * 8]
	ldr	     x19, [x4, #PCB_REGS + 19 * 8]
	ldp	x20, x21, [x4, #PCB_REGS + 20 * 8]
	ldp	x22, x23, [x4, #PCB_REGS + 22 * 8]
	ldp	x24, x25, [x4, #PCB_REGS + 24 * 8]
	ldp	x26, x27, [x4, #PCB_REGS + 26 * 8]
	ldp	x28, x29, [x4, #PCB_REGS + 28 * 8]
	ldr	x30, [x4, #PCB_REGS + 30 * 8]

	ret
END(cpu_throw)

/*
 * void cpu_switch(struct thread *old, struct thread *new, struct mtx *mtx)
 *
 * x0 = old
 * x1 = new
 * x2 = mtx
 * x3 to x7, x16 and x17 are caller saved
 */
ENTRY(cpu_switch)
	/*
	 * Save the old context.
	 */
	ldr	x4, [x0, #TD_PCB]

	/* Store the callee-saved registers */
	stp	x8, x9, [x4, #PCB_REGS + 8 * 8]
	stp	x10, x11, [x4, #PCB_REGS + 10 * 8]
	stp	x12, x13, [x4, #PCB_REGS + 12 * 8]
	stp	x14, x15, [x4, #PCB_REGS + 14 * 8]
	stp	x16, x17, [x4, #PCB_REGS + 16 * 8]
	stp	x18, x19, [x4, #PCB_REGS + 18 * 8]
	stp	x20, x21, [x4, #PCB_REGS + 20 * 8]
	stp	x22, x23, [x4, #PCB_REGS + 22 * 8]
	stp	x24, x25, [x4, #PCB_REGS + 24 * 8]
	stp	x26, x27, [x4, #PCB_REGS + 26 * 8]
	stp	x28, x29, [x4, #PCB_REGS + 28 * 8]
	str	x30, [x4, #PCB_REGS + 30 * 8]
	/* And the old stack pointer */
	mov	x5, sp
	mrs	x6, tpidrro_el0
	str	x6, [x4, #PCB_TPIDRRO]
	mrs	x6, tpidr_el0
	stp	x5, x6, [x4, #PCB_SP]

	/* If we were single stepping, disable it */
	ldr	w5, [x4, #PCB_FLAGS]
	clear_step_flag w5, x6

	mov	x19, x0
	mov	x20, x1
	mov	x21, x2

#ifdef VFP
	/* Load the pcb address */
	mov	x1, x4
	bl	vfp_save_state
	mov	x1, x20
	mov	x0, x19
#endif

	bl	pmap_switch
	/* Move the new pcb out of the way */
	mov	x4, x0

	mov	x2, x21
	mov	x1, x20
	mov	x0, x19

	/*
	 * Release the old thread.
	 */
	stlr	x2, [x0, #TD_LOCK]
#if defined(SCHED_ULE) && defined(SMP)
	/* Spin if TD_LOCK points to a blocked_lock */
	ldr	x2, =_C_LABEL(blocked_lock)
1:
	ldar	x3, [x1, #TD_LOCK]
	cmp	x3, x2
	b.eq	1b
#endif

	/* If we are single stepping, enable it */
	ldr	w5, [x4, #PCB_FLAGS]
	set_step_flag w5, x6

	/* Restore the registers */
	ldp	x5, x6, [x4, #PCB_SP]
	mov	sp, x5
	msr	tpidr_el0, x6
	ldr	x6, [x4, #PCB_TPIDRRO]
	msr	tpidrro_el0, x6
	ldp	x8, x9, [x4, #PCB_REGS + 8 * 8]
	ldp	x10, x11, [x4, #PCB_REGS + 10 * 8]
	ldp	x12, x13, [x4, #PCB_REGS + 12 * 8]
	ldp	x14, x15, [x4, #PCB_REGS + 14 * 8]
	ldp	x16, x17, [x4, #PCB_REGS + 16 * 8]
	ldr	     x19, [x4, #PCB_REGS + 19 * 8]
	ldp	x20, x21, [x4, #PCB_REGS + 20 * 8]
	ldp	x22, x23, [x4, #PCB_REGS + 22 * 8]
	ldp	x24, x25, [x4, #PCB_REGS + 24 * 8]
	ldp	x26, x27, [x4, #PCB_REGS + 26 * 8]
	ldp	x28, x29, [x4, #PCB_REGS + 28 * 8]
	ldr	x30, [x4, #PCB_REGS + 30 * 8]

	str	xzr, [x4, #PCB_REGS + 18 * 8]
	ret
.Lcpu_switch_panic_str:
	.asciz "cpu_switch: %p\0"
END(cpu_switch)

ENTRY(fork_trampoline)
	mov	x0, x8
	mov	x1, x9
	mov	x2, sp
	mov	fp, #0	/* Stack traceback stops here. */
	bl	_C_LABEL(fork_exit)

	/* Restore the registers other than x0 and x1 */
	ldp	x2, x3, [sp, #TF_X + 2 * 8]
	ldp	x4, x5, [sp, #TF_X + 4 * 8]
	ldp	x6, x7, [sp, #TF_X + 6 * 8]
	ldp	x8, x9, [sp, #TF_X + 8 * 8]
	ldp	x10, x11, [sp, #TF_X + 10 * 8]
	ldp	x12, x13, [sp, #TF_X + 12 * 8]
	ldp	x14, x15, [sp, #TF_X + 14 * 8]
	ldp	x16, x17, [sp, #TF_X + 16 * 8]
	ldr	     x19, [sp, #TF_X + 19 * 8]
	ldp	x20, x21, [sp, #TF_X + 20 * 8]
	ldp	x22, x23, [sp, #TF_X + 22 * 8]
	ldp	x24, x25, [sp, #TF_X + 24 * 8]
	ldp	x26, x27, [sp, #TF_X + 26 * 8]
	ldp	x28, x29, [sp, #TF_X + 28 * 8]

	/*
	 * Disable interrupts to avoid
	 * overwriting spsr_el1 and sp_el0 by an IRQ exception.
	 */
	msr	daifset, #2

	/* Restore sp and lr */
	ldp	x0, x1, [sp, #TF_SP]
	msr	sp_el0, x0
	mov	lr, x1

	/* Restore elr and spsr */
	ldp	x0, x1, [sp, #TF_ELR]
	msr	elr_el1, x0
	msr	spsr_el1, x1

	/* Finally x0 and x1 */
	ldp	x0, x1, [sp, #TF_X + 0 * 8]
	ldr	x18, [sp, #TF_X + 18 * 8]

	/*
	 * No need for interrupts reenabling since PSR
	 * will be set to the desired value anyway.
	 */
	eret
	
END(fork_trampoline)

ENTRY(savectx)
	/* Store the callee-saved registers */
	stp	x8,  x9,  [x0, #PCB_REGS + 8 * 8]
	stp	x10, x11, [x0, #PCB_REGS + 10 * 8]
	stp	x12, x13, [x0, #PCB_REGS + 12 * 8]
	stp	x14, x15, [x0, #PCB_REGS + 14 * 8]
	stp	x16, x17, [x0, #PCB_REGS + 16 * 8]
	stp	x18, x19, [x0, #PCB_REGS + 18 * 8]
	stp	x20, x21, [x0, #PCB_REGS + 20 * 8]
	stp	x22, x23, [x0, #PCB_REGS + 22 * 8]
	stp	x24, x25, [x0, #PCB_REGS + 24 * 8]
	stp	x26, x27, [x0, #PCB_REGS + 26 * 8]
	stp	x28, x29, [x0, #PCB_REGS + 28 * 8]
	str	x30, [x0, #PCB_REGS + 30 * 8]
	/* And the old stack pointer */
	mov	x5, sp
	mrs	x6, tpidrro_el0
	str	x6, [x0, #PCB_TPIDRRO]
	mrs	x6, tpidr_el0
	stp	x5, x6, [x0, #PCB_SP]

	/* Store the VFP registers */
#ifdef VFP
	mov	x28, lr
	mov	x1, x0			/* move pcb to the correct register */
	mov	x0, xzr			/* td = NULL */
	bl	vfp_save_state
	mov	lr, x28
#endif

	ret
END(savectx)

