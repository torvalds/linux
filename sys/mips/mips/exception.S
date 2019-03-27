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
 *	from: @(#)locore.s	8.5 (Berkeley) 1/4/94
 *	JNPR: exception.S,v 1.5 2007/01/08 04:58:37 katta
 * $FreeBSD$
 */

/*
 *	Contains code that is the first executed at boot time plus
 *	assembly language support routines.
 */

#include "opt_ddb.h"

#include <machine/asm.h>
#include <machine/cpu.h>
#include <machine/regnum.h>
#include <machine/cpuregs.h>
#include <machine/pte.h>
#include <machine/pcb.h>

#include "assym.inc"

	.set	noreorder		# Noreorder is default style!

#ifdef KDTRACE_HOOKS
	.data
	.globl	dtrace_invop_calltrap_addr
	.align	4
	.type	dtrace_invop_calltrap_addr, @object
        .size	dtrace_invop_calltrap_addr, 8
dtrace_invop_calltrap_addr:
	.word	0
	.word	0

	.text
#endif

/*
 * Reasonable limit
 */
#define	INTRCNT_COUNT	256


/*
 *----------------------------------------------------------------------------
 *
 * MipsTLBMiss --
 *
 *	Vector code for the TLB-miss exception vector 0x80000000.
 *
 * This code is copied to the TLB exception vector address to
 * which the CPU jumps in response to an exception or a TLB miss.
 * NOTE: This code must be position independent!!!
 *
 *
 */
VECTOR(MipsTLBMiss, unknown)
	.set push
	.set noat
	j	MipsDoTLBMiss
	MFC0	k0, MIPS_COP_0_BAD_VADDR	# get the fault address
	.set pop
VECTOR_END(MipsTLBMiss)

/*
 *----------------------------------------------------------------------------
 *
 * MipsDoTLBMiss --
 *
 * This is the real TLB Miss Handler code.
 * 'segbase' points to the base of the segment table for user processes.
 *
 * Don't check for invalid pte's here. We load them as well and
 * let the processor trap to load the correct value after service.
 *----------------------------------------------------------------------------
 */
 	.set push
	.set noat
MipsDoTLBMiss:
	bltz		k0, 1f				#02: k0<0 -> 1f (kernel fault)
	PTR_SRL		k0, k0, SEGSHIFT - PTRSHIFT	#03: k0=seg offset (almost)

	GET_CPU_PCPU(k1)
	PTR_L		k1, PC_SEGBASE(k1)
	beqz		k1, 2f				#05: make sure segbase is not null
	andi		k0, k0, PDEPTRMASK		#06: k0=seg offset
	PTR_ADDU	k1, k0, k1			#07: k1=seg entry address

	PTR_L		k1, 0(k1)			#08: k1=seg entry
	MFC0		k0, MIPS_COP_0_BAD_VADDR	#09: k0=bad address (again)
	beq		k1, zero, 2f			#0a: ==0 -- no page table
#ifdef __mips_n64
	PTR_SRL		k0, PDRSHIFT - PTRSHIFT		# k0=VPN
	andi		k0, k0, PDEPTRMASK		# k0=pde offset
	PTR_ADDU	k1, k0, k1			# k1=pde entry address
	PTR_L		k1, 0(k1)			# k1=pde entry
	MFC0		k0, MIPS_COP_0_BAD_VADDR	# k0=bad address (again)
	beq		k1, zero, 2f			# ==0 -- no page table
#endif
	PTR_SRL		k0, PAGE_SHIFT - PTESHIFT	#0b: k0=VPN (aka va>>10)
	andi		k0, k0, PTE2MASK		#0c: k0=page tab offset
	PTR_ADDU	k1, k1, k0			#0d: k1=pte address
	PTE_L		k0, 0(k1)			#0e: k0=lo0 pte
	PTE_L		k1, PTESIZE(k1)			#0f: k1=lo0 pte
	CLEAR_PTE_SWBITS(k0)
	PTE_MTC0	k0, MIPS_COP_0_TLB_LO0		#12: lo0 is loaded
	COP0_SYNC
	CLEAR_PTE_SWBITS(k1)
	PTE_MTC0	k1, MIPS_COP_0_TLB_LO1		#15: lo1 is loaded
	COP0_SYNC
	tlbwr						#1a: write to tlb
	HAZARD_DELAY
	eret						#1f: retUrn from exception
1:	j		MipsTLBMissException		#20: kernel exception
	nop						#21: branch delay slot
2:	j		SlowFault			#22: no page table present
	nop						#23: branch delay slot
	.set pop

/*
 * This code is copied to the general exception vector address to
 * handle all execptions except RESET and TLBMiss.
 * NOTE: This code must be position independent!!!
 */
VECTOR(MipsException, unknown)
/*
 * Find out what mode we came from and jump to the proper handler.
 *
 * Note: at turned off here because we cannot trash the at register
 * in this exception code. Only k0 and k1 may be modified before
 * we save registers. This is true of all functions called through
 * the pointer magic: Mips{User,Kern}Intr, Mips{User,Kern}GenException
 * and MipsTLBInvalidException
 */
	.set	noat
	mfc0	k0, MIPS_COP_0_STATUS		# Get the status register
	mfc0	k1, MIPS_COP_0_CAUSE		# Get the cause register value.
	and	k0, k0, MIPS_SR_KSU_USER	# test for user mode
						# sneaky but the bits are
						# with us........
	sll	k0, k0, 3			# shift user bit for cause index
	and	k1, k1, MIPS_CR_EXC_CODE	# Mask out the cause bits.
	or	k1, k1, k0			# change index to user table
#if defined(__mips_n64)
	PTR_SLL	k1, k1, 1			# shift to get 8-byte offset
#endif
1:
	PTR_LA	k0, _C_LABEL(machExceptionTable)  # get base of the jump table
	PTR_ADDU k0, k0, k1			# Get the address of the
						#  function entry.  Note that
						#  the cause is already
						#  shifted left by 2 bits so
						#  we dont have to shift.
	PTR_L	k0, 0(k0)			# Get the function address
	nop
	j	k0				# Jump to the function.
	nop
	.set	at
VECTOR_END(MipsException)

/*
 * We couldn't find a TLB entry.
 * Find out what mode we came from and call the appropriate handler.
 */
SlowFault:
	.set	noat
	mfc0	k0, MIPS_COP_0_STATUS
	nop
	and	k0, k0, MIPS_SR_KSU_USER
	bne	k0, zero, _C_LABEL(MipsUserGenException)
	nop
	.set	at
/*
 * Fall though ...
 */

/*----------------------------------------------------------------------------
 *
 * MipsKernGenException --
 *
 *	Handle an exception from kernel mode.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */

#define	SAVE_REG(reg, offs, base) \
	REG_S	reg, CALLFRAME_SIZ + (SZREG * offs) (base)

#if defined(CPU_CNMIPS)
#define CLEAR_STATUS \
	mfc0    a0, MIPS_COP_0_STATUS   ;\
	li      a2, (MIPS_SR_KX | MIPS_SR_SX | MIPS_SR_UX) ; \
	or      a0, a0, a2	        ; \
	li      a2, ~(MIPS_SR_INT_IE | MIPS_SR_EXL | MIPS_SR_KSU_USER)   ; \
	and     a0, a0, a2              ; \
        mtc0    a0, MIPS_COP_0_STATUS   ; \
	ITLBNOPFIX
#elif defined(CPU_RMI) || defined(CPU_NLM)
#define CLEAR_STATUS \
	mfc0    a0, MIPS_COP_0_STATUS   ;\
	li      a2, (MIPS_SR_KX | MIPS_SR_UX | MIPS_SR_COP_2_BIT) ; \
	or      a0, a0, a2	        ; \
	li      a2, ~(MIPS_SR_INT_IE | MIPS_SR_EXL | MIPS_SR_KSU_USER)   ; \
	and     a0, a0, a2              ; \
        mtc0    a0, MIPS_COP_0_STATUS   ; \
	ITLBNOPFIX
#else
#define CLEAR_STATUS \
	mfc0    a0, MIPS_COP_0_STATUS   ;\
	li      a2, ~(MIPS_SR_INT_IE | MIPS_SR_EXL | MIPS_SR_KSU_USER)   ; \
	and     a0, a0, a2              ; \
	mtc0	a0, MIPS_COP_0_STATUS   ; \
	ITLBNOPFIX
#endif

/*
 * Save CPU and CP0 register state.
 *
 * This is straightforward except for saving the exception program
 * counter. The ddb backtrace code looks for the first instruction
 * matching the form "sw ra, (off)sp" to figure out the address of the
 * calling function. So we must make sure that we save the exception
 * PC by staging it through 'ra' as opposed to any other register.
 */
#define	SAVE_CPU \
	SAVE_REG(AT, AST, sp)		;\
	.set	at		        ; \
	SAVE_REG(v0, V0, sp)		;\
	SAVE_REG(v1, V1, sp)		;\
	SAVE_REG(a0, A0, sp)		;\
	SAVE_REG(a1, A1, sp)		;\
	SAVE_REG(a2, A2, sp)		;\
	SAVE_REG(a3, A3, sp)		;\
	SAVE_REG(t0, T0, sp)		;\
	SAVE_REG(t1, T1, sp)		;\
	SAVE_REG(t2, T2, sp)		;\
	SAVE_REG(t3, T3, sp)		;\
	SAVE_REG(ta0, TA0, sp)		;\
	SAVE_REG(ta1, TA1, sp)		;\
	SAVE_REG(ta2, TA2, sp)		;\
	SAVE_REG(ta3, TA3, sp)		;\
	SAVE_REG(t8, T8, sp)		;\
	SAVE_REG(t9, T9, sp)		;\
	SAVE_REG(gp, GP, sp)		;\
	SAVE_REG(s0, S0, sp)		;\
	SAVE_REG(s1, S1, sp)		;\
	SAVE_REG(s2, S2, sp)		;\
	SAVE_REG(s3, S3, sp)		;\
	SAVE_REG(s4, S4, sp)		;\
	SAVE_REG(s5, S5, sp)		;\
	SAVE_REG(s6, S6, sp)		;\
	SAVE_REG(s7, S7, sp)		;\
	SAVE_REG(s8, S8, sp)	        ;\
	mflo	v0			;\
	mfhi	v1			;\
	mfc0	a0, MIPS_COP_0_STATUS	;\
	mfc0	a1, MIPS_COP_0_CAUSE	;\
	MFC0	a2, MIPS_COP_0_BAD_VADDR;\
	MFC0	a3, MIPS_COP_0_EXC_PC	;\
	SAVE_REG(v0, MULLO, sp)		;\
	SAVE_REG(v1, MULHI, sp)		;\
	SAVE_REG(a0, SR, sp)		;\
	SAVE_REG(a1, CAUSE, sp)		;\
	SAVE_REG(a2, BADVADDR, sp)	;\
	move	t0, ra			;\
	move	ra, a3			;\
	SAVE_REG(ra, PC, sp)		;\
	move	ra, t0			;\
	SAVE_REG(ra, RA, sp)		;\
	PTR_ADDU v0, sp, KERN_EXC_FRAME_SIZE ;\
	SAVE_REG(v0, SP, sp)		;\
	CLEAR_STATUS			;\
	PTR_ADDU a0, sp, CALLFRAME_SIZ	;\
	ITLBNOPFIX

#define	RESTORE_REG(reg, offs, base) \
	REG_L	reg, CALLFRAME_SIZ + (SZREG * offs) (base)

#define	RESTORE_CPU \
	CLEAR_STATUS			;\
	RESTORE_REG(k0, SR, sp)		;\
	RESTORE_REG(t0, MULLO, sp)	;\
	RESTORE_REG(t1, MULHI, sp)	;\
	mtlo	t0			;\
	mthi	t1			;\
	MTC0	v0, MIPS_COP_0_EXC_PC	;\
	.set noat		        ;\
	RESTORE_REG(AT, AST, sp)	;\
	RESTORE_REG(v0, V0, sp)		;\
	RESTORE_REG(v1, V1, sp)		;\
	RESTORE_REG(a0, A0, sp)		;\
	RESTORE_REG(a1, A1, sp)		;\
	RESTORE_REG(a2, A2, sp)		;\
	RESTORE_REG(a3, A3, sp)		;\
	RESTORE_REG(t0, T0, sp)		;\
	RESTORE_REG(t1, T1, sp)		;\
	RESTORE_REG(t2, T2, sp)		;\
	RESTORE_REG(t3, T3, sp)		;\
	RESTORE_REG(ta0, TA0, sp)	;\
	RESTORE_REG(ta1, TA1, sp)	;\
	RESTORE_REG(ta2, TA2, sp)	;\
	RESTORE_REG(ta3, TA3, sp)	;\
	RESTORE_REG(t8, T8, sp)		;\
	RESTORE_REG(t9, T9, sp)		;\
	RESTORE_REG(s0, S0, sp)		;\
	RESTORE_REG(s1, S1, sp)		;\
	RESTORE_REG(s2, S2, sp)		;\
	RESTORE_REG(s3, S3, sp)		;\
	RESTORE_REG(s4, S4, sp)		;\
	RESTORE_REG(s5, S5, sp)		;\
	RESTORE_REG(s6, S6, sp)		;\
	RESTORE_REG(s7, S7, sp)		;\
	RESTORE_REG(s8, S8, sp)	        ;\
	RESTORE_REG(gp, GP, sp)		;\
	RESTORE_REG(ra, RA, sp)		;\
	PTR_ADDU sp, sp, KERN_EXC_FRAME_SIZE;\
	mtc0	k0, MIPS_COP_0_STATUS


/*
 * The kernel exception stack contains 18 saved general registers,
 * the status register and the multiply lo and high registers.
 * In addition, we set this up for linkage conventions.
 */
#define	KERN_REG_SIZE		(NUMSAVEREGS * SZREG)
#define	KERN_EXC_FRAME_SIZE	(CALLFRAME_SIZ + KERN_REG_SIZE + 16)

NESTED_NOPROFILE(MipsKernGenException, KERN_EXC_FRAME_SIZE, ra)
	.set	noat
	PTR_SUBU	sp, sp, KERN_EXC_FRAME_SIZE
	.mask	0x80000000, (CALLFRAME_RA - KERN_EXC_FRAME_SIZE)
/*
 *  Save CPU state, building 'frame'.
 */
	SAVE_CPU
/*
 *  Call the exception handler. a0 points at the saved frame.
 */
	PTR_LA	gp, _C_LABEL(_gp)
	PTR_LA	k0, _C_LABEL(trap)
	jalr	k0
	REG_S	a3, CALLFRAME_RA + KERN_REG_SIZE(sp)		# for debugging

	/*
	 * Update interrupt and CPU mask in saved status register
	 * Some of interrupts could be disabled by
	 * intr filters if interrupts are enabled later
	 * in trap handler
	 */
	mfc0	a0, MIPS_COP_0_STATUS
	and	a0, a0, (MIPS_SR_INT_MASK|MIPS_SR_COP_USABILITY)
	RESTORE_REG(a1, SR, sp)
	and	a1, a1, ~(MIPS_SR_INT_MASK|MIPS_SR_COP_USABILITY)
	or	a1, a1, a0
	SAVE_REG(a1, SR, sp)
	RESTORE_CPU			# v0 contains the return address.
	sync
	eret
	.set	at
END(MipsKernGenException)


/*----------------------------------------------------------------------------
 *
 * MipsUserGenException --
 *
 *	Handle an exception from user mode.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */
NESTED_NOPROFILE(MipsUserGenException, CALLFRAME_SIZ, ra)
	.set	noat
	.mask	0x80000000, (CALLFRAME_RA - CALLFRAME_SIZ)
/*
 * Save all of the registers except for the kernel temporaries in u.u_pcb.
 */
	GET_CPU_PCPU(k1)
	PTR_L	k1, PC_CURPCB(k1)
	SAVE_U_PCB_REG(AT, AST, k1)
	.set	at
	SAVE_U_PCB_REG(v0, V0, k1)
	SAVE_U_PCB_REG(v1, V1, k1)
	SAVE_U_PCB_REG(a0, A0, k1)
	mflo	v0
	SAVE_U_PCB_REG(a1, A1, k1)
	SAVE_U_PCB_REG(a2, A2, k1)
	SAVE_U_PCB_REG(a3, A3, k1)
	SAVE_U_PCB_REG(t0, T0, k1)
	mfhi	v1
	SAVE_U_PCB_REG(t1, T1, k1)
	SAVE_U_PCB_REG(t2, T2, k1)
	SAVE_U_PCB_REG(t3, T3, k1)
	SAVE_U_PCB_REG(ta0, TA0, k1)
	mfc0	a0, MIPS_COP_0_STATUS		# First arg is the status reg.
	SAVE_U_PCB_REG(ta1, TA1, k1)
	SAVE_U_PCB_REG(ta2, TA2, k1)
	SAVE_U_PCB_REG(ta3, TA3, k1)
	SAVE_U_PCB_REG(s0, S0, k1)
	mfc0	a1, MIPS_COP_0_CAUSE		# Second arg is the cause reg.
	SAVE_U_PCB_REG(s1, S1, k1)
	SAVE_U_PCB_REG(s2, S2, k1)
	SAVE_U_PCB_REG(s3, S3, k1)
	SAVE_U_PCB_REG(s4, S4, k1)
	MFC0	a2, MIPS_COP_0_BAD_VADDR	# Third arg is the fault addr
	SAVE_U_PCB_REG(s5, S5, k1)
	SAVE_U_PCB_REG(s6, S6, k1)
	SAVE_U_PCB_REG(s7, S7, k1)
	SAVE_U_PCB_REG(t8, T8, k1)
	MFC0	a3, MIPS_COP_0_EXC_PC		# Fourth arg is the pc.
	SAVE_U_PCB_REG(t9, T9, k1)
	SAVE_U_PCB_REG(gp, GP, k1)
	SAVE_U_PCB_REG(sp, SP, k1)
	SAVE_U_PCB_REG(s8, S8, k1)
	PTR_SUBU	sp, k1, CALLFRAME_SIZ	 # switch to kernel SP
	SAVE_U_PCB_REG(ra, RA, k1)
	SAVE_U_PCB_REG(v0, MULLO, k1)
	SAVE_U_PCB_REG(v1, MULHI, k1)
	SAVE_U_PCB_REG(a0, SR, k1)
	SAVE_U_PCB_REG(a1, CAUSE, k1)
	SAVE_U_PCB_REG(a2, BADVADDR, k1)
	SAVE_U_PCB_REG(a3, PC, k1)
	REG_S	a3, CALLFRAME_RA(sp)	# for debugging
	PTR_LA	gp, _C_LABEL(_gp)	# switch to kernel GP
# Turn off fpu and enter kernel mode
	and	t0, a0, ~(MIPS_SR_COP_1_BIT | MIPS_SR_EXL | MIPS_SR_KSU_MASK | MIPS_SR_INT_IE)
#if defined(CPU_CNMIPS)
	and	t0, t0, ~(MIPS_SR_COP_2_BIT)
	or      t0, t0, (MIPS_SR_KX | MIPS_SR_SX | MIPS_SR_UX | MIPS_SR_PX)
#elif defined(CPU_RMI)	|| defined(CPU_NLM)
	or      t0, t0, (MIPS_SR_KX | MIPS_SR_UX | MIPS_SR_COP_2_BIT)
#endif	
	mtc0	t0, MIPS_COP_0_STATUS
	PTR_ADDU a0, k1, U_PCB_REGS
	ITLBNOPFIX

/*
 * Call the exception handler.
 */
	PTR_LA	k0, _C_LABEL(trap)
	jalr	k0
	nop

/*
 * Restore user registers and return.
 * First disable interrupts and set exeption level.
 */
	DO_AST

	CLEAR_STATUS

/*
 * The use of k1 for storing the PCB pointer must be done only
 * after interrupts are disabled.  Otherwise it will get overwritten
 * by the interrupt code.
 */
	GET_CPU_PCPU(k1)
	PTR_L	k1, PC_CURPCB(k1)

	/*
	 * Update interrupt mask in saved status register
	 * Some of interrupts could be enabled by ithread
	 * scheduled by ast()
	 */
	mfc0	a0, MIPS_COP_0_STATUS
	and	a0, a0, MIPS_SR_INT_MASK
	RESTORE_U_PCB_REG(a1, SR, k1)
	and	a1, a1, ~MIPS_SR_INT_MASK
	or	a1, a1, a0
	SAVE_U_PCB_REG(a1, SR, k1)

	RESTORE_U_PCB_REG(t0, MULLO, k1)
	RESTORE_U_PCB_REG(t1, MULHI, k1)
	mtlo	t0
	mthi	t1
	RESTORE_U_PCB_REG(a0, PC, k1)
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
	RESTORE_U_PCB_REG(gp, GP, k1)
	RESTORE_U_PCB_REG(sp, SP, k1)
	RESTORE_U_PCB_REG(k0, SR, k1)
	RESTORE_U_PCB_REG(s8, S8, k1)
	RESTORE_U_PCB_REG(ra, RA, k1)
	.set noat
	RESTORE_U_PCB_REG(AT, AST, k1)

	mtc0	k0, MIPS_COP_0_STATUS	# still exception level
	ITLBNOPFIX
	sync
	eret
	.set	at
END(MipsUserGenException)

	.set	push
	.set	noat
NESTED(mips_wait, CALLFRAME_SIZ, ra)
	PTR_SUBU        sp, sp, CALLFRAME_SIZ
	.mask   0x80000000, (CALLFRAME_RA - CALLFRAME_SIZ)
	REG_S   ra, CALLFRAME_RA(sp)		# save RA
	mfc0	t0, MIPS_COP_0_STATUS
	xori	t1, t0, MIPS_SR_INT_IE
	mtc0	t1, MIPS_COP_0_STATUS
	COP0_SYNC
	jal	sched_runnable
	nop
	REG_L   ra, CALLFRAME_RA(sp)
	mfc0	t0, MIPS_COP_0_STATUS
	ori	t1, t0, MIPS_SR_INT_IE
	.align 4
GLOBAL(MipsWaitStart)			# this is 16 byte aligned
	mtc0	t1, MIPS_COP_0_STATUS
	bnez	v0, MipsWaitEnd
	nop
#if defined(CPU_XBURST) && defined(SMP)
	nop
#else
	wait
#endif
GLOBAL(MipsWaitEnd)			# MipsWaitStart + 16
	jr	ra
	PTR_ADDU        sp, sp, CALLFRAME_SIZ
END(mips_wait)
	.set	pop

/*----------------------------------------------------------------------------
 *
 * MipsKernIntr --
 *
 *	Handle an interrupt from kernel mode.
 *	Interrupts use the standard kernel stack.
 *	switch_exit sets up a kernel stack after exit so interrupts won't fail.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */

NESTED_NOPROFILE(MipsKernIntr, KERN_EXC_FRAME_SIZE, ra)
	.set	noat
	PTR_SUBU	sp, sp, KERN_EXC_FRAME_SIZE
	.mask	0x80000000, (CALLFRAME_RA - KERN_EXC_FRAME_SIZE)

/*
 * Check for getting interrupts just before wait
 */
	MFC0	k0, MIPS_COP_0_EXC_PC
	ori	k0, 0xf
	xori	k0, 0xf			# 16 byte align
	PTR_LA	k1, MipsWaitStart
	bne	k0, k1, 1f
	nop
	PTR_ADDU k1, 16			# skip over wait
	MTC0	k1, MIPS_COP_0_EXC_PC
1:
/*
 *  Save CPU state, building 'frame'.
 */
	SAVE_CPU
/*
 *  Call the interrupt handler. a0 points at the saved frame.
 */
	PTR_LA	gp, _C_LABEL(_gp)
#ifdef INTRNG
	PTR_LA	k0, _C_LABEL(intr_irq_handler)
#else
	PTR_LA	k0, _C_LABEL(cpu_intr)
#endif
	jalr	k0
	REG_S	a3, CALLFRAME_RA + KERN_REG_SIZE(sp)		# for debugging

	/*
	 * Update interrupt and CPU mask in saved status register
	 * Some of interrupts could be disabled by
	 * intr filters if interrupts are enabled later
	 * in trap handler
	 */
	mfc0	a0, MIPS_COP_0_STATUS
	and	a0, a0, (MIPS_SR_INT_MASK|MIPS_SR_COP_USABILITY)
	RESTORE_REG(a1, SR, sp)
	and	a1, a1, ~(MIPS_SR_INT_MASK|MIPS_SR_COP_USABILITY)
	or	a1, a1, a0
	SAVE_REG(a1, SR, sp)
	REG_L	v0, CALLFRAME_RA + KERN_REG_SIZE(sp)
	RESTORE_CPU			# v0 contains the return address.
	sync
	eret
	.set	at
END(MipsKernIntr)

/*----------------------------------------------------------------------------
 *
 * MipsUserIntr --
 *
 *	Handle an interrupt from user mode.
 *	Note: we save minimal state in the u.u_pcb struct and use the standard
 *	kernel stack since there has to be a u page if we came from user mode.
 *	If there is a pending software interrupt, then save the remaining state
 *	and call softintr(). This is all because if we call switch() inside
 *	interrupt(), not all the user registers have been saved in u.u_pcb.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */
NESTED_NOPROFILE(MipsUserIntr, CALLFRAME_SIZ, ra)
	.set	noat
	.mask	0x80000000, (CALLFRAME_RA - CALLFRAME_SIZ)
/*
 * Save the relevant user registers into the u.u_pcb struct.
 * We don't need to save s0 - s8 because the compiler does it for us.
 */
	GET_CPU_PCPU(k1)
	PTR_L	k1, PC_CURPCB(k1)
	SAVE_U_PCB_REG(AT, AST, k1)
	.set	at
	SAVE_U_PCB_REG(v0, V0, k1)
	SAVE_U_PCB_REG(v1, V1, k1)
	SAVE_U_PCB_REG(a0, A0, k1)
	SAVE_U_PCB_REG(a1, A1, k1)
	SAVE_U_PCB_REG(a2, A2, k1)
	SAVE_U_PCB_REG(a3, A3, k1)
	SAVE_U_PCB_REG(t0, T0, k1)
	SAVE_U_PCB_REG(t1, T1, k1)
	SAVE_U_PCB_REG(t2, T2, k1)
	SAVE_U_PCB_REG(t3, T3, k1)
	SAVE_U_PCB_REG(ta0, TA0, k1)
	SAVE_U_PCB_REG(ta1, TA1, k1)
	SAVE_U_PCB_REG(ta2, TA2, k1)
	SAVE_U_PCB_REG(ta3, TA3, k1)
	SAVE_U_PCB_REG(t8, T8, k1)
	SAVE_U_PCB_REG(t9, T9, k1)
	SAVE_U_PCB_REG(gp, GP, k1)
	SAVE_U_PCB_REG(sp, SP, k1)
	SAVE_U_PCB_REG(ra, RA, k1)
/*
 *  save remaining user state in u.u_pcb.
 */
	SAVE_U_PCB_REG(s0, S0, k1)
	SAVE_U_PCB_REG(s1, S1, k1)
	SAVE_U_PCB_REG(s2, S2, k1)
	SAVE_U_PCB_REG(s3, S3, k1)
	SAVE_U_PCB_REG(s4, S4, k1)
	SAVE_U_PCB_REG(s5, S5, k1)
	SAVE_U_PCB_REG(s6, S6, k1)
	SAVE_U_PCB_REG(s7, S7, k1)
	SAVE_U_PCB_REG(s8, S8, k1)

	mflo	v0			# get lo/hi late to avoid stall
	mfhi	v1
	mfc0	a0, MIPS_COP_0_STATUS
	mfc0	a1, MIPS_COP_0_CAUSE
	MFC0	a3, MIPS_COP_0_EXC_PC
	SAVE_U_PCB_REG(v0, MULLO, k1)
	SAVE_U_PCB_REG(v1, MULHI, k1)
	SAVE_U_PCB_REG(a0, SR, k1)
	SAVE_U_PCB_REG(a1, CAUSE, k1)
	SAVE_U_PCB_REG(a3, PC, k1)	# PC in a3, note used later!
	PTR_SUBU	sp, k1, CALLFRAME_SIZ  # switch to kernel SP
	PTR_LA	gp, _C_LABEL(_gp)	# switch to kernel GP

# Turn off fpu, disable interrupts, set kernel mode kernel mode, clear exception level.
	and	t0, a0, ~(MIPS_SR_COP_1_BIT | MIPS_SR_EXL | MIPS_SR_INT_IE | MIPS_SR_KSU_MASK)
#ifdef CPU_CNMIPS
	and	t0, t0, ~(MIPS_SR_COP_2_BIT)
	or      t0, t0, (MIPS_SR_KX | MIPS_SR_SX | MIPS_SR_UX | MIPS_SR_PX)
#elif defined(CPU_RMI)	|| defined(CPU_NLM)
	or      t0, t0, (MIPS_SR_KX | MIPS_SR_UX | MIPS_SR_COP_2_BIT)
#endif	
	mtc0	t0, MIPS_COP_0_STATUS
	ITLBNOPFIX
	PTR_ADDU a0, k1, U_PCB_REGS
/*
 * Call the interrupt handler.
 */
#ifdef INTRNG
	PTR_LA	k0, _C_LABEL(intr_irq_handler)
#else
	PTR_LA	k0, _C_LABEL(cpu_intr)
#endif
	jalr	k0
	REG_S	a3, CALLFRAME_RA(sp)	# for debugging

/*
 * Enable interrupts before doing ast().
 *
 * On SMP kernels the AST processing might trigger IPI to other processors.
 * If that processor is also doing AST processing with interrupts disabled
 * then we may deadlock.
 */
	mfc0	a0, MIPS_COP_0_STATUS
	or	a0, a0, MIPS_SR_INT_IE
	mtc0	a0, MIPS_COP_0_STATUS
	ITLBNOPFIX

/*
 * DO_AST enabled interrupts
 */
	DO_AST
	
/*
 * Restore user registers and return. 
 */
 	CLEAR_STATUS

	GET_CPU_PCPU(k1)
	PTR_L	k1, PC_CURPCB(k1)

	/*
	 * Update interrupt mask in saved status register
	 * Some of interrupts could be disabled by
	 * intr filters
	 */
	mfc0	a0, MIPS_COP_0_STATUS
	and	a0, a0, MIPS_SR_INT_MASK
	RESTORE_U_PCB_REG(a1, SR, k1)
	and	a1, a1, ~MIPS_SR_INT_MASK
	or	a1, a1, a0
	SAVE_U_PCB_REG(a1, SR, k1)

	RESTORE_U_PCB_REG(s0, S0, k1)
	RESTORE_U_PCB_REG(s1, S1, k1)
	RESTORE_U_PCB_REG(s2, S2, k1)
	RESTORE_U_PCB_REG(s3, S3, k1)
	RESTORE_U_PCB_REG(s4, S4, k1)
	RESTORE_U_PCB_REG(s5, S5, k1)
	RESTORE_U_PCB_REG(s6, S6, k1)
	RESTORE_U_PCB_REG(s7, S7, k1)
	RESTORE_U_PCB_REG(s8, S8, k1)
	RESTORE_U_PCB_REG(t0, MULLO, k1)
	RESTORE_U_PCB_REG(t1, MULHI, k1)
	RESTORE_U_PCB_REG(t2, PC, k1)
	mtlo	t0
	mthi	t1
	MTC0	t2, MIPS_COP_0_EXC_PC	# set return address
	RESTORE_U_PCB_REG(v0, V0, k1)
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
	RESTORE_U_PCB_REG(t8, T8, k1)
	RESTORE_U_PCB_REG(t9, T9, k1)
	RESTORE_U_PCB_REG(gp, GP, k1)
	RESTORE_U_PCB_REG(k0, SR, k1)
	RESTORE_U_PCB_REG(sp, SP, k1)
	RESTORE_U_PCB_REG(ra, RA, k1)
	.set	noat
	RESTORE_U_PCB_REG(AT, AST, k1)

	mtc0	k0, MIPS_COP_0_STATUS	# SR with EXL set. 
	ITLBNOPFIX
	sync
	eret
	.set	at
END(MipsUserIntr)

LEAF_NOPROFILE(MipsTLBInvalidException)
	.set push
	.set noat
	.set noreorder

	MFC0		k0, MIPS_COP_0_BAD_VADDR
	PTR_LI		k1, VM_MAXUSER_ADDRESS
	sltu		k1, k0, k1
	bnez		k1, 1f
	nop

	/* Kernel address.  */
	lui		k1, %hi(kernel_segmap)		# k1=hi of segbase
	b		2f
	PTR_L		k1, %lo(kernel_segmap)(k1)	# k1=segment tab base

1:	/* User address.  */
	GET_CPU_PCPU(k1)
	PTR_L		k1, PC_SEGBASE(k1)

2:	/* Validate page directory pointer.  */
	beqz		k1, 3f
	nop

	PTR_SRL		k0, SEGSHIFT - PTRSHIFT		# k0=seg offset (almost)
	beq		k1, zero, MipsKernGenException	# ==0 -- no seg tab
	andi		k0, k0, PDEPTRMASK		#06: k0=seg offset
	PTR_ADDU	k1, k0, k1			# k1=seg entry address
	PTR_L		k1, 0(k1)			# k1=seg entry

	/* Validate page table pointer.  */
	beqz		k1, 3f
	nop

#ifdef __mips_n64
	MFC0		k0, MIPS_COP_0_BAD_VADDR
	PTR_SRL		k0, PDRSHIFT - PTRSHIFT		# k0=pde offset (almost)
	beq		k1, zero, MipsKernGenException	# ==0 -- no pde tab
	andi		k0, k0, PDEPTRMASK		# k0=pde offset
	PTR_ADDU	k1, k0, k1			# k1=pde entry address
	PTR_L		k1, 0(k1)			# k1=pde entry

	/* Validate pde table pointer.  */
	beqz		k1, 3f
	nop
#endif
	MFC0		k0, MIPS_COP_0_BAD_VADDR	# k0=bad address (again)
	PTR_SRL		k0, PAGE_SHIFT - PTESHIFT	# k0=VPN
	andi		k0, k0, PTEMASK			# k0=page tab offset
	PTR_ADDU	k1, k1, k0			# k1=pte address
	PTE_L		k0, 0(k1)			# k0=this PTE

	/* Validate page table entry.  */
	andi		k0, PTE_V
	beqz		k0, 3f
	nop

	/* Check whether this is an even or odd entry.  */
	andi		k0, k1, PTESIZE
	bnez		k0, odd_page
	nop

	PTE_L		k0, 0(k1)
	PTE_L		k1, PTESIZE(k1)
	CLEAR_PTE_SWBITS(k0)
	PTE_MTC0	k0, MIPS_COP_0_TLB_LO0
	COP0_SYNC
	CLEAR_PTE_SWBITS(k1)
	PTE_MTC0	k1, MIPS_COP_0_TLB_LO1
	COP0_SYNC

	b		tlb_insert_entry
	nop

odd_page:
	PTE_L		k0, -PTESIZE(k1)
	PTE_L		k1, 0(k1)
	CLEAR_PTE_SWBITS(k0)
	PTE_MTC0	k0, MIPS_COP_0_TLB_LO0
	COP0_SYNC
	CLEAR_PTE_SWBITS(k1)
	PTE_MTC0	k1, MIPS_COP_0_TLB_LO1
	COP0_SYNC

tlb_insert_entry:
	tlbp
	HAZARD_DELAY
	mfc0		k0, MIPS_COP_0_TLB_INDEX
	bltz		k0, tlb_insert_random
	nop
	tlbwi
	eret
	ssnop

tlb_insert_random:
	tlbwr
	eret
	ssnop

3:
	/*
	 * Branch to the comprehensive exception processing.
	 */
	mfc0	k1, MIPS_COP_0_STATUS
	andi	k1, k1, MIPS_SR_KSU_USER
	bnez	k1, _C_LABEL(MipsUserGenException)
	nop

	/*
	 * Check for kernel stack overflow.
	 */
	GET_CPU_PCPU(k1)
	PTR_L	k0, PC_CURTHREAD(k1)
	PTR_L	k0, TD_KSTACK(k0)
	sltu	k0, k0, sp
	bnez	k0, _C_LABEL(MipsKernGenException)
	nop

	/*
	 * Kernel stack overflow.
	 *
	 * Move to a valid stack before we call panic. We use the boot stack
	 * for this purpose.
	 */
	GET_CPU_PCPU(k1)
	lw	k1, PC_CPUID(k1)
	sll	k1, k1, PAGE_SHIFT + 1

	PTR_LA	k0, _C_LABEL(pcpu_space)
	PTR_ADDU	k0, PAGE_SIZE * 2
	PTR_ADDU	k0, k0, k1

	/*
	 * Stash the original value of 'sp' so we can update trapframe later.
	 * We assume that SAVE_CPU does not trash 'k1'.
	 */
	move	k1, sp

	move	sp, k0
	PTR_SUBU	sp, sp, KERN_EXC_FRAME_SIZE

	move	k0, ra
	move	ra, zero
	REG_S	ra, CALLFRAME_RA(sp)	/* stop the ddb backtrace right here */
	REG_S	zero, CALLFRAME_SP(sp)
	move	ra, k0

	SAVE_CPU

	/*
	 * Now restore the value of 'sp' at the time of the tlb exception in
	 * the trapframe.
	 */
	SAVE_REG(k1, SP, sp)

	/*
	 * Squelch any more overflow checks by setting the stack base to 0.
	 */
	GET_CPU_PCPU(k1)
	PTR_L	k0, PC_CURTHREAD(k1)
	PTR_S	zero, TD_KSTACK(k0)

	move	a1, a0
	PANIC("kernel stack overflow - trapframe at %p")

	/*
	 * This nop is necessary so that the 'ra' remains within the bounds
	 * of this handler. Otherwise the ddb backtrace code will think that
	 * the panic() was called from MipsTLBMissException.
	 */
	.globl	MipsKStackOverflow
MipsKStackOverflow:
	nop

	.set pop
END(MipsTLBInvalidException)

/*----------------------------------------------------------------------------
 *
 * MipsTLBMissException --
 *
 *	Handle a TLB miss exception from kernel mode in kernel space.
 *	The BaddVAddr, Context, and EntryHi registers contain the failed
 *	virtual address.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */
LEAF_NOPROFILE(MipsTLBMissException)
	.set	noat
	MFC0		k0, MIPS_COP_0_BAD_VADDR	# k0=bad address
	PTR_LI		k1, VM_MAX_KERNEL_ADDRESS	# check fault address against
	sltu		k1, k1, k0			# upper bound of kernel_segmap
	bnez		k1, MipsKernGenException	# out of bound
	lui		k1, %hi(kernel_segmap)		# k1=hi of segbase
	PTR_SRL		k0, SEGSHIFT - PTRSHIFT		# k0=seg offset (almost)
	PTR_L		k1, %lo(kernel_segmap)(k1)	# k1=segment tab base
	beq		k1, zero, MipsKernGenException	# ==0 -- no seg tab
	andi		k0, k0, PDEPTRMASK		#06: k0=seg offset
	PTR_ADDU	k1, k0, k1			# k1=seg entry address
	PTR_L		k1, 0(k1)			# k1=seg entry
	MFC0		k0, MIPS_COP_0_BAD_VADDR	# k0=bad address (again)
	beq		k1, zero, MipsKernGenException	# ==0 -- no page table
#ifdef __mips_n64
	PTR_SRL		k0, PDRSHIFT - PTRSHIFT		# k0=VPN
	andi		k0, k0, PDEPTRMASK		# k0=pde offset
	PTR_ADDU	k1, k0, k1			# k1=pde entry address
	PTR_L		k1, 0(k1)			# k1=pde entry
	MFC0		k0, MIPS_COP_0_BAD_VADDR	# k0=bad address (again)
  	beq		k1, zero, MipsKernGenException	# ==0 -- no page table
#endif
	PTR_SRL		k0, PAGE_SHIFT - PTESHIFT	# k0=VPN
	andi		k0, k0, PTE2MASK		# k0=page tab offset
	PTR_ADDU	k1, k1, k0			# k1=pte address
	PTE_L		k0, 0(k1)			# k0=lo0 pte
	PTE_L		k1, PTESIZE(k1)			# k1=lo1 pte
	CLEAR_PTE_SWBITS(k0)
	PTE_MTC0	k0, MIPS_COP_0_TLB_LO0		# lo0 is loaded
	COP0_SYNC
	CLEAR_PTE_SWBITS(k1)
	PTE_MTC0	k1, MIPS_COP_0_TLB_LO1		# lo1 is loaded
	COP0_SYNC
	tlbwr					# write to tlb
	HAZARD_DELAY
	eret					# return from exception
	.set	at
END(MipsTLBMissException)

/*----------------------------------------------------------------------------
 *
 * MipsFPTrap --
 *
 *	Handle a floating point Trap.
 *
 *	MipsFPTrap(statusReg, causeReg, pc)
 *		unsigned statusReg;
 *		unsigned causeReg;
 *		unsigned pc;
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */
NESTED(MipsFPTrap, CALLFRAME_SIZ, ra)
	.set push
	.set hardfloat
	PTR_SUBU	sp, sp, CALLFRAME_SIZ
	mfc0	t0, MIPS_COP_0_STATUS
	HAZARD_DELAY
	REG_S	ra, CALLFRAME_RA(sp)
	.mask	0x80000000, (CALLFRAME_RA - CALLFRAME_SIZ)

#if defined(__mips_n32) || defined(__mips_n64)
	or	t1, t0, MIPS_SR_COP_1_BIT | MIPS_SR_FR
#else
	or	t1, t0, MIPS_SR_COP_1_BIT
#endif
	mtc0	t1, MIPS_COP_0_STATUS
	HAZARD_DELAY
	ITLBNOPFIX
	cfc1	t1, MIPS_FPU_CSR		# stall til FP done
	cfc1	t1, MIPS_FPU_CSR		# now get status
	nop
	sll	t2, t1, (31 - 17)		# unimplemented operation?
	bgez	t2, 3f				# no, normal trap
	nop
/*
 * We got an unimplemented operation trap so
 * fetch the instruction, compute the next PC and emulate the instruction.
 */
	bgez	a1, 1f				# Check the branch delay bit.
	nop
/*
 * The instruction is in the branch delay slot so the branch will have to
 * be emulated to get the resulting PC.
 */
	PTR_S	a2, CALLFRAME_SIZ + 8(sp)
	GET_CPU_PCPU(a0)
#mips64 unsafe?
	PTR_L	a0, PC_CURPCB(a0)
	PTR_ADDU a0, a0, U_PCB_REGS		# first arg is ptr to CPU registers
	move	a1, a2				# second arg is instruction PC
	move	a2, t1				# third arg is floating point CSR
	PTR_LA	t3, _C_LABEL(MipsEmulateBranch)	# compute PC after branch
	jalr	t3				# compute PC after branch
	move	a3, zero			# fourth arg is FALSE
/*
 * Now load the floating-point instruction in the branch delay slot
 * to be emulated.
 */
	PTR_L	a2, CALLFRAME_SIZ + 8(sp)	# restore EXC pc
	b	2f
	lw	a0, 4(a2)			# a0 = coproc instruction
/*
 * This is not in the branch delay slot so calculate the resulting
 * PC (epc + 4) into v0 and continue to MipsEmulateFP().
 */
1:
	lw	a0, 0(a2)			# a0 = coproc instruction
#xxx mips64 unsafe?
	PTR_ADDU	v0, a2, 4			# v0 = next pc
2:
	GET_CPU_PCPU(t2)
	PTR_L	t2, PC_CURPCB(t2)
	SAVE_U_PCB_REG(v0, PC, t2)		# save new pc
/*
 * Check to see if the instruction to be emulated is a floating-point
 * instruction.
 */
	srl	a3, a0, MIPS_OPCODE_SHIFT
	beq	a3, MIPS_OPCODE_C1, 4f		# this should never fail
	nop
/*
 * Send a floating point exception signal to the current process.
 */
3:
	GET_CPU_PCPU(a0)
	PTR_L	a0, PC_CURTHREAD(a0)		# get current thread
	cfc1	a2, MIPS_FPU_CSR		# code = FP execptions
	ctc1	zero, MIPS_FPU_CSR		# Clear exceptions
	PTR_LA	t3, _C_LABEL(trapsignal)
	jalr	t3
	li	a1, SIGFPE
	b	FPReturn
	nop

/*
 * Finally, we can call MipsEmulateFP() where a0 is the instruction to emulate.
 */
4:
	PTR_LA	t3, _C_LABEL(MipsEmulateFP)
	jalr	t3
	nop

/*
 * Turn off the floating point coprocessor and return.
 */
FPReturn:
	mfc0	t0, MIPS_COP_0_STATUS
	PTR_L	ra, CALLFRAME_RA(sp)
	and	t0, t0, ~MIPS_SR_COP_1_BIT
	mtc0	t0, MIPS_COP_0_STATUS
	ITLBNOPFIX
	j	ra
	PTR_ADDU sp, sp, CALLFRAME_SIZ
	.set pop
END(MipsFPTrap)

#ifndef INTRNG
/*
 * Interrupt counters for vmstat.
 */
	.data
	.globl intrcnt
	.globl sintrcnt
	.globl intrnames
	.globl sintrnames
intrnames:
	.space  INTRCNT_COUNT * (MAXCOMLEN + 1) * 2
sintrnames:
#ifdef __mips_n64
	.quad  INTRCNT_COUNT * (MAXCOMLEN + 1) * 2
#else
	.int  INTRCNT_COUNT * (MAXCOMLEN + 1) * 2
#endif

	.align	(_MIPS_SZLONG / 8)
intrcnt:
	.space  INTRCNT_COUNT * (_MIPS_SZLONG / 8) * 2
sintrcnt:
#ifdef __mips_n64
	.quad  INTRCNT_COUNT * (_MIPS_SZLONG / 8) * 2
#else
	.int  INTRCNT_COUNT * (_MIPS_SZLONG / 8) * 2
#endif
#endif /* INTRNG */


/*
 * Vector to real handler in KSEG1.
 */
	.text
VECTOR(MipsCache, unknown)
	PTR_LA	k0, _C_LABEL(MipsCacheException)
	li	k1, MIPS_KSEG0_PHYS_MASK
	and	k0, k1
	PTR_LI	k1, MIPS_KSEG1_START
	or	k0, k1
	j	k0
	nop
VECTOR_END(MipsCache)

	.set	at


/*
 * Panic on cache errors.  A lot more could be done to recover
 * from some types of errors but it is tricky.
 */
NESTED_NOPROFILE(MipsCacheException, KERN_EXC_FRAME_SIZE, ra)
	.set	noat
	.mask	0x80000000, -4
	PTR_LA	k0, _C_LABEL(panic)		# return to panic
	PTR_LA	a0, 9f				# panicstr
	MFC0	a1, MIPS_COP_0_ERROR_PC
	mfc0	a2, MIPS_COP_0_CACHE_ERR	# 3rd arg cache error

	MTC0	k0, MIPS_COP_0_ERROR_PC		# set return address

	mfc0	k0, MIPS_COP_0_STATUS		# restore status
	li	k1, MIPS_SR_DIAG_PE		# ignore further errors
	or	k0, k1
	mtc0	k0, MIPS_COP_0_STATUS		# restore status
	COP0_SYNC

	eret

	MSG("cache error @ EPC 0x%x CachErr 0x%x");
	.set	at
END(MipsCacheException)
