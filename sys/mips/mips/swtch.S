/*	$OpenBSD: locore.S,v 1.18 1998/09/15 10:58:53 pefo Exp $	*/
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Digital Equipment Corporation and Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (C) 1989 Digital Equipment Corporation.
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies.
 * Digital Equipment Corporation makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * from: Header: /sprite/src/kernel/mach/ds3100.md/RCS/loMem.s,
 *	v 1.1 89/07/11 17:55:04 nelson Exp  SPRITE (DECWRL)
 * from: Header: /sprite/src/kernel/mach/ds3100.md/RCS/machAsm.s,
 *	v 9.2 90/01/29 18:00:39 shirriff Exp  SPRITE (DECWRL)
 * from: Header: /sprite/src/kernel/vm/ds3100.md/vmPmaxAsm.s,
 *	v 1.1 89/07/10 14:27:41 nelson Exp  SPRITE (DECWRL)
 *
 *	from: @(#)locore.s	8.5 (Berkeley) 1/4/94
 *	JNPR: swtch.S,v 1.6.2.1 2007/09/10 10:36:50 girish
 * $FreeBSD$
 */

/*
 *	Contains code that is the first executed at boot time plus
 *	assembly language support routines.
 */

#include <sys/syscall.h>
#include <machine/asm.h>
#include <machine/cpu.h>
#include <machine/cpuregs.h>
#include <machine/regnum.h>
#include <machine/pte.h>
#include <machine/pcb.h>

#include "assym.inc"

	.set	noreorder			# Noreorder is default style!

/*
 * Setup for and return to user.
 */
LEAF(fork_trampoline)
	move	a0,s0
	move	a1,s1
	jal	_C_LABEL(fork_exit)
	move	a2,s2			  #BDSlot

	DO_AST

	mfc0	v0, MIPS_COP_0_STATUS
	and     v0, ~(MIPS_SR_INT_IE)
	mtc0	v0, MIPS_COP_0_STATUS	# disable interrupts
	COP0_SYNC
/*
 * The use of k1 for storing the PCB pointer must be done only
 * after interrupts are disabled.  Otherwise it will get overwritten
 * by the interrupt code.
 */
	.set	noat
	GET_CPU_PCPU(k1)
	PTR_L	k1, PC_CURPCB(k1)

	RESTORE_U_PCB_REG(t0, MULLO, k1)
	RESTORE_U_PCB_REG(t1, MULHI, k1)
	mtlo	t0
	mthi	t1
	RESTORE_U_PCB_REG(a0, PC, k1)
	RESTORE_U_PCB_REG(AT, AST, k1)
	RESTORE_U_PCB_REG(v0, V0, k1)
	MTC0	a0, MIPS_COP_0_EXC_PC	# set return address

	RESTORE_U_PCB_REG(v1, V1, k1)
	RESTORE_U_PCB_REG(a0, A0, k1)
	RESTORE_U_PCB_REG(a1, A1, k1)
	RESTORE_U_PCB_REG(a2, A2, k1)
	RESTORE_U_PCB_REG(a3, A3, k1)
	RESTORE_U_PCB_REG(t0, T0, k1)
	RESTORE_U_PCB_REG(t1, T1, k1)
	RESTORE_U_PCB_REG(t2, T2, k1)
	RESTORE_U_PCB_REG(t3, T3, k1)
	RESTORE_U_PCB_REG(ta0, TA0, k1)
	RESTORE_U_PCB_REG(ta1, TA1, k1)
	RESTORE_U_PCB_REG(ta2, TA2, k1)
	RESTORE_U_PCB_REG(ta3, TA3, k1)
	RESTORE_U_PCB_REG(s0, S0, k1)
	RESTORE_U_PCB_REG(s1, S1, k1)
	RESTORE_U_PCB_REG(s2, S2, k1)
	RESTORE_U_PCB_REG(s3, S3, k1)
	RESTORE_U_PCB_REG(s4, S4, k1)
	RESTORE_U_PCB_REG(s5, S5, k1)
	RESTORE_U_PCB_REG(s6, S6, k1)
	RESTORE_U_PCB_REG(s7, S7, k1)
	RESTORE_U_PCB_REG(t8, T8, k1)
	RESTORE_U_PCB_REG(t9, T9, k1)
	RESTORE_U_PCB_REG(k0, SR, k1)
	RESTORE_U_PCB_REG(gp, GP, k1)
	RESTORE_U_PCB_REG(s8, S8, k1)
	RESTORE_U_PCB_REG(ra, RA, k1)
	RESTORE_U_PCB_REG(sp, SP, k1)
	li	k1, ~MIPS_SR_INT_MASK
	and	k0, k0, k1
	mfc0	k1, MIPS_COP_0_STATUS
	and	k1, k1, MIPS_SR_INT_MASK
	or	k0, k0, k1
	mtc0	k0, MIPS_COP_0_STATUS	# switch to user mode (when eret...)
	HAZARD_DELAY
	sync
	eret
	.set	at
END(fork_trampoline)

/*
 * Update pcb, saving current processor state.
 * Note: this only works if pcbp != curproc's pcb since
 * cpu_switch() will copy over pcb_context.
 *
 *	savectx(struct pcb *pcbp);
 */
LEAF(savectx)
	SAVE_U_PCB_CONTEXT(s0, PCB_REG_S0, a0)
	SAVE_U_PCB_CONTEXT(s1, PCB_REG_S1, a0)
	SAVE_U_PCB_CONTEXT(s2, PCB_REG_S2, a0)
	SAVE_U_PCB_CONTEXT(s3, PCB_REG_S3, a0)
	mfc0	v0, MIPS_COP_0_STATUS
	SAVE_U_PCB_CONTEXT(s4, PCB_REG_S4, a0)
	SAVE_U_PCB_CONTEXT(s5, PCB_REG_S5, a0)
	SAVE_U_PCB_CONTEXT(s6, PCB_REG_S6, a0)
	SAVE_U_PCB_CONTEXT(s7, PCB_REG_S7, a0)
	SAVE_U_PCB_CONTEXT(sp, PCB_REG_SP, a0)
	SAVE_U_PCB_CONTEXT(s8, PCB_REG_S8, a0)
	SAVE_U_PCB_CONTEXT(ra, PCB_REG_RA, a0)
	SAVE_U_PCB_CONTEXT(v0, PCB_REG_SR, a0)
	SAVE_U_PCB_CONTEXT(gp, PCB_REG_GP, a0)

	move	v0, ra			/* save 'ra' before we trash it */
	jal	1f
	nop
1:
	SAVE_U_PCB_CONTEXT(ra, PCB_REG_PC, a0)
	move	ra, v0			/* restore 'ra' before returning */

	j	ra
	move	v0, zero
END(savectx)

NESTED(cpu_throw, CALLFRAME_SIZ, ra)
	mfc0	t0, MIPS_COP_0_STATUS		# t0 = saved status register
	nop
	nop
	and     a3, t0, ~(MIPS_SR_INT_IE)
	mtc0	a3, MIPS_COP_0_STATUS		# Disable all interrupts
	ITLBNOPFIX
	j	mips_sw1			# We're not interested in old 
						# thread's context, so jump 
						# right to action
	nop					# BDSLOT
END(cpu_throw)

/*
 * cpu_switch(struct thread *old, struct thread *new, struct mutex *mtx);
 *	a0 - old
 *	a1 - new
 *	a2 - mtx
 * Find the highest priority process and resume it.
 */
NESTED(cpu_switch, CALLFRAME_SIZ, ra)
	mfc0	t0, MIPS_COP_0_STATUS		# t0 = saved status register
	nop
	nop
	and     a3, t0, ~(MIPS_SR_INT_IE)	
	mtc0	a3, MIPS_COP_0_STATUS		# Disable all interrupts
	ITLBNOPFIX
	beqz	a0, mips_sw1
	move	a3, a0
	PTR_L	a0, TD_PCB(a0)		# load PCB addr of curproc
	SAVE_U_PCB_CONTEXT(sp, PCB_REG_SP, a0)		# save old sp
	PTR_SUBU	sp, sp, CALLFRAME_SIZ
	REG_S	ra, CALLFRAME_RA(sp)
	.mask	0x80000000, (CALLFRAME_RA - CALLFRAME_SIZ)
	SAVE_U_PCB_CONTEXT(s0, PCB_REG_S0, a0)		# do a 'savectx()'
	SAVE_U_PCB_CONTEXT(s1, PCB_REG_S1, a0)
	SAVE_U_PCB_CONTEXT(s2, PCB_REG_S2, a0)
	SAVE_U_PCB_CONTEXT(s3, PCB_REG_S3, a0)
	SAVE_U_PCB_CONTEXT(s4, PCB_REG_S4, a0)
	SAVE_U_PCB_CONTEXT(s5, PCB_REG_S5, a0)
	SAVE_U_PCB_CONTEXT(s6, PCB_REG_S6, a0)
	SAVE_U_PCB_CONTEXT(s7, PCB_REG_S7, a0)
	SAVE_U_PCB_CONTEXT(s8, PCB_REG_S8, a0)
	SAVE_U_PCB_CONTEXT(ra, PCB_REG_RA, a0)		# save return address
	SAVE_U_PCB_CONTEXT(t0, PCB_REG_SR, a0)		# save status register
	SAVE_U_PCB_CONTEXT(gp, PCB_REG_GP, a0)
	jal	getpc
	nop
getpc:
	SAVE_U_PCB_CONTEXT(ra, PCB_REG_PC, a0)		# save return address

#ifdef CPU_CNMIPS

	lw	t2, TD_MDFLAGS(a3)		# get md_flags
	and	t1, t2, MDTD_COP2USED
	beqz	t1, cop2_untouched
	nop

	/* Clear cop2used flag */
	and	t2, t2, ~MDTD_COP2USED
	sw	t2, TD_MDFLAGS(a3)

	and	t2, t0, ~MIPS_SR_COP_2_BIT	# clear COP_2 enable bit
	SAVE_U_PCB_CONTEXT(t2, PCB_REG_SR, a0)	# save status register

	RESTORE_U_PCB_REG(t0, PS, a0)		# get CPU status register
	and	t2, t0, ~MIPS_SR_COP_2_BIT	# clear COP_2 enable bit
	SAVE_U_PCB_REG(t2, PS, a0)		# save stratus register

	/* preserve a0..a3 */
	move	s0, a0
	move	s1, a1
	move	s2, a2
	move	s3, a3

	/* does kernel own COP2 context? */
	lw	t1, TD_COP2OWNER(a3)		# get md_cop2owner
	beqz	t1, userland_cop2		# 0 - it's userland context
	nop

	PTR_L	a0, TD_COP2(a3)
	beqz	a0, no_cop2_context
	nop

	j	do_cop2_save
	nop

userland_cop2:

	PTR_L	a0, TD_UCOP2(a3)
	beqz	a0, no_cop2_context
	nop

do_cop2_save:
	jal	octeon_cop2_save
	nop

no_cop2_context:
	move	a3, s3
	move	a2, s2
	move	a1, s1
	move	a0, s0

cop2_untouched:
#endif

	PTR_S	a2, TD_LOCK(a3)			# Switchout td_lock 

mips_sw1:
#if defined(SMP) && defined(SCHED_ULE)
	PTR_LA	t0, _C_LABEL(blocked_lock)
blocked_loop:
	PTR_L	t1, TD_LOCK(a1)
	beq	t0, t1, blocked_loop
	nop
#endif
	move	s7, a1	# Store newthread
/*
 * Switch to new context.
 */
	GET_CPU_PCPU(a3)
	PTR_S	a1, PC_CURTHREAD(a3)
	PTR_L	a2, TD_PCB(a1)
	PTR_S	a2, PC_CURPCB(a3)
	PTR_L	v0, TD_KSTACK(a1)
#if defined(__mips_n64)
	PTR_LI	s0, MIPS_XKSEG_START
#else
	PTR_LI	s0, MIPS_KSEG2_START		# If Uarea addr is below kseg2,
#endif
	bltu	v0, s0, sw2			# no need to insert in TLB.
	PTE_L	a1, TD_UPTE + 0(s7)		# a1 = u. pte #0
	PTE_L	a2, TD_UPTE + PTESIZE(s7)	# a2 = u. pte #1
/*
 * Wiredown the USPACE of newproc in TLB entry#0.  Check whether target
 * USPACE is already in another place of TLB before that, and if so
 * invalidate that TLB entry.
 * NOTE: This is hard coded to UPAGES == 2.
 * Also, there should be no TLB faults at this point.
 */
	MTC0	v0, MIPS_COP_0_TLB_HI		# VPN = va
	HAZARD_DELAY
	tlbp					# probe VPN
	HAZARD_DELAY
	mfc0	s0, MIPS_COP_0_TLB_INDEX
	HAZARD_DELAY

	PTR_LI	t1, MIPS_KSEG0_START		# invalidate tlb entry
	bltz	s0, entry0set
	nop
	sll	s0, PAGE_SHIFT + 1
	addu	t1, s0
	MTC0	t1, MIPS_COP_0_TLB_HI
	PTE_MTC0	zero, MIPS_COP_0_TLB_LO0
	PTE_MTC0	zero, MIPS_COP_0_TLB_LO1
	HAZARD_DELAY
	tlbwi
	HAZARD_DELAY
	MTC0	v0, MIPS_COP_0_TLB_HI		# set VPN again

entry0set:
/* SMP!! - Works only for  unshared TLB case - i.e. no v-cpus */
	mtc0	zero, MIPS_COP_0_TLB_INDEX		# TLB entry #0
	HAZARD_DELAY
	PTE_MTC0	a1, MIPS_COP_0_TLB_LO0		# upte[0]
	HAZARD_DELAY
	PTE_MTC0	a2, MIPS_COP_0_TLB_LO1		# upte[1]
	HAZARD_DELAY
	tlbwi					# set TLB entry #0
	HAZARD_DELAY
/*
 * Now running on new u struct.
 */
sw2:
	PTR_L	s0, TD_PCB(s7)
	RESTORE_U_PCB_CONTEXT(sp, PCB_REG_SP, s0)
	PTR_LA	t1, _C_LABEL(pmap_activate)	# s7 = new proc pointer
	jalr	t1				# s7 = new proc pointer
	move	a0, s7				# BDSLOT
/*
 * Restore registers and return.
 */
	move	a0, s0
	move	a1, s7
	RESTORE_U_PCB_CONTEXT(gp, PCB_REG_GP, a0)
	RESTORE_U_PCB_CONTEXT(v0, PCB_REG_SR, a0)	# restore kernel context
	RESTORE_U_PCB_CONTEXT(ra, PCB_REG_RA, a0)
	RESTORE_U_PCB_CONTEXT(s0, PCB_REG_S0, a0)
	RESTORE_U_PCB_CONTEXT(s1, PCB_REG_S1, a0)
	RESTORE_U_PCB_CONTEXT(s2, PCB_REG_S2, a0)
	RESTORE_U_PCB_CONTEXT(s3, PCB_REG_S3, a0)
	RESTORE_U_PCB_CONTEXT(s4, PCB_REG_S4, a0)
	RESTORE_U_PCB_CONTEXT(s5, PCB_REG_S5, a0)
	RESTORE_U_PCB_CONTEXT(s6, PCB_REG_S6, a0)
	RESTORE_U_PCB_CONTEXT(s7, PCB_REG_S7, a0)
	RESTORE_U_PCB_CONTEXT(s8, PCB_REG_S8, a0)

	mfc0	t0, MIPS_COP_0_STATUS
	and	t0, t0, MIPS_SR_INT_MASK
	and	v0, v0, ~MIPS_SR_INT_MASK
	or	v0, v0, t0
	mtc0	v0, MIPS_COP_0_STATUS
	ITLBNOPFIX
/*
 * Set the new thread's TLS pointer.
 *
 * Note that this code is removed if the CPU doesn't support ULRI by
 * remove_userlocal_code() in cpu.c.
 */
	PTR_L	t0, TD_MDTLS(a1)		# Get TLS pointer
	PTR_L	t1, TD_MDTLS_TCB_OFFSET(a1)	# Get TLS/TCB offset
	PTR_ADDU v0, t0, t1
	MTC0	v0, MIPS_COP_0_USERLOCAL, 2	# write it to ULR for rdhwr

	j	ra
	nop
END(cpu_switch)

/*----------------------------------------------------------------------------
 *
 * MipsSwitchFPState --
 *
 *	Save the current state into 'from' and restore it from 'to'.
 *
 *	MipsSwitchFPState(from, to)
 *		struct thread *from;
 *		struct trapframe *to;
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */
LEAF(MipsSwitchFPState)
	.set push
	.set hardfloat
	mfc0	t1, MIPS_COP_0_STATUS	# Save old SR
	HAZARD_DELAY
#if defined(__mips_n32) || defined(__mips_n64)
	or	t0, t1, MIPS_SR_COP_1_BIT | MIPS_SR_FR	# enable the coprocessor
#else
	or	t0, t1, MIPS_SR_COP_1_BIT 		# enable the coprocessor
#endif
	mtc0	t0, MIPS_COP_0_STATUS
	HAZARD_DELAY
	ITLBNOPFIX

	beq	a0, zero, 1f		# skip save if NULL pointer
	nop
/*
 * First read out the status register to make sure that all FP operations
 * have completed.
 */
	PTR_L	a0, TD_PCB(a0)			# get pointer to pcb for proc
	cfc1	t0, MIPS_FPU_CSR		# stall til FP done
	cfc1	t0, MIPS_FPU_CSR		# now get status
	li	t3, ~MIPS_SR_COP_1_BIT
	RESTORE_U_PCB_REG(t2, PS, a0)		# get CPU status register
	SAVE_U_PCB_FPSR(t0, FSR_NUM, a0)	# save FP status
	and	t2, t2, t3			# clear COP_1 enable bit
	SAVE_U_PCB_REG(t2, PS, a0)		# save new status register
/*
 * Save the floating point registers.
 */
	SAVE_U_PCB_FPREG($f0, F0_NUM, a0)
	SAVE_U_PCB_FPREG($f1, F1_NUM, a0)
	SAVE_U_PCB_FPREG($f2, F2_NUM, a0)
	SAVE_U_PCB_FPREG($f3, F3_NUM, a0)
	SAVE_U_PCB_FPREG($f4, F4_NUM, a0)
	SAVE_U_PCB_FPREG($f5, F5_NUM, a0)
	SAVE_U_PCB_FPREG($f6, F6_NUM, a0)
	SAVE_U_PCB_FPREG($f7, F7_NUM, a0)
	SAVE_U_PCB_FPREG($f8, F8_NUM, a0)
	SAVE_U_PCB_FPREG($f9, F9_NUM, a0)
	SAVE_U_PCB_FPREG($f10, F10_NUM, a0)
	SAVE_U_PCB_FPREG($f11, F11_NUM, a0)
	SAVE_U_PCB_FPREG($f12, F12_NUM, a0)
	SAVE_U_PCB_FPREG($f13, F13_NUM, a0)
	SAVE_U_PCB_FPREG($f14, F14_NUM, a0)
	SAVE_U_PCB_FPREG($f15, F15_NUM, a0)
	SAVE_U_PCB_FPREG($f16, F16_NUM, a0)
	SAVE_U_PCB_FPREG($f17, F17_NUM, a0)
	SAVE_U_PCB_FPREG($f18, F18_NUM, a0)
	SAVE_U_PCB_FPREG($f19, F19_NUM, a0)
	SAVE_U_PCB_FPREG($f20, F20_NUM, a0)
	SAVE_U_PCB_FPREG($f21, F21_NUM, a0)
	SAVE_U_PCB_FPREG($f22, F22_NUM, a0)
	SAVE_U_PCB_FPREG($f23, F23_NUM, a0)
	SAVE_U_PCB_FPREG($f24, F24_NUM, a0)
	SAVE_U_PCB_FPREG($f25, F25_NUM, a0)
	SAVE_U_PCB_FPREG($f26, F26_NUM, a0)
	SAVE_U_PCB_FPREG($f27, F27_NUM, a0)
	SAVE_U_PCB_FPREG($f28, F28_NUM, a0)
	SAVE_U_PCB_FPREG($f29, F29_NUM, a0)
	SAVE_U_PCB_FPREG($f30, F30_NUM, a0)
	SAVE_U_PCB_FPREG($f31, F31_NUM, a0)

1:
/*
 *  Restore the floating point registers.
 */
	RESTORE_U_PCB_FPSR(t0, FSR_NUM, a1)	# get status register
	RESTORE_U_PCB_FPREG($f0, F0_NUM, a1)
	RESTORE_U_PCB_FPREG($f1, F1_NUM, a1)
	RESTORE_U_PCB_FPREG($f2, F2_NUM, a1)
	RESTORE_U_PCB_FPREG($f3, F3_NUM, a1)
	RESTORE_U_PCB_FPREG($f4, F4_NUM, a1)
	RESTORE_U_PCB_FPREG($f5, F5_NUM, a1)
	RESTORE_U_PCB_FPREG($f6, F6_NUM, a1)
	RESTORE_U_PCB_FPREG($f7, F7_NUM, a1)
	RESTORE_U_PCB_FPREG($f8, F8_NUM, a1)
	RESTORE_U_PCB_FPREG($f9, F9_NUM, a1)
	RESTORE_U_PCB_FPREG($f10, F10_NUM, a1)
	RESTORE_U_PCB_FPREG($f11, F11_NUM, a1)
	RESTORE_U_PCB_FPREG($f12, F12_NUM, a1)
	RESTORE_U_PCB_FPREG($f13, F13_NUM, a1)
	RESTORE_U_PCB_FPREG($f14, F14_NUM, a1)
	RESTORE_U_PCB_FPREG($f15, F15_NUM, a1)
	RESTORE_U_PCB_FPREG($f16, F16_NUM, a1)
	RESTORE_U_PCB_FPREG($f17, F17_NUM, a1)
	RESTORE_U_PCB_FPREG($f18, F18_NUM, a1)
	RESTORE_U_PCB_FPREG($f19, F19_NUM, a1)
	RESTORE_U_PCB_FPREG($f20, F20_NUM, a1)
	RESTORE_U_PCB_FPREG($f21, F21_NUM, a1)
	RESTORE_U_PCB_FPREG($f22, F22_NUM, a1)
	RESTORE_U_PCB_FPREG($f23, F23_NUM, a1)
	RESTORE_U_PCB_FPREG($f24, F24_NUM, a1)
	RESTORE_U_PCB_FPREG($f25, F25_NUM, a1)
	RESTORE_U_PCB_FPREG($f26, F26_NUM, a1)
	RESTORE_U_PCB_FPREG($f27, F27_NUM, a1)
	RESTORE_U_PCB_FPREG($f28, F28_NUM, a1)
	RESTORE_U_PCB_FPREG($f29, F29_NUM, a1)
	RESTORE_U_PCB_FPREG($f30, F30_NUM, a1)
	RESTORE_U_PCB_FPREG($f31, F31_NUM, a1)

	and	t0, t0, ~MIPS_FPU_EXCEPTION_BITS
	ctc1	t0, MIPS_FPU_CSR
	nop

	mtc0	t1, MIPS_COP_0_STATUS		# Restore the status register.
	ITLBNOPFIX
	j	ra
	nop
	.set pop
END(MipsSwitchFPState)

/*----------------------------------------------------------------------------
 *
 * MipsFPID --
 *
 *	Read and return the floating point implementation register.
 *
 *	uint32_t
 *	MipsFPID(void)
 *
 * Results:
 *	Floating point implementation register.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */
LEAF(MipsFPID)
	.set push
	.set hardfloat
	mfc0	t1, MIPS_COP_0_STATUS		# Save the status register.
	HAZARD_DELAY
#if defined(__mips_n32) || defined(__mips_n64)
	or	t0, t1, MIPS_SR_COP_1_BIT | MIPS_SR_FR
#else
	or	t0, t1, MIPS_SR_COP_1_BIT
#endif
	mtc0	t0, MIPS_COP_0_STATUS 		# Enable the coprocessor
	HAZARD_DELAY
	ITLBNOPFIX
	cfc1	v0, MIPS_FPU_ID
	mtc0	t1, MIPS_COP_0_STATUS		# Restore the status register.
	ITLBNOPFIX
	j	ra
	nop
	.set pop
END(MipsFPID)

/*----------------------------------------------------------------------------
 *
 * MipsSaveCurFPState --
 *
 *	Save the current floating point coprocessor state.
 *
 *	MipsSaveCurFPState(td)
 *		struct thread *td;
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	machFPCurProcPtr is cleared.
 *
 *----------------------------------------------------------------------------
 */
LEAF(MipsSaveCurFPState)
	.set push
	.set hardfloat
	PTR_L	a0, TD_PCB(a0)			# get pointer to pcb for thread
	mfc0	t1, MIPS_COP_0_STATUS		# Disable interrupts and
	HAZARD_DELAY
#if defined(__mips_n32) || defined(__mips_n64)
	or	t0, t1, MIPS_SR_COP_1_BIT | MIPS_SR_FR		#  enable the coprocessor
#else
	or	t0, t1, MIPS_SR_COP_1_BIT 			#  enable the coprocessor
#endif
	mtc0	t0, MIPS_COP_0_STATUS
	HAZARD_DELAY
	ITLBNOPFIX
	GET_CPU_PCPU(a1)
	PTR_S	zero, PC_FPCURTHREAD(a1)	# indicate state has been saved
/*
 * First read out the status register to make sure that all FP operations
 * have completed.
 */
	RESTORE_U_PCB_REG(t2, PS, a0)		# get CPU status register
	li	t3, ~MIPS_SR_COP_1_BIT
	and	t2, t2, t3			# clear COP_1 enable bit
	cfc1	t0, MIPS_FPU_CSR		# stall til FP done
	cfc1	t0, MIPS_FPU_CSR		# now get status
	SAVE_U_PCB_REG(t2, PS, a0)		# save new status register
	SAVE_U_PCB_FPSR(t0, FSR_NUM, a0)	# save FP status
/*
 * Save the floating point registers.
 */
	SAVE_U_PCB_FPREG($f0, F0_NUM, a0)
	SAVE_U_PCB_FPREG($f1, F1_NUM, a0)
	SAVE_U_PCB_FPREG($f2, F2_NUM, a0)
	SAVE_U_PCB_FPREG($f3, F3_NUM, a0)
	SAVE_U_PCB_FPREG($f4, F4_NUM, a0)
	SAVE_U_PCB_FPREG($f5, F5_NUM, a0)
	SAVE_U_PCB_FPREG($f6, F6_NUM, a0)
	SAVE_U_PCB_FPREG($f7, F7_NUM, a0)
	SAVE_U_PCB_FPREG($f8, F8_NUM, a0)
	SAVE_U_PCB_FPREG($f9, F9_NUM, a0)
	SAVE_U_PCB_FPREG($f10, F10_NUM, a0)
	SAVE_U_PCB_FPREG($f11, F11_NUM, a0)
	SAVE_U_PCB_FPREG($f12, F12_NUM, a0)
	SAVE_U_PCB_FPREG($f13, F13_NUM, a0)
	SAVE_U_PCB_FPREG($f14, F14_NUM, a0)
	SAVE_U_PCB_FPREG($f15, F15_NUM, a0)
	SAVE_U_PCB_FPREG($f16, F16_NUM, a0)
	SAVE_U_PCB_FPREG($f17, F17_NUM, a0)
	SAVE_U_PCB_FPREG($f18, F18_NUM, a0)
	SAVE_U_PCB_FPREG($f19, F19_NUM, a0)
	SAVE_U_PCB_FPREG($f20, F20_NUM, a0)
	SAVE_U_PCB_FPREG($f21, F21_NUM, a0)
	SAVE_U_PCB_FPREG($f22, F22_NUM, a0)
	SAVE_U_PCB_FPREG($f23, F23_NUM, a0)
	SAVE_U_PCB_FPREG($f24, F24_NUM, a0)
	SAVE_U_PCB_FPREG($f25, F25_NUM, a0)
	SAVE_U_PCB_FPREG($f26, F26_NUM, a0)
	SAVE_U_PCB_FPREG($f27, F27_NUM, a0)
	SAVE_U_PCB_FPREG($f28, F28_NUM, a0)
	SAVE_U_PCB_FPREG($f29, F29_NUM, a0)
	SAVE_U_PCB_FPREG($f30, F30_NUM, a0)
	SAVE_U_PCB_FPREG($f31, F31_NUM, a0)

	mtc0	t1, MIPS_COP_0_STATUS		# Restore the status register.
	ITLBNOPFIX
	j	ra
	nop
	.set pop
END(MipsSaveCurFPState)

/*
 * This code is copied the user's stack for returning from signal handlers
 * (see sendsig() and sigreturn()). We have to compute the address
 * of the sigcontext struct for the sigreturn call.
 */
	.globl	_C_LABEL(sigcode)
_C_LABEL(sigcode):
	PTR_ADDU	a0, sp, SIGF_UC		# address of ucontext
	li		v0, SYS_sigreturn
# sigreturn (ucp)
	syscall
	break	0				# just in case sigreturn fails
	.globl	_C_LABEL(esigcode)
_C_LABEL(esigcode):

	.data
	.globl	szsigcode
szsigcode:
	.long	esigcode-sigcode
	.text

#if (defined(__mips_n32) || defined(__mips_n64)) && defined(COMPAT_FREEBSD32)
	.globl	_C_LABEL(sigcode32)
_C_LABEL(sigcode32):
	addu		a0, sp, SIGF32_UC	# address of ucontext
	li		v0, SYS_sigreturn
# sigreturn (ucp)
	syscall
	break	0				# just in case sigreturn fails
	.globl	_C_LABEL(esigcode32)
_C_LABEL(esigcode32):

	.data
	.globl	szsigcode32
szsigcode32:
	.long	esigcode32-sigcode32
	.text
#endif
