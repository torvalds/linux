/*	$OpenBSD: vectors.S,v 1.10 2024/09/04 07:54:52 mglocker Exp $	*/
/*	$NetBSD: exception_vector.S,v 1.19 2006/08/22 21:47:57 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "assym.h"

#include <sh/param.h>
#include <sh/locore.h>
#include <sh/pte.h>
#include <sh/trap.h>
#include <sh/ubcreg.h>
#include <sh/mmu_sh3.h>
#include <sh/mmu_sh4.h>

/* 
 * Align vectors more strictly here (where we don't really care) so
 * that .align 5 (i.e. 32B cache line) before data block does the
 * right thing w.r.t. final destinations after vectors are copied.
 */
#define _ALIGN_TEXT	.align 5
#include <sh/asm.h>

/*
 * Exception vectors.
 * The following routines are copied to vector addresses.
 *	sh_vector_generic:	VBR + 0x100
 *	sh_vector_tlbmiss:	VBR + 0x400
 *	sh_vector_interrupt:	VBR + 0x600
 */

#define VECTOR_END_MARKER(sym)			\
		.globl	sym;			\
	sym:


/*
 * LINTSTUB: Var: char sh_vector_generic[1];
 *
 * void sh_vector_generic(void);
 *	Copied to VBR+0x100.  This code should be position independent
 *	and maximum 786 bytes long (== 0x400 - 0x100).
 */
NENTRY(sh_vector_generic)
	__EXCEPTION_ENTRY
	__INTR_MASK(r0, r1)
	/* Identify exception cause */
	MOV	(EXPEVT, r0)
	mov.l	@r0,	r0
	mov.l	r0, @(TF_EXPEVT, r14)	/* trapframe->tf_expevt = EXPEVT */
	/* Get curproc */
	mov.l	.Lg_curproc, r1
	mov.l	@r1,	r4	/* 1st arg */
	/* Get TEA */
	MOV	(TEA,	r1)
	mov.l	@r1,	r6	/* 3rd arg */
	/* Check TLB exception or not */
	mov.l	.Lg_TLB_PROT_ST, r1
	cmp/hi	r1,	r0
	bt	1f

	/* tlb_exception(curproc, trapframe, TEA); */
	mov.l	.Lg_VPN_MASK, r1
	and	r1,	r6	/* va = trunc_page(va) */
	__EXCEPTION_UNBLOCK(r0, r1)
	mov.l	.Lg_tlb_exception, r0
	jsr	@r0
	 mov	r14,	r5	/* 2nd arg */
	bra	2f
	 nop

	/* general_exception(curproc, trapframe, TEA); */
1:	mov	r4,	r8
#ifdef DDB
	mov	#0,	r2
	MOV	(BBRA, r1)
	mov.w	r2,	@r1	/* disable UBC */
	mov.l	r2,	@(TF_UBC, r14)	/* clear trapframe->tf_ubc */
#endif /* DDB */
	__EXCEPTION_UNBLOCK(r0, r1)
	mov.l	.Lg_general_exception, r0
	jsr	@r0
	 mov	r14,	r5	/* 2nd arg */

	/* Check for ASTs on exit to user mode. */
	mov	r8,	r4
	mov.l	.Lg_ast,	r0
	jsr	@r0
	 mov	r14,	r5
#ifdef DDB	/* BBRA = trapframe->tf_ubc */
	__EXCEPTION_BLOCK(r0, r1)
	mov.l	@(TF_UBC, r14), r0
	MOV	(BBRA, r1)
	mov.w	r0,	@r1
#endif /* DDB */
2:	__EXCEPTION_RETURN
	/* NOTREACHED */
	.align	5
.Lg_curproc:			.long	cpu_info_store + CI_CURPROC
REG_SYMBOL(EXPEVT)
REG_SYMBOL(BBRA)
REG_SYMBOL(TEA)
.Lg_tlb_exception:		.long	tlb_exception
.Lg_general_exception:		.long	general_exception
.Lg_ast:			.long	ast
.Lg_TLB_PROT_ST:		.long	EXPEVT_TLB_PROT_ST
.Lg_VPN_MASK:			.long	0xfffff000

/* LINTSTUB: Var: char sh_vector_generic_end[1]; */
VECTOR_END_MARKER(sh_vector_generic_end)
	SET_ENTRY_SIZE(sh_vector_generic)


#ifdef SH3
/*
 * LINTSTUB: Var: char sh3_vector_tlbmiss[1];
 *
 * TLB miss vector.  We run through the fast path first, checking if
 * there's a valid mapping in curproc or kernel pmap.  We do fast path
 * with exceptions disabled, so no P3 addresses please (including no
 * kernel stack, as we cannot wire TLB entries on sh3).  We can only
 * use BANK1 registers, and of those r6 and r7 are already taken.
 *
 * If we don't find a valid mapping in the fast path, we do context
 * save and call tlb exception handler.
 *
 * Copied to VBR+0x400.  This code should be position independent
 * and maximum 512 bytes long (== 0x600 - 0x400).
 */
NENTRY(sh3_vector_tlbmiss)
	__EXCEPTION_ENTRY
	mov	#(SH3_PTEH & 0xff), r4
	mov.l	.L3_VPN_cleanup, r0
	mov.l	@r4, r5
	and	r0, r5		/* trim vpn to 4K page boundary */
	/*
	 * For the duration of fast path we keep
	 * r4: SH3_PTEH - other PTE regs are addressable as @(offset, r4)
	 * r5: { VPN, ASID } that caused the miss
	 */

	cmp/pz	r5		/* user space address? */
	bt/s	.L3_user_va
	 mov	r5, r2		/* copy of vpn to compute indices into ptd/ptp */

	/*
	 * kernel space address, use pmap_kernel(), adjust vpn for indexing
	 * see __pmap_kpte_lookup
	 */
.L3_kernel_va:
	mov.l	.L3_VM_MIN_KERNEL_ADDRESS, r0
	mov.l	.L3_kernptd,  r1 /* pmap_kernel()->pm_ptp */
	bra	.L3_fetch_pte
	 sub	r0, r2		/* vpn -= VM_MIN_KERNEL_ADDRESS */

	/* user space address, use curproc's pmap */
.L3_user_va:
	mov.l	.L3_curptd,  r1	! curproc->...->pm_ptp

	/* see __pmap_pte_lookup */
.L3_fetch_pte:
	mov.l	@r1, r3		/* fetch ptd */

	/*
	 * r2: vpn, prepared for indexing into ptd
	 * r3: pt_entry_t **ptd => pt_entry_t *ptp => pt_entry_t pte
	 */
#ifdef DEBUG
	tst	r3, r3		/* ptd == NULL  - cannot happen */
	bt/s	.L3_call_tlb_exception
#endif
	 mov	#-22, r1	/* __PMAP_PTP_SHIFT */

	/* __PMAP_PTP_INDEX(vpn) */
	mov	r2, r0
	shld	r1, r0		/* vpn >> __PMAP_PTP_SHIFT */
	mov.l	.L3_ptp_index_mask, r1
	and	r1, r0		/* ... & (__PMAP_PTP_N - 1) */
	shll2	r0		/* array index -> array offset */
	mov.l	@(r0, r3), r3	/* ptp = ptd[idx] */
	tst	r3, r3		/* if (ptp == NULL) */
	bt/s	.L3_call_tlb_exception
	 mov	#-(PGSHIFT - 2), r1

	/*
	 * __PMAP_PTP_OFSET(vpn) - except we pre-shift 2 bits left to
	 * get the array offset directly, as we know bits 10 and 11
	 * are zero (we cleaned them in r5 to get 4K aligned VPN)
	 */
	shld	r1, r2		/* vpn >> (PGSHIFT - 2) */
	mov.l	.L3_ptp_offset_mask, r0
	and	r2, r0		/* ... & ((__PMAP_PTP_PG_N - 1) << 2) */
	mov.l	@(r0, r3), r3	/* pte = ptp[idx] */


	/* r3: pte */
	/* r4: SH3_PTEH */
	/* r5: { VPN, ASID } */

	mov.l	.L3_PG_V, r0
	tst	r0, r3		/* if ((pte & PG_V) == 0) */
	bt/s	.L3_call_tlb_exception
	 nop

	mov.l	.L3_PG_HW_BITS, r1
	cmp/pz	r5		/* user space address? */
	and	r1, r3		/* pte &= PG_HW_BITS */
	bf/s	.L3_load_kernel
	 mov.l	r3, @(0x04, r4)	/* *SH3_PTEL = pte */

	/*
	 * load mapping for a user space page
	 * we reload PTEH to enter VPN aligned to 4K page boundary
	 */
.L3_load_user:
	mov.l	r5, @r4		/* *SH3_PTEH = { VPN, ASID } */
	ldtlb			/* needs 2 insns padding before RTE */
	nop
	nop
	rte
	 nop

	/*
	 * load mapping for a kernel space page
	 * we need to temporary set ASID to 0
	 */
.L3_load_kernel:
	mov.l	.L3_clear_ASID, r1
	and	r5, r1		/* *SH3_PTEH & ~SH3_PTEH_ASID_MASK */
	mov.l	r1, @r4		/* *SH3_PTEH = { VPN, ASID = 0 } */
 	ldtlb
	mov.l	r5, @r4		/* restore ASID */
	nop
	rte
	 nop


	/*
	 * If we haven't found a valid mapping in the fast path
	 *     tlb_exception(curproc, trapframe, tea)
	 */
.L3_call_tlb_exception:
	__EXCEPTION_ENTRY
	mov.l	.L3_SH3_EXPEVT, r2
	mov.l	.L3_curproc, r1
	mov	#(SH3_TEA & 0xff), r0
	mov.l	@r2, r2			/* *SH3_EXPEVT */
	mov.l	@r0, r6			/* arg3: va = *SH3_TEA */
	mov.l	@r1, r4			/* arg1: curproc */
	__INTR_MASK(r0, r1)
	__EXCEPTION_UNBLOCK(r0, r1)
	mov.l	.L3_tlb_exception,	r0
	ov.l	r2, @(TF_EXPEVT, r14)	/* tf->tf_expevt = EXPEVT */
	jsr	@r0
	 mov	r14, r5			/* arg2: trapframe */
	__EXCEPTION_RETURN

	.align	4
.L3_VPN_cleanup:		.long	~0x00000c00
.L3_curptd:			.long	curptd
.L3_kernptd:			.long	__pmap_kernel
.L3_VM_MIN_KERNEL_ADDRESS:	.long	VM_MIN_KERNEL_ADDRESS
.L3_ptp_index_mask:		.long	0x1ff
.L3_ptp_offset_mask:		.long	0x3ff << 2
.L3_PG_HW_BITS:			.long	PG_HW_BITS
.L3_PG_V:			.long	PG_V
.L3_clear_ASID:			.long	~SH3_PTEH_ASID_MASK
.L3_SH3_EXPEVT:			.long	SH3_EXPEVT
.L3_curproc:			.long	cpu_info_store + CI_CURPROC
.L3_tlb_exception:		.long	tlb_exception

/* LINTSTUB: Var: char sh3_vector_tlbmiss_end[1]; */
VECTOR_END_MARKER(sh3_vector_tlbmiss_end)
	SET_ENTRY_SIZE(sh3_vector_tlbmiss)
#endif /* SH3 */


#ifdef SH4
/*
 * LINTSTUB: Var: char sh4_vector_tlbmiss[1];
 *
 * TLB miss vector.  We run through the fast path first, checking if
 * there's a valid mapping in curproc or kernel pmap.  We do fast path
 * with exceptions disabled, so no P3 addresses please (though we can
 * use kernel stack if need be, as its TLB entries are wired).  We can
 * only use BANK1 registers, and of those r6 and r7 are already taken.
 *
 * If we don't find a valid mapping in the fast path, we do context
 * save and call tlb exception handler.
 *
 * Copied to VBR+0x400.  This code should be relocatable
 * and maximum 512 bytes long (== 0x600 - 0x400).
 */
NENTRY(sh4_vector_tlbmiss)
	mov.l	.L4_SH4_PTEH, r4
	mov.l	.L4_VPN_cleanup, r0
	mov.l	@r4, r5
	and	r0, r5		/* trim vpn to 4K page boundary */
	/*
	 * For the duration of fast path we keep
	 * r4: SH4_PTEH - other PTE regs are addressable as @(offset, r4)
	 * r5: { VPN, ASID } that caused the miss
	  */

	cmp/pz	r5		/* user space address? */
	bt/s	.L4_user_va
	 mov	r5, r2		/* copy of vpn to compute indices into ptd/ptp */

	/*
	 * kernel space address, use pmap_kernel(), adjust vpn for indexing
	 * see __pmap_kpte_lookup
	 */
.L4_kernel_va:
	mov.l	.L4_VM_MIN_KERNEL_ADDRESS, r0
	mov.l	.L4_kernptd,  r1 /* pmap_kernel()->pm_ptp */
	bra	.L4_fetch_pte
	 sub	r0, r2		/* vpn -= VM_MIN_KERNEL_ADDRESS */

	/* user space address, use curproc's pmap */
.L4_user_va:
	mov.l	.L4_curptd,  r1	/* curproc->...->pm_ptp */

	/* see __pmap_pte_lookup */
.L4_fetch_pte:
	mov.l	@r1, r3		/* fetch ptd */

	/*
	 * r2: vpn, prepared for indexing into ptd
	 * r3: pt_entry_t **ptd => pt_entry_t *ptp => pt_entry_t pte
	 */
#ifdef DEBUG
	tst	r3, r3		/* ptd == NULL  - cannot happen */
	bt/s	.L4_call_tlb_exception
#endif
	 mov	#-22, r1	/* __PMAP_PTP_SHIFT */

	/* __PMAP_PTP_INDEX(vpn) */
	mov	r2, r0
	shld	r1, r0		/* vpn >> __PMAP_PTP_SHIFT */
	mov.l	.L4_ptp_index_mask, r1
	and	r1, r0		/* ... & (__PMAP_PTP_N - 1) */
	shll2	r0		/* array index -> array offset */
	mov.l	@(r0, r3), r3	/* ptp = ptd[idx] */
	tst	r3, r3		/* if (ptp == NULL) */
	bt/s	.L4_call_tlb_exception
	 mov	#-(PGSHIFT - 2), r1

	/*
	 * __PMAP_PTP_OFSET(vpn) - except we pre-shift 2 bits left to
	 * get the array offset directly, as we know bits 10 and 11
	 * are zero (we cleaned them in r5 to get 4K aligned VPN)
	 */
	shld	r1, r2		/* vpn >> (PGSHIFT - 2) */
	mov.l	.L4_ptp_offset_mask, r0
	and	r2, r0		/* ... & ((__PMAP_PTP_PG_N - 1) << 2) */
	mov.l	@(r0, r3), r3	/* pte = ptp[idx] */


	/* r3: pte */
	/* r4: SH4_PTEH */
	/* r5: { VPN, ASID } */

	mov.l	.L4_PG_V, r0
	tst	r0, r3		/* if ((pte & PG_V) == 0) */
	bt/s	.L4_call_tlb_exception
	 mov	r3, r0		/* prepare PCMCIA SA bits for SH4_PTEA */

	mov.l	.L4_PG_HW_BITS, r1
	shlr8	r0
	and	r1, r3		/* pte &= PG_HW_BITS */
	shlr	r0		/* pte >> _PG_PCMCIA_SHIFT */
	cmp/pz	r5		/* user space address? */
	and	#SH4_PTEA_SA_MASK, r0
	mov.l	r3, @(0x04, r4)	/* *SH4_PTEL = pte */
	bf/s	.L4_load_kernel
	 mov.l	r0, @(0x34, r4)	/* *SH4_PTEA = PCMCIA space attrs */

	/*
	 * Load mapping for a user space page
	 * we reload PTEH to enter VPN aligned to 4K page boundary
	 */
.L4_load_user:
	mov.l	r5, @r4		/* *SH4_PTEH = { VPN, ASID } */
	ldtlb			/* needs 1 insn padding before RTE */
	nop
	rte
	 nop

	/*
	 * Load mapping for a kernel space page
	 * we need to temporary set ASID to 0
	 */
.L4_load_kernel:
	mov.l	.L4_clear_ASID, r1
	and	r5, r1		/* *SH4_PTEH & ~SH4_PTEH_ASID_MASK */
	mov.l	r1, @r4		/* *SH4_PTEH = { VPN, ASID = 0 } */
	ldtlb
	mov.l	r5, @r4		/* restore ASID */
	rte
	 nop


	/*
	 * If we haven't found a valid mapping in the fast path
	 *     tlb_exception(curproc, trapframe, tea)
	 */
.L4_call_tlb_exception:
	__EXCEPTION_ENTRY
	mov.l	.L4_SH4_PTEH, r0
	mov.l	.L4_curproc, r1
	mov.l	@(0x24, r0), r2		/* *SH4_EXPEVT */
	mov.l	@(0x0c, r0), r6		/* arg3: va = *SH4_TEA */
	mov.l	@r1, r4			/* arg1: curproc */
	__INTR_MASK(r0, r1)
	__EXCEPTION_UNBLOCK(r0, r1)
	mov.l	.L4_tlb_exception,	r0
	mov.l	r2, @(TF_EXPEVT, r14)	/* tf->tf_expevt = EXPEVT */
	jsr	@r0
	 mov	r14,	r5	/* arg2: trapframe */
	__EXCEPTION_RETURN

	.align	5

.L4_SH4_PTEH:			.long	SH4_PTEH
.L4_VPN_cleanup:		.long	~0x00000c00
.L4_curptd:			.long	curptd
.L4_kernptd:			.long	__pmap_kernel
.L4_VM_MIN_KERNEL_ADDRESS:	.long	VM_MIN_KERNEL_ADDRESS
.L4_ptp_index_mask:		.long	0x1ff
.L4_ptp_offset_mask:		.long	0x3ff << 2
.L4_PG_HW_BITS:			.long	PG_HW_BITS
.L4_PG_V:			.long	PG_V
.L4_clear_ASID:			.long	~SH4_PTEH_ASID_MASK
.L4_curproc:			.long	cpu_info_store + CI_CURPROC
.L4_tlb_exception:		.long	tlb_exception


/* LINTSTUB: Var: char sh4_vector_tlbmiss_end[1]; */
VECTOR_END_MARKER(sh4_vector_tlbmiss_end)
	SET_ENTRY_SIZE(sh4_vector_tlbmiss)
#endif /* SH4 */


/*
 * LINTSTUB: Var: char sh_vector_interrupt[1];
 *
 * void sh_vector_interrupt(void);
 *	Copied to VBR+0x600.  This code should be position independent.
 */
NENTRY(sh_vector_interrupt)
	__EXCEPTION_ENTRY
	xor	r0,	r0
	mov.l	r0,	@(TF_EXPEVT, r14) /* (for debug) */
	stc	r0_bank,r6	/* ssp */
	/* Enable exceptions for P3 access */
	__INTR_MASK(r0, r1)
	__EXCEPTION_UNBLOCK(r0, r1)
	/* uvmexp.intrs++ */
	mov.l	.Li_uvmexp_intrs, r0
	mov.l	@r0,	r1
	add	#1	r1
	mov.l	r1,	@r0
	/* Dispatch interrupt handler */
	mov.l	.Li_intc_intr, r0
	jsr	@r0		/* intc_intr(ssr, spc, ssp) */
	 nop
	/* Check for ASTs on exit to user mode. */
	mov.l	.Li_curproc,	r0
	mov.l	@r0,	r4	/* 1st arg */
	mov.l	.Li_ast, r0
	jsr	@r0
	 mov	r14,	r5	/* 2nd arg */
	__EXCEPTION_RETURN

	.align	5
.Li_curproc:		.long	cpu_info_store + CI_CURPROC
.Li_intc_intr:		.long	intc_intr
.Li_ast:		.long	ast
.Li_uvmexp_intrs:	.long	uvmexp + UVMEXP_INTRS

/* LINTSTUB: Var: char sh_vector_interrupt_end[1]; */
VECTOR_END_MARKER(sh_vector_interrupt_end)
	SET_ENTRY_SIZE(sh_vector_interrupt)
