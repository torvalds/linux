/* $FreeBSD$ */
/* $NetBSD: trap_subr.S,v 1.20 2002/04/22 23:20:08 kleink Exp $	*/

/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * NOTICE: This is not a standalone file.  to use it, #include it in
 * your port's locore.S, like so:
 *
 *	#include <powerpc/aim/trap_subr.S>
 */

/*
 * Save/restore segment registers
 */
#define RESTORE_SRS(pmap,sr)	mtsr    0,sr; \
	lwz	sr,1*4(pmap);	mtsr	1,sr; \
	lwz	sr,2*4(pmap);	mtsr	2,sr; \
	lwz	sr,3*4(pmap);	mtsr	3,sr; \
	lwz	sr,4*4(pmap);	mtsr	4,sr; \
	lwz	sr,5*4(pmap);	mtsr	5,sr; \
	lwz	sr,6*4(pmap);	mtsr	6,sr; \
	lwz	sr,7*4(pmap);	mtsr	7,sr; \
	lwz	sr,8*4(pmap);	mtsr	8,sr; \
	lwz	sr,9*4(pmap);	mtsr	9,sr; \
	lwz	sr,10*4(pmap);	mtsr	10,sr; \
	lwz	sr,11*4(pmap);	mtsr	11,sr; \
	/* Skip segment 12 (USER_SR), which is restored differently */ \
	lwz	sr,13*4(pmap);	mtsr	13,sr; \
	lwz	sr,14*4(pmap);	mtsr	14,sr; \
	lwz	sr,15*4(pmap);	mtsr	15,sr; isync;

/*
 * User SRs are loaded through a pointer to the current pmap.
 */
#define RESTORE_USER_SRS(pmap,sr) \
	GET_CPUINFO(pmap); \
	lwz	pmap,PC_CURPMAP(pmap); \
	lwzu	sr,PM_SR(pmap); \
	RESTORE_SRS(pmap,sr) \
	/* Restore SR 12 */ \
	lwz	sr,12*4(pmap);	mtsr	12,sr

/*
 * Kernel SRs are loaded directly from kernel_pmap_
 */
#define RESTORE_KERN_SRS(pmap,sr) \
	lwz	pmap,TRAP_TOCBASE(0); \
	lwz	pmap,CNAME(kernel_pmap_store)@got(pmap); \
	lwzu	sr,PM_SR(pmap); \
	RESTORE_SRS(pmap,sr)

/*
 * FRAME_SETUP assumes:
 *	SPRG1		SP (1)
 * 	SPRG3		trap type
 *	savearea	r28-r31,DAR,DSISR   (DAR & DSISR only for DSI traps)
 *	r28		LR
 *	r29		CR
 *	r30		scratch
 *	r31		scratch
 *	r1		kernel stack
 *	SRR0/1		as at start of trap
 */
#define	FRAME_SETUP(savearea)						\
/* Have to enable translation to allow access of kernel stack: */	\
	GET_CPUINFO(%r31);						\
	mfsrr0	%r30;							\
	stw	%r30,(savearea+CPUSAVE_SRR0)(%r31);	/* save SRR0 */	\
	mfsrr1	%r30;							\
	stw	%r30,(savearea+CPUSAVE_SRR1)(%r31);	/* save SRR1 */	\
	mfmsr	%r30;							\
	ori	%r30,%r30,(PSL_DR|PSL_IR|PSL_RI)@l; /* relocation on */	\
	mtmsr	%r30;			/* stack can now be accessed */	\
	isync;								\
	mfsprg1	%r31;			/* get saved SP */		\
	stwu	%r31,-FRAMELEN(%r1);	/* save it in the callframe */	\
	stw	%r0, FRAME_0+8(%r1);	/* save r0 in the trapframe */	\
	stw	%r31,FRAME_1+8(%r1);	/* save SP   "      "       */	\
	stw	%r2, FRAME_2+8(%r1);	/* save r2   "      "       */	\
	stw	%r28,FRAME_LR+8(%r1);	/* save LR   "      "       */	\
	stw	%r29,FRAME_CR+8(%r1);	/* save CR   "      "       */	\
	GET_CPUINFO(%r2);						\
	lwz	%r28,(savearea+CPUSAVE_R28)(%r2); /* get saved r28 */	\
	lwz	%r29,(savearea+CPUSAVE_R29)(%r2); /* get saved r29 */	\
	lwz	%r30,(savearea+CPUSAVE_R30)(%r2); /* get saved r30 */	\
	lwz	%r31,(savearea+CPUSAVE_R31)(%r2); /* get saved r31 */	\
	stw	%r3,  FRAME_3+8(%r1);	/* save r3-r31 */		\
	stw	%r4,  FRAME_4+8(%r1);					\
	stw	%r5,  FRAME_5+8(%r1);					\
	stw	%r6,  FRAME_6+8(%r1);					\
	stw	%r7,  FRAME_7+8(%r1);					\
	stw	%r8,  FRAME_8+8(%r1);					\
	stw	%r9,  FRAME_9+8(%r1);					\
	stw	%r10, FRAME_10+8(%r1);					\
	stw	%r11, FRAME_11+8(%r1);					\
	stw	%r12, FRAME_12+8(%r1);					\
	stw	%r13, FRAME_13+8(%r1);					\
	stw	%r14, FRAME_14+8(%r1);					\
	stw	%r15, FRAME_15+8(%r1);					\
	stw	%r16, FRAME_16+8(%r1);					\
	stw	%r17, FRAME_17+8(%r1);					\
	stw	%r18, FRAME_18+8(%r1);					\
	stw	%r19, FRAME_19+8(%r1);					\
	stw	%r20, FRAME_20+8(%r1);					\
	stw	%r21, FRAME_21+8(%r1);					\
	stw	%r22, FRAME_22+8(%r1);					\
	stw	%r23, FRAME_23+8(%r1);					\
	stw	%r24, FRAME_24+8(%r1);					\
	stw	%r25, FRAME_25+8(%r1);					\
	stw	%r26, FRAME_26+8(%r1);					\
	stw	%r27, FRAME_27+8(%r1);					\
	stw	%r28, FRAME_28+8(%r1);					\
	stw	%r29, FRAME_29+8(%r1);					\
	stw	%r30, FRAME_30+8(%r1);					\
	stw	%r31, FRAME_31+8(%r1);					\
	lwz	%r28,(savearea+CPUSAVE_AIM_DAR)(%r2);  /* saved DAR */	\
	lwz	%r29,(savearea+CPUSAVE_AIM_DSISR)(%r2);/* saved DSISR */\
	lwz	%r30,(savearea+CPUSAVE_SRR0)(%r2); /* saved SRR0 */	\
	lwz	%r31,(savearea+CPUSAVE_SRR1)(%r2); /* saved SRR1 */	\
	mfxer	%r3;							\
	mfctr	%r4;							\
	mfsprg3	%r5;							\
	stw	%r3, FRAME_XER+8(1);	/* save xer/ctr/exc */		\
	stw	%r4, FRAME_CTR+8(1);					\
	stw	%r5, FRAME_EXC+8(1);					\
	stw	%r28,FRAME_AIM_DAR+8(1);				\
	stw	%r29,FRAME_AIM_DSISR+8(1); /* save dsisr/srr0/srr1 */	\
	stw	%r30,FRAME_SRR0+8(1);					\
	stw	%r31,FRAME_SRR1+8(1);					\
	lwz	%r2,PC_CURTHREAD(%r2)	/* set curthread pointer */

#define	FRAME_LEAVE(savearea)						\
/* Disable exceptions: */						\
	mfmsr	%r2;							\
	andi.	%r2,%r2,~PSL_EE@l;					\
	mtmsr	%r2;							\
	isync;								\
/* Now restore regs: */							\
	lwz	%r2,FRAME_SRR0+8(%r1);					\
	lwz	%r3,FRAME_SRR1+8(%r1);					\
	lwz	%r4,FRAME_CTR+8(%r1);					\
	lwz	%r5,FRAME_XER+8(%r1);					\
	lwz	%r6,FRAME_LR+8(%r1);					\
	GET_CPUINFO(%r7);						\
	stw	%r2,(savearea+CPUSAVE_SRR0)(%r7); /* save SRR0 */	\
	stw	%r3,(savearea+CPUSAVE_SRR1)(%r7); /* save SRR1 */	\
	lwz	%r7,FRAME_CR+8(%r1);					\
	mtctr	%r4;							\
	mtxer	%r5;							\
	mtlr	%r6;							\
	mtsprg1	%r7;			/* save cr */			\
	lwz	%r31,FRAME_31+8(%r1);   /* restore r0-31 */		\
	lwz	%r30,FRAME_30+8(%r1);					\
	lwz	%r29,FRAME_29+8(%r1);					\
	lwz	%r28,FRAME_28+8(%r1);					\
	lwz	%r27,FRAME_27+8(%r1);					\
	lwz	%r26,FRAME_26+8(%r1);					\
	lwz	%r25,FRAME_25+8(%r1);					\
	lwz	%r24,FRAME_24+8(%r1);					\
	lwz	%r23,FRAME_23+8(%r1);					\
	lwz	%r22,FRAME_22+8(%r1);					\
	lwz	%r21,FRAME_21+8(%r1);					\
	lwz	%r20,FRAME_20+8(%r1);					\
	lwz	%r19,FRAME_19+8(%r1);					\
	lwz	%r18,FRAME_18+8(%r1);					\
	lwz	%r17,FRAME_17+8(%r1);					\
	lwz	%r16,FRAME_16+8(%r1);					\
	lwz	%r15,FRAME_15+8(%r1);					\
	lwz	%r14,FRAME_14+8(%r1);					\
	lwz	%r13,FRAME_13+8(%r1);					\
	lwz	%r12,FRAME_12+8(%r1);					\
	lwz	%r11,FRAME_11+8(%r1);					\
	lwz	%r10,FRAME_10+8(%r1);					\
	lwz	%r9, FRAME_9+8(%r1);					\
	lwz	%r8, FRAME_8+8(%r1);					\
	lwz	%r7, FRAME_7+8(%r1);					\
	lwz	%r6, FRAME_6+8(%r1);					\
	lwz	%r5, FRAME_5+8(%r1);					\
	lwz	%r4, FRAME_4+8(%r1);					\
	lwz	%r3, FRAME_3+8(%r1);					\
	lwz	%r2, FRAME_2+8(%r1);					\
	lwz	%r0, FRAME_0+8(%r1);					\
	lwz	%r1, FRAME_1+8(%r1);					\
/* Can't touch %r1 from here on */					\
	mtsprg2	%r2;			/* save r2 & r3 */		\
	mtsprg3	%r3;							\
/* Disable translation, machine check and recoverability: */		\
	mfmsr	%r2;							\
	andi.	%r2,%r2,~(PSL_DR|PSL_IR|PSL_ME|PSL_RI)@l;	\
	mtmsr	%r2;							\
	isync;								\
/* Decide whether we return to user mode: */				\
	GET_CPUINFO(%r2);						\
	lwz	%r3,(savearea+CPUSAVE_SRR1)(%r2);			\
	mtcr	%r3;							\
	bf	17,1f;			/* branch if PSL_PR is false */	\
/* Restore user SRs */							\
	RESTORE_USER_SRS(%r2,%r3);					\
1:	mfsprg1	%r2;			/* restore cr */		\
	mtcr	%r2;							\
	GET_CPUINFO(%r2);						\
	lwz	%r3,(savearea+CPUSAVE_SRR0)(%r2); /* restore srr0 */	\
	mtsrr0	%r3;							\
	lwz	%r3,(savearea+CPUSAVE_SRR1)(%r2); /* restore srr1 */	\
									\
	/* Make sure HV bit of MSR propagated to SRR1 */		\
	mfmsr	%r2;							\
	or	%r3,%r2,%r3;						\
									\
	mtsrr1	%r3;							\
	mfsprg2	%r2;			/* restore r2 & r3 */		\
	mfsprg3	%r3

#ifdef KDTRACE_HOOKS
	.data
	.globl	dtrace_invop_calltrap_addr
	.align	4
	.type	dtrace_invop_calltrap_addr, @object
        .size	dtrace_invop_calltrap_addr, 4
dtrace_invop_calltrap_addr:
	.word	0
	.word	0

	.text
#endif

/*
 * The next two routines are 64-bit glue code. The first is used to test if
 * we are on a 64-bit system. By copying it to the illegal instruction
 * handler, we can test for 64-bit mode by trying to execute a 64-bit
 * instruction and seeing what happens. The second gets copied in front
 * of all the other handlers to restore 32-bit bridge mode when traps
 * are taken.
 */

/* 64-bit test code. Sets SPRG2 to 0 if an illegal instruction is executed */

	.globl	CNAME(testppc64),CNAME(testppc64size)
CNAME(testppc64):
	mtsprg1 %r31
	mfsrr0  %r31
	addi	%r31, %r31, 4
	mtsrr0  %r31

	li	%r31, 0
	mtsprg2 %r31
	mfsprg1 %r31

	rfi
CNAME(testppc64size) = .-CNAME(testppc64)


/* 64-bit bridge mode restore snippet. Gets copied in front of everything else
 * on 64-bit systems. */

	.globl	CNAME(restorebridge),CNAME(restorebridgesize)
CNAME(restorebridge):
	mtsprg1	%r31
	mfmsr	%r31
	clrldi	%r31,%r31,1
	mtmsrd	%r31
	mfsprg1	%r31
	isync
CNAME(restorebridgesize) = .-CNAME(restorebridge)

/*
 * Processor reset exception handler. These are typically
 * the first instructions the processor executes after a
 * software reset. We do this in two bits so that we are
 * not still hanging around in the trap handling region
 * once the MMU is turned on.
 */
	.globl	CNAME(rstcode), CNAME(rstcodeend)
CNAME(rstcode):
	bl	1f
	.long	cpu_reset
1:	mflr	%r31
	lwz	%r31,0(%r31)
	mtlr	%r31
	blrl
CNAME(rstcodeend):

cpu_reset:
	bl	1f

	.space	124

1:
	mflr	%r1
	addi	%r1,%r1,(124-16)@l

	bl	CNAME(cpudep_ap_early_bootstrap)
	lis	%r3,1@l
	bl	CNAME(pmap_cpu_bootstrap)
	bl	CNAME(cpudep_ap_bootstrap)
	mr	%r1,%r3
	bl	CNAME(cpudep_ap_setup)
	GET_CPUINFO(%r5)
	lwz	%r3,(PC_RESTORE)(%r5)
	cmplwi	%cr0,%r3,0
	beq	%cr0,2f
	li	%r4, 1
	b	CNAME(longjmp)
2:
#ifdef SMP
	bl	CNAME(machdep_ap_bootstrap)
#endif

	/* Should not be reached */
9:
	b	9b

/*
 * This code gets copied to all the trap vectors
 * (except ISI/DSI, ALI, and the interrupts)
 */

	.globl	CNAME(trapcode),CNAME(trapcodeend)
CNAME(trapcode):
	mtsprg1	%r1			/* save SP */
	mflr	%r1			/* Save the old LR in r1 */
	mtsprg2 %r1			/* And then in SPRG2 */
	lwz	%r1, TRAP_GENTRAP(0)	/* Get branch address */
	mtlr	%r1
	li	%r1, 0xe0		/* How to get the vector from LR */
	blrl				/* LR & (0xff00 | r1) is exception # */
CNAME(trapcodeend):

/*
 * For ALI: has to save DSISR and DAR
 */
	.globl	CNAME(alitrap),CNAME(aliend)
CNAME(alitrap):
	mtsprg1	%r1			/* save SP */
	GET_CPUINFO(%r1)
	stw	%r28,(PC_TEMPSAVE+CPUSAVE_R28)(%r1)	/* free r28-r31 */
	stw	%r29,(PC_TEMPSAVE+CPUSAVE_R29)(%r1)
	stw	%r30,(PC_TEMPSAVE+CPUSAVE_R30)(%r1)
	stw	%r31,(PC_TEMPSAVE+CPUSAVE_R31)(%r1)
	mfdar	%r30
	mfdsisr	%r31
	stw	%r30,(PC_TEMPSAVE+CPUSAVE_AIM_DAR)(%r1)
	stw	%r31,(PC_TEMPSAVE+CPUSAVE_AIM_DSISR)(%r1)
	mfsprg1	%r1			/* restore SP, in case of branch */
	mflr	%r28			/* save LR */
	mfcr	%r29			/* save CR */

	/* Put our exception vector in SPRG3 */
	li	%r31, EXC_ALI
	mtsprg3	%r31

	/* Test whether we already had PR set */
	mfsrr1	%r31
	mtcr	%r31

	/* Jump to s_trap */
	bl	1f
	.long	s_trap
1:	mflr	%r31
	lwz	%r31,0(%r31)
	mtlr	%r31
	blrl
CNAME(aliend):

/*
 * G2 specific: instuction TLB miss.
 */
	.globl	CNAME(imisstrap),CNAME(imisssize)
CNAME(imisstrap):
	mfspr %r2, SPR_HASH1		/* get first pointer */
	addi %r1, 0, 8			/* load 8 for counter */
	mfctr %r0			/* save counter */
	mfspr %r3, SPR_ICMP		/* get first compare value */
	addi %r2, %r2, -8		/* pre dec the pointer */
im0:
	mtctr %r1			/* load counter */
im1:
	lwzu %r1, 8(%r2)		/* get next pte */
	cmp 0, 0, %r1, %r3		/* see if found pte */
	bdnzf 2, im1			/* dec count br if cmp ne and if
					 * count not zero */
	bne instr_sec_hash		/* if not found set up second hash
					 * or exit */
	lwz %r1, +4(%r2)		/* load tlb entry lower-word */
	andi. %r3, %r1, 8		/* check G bit */
	bne do_isi_prot			/* if guarded, take an ISI */
	mtctr %r0			/* restore counter */
	mfspr %r0, SPR_IMISS		/* get the miss address for the tlbli */
	mfspr %r3, SPR_SRR1		/* get the saved cr0 bits */
	mtcrf 0x80, %r3			/* restore CR0 */
	mtspr SPR_RPA, %r1		/* set the pte */
	ori %r1, %r1, 0x100		/* set reference bit */
	srwi %r1, %r1, 8		/* get byte 7 of pte */
	tlbli %r0 			/* load the itlb */
	stb %r1, +6(%r2)		/* update page table */
	rfi				/* return to executing program */

instr_sec_hash:
	andi. %r1, %r3, 0x0040		/* see if we have done second hash */
	bne do_isi			/* if so, go to ISI interrupt */
	mfspr %r2, SPR_HASH2		/* get the second pointer */
	ori %r3, %r3, 0x0040		/* change the compare value */
	addi %r1, %r0, 8		/* load 8 for counter */
	addi %r2, %r2, -8		/* pre dec for update on load */
	b im0				/* try second hash */

/* Create a faked ISI interrupt as the address was not found */ 
do_isi_prot:
	mfspr %r3, SPR_SRR1		/* get srr1 */
	andi. %r2, %r3, 0xffff		/* clean upper srr1 */
	addis %r2, %r2, 0x0800		/* or in srr<4> = 1 to flag prot
					 * violation */
	b isi1
do_isi:
	mfspr %r3, SPR_SRR1		/* get srr1 */
	andi. %r2, %r3, 0xffff		/* clean srr1 */
	addis %r2, %r2, 0x4000		/* or in srr1<1> = 1 to flag pte
					 * not found */
isi1: 
	mtctr %r0			/* restore counter */
	mtspr SPR_SRR1, %r2		/* set srr1 */
	mfmsr %r0			/* get msr */
	xoris %r0, %r0, 0x2		/* flip the msr<tgpr> bit */
	mtcrf 0x80, %r3			/* restore CR0 */
	mtmsr %r0			/* flip back to the native gprs */
	ba EXC_ISI 			/* go to instr. access interrupt */

CNAME(imisssize) = .-CNAME(imisstrap)

/*
 * G2 specific: data load TLB miss.
 */
	.globl	CNAME(dlmisstrap),CNAME(dlmisssize)
CNAME(dlmisstrap):
	mfspr %r2, SPR_HASH1		/* get first pointer */
	addi %r1, 0, 8			/* load 8 for counter */
	mfctr %r0			/* save counter */
	mfspr %r3, SPR_DCMP		/* get first compare value */
	addi %r2, %r2, -8		/* pre dec the pointer */
dm0: 
	mtctr %r1			/* load counter */
dm1:	
	lwzu %r1, 8(%r2)		/* get next pte */
	cmp 0, 0, %r1, %r3		/* see if found pte */
	bdnzf 2, dm1			/* dec count br if cmp ne and if
					 * count not zero */
	bne data_sec_hash		/* if not found set up second hash
					 * or exit */
	lwz %r1, +4(%r2)		/* load tlb entry lower-word */
	mtctr %r0			/* restore counter */
	mfspr %r0, SPR_DMISS		/* get the miss address for the tlbld */
	mfspr %r3, SPR_SRR1		/* get the saved cr0 bits */
	mtcrf 0x80, %r3			/* restore CR0 */
	mtspr SPR_RPA, %r1		/* set the pte */
	ori %r1, %r1, 0x100		/* set reference bit */
	srwi %r1, %r1, 8		/* get byte 7 of pte */
	tlbld %r0			/* load the dtlb */
	stb %r1, +6(%r2)		/* update page table */
	rfi				/* return to executing program */
 
data_sec_hash:
	andi. %r1, %r3, 0x0040		/* see if we have done second hash */
	bne do_dsi			/* if so, go to DSI interrupt */
	mfspr %r2, SPR_HASH2		/* get the second pointer */
	ori %r3, %r3, 0x0040		/* change the compare value */
	addi %r1, 0, 8			/* load 8 for counter */
	addi %r2, %r2, -8		/* pre dec for update on load */
	b dm0				/* try second hash */

CNAME(dlmisssize) = .-CNAME(dlmisstrap)

/*
 *  G2 specific: data store TLB miss.
 */
	.globl	CNAME(dsmisstrap),CNAME(dsmisssize)
CNAME(dsmisstrap):
	mfspr %r2, SPR_HASH1		/* get first pointer */
	addi %r1, 0, 8			/* load 8 for counter */
	mfctr %r0			/* save counter */
	mfspr %r3, SPR_DCMP		/* get first compare value */
	addi %r2, %r2, -8		/* pre dec the pointer */
ds0:
	mtctr %r1			/* load counter */
ds1:
	lwzu %r1, 8(%r2)		/* get next pte */
	cmp 0, 0, %r1, %r3		/* see if found pte */
	bdnzf 2, ds1			/* dec count br if cmp ne and if
					 * count not zero */
	bne data_store_sec_hash		/* if not found set up second hash
					 * or exit */
	lwz %r1, +4(%r2)		/* load tlb entry lower-word */
	andi. %r3, %r1, 0x80		/* check the C-bit */
	beq data_store_chk_prot		/* if (C==0)
					 *     go check protection modes */
ds2:
	mtctr %r0			/* restore counter */
	mfspr %r0, SPR_DMISS		/* get the miss address for the tlbld */
	mfspr %r3, SPR_SRR1		/* get the saved cr0 bits */
	mtcrf 0x80, %r3			/* restore CR0 */
	mtspr SPR_RPA, %r1		/* set the pte */
	tlbld %r0			/* load the dtlb */
	rfi				/* return to executing program */

data_store_sec_hash:
	andi. %r1, %r3, 0x0040		/* see if we have done second hash */
	bne do_dsi			/* if so, go to DSI interrupt */
	mfspr %r2, SPR_HASH2		/* get the second pointer */
	ori %r3, %r3, 0x0040		/* change the compare value */
	addi %r1, 0, 8			/* load 8 for counter */
	addi %r2, %r2, -8		/* pre dec for update on load */
	b ds0				/* try second hash */
	
/* Check the protection before setting PTE(c-bit) */
data_store_chk_prot:
	rlwinm. %r3,%r1,30,0,1		/* test PP */
	bge- chk0			/* if (PP == 00 or PP == 01)
					 *     goto chk0: */
	andi. %r3, %r1, 1		/* test PP[0] */
	beq+ chk2			/* return if PP[0] == 0 */
	b do_dsi_prot			/* else DSIp */
chk0:
	mfspr %r3,SPR_SRR1		/* get old msr */
	andis. %r3,%r3,0x0008		/* test the KEY bit (SRR1-bit 12) */
	beq chk2			/* if (KEY==0) goto chk2: */
	b do_dsi_prot			/* else do_dsi_prot */
chk2:
	ori %r1, %r1, 0x180		/* set reference and change bit */
	sth %r1, 6(%r2)			/* update page table */
	b ds2				/* and back we go */
	
/* Create a faked DSI interrupt as the address was not found */ 
do_dsi:
	mfspr %r3, SPR_SRR1		/* get srr1 */
	rlwinm %r1,%r3,9,6,6		/* get srr1<flag> to bit 6 for
					 * load/store, zero rest */
	addis %r1, %r1, 0x4000		/* or in dsisr<1> = 1 to flag pte
					 * not found */
	b dsi1

do_dsi_prot:
	mfspr %r3, SPR_SRR1		/* get srr1 */
	rlwinm %r1,%r3,9,6,6		/* get srr1<flag> to bit 6 for
					   *load/store, zero rest */
	addis %r1, %r1, 0x0800		/* or in dsisr<4> = 1 to flag prot
					 * violation */

dsi1:
	mtctr %r0			/* restore counter */
	andi. %r2, %r3, 0xffff		/* clear upper bits of srr1 */
	mtspr SPR_SRR1, %r2		/* set srr1 */
	mtspr SPR_DSISR, %r1		/* load the dsisr */
	mfspr %r1, SPR_DMISS		/* get miss address */
	rlwinm. %r2,%r2,0,31,31		/* test LE bit */
	beq dsi2			/* if little endian then: */
	xor %r1, %r1, 0x07		/* de-mung the data address */
dsi2:
	mtspr SPR_DAR, %r1		/* put in dar */
	mfmsr %r0			/* get msr */
	xoris %r0, %r0, 0x2		/* flip the msr<tgpr> bit */
	mtcrf 0x80, %r3			/* restore CR0 */
	mtmsr %r0			/* flip back to the native gprs */
	ba EXC_DSI			/* branch to DSI interrupt */

CNAME(dsmisssize) = .-CNAME(dsmisstrap)

/*
 * Similar to the above for DSI
 * Has to handle BAT spills
 * and standard pagetable spills
 */
	.globl	CNAME(dsitrap),CNAME(dsiend)
CNAME(dsitrap):
	mtsprg1	%r1			/* save SP */
	GET_CPUINFO(%r1)
	stw	%r28,(PC_DISISAVE+CPUSAVE_R28)(%r1)	/* free r28-r31 */
	stw	%r29,(PC_DISISAVE+CPUSAVE_R29)(%r1)
	stw	%r30,(PC_DISISAVE+CPUSAVE_R30)(%r1)
	stw	%r31,(PC_DISISAVE+CPUSAVE_R31)(%r1)
	mfsprg1	%r1			/* restore SP */
	mfcr	%r29			/* save CR */
	mfxer	%r30			/* save XER */
	mtsprg2	%r30			/* in SPRG2 */
	mfsrr1	%r31			/* test kernel mode */
	mtcr	%r31
	bt	17,1f			/* branch if PSL_PR is set */
	mfdar	%r31			/* get fault address */
	rlwinm	%r31,%r31,7,25,28	/* get segment * 8 */

	/* get batu */
	lwz	%r30,TRAP_TOCBASE(0)
	lwz	%r30,CNAME(battable)@got(%r30)
	add	%r31,%r30,%r31
	lwz	%r30,0(%r31)
	mtcr	%r30
	bf	30,1f			/* branch if supervisor valid is
					   false */
	/* get batl */
	lwz	%r31,4(%r31)
/* We randomly use the highest two bat registers here */
	mftb	%r28
	andi.	%r28,%r28,1
	bne	2f
	mtdbatu	2,%r30
	mtdbatl	2,%r31
	b	3f
2:
	mtdbatu	3,%r30
	mtdbatl	3,%r31
3:
	mfsprg2	%r30			/* restore XER */
	mtxer	%r30
	mtcr	%r29			/* restore CR */
	mtsprg1	%r1
	GET_CPUINFO(%r1)
	lwz	%r28,(PC_DISISAVE+CPUSAVE_R28)(%r1)	/* restore r28-r31 */
	lwz	%r29,(PC_DISISAVE+CPUSAVE_R29)(%r1)
	lwz	%r30,(PC_DISISAVE+CPUSAVE_R30)(%r1)
	lwz	%r31,(PC_DISISAVE+CPUSAVE_R31)(%r1)
	mfsprg1	%r1
	rfi				/* return to trapped code */
1:
	mflr	%r28			/* save LR (SP already saved) */

	/* Jump to disitrap */
	bl	4f
	.long	disitrap
4:	mflr	%r1
	lwz	%r1,0(%r1)
	mtlr	%r1
	blrl
CNAME(dsiend):

/*
 * Preamble code for DSI/ISI traps
 */
disitrap:
	/* Write the trap vector to SPRG3 by computing LR & 0xff00 */
	mflr	%r1
	andi.	%r1,%r1,0xff00
	mtsprg3	%r1
	
	GET_CPUINFO(%r1)
	lwz	%r30,(PC_DISISAVE+CPUSAVE_R28)(%r1)
	stw	%r30,(PC_TEMPSAVE+CPUSAVE_R28)(%r1)
	lwz	%r31,(PC_DISISAVE+CPUSAVE_R29)(%r1)
	stw	%r31,(PC_TEMPSAVE+CPUSAVE_R29)(%r1)
	lwz	%r30,(PC_DISISAVE+CPUSAVE_R30)(%r1)
	stw	%r30,(PC_TEMPSAVE+CPUSAVE_R30)(%r1)
	lwz	%r31,(PC_DISISAVE+CPUSAVE_R31)(%r1)
	stw	%r31,(PC_TEMPSAVE+CPUSAVE_R31)(%r1)
	mfdar	%r30
	mfdsisr	%r31
	stw	%r30,(PC_TEMPSAVE+CPUSAVE_AIM_DAR)(%r1)
	stw	%r31,(PC_TEMPSAVE+CPUSAVE_AIM_DSISR)(%r1)

#ifdef KDB
	/* Try to detect a kernel stack overflow */
	mfsrr1	%r31
	mtcr	%r31
	bt	17,realtrap		/* branch is user mode */
	mfsprg1	%r31			/* get old SP */
	clrrwi	%r31,%r31,12		/* Round SP down to nearest page */
	sub.	%r30,%r31,%r30		/* SP - DAR */
	bge	1f
	neg	%r30,%r30		/* modulo value */
1:	cmplwi	%cr0,%r30,4096		/* is DAR within a page of SP? */
	bge	%cr0,realtrap		/* no, too far away. */

	/* Now convert this DSI into a DDB trap.  */
	GET_CPUINFO(%r1)
	lwz	%r30,(PC_TEMPSAVE+CPUSAVE_AIM_DAR)(%r1) /* get DAR */
	stw	%r30,(PC_DBSAVE  +CPUSAVE_AIM_DAR)(%r1) /* save DAR */
	lwz	%r31,(PC_TEMPSAVE+CPUSAVE_AIM_DSISR)(%r1) /* get DSISR */
	stw	%r31,(PC_DBSAVE  +CPUSAVE_AIM_DSISR)(%r1) /* save DSISR */
	lwz	%r30,(PC_DISISAVE+CPUSAVE_R28)(%r1) /* get  r28 */
	stw	%r30,(PC_DBSAVE  +CPUSAVE_R28)(%r1) /* save r28 */
	lwz	%r31,(PC_DISISAVE+CPUSAVE_R29)(%r1) /* get  r29 */
	stw	%r31,(PC_DBSAVE  +CPUSAVE_R29)(%r1) /* save r29 */
	lwz	%r30,(PC_DISISAVE+CPUSAVE_R30)(%r1) /* get  r30 */
	stw	%r30,(PC_DBSAVE  +CPUSAVE_R30)(%r1) /* save r30 */
	lwz	%r31,(PC_DISISAVE+CPUSAVE_R31)(%r1) /* get  r31 */
	stw	%r31,(PC_DBSAVE  +CPUSAVE_R31)(%r1) /* save r31 */
	b	dbtrap
#endif

	/* XXX need stack probe here */
realtrap:
/* Test whether we already had PR set */
	mfsrr1	%r1
	mtcr	%r1
	mfsprg1	%r1			/* restore SP (might have been
					   overwritten) */
	bf	17,k_trap		/* branch if PSL_PR is false */
	GET_CPUINFO(%r1)
	lwz	%r1,PC_CURPCB(%r1)
	RESTORE_KERN_SRS(%r30,%r31)	/* enable kernel mapping */
	b	s_trap

/*
 * generictrap does some standard setup for trap handling to minimize
 * the code that need be installed in the actual vectors. It expects
 * the following conditions.
 * 
 * R1 - Trap vector = LR & (0xff00 | R1)
 * SPRG1 - Original R1 contents
 * SPRG2 - Original LR
 */

	.globl	CNAME(generictrap64)
generictrap64:
	mtsprg3	%r31
	mfmsr	%r31
	clrldi	%r31,%r31,1
	mtmsrd	%r31
	mfsprg3	%r31
	isync

	.globl	CNAME(generictrap)
generictrap:
	/* Save R1 for computing the exception vector */
	mtsprg3 %r1

	/* Save interesting registers */
	GET_CPUINFO(%r1)
	stw	%r28,(PC_TEMPSAVE+CPUSAVE_R28)(%r1)	/* free r28-r31 */
	stw	%r29,(PC_TEMPSAVE+CPUSAVE_R29)(%r1)
	stw	%r30,(PC_TEMPSAVE+CPUSAVE_R30)(%r1)
	stw	%r31,(PC_TEMPSAVE+CPUSAVE_R31)(%r1)
	mfsprg1	%r1			/* restore SP, in case of branch */
	mfsprg2	%r28			/* save LR */
	mfcr	%r29			/* save CR */

	/* Compute the exception vector from the link register */
	mfsprg3 %r31
	ori	%r31,%r31,0xff00
	mflr	%r30
	and	%r30,%r30,%r31
	mtsprg3	%r30

	/* Test whether we already had PR set */
	mfsrr1	%r31
	mtcr	%r31

s_trap:
	bf	17,k_trap		/* branch if PSL_PR is false */
	GET_CPUINFO(%r1)
u_trap:
	lwz	%r1,PC_CURPCB(%r1)
	RESTORE_KERN_SRS(%r30,%r31)	/* enable kernel mapping */

/*
 * Now the common trap catching code.
 */
k_trap:
	FRAME_SETUP(PC_TEMPSAVE)
	/* Restore USER_SR */
	GET_CPUINFO(%r30)
	lwz	%r30,PC_CURPCB(%r30)
	lwz	%r30,PCB_AIM_USR_VSID(%r30)
	mtsr	USER_SR,%r30; sync; isync
/* Call C interrupt dispatcher: */
trapagain:
	addi	%r3,%r1,8
	bl	CNAME(powerpc_interrupt)
	.globl	CNAME(trapexit)		/* backtrace code sentinel */
CNAME(trapexit):

/* Disable interrupts: */
	mfmsr	%r3
	andi.	%r3,%r3,~PSL_EE@l
	mtmsr	%r3
/* Test AST pending: */
	lwz	%r5,FRAME_SRR1+8(%r1)
	mtcr	%r5
	bf	17,1f			/* branch if PSL_PR is false */

	GET_CPUINFO(%r3)		/* get per-CPU pointer */
	lwz	%r4, TD_FLAGS(%r2)	/* get thread flags value
					 * (r2 is curthread) */
	lis	%r5, (TDF_ASTPENDING|TDF_NEEDRESCHED)@h
	ori	%r5,%r5, (TDF_ASTPENDING|TDF_NEEDRESCHED)@l
	and.	%r4,%r4,%r5
	beq	1f
	mfmsr	%r3			/* re-enable interrupts */
	ori	%r3,%r3,PSL_EE@l
	mtmsr	%r3
	isync
	addi	%r3,%r1,8
	bl	CNAME(ast)
	.globl	CNAME(asttrapexit)	/* backtrace code sentinel #2 */
CNAME(asttrapexit):
	b	trapexit		/* test ast ret value ? */
1:
	FRAME_LEAVE(PC_TEMPSAVE)

	.globl	CNAME(rfi_patch1)	/* replace rfi with rfid on ppc64 */
CNAME(rfi_patch1):
	rfi

	.globl	CNAME(rfid_patch)
CNAME(rfid_patch):
	rfid

#if defined(KDB)
/*
 * Deliberate entry to dbtrap
 */
	.globl	CNAME(breakpoint)
CNAME(breakpoint):
	mtsprg1	%r1
	mfmsr	%r3
	mtsrr1	%r3
	andi.	%r3,%r3,~(PSL_EE|PSL_ME)@l
	mtmsr	%r3			/* disable interrupts */
	isync
	GET_CPUINFO(%r3)
	stw	%r28,(PC_DBSAVE+CPUSAVE_R28)(%r3)
	stw	%r29,(PC_DBSAVE+CPUSAVE_R29)(%r3)
	stw	%r30,(PC_DBSAVE+CPUSAVE_R30)(%r3)
	stw	%r31,(PC_DBSAVE+CPUSAVE_R31)(%r3)
	mflr	%r28
	li	%r29,EXC_BPT
	mtlr	%r29
	mfcr	%r29
	mtsrr0	%r28

/*
 * Now the kdb trap catching code.
 */
dbtrap:
	/* Write the trap vector to SPRG3 by computing LR & 0xff00 */
	mflr	%r1
	andi.	%r1,%r1,0xff00
	mtsprg3	%r1

	lwz	%r1,TRAP_TOCBASE(0)		/* get new SP */
	lwz	%r1,trapstk@got(%r1)
	addi	%r1,%r1,TRAPSTKSZ-16

	FRAME_SETUP(PC_DBSAVE)
/* Call C trap code: */
	addi	%r3,%r1,8
	bl	CNAME(db_trap_glue)
	or.	%r3,%r3,%r3
	bne	dbleave
/* This wasn't for KDB, so switch to real trap: */
	lwz	%r3,FRAME_EXC+8(%r1)	/* save exception */
	GET_CPUINFO(%r4)
	stw	%r3,(PC_DBSAVE+CPUSAVE_R31)(%r4)
	FRAME_LEAVE(PC_DBSAVE)
	mtsprg1	%r1			/* prepare for entrance to realtrap */
	GET_CPUINFO(%r1)
	stw	%r28,(PC_TEMPSAVE+CPUSAVE_R28)(%r1)
	stw	%r29,(PC_TEMPSAVE+CPUSAVE_R29)(%r1)
	stw	%r30,(PC_TEMPSAVE+CPUSAVE_R30)(%r1)
	stw	%r31,(PC_TEMPSAVE+CPUSAVE_R31)(%r1)
	mflr	%r28
	mfcr	%r29
	lwz	%r31,(PC_DBSAVE+CPUSAVE_R31)(%r1)
	mtsprg3	%r31			/* SPRG3 was clobbered by FRAME_LEAVE */
	mfsprg1	%r1
	b	realtrap
dbleave:
	FRAME_LEAVE(PC_DBSAVE)
	.globl	CNAME(rfi_patch2)	/* replace rfi with rfid on ppc64 */
CNAME(rfi_patch2):
	rfi

/*
 * In case of KDB we want a separate trap catcher for it
 */
	.globl	CNAME(dblow),CNAME(dbend)
CNAME(dblow):
	mtsprg1	%r1			/* save SP */
	mtsprg2	%r29			/* save r29 */
	mfcr	%r29			/* save CR in r29 */
	mfsrr1	%r1
	mtcr	%r1
	bf	17,1f			/* branch if privileged */
	/* Unprivileged case */
	mtcr	%r29			/* put the condition register back */
        mfsprg2	%r29			/* ... and r29 */
        mflr	%r1			/* save LR */
	mtsprg2 %r1			/* And then in SPRG2 */

	lwz	%r1, TRAP_GENTRAP(0)	/* Get branch address */
	mtlr	%r1
	li	%r1, 0			/* How to get the vector from LR */
	blrl				/* LR & (0xff00 | r1) is exception # */
1:
	/* Privileged, so drop to KDB */
	GET_CPUINFO(%r1)
	stw	%r28,(PC_DBSAVE+CPUSAVE_R28)(%r1)	/* free r28 */
        mfsprg2	%r28				/* r29 holds cr...  */
        stw	%r28,(PC_DBSAVE+CPUSAVE_R29)(%r1)	/* free r29 */
        stw	%r30,(PC_DBSAVE+CPUSAVE_R30)(%r1)	/* free r30 */
        stw	%r31,(PC_DBSAVE+CPUSAVE_R31)(%r1)	/* free r31 */
        mflr	%r28					/* save LR */

	/* Jump to dbtrap */
	bl	2f
	.long	dbtrap
2:	mflr	%r1
	lwz	%r1,0(%r1)
	mtlr	%r1
	blrl
CNAME(dbend):
#endif /* KDB */
