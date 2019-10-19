/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 */

#include "basic_asm.h"
#include "gpr_asm.h"
#include "fpu_asm.h"
#include "vmx_asm.h"
#include "vsx_asm.h"

/*
 * Large caveat here being that the caller cannot expect the
 * signal to always be sent! The hardware can (AND WILL!) abort
 * the transaction between the tbegin and the tsuspend (however
 * unlikely it seems or infrequently it actually happens).
 * You have been warned.
 */
/* long tm_signal_self(pid_t pid, long *gprs, double *fps, vector *vms, vector *vss); */
FUNC_START(tm_signal_self_context_load)
	PUSH_BASIC_STACK(512)
	/*
	 * Don't strictly need to save and restore as it depends on if
	 * we're going to use them, however this reduces messy logic
	 */
	PUSH_VMX(STACK_FRAME_LOCAL(5,0),r8)
	PUSH_FPU(512)
	PUSH_NVREGS_BELOW_FPU(512)
	std r3, STACK_FRAME_PARAM(0)(sp) /* pid */
	std r4, STACK_FRAME_PARAM(1)(sp) /* gps */
	std r5, STACK_FRAME_PARAM(2)(sp) /* fps */
	std r6, STACK_FRAME_PARAM(3)(sp) /* vms */
	std r7, STACK_FRAME_PARAM(4)(sp) /* vss */

	ld r3, STACK_FRAME_PARAM(1)(sp)
	cmpdi r3, 0
	beq skip_gpr_lc
	bl load_gpr
skip_gpr_lc:
	ld r3, STACK_FRAME_PARAM(2)(sp)
	cmpdi	r3, 0
	beq	skip_fpu_lc
	bl load_fpu
skip_fpu_lc:
	ld r3, STACK_FRAME_PARAM(3)(sp)
	cmpdi r3, 0
	beq	skip_vmx_lc
	bl load_vmx
skip_vmx_lc:
	ld r3, STACK_FRAME_PARAM(4)(sp)
	cmpdi	r3, 0
	beq	skip_vsx_lc
	bl load_vsx
skip_vsx_lc:
	/*
	 * Set r3 (return value) before tbegin. Use the pid as a known
	 * 'all good' return value, zero is used to indicate a non-doomed
	 * transaction.
	 */
	ld	r3, STACK_FRAME_PARAM(0)(sp)
	tbegin.
	beq	1f
	tsuspend. /* Can't enter a syscall transactionally */
	ld	r3, STACK_FRAME_PARAM(1)(sp)
	cmpdi	r3, 0
	beq skip_gpr_lt
	/* Get the second half of the array */
	addi	r3, r3, 8 * 18
	bl load_gpr
skip_gpr_lt:
	ld r3, STACK_FRAME_PARAM(2)(sp)
	cmpdi	r3, 0
	beq	skip_fpu_lt
	/* Get the second half of the array */
	addi	r3, r3, 8 * 18
	bl load_fpu
skip_fpu_lt:
	ld r3, STACK_FRAME_PARAM(3)(sp)
	cmpdi r3, 0
	beq	skip_vmx_lt
	/* Get the second half of the array */
	addi	r3, r3, 16 * 12
	bl load_vmx
skip_vmx_lt:
	ld r3, STACK_FRAME_PARAM(4)(sp)
	cmpdi	r3, 0
	beq	skip_vsx_lt
	/* Get the second half of the array */
	addi	r3, r3, 16 * 12
	bl load_vsx
skip_vsx_lt:
	li	r0, 37 /* sys_kill */
	ld r3, STACK_FRAME_PARAM(0)(sp) /* pid */
	li r4, 10 /* SIGUSR1 */
	sc /* Taking the signal will doom the transaction */
	tabort. 0
	tresume. /* Be super sure we abort */
	/*
	 * This will cause us to resume doomed transaction and cause
	 * hardware to cleanup, we'll end up at 1: anything between
	 * tresume. and 1: shouldn't ever run.
	 */
	li r3, 0
	1:
	POP_VMX(STACK_FRAME_LOCAL(5,0),r4)
	POP_FPU(512)
	POP_NVREGS_BELOW_FPU(512)
	POP_BASIC_STACK(512)
	blr
FUNC_END(tm_signal_self_context_load)
