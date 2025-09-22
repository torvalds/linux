/*	$OpenBSD: lcore_float.S,v 1.23 2019/01/14 15:02:57 visa Exp $ */

/*
 * Copyright (c) 2001-2003 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/errno.h>
#include <sys/syscall.h>

#include <machine/param.h>
#include <machine/asm.h>
#include <machine/cpu.h>
#include <mips64/mips_cpu.h>
#include <machine/regnum.h>
#include <machine/cpustate.h>

#include "assym.h"

	.set	mips3
	.set	noreorder		# Noreorder is default style!
	.set	hardfloat

/*----------------------------------------------------------------------------
 *
 * MipsSwitchFPState --
 *
 *	Save the current state into 'from' and restore it from 'to'.
 *
 *	MipsSwitchFPState(from, to)
 *		struct proc *from;
 *		struct user *to;
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */
LEAF(MipsSwitchFPState, 0)
	MFC0	t1, COP_0_STATUS_REG	# Save old SR
	MFC0_HAZARD
	or	t0, t1, SR_COP_1_BIT|SR_FR_32	# enable the coprocessor
	MTC0	t0, COP_0_STATUS_REG
	MTC0_SR_CU_HAZARD

	beq	a0, zero, 1f		# skip save if NULL pointer
	 NOP
/*
 * First read out the status register to make sure that all FP operations
 * have completed.
 */
	PTR_L	a0, P_ADDR(a0)		# get pointer to pcb for proc
	cfc1	t0, FPC_CSR		# stall til FP done
	cfc1	t0, FPC_CSR		# now get status
	LI	t3, ~SR_COP_1_BIT
	REG_L	t2, PCB_REGS+(SR * REGSZ)(a0)	# get CPU status register
	REG_S	t0, PCB_FPREGS+(32 * REGSZ)(a0)	# save FP status
	and	t2, t2, t3			# clear COP_1 enable bit
	REG_S	t2, PCB_REGS+(SR * REGSZ)(a0)	# save new status register
/*
 * Save the floating point registers.
 */
	sdc1	$f0, PCB_FPREGS+(0 * REGSZ)(a0)
	sdc1	$f1, PCB_FPREGS+(1 * REGSZ)(a0)
	sdc1	$f2, PCB_FPREGS+(2 * REGSZ)(a0)
	sdc1	$f3, PCB_FPREGS+(3 * REGSZ)(a0)
	sdc1	$f4, PCB_FPREGS+(4 * REGSZ)(a0)
	sdc1	$f5, PCB_FPREGS+(5 * REGSZ)(a0)
	sdc1	$f6, PCB_FPREGS+(6 * REGSZ)(a0)
	sdc1	$f7, PCB_FPREGS+(7 * REGSZ)(a0)
	sdc1	$f8, PCB_FPREGS+(8 * REGSZ)(a0)
	sdc1	$f9, PCB_FPREGS+(9 * REGSZ)(a0)
	sdc1	$f10, PCB_FPREGS+(10 * REGSZ)(a0)
	sdc1	$f11, PCB_FPREGS+(11 * REGSZ)(a0)
	sdc1	$f12, PCB_FPREGS+(12 * REGSZ)(a0)
	sdc1	$f13, PCB_FPREGS+(13 * REGSZ)(a0)
	sdc1	$f14, PCB_FPREGS+(14 * REGSZ)(a0)
	sdc1	$f15, PCB_FPREGS+(15 * REGSZ)(a0)
	sdc1	$f16, PCB_FPREGS+(16 * REGSZ)(a0)
	sdc1	$f17, PCB_FPREGS+(17 * REGSZ)(a0)
	sdc1	$f18, PCB_FPREGS+(18 * REGSZ)(a0)
	sdc1	$f19, PCB_FPREGS+(19 * REGSZ)(a0)
	sdc1	$f20, PCB_FPREGS+(20 * REGSZ)(a0)
	sdc1	$f21, PCB_FPREGS+(21 * REGSZ)(a0)
	sdc1	$f22, PCB_FPREGS+(22 * REGSZ)(a0)
	sdc1	$f23, PCB_FPREGS+(23 * REGSZ)(a0)
	sdc1	$f24, PCB_FPREGS+(24 * REGSZ)(a0)
	sdc1	$f25, PCB_FPREGS+(25 * REGSZ)(a0)
	sdc1	$f26, PCB_FPREGS+(26 * REGSZ)(a0)
	sdc1	$f27, PCB_FPREGS+(27 * REGSZ)(a0)
	sdc1	$f28, PCB_FPREGS+(28 * REGSZ)(a0)
	sdc1	$f29, PCB_FPREGS+(29 * REGSZ)(a0)
	sdc1	$f30, PCB_FPREGS+(30 * REGSZ)(a0)
	sdc1	$f31, PCB_FPREGS+(31 * REGSZ)(a0)

1:
/*
 *  Restore the floating point registers.
 */
	REG_L	t0, PCB_FPREGS+(32 * REGSZ)(a1)	# get status register
	ldc1	$f0, PCB_FPREGS+(0 * REGSZ)(a1)
	ldc1	$f1, PCB_FPREGS+(1 * REGSZ)(a1)
	ldc1	$f2, PCB_FPREGS+(2 * REGSZ)(a1)
	ldc1	$f3, PCB_FPREGS+(3 * REGSZ)(a1)
	ldc1	$f4, PCB_FPREGS+(4 * REGSZ)(a1)
	ldc1	$f5, PCB_FPREGS+(5 * REGSZ)(a1)
	ldc1	$f6, PCB_FPREGS+(6 * REGSZ)(a1)
	ldc1	$f7, PCB_FPREGS+(7 * REGSZ)(a1)
	ldc1	$f8, PCB_FPREGS+(8 * REGSZ)(a1)
	ldc1	$f9, PCB_FPREGS+(9 * REGSZ)(a1)
	ldc1	$f10, PCB_FPREGS+(10 * REGSZ)(a1)
	ldc1	$f11, PCB_FPREGS+(11 * REGSZ)(a1)
	ldc1	$f12, PCB_FPREGS+(12 * REGSZ)(a1)
	ldc1	$f13, PCB_FPREGS+(13 * REGSZ)(a1)
	ldc1	$f14, PCB_FPREGS+(14 * REGSZ)(a1)
	ldc1	$f15, PCB_FPREGS+(15 * REGSZ)(a1)
	ldc1	$f16, PCB_FPREGS+(16 * REGSZ)(a1)
	ldc1	$f17, PCB_FPREGS+(17 * REGSZ)(a1)
	ldc1	$f18, PCB_FPREGS+(18 * REGSZ)(a1)
	ldc1	$f19, PCB_FPREGS+(19 * REGSZ)(a1)
	ldc1	$f20, PCB_FPREGS+(20 * REGSZ)(a1)
	ldc1	$f21, PCB_FPREGS+(21 * REGSZ)(a1)
	ldc1	$f22, PCB_FPREGS+(22 * REGSZ)(a1)
	ldc1	$f23, PCB_FPREGS+(23 * REGSZ)(a1)
	ldc1	$f24, PCB_FPREGS+(24 * REGSZ)(a1)
	ldc1	$f25, PCB_FPREGS+(25 * REGSZ)(a1)
	ldc1	$f26, PCB_FPREGS+(26 * REGSZ)(a1)
	ldc1	$f27, PCB_FPREGS+(27 * REGSZ)(a1)
	ldc1	$f28, PCB_FPREGS+(28 * REGSZ)(a1)
	ldc1	$f29, PCB_FPREGS+(29 * REGSZ)(a1)
	ldc1	$f30, PCB_FPREGS+(30 * REGSZ)(a1)
	ldc1	$f31, PCB_FPREGS+(31 * REGSZ)(a1)

	ctc1	t0, FPC_CSR
	NOP

	MTC0	t1, COP_0_STATUS_REG	# Restore the status register.
	MTC0_SR_CU_HAZARD
	j	ra
	 NOP
END(MipsSwitchFPState)

LEAF(MipsSwitchFPState16, 0)
	MFC0	t1, COP_0_STATUS_REG	# Save old SR
	MFC0_HAZARD
	or	t0, t1, SR_COP_1_BIT	# enable the coprocessor
	MTC0	t0, COP_0_STATUS_REG
	MTC0_SR_CU_HAZARD

	beq	a0, zero, 1f		# skip save if NULL pointer
	 NOP
/*
 * First read out the status register to make sure that all FP operations
 * have completed.
 */
	PTR_L	a0, P_ADDR(a0)		# get pointer to pcb for proc
	cfc1	t0, FPC_CSR		# stall til FP done
	cfc1	t0, FPC_CSR		# now get status
	LI	t3, ~SR_COP_1_BIT
	REG_L	t2, PCB_REGS+(SR * REGSZ)(a0)	# get CPU status register
	REG_S	t0, PCB_FPREGS+(32 * REGSZ)(a0)	# save FP status
	and	t2, t2, t3			# clear COP_1 enable bit
	REG_S	t2, PCB_REGS+(SR * REGSZ)(a0)	# save new status register
/*
 * Save the floating point registers.
 */
	swc1	$f0, PCB_FPREGS+(0 * REGSZ)(a0)
	swc1	$f1, PCB_FPREGS+(1 * REGSZ)(a0)
	swc1	$f2, PCB_FPREGS+(2 * REGSZ)(a0)
	swc1	$f3, PCB_FPREGS+(3 * REGSZ)(a0)
	swc1	$f4, PCB_FPREGS+(4 * REGSZ)(a0)
	swc1	$f5, PCB_FPREGS+(5 * REGSZ)(a0)
	swc1	$f6, PCB_FPREGS+(6 * REGSZ)(a0)
	swc1	$f7, PCB_FPREGS+(7 * REGSZ)(a0)
	swc1	$f8, PCB_FPREGS+(8 * REGSZ)(a0)
	swc1	$f9, PCB_FPREGS+(9 * REGSZ)(a0)
	swc1	$f10, PCB_FPREGS+(10 * REGSZ)(a0)
	swc1	$f11, PCB_FPREGS+(11 * REGSZ)(a0)
	swc1	$f12, PCB_FPREGS+(12 * REGSZ)(a0)
	swc1	$f13, PCB_FPREGS+(13 * REGSZ)(a0)
	swc1	$f14, PCB_FPREGS+(14 * REGSZ)(a0)
	swc1	$f15, PCB_FPREGS+(15 * REGSZ)(a0)
	swc1	$f16, PCB_FPREGS+(16 * REGSZ)(a0)
	swc1	$f17, PCB_FPREGS+(17 * REGSZ)(a0)
	swc1	$f18, PCB_FPREGS+(18 * REGSZ)(a0)
	swc1	$f19, PCB_FPREGS+(19 * REGSZ)(a0)
	swc1	$f20, PCB_FPREGS+(20 * REGSZ)(a0)
	swc1	$f21, PCB_FPREGS+(21 * REGSZ)(a0)
	swc1	$f22, PCB_FPREGS+(22 * REGSZ)(a0)
	swc1	$f23, PCB_FPREGS+(23 * REGSZ)(a0)
	swc1	$f24, PCB_FPREGS+(24 * REGSZ)(a0)
	swc1	$f25, PCB_FPREGS+(25 * REGSZ)(a0)
	swc1	$f26, PCB_FPREGS+(26 * REGSZ)(a0)
	swc1	$f27, PCB_FPREGS+(27 * REGSZ)(a0)
	swc1	$f28, PCB_FPREGS+(28 * REGSZ)(a0)
	swc1	$f29, PCB_FPREGS+(29 * REGSZ)(a0)
	swc1	$f30, PCB_FPREGS+(30 * REGSZ)(a0)
	swc1	$f31, PCB_FPREGS+(31 * REGSZ)(a0)

1:
/*
 *  Restore the floating point registers.
 */
	REG_L	t0, PCB_FPREGS+(32 * REGSZ)(a1)	# get status register
	lwc1	$f0, PCB_FPREGS+(0 * REGSZ)(a1)
	lwc1	$f1, PCB_FPREGS+(1 * REGSZ)(a1)
	lwc1	$f2, PCB_FPREGS+(2 * REGSZ)(a1)
	lwc1	$f3, PCB_FPREGS+(3 * REGSZ)(a1)
	lwc1	$f4, PCB_FPREGS+(4 * REGSZ)(a1)
	lwc1	$f5, PCB_FPREGS+(5 * REGSZ)(a1)
	lwc1	$f6, PCB_FPREGS+(6 * REGSZ)(a1)
	lwc1	$f7, PCB_FPREGS+(7 * REGSZ)(a1)
	lwc1	$f8, PCB_FPREGS+(8 * REGSZ)(a1)
	lwc1	$f9, PCB_FPREGS+(9 * REGSZ)(a1)
	lwc1	$f10, PCB_FPREGS+(10 * REGSZ)(a1)
	lwc1	$f11, PCB_FPREGS+(11 * REGSZ)(a1)
	lwc1	$f12, PCB_FPREGS+(12 * REGSZ)(a1)
	lwc1	$f13, PCB_FPREGS+(13 * REGSZ)(a1)
	lwc1	$f14, PCB_FPREGS+(14 * REGSZ)(a1)
	lwc1	$f15, PCB_FPREGS+(15 * REGSZ)(a1)
	lwc1	$f16, PCB_FPREGS+(16 * REGSZ)(a1)
	lwc1	$f17, PCB_FPREGS+(17 * REGSZ)(a1)
	lwc1	$f18, PCB_FPREGS+(18 * REGSZ)(a1)
	lwc1	$f19, PCB_FPREGS+(19 * REGSZ)(a1)
	lwc1	$f20, PCB_FPREGS+(20 * REGSZ)(a1)
	lwc1	$f21, PCB_FPREGS+(21 * REGSZ)(a1)
	lwc1	$f22, PCB_FPREGS+(22 * REGSZ)(a1)
	lwc1	$f23, PCB_FPREGS+(23 * REGSZ)(a1)
	lwc1	$f24, PCB_FPREGS+(24 * REGSZ)(a1)
	lwc1	$f25, PCB_FPREGS+(25 * REGSZ)(a1)
	lwc1	$f26, PCB_FPREGS+(26 * REGSZ)(a1)
	lwc1	$f27, PCB_FPREGS+(27 * REGSZ)(a1)
	lwc1	$f28, PCB_FPREGS+(28 * REGSZ)(a1)
	lwc1	$f29, PCB_FPREGS+(29 * REGSZ)(a1)
	lwc1	$f30, PCB_FPREGS+(30 * REGSZ)(a1)
	lwc1	$f31, PCB_FPREGS+(31 * REGSZ)(a1)

	ctc1	t0, FPC_CSR
	NOP

	MTC0	t1, COP_0_STATUS_REG	# Restore the status register.
	MTC0_SR_CU_HAZARD
	j	ra
	 NOP
END(MipsSwitchFPState16)

/*----------------------------------------------------------------------------
 *
 * MipsSaveCurFPState --
 *
 *	Save the current floating point coprocessor state.
 *
 *	MipsSaveCurFPState(p)
 *		struct proc *p;
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	curcpu()->ci_fpuproc is cleared.
 *
 *----------------------------------------------------------------------------
 */
LEAF(MipsSaveCurFPState, 0)
	PTR_L	a0, P_ADDR(a0)			# get pointer to pcb for proc
	MFC0	t1, COP_0_STATUS_REG	# Disable interrupts and
	MFC0_HAZARD
	or	t0, t1, SR_COP_1_BIT|SR_FR_32	# enable the coprocessor
	MTC0	t0, COP_0_STATUS_REG
	MTC0_SR_IE_HAZARD
	GET_CPU_INFO(t2, t3)
	PTR_S	zero, CI_FPUPROC(t2)		# indicate state has been saved
/*
 * First read out the status register to make sure that all FP operations
 * have completed.
 */
	REG_L	t2, PCB_REGS+(SR * REGSZ)(a0)	# get CPU status register
	LI	t3, ~SR_COP_1_BIT
	and	t2, t2, t3			# clear COP_1 enable bit
	cfc1	t0, FPC_CSR		# stall til FP done
	cfc1	t0, FPC_CSR		# now get status
	REG_S	t2, PCB_REGS+(SR * REGSZ)(a0)	# save new status register
	REG_S	t0, PCB_FPREGS+(32 * REGSZ)(a0)	# save FP status
/*
 * Save the floating point registers.
 */
	sdc1	$f0, PCB_FPREGS+(0 * REGSZ)(a0)
	sdc1	$f1, PCB_FPREGS+(1 * REGSZ)(a0)
	sdc1	$f2, PCB_FPREGS+(2 * REGSZ)(a0)
	sdc1	$f3, PCB_FPREGS+(3 * REGSZ)(a0)
	sdc1	$f4, PCB_FPREGS+(4 * REGSZ)(a0)
	sdc1	$f5, PCB_FPREGS+(5 * REGSZ)(a0)
	sdc1	$f6, PCB_FPREGS+(6 * REGSZ)(a0)
	sdc1	$f7, PCB_FPREGS+(7 * REGSZ)(a0)
	sdc1	$f8, PCB_FPREGS+(8 * REGSZ)(a0)
	sdc1	$f9, PCB_FPREGS+(9 * REGSZ)(a0)
	sdc1	$f10, PCB_FPREGS+(10 * REGSZ)(a0)
	sdc1	$f11, PCB_FPREGS+(11 * REGSZ)(a0)
	sdc1	$f12, PCB_FPREGS+(12 * REGSZ)(a0)
	sdc1	$f13, PCB_FPREGS+(13 * REGSZ)(a0)
	sdc1	$f14, PCB_FPREGS+(14 * REGSZ)(a0)
	sdc1	$f15, PCB_FPREGS+(15 * REGSZ)(a0)
	sdc1	$f16, PCB_FPREGS+(16 * REGSZ)(a0)
	sdc1	$f17, PCB_FPREGS+(17 * REGSZ)(a0)
	sdc1	$f18, PCB_FPREGS+(18 * REGSZ)(a0)
	sdc1	$f19, PCB_FPREGS+(19 * REGSZ)(a0)
	sdc1	$f20, PCB_FPREGS+(20 * REGSZ)(a0)
	sdc1	$f21, PCB_FPREGS+(21 * REGSZ)(a0)
	sdc1	$f22, PCB_FPREGS+(22 * REGSZ)(a0)
	sdc1	$f23, PCB_FPREGS+(23 * REGSZ)(a0)
	sdc1	$f24, PCB_FPREGS+(24 * REGSZ)(a0)
	sdc1	$f25, PCB_FPREGS+(25 * REGSZ)(a0)
	sdc1	$f26, PCB_FPREGS+(26 * REGSZ)(a0)
	sdc1	$f27, PCB_FPREGS+(27 * REGSZ)(a0)
	sdc1	$f28, PCB_FPREGS+(28 * REGSZ)(a0)
	sdc1	$f29, PCB_FPREGS+(29 * REGSZ)(a0)
	sdc1	$f30, PCB_FPREGS+(30 * REGSZ)(a0)
	sdc1	$f31, PCB_FPREGS+(31 * REGSZ)(a0)

	MTC0	t1, COP_0_STATUS_REG	# Restore the status register.
	MTC0_SR_IE_HAZARD
	j	ra
	 NOP
END(MipsSaveCurFPState)

LEAF(MipsSaveCurFPState16, 0)
	PTR_L	a0, P_ADDR(a0)			# get pointer to pcb for proc
	MFC0	t1, COP_0_STATUS_REG	# Disable interrupts and
	MFC0_HAZARD
	or	t0, t1, SR_COP_1_BIT	# enable the coprocessor
	MTC0	t0, COP_0_STATUS_REG
	MTC0_SR_IE_HAZARD
	GET_CPU_INFO(t2, t3)
	PTR_S	zero, CI_FPUPROC(t2)		# indicate state has been saved
/*
 * First read out the status register to make sure that all FP operations
 * have completed.
 */
	REG_L	t2, PCB_REGS+(SR * REGSZ)(a0)	# get CPU status register
	LI	t3, ~SR_COP_1_BIT
	and	t2, t2, t3			# clear COP_1 enable bit
	cfc1	t0, FPC_CSR		# stall til FP done
	cfc1	t0, FPC_CSR		# now get status
	REG_S	t2, PCB_REGS+(SR * REGSZ)(a0)	# save new status register
	REG_S	t0, PCB_FPREGS+(32 * REGSZ)(a0)	# save FP status
/*
 * Save the floating point registers.
 */
	swc1	$f0, PCB_FPREGS+(0 * REGSZ)(a0)
	swc1	$f1, PCB_FPREGS+(1 * REGSZ)(a0)
	swc1	$f2, PCB_FPREGS+(2 * REGSZ)(a0)
	swc1	$f3, PCB_FPREGS+(3 * REGSZ)(a0)
	swc1	$f4, PCB_FPREGS+(4 * REGSZ)(a0)
	swc1	$f5, PCB_FPREGS+(5 * REGSZ)(a0)
	swc1	$f6, PCB_FPREGS+(6 * REGSZ)(a0)
	swc1	$f7, PCB_FPREGS+(7 * REGSZ)(a0)
	swc1	$f8, PCB_FPREGS+(8 * REGSZ)(a0)
	swc1	$f9, PCB_FPREGS+(9 * REGSZ)(a0)
	swc1	$f10, PCB_FPREGS+(10 * REGSZ)(a0)
	swc1	$f11, PCB_FPREGS+(11 * REGSZ)(a0)
	swc1	$f12, PCB_FPREGS+(12 * REGSZ)(a0)
	swc1	$f13, PCB_FPREGS+(13 * REGSZ)(a0)
	swc1	$f14, PCB_FPREGS+(14 * REGSZ)(a0)
	swc1	$f15, PCB_FPREGS+(15 * REGSZ)(a0)
	swc1	$f16, PCB_FPREGS+(16 * REGSZ)(a0)
	swc1	$f17, PCB_FPREGS+(17 * REGSZ)(a0)
	swc1	$f18, PCB_FPREGS+(18 * REGSZ)(a0)
	swc1	$f19, PCB_FPREGS+(19 * REGSZ)(a0)
	swc1	$f20, PCB_FPREGS+(20 * REGSZ)(a0)
	swc1	$f21, PCB_FPREGS+(21 * REGSZ)(a0)
	swc1	$f22, PCB_FPREGS+(22 * REGSZ)(a0)
	swc1	$f23, PCB_FPREGS+(23 * REGSZ)(a0)
	swc1	$f24, PCB_FPREGS+(24 * REGSZ)(a0)
	swc1	$f25, PCB_FPREGS+(25 * REGSZ)(a0)
	swc1	$f26, PCB_FPREGS+(26 * REGSZ)(a0)
	swc1	$f27, PCB_FPREGS+(27 * REGSZ)(a0)
	swc1	$f28, PCB_FPREGS+(28 * REGSZ)(a0)
	swc1	$f29, PCB_FPREGS+(29 * REGSZ)(a0)
	swc1	$f30, PCB_FPREGS+(30 * REGSZ)(a0)
	swc1	$f31, PCB_FPREGS+(31 * REGSZ)(a0)

	MTC0	t1, COP_0_STATUS_REG	# Restore the status register.
	MTC0_SR_IE_HAZARD
	j	ra
	 NOP
END(MipsSaveCurFPState16)

/*----------------------------------------------------------------------------
 *
 * cp1_get_prid
 *
 *	Get the floating point co-processor id.
 *
 *	cp1_get_prid(void)
 *
 * Results:
 *	FPC_ID
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */
LEAF(cp1_get_prid, 0)
	MFC0	v1, COP_0_STATUS_REG
	MFC0_HAZARD
	li	a0, SR_COP_1_BIT
	or	v1, a0
	MTC0	v1, COP_0_STATUS_REG
	MTC0_SR_CU_HAZARD
	cfc1	v0, FPC_ID
	xor	v1, a0
	MTC0	v1, COP_0_STATUS_REG
	MTC0_SR_CU_HAZARD
	jr	ra
	 NOP
END(cp1_get_prid)
