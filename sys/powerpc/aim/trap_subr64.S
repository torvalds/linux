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

/* Locate the per-CPU data structure */
#define GET_CPUINFO(r)  \
        mfsprg0  r
#define GET_TOCBASE(r)  \
	lis	r,DMAP_BASE_ADDRESS@highesta;	/* To real-mode alias/dmap */ \
	sldi	r,r,32;							\
	ori	r,r,TRAP_TOCBASE;	/* Magic address for TOC */	\
	ld	r,0(r)

/*
 * Restore SRs for a pmap
 *
 * Requires that r28-r31 be scratch, with r28 initialized to the SLB cache
 */

/*
 * User SRs are loaded through a pointer to the current pmap.
 */
restore_usersrs:
	GET_CPUINFO(%r28)
	ld	%r28,PC_USERSLB(%r28)
	cmpdi	%r28, 0			/* If user SLB pointer NULL, exit */
	beqlr

	li	%r29, 0			/* Set the counter to zero */

	slbia
	slbmfee	%r31,%r29		
	clrrdi	%r31,%r31,28
	slbie	%r31
1:	ld	%r31, 0(%r28)		/* Load SLB entry pointer */
	cmpdi	%r31, 0			/* If NULL, stop */
	beqlr

	ld	%r30, 0(%r31)		/* Load SLBV */
	ld	%r31, 8(%r31)		/* Load SLBE */
	or	%r31, %r31, %r29	/*  Set SLBE slot */
	slbmte	%r30, %r31		/* Install SLB entry */

	addi	%r28, %r28, 8		/* Advance pointer */
	addi	%r29, %r29, 1
	b	1b			/* Repeat */

/*
 * Kernel SRs are loaded directly from the PCPU fields
 */
restore_kernsrs:
	GET_CPUINFO(%r28)
	addi	%r28,%r28,PC_KERNSLB
	ld	%r29,16(%r28)		/* One past USER_SLB_SLOT */
	cmpdi	%r29,0
	beqlr				/* If first kernel entry is invalid,
					 * SLBs not in use, so exit early */

	/* Otherwise, set up SLBs */
	li	%r29, 0			/* Set the counter to zero */

	slbia
	slbmfee	%r31,%r29		
	clrrdi	%r31,%r31,28
	slbie	%r31
1:	cmpdi	%r29, USER_SLB_SLOT	/* Skip the user slot */
	beq-	2f

	ld	%r31, 8(%r28)		/* Load SLBE */
	cmpdi	%r31, 0			/* If SLBE is not valid, stop */
	beqlr
	ld	%r30, 0(%r28)		/* Load SLBV  */
	slbmte	%r30, %r31		/* Install SLB entry */

2:	addi	%r28, %r28, 16		/* Advance pointer */
	addi	%r29, %r29, 1
	cmpdi	%r29, 64		/* Repeat if we are not at the end */
	blt	1b 
	blr

/*
 * FRAME_SETUP assumes:
 *	SPRG1		SP (1)
 * 	SPRG3		trap type
 *	savearea	r27-r31,DAR,DSISR   (DAR & DSISR only for DSI traps)
 *	r28		LR
 *	r29		CR
 *	r30		scratch
 *	r31		scratch
 *	r1		kernel stack
 *	SRR0/1		as at start of trap
 *
 * NOTE: SPRG1 is never used while the MMU is on, making it safe to reuse
 * in any real-mode fault handler, including those handling double faults.
 */
#define	FRAME_SETUP(savearea)						\
/* Have to enable translation to allow access of kernel stack: */	\
	GET_CPUINFO(%r31);						\
	mfsrr0	%r30;							\
	std	%r30,(savearea+CPUSAVE_SRR0)(%r31);	/* save SRR0 */	\
	mfsrr1	%r30;							\
	std	%r30,(savearea+CPUSAVE_SRR1)(%r31);	/* save SRR1 */	\
	mfsprg1	%r31;			/* get saved SP (clears SPRG1) */ \
	mfmsr	%r30;							\
	ori	%r30,%r30,(PSL_DR|PSL_IR|PSL_RI)@l; /* relocation on */	\
	mtmsr	%r30;			/* stack can now be accessed */	\
	isync;								\
	stdu	%r31,-(FRAMELEN+288)(%r1); /* save it in the callframe */ \
	std	%r0, FRAME_0+48(%r1);	/* save r0 in the trapframe */	\
	std	%r31,FRAME_1+48(%r1);	/* save SP   "      "       */	\
	std	%r2, FRAME_2+48(%r1);	/* save r2   "      "       */	\
	std	%r28,FRAME_LR+48(%r1);	/* save LR   "      "       */	\
	std	%r29,FRAME_CR+48(%r1);	/* save CR   "      "       */	\
	GET_CPUINFO(%r2);						\
	ld	%r27,(savearea+CPUSAVE_R27)(%r2); /* get saved r27 */	\
	ld	%r28,(savearea+CPUSAVE_R28)(%r2); /* get saved r28 */	\
	ld	%r29,(savearea+CPUSAVE_R29)(%r2); /* get saved r29 */	\
	ld	%r30,(savearea+CPUSAVE_R30)(%r2); /* get saved r30 */	\
	ld	%r31,(savearea+CPUSAVE_R31)(%r2); /* get saved r31 */	\
	std	%r3,  FRAME_3+48(%r1);	/* save r3-r31 */		\
	std	%r4,  FRAME_4+48(%r1);					\
	std	%r5,  FRAME_5+48(%r1);					\
	std	%r6,  FRAME_6+48(%r1);					\
	std	%r7,  FRAME_7+48(%r1);					\
	std	%r8,  FRAME_8+48(%r1);					\
	std	%r9,  FRAME_9+48(%r1);					\
	std	%r10, FRAME_10+48(%r1);					\
	std	%r11, FRAME_11+48(%r1);					\
	std	%r12, FRAME_12+48(%r1);					\
	std	%r13, FRAME_13+48(%r1);					\
	std	%r14, FRAME_14+48(%r1);					\
	std	%r15, FRAME_15+48(%r1);					\
	std	%r16, FRAME_16+48(%r1);					\
	std	%r17, FRAME_17+48(%r1);					\
	std	%r18, FRAME_18+48(%r1);					\
	std	%r19, FRAME_19+48(%r1);					\
	std	%r20, FRAME_20+48(%r1);					\
	std	%r21, FRAME_21+48(%r1);					\
	std	%r22, FRAME_22+48(%r1);					\
	std	%r23, FRAME_23+48(%r1);					\
	std	%r24, FRAME_24+48(%r1);					\
	std	%r25, FRAME_25+48(%r1);					\
	std	%r26, FRAME_26+48(%r1);					\
	std	%r27, FRAME_27+48(%r1);					\
	std	%r28, FRAME_28+48(%r1);					\
	std	%r29, FRAME_29+48(%r1);					\
	std	%r30, FRAME_30+48(%r1);					\
	std	%r31, FRAME_31+48(%r1);					\
	ld	%r28,(savearea+CPUSAVE_AIM_DAR)(%r2);  /* saved DAR */	\
	ld	%r29,(savearea+CPUSAVE_AIM_DSISR)(%r2);/* saved DSISR */\
	ld	%r30,(savearea+CPUSAVE_SRR0)(%r2); /* saved SRR0 */	\
	ld	%r31,(savearea+CPUSAVE_SRR1)(%r2); /* saved SRR1 */	\
	mfxer	%r3;							\
	mfctr	%r4;							\
	mfsprg3	%r5;							\
	std	%r3, FRAME_XER+48(1);	/* save xer/ctr/exc */		\
	std	%r4, FRAME_CTR+48(1);					\
	std	%r5, FRAME_EXC+48(1);					\
	std	%r28,FRAME_AIM_DAR+48(1);				\
	std	%r29,FRAME_AIM_DSISR+48(1); /* save dsisr/srr0/srr1 */	\
	std	%r30,FRAME_SRR0+48(1);					\
	std	%r31,FRAME_SRR1+48(1);					\
	ld	%r13,PC_CURTHREAD(%r2)	/* set kernel curthread */

#define	FRAME_LEAVE(savearea)						\
/* Disable exceptions: */						\
	mfmsr	%r2;							\
	andi.	%r2,%r2,~PSL_EE@l;					\
	mtmsr	%r2;							\
	isync;								\
/* Now restore regs: */							\
	ld	%r2,FRAME_SRR0+48(%r1);					\
	ld	%r3,FRAME_SRR1+48(%r1);					\
	ld	%r4,FRAME_CTR+48(%r1);					\
	ld	%r5,FRAME_XER+48(%r1);					\
	ld	%r6,FRAME_LR+48(%r1);					\
	GET_CPUINFO(%r7);						\
	std	%r2,(savearea+CPUSAVE_SRR0)(%r7); /* save SRR0 */	\
	std	%r3,(savearea+CPUSAVE_SRR1)(%r7); /* save SRR1 */	\
	ld	%r7,FRAME_CR+48(%r1);					\
	mtctr	%r4;							\
	mtxer	%r5;							\
	mtlr	%r6;							\
	mtsprg2	%r7;			/* save cr */			\
	ld	%r31,FRAME_31+48(%r1);   /* restore r0-31 */		\
	ld	%r30,FRAME_30+48(%r1);					\
	ld	%r29,FRAME_29+48(%r1);					\
	ld	%r28,FRAME_28+48(%r1);					\
	ld	%r27,FRAME_27+48(%r1);					\
	ld	%r26,FRAME_26+48(%r1);					\
	ld	%r25,FRAME_25+48(%r1);					\
	ld	%r24,FRAME_24+48(%r1);					\
	ld	%r23,FRAME_23+48(%r1);					\
	ld	%r22,FRAME_22+48(%r1);					\
	ld	%r21,FRAME_21+48(%r1);					\
	ld	%r20,FRAME_20+48(%r1);					\
	ld	%r19,FRAME_19+48(%r1);					\
	ld	%r18,FRAME_18+48(%r1);					\
	ld	%r17,FRAME_17+48(%r1);					\
	ld	%r16,FRAME_16+48(%r1);					\
	ld	%r15,FRAME_15+48(%r1);					\
	ld	%r14,FRAME_14+48(%r1);					\
	ld	%r13,FRAME_13+48(%r1);					\
	ld	%r12,FRAME_12+48(%r1);					\
	ld	%r11,FRAME_11+48(%r1);					\
	ld	%r10,FRAME_10+48(%r1);					\
	ld	%r9, FRAME_9+48(%r1);					\
	ld	%r8, FRAME_8+48(%r1);					\
	ld	%r7, FRAME_7+48(%r1);					\
	ld	%r6, FRAME_6+48(%r1);					\
	ld	%r5, FRAME_5+48(%r1);					\
	ld	%r4, FRAME_4+48(%r1);					\
	ld	%r3, FRAME_3+48(%r1);					\
	ld	%r2, FRAME_2+48(%r1);					\
	ld	%r0, FRAME_0+48(%r1);					\
	ld	%r1, FRAME_1+48(%r1);					\
/* Can't touch %r1 from here on */					\
	mtsprg3	%r3;			/* save r3 */			\
/* Disable translation, machine check and recoverability: */		\
	mfmsr	%r3;							\
	andi.	%r3,%r3,~(PSL_DR|PSL_IR|PSL_ME|PSL_RI)@l;		\
	mtmsr	%r3;							\
	isync;								\
/* Decide whether we return to user mode: */				\
	GET_CPUINFO(%r3);						\
	ld	%r3,(savearea+CPUSAVE_SRR1)(%r3);			\
	mtcr	%r3;							\
	bf	17,1f;			/* branch if PSL_PR is false */	\
/* Restore user SRs */							\
	GET_CPUINFO(%r3);						\
	std	%r27,(savearea+CPUSAVE_R27)(%r3);			\
	std	%r28,(savearea+CPUSAVE_R28)(%r3);			\
	std	%r29,(savearea+CPUSAVE_R29)(%r3);			\
	std	%r30,(savearea+CPUSAVE_R30)(%r3);			\
	std	%r31,(savearea+CPUSAVE_R31)(%r3);			\
	mflr	%r27;			/* preserve LR */		\
	bl	restore_usersrs;	/* uses r28-r31 */		\
	mtlr	%r27;							\
	ld	%r31,(savearea+CPUSAVE_R31)(%r3);			\
	ld	%r30,(savearea+CPUSAVE_R30)(%r3);			\
	ld	%r29,(savearea+CPUSAVE_R29)(%r3);			\
	ld	%r28,(savearea+CPUSAVE_R28)(%r3);			\
	ld	%r27,(savearea+CPUSAVE_R27)(%r3);			\
1:	mfsprg2	%r3;			/* restore cr */		\
	mtcr	%r3;							\
	GET_CPUINFO(%r3);						\
	ld	%r3,(savearea+CPUSAVE_SRR0)(%r3); /* restore srr0 */	\
	mtsrr0	%r3;							\
	GET_CPUINFO(%r3);						\
	ld	%r3,(savearea+CPUSAVE_SRR1)(%r3); /* restore srr1 */	\
	mtsrr1	%r3;							\
	mfsprg3	%r3			/* restore r3 */

#ifdef KDTRACE_HOOKS
	.data
	.globl	dtrace_invop_calltrap_addr
	.align	8
	.type	dtrace_invop_calltrap_addr, @object
        .size	dtrace_invop_calltrap_addr, 8
dtrace_invop_calltrap_addr:
	.word	0
	.word	0

	.text
#endif

/*
 * Processor reset exception handler. These are typically
 * the first instructions the processor executes after a
 * software reset. We do this in two bits so that we are
 * not still hanging around in the trap handling region
 * once the MMU is turned on.
 */
	.globl	CNAME(rstcode), CNAME(rstcodeend), CNAME(cpu_reset_handler)
	.globl	CNAME(cpu_wakeup_handler)
	.p2align 3
CNAME(rstcode):
	/*
	 * Check if this is software reset or
	 * processor is waking up from power saving mode
	 * It is software reset when 46:47 = 0b00
	 */
	mfsrr1	%r9			/* Load SRR1 into r9 */
	andis.	%r9,%r9,0x3		/* Logic AND with 46:47 bits */
	beq	2f			/* Branch if software reset */
	bl	1f
	.llong	cpu_wakeup_handler

	/* It is software reset */

	/* Explicitly set MSR[SF] */
2:	mfmsr	%r9
	li	%r8,1
	insrdi	%r9,%r8,1,0
	mtmsrd	%r9
	isync

	bl	1f
	.llong	cpu_reset_handler /* Make sure to maintain 8-byte alignment */

1:	mflr	%r9
	ld	%r9,0(%r9)
	mtlr	%r9

	blr
CNAME(rstcodeend):

cpu_reset_handler:
	GET_TOCBASE(%r2)

	ld	%r1,TOC_REF(tmpstk)(%r2)	/* get new SP */
	addi	%r1,%r1,(TMPSTKSZ-48)

	bl	CNAME(cpudep_ap_early_bootstrap) /* Set PCPU */
	nop
	lis	%r3,1@l
	bl	CNAME(pmap_cpu_bootstrap)	/* Turn on virtual memory */
	nop
	bl	CNAME(cpudep_ap_bootstrap)	/* Set up PCPU and stack */
	nop
	mr	%r1,%r3				/* Use new stack */
	bl	CNAME(cpudep_ap_setup)
	nop
	GET_CPUINFO(%r5)
	ld	%r3,(PC_RESTORE)(%r5)
	cmpldi	%cr0,%r3,0
	beq	%cr0,2f
	nop
	li	%r4,1
	bl	CNAME(longjmp)
	nop
2:
#ifdef SMP
	bl	CNAME(machdep_ap_bootstrap)	/* And away! */
	nop
#endif

	/* Should not be reached */
9:
	b	9b

cpu_wakeup_handler:
	GET_TOCBASE(%r2)

	/* Check for false wake up due to badly SRR1 set (eg. by OPAL) */
	ld	%r3,TOC_REF(can_wakeup)(%r2)
	ld	%r3,0(%r3)
	cmpdi	%r3,0
	beq	cpu_reset_handler

	/* Turn on MMU after return from interrupt */
	mfsrr1	%r3
	ori	%r3,%r3,(PSL_IR | PSL_DR)
	mtsrr1	%r3

	/* Turn on MMU (needed to access PCB) */
	mfmsr	%r3
	ori	%r3,%r3,(PSL_IR | PSL_DR)
	mtmsr	%r3
	isync

	mfsprg0	%r3

	ld	%r3,PC_CURTHREAD(%r3)	/* Get current thread */
	ld	%r3,TD_PCB(%r3)		/* Get PCB of current thread */
	ld	%r12,PCB_CONTEXT(%r3)	/* Load the non-volatile GP regs. */
	ld	%r13,PCB_CONTEXT+1*8(%r3)
	ld	%r14,PCB_CONTEXT+2*8(%r3)
	ld	%r15,PCB_CONTEXT+3*8(%r3)
	ld	%r16,PCB_CONTEXT+4*8(%r3)
	ld	%r17,PCB_CONTEXT+5*8(%r3)
	ld	%r18,PCB_CONTEXT+6*8(%r3)
	ld	%r19,PCB_CONTEXT+7*8(%r3)
	ld	%r20,PCB_CONTEXT+8*8(%r3)
	ld	%r21,PCB_CONTEXT+9*8(%r3)
	ld	%r22,PCB_CONTEXT+10*8(%r3)
	ld	%r23,PCB_CONTEXT+11*8(%r3)
	ld	%r24,PCB_CONTEXT+12*8(%r3)
	ld	%r25,PCB_CONTEXT+13*8(%r3)
	ld	%r26,PCB_CONTEXT+14*8(%r3)
	ld	%r27,PCB_CONTEXT+15*8(%r3)
	ld	%r28,PCB_CONTEXT+16*8(%r3)
	ld	%r29,PCB_CONTEXT+17*8(%r3)
	ld	%r30,PCB_CONTEXT+18*8(%r3)
	ld	%r31,PCB_CONTEXT+19*8(%r3)
	ld	%r5,PCB_CR(%r3)		/* Load the condition register */
	mtcr	%r5
	ld	%r5,PCB_LR(%r3)		/* Load the link register */
	mtsrr0	%r5
	ld	%r1,PCB_SP(%r3)		/* Load the stack pointer */
	ld	%r2,PCB_TOC(%r3)	/* Load the TOC pointer */

	rfid

/*
 * This code gets copied to all the trap vectors
 * (except ISI/DSI, ALI, and the interrupts). Has to fit in 8 instructions!
 */

	.globl	CNAME(trapcode),CNAME(trapcodeend)
	.p2align 3
CNAME(trapcode):
	mtsprg1	%r1			/* save SP */
	mflr	%r1			/* Save the old LR in r1 */
	mtsprg2 %r1			/* And then in SPRG2 */
	ld	%r1,TRAP_GENTRAP(0)
	mtlr	%r1
	li	%r1, 0xe0		/* How to get the vector from LR */
	blrl				/* Branch to generictrap */
CNAME(trapcodeend):

/* Same thing for traps setting HSRR0/HSS1 */
	.globl	CNAME(hypertrapcode),CNAME(hypertrapcodeend)
	.p2align 3
CNAME(hypertrapcode):
	mtsprg1	%r1			/* save SP */
	mflr	%r1			/* Save the old LR in r1 */
	mtsprg2 %r1			/* And then in SPRG2 */
	ld	%r1,TRAP_GENTRAP(0)
	addi	%r1,%r1,(generichypertrap-generictrap)
	mtlr	%r1
	li	%r1, 0xe0		/* How to get the vector from LR */
	blrl				/* Branch to generictrap */
CNAME(hypertrapcodeend):

/*
 * For SLB misses: do special things for the kernel
 *
 * Note: SPRG1 is always safe to overwrite any time the MMU is on, which is
 * the only time this can be called.
 */
	.globl	CNAME(slbtrap),CNAME(slbtrapend)
	.p2align 3
CNAME(slbtrap):
	mtsprg1	%r1			/* save SP */
	GET_CPUINFO(%r1)
	std	%r2,(PC_SLBSAVE+16)(%r1)
	mfcr	%r2			/* save CR */
	std	%r2,(PC_SLBSAVE+104)(%r1)
	mfsrr1	%r2			/* test kernel mode */
	mtcr	%r2
	bf	17,2f			/* branch if PSL_PR is false */
	/* User mode */
	ld	%r2,(PC_SLBSAVE+104)(%r1) /* Restore CR */
	mtcr	%r2
	ld	%r2,(PC_SLBSAVE+16)(%r1) /* Restore R2 */
	mflr	%r1			/* Save the old LR in r1 */
	mtsprg2 %r1			/* And then in SPRG2 */
					/* 52 bytes so far */
	bl	1f
	.llong	generictrap
1:	mflr	%r1
	ld	%r1,0(%r1)
	mtlr	%r1
	li	%r1, 0x80		/* How to get the vector from LR */
	blrl				/* Branch to generictrap */
					/* 84 bytes */
2:	mflr	%r2			/* Save the old LR in r2 */
	nop
	bl	3f			/* Begin dance to jump to kern_slbtrap*/
	.llong	kern_slbtrap
3:	mflr	%r1
	ld	%r1,0(%r1)
	mtlr	%r1
	GET_CPUINFO(%r1)
	blrl				/* 124 bytes -- 4 to spare */
CNAME(slbtrapend):

kern_slbtrap:
	std	%r2,(PC_SLBSAVE+136)(%r1) /* old LR */
	std	%r3,(PC_SLBSAVE+24)(%r1) /* save R3 */

	/* Check if this needs to be handled as a regular trap (userseg miss) */
	mflr	%r2
	andi.	%r2,%r2,0xff80
	cmpwi	%r2,0x380
	bne	1f
	mfdar	%r2
	b	2f
1:	mfsrr0	%r2
2:	/* r2 now contains the fault address */
	lis	%r3,SEGMENT_MASK@highesta
	ori	%r3,%r3,SEGMENT_MASK@highera
	sldi	%r3,%r3,32
	oris	%r3,%r3,SEGMENT_MASK@ha
	ori	%r3,%r3,SEGMENT_MASK@l
	and	%r2,%r2,%r3	/* R2 = segment base address */
	lis	%r3,USER_ADDR@highesta
	ori	%r3,%r3,USER_ADDR@highera
	sldi	%r3,%r3,32
	oris	%r3,%r3,USER_ADDR@ha
	ori	%r3,%r3,USER_ADDR@l
	cmpd	%r2,%r3		/* Compare fault base to USER_ADDR */
	bne	3f

	/* User seg miss, handle as a regular trap */
	ld	%r2,(PC_SLBSAVE+104)(%r1) /* Restore CR */
	mtcr	%r2
	ld	%r2,(PC_SLBSAVE+16)(%r1) /* Restore R2,R3 */
	ld	%r3,(PC_SLBSAVE+24)(%r1)
	ld	%r1,(PC_SLBSAVE+136)(%r1) /* Save the old LR in r1 */
	mtsprg2 %r1			/* And then in SPRG2 */
	li	%r1, 0x80		/* How to get the vector from LR */
	b	generictrap		/* Retain old LR using b */
	
3:	/* Real kernel SLB miss */
	std	%r0,(PC_SLBSAVE+0)(%r1)	/* free all volatile regs */
	mfsprg1	%r2			/* Old R1 */
	std	%r2,(PC_SLBSAVE+8)(%r1)
	/* R2,R3 already saved */
	std	%r4,(PC_SLBSAVE+32)(%r1)
	std	%r5,(PC_SLBSAVE+40)(%r1)
	std	%r6,(PC_SLBSAVE+48)(%r1)
	std	%r7,(PC_SLBSAVE+56)(%r1)
	std	%r8,(PC_SLBSAVE+64)(%r1)
	std	%r9,(PC_SLBSAVE+72)(%r1)
	std	%r10,(PC_SLBSAVE+80)(%r1)
	std	%r11,(PC_SLBSAVE+88)(%r1)
	std	%r12,(PC_SLBSAVE+96)(%r1)
	/* CR already saved */
	mfxer	%r2			/* save XER */
	std	%r2,(PC_SLBSAVE+112)(%r1)
	mflr	%r2			/* save LR (SP already saved) */
	std	%r2,(PC_SLBSAVE+120)(%r1)
	mfctr	%r2			/* save CTR */
	std	%r2,(PC_SLBSAVE+128)(%r1)

	/* Call handler */
	addi	%r1,%r1,PC_SLBSTACK-48+1024
	li	%r2,~15
	and	%r1,%r1,%r2
	GET_TOCBASE(%r2)
	mflr	%r3
	andi.	%r3,%r3,0xff80
	mfdar	%r4
	mfsrr0	%r5
	bl	handle_kernel_slb_spill
	nop

	/* Save r28-31, restore r4-r12 */
	GET_CPUINFO(%r1)
	ld	%r4,(PC_SLBSAVE+32)(%r1)
	ld	%r5,(PC_SLBSAVE+40)(%r1)
	ld	%r6,(PC_SLBSAVE+48)(%r1)
	ld	%r7,(PC_SLBSAVE+56)(%r1)
	ld	%r8,(PC_SLBSAVE+64)(%r1)
	ld	%r9,(PC_SLBSAVE+72)(%r1)
	ld	%r10,(PC_SLBSAVE+80)(%r1)
	ld	%r11,(PC_SLBSAVE+88)(%r1)
	ld	%r12,(PC_SLBSAVE+96)(%r1)
	std	%r28,(PC_SLBSAVE+64)(%r1)
	std	%r29,(PC_SLBSAVE+72)(%r1)
	std	%r30,(PC_SLBSAVE+80)(%r1)
	std	%r31,(PC_SLBSAVE+88)(%r1)

	/* Restore kernel mapping */
	bl	restore_kernsrs

	/* Restore remaining registers */
	ld	%r28,(PC_SLBSAVE+64)(%r1)
	ld	%r29,(PC_SLBSAVE+72)(%r1)
	ld	%r30,(PC_SLBSAVE+80)(%r1)
	ld	%r31,(PC_SLBSAVE+88)(%r1)

	ld	%r2,(PC_SLBSAVE+104)(%r1)
	mtcr	%r2
	ld	%r2,(PC_SLBSAVE+112)(%r1)
	mtxer	%r2
	ld	%r2,(PC_SLBSAVE+120)(%r1)
	mtlr	%r2
	ld	%r2,(PC_SLBSAVE+128)(%r1)
	mtctr	%r2
	ld	%r2,(PC_SLBSAVE+136)(%r1)
	mtlr	%r2

	/* Restore r0-r3 */
	ld	%r0,(PC_SLBSAVE+0)(%r1)
	ld	%r2,(PC_SLBSAVE+16)(%r1)
	ld	%r3,(PC_SLBSAVE+24)(%r1)
	mfsprg1	%r1

	/* Back to whatever we were doing */
	rfid

/*
 * For ALI: has to save DSISR and DAR
 */
	.globl	CNAME(alitrap),CNAME(aliend)
CNAME(alitrap):
	mtsprg1	%r1			/* save SP */
	GET_CPUINFO(%r1)
	std	%r27,(PC_TEMPSAVE+CPUSAVE_R27)(%r1)	/* free r27-r31 */
	std	%r28,(PC_TEMPSAVE+CPUSAVE_R28)(%r1)
	std	%r29,(PC_TEMPSAVE+CPUSAVE_R29)(%r1)
	std	%r30,(PC_TEMPSAVE+CPUSAVE_R30)(%r1)
	std	%r31,(PC_TEMPSAVE+CPUSAVE_R31)(%r1)
	mfdar	%r30
	mfdsisr	%r31
	std	%r30,(PC_TEMPSAVE+CPUSAVE_AIM_DAR)(%r1)
	std	%r31,(PC_TEMPSAVE+CPUSAVE_AIM_DSISR)(%r1)
	mfsprg1	%r1			/* restore SP, in case of branch */
	mflr	%r28			/* save LR */
	mfcr	%r29			/* save CR */

	/* Begin dance to branch to s_trap in a bit */
	b	1f
	.p2align 3
1:	nop
	bl	1f
	.llong	s_trap
1:	mflr	%r31
	ld	%r31,0(%r31)
	mtlr	%r31

	/* Put our exception vector in SPRG3 */
	li	%r31, EXC_ALI
	mtsprg3	%r31

	/* Test whether we already had PR set */
	mfsrr1	%r31
	mtcr	%r31
	blrl
CNAME(aliend):

/*
 * Similar to the above for DSI
 * Has to handle standard pagetable spills
 */
	.globl	CNAME(dsitrap),CNAME(dsiend)
	.p2align 3
CNAME(dsitrap):
	mtsprg1	%r1			/* save SP */
	GET_CPUINFO(%r1)
	std	%r27,(PC_DISISAVE+CPUSAVE_R27)(%r1)	/* free r27-r31 */
	std	%r28,(PC_DISISAVE+CPUSAVE_R28)(%r1)
	std	%r29,(PC_DISISAVE+CPUSAVE_R29)(%r1)
	std	%r30,(PC_DISISAVE+CPUSAVE_R30)(%r1)
	std	%r31,(PC_DISISAVE+CPUSAVE_R31)(%r1)
	mfcr	%r29			/* save CR */
	mfxer	%r30			/* save XER */
	mtsprg2	%r30			/* in SPRG2 */
	mfsrr1	%r31			/* test kernel mode */
	mtcr	%r31
	mflr	%r28			/* save LR (SP already saved) */
	bl	1f			/* Begin branching to disitrap */
	.llong	disitrap
1:	mflr	%r1
	ld	%r1,0(%r1)
	mtlr	%r1
	blrl				/* Branch to generictrap */
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
	ld	%r31,(PC_DISISAVE+CPUSAVE_R27)(%r1)
	std	%r31,(PC_TEMPSAVE+CPUSAVE_R27)(%r1)
	ld	%r30,(PC_DISISAVE+CPUSAVE_R28)(%r1)
	std	%r30,(PC_TEMPSAVE+CPUSAVE_R28)(%r1)
	ld	%r31,(PC_DISISAVE+CPUSAVE_R29)(%r1)
	std	%r31,(PC_TEMPSAVE+CPUSAVE_R29)(%r1)
	ld	%r30,(PC_DISISAVE+CPUSAVE_R30)(%r1)
	std	%r30,(PC_TEMPSAVE+CPUSAVE_R30)(%r1)
	ld	%r31,(PC_DISISAVE+CPUSAVE_R31)(%r1)
	std	%r31,(PC_TEMPSAVE+CPUSAVE_R31)(%r1)
	mfdar	%r30
	mfdsisr	%r31
	std	%r30,(PC_TEMPSAVE+CPUSAVE_AIM_DAR)(%r1)
	std	%r31,(PC_TEMPSAVE+CPUSAVE_AIM_DSISR)(%r1)

#ifdef KDB
	/* Try to detect a kernel stack overflow */
	mfsrr1	%r31
	mtcr	%r31
	bt	17,realtrap		/* branch is user mode */
	mfsprg1	%r31			/* get old SP */
	clrrdi	%r31,%r31,12		/* Round SP down to nearest page */
	sub.	%r30,%r31,%r30		/* SP - DAR */
	bge	1f
	neg	%r30,%r30		/* modulo value */
1:	cmpldi	%cr0,%r30,4096		/* is DAR within a page of SP? */
	bge	%cr0,realtrap		/* no, too far away. */

	/* Now convert this DSI into a DDB trap.  */
	GET_CPUINFO(%r1)
	ld	%r30,(PC_TEMPSAVE+CPUSAVE_AIM_DAR)(%r1) /* get DAR */
	std	%r30,(PC_DBSAVE  +CPUSAVE_AIM_DAR)(%r1) /* save DAR */
	ld	%r30,(PC_TEMPSAVE+CPUSAVE_AIM_DSISR)(%r1) /* get DSISR */
	std	%r30,(PC_DBSAVE  +CPUSAVE_AIM_DSISR)(%r1) /* save DSISR */
	ld	%r31,(PC_DISISAVE+CPUSAVE_R27)(%r1) /* get  r27 */
	std	%r31,(PC_DBSAVE  +CPUSAVE_R27)(%r1) /* save r27 */
	ld	%r30,(PC_DISISAVE+CPUSAVE_R28)(%r1) /* get  r28 */
	std	%r30,(PC_DBSAVE  +CPUSAVE_R28)(%r1) /* save r28 */
	ld	%r31,(PC_DISISAVE+CPUSAVE_R29)(%r1) /* get  r29 */
	std	%r31,(PC_DBSAVE  +CPUSAVE_R29)(%r1) /* save r29 */
	ld	%r30,(PC_DISISAVE+CPUSAVE_R30)(%r1) /* get  r30 */
	std	%r30,(PC_DBSAVE  +CPUSAVE_R30)(%r1) /* save r30 */
	ld	%r31,(PC_DISISAVE+CPUSAVE_R31)(%r1) /* get  r31 */
	std	%r31,(PC_DBSAVE  +CPUSAVE_R31)(%r1) /* save r31 */
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
	ld	%r1,PC_CURPCB(%r1)
	mr	%r27,%r28		/* Save LR, r29 */
	mtsprg2	%r29
	bl	restore_kernsrs		/* enable kernel mapping */
	mfsprg2	%r29
	mr	%r28,%r27
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

generichypertrap:
	mtsprg3 %r1
	mfspr	%r1, SPR_HSRR0
	mtsrr0	%r1
	mfspr	%r1, SPR_HSRR1
	mtsrr1	%r1
	mfsprg3	%r1
	.globl	CNAME(generictrap)
generictrap:
	/* Save R1 for computing the exception vector */
	mtsprg3 %r1

	/* Save interesting registers */
	GET_CPUINFO(%r1)
	std	%r27,(PC_TEMPSAVE+CPUSAVE_R27)(%r1)	/* free r27-r31 */
	std	%r28,(PC_TEMPSAVE+CPUSAVE_R28)(%r1)
	std	%r29,(PC_TEMPSAVE+CPUSAVE_R29)(%r1)
	std	%r30,(PC_TEMPSAVE+CPUSAVE_R30)(%r1)
	std	%r31,(PC_TEMPSAVE+CPUSAVE_R31)(%r1)
	mfdar	%r30
	std	%r30,(PC_TEMPSAVE+CPUSAVE_AIM_DAR)(%r1)
	mfsprg1	%r1			/* restore SP, in case of branch */
	mfsprg2	%r28			/* save LR */
	mfcr	%r29			/* save CR */

	/* Compute the exception vector from the link register */
	mfsprg3 %r31
	ori	%r31,%r31,0xff00
	mflr	%r30
	addi	%r30,%r30,-4 /* The branch instruction, not the next */
	and	%r30,%r30,%r31
	mtsprg3	%r30

	/* Test whether we already had PR set */
	mfsrr1	%r31
	mtcr	%r31

s_trap:
	bf	17,k_trap		/* branch if PSL_PR is false */
	GET_CPUINFO(%r1)
u_trap:
	ld	%r1,PC_CURPCB(%r1)
	mr	%r27,%r28		/* Save LR, r29 */
	mtsprg2	%r29
	bl	restore_kernsrs		/* enable kernel mapping */
	mfsprg2	%r29
	mr	%r28,%r27

/*
 * Now the common trap catching code.
 */
k_trap:
	FRAME_SETUP(PC_TEMPSAVE)
/* Call C interrupt dispatcher: */
trapagain:
	GET_TOCBASE(%r2)
	addi	%r3,%r1,48
	bl	CNAME(powerpc_interrupt)
	nop

	.globl	CNAME(trapexit)	/* backtrace code sentinel */
CNAME(trapexit):
/* Disable interrupts: */
	mfmsr	%r3
	andi.	%r3,%r3,~PSL_EE@l
	mtmsr	%r3
	isync
/* Test AST pending: */
	ld	%r5,FRAME_SRR1+48(%r1)
	mtcr	%r5
	bf	17,1f			/* branch if PSL_PR is false */

	GET_CPUINFO(%r3)		/* get per-CPU pointer */
	lwz	%r4, TD_FLAGS(%r13)	/* get thread flags value */
	lis	%r5, (TDF_ASTPENDING|TDF_NEEDRESCHED)@h
	ori	%r5,%r5, (TDF_ASTPENDING|TDF_NEEDRESCHED)@l
	and.	%r4,%r4,%r5
	beq	1f
	mfmsr	%r3			/* re-enable interrupts */
	ori	%r3,%r3,PSL_EE@l
	mtmsr	%r3
	isync
	GET_TOCBASE(%r2)
	addi	%r3,%r1,48
	bl	CNAME(ast)
	nop
	.globl	CNAME(asttrapexit)	/* backtrace code sentinel #2 */
CNAME(asttrapexit):
	b	trapexit		/* test ast ret value ? */
1:
	FRAME_LEAVE(PC_TEMPSAVE)
	rfid

#if defined(KDB)
/*
 * Deliberate entry to dbtrap
 */
ASENTRY_NOPROF(breakpoint)
	mtsprg1	%r1
	mfmsr	%r3
	mtsrr1	%r3
	andi.	%r3,%r3,~(PSL_EE|PSL_ME)@l
	mtmsr	%r3			/* disable interrupts */
	isync
	GET_CPUINFO(%r3)
	std	%r27,(PC_DBSAVE+CPUSAVE_R27)(%r3)
	std	%r28,(PC_DBSAVE+CPUSAVE_R28)(%r3)
	std	%r29,(PC_DBSAVE+CPUSAVE_R29)(%r3)
	std	%r30,(PC_DBSAVE+CPUSAVE_R30)(%r3)
	std	%r31,(PC_DBSAVE+CPUSAVE_R31)(%r3)
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

	GET_TOCBASE(%r1)			/* get new SP */
	ld	%r1,TOC_REF(trapstk)(%r1)
	addi	%r1,%r1,(TRAPSTKSZ-48)

	FRAME_SETUP(PC_DBSAVE)
/* Call C trap code: */
	GET_TOCBASE(%r2)
	addi	%r3,%r1,48
	bl	CNAME(db_trap_glue)
	nop
	or.	%r3,%r3,%r3
	bne	dbleave
/* This wasn't for KDB, so switch to real trap: */
	ld	%r3,FRAME_EXC+48(%r1)	/* save exception */
	GET_CPUINFO(%r4)
	std	%r3,(PC_DBSAVE+CPUSAVE_R31)(%r4)
	FRAME_LEAVE(PC_DBSAVE)
	mtsprg1	%r1			/* prepare for entrance to realtrap */
	GET_CPUINFO(%r1)
	std	%r27,(PC_TEMPSAVE+CPUSAVE_R27)(%r1)
	std	%r28,(PC_TEMPSAVE+CPUSAVE_R28)(%r1)
	std	%r29,(PC_TEMPSAVE+CPUSAVE_R29)(%r1)
	std	%r30,(PC_TEMPSAVE+CPUSAVE_R30)(%r1)
	std	%r31,(PC_TEMPSAVE+CPUSAVE_R31)(%r1)
	mflr	%r28
	mfcr	%r29
	ld	%r31,(PC_DBSAVE+CPUSAVE_R31)(%r1)
	mtsprg3	%r31			/* SPRG3 was clobbered by FRAME_LEAVE */
	mfsprg1	%r1
	b	realtrap
dbleave:
	FRAME_LEAVE(PC_DBSAVE)
	rfid

/*
 * In case of KDB we want a separate trap catcher for it
 */
	.globl	CNAME(dblow),CNAME(dbend)
	.p2align 3
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

	ld	%r1, TRAP_GENTRAP(0)	/* Get branch address */
	mtlr	%r1
	li	%r1, 0	 		/* How to get the vector from LR */
	blrl				/* Branch to generictrap */

1:
	GET_CPUINFO(%r1)
	std	%r27,(PC_DBSAVE+CPUSAVE_R27)(%r1)	/* free r27 */
	std	%r28,(PC_DBSAVE+CPUSAVE_R28)(%r1)	/* free r28 */
        mfsprg2	%r28				/* r29 holds cr...  */
        std	%r28,(PC_DBSAVE+CPUSAVE_R29)(%r1)	/* free r29 */
        std	%r30,(PC_DBSAVE+CPUSAVE_R30)(%r1)	/* free r30 */
        std	%r31,(PC_DBSAVE+CPUSAVE_R31)(%r1)	/* free r31 */
        mflr	%r28					/* save LR */
	nop						/* alignment */
	bl	9f					/* Begin branch */
	.llong	dbtrap
9:	mflr	%r1
	ld	%r1,0(%r1)
	mtlr	%r1
	blrl				/* Branch to generictrap */
CNAME(dbend):
#endif /* KDB */
