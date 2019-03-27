/*-
 * Copyright (C) 2007-2009 Semihalf, Rafal Jaworowski <raj@semihalf.com>
 * Copyright (C) 2006 Semihalf, Marian Balakowicz <m8@semihalf.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include "assym.inc"

#include "opt_hwpmc_hooks.h"

#include <machine/asm.h>
#include <machine/hid.h>
#include <machine/param.h>
#include <machine/spr.h>
#include <machine/pte.h>
#include <machine/trap.h>
#include <machine/vmparam.h>
#include <machine/tlb.h>

#define TMPSTACKSZ	16384

#ifdef __powerpc64__
#define GET_TOCBASE(r)  \
	mfspr	r, SPR_SPRG8
#define	TOC_RESTORE	nop
#define	CMPI	cmpdi
#define	CMPL	cmpld
#define	LOAD	ld
#define	LOADX	ldarx
#define	STORE	std
#define	STOREX	stdcx.
#define	STU	stdu
#define	CALLSIZE	48
#define	REDZONE		288
#define	THREAD_REG	%r13
#define	ADDR(x)	\
	.llong	x
#define	WORD_SIZE	8
#else
#define	GET_TOCBASE(r)
#define	TOC_RESTORE
#define	CMPI	cmpwi
#define	CMPL	cmplw
#define	LOAD	lwz
#define	LOADX	lwarx
#define	STOREX	stwcx.
#define	STORE	stw
#define	STU	stwu
#define	CALLSIZE	8
#define	REDZONE		0
#define	THREAD_REG	%r2
#define	ADDR(x)	\
	.long	x
#define	WORD_SIZE	4
#endif

	.text
	.globl	btext
btext:

/*
 * This symbol is here for the benefit of kvm_mkdb, and is supposed to
 * mark the start of kernel text.
 */
	.globl	kernel_text
kernel_text:

/*
 * Startup entry.  Note, this must be the first thing in the text segment!
 */
	.text
	.globl	__start
__start:

/*
 * Assumptions on the boot loader:
 *  - System memory starts from physical address 0
 *  - It's mapped by a single TLB1 entry
 *  - TLB1 mapping is 1:1 pa to va
 *  - Kernel is loaded at 64MB boundary
 *  - All PID registers are set to the same value
 *  - CPU is running in AS=0
 *
 * Registers contents provided by the loader(8):
 *	r1	: stack pointer
 *	r3	: metadata pointer
 *
 * We rearrange the TLB1 layout as follows:
 *  - Find TLB1 entry we started in
 *  - Make sure it's protected, invalidate other entries
 *  - Create temp entry in the second AS (make sure it's not TLB[1])
 *  - Switch to temp mapping
 *  - Map 64MB of RAM in TLB1[1]
 *  - Use AS=0, set EPN to VM_MIN_KERNEL_ADDRESS and RPN to kernel load address
 *  - Switch to TLB1[1] mapping
 *  - Invalidate temp mapping
 *
 * locore registers use:
 *	r1	: stack pointer
 *	r2	: trace pointer (AP only, for early diagnostics)
 *	r3-r27	: scratch registers
 *	r28	: temp TLB1 entry
 *	r29	: initial TLB1 entry we started in
 *	r30-r31	: arguments (metadata pointer)
 */

/*
 * Keep arguments in r30 & r31 for later use.
 */
	mr	%r30, %r3
	mr	%r31, %r4

/*
 * Initial cleanup
 */
	li	%r3, PSL_DE	/* Keep debug exceptions for CodeWarrior. */
#ifdef __powerpc64__
	oris	%r3, %r3, PSL_CM@h
#endif
	mtmsr	%r3
	isync

/*
 * Initial HIDs configuration
 */
1:
	mfpvr	%r3
	rlwinm	%r3, %r3, 16, 16, 31

	lis	%r4, HID0_E500_DEFAULT_SET@h
	ori	%r4, %r4, HID0_E500_DEFAULT_SET@l

	/* Check for e500mc and e5500 */
	cmpli	0, 0, %r3, FSL_E500mc
	bne	2f

	lis	%r4, HID0_E500MC_DEFAULT_SET@h
	ori	%r4, %r4, HID0_E500MC_DEFAULT_SET@l
	b	3f
2:
	cmpli	0, 0, %r3, FSL_E5500
	bne	3f

	lis	%r4, HID0_E5500_DEFAULT_SET@h
	ori	%r4, %r4, HID0_E5500_DEFAULT_SET@l

3:
	mtspr	SPR_HID0, %r4
	isync

/*
 * E500mc and E5500 do not have HID1 register, so skip HID1 setup on
 * this core.
 */
	cmpli	0, 0, %r3, FSL_E500mc
	beq	1f
	cmpli	0, 0, %r3, FSL_E5500
	beq	1f
	cmpli	0, 0, %r3, FSL_E6500
	beq	1f

	lis	%r3, HID1_E500_DEFAULT_SET@h
	ori	%r3, %r3, HID1_E500_DEFAULT_SET@l
	mtspr	SPR_HID1, %r3
	isync
1:
	/* Invalidate all entries in TLB0 */
	li	%r3, 0
	bl	tlb_inval_all

	cmpwi	%r30, 0
	beq	done_mapping

/*
 * Locate the TLB1 entry that maps this code
 */
	bl	1f
1:	mflr	%r3
	bl	tlb1_find_current	/* the entry found is returned in r29 */

	bl	tlb1_inval_all_but_current

/*
 * Create temporary mapping in AS=1 and switch to it
 */
	bl	tlb1_temp_mapping_as1

	mfmsr	%r3
	ori	%r3, %r3, (PSL_IS | PSL_DS)
	bl	2f
2:	mflr	%r4
	addi	%r4, %r4, (3f - 2b)
	mtspr	SPR_SRR0, %r4
	mtspr	SPR_SRR1, %r3
	rfi				/* Switch context */

/*
 * Invalidate initial entry
 */
3:
	mr	%r3, %r29
	bl	tlb1_inval_entry

/*
 * Setup final mapping in TLB1[1] and switch to it
 */
	/* Final kernel mapping, map in 64 MB of RAM */
	lis	%r3, MAS0_TLBSEL1@h	/* Select TLB1 */
	li	%r4, 0			/* Entry 0 */
	rlwimi	%r3, %r4, 16, 10, 15
	mtspr	SPR_MAS0, %r3
	isync

	li	%r3, (TLB_SIZE_64M << MAS1_TSIZE_SHIFT)@l
	oris	%r3, %r3, (MAS1_VALID | MAS1_IPROT)@h
	mtspr	SPR_MAS1, %r3		/* note TS was not filled, so it's TS=0 */
	isync

	LOAD_ADDR(%r3, VM_MIN_KERNEL_ADDRESS)
	ori	%r3, %r3, (_TLB_ENTRY_SHARED | MAS2_M)@l /* WIMGE = 0b00100 */
	mtspr	SPR_MAS2, %r3
	isync

	/* Discover phys load address */
	bl	3f
3:	mflr	%r4			/* Use current address */
	rlwinm	%r4, %r4, 0, 0, 5	/* 64MB alignment mask */
	ori	%r4, %r4, (MAS3_SX | MAS3_SW | MAS3_SR)@l
	mtspr	SPR_MAS3, %r4		/* Set RPN and protection */
	isync
	bl	zero_mas7
	bl	zero_mas8
	isync
	tlbwe
	isync
	msync

	/* Switch to the above TLB1[1] mapping */
	bl	4f
4:	mflr	%r4
#ifdef __powerpc64__
	clrldi	%r4, %r4, 38
	clrrdi	%r3, %r3, 12
#else
	rlwinm	%r4, %r4, 0, 6, 31	/* Current offset from kernel load address */
	rlwinm	%r3, %r3, 0, 0, 19
#endif
	add	%r4, %r4, %r3		/* Convert to kernel virtual address */
	addi	%r4, %r4, (5f - 4b)
	li	%r3, PSL_DE		/* Note AS=0 */
#ifdef __powerpc64__
	oris	%r3, %r3, PSL_CM@h
#endif
	mtspr   SPR_SRR0, %r4
	mtspr   SPR_SRR1, %r3
	rfi

/*
 * Invalidate temp mapping
 */
5:
	mr	%r3, %r28
	bl	tlb1_inval_entry

done_mapping:

#ifdef __powerpc64__
	/* Set up the TOC pointer */
	b	0f
	.align 3
0:	nop
	bl	1f
	.llong	__tocbase + 0x8000 - .
1:	mflr	%r2
	ld	%r1,0(%r2)
	add	%r2,%r1,%r2
	mtspr	SPR_SPRG8, %r2

	/* Get load offset */
	ld	%r31,-0x8000(%r2) /* First TOC entry is TOC base */
	subf    %r31,%r31,%r2	/* Subtract from real TOC base to get base */

	/* Set up the stack pointer */
	ld	%r1,TOC_REF(tmpstack)(%r2)
	addi	%r1,%r1,TMPSTACKSZ-96
	add	%r1,%r1,%r31
	bl	1f
	.llong _DYNAMIC-.
1:	mflr	%r3
	ld	%r4,0(%r3)
	add	%r3,%r4,%r3
	mr	%r4,%r31
#else
/*
 * Setup a temporary stack
 */
	bl	1f
	.long tmpstack-.
1:	mflr	%r1
	lwz	%r2,0(%r1)
	add	%r1,%r1,%r2
	addi	%r1, %r1, (TMPSTACKSZ - 16)

/*
 * Relocate kernel
 */
	bl      1f
	.long   _DYNAMIC-.
	.long   _GLOBAL_OFFSET_TABLE_-.
1:	mflr    %r5
	lwz	%r3,0(%r5)	/* _DYNAMIC in %r3 */
	add	%r3,%r3,%r5
	lwz	%r4,4(%r5)	/* GOT pointer */
	add	%r4,%r4,%r5
	lwz	%r4,4(%r4)	/* got[0] is _DYNAMIC link addr */
	subf	%r4,%r4,%r3	/* subtract to calculate relocbase */
#endif
	bl	CNAME(elf_reloc_self)
	TOC_RESTORE

/*
 * Initialise exception vector offsets
 */
	bl	CNAME(ivor_setup)
	TOC_RESTORE

/*
 * Set up arguments and jump to system initialization code
 */
	mr	%r3, %r30
	mr	%r4, %r31

	/* Prepare core */
	bl	CNAME(booke_init)
	TOC_RESTORE

	/* Switch to thread0.td_kstack now */
	mr	%r1, %r3
	li	%r3, 0
	STORE	%r3, 0(%r1)

	/* Machine independet part, does not return */
	bl	CNAME(mi_startup)
	TOC_RESTORE
	/* NOT REACHED */
5:	b	5b


#ifdef SMP
/************************************************************************/
/* AP Boot page */
/************************************************************************/
	.text
	.globl	__boot_page
	.align	12
__boot_page:
	bl	1f

	.globl	bp_trace
bp_trace:
	.long	0

	.globl	bp_kernload
bp_kernload:
	.long	0

/*
 * Initial configuration
 */
1:
	mflr    %r31		/* r31 hold the address of bp_trace */

	/* Set HIDs */
	mfpvr	%r3
	rlwinm	%r3, %r3, 16, 16, 31

	/* HID0 for E500 is default */
	lis	%r4, HID0_E500_DEFAULT_SET@h
	ori	%r4, %r4, HID0_E500_DEFAULT_SET@l

	cmpli	0, 0, %r3, FSL_E500mc
	bne	2f
	lis	%r4, HID0_E500MC_DEFAULT_SET@h
	ori	%r4, %r4, HID0_E500MC_DEFAULT_SET@l
	b	3f
2:
	cmpli	0, 0, %r3, FSL_E5500
	bne	3f
	lis	%r4, HID0_E5500_DEFAULT_SET@h
	ori	%r4, %r4, HID0_E5500_DEFAULT_SET@l
3:
	mtspr	SPR_HID0, %r4
	isync

	/* Enable branch prediction */
	li	%r3, BUCSR_BPEN
	mtspr	SPR_BUCSR, %r3
	isync

	/* Invalidate all entries in TLB0 */
	li	%r3, 0
	bl	tlb_inval_all

/*
 * Find TLB1 entry which is translating us now
 */
	bl	2f
2:	mflr	%r3
	bl	tlb1_find_current	/* the entry number found is in r29 */

	bl	tlb1_inval_all_but_current

/*
 * Create temporary translation in AS=1 and switch to it
 */

	bl	tlb1_temp_mapping_as1

	mfmsr	%r3
	ori	%r3, %r3, (PSL_IS | PSL_DS)
#ifdef __powerpc64__
	oris	%r3, %r3, PSL_CM@h
#endif
	bl	3f
3:	mflr	%r4
	addi	%r4, %r4, (4f - 3b)
	mtspr	SPR_SRR0, %r4
	mtspr	SPR_SRR1, %r3
	rfi				/* Switch context */

/*
 * Invalidate initial entry
 */
4:
	mr	%r3, %r29
	bl	tlb1_inval_entry

/*
 * Setup final mapping in TLB1[1] and switch to it
 */
	/* Final kernel mapping, map in 64 MB of RAM */
	lis	%r3, MAS0_TLBSEL1@h	/* Select TLB1 */
	li	%r4, 0			/* Entry 0 */
	rlwimi	%r3, %r4, 16, 4, 15
	mtspr	SPR_MAS0, %r3
	isync

	li	%r3, (TLB_SIZE_64M << MAS1_TSIZE_SHIFT)@l
	oris	%r3, %r3, (MAS1_VALID | MAS1_IPROT)@h
	mtspr	SPR_MAS1, %r3		/* note TS was not filled, so it's TS=0 */
	isync

	LOAD_ADDR(%r3, VM_MIN_KERNEL_ADDRESS)
	ori	%r3, %r3, (_TLB_ENTRY_SHARED | MAS2_M)@l /* WIMGE = 0b00100 */
	mtspr	SPR_MAS2, %r3
	isync

	/* Retrieve kernel load [physical] address from bp_kernload */
#ifdef __powerpc64__
	b	0f
	.align	3
0:
	nop
#endif
	bl 5f
	ADDR(bp_kernload)
	ADDR(__boot_page)
5:	mflr	%r3
#ifdef __powerpc64__
	ld	%r4, 0(%r3)
	ld	%r5, 8(%r3)
	clrrdi	%r3, %r3, 12
#else
	lwz	%r4, 0(%r3)
	lwz	%r5, 4(%r3)
	rlwinm	%r3, %r3, 0, 0, 19
#endif
	sub	%r4, %r4, %r5	/* offset of bp_kernload within __boot_page */
	lwzx	%r3, %r4, %r3

	/* Set RPN and protection */
	ori	%r3, %r3, (MAS3_SX | MAS3_SW | MAS3_SR)@l
	mtspr	SPR_MAS3, %r3
	isync
	bl	zero_mas7
	bl	zero_mas8
	isync
	tlbwe
	isync
	msync

	/* Switch to the final mapping */
	bl	6f
6:	mflr	%r3
	rlwinm	%r3, %r3, 0, 0xfff	/* Offset from boot page start */
	add	%r3, %r3, %r5		/* Make this virtual address */
	addi	%r3, %r3, (7f - 6b)
#ifdef __powerpc64__
	lis	%r4, PSL_CM@h		/* Note AS=0 */
#else
	li	%r4, 0			/* Note AS=0 */
#endif
	mtspr	SPR_SRR0, %r3
	mtspr	SPR_SRR1, %r4
	rfi
7:

/*
 * At this point we're running at virtual addresses VM_MIN_KERNEL_ADDRESS and
 * beyond so it's allowed to directly access all locations the kernel was linked
 * against.
 */

/*
 * Invalidate temp mapping
 */
	mr	%r3, %r28
	bl	tlb1_inval_entry

#ifdef __powerpc64__
	/* Set up the TOC pointer */
	b	0f
	.align 3
0:	nop
	bl	1f
	.llong	__tocbase + 0x8000 - .
1:	mflr	%r2
	ld	%r1,0(%r2)
	add	%r2,%r1,%r2
	mtspr	SPR_SPRG8, %r2

	/* Set up the stack pointer */
	ld	%r1,TOC_REF(tmpstack)(%r2)
	addi	%r1,%r1,TMPSTACKSZ-96
#else
/*
 * Setup a temporary stack
 */
	bl	1f
	.long tmpstack-.
1:	mflr	%r1
	lwz	%r2,0(%r1)
	add	%r1,%r1,%r2
	stw	%r1, 0(%r1)
	addi	%r1, %r1, (TMPSTACKSZ - 16)
#endif

/*
 * Initialise exception vector offsets
 */
	bl	CNAME(ivor_setup)
	TOC_RESTORE

	/*
	 * Assign our pcpu instance
	 */
	bl	1f
	.long ap_pcpu-.
1:	mflr	%r4
	lwz	%r3, 0(%r4)
	add	%r3, %r3, %r4
	LOAD	%r3, 0(%r3)
	mtsprg0	%r3

	bl	CNAME(pmap_bootstrap_ap)
	TOC_RESTORE

	bl	CNAME(cpudep_ap_bootstrap)
	TOC_RESTORE
	/* Switch to the idle thread's kstack */
	mr	%r1, %r3
	
	bl	CNAME(machdep_ap_bootstrap)
	TOC_RESTORE

	/* NOT REACHED */
6:	b	6b
#endif /* SMP */

#if defined (BOOKE_E500)
/*
 * Invalidate all entries in the given TLB.
 *
 * r3	TLBSEL
 */
tlb_inval_all:
	rlwinm	%r3, %r3, 3, (1 << 3)	/* TLBSEL */
	ori	%r3, %r3, (1 << 2)	/* INVALL */
	tlbivax	0, %r3
	isync
	msync

	tlbsync
	msync
	blr

/*
 * expects address to look up in r3, returns entry number in r29
 *
 * FIXME: the hidden assumption is we are now running in AS=0, but we should
 * retrieve actual AS from MSR[IS|DS] and put it in MAS6[SAS]
 */
tlb1_find_current:
	mfspr	%r17, SPR_PID0
	slwi	%r17, %r17, MAS6_SPID0_SHIFT
	mtspr	SPR_MAS6, %r17
	isync
	tlbsx	0, %r3
	mfspr	%r17, SPR_MAS0
	rlwinm	%r29, %r17, 16, 26, 31		/* MAS0[ESEL] -> r29 */

	/* Make sure we have IPROT set on the entry */
	mfspr	%r17, SPR_MAS1
	oris	%r17, %r17, MAS1_IPROT@h
	mtspr	SPR_MAS1, %r17
	isync
	tlbwe
	isync
	msync
	blr

/*
 * Invalidates a single entry in TLB1.
 *
 * r3		ESEL
 * r4-r5	scratched
 */
tlb1_inval_entry:
	lis	%r4, MAS0_TLBSEL1@h	/* Select TLB1 */
	rlwimi	%r4, %r3, 16, 10, 15	/* Select our entry */
	mtspr	SPR_MAS0, %r4
	isync
	tlbre
	li	%r5, 0			/* MAS1[V] = 0 */
	mtspr	SPR_MAS1, %r5
	isync
	tlbwe
	isync
	msync
	blr

/*
 * r29		current entry number
 * r28		returned temp entry
 * r3-r5	scratched
 */
tlb1_temp_mapping_as1:
	/* Read our current translation */
	lis	%r3, MAS0_TLBSEL1@h	/* Select TLB1 */
	rlwimi	%r3, %r29, 16, 10, 15	/* Select our current entry */
	mtspr	SPR_MAS0, %r3
	isync
	tlbre

	/*
	 * Prepare and write temp entry
	 *
	 * FIXME this is not robust against overflow i.e. when the current
	 * entry is the last in TLB1
	 */
	lis	%r3, MAS0_TLBSEL1@h	/* Select TLB1 */
	addi	%r28, %r29, 1		/* Use next entry. */
	rlwimi	%r3, %r28, 16, 10, 15	/* Select temp entry */
	mtspr	SPR_MAS0, %r3
	isync
	mfspr	%r5, SPR_MAS1
	li	%r4, 1			/* AS=1 */
	rlwimi	%r5, %r4, 12, 19, 19
	li	%r4, 0			/* Global mapping, TID=0 */
	rlwimi	%r5, %r4, 16, 8, 15
	oris	%r5, %r5, (MAS1_VALID | MAS1_IPROT)@h
	mtspr	SPR_MAS1, %r5
	isync
	mflr	%r3
	bl	zero_mas7
	bl	zero_mas8
	mtlr	%r3
	isync
	tlbwe
	isync
	msync
	blr

/*
 * Loops over TLB1, invalidates all entries skipping the one which currently
 * maps this code.
 *
 * r29		current entry
 * r3-r5	scratched
 */
tlb1_inval_all_but_current:
	mfspr	%r3, SPR_TLB1CFG	/* Get number of entries */
	andi.	%r3, %r3, TLBCFG_NENTRY_MASK@l
	li	%r4, 0			/* Start from Entry 0 */
1:	lis	%r5, MAS0_TLBSEL1@h
	rlwimi	%r5, %r4, 16, 10, 15
	mtspr	SPR_MAS0, %r5
	isync
	tlbre
	mfspr	%r5, SPR_MAS1
	cmpw	%r4, %r29		/* our current entry? */
	beq	2f
	rlwinm	%r5, %r5, 0, 2, 31	/* clear VALID and IPROT bits */
	mtspr	SPR_MAS1, %r5
	isync
	tlbwe
	isync
	msync
2:	addi	%r4, %r4, 1
	cmpw	%r4, %r3		/* Check if this is the last entry */
	bne	1b
	blr

/*
 * MAS7 and MAS8 conditional zeroing.
 */
.globl zero_mas7
zero_mas7:
	mfpvr	%r20
	rlwinm	%r20, %r20, 16, 16, 31
	cmpli	0, 0, %r20, FSL_E500v1
	beq	1f

	li	%r20, 0
	mtspr	SPR_MAS7, %r20
1:
	blr

.globl zero_mas8
zero_mas8:
	mfpvr	%r20
	rlwinm	%r20, %r20, 16, 16, 31
	cmpli	0, 0, %r20, FSL_E500mc
	beq	1f
	cmpli	0, 0, %r20, FSL_E5500
	beq	1f

	blr
1:
	li	%r20, 0
	mtspr	SPR_MAS8, %r20
	blr
#endif

#ifdef SMP
.globl __boot_tlb1
	/*
	 * The __boot_tlb1 table is used to hold BSP TLB1 entries
	 * marked with _TLB_ENTRY_SHARED flag during AP bootstrap.
	 * The BSP fills in the table in tlb_ap_prep() function. Next,
	 * AP loads its contents to TLB1 hardware in pmap_bootstrap_ap().
	 */
__boot_tlb1:
	.space TLB1_MAX_ENTRIES * TLB_ENTRY_SIZE

__boot_page_padding:
	/*
	 * Boot page needs to be exactly 4K, with the last word of this page
	 * acting as the reset vector, so we need to stuff the remainder.
	 * Upon release from holdoff CPU fetches the last word of the boot
	 * page.
	 */
	.space	4092 - (__boot_page_padding - __boot_page)
	b	__boot_page
#endif /* SMP */

/************************************************************************/
/* locore subroutines */
/************************************************************************/

/*
 * Cache disable/enable/inval sequences according
 * to section 2.16 of E500CORE RM.
 */
ENTRY(dcache_inval)
	/* Invalidate d-cache */
	mfspr	%r3, SPR_L1CSR0
	ori	%r3, %r3, (L1CSR0_DCFI | L1CSR0_DCLFR)@l
	msync
	isync
	mtspr	SPR_L1CSR0, %r3
	isync
1:	mfspr	%r3, SPR_L1CSR0
	andi.	%r3, %r3, L1CSR0_DCFI
	bne	1b
	blr

ENTRY(dcache_disable)
	/* Disable d-cache */
	mfspr	%r3, SPR_L1CSR0
	li	%r4, L1CSR0_DCE@l
	not	%r4, %r4
	and	%r3, %r3, %r4
	msync
	isync
	mtspr	SPR_L1CSR0, %r3
	isync
	blr

ENTRY(dcache_enable)
	/* Enable d-cache */
	mfspr	%r3, SPR_L1CSR0
	oris	%r3, %r3, (L1CSR0_DCPE | L1CSR0_DCE)@h
	ori	%r3, %r3, (L1CSR0_DCPE | L1CSR0_DCE)@l
	msync
	isync
	mtspr	SPR_L1CSR0, %r3
	isync
	blr

ENTRY(icache_inval)
	/* Invalidate i-cache */
	mfspr	%r3, SPR_L1CSR1
	ori	%r3, %r3, (L1CSR1_ICFI | L1CSR1_ICLFR)@l
	isync
	mtspr	SPR_L1CSR1, %r3
	isync
1:	mfspr	%r3, SPR_L1CSR1
	andi.	%r3, %r3, L1CSR1_ICFI
	bne	1b
	blr

ENTRY(icache_disable)
	/* Disable i-cache */
	mfspr	%r3, SPR_L1CSR1
	li	%r4, L1CSR1_ICE@l
	not	%r4, %r4
	and	%r3, %r3, %r4
	isync
	mtspr	SPR_L1CSR1, %r3
	isync
	blr

ENTRY(icache_enable)
	/* Enable i-cache */
	mfspr	%r3, SPR_L1CSR1
	oris	%r3, %r3, (L1CSR1_ICPE | L1CSR1_ICE)@h
	ori	%r3, %r3, (L1CSR1_ICPE | L1CSR1_ICE)@l
	isync
	mtspr	SPR_L1CSR1, %r3
	isync
	blr

/*
 * L2 cache disable/enable/inval sequences for E500mc.
 */

ENTRY(l2cache_inval)
	mfspr	%r3, SPR_L2CSR0
	oris	%r3, %r3, (L2CSR0_L2FI | L2CSR0_L2LFC)@h
	ori	%r3, %r3, (L2CSR0_L2FI | L2CSR0_L2LFC)@l
	isync
	mtspr	SPR_L2CSR0, %r3
	isync
1:	mfspr   %r3, SPR_L2CSR0
	andis.	%r3, %r3, L2CSR0_L2FI@h
	bne	1b
	blr

ENTRY(l2cache_enable)
	mfspr	%r3, SPR_L2CSR0
	oris	%r3, %r3, (L2CSR0_L2E | L2CSR0_L2PE)@h
	isync
	mtspr	SPR_L2CSR0, %r3
	isync
	blr

/*
 * Branch predictor setup.
 */
ENTRY(bpred_enable)
	mfspr	%r3, SPR_BUCSR
	ori	%r3, %r3, BUCSR_BBFI
	isync
	mtspr	SPR_BUCSR, %r3
	isync
	ori	%r3, %r3, BUCSR_BPEN
	isync
	mtspr	SPR_BUCSR, %r3
	isync
	blr

/*
 * XXX: This should be moved to a shared AIM/booke asm file, if one ever is
 * created.
 */
ENTRY(get_spr)
	mfspr	%r3, 0
	blr

/************************************************************************/
/* Data section								*/
/************************************************************************/
	.data
	.align 3
GLOBAL(__startkernel)
	ADDR(begin)
GLOBAL(__endkernel)
	ADDR(end)
	.align	4
tmpstack:
	.space	TMPSTACKSZ
tmpstackbound:
	.space 10240	/* XXX: this really should not be necessary */
#ifdef __powerpc64__
TOC_ENTRY(tmpstack)
TOC_ENTRY(bp_kernload)
#endif

/*
 * Compiled KERNBASE locations
 */
	.globl	kernbase
	.set	kernbase, KERNBASE

#include <powerpc/booke/trap_subr.S>
