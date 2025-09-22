/*	$OpenBSD: context.S,v 1.66 2023/10/24 13:20:10 claudio Exp $ */

/*
 * Copyright (c) 2002-2003 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
#include <machine/pte.h>
#include <machine/regnum.h>
#include <mips64/mips_cpu.h>
#include <machine/cpustate.h>
#ifdef CPU_LOONGSON2
#include <machine/loongson2.h>
#endif

#include "assym.h"

	.set	mips3
	.set	noreorder		# Noreorder is default style!

/*
 * Save registers and state used by reboot to take snapshot.
 */
LEAF(savectx, 0)
	REG_S	s0, PCB_CONTEXT+0*REGSZ(a0)
	REG_S	s1, PCB_CONTEXT+1*REGSZ(a0)
	REG_S	s2, PCB_CONTEXT+2*REGSZ(a0)
	REG_S	s3, PCB_CONTEXT+3*REGSZ(a0)
	MFC0	v0, COP_0_STATUS_REG
	MFC0_HAZARD
	REG_S	s4, PCB_CONTEXT+4*REGSZ(a0)
	REG_S	s5, PCB_CONTEXT+5*REGSZ(a0)
	REG_S	s6, PCB_CONTEXT+6*REGSZ(a0)
	REG_S	s7, PCB_CONTEXT+7*REGSZ(a0)
	REG_S	sp, PCB_CONTEXT+8*REGSZ(a0)
	REG_S	s8, PCB_CONTEXT+9*REGSZ(a0)
	REG_S	ra, PCB_CONTEXT+10*REGSZ(a0)
	REG_S	v0, PCB_CONTEXT+11*REGSZ(a0)
	j	ra
	 move	v0, zero
END(savectx)

LEAF(cpu_idle_cycle_nop, 0)
	j	ra
	 NOP
END(cpu_idle_cycle_nop)

LEAF(cpu_idle_cycle_wait, 0)
	wait
	j	ra
	 NOP
END(cpu_idle_cycle_wait)

/*
 * cpu_switchto_asm(struct proc *oldproc, struct proc *newproc)
 */
NON_LEAF(cpu_switchto_asm, FRAMESZ(CF_SZ), ra)
	GET_CPU_INFO(t1, t3)
	PTR_L	t3, CI_CURPROCPADDR(t1)
	REG_S	sp, PCB_CONTEXT+8*REGSZ(t3)	# save old sp

	PTR_SUBU sp, sp, FRAMESZ(CF_SZ)
	REG_S	ra, CF_RA_OFFS(sp)
	.mask	0x80000000, (CF_RA_OFFS - FRAMESZ(CF_SZ))

	beqz	a0, 1f
	 MFC0	v0, COP_0_STATUS_REG

	REG_S	s0, PCB_CONTEXT+0*REGSZ(t3)	# do a 'savectx()'
	REG_S	s1, PCB_CONTEXT+1*REGSZ(t3)
	REG_S	s2, PCB_CONTEXT+2*REGSZ(t3)
	REG_S	s3, PCB_CONTEXT+3*REGSZ(t3)
	REG_S	s4, PCB_CONTEXT+4*REGSZ(t3)
	REG_S	s5, PCB_CONTEXT+5*REGSZ(t3)
	REG_S	s6, PCB_CONTEXT+6*REGSZ(t3)
	REG_S	s7, PCB_CONTEXT+7*REGSZ(t3)
	REG_S	s8, PCB_CONTEXT+9*REGSZ(t3)
	REG_S	ra, PCB_CONTEXT+10*REGSZ(t3)
	REG_S	v0, PCB_CONTEXT+11*REGSZ(t3)

1:
	/*
	 * Switch to new context
	 */
	move	s0, a1				# save p
	move	s1, v0				# save status register
	jal	pmap_activate
	 move	a0, s0

	/*
	 * Disable interrupts
	 */
	ori	s1, SR_INT_ENAB
	xori	s1, SR_INT_ENAB
	MTC0	s1, COP_0_STATUS_REG
	MTC0_SR_IE_HAZARD

	PTR_L	t3, P_ADDR(s0)			# get uarea pointer.
	GET_CPU_INFO(t1, t0)
	PTR_S	s0, CI_CURPROC(t1)		# set curproc
	PTR_S	t3, CI_CURPROCPADDR(t1)
	
#ifdef MULTIPROCESSOR
	PTR_S	t1, P_CPU(s0)
#endif
	li	t1, SONPROC
	sb	t1, P_STAT(s0)			# set to onproc.

	/* get process ASID */
	PTR_L	t0, P_VMSPACE(s0)		# p->p_vmspace
	PTR_L	t1, VMSPACE_PMAP(t0)		# ->vm_map.pmap
#ifdef MULTIPROCESSOR
	GET_CPU_INFO(v0, t2)
	PTR_L	v0, CI_CPUID(v0)
	PTR_SLL	v0, v0, 0x3			# size of pmap_asid_info
	PTR_ADDU t1, t1, v0
#endif
	lw	v0, PM_ASID(t1)			# ->pm_asid[cpuid].pma_asid

#if UPAGES > 1	/* { */
	or	v0, t3
	dmtc0	v0, COP_0_TLB_HI		# init high entry (tlbid)

	/*
	 * We need to wire the process kernel stack mapping so there
	 * will be no tlb misses in exception handlers. This is done
	 * by invalidating any tlb entries mapping the U-area and
	 * put valid mappings in tlb entries 0 and 1.
	 */

	LA	t1, CKSEG0_BASE
	PTR_SUBU t2, t3, t1
	bgez	t2, ctx3			# in CKSEG0
	LA	t1, VM_MIN_KERNEL_ADDRESS	# (safe if expands to > 1 insn)
	PTR_SUBU t2, t3, t1
	bltz	t2, ctx3			# not mapped.
	PTR_SRL	t2, PGSHIFT+1
	PTR_L	t1, Sysmap
	TLB_HAZARD
	tlbp
	TLB_HAZARD			# necessary?
	PTR_SLL	t2, PTE_LOG + 1
	PTR_ADDU t1, t2				# t1 now points at ptes.
	mfc0	t0, COP_0_TLB_INDEX
	nop
	bltz	t0, ctx1			# not in tlb
	LA	t2, CKSEG0_BASE			# safe if expands to > 1 insn

	dmtc0	t2, COP_0_TLB_HI		# invalidate it.
	dmtc0	zero, COP_0_TLB_LO0
	dmtc0	zero, COP_0_TLB_LO1
	TLB_HAZARD
	tlbwi
	TLB_HAZARD

ctx1:
	mtc0	zero, COP_0_TLB_INDEX
	dmtc0	v0, COP_0_TLB_HI
	PTE_LOAD ta0, 0(t1)
	PTE_LOAD ta1, PTE_OFFS(t1)
	PTE_CLEAR_SWBITS(ta0)
	PTE_CLEAR_SWBITS(ta1)
	dmtc0	ta0, COP_0_TLB_LO0
	dmtc0	ta1, COP_0_TLB_LO1
	PTR_ADDU v0, 2*PAGE_SIZE
	TLB_HAZARD
	tlbwi
	TLB_HAZARD

#if UPAGES > 2	/* { */
	dmtc0	v0, COP_0_TLB_HI		# init high entry (tlbid)
	PTE_LOAD ta0, (2*PTE_OFFS)(t1)
	PTE_LOAD ta1, (3*PTE_OFFS)(t1)
	PTE_CLEAR_SWBITS(ta0)
	TLB_HAZARD
	tlbp
	TLB_HAZARD			# necessary?
	PTE_CLEAR_SWBITS(ta1)
	mfc0	t0, COP_0_TLB_INDEX
	nop
	bltz	t0, ctx2			# not in tlb
	li	t2, 1

	dmtc0	t2, COP_0_TLB_HI		# invalidate it.
	dmtc0	zero, COP_0_TLB_LO0
	dmtc0	zero, COP_0_TLB_LO1
	TLB_HAZARD
	tlbwi
	TLB_HAZARD

ctx2:
	mtc0	t2, COP_0_TLB_INDEX
	dmtc0	v0, COP_0_TLB_HI
	dmtc0	ta0, COP_0_TLB_LO0
	dmtc0	ta1, COP_0_TLB_LO1
	TLB_HAZARD
	tlbwi
	TLB_HAZARD
#endif	/* } UPAGES > 2 */
ctx3:
#else	/* } UPAGES > 1 { */
#if PG_ASID_SHIFT != 0
	dsll	v0, PG_ASID_SHIFT
#endif
	DMTC0	v0, COP_0_TLB_HI		# init high entry (tlbid)
	MTC0_HAZARD
#endif	/* } UPAGES > 1 */

#ifdef CPU_LOONGSON2
	li	v0, COP_0_DIAG_ITLB_CLEAR | COP_0_DIAG_BTB_CLEAR | COP_0_DIAG_RAS_DISABLE
	dmtc0	v0, COP_0_DIAG
#endif

#ifdef CPU_MIPS64R2
	/*
	 * Restore UserLocal register.
	 */
	lw	t1, cpu_has_userlocal
	beq	t1, zero, 1f
	 nop
	.set	push
	.set	mips64r2
	ld	t0, P_TCB(s0)
	dmtc0	t0, COP_0_USERLOCAL
	.set	pop
1:
#endif /* CPU_MIPS64R2 */

	/*
	 * Restore registers and return.
	 */

	REG_L	s0, PCB_CONTEXT+0*REGSZ(t3)
	REG_L	s1, PCB_CONTEXT+1*REGSZ(t3)
	REG_L	s2, PCB_CONTEXT+2*REGSZ(t3)
	REG_L	s3, PCB_CONTEXT+3*REGSZ(t3)
	REG_L	s4, PCB_CONTEXT+4*REGSZ(t3)
	REG_L	s5, PCB_CONTEXT+5*REGSZ(t3)
	REG_L	s6, PCB_CONTEXT+6*REGSZ(t3)
	REG_L	s7, PCB_CONTEXT+7*REGSZ(t3)
	REG_L	sp, PCB_CONTEXT+8*REGSZ(t3)
	REG_L	s8, PCB_CONTEXT+9*REGSZ(t3)
	REG_L	ra, PCB_CONTEXT+10*REGSZ(t3)
	REG_L	v0, PCB_CONTEXT+11*REGSZ(t3)
	ori	v0, v0, SR_INT_ENAB
	MTC0	v0, COP_0_STATUS_REG
	MTC0_SR_IE_HAZARD
	j	ra
	 NOP
END(cpu_switchto_asm)

/*-------------------------------------------------------------- proc_trampoline
 *	Setup for and return to user.
 */
LEAF(proc_trampoline, 0)
#ifdef DDB
	move	zero, ra
#endif
	jal	proc_trampoline_mi
	 NOP
	jal	updateimask		# Make sure SR imask is updated
	 xor	a0, a0			# and interrupts enabled

	jal	s0
	 move	a0,s1			# invoke callback.

	MFC0	t0, COP_0_STATUS_REG
	MFC0_HAZARD
	LI	t1, ~SR_INT_ENAB
	and	t0, t0, t1
	MTC0	t0, COP_0_STATUS_REG
	MTC0_SR_IE_HAZARD

	ori	t0, SR_EXL		# restoring to user mode.
	MTC0	t0, COP_0_STATUS_REG	# must set exception level bit.
	MTC0_SR_IE_HAZARD

	.set	noat
	GET_CPU_INFO(k1, k0)
	PTR_L	k0, CI_CURPROCPADDR(k1)
	RESTORE_CPU_SREG(k0, 0)
	RESTORE_REG(a0, PC, k0, 0)
	RESTORE_CPU(k0, 0)
	RESTORE_REG(sp, SP, k0, 0)
	LI	k0, 0
	LI	k1, 0
	ERET
	.set	at
END(proc_trampoline)
