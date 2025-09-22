/* $OpenBSD: trap_subr.S,v 1.19 2020/12/30 06:06:30 gkoehler Exp $ */
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

#include "assym.h"

#include <machine/param.h>
#include <machine/psl.h>
#include <machine/trap.h>

#define SPR_VRSAVE	256

	.abiversion 2

#define GET_CPUINFO(r) \
	mfsprg0  r

#define GET_TOCBASE(r) \
	bl	99f;							\
99:	mflr	r;							\
	addis	r, r, (.TOC. - 99b)@ha;					\
	addi	r, r, (.TOC. - 99b)@l;

/*
 * Restore SRs for a pmap
 *
 * Requires that r28-r31 be scratch, with r28 initialized to the SLB cache
 */

restore_usersrs:
	GET_CPUINFO(%r28)
	ld	%r28, CI_USER_SLB_PA(%r28)
	li	%r29, 0			/* Set the counter to zero */

	/* Invalidate entire SLB */
	slbia
	slbmfee	%r31, %r29
	clrrdi	%r31, %r31, 28
	slbie	%r31

1:	ld	%r31, 0(%r28)		/* Load SLBE */
	cmpdi	%r31, 0			/* If SLBE is not valid, stop */
	beqlr
	ld	%r30, 8(%r28)		/* Load SLBV  */
	slbmte	%r30, %r31		/* Install SLB entry */

	addi	%r28, %r28, 16		/* Advance pointer */
	addi	%r29, %r29, 1
	cmpdi	%r29, 32		/* Repeat if we are not at the end */
	blt	1b
	blr

restore_kernsrs:
	GET_CPUINFO(%r28)
	addi	%r28, %r28, CI_KERNEL_SLB

	li	%r29, 0			/* Set the counter to zero */

	/* Invalidate entire SLB */
	slbia
	slbmfee	%r31, %r29
	clrrdi	%r31, %r31, 28
	slbie	%r31

1:	ld	%r31, 0(%r28)		/* Load SLBE */
	cmpdi	%r31, 0			/* If SLBE is not valid, stop */
	beqlr
	ld	%r30, 8(%r28)		/* Load SLBV  */
	slbmte	%r30, %r31		/* Install SLB entry */

	addi	%r28, %r28, 16		/* Advance pointer */
	addi	%r29, %r29, 1
	cmpdi	%r29, 31		/* Repeat if we are not at the end */
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
	std	%r30, (savearea+CPUSAVE_SRR0)(%r31);	/* save SRR0 */	\
	mfsrr1	%r30;							\
	std	%r30, (savearea+CPUSAVE_SRR1)(%r31);	/* save SRR1 */	\
	mfsprg1	%r31;			/* get saved SP (clears SPRG1) */ \
	mfmsr	%r30;							\
	ori	%r30, %r30, (PSL_DR|PSL_IR|PSL_RI)@l; /* relocation on */ \
	mtmsr	%r30;			/* stack can now be accessed */	\
	isync;								\
	stdu	%r31, -(FRAMELEN+288)(%r1); /* save it in the callframe */ \
	std	%r0, FRAME_0+32(%r1);	/* save r0 in the trapframe */	\
	std	%r31, FRAME_1+32(%r1);	/* save SP   "      "       */	\
	std	%r2, FRAME_2+32(%r1);	/* save r2   "      "       */	\
	std	%r28, FRAME_LR+32(%r1);	/* save LR   "      "       */	\
	std	%r29, FRAME_CR+32(%r1);	/* save CR   "      "       */	\
	GET_CPUINFO(%r2);						\
	ld	%r27, (savearea+CPUSAVE_R27)(%r2); /* get saved r27 */	\
	ld	%r28, (savearea+CPUSAVE_R28)(%r2); /* get saved r28 */	\
	ld	%r29, (savearea+CPUSAVE_R29)(%r2); /* get saved r29 */	\
	ld	%r30, (savearea+CPUSAVE_R30)(%r2); /* get saved r30 */	\
	ld	%r31, (savearea+CPUSAVE_R31)(%r2); /* get saved r31 */	\
	std	%r3, FRAME_3+32(%r1);	/* save r3-r31 */		\
	std	%r4, FRAME_4+32(%r1);					\
	std	%r5, FRAME_5+32(%r1);					\
	std	%r6, FRAME_6+32(%r1);					\
	std	%r7, FRAME_7+32(%r1);					\
	std	%r8, FRAME_8+32(%r1);					\
	std	%r9, FRAME_9+32(%r1);					\
	std	%r10, FRAME_10+32(%r1);					\
	std	%r11, FRAME_11+32(%r1);					\
	std	%r12, FRAME_12+32(%r1);					\
	std	%r13, FRAME_13+32(%r1);					\
	std	%r14, FRAME_14+32(%r1);					\
	std	%r15, FRAME_15+32(%r1);					\
	std	%r16, FRAME_16+32(%r1);					\
	std	%r17, FRAME_17+32(%r1);					\
	std	%r18, FRAME_18+32(%r1);					\
	std	%r19, FRAME_19+32(%r1);					\
	std	%r20, FRAME_20+32(%r1);					\
	std	%r21, FRAME_21+32(%r1);					\
	std	%r22, FRAME_22+32(%r1);					\
	std	%r23, FRAME_23+32(%r1);					\
	std	%r24, FRAME_24+32(%r1);					\
	std	%r25, FRAME_25+32(%r1);					\
	std	%r26, FRAME_26+32(%r1);					\
	std	%r27, FRAME_27+32(%r1);					\
	std	%r28, FRAME_28+32(%r1);					\
	std	%r29, FRAME_29+32(%r1);					\
	std	%r30, FRAME_30+32(%r1);					\
	std	%r31, FRAME_31+32(%r1);					\
	ld	%r28, (savearea+CPUSAVE_DAR)(%r2);  /* saved DAR */	\
	ld	%r29, (savearea+CPUSAVE_DSISR)(%r2);/* saved DSISR */	\
	ld	%r30, (savearea+CPUSAVE_SRR0)(%r2); /* saved SRR0 */	\
	ld	%r31, (savearea+CPUSAVE_SRR1)(%r2); /* saved SRR1 */	\
	mfxer	%r3;							\
	mfctr	%r4;							\
	mfsprg3	%r5;							\
	mfspr	%r6, SPR_VRSAVE;					\
	std	%r3, FRAME_XER+32(%r1);	/* save xer/ctr/exc */		\
	std	%r4, FRAME_CTR+32(%r1);					\
	std	%r5, FRAME_EXC+32(%r1);					\
	std	%r6, FRAME_VRSAVE+32(%r1);				\
	std	%r28, FRAME_DAR+32(%r1);				\
	std	%r29, FRAME_DSISR+32(%r1); /* save dsisr/srr0/srr1 */	\
	std	%r30, FRAME_SRR0+32(%r1);				\
	std	%r31, FRAME_SRR1+32(%r1);

#define FRAME_LEAVE(savearea)						\
/* Disable exceptions: */						\
	mfmsr	%r2;							\
	andi.	%r2,%r2,~PSL_EE@l;					\
	mtmsr	%r2;							\
	isync;								\
/* Now restore regs: */							\
	ld	%r2, FRAME_SRR0+32(%r1);				\
	ld	%r3, FRAME_SRR1+32(%r1);				\
	ld	%r4, FRAME_CTR+32(%r1);					\
	ld	%r5, FRAME_XER+32(%r1);					\
	ld	%r6, FRAME_LR+32(%r1);					\
	GET_CPUINFO(%r7);						\
	std	%r2, (savearea+CPUSAVE_SRR0)(%r7); /* save SRR0 */	\
	std	%r3, (savearea+CPUSAVE_SRR1)(%r7); /* save SRR1 */	\
	ld	%r7, FRAME_CR+32(%r1);					\
	ld	%r8, FRAME_VRSAVE+32(%r1);				\
	mtctr	%r4;							\
	mtxer	%r5;							\
	mtlr	%r6;							\
	mtsprg2	%r7;							\
	mtspr	SPR_VRSAVE, %r8;					\
	ld	%r31, FRAME_31+32(%r1);	/* restore r0-31 */		\
	ld	%r30, FRAME_30+32(%r1);					\
	ld	%r29, FRAME_29+32(%r1);					\
	ld	%r28, FRAME_28+32(%r1);					\
	ld	%r27, FRAME_27+32(%r1);					\
	ld	%r26, FRAME_26+32(%r1);					\
	ld	%r25, FRAME_25+32(%r1);					\
	ld	%r24, FRAME_24+32(%r1);					\
	ld	%r23, FRAME_23+32(%r1);					\
	ld	%r22, FRAME_22+32(%r1);					\
	ld	%r21, FRAME_21+32(%r1);					\
	ld	%r20, FRAME_20+32(%r1);					\
	ld	%r19, FRAME_19+32(%r1);					\
	ld	%r18, FRAME_18+32(%r1);					\
	ld	%r17, FRAME_17+32(%r1);					\
	ld	%r16, FRAME_16+32(%r1);					\
	ld	%r15, FRAME_15+32(%r1);					\
	ld	%r14, FRAME_14+32(%r1);					\
	ld	%r13, FRAME_13+32(%r1);					\
	ld	%r12, FRAME_12+32(%r1);					\
	ld	%r11, FRAME_11+32(%r1);					\
	ld	%r10, FRAME_10+32(%r1);					\
	ld	%r9, FRAME_9+32(%r1);					\
	ld	%r8, FRAME_8+32(%r1);					\
	ld	%r7, FRAME_7+32(%r1);					\
	ld	%r6, FRAME_6+32(%r1);					\
	ld	%r5, FRAME_5+32(%r1);					\
	ld	%r4, FRAME_4+32(%r1);					\
	ld	%r3, FRAME_3+32(%r1);					\
	ld	%r2, FRAME_2+32(%r1);					\
	ld	%r0, FRAME_0+32(%r1);					\
	ld	%r1, FRAME_1+32(%r1);					\
/* Can't touch %r1 from here on */					\
	mtsprg3	%r3;			/* save r3 */			\
/* Disable translation, machine check and recoverability: */		\
	mfmsr	%r3;							\
	andi.	%r3, %r3, ~(PSL_DR|PSL_IR|PSL_ME|PSL_RI)@l;		\
	mtmsr	%r3;							\
	isync;								\
/* Decide whether we return to user mode: */				\
	GET_CPUINFO(%r3);						\
	ld	%r3, (savearea+CPUSAVE_SRR1)(%r3);			\
	mtcr	%r3;							\
	bf	17, 1f;			/* branch if PSL_PR is false */	\
/* Restore user SRs */							\
	GET_CPUINFO(%r3);						\
	std	%r27, (savearea+CPUSAVE_R27)(%r3);			\
	std	%r28, (savearea+CPUSAVE_R28)(%r3);			\
	std	%r29, (savearea+CPUSAVE_R29)(%r3);			\
	std	%r30, (savearea+CPUSAVE_R30)(%r3);			\
	std	%r31, (savearea+CPUSAVE_R31)(%r3);			\
	mflr	%r27;			/* preserve LR */		\
	bl	restore_usersrs;	/* uses r28-r31 */		\
	mtlr	%r27;							\
	ld	%r31, (savearea+CPUSAVE_R31)(%r3);			\
	ld	%r30, (savearea+CPUSAVE_R30)(%r3);			\
	ld	%r29, (savearea+CPUSAVE_R29)(%r3);			\
	ld	%r28, (savearea+CPUSAVE_R28)(%r3);			\
	ld	%r27, (savearea+CPUSAVE_R27)(%r3);			\
1:	mfsprg2	%r3;			/* restore cr */		\
	mtcr	%r3;							\
	GET_CPUINFO(%r3);						\
	ld	%r3, (savearea+CPUSAVE_SRR0)(%r3); /* restore srr0 */	\
	mtsrr0	%r3;							\
	GET_CPUINFO(%r3);						\
	ld	%r3, (savearea+CPUSAVE_SRR1)(%r3); /* restore srr1 */	\
	mtsrr1	%r3;							\
	mfsprg3	%r3			/* restore r3 */


	.text

	.globl trapcode, trapcodeend
trapcode:
	mtsprg1	%r1
	mflr	%r1
	mtsprg2	%r1
	ld	%r1, TRAP_ENTRY(0)
	mtlr	%r1
	li	%r1, 0xe0
	blrl
trapcodeend:

	.globl hvtrapcode, hvtrapcodeend
hvtrapcode:
	mtsprg1	%r1
	mflr	%r1
	mtsprg2	%r1
	ld	%r1, TRAP_HVENTRY(0)
	mtlr	%r1
	li	%r1, 0xe0
	blrl
hvtrapcodeend:

/* System reset might be an exit from power-saving mode. */
	.globl rsttrapcode, rsttrapcodeend
rsttrapcode:
	mtsprg1	%r1
	mfcr	%r1
	mtsprg2	%r1			/* save cr */
	mfsrr1	%r1
	andis.	%r1, %r1, 0x3		/* test srr1 bits 46:47 */
	beq	1f
	/* This is an exit from power-saving mode. */
	ld	%r1, TRAP_RSTENTRY(0)	/* cpu_idle_restore_context */
	mtctr	%r1
	bctr
1:	/* This is something else. */
	mfsprg2	%r1
	mtcr	%r1			/* restore cr */
	mflr	%r1
	mtsprg2	%r1
	ld	%r1, TRAP_ENTRY(0)	/* generictrap */
	mtlr	%r1
	li	%r1, 0xe0
	blrl
rsttrapcodeend:

/*
 * For SLB misses: do special things for the kernel
 *
 * Note: SPRG1 is always safe to overwrite any time the MMU was on, which is
 * the only time this can be called.
 */
	.globl slbtrapcode, slbtrapcodeend
slbtrapcode:
	/* 0x00 */
	mtsprg1	%r1			/* save SP */
	GET_CPUINFO(%r1)
	std	%r2, (CI_SLBSAVE+16)(%r1)	/* save r2 */
	mfcr	%r2
	/* 0x10 */
	std	%r2, (CI_SLBSAVE+104)(%r1)	/* save CR */
	mfsrr1	%r2			/* test kernel mode */
	mtcr	%r2
	bf	17, 1f			/* branch if PSL_PR is false */
	/* 0x20 */
	/* User mode */
	ld	%r2, (CI_SLBSAVE+104)(%r1)
	mtcr	%r2				/* restore CR */
	ld	%r2, (CI_SLBSAVE+16)(%r1) 	/* restore r2 */
	mflr	%r1
	/* 0x30 */
	mtsprg2 %r1				/* save LR in SPRG2 */
	ld	%r1, TRAP_ENTRY(0)
	mtlr	%r1
	li	%r1, 0x80		/* How to get the vector from LR */
	/* 0x40 */
	blrl				/* Branch to generictrap */
1:	mflr	%r2			/* Save the old LR in r2 */
	/* Kernel mode */
	ld	%r1, TRAP_SLBENTRY(0)
	mtlr	%r1
	/* 0x50 */
	GET_CPUINFO(%r1)
	blrl					/* Branch to kern_slbtrap */
/* must fit in 128 bytes! */
slbtrapcodeend:

/*
 * On entry:
 * SPRG1: SP
 * r1: pcpu
 * r2: LR
 * LR: branch address in trap region
 */
	.globl kern_slbtrap
kern_slbtrap:
	std	%r2, (CI_SLBSAVE+136)(%r1) /* old LR */
	std	%r3, (CI_SLBSAVE+24)(%r1) /* save R3 */

	/* Check if this needs to be handled as a regular trap (userseg miss) */
	mfdar	%r2
	lis	%r3, SEGMENT_MASK@h
	ori	%r3, %r3, SEGMENT_MASK@l
	andc	%r2, %r2, %r3	/* R2 = segment base address */
	lis	%r3, USER_ADDR@highesta
	ori	%r3, %r3, USER_ADDR@highera
	sldi	%r3, %r3, 32
	oris	%r3, %r3, USER_ADDR@ha
	ori	%r3, %r3, USER_ADDR@l
	cmpd	%r2, %r3	/* Compare fault base to USER_ADDR */
	bne	1f

	/* User seg miss, handle as a regular trap */
	ld	%r2, (CI_SLBSAVE+104)(%r1) /* Restore CR */
	mtcr	%r2
	ld	%r2, (CI_SLBSAVE+16)(%r1) /* Restore R2,R3 */
	ld	%r3, (CI_SLBSAVE+24)(%r1)
	ld	%r1, (CI_SLBSAVE+136)(%r1) /* Save the old LR in r1 */
	mtsprg2 %r1			/* And then in SPRG2 */
	li	%r1, 0x80		/* How to get the vector from LR */
	b	generictrap		/* Retain old LR using b */

1:	/* Real kernel SLB miss */
	std	%r0, (CI_SLBSAVE+0)(%r1) /* free all volatile regs */
	mfsprg1	%r2			/* Old R1 */
	std	%r2, (CI_SLBSAVE+8)(%r1)
	/* R2, R3 already saved */
	std	%r4, (CI_SLBSAVE+32)(%r1)
	std	%r5, (CI_SLBSAVE+40)(%r1)
	std	%r6, (CI_SLBSAVE+48)(%r1)
	std	%r7, (CI_SLBSAVE+56)(%r1)
	std	%r8, (CI_SLBSAVE+64)(%r1)
	std	%r9, (CI_SLBSAVE+72)(%r1)
	std	%r10, (CI_SLBSAVE+80)(%r1)
	std	%r11, (CI_SLBSAVE+88)(%r1)
	std	%r12, (CI_SLBSAVE+96)(%r1)
	/* CR already saved */
	mfxer	%r2			/* save XER */
	std	%r2, (CI_SLBSAVE+112)(%r1)
	mflr	%r2			/* save LR (SP already saved) */
	std	%r2, (CI_SLBSAVE+120)(%r1)
	mfctr	%r2			/* save CTR */
	std	%r2, (CI_SLBSAVE+128)(%r1)

	/* Call handler */
	addi	%r1, %r1, CI_SLBSTACK-48+1024
	li	%r2, ~15
	and	%r1, %r1, %r2
	GET_TOCBASE(%r2)
	mfdar	%r3
	bl	pmap_spill_kernel_slb
	nop

	/* Save r28-31, restore r4-r12 */
	GET_CPUINFO(%r1)
	ld	%r4, (CI_SLBSAVE+32)(%r1)
	ld	%r5, (CI_SLBSAVE+40)(%r1)
	ld	%r6, (CI_SLBSAVE+48)(%r1)
	ld	%r7, (CI_SLBSAVE+56)(%r1)
	ld	%r8, (CI_SLBSAVE+64)(%r1)
	ld	%r9, (CI_SLBSAVE+72)(%r1)
	ld	%r10, (CI_SLBSAVE+80)(%r1)
	ld	%r11, (CI_SLBSAVE+88)(%r1)
	ld	%r12, (CI_SLBSAVE+96)(%r1)
	std	%r28, (CI_SLBSAVE+64)(%r1)
	std	%r29, (CI_SLBSAVE+72)(%r1)
	std	%r30, (CI_SLBSAVE+80)(%r1)
	std	%r31, (CI_SLBSAVE+88)(%r1)

	/* Restore kernel mapping */
	bl	restore_kernsrs

	/* Restore remaining registers */
	ld	%r28, (CI_SLBSAVE+64)(%r1)
	ld	%r29, (CI_SLBSAVE+72)(%r1)
	ld	%r30, (CI_SLBSAVE+80)(%r1)
	ld	%r31, (CI_SLBSAVE+88)(%r1)

	ld	%r2, (CI_SLBSAVE+104)(%r1)
	mtcr	%r2
	ld	%r2, (CI_SLBSAVE+112)(%r1)
	mtxer	%r2
	ld	%r2, (CI_SLBSAVE+120)(%r1)
	mtlr	%r2
	ld	%r2, (CI_SLBSAVE+128)(%r1)
	mtctr	%r2
	ld	%r2, (CI_SLBSAVE+136)(%r1)
	mtlr	%r2

	/* Restore r0-r3 */
	ld	%r0, (CI_SLBSAVE+0)(%r1)
	ld	%r2, (CI_SLBSAVE+16)(%r1)
	ld	%r3, (CI_SLBSAVE+24)(%r1)
	mfsprg1	%r1

	/* Back to whatever we were doing */
	rfid

/*
 * generichvtrap makes a hypervisor trap look like a normal trap.
 */

	.globl generichvtrap
generichvtrap:
	/* Move HSRR0/HSRR1 to SSR0/SRR1 */
	mtsprg3	%r1
	mfspr	%r1, 314	/* HSRR0 */
	mtsrr0	%r1
	mfspr	%r1, 315	/* HSRR1 */
	mtsrr1	%r1
	mfsprg3	%r1
	/* FALLTHROUGH */

/*
 * generictrap does some standard setup for trap handling to minimize
 * the code that need be installed in the actual vectors. It expects
 * the following conditions.
 * 
 * R1 - Trap vector = LR & (0xff00 | R1)
 * SPRG1 - Original R1 contents
 * SPRG2 - Original LR
 */

	.globl generictrap
	.type generictrap, @function
generictrap:
	/* Save R1 for computing the exception vector */
	mtsprg3	%r1

	/* Save interesting registers */
	GET_CPUINFO(%r1)
	std	%r27, (CI_TEMPSAVE+CPUSAVE_R27)(%r1)	/* free r27-r31 */
	std	%r28, (CI_TEMPSAVE+CPUSAVE_R28)(%r1)
	std	%r29, (CI_TEMPSAVE+CPUSAVE_R29)(%r1)
	std	%r30, (CI_TEMPSAVE+CPUSAVE_R30)(%r1)
	std	%r31, (CI_TEMPSAVE+CPUSAVE_R31)(%r1)
	mfdar	%r30
	std	%r30, (CI_TEMPSAVE+CPUSAVE_DAR)(%r1)
	mfdsisr	%r30
	std	%r30, (CI_TEMPSAVE+CPUSAVE_DSISR)(%r1)
	mfsprg1	%r1			/* restore SP, in case of branch */
	mfsprg2	%r28			/* save LR */
	mfcr	%r29			/* save CR */

	/* Compute the exception vector from the link register */
	mfsprg3 %r31
	ori	%r31, %r31, 0xff00
	mflr	%r30
	addi	%r30, %r30, -4 /* The branch instruction, not the next */
	and	%r30, %r30, %r31
	mtsprg3	%r30

	/* Test whether we already had PR set */
	mfsrr1	%r31
	mtcr	%r31
	bf	17, k_trap		/* branch if PSL_PR is false */

u_trap:
	GET_CPUINFO(%r1)
	ld	%r1, CI_CURPCB(%r1)
	addi	%r1, %r1, USPACE
	mr	%r27, %r28
	mtsprg2	%r29
	bl	restore_kernsrs		/* enable kernel mapping */
	mfsprg2	%r29
	mr	%r28, %r27

k_trap:
	FRAME_SETUP(CI_TEMPSAVE)
	GET_TOCBASE(%r2)
trapagain:
	addi	%r3, %r1, 32
	bl	trap

	.globl	trapexit
trapexit:
/* Disable interrupts: */
	mfmsr	%r3
	andi.	%r3, %r3, ~PSL_EE@l
	mtmsr	%r3
	isync
/* Test AST pending: */
	ld	%r5, FRAME_SRR1+32(%r1)
	mtcr	%r5
	bf	17, 1f			/* branch if PSL_PR is false */

	GET_CPUINFO(%r3)		/* get per-CPU pointer */
	ld	%r4, CI_CURPROC(%r3)
	lwz	%r4, P_MD_ASTPENDING(%r4)
	cmpwi	%r4, 0
	beq	1f
	li	%r6, EXC_AST
	std	%r6, FRAME_EXC+32(%r1)
	b	trapagain
1:
	FRAME_LEAVE(CI_TEMPSAVE)
	rfid
