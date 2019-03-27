/*-
 * Copyright (C) 2006-2009 Semihalf, Rafal Jaworowski <raj@semihalf.com>
 * Copyright (C) 2006 Semihalf, Marian Balakowicz <m8@semihalf.com>
 * Copyright (C) 2006 Juniper Networks, Inc.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 *	from: $NetBSD: trap_subr.S,v 1.20 2002/04/22 23:20:08 kleink Exp $
 */

/*
 * NOTICE: This is not a standalone file.  to use it, #include it in
 * your port's locore.S, like so:
 *
 *	#include <powerpc/booke/trap_subr.S>
 */

/*
 * SPRG usage notes
 *
 * SPRG0 - pcpu pointer
 * SPRG1 - all interrupts except TLB miss, critical, machine check
 * SPRG2 - critical
 * SPRG3 - machine check
 * SPRG4-6 - scratch
 *
 */

/* Get the per-CPU data structure */
#define GET_CPUINFO(r) mfsprg0 r

#define RES_GRANULE	64
#define RES_LOCK	0	/* offset to the 'lock' word */
#ifdef __powerpc64__
#define RES_RECURSE	8	/* offset to the 'recurse' word */
#else
#define RES_RECURSE	4	/* offset to the 'recurse' word */
#endif

/*
 * Standard interrupt prolog
 *
 * sprg_sp - SPRG{1-3} reg used to temporarily store the SP
 * savearea - temp save area (pc_{tempsave, disisave, critsave, mchksave})
 * isrr0-1 - save restore registers with CPU state at interrupt time (may be
 *           SRR0-1, CSRR0-1, MCSRR0-1
 *
 * 1. saves in the given savearea:
 *   - R30-31
 *   - DEAR, ESR
 *   - xSRR0-1
 *
 * 2. saves CR -> R30
 *
 * 3. switches to kstack if needed
 *
 * 4. notes:
 *   - R31 can be used as scratch register until a new frame is layed on
 *     the stack with FRAME_SETUP
 *
 *   - potential TLB miss: NO. Saveareas are always acessible via TLB1 
 *     permanent entries, and within this prolog we do not dereference any
 *     locations potentially not in the TLB
 */
#define STANDARD_PROLOG(sprg_sp, savearea, isrr0, isrr1)		\
	mtspr	sprg_sp, %r1;		/* Save SP */			\
	GET_CPUINFO(%r1);		/* Per-cpu structure */		\
	STORE	%r30, (savearea+CPUSAVE_R30)(%r1);			\
	STORE	%r31, (savearea+CPUSAVE_R31)(%r1); 			\
	mfdear	%r30;		 					\
	mfesr	%r31;							\
	STORE	%r30, (savearea+CPUSAVE_BOOKE_DEAR)(%r1); 		\
	STORE	%r31, (savearea+CPUSAVE_BOOKE_ESR)(%r1); 		\
	mfspr	%r30, isrr0;						\
	mfspr	%r31, isrr1;	 	/* MSR at interrupt time */	\
	STORE	%r30, (savearea+CPUSAVE_SRR0)(%r1);			\
	STORE	%r31, (savearea+CPUSAVE_SRR1)(%r1);			\
	isync;			 					\
	mfspr	%r1, sprg_sp;	 	/* Restore SP */		\
	mfcr	%r30;		 	/* Save CR */			\
	/* switch to per-thread kstack if intr taken in user mode */	\
	mtcr	%r31;			/* MSR at interrupt time  */	\
	bf	17, 1f;							\
	GET_CPUINFO(%r1);		/* Per-cpu structure */		\
	LOAD	%r1, PC_CURPCB(%r1); 	/* Per-thread kernel stack */	\
1:

#define	STANDARD_CRIT_PROLOG(sprg_sp, savearea, isrr0, isrr1)		\
	mtspr	sprg_sp, %r1;		/* Save SP */			\
	GET_CPUINFO(%r1);		/* Per-cpu structure */		\
	STORE	%r30, (savearea+CPUSAVE_R30)(%r1);			\
	STORE	%r31, (savearea+CPUSAVE_R31)(%r1);			\
	mfdear	%r30;							\
	mfesr	%r31;							\
	STORE	%r30, (savearea+CPUSAVE_BOOKE_DEAR)(%r1);		\
	STORE	%r31, (savearea+CPUSAVE_BOOKE_ESR)(%r1);		\
	mfspr	%r30, isrr0;						\
	mfspr	%r31, isrr1;		/* MSR at interrupt time */	\
	STORE	%r30, (savearea+CPUSAVE_SRR0)(%r1);			\
	STORE	%r31, (savearea+CPUSAVE_SRR1)(%r1);			\
	mfspr	%r30, SPR_SRR0;						\
	mfspr	%r31, SPR_SRR1;		/* MSR at interrupt time */	\
	STORE	%r30, (savearea+BOOKE_CRITSAVE_SRR0)(%r1);		\
	STORE	%r31, (savearea+BOOKE_CRITSAVE_SRR1)(%r1);		\
	isync;								\
	mfspr	%r1, sprg_sp;		/* Restore SP */		\
	mfcr	%r30;			/* Save CR */			\
	/* switch to per-thread kstack if intr taken in user mode */	\
	mtcr	%r31;			/* MSR at interrupt time  */	\
	bf	17, 1f;							\
	GET_CPUINFO(%r1);		/* Per-cpu structure */		\
	LOAD	%r1, PC_CURPCB(%r1);	/* Per-thread kernel stack */	\
1:

/*
 * FRAME_SETUP assumes:
 *	SPRG{1-3}	SP at the time interrupt occured
 *	savearea	r30-r31, DEAR, ESR, xSRR0-1
 *	r30		CR
 *	r31		scratch
 *	r1		kernel stack
 *
 * sprg_sp - SPRG reg containing SP at the time interrupt occured
 * savearea - temp save
 * exc - exception number (EXC_xxx)
 *
 * 1. sets a new frame
 * 2. saves in the frame:
 *   - R0, R1 (SP at the time of interrupt), R2, LR, CR
 *   - R3-31 (R30-31 first restored from savearea)
 *   - XER, CTR, DEAR, ESR (from savearea), xSRR0-1
 *
 * Notes:
 * - potential TLB miss: YES, since we make dereferences to kstack, which
 *   can happen not covered (we can have up to two DTLB misses if fortunate
 *   enough i.e. when kstack crosses page boundary and both pages are
 *   untranslated)
 */
#ifdef __powerpc64__
#define SAVE_REGS(r)							\
	std	%r3, FRAME_3+CALLSIZE(r);				\
	std	%r4, FRAME_4+CALLSIZE(r);				\
	std	%r5, FRAME_5+CALLSIZE(r);				\
	std	%r6, FRAME_6+CALLSIZE(r);				\
	std	%r7, FRAME_7+CALLSIZE(r);				\
	std	%r8, FRAME_8+CALLSIZE(r);				\
	std	%r9, FRAME_9+CALLSIZE(r);				\
	std	%r10, FRAME_10+CALLSIZE(r);				\
	std	%r11, FRAME_11+CALLSIZE(r);				\
	std	%r12, FRAME_12+CALLSIZE(r);				\
	std	%r13, FRAME_13+CALLSIZE(r);				\
	std	%r14, FRAME_14+CALLSIZE(r);				\
	std	%r15, FRAME_15+CALLSIZE(r);				\
	std	%r16, FRAME_16+CALLSIZE(r);				\
	std	%r17, FRAME_17+CALLSIZE(r);				\
	std	%r18, FRAME_18+CALLSIZE(r);				\
	std	%r19, FRAME_19+CALLSIZE(r);				\
	std	%r20, FRAME_20+CALLSIZE(r);				\
	std	%r21, FRAME_21+CALLSIZE(r);				\
	std	%r22, FRAME_22+CALLSIZE(r);				\
	std	%r23, FRAME_23+CALLSIZE(r);				\
	std	%r24, FRAME_24+CALLSIZE(r);				\
	std	%r25, FRAME_25+CALLSIZE(r);				\
	std	%r26, FRAME_26+CALLSIZE(r);				\
	std	%r27, FRAME_27+CALLSIZE(r);				\
	std	%r28, FRAME_28+CALLSIZE(r);				\
	std	%r29, FRAME_29+CALLSIZE(r);				\
	std	%r30, FRAME_30+CALLSIZE(r);				\
	std	%r31, FRAME_31+CALLSIZE(r)
#define LD_REGS(r)							\
	ld	%r3, FRAME_3+CALLSIZE(r);				\
	ld	%r4, FRAME_4+CALLSIZE(r);				\
	ld	%r5, FRAME_5+CALLSIZE(r);				\
	ld	%r6, FRAME_6+CALLSIZE(r);				\
	ld	%r7, FRAME_7+CALLSIZE(r);				\
	ld	%r8, FRAME_8+CALLSIZE(r);				\
	ld	%r9, FRAME_9+CALLSIZE(r);				\
	ld	%r10, FRAME_10+CALLSIZE(r);				\
	ld	%r11, FRAME_11+CALLSIZE(r);				\
	ld	%r12, FRAME_12+CALLSIZE(r);				\
	ld	%r13, FRAME_13+CALLSIZE(r);				\
	ld	%r14, FRAME_14+CALLSIZE(r);				\
	ld	%r15, FRAME_15+CALLSIZE(r);				\
	ld	%r16, FRAME_16+CALLSIZE(r);				\
	ld	%r17, FRAME_17+CALLSIZE(r);				\
	ld	%r18, FRAME_18+CALLSIZE(r);				\
	ld	%r19, FRAME_19+CALLSIZE(r);				\
	ld	%r20, FRAME_20+CALLSIZE(r);				\
	ld	%r21, FRAME_21+CALLSIZE(r);				\
	ld	%r22, FRAME_22+CALLSIZE(r);				\
	ld	%r23, FRAME_23+CALLSIZE(r);				\
	ld	%r24, FRAME_24+CALLSIZE(r);				\
	ld	%r25, FRAME_25+CALLSIZE(r);				\
	ld	%r26, FRAME_26+CALLSIZE(r);				\
	ld	%r27, FRAME_27+CALLSIZE(r);				\
	ld	%r28, FRAME_28+CALLSIZE(r);				\
	ld	%r29, FRAME_29+CALLSIZE(r);				\
	ld	%r30, FRAME_30+CALLSIZE(r);				\
	ld	%r31, FRAME_31+CALLSIZE(r)
#else
#define SAVE_REGS(r)							\
	stmw	%r3,  FRAME_3+CALLSIZE(r)
#define LD_REGS(r)							\
	lmw	%r3,  FRAME_3+CALLSIZE(r)
#endif
#define	FRAME_SETUP(sprg_sp, savearea, exc)				\
	mfspr	%r31, sprg_sp;		/* get saved SP */		\
	/* establish a new stack frame and put everything on it */	\
	STU	%r31, -(FRAMELEN+REDZONE)(%r1);				\
	STORE	%r0, FRAME_0+CALLSIZE(%r1);	/* save r0 in the trapframe */	\
	STORE	%r31, FRAME_1+CALLSIZE(%r1);	/* save SP   "     " */	\
	STORE	%r2, FRAME_2+CALLSIZE(%r1);	/* save r2   "     " */	\
	mflr	%r31;		 					\
	STORE	%r31, FRAME_LR+CALLSIZE(%r1);	/* save LR   "     " */	\
	STORE	%r30, FRAME_CR+CALLSIZE(%r1);	/* save CR   "     " */	\
	GET_CPUINFO(%r2);						\
	LOAD	%r30, (savearea+CPUSAVE_R30)(%r2); /* get saved r30 */	\
	LOAD	%r31, (savearea+CPUSAVE_R31)(%r2); /* get saved r31 */	\
	/* save R3-31 */						\
	SAVE_REGS(%r1);							\
	/* save DEAR, ESR */						\
	LOAD	%r28, (savearea+CPUSAVE_BOOKE_DEAR)(%r2);		\
	LOAD	%r29, (savearea+CPUSAVE_BOOKE_ESR)(%r2);		\
	STORE	%r28, FRAME_BOOKE_DEAR+CALLSIZE(%r1);			\
	STORE	%r29, FRAME_BOOKE_ESR+CALLSIZE(%r1);			\
	/* save XER, CTR, exc number */					\
	mfxer	%r3;							\
	mfctr	%r4;							\
	STORE	%r3, FRAME_XER+CALLSIZE(%r1);				\
	STORE	%r4, FRAME_CTR+CALLSIZE(%r1);				\
	li	%r5, exc;						\
	STORE	%r5, FRAME_EXC+CALLSIZE(%r1);				\
	/* save DBCR0 */						\
	mfspr	%r3, SPR_DBCR0;						\
	STORE	%r3, FRAME_BOOKE_DBCR0+CALLSIZE(%r1);			\
	/* save xSSR0-1 */						\
	LOAD	%r30, (savearea+CPUSAVE_SRR0)(%r2);			\
	LOAD	%r31, (savearea+CPUSAVE_SRR1)(%r2);			\
	STORE	%r30, FRAME_SRR0+CALLSIZE(%r1);				\
	STORE	%r31, FRAME_SRR1+CALLSIZE(%r1);				\
	LOAD	THREAD_REG, PC_CURTHREAD(%r2);				\

/*
 *
 * isrr0-1 - save restore registers to restore CPU state to (may be
 *           SRR0-1, CSRR0-1, MCSRR0-1
 *
 * Notes:
 *  - potential TLB miss: YES. The deref'd kstack may be not covered
 */
#define	FRAME_LEAVE(isrr0, isrr1)					\
	wrteei 0;							\
	/* restore CTR, XER, LR, CR */					\
	LOAD	%r4, FRAME_CTR+CALLSIZE(%r1);				\
	LOAD	%r5, FRAME_XER+CALLSIZE(%r1);				\
	LOAD	%r6, FRAME_LR+CALLSIZE(%r1);				\
	LOAD	%r7, FRAME_CR+CALLSIZE(%r1);				\
	mtctr	%r4;							\
	mtxer	%r5;							\
	mtlr	%r6;							\
	mtcr	%r7;							\
	/* restore DBCR0 */						\
	LOAD	%r4, FRAME_BOOKE_DBCR0+CALLSIZE(%r1);			\
	mtspr	SPR_DBCR0, %r4;						\
	/* restore xSRR0-1 */						\
	LOAD	%r30, FRAME_SRR0+CALLSIZE(%r1);				\
	LOAD	%r31, FRAME_SRR1+CALLSIZE(%r1);				\
	mtspr	isrr0, %r30;						\
	mtspr	isrr1, %r31;						\
	/* restore R2-31, SP */						\
	LD_REGS(%r1);							\
	LOAD	%r2, FRAME_2+CALLSIZE(%r1);				\
	LOAD	%r0, FRAME_0+CALLSIZE(%r1);				\
	LOAD	%r1, FRAME_1+CALLSIZE(%r1);				\
	isync

/*
 * TLB miss prolog
 *
 * saves LR, CR, SRR0-1, R20-31 in the TLBSAVE area
 *
 * Notes:
 *  - potential TLB miss: NO. It is crucial that we do not generate a TLB
 *    miss within the TLB prolog itself!
 *  - TLBSAVE is always translated
 */
#ifdef __powerpc64__
#define	TLB_SAVE_REGS(br)						\
	std	%r20, (TLBSAVE_BOOKE_R20)(br);				\
	std	%r21, (TLBSAVE_BOOKE_R21)(br);				\
	std	%r22, (TLBSAVE_BOOKE_R22)(br);				\
	std	%r23, (TLBSAVE_BOOKE_R23)(br);				\
	std	%r24, (TLBSAVE_BOOKE_R24)(br);				\
	std	%r25, (TLBSAVE_BOOKE_R25)(br);				\
	std	%r26, (TLBSAVE_BOOKE_R26)(br);				\
	std	%r27, (TLBSAVE_BOOKE_R27)(br);				\
	std	%r28, (TLBSAVE_BOOKE_R28)(br);				\
	std	%r29, (TLBSAVE_BOOKE_R29)(br);				\
	std	%r30, (TLBSAVE_BOOKE_R30)(br);				\
	std	%r31, (TLBSAVE_BOOKE_R31)(br);				
#define	TLB_RESTORE_REGS(br)						\
	ld	%r20, (TLBSAVE_BOOKE_R20)(br);				\
	ld	%r21, (TLBSAVE_BOOKE_R21)(br);				\
	ld	%r22, (TLBSAVE_BOOKE_R22)(br);				\
	ld	%r23, (TLBSAVE_BOOKE_R23)(br);				\
	ld	%r24, (TLBSAVE_BOOKE_R24)(br);				\
	ld	%r25, (TLBSAVE_BOOKE_R25)(br);				\
	ld	%r26, (TLBSAVE_BOOKE_R26)(br);				\
	ld	%r27, (TLBSAVE_BOOKE_R27)(br);				\
	ld	%r28, (TLBSAVE_BOOKE_R28)(br);				\
	ld	%r29, (TLBSAVE_BOOKE_R29)(br);				\
	ld	%r30, (TLBSAVE_BOOKE_R30)(br);				\
	ld	%r31, (TLBSAVE_BOOKE_R31)(br);				
#define TLB_NEST(outr,inr)						\
	rlwinm	outr, inr, 7, 22, 24;	/* 8 x TLBSAVE_LEN */
#else
#define TLB_SAVE_REGS(br)						\
	stmw	%r20, TLBSAVE_BOOKE_R20(br)
#define TLB_RESTORE_REGS(br)						\
	lmw	%r20, TLBSAVE_BOOKE_R20(br)
#define TLB_NEST(outr,inr)						\
	rlwinm	outr, inr, 6, 23, 25;	/* 4 x TLBSAVE_LEN */
#endif
#define TLB_PROLOG							\
	mtsprg4	%r1;			/* Save SP */			\
	mtsprg5 %r28;							\
	mtsprg6 %r29;							\
	/* calculate TLB nesting level and TLBSAVE instance address */	\
	GET_CPUINFO(%r1);	 	/* Per-cpu structure */		\
	LOAD	%r28, PC_BOOKE_TLB_LEVEL(%r1);				\
	TLB_NEST(%r29,%r28);						\
	addi	%r28, %r28, 1;						\
	STORE	%r28, PC_BOOKE_TLB_LEVEL(%r1);				\
	addi	%r29, %r29, PC_BOOKE_TLBSAVE@l; 			\
	add	%r1, %r1, %r29;		/* current TLBSAVE ptr */	\
									\
	/* save R20-31 */						\
	mfsprg5 %r28;		 					\
	mfsprg6 %r29;							\
	TLB_SAVE_REGS(%r1);			\
	/* save LR, CR */						\
	mflr	%r30;		 					\
	mfcr	%r31;							\
	STORE	%r30, (TLBSAVE_BOOKE_LR)(%r1);				\
	STORE	%r31, (TLBSAVE_BOOKE_CR)(%r1);				\
	/* save SRR0-1 */						\
	mfsrr0	%r30;		/* execution addr at interrupt time */	\
	mfsrr1	%r31;		/* MSR at interrupt time*/		\
	STORE	%r30, (TLBSAVE_BOOKE_SRR0)(%r1);	/* save SRR0 */	\
	STORE	%r31, (TLBSAVE_BOOKE_SRR1)(%r1);	/* save SRR1 */	\
	isync;								\
	mfsprg4	%r1

/*
 * restores LR, CR, SRR0-1, R20-31 from the TLBSAVE area
 *
 * same notes as for the TLB_PROLOG
 */
#define TLB_RESTORE							\
	mtsprg4	%r1;			/* Save SP */			\
	GET_CPUINFO(%r1);	 	/* Per-cpu structure */		\
	/* calculate TLB nesting level and TLBSAVE instance addr */	\
	LOAD	%r28, PC_BOOKE_TLB_LEVEL(%r1);				\
	subi	%r28, %r28, 1;						\
	STORE	%r28, PC_BOOKE_TLB_LEVEL(%r1);				\
	TLB_NEST(%r29,%r28);						\
	addi	%r29, %r29, PC_BOOKE_TLBSAVE@l;				\
	add	%r1, %r1, %r29;						\
									\
	/* restore LR, CR */						\
	LOAD	%r30, (TLBSAVE_BOOKE_LR)(%r1);				\
	LOAD	%r31, (TLBSAVE_BOOKE_CR)(%r1);				\
	mtlr	%r30;							\
	mtcr	%r31;							\
	/* restore SRR0-1 */						\
	LOAD	%r30, (TLBSAVE_BOOKE_SRR0)(%r1);			\
	LOAD	%r31, (TLBSAVE_BOOKE_SRR1)(%r1);			\
	mtsrr0	%r30;							\
	mtsrr1	%r31;							\
	/* restore R20-31 */						\
	TLB_RESTORE_REGS(%r1);						\
	mfsprg4	%r1

#ifdef SMP
#define TLB_LOCK							\
	GET_CPUINFO(%r20);						\
	LOAD	%r21, PC_CURTHREAD(%r20);				\
	LOAD	%r22, PC_BOOKE_TLB_LOCK(%r20);				\
									\
1:	LOADX	%r23, 0, %r22;						\
	CMPI	%r23, TLB_UNLOCKED;					\
	beq	2f;							\
									\
	/* check if this is recursion */				\
	CMPL	cr0, %r21, %r23;					\
	bne-	1b;							\
									\
2:	/* try to acquire lock */					\
	STOREX	%r21, 0, %r22;						\
	bne-	1b;							\
									\
	/* got it, update recursion counter */				\
	lwz	%r21, RES_RECURSE(%r22);				\
	addi	%r21, %r21, 1;						\
	stw	%r21, RES_RECURSE(%r22);				\
	isync;								\
	msync

#define TLB_UNLOCK							\
	GET_CPUINFO(%r20);						\
	LOAD	%r21, PC_CURTHREAD(%r20);				\
	LOAD	%r22, PC_BOOKE_TLB_LOCK(%r20);				\
									\
	/* update recursion counter */					\
	lwz	%r23, RES_RECURSE(%r22);				\
	subi	%r23, %r23, 1;						\
	stw	%r23, RES_RECURSE(%r22);				\
									\
	cmplwi	%r23, 0;						\
	bne	1f;							\
	isync;								\
	msync;								\
									\
	/* release the lock */						\
	li	%r23, TLB_UNLOCKED;					\
	STORE	%r23, 0(%r22);						\
1:	isync;								\
	msync
#else
#define TLB_LOCK
#define TLB_UNLOCK
#endif	/* SMP */

#define INTERRUPT(label)						\
	.globl	label;							\
	.align	5;							\
	CNAME(label):

/*
 * Interrupt handling routines in BookE can be flexibly placed and do not have
 * to live in pre-defined vectors location. Note they need to be TLB-mapped at
 * all times in order to be able to handle exceptions. We thus arrange for
 * them to be part of kernel text which is always TLB-accessible.
 *
 * The interrupt handling routines have to be 16 bytes aligned: we align them
 * to 32 bytes (cache line length) which supposedly performs better.
 *
 */
	.text
	.globl CNAME(interrupt_vector_base)
	.align 5
interrupt_vector_base:
/*****************************************************************************
 * Catch-all handler to handle uninstalled IVORs
 ****************************************************************************/
INTERRUPT(int_unknown)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_RSVD)
	b	trap_common

/*****************************************************************************
 * Critical input interrupt
 ****************************************************************************/
INTERRUPT(int_critical_input)
	STANDARD_CRIT_PROLOG(SPR_SPRG2, PC_BOOKE_CRITSAVE, SPR_CSRR0, SPR_CSRR1)
	FRAME_SETUP(SPR_SPRG2, PC_BOOKE_CRITSAVE, EXC_CRIT)
	GET_TOCBASE(%r2)
	addi	%r3, %r1, CALLSIZE
	bl	CNAME(powerpc_interrupt)
	TOC_RESTORE
	FRAME_LEAVE(SPR_CSRR0, SPR_CSRR1)
	rfci


/*****************************************************************************
 * Machine check interrupt
 ****************************************************************************/
INTERRUPT(int_machine_check)
	STANDARD_PROLOG(SPR_SPRG3, PC_BOOKE_MCHKSAVE, SPR_MCSRR0, SPR_MCSRR1)
	FRAME_SETUP(SPR_SPRG3, PC_BOOKE_MCHKSAVE, EXC_MCHK)
	GET_TOCBASE(%r2)
	addi	%r3, %r1, CALLSIZE
	bl	CNAME(powerpc_interrupt)
	TOC_RESTORE
	FRAME_LEAVE(SPR_MCSRR0, SPR_MCSRR1)
	rfmci


/*****************************************************************************
 * Data storage interrupt
 ****************************************************************************/
INTERRUPT(int_data_storage)
	STANDARD_PROLOG(SPR_SPRG1, PC_DISISAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_DISISAVE, EXC_DSI)
	b	trap_common


/*****************************************************************************
 * Instruction storage interrupt
 ****************************************************************************/
INTERRUPT(int_instr_storage)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_ISI)
	b	trap_common


/*****************************************************************************
 * External input interrupt
 ****************************************************************************/
INTERRUPT(int_external_input)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_EXI)
	b	trap_common


INTERRUPT(int_alignment)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_ALI)
	b	trap_common


INTERRUPT(int_program)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_PGM)
	b	trap_common


INTERRUPT(int_fpu)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_FPU)
	b	trap_common


/*****************************************************************************
 * System call
 ****************************************************************************/
INTERRUPT(int_syscall)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_SC)
	b	trap_common


/*****************************************************************************
 * Decrementer interrupt
 ****************************************************************************/
INTERRUPT(int_decrementer)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_DECR)
	b	trap_common


/*****************************************************************************
 * Fixed interval timer
 ****************************************************************************/
INTERRUPT(int_fixed_interval_timer)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_FIT)
	b	trap_common


/*****************************************************************************
 * Watchdog interrupt
 ****************************************************************************/
INTERRUPT(int_watchdog)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_WDOG)
	b	trap_common


/*****************************************************************************
 * Altivec Unavailable interrupt
 ****************************************************************************/
INTERRUPT(int_vec)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_VEC)
	b	trap_common


/*****************************************************************************
 * Altivec Assist interrupt
 ****************************************************************************/
INTERRUPT(int_vecast)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_VECAST_E)
	b	trap_common


#ifdef __SPE__
/*****************************************************************************
 * Floating point Assist interrupt
 ****************************************************************************/
INTERRUPT(int_spe_fpdata)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_SPFPD)
	addi	%r3, %r1, CALLSIZE
	bl	spe_handle_fpdata
	FRAME_LEAVE(SPR_SRR0, SPR_SRR1)
	rfi

INTERRUPT(int_spe_fpround)
	STANDARD_PROLOG(SPR_SPRG1, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, PC_TEMPSAVE, EXC_SPFPR)
	addi	%r3, %r1, CALLSIZE
	bl	spe_handle_fpround
	FRAME_LEAVE(SPR_SRR0, SPR_SRR1)
	rfi
#endif


#ifdef HWPMC_HOOKS
/*****************************************************************************
 * PMC Interrupt
 ****************************************************************************/
INTERRUPT(int_performance_counter)
	STANDARD_PROLOG(SPR_SPRG3, PC_TEMPSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG3, PC_TEMPSAVE, EXC_PERF)
	b	trap_common
#endif


/*****************************************************************************
 * Data TLB miss interrupt
 *
 * There can be nested TLB misses - while handling a TLB miss we reference
 * data structures that may be not covered by translations. We support up to
 * TLB_NESTED_MAX-1 nested misses.
 * 
 * Registers use:
 *	r31 - dear
 *	r30 - unused
 *	r29 - saved mas0
 *	r28 - saved mas1
 *	r27 - saved mas2
 *	r26 - pmap address
 *	r25 - pte address
 *
 *	r20:r23 - scratch registers
 ****************************************************************************/
INTERRUPT(int_data_tlb_error)
	TLB_PROLOG
	TLB_LOCK

	mfdear	%r31

	/*
	 * Save MAS0-MAS2 registers. There might be another tlb miss during
	 * pte lookup overwriting current contents (which was hw filled).
	 */
	mfspr	%r29, SPR_MAS0
	mfspr	%r28, SPR_MAS1
	mfspr	%r27, SPR_MAS2

	/* Check faulting address. */
	LOAD_ADDR(%r21, VM_MAXUSER_ADDRESS)
	CMPL	cr0, %r31, %r21
	blt	search_user_pmap
	
	/* If it's kernel address, allow only supervisor mode misses. */
	mfsrr1	%r21
	mtcr	%r21
	bt	17, search_failed	/* check MSR[PR] */

search_kernel_pmap:
	/* Load r26 with kernel_pmap address */
	bl	1f
#ifdef __powerpc64__
	.llong kernel_pmap_store-.
#else
	.long kernel_pmap_store-.
#endif
1:	mflr	%r21
	LOAD	%r26, 0(%r21)
	add	%r26, %r21, %r26	/* kernel_pmap_store in r26 */

	/* Force kernel tid, set TID to 0 in MAS1. */
	li	%r21, 0
	rlwimi	%r28, %r21, 0, 8, 15	/* clear TID bits */

tlb_miss_handle:
	/* This may result in nested tlb miss. */
	bl	pte_lookup		/* returns PTE address in R25 */

	CMPI	%r25, 0			/* pte found? */
	beq	search_failed

	/* Finish up, write TLB entry. */
	bl	tlb_fill_entry

tlb_miss_return:
	TLB_UNLOCK
	TLB_RESTORE
	rfi

search_user_pmap:
	/* Load r26 with current user space process pmap */
	GET_CPUINFO(%r26)
	LOAD	%r26, PC_CURPMAP(%r26)

	b	tlb_miss_handle

search_failed:
	/*
	 * Whenever we don't find a TLB mapping in PT, set a TLB0 entry with
	 * the faulting virtual address anyway, but put a fake RPN and no
	 * access rights. This should cause a following {D,I}SI exception.
	 */
	lis	%r23, 0xffff0000@h	/* revoke all permissions */

	/* Load MAS registers. */
	mtspr	SPR_MAS0, %r29
	mtspr	SPR_MAS1, %r28
	mtspr	SPR_MAS2, %r27
	mtspr	SPR_MAS3, %r23

	bl	zero_mas7
	bl	zero_mas8

	isync
	tlbwe
	msync
	isync
	b	tlb_miss_return

/*****************************************************************************
 *
 * Return pte address that corresponds to given pmap/va.  If there is no valid
 * entry return 0.
 *
 * input: r26 - pmap
 * input: r31 - dear
 * output: r25 - pte address
 *
 * scratch regs used: r21
 *
 ****************************************************************************/
pte_lookup:
	CMPI	%r26, 0
	beq	1f			/* fail quickly if pmap is invalid */

#ifdef __powerpc64__
	rldicl  %r21, %r31, (64 - PP2D_L_L), (64 - PP2D_L_NUM) /* pp2d offset */
	rldicl  %r25, %r31, (64 - PP2D_H_L), (64 - PP2D_H_NUM)
	rldimi  %r21, %r25, PP2D_L_NUM, (64 - (PP2D_L_NUM + PP2D_H_NUM))
	slwi    %r21, %r21, PP2D_ENTRY_SHIFT	/* multiply by pp2d entry size */
	addi    %r25, %r26, PM_PP2D		/* pmap pm_pp2d[] address */
	add     %r25, %r25, %r21		/* offset within pm_pp2d[] table */
	ld      %r25, 0(%r25)			/* get pdir address, i.e.  pmap->pm_pp2d[pp2d_idx] * */

	cmpdi   %r25, 0
	beq 1f

#if PAGE_SIZE < 65536
	rldicl  %r21, %r31, (64 - PDIR_L), (64 - PDIR_NUM)      /* pdir offset */
	slwi    %r21, %r21, PDIR_ENTRY_SHIFT    /* multiply by pdir entry size */
	add     %r25, %r25, %r21                /* offset within pdir table */
	ld      %r25, 0(%r25)                   /* get ptbl address, i.e.  pmap->pm_pp2d[pp2d_idx][pdir_idx] */

	cmpdi   %r25, 0
	beq     1f
#endif

	rldicl  %r21, %r31, (64 - PTBL_L), (64 - PTBL_NUM) /* ptbl offset */
	slwi    %r21, %r21, PTBL_ENTRY_SHIFT   /* multiply by pte entry size */

#else
	srwi	%r21, %r31, PDIR_SHIFT		/* pdir offset */
	slwi	%r21, %r21, PDIR_ENTRY_SHIFT	/* multiply by pdir entry size */

	addi	%r25, %r26, PM_PDIR	/* pmap pm_dir[] address */
	add	%r25, %r25, %r21	/* offset within pm_pdir[] table */
	/*
	 * Get ptbl address, i.e. pmap->pm_pdir[pdir_idx]
	 * This load may cause a Data TLB miss for non-kernel pmap!
	 */
	LOAD	%r25, 0(%r25)
	CMPI	%r25, 0
	beq	2f

	lis	%r21, PTBL_MASK@h
	ori	%r21, %r21, PTBL_MASK@l
	and	%r21, %r21, %r31

	/* ptbl offset, multiply by ptbl entry size */
	srwi	%r21, %r21, (PTBL_SHIFT - PTBL_ENTRY_SHIFT)
#endif

	add	%r25, %r25, %r21		/* address of pte entry */
	/*
	 * Get pte->flags
	 * This load may cause a Data TLB miss for non-kernel pmap!
	 */
	lwz	%r21, PTE_FLAGS(%r25)
	andi.	%r21, %r21, PTE_VALID@l
	bne	2f
1:
	li	%r25, 0
2:
	blr

/*****************************************************************************
 *
 * Load MAS1-MAS3 registers with data, write TLB entry
 *
 * input:
 * r29 - mas0
 * r28 - mas1
 * r27 - mas2
 * r25 - pte
 *
 * output: none
 *
 * scratch regs: r21-r23
 *
 ****************************************************************************/
tlb_fill_entry:
	/*
	 * Update PTE flags: we have to do it atomically, as pmap_protect()
	 * running on other CPUs could attempt to update the flags at the same
	 * time.
	 */
	li	%r23, PTE_FLAGS
1:
	lwarx	%r21, %r23, %r25		/* get pte->flags */
	oris	%r21, %r21, PTE_REFERENCED@h	/* set referenced bit */

	andi.	%r22, %r21, (PTE_SW | PTE_UW)@l	/* check if writable */
	beq	2f
	ori	%r21, %r21, PTE_MODIFIED@l	/* set modified bit */
2:
	stwcx.	%r21, %r23, %r25		/* write it back */
	bne-	1b

	/* Update MAS2. */
	rlwimi	%r27, %r21, 13, 27, 30		/* insert WIMG bits from pte */

	/* Setup MAS3 value in r23. */
	LOAD	%r23, PTE_RPN(%r25)		/* get pte->rpn */
#ifdef __powerpc64__
	rldicr	%r22, %r23, 52, 51		/* extract MAS3 portion of RPN */
	rldicl	%r23, %r23, 20, 54		/* extract MAS7 portion of RPN */

	rlwimi	%r22, %r21, 30, 26, 31		/* insert protection bits from pte */
#else
	rlwinm	%r22, %r23, 20, 0, 11		/* extract MAS3 portion of RPN */

	rlwimi	%r22, %r21, 30, 26, 31		/* insert protection bits from pte */
	rlwimi	%r22, %r21, 20, 12, 19		/* insert lower 8 RPN bits to MAS3 */
	rlwinm	%r23, %r23, 20, 24, 31		/* MAS7 portion of RPN */
#endif

	/* Load MAS registers. */
	mtspr	SPR_MAS0, %r29
	mtspr	SPR_MAS1, %r28
	mtspr	SPR_MAS2, %r27
	mtspr	SPR_MAS3, %r22
	mtspr	SPR_MAS7, %r23

	mflr	%r21
	bl	zero_mas8
	mtlr	%r21

	isync
	tlbwe
	isync
	msync
	blr

/*****************************************************************************
 * Instruction TLB miss interrupt
 *
 * Same notes as for the Data TLB miss
 ****************************************************************************/
INTERRUPT(int_inst_tlb_error)
	TLB_PROLOG
	TLB_LOCK

	mfsrr0	%r31			/* faulting address */

	/*
	 * Save MAS0-MAS2 registers. There might be another tlb miss during pte
	 * lookup overwriting current contents (which was hw filled).
	 */
	mfspr	%r29, SPR_MAS0
	mfspr	%r28, SPR_MAS1
	mfspr	%r27, SPR_MAS2

	mfsrr1	%r21
	mtcr	%r21

	/* check MSR[PR] */
	bt	17, search_user_pmap
	b	search_kernel_pmap


	.globl	interrupt_vector_top
interrupt_vector_top:

/*****************************************************************************
 * Debug interrupt
 ****************************************************************************/
INTERRUPT(int_debug)
	STANDARD_CRIT_PROLOG(SPR_SPRG2, PC_BOOKE_CRITSAVE, SPR_CSRR0, SPR_CSRR1)
	FRAME_SETUP(SPR_SPRG2, PC_BOOKE_CRITSAVE, EXC_DEBUG)
	bl	int_debug_int
	FRAME_LEAVE(SPR_CSRR0, SPR_CSRR1)
	rfci

INTERRUPT(int_debug_ed)
	STANDARD_CRIT_PROLOG(SPR_SPRG2, PC_BOOKE_CRITSAVE, SPR_DSRR0, SPR_DSRR1)
	FRAME_SETUP(SPR_SPRG2, PC_BOOKE_CRITSAVE, EXC_DEBUG)
	bl	int_debug_int
	FRAME_LEAVE(SPR_DSRR0, SPR_DSRR1)
	rfdi
	/* .long 0x4c00004e */

/* Internal helper for debug interrupt handling. */
/* Common code between e500v1/v2 and e500mc-based cores. */
int_debug_int:
	mflr	%r14
	GET_CPUINFO(%r3)
	LOAD	%r3, (PC_BOOKE_CRITSAVE+CPUSAVE_SRR0)(%r3)
	bl	0f
	ADDR(interrupt_vector_base-.)
	ADDR(interrupt_vector_top-.)
0:	mflr	%r5
	LOAD	%r4,0(%r5)	/* interrupt_vector_base in r4 */
	add	%r4,%r4,%r5
	CMPL	cr0, %r3, %r4
	blt	trap_common
	LOAD	%r4,WORD_SIZE(%r5)	/* interrupt_vector_top in r4 */
	add	%r4,%r4,%r5
	addi	%r4,%r4,4
	CMPL	cr0, %r3, %r4
	bge	trap_common
	/* Disable single-stepping for the interrupt handlers. */
	LOAD	%r3, FRAME_SRR1+CALLSIZE(%r1);
	rlwinm	%r3, %r3, 0, 23, 21
	STORE	%r3, FRAME_SRR1+CALLSIZE(%r1);
	/* Restore srr0 and srr1 as they could have been clobbered. */
	GET_CPUINFO(%r4)
	LOAD	%r3, (PC_BOOKE_CRITSAVE+BOOKE_CRITSAVE_SRR0)(%r4);
	mtspr	SPR_SRR0, %r3
	LOAD	%r4, (PC_BOOKE_CRITSAVE+BOOKE_CRITSAVE_SRR1)(%r4);
	mtspr	SPR_SRR1, %r4
	mtlr	%r14
	blr

/*****************************************************************************
 * Common trap code
 ****************************************************************************/
trap_common:
	/* Call C trap dispatcher */
	GET_TOCBASE(%r2)
	addi	%r3, %r1, CALLSIZE
	bl	CNAME(powerpc_interrupt)
	TOC_RESTORE

	.globl	CNAME(trapexit)		/* exported for db_backtrace use */
CNAME(trapexit):
	/* disable interrupts */
	wrteei	0

	/* Test AST pending - makes sense for user process only */
	LOAD	%r5, FRAME_SRR1+CALLSIZE(%r1)
	mtcr	%r5
	bf	17, 1f

	GET_CPUINFO(%r3)
	LOAD	%r4, PC_CURTHREAD(%r3)
	lwz	%r4, TD_FLAGS(%r4)
	lis	%r5, (TDF_ASTPENDING | TDF_NEEDRESCHED)@h
	ori	%r5, %r5, (TDF_ASTPENDING | TDF_NEEDRESCHED)@l
	and.	%r4, %r4, %r5
	beq	1f

	/* re-enable interrupts before calling ast() */
	wrteei	1

	addi	%r3, %r1, CALLSIZE
	bl	CNAME(ast)
	TOC_RESTORE
	.globl	CNAME(asttrapexit)	/* db_backtrace code sentinel #2 */
CNAME(asttrapexit):
	b	trapexit		/* test ast ret value ? */
1:
	FRAME_LEAVE(SPR_SRR0, SPR_SRR1)
	rfi


#if defined(KDB)
/*
 * Deliberate entry to dbtrap
 */
	/* .globl	CNAME(breakpoint)*/
ASENTRY_NOPROF(breakpoint)
	mtsprg1	%r1
	mfmsr	%r3
	mtsrr1	%r3
	li	%r4, ~(PSL_EE | PSL_ME)@l
	oris	%r4, %r4, ~(PSL_EE | PSL_ME)@h
	and	%r3, %r3, %r4
	mtmsr	%r3			/* disable interrupts */
	isync
	GET_CPUINFO(%r3)
	STORE	%r30, (PC_DBSAVE+CPUSAVE_R30)(%r3)
	STORE	%r31, (PC_DBSAVE+CPUSAVE_R31)(%r3)

	mflr	%r31
	mtsrr0	%r31

	mfdear	%r30
	mfesr	%r31
	STORE	%r30, (PC_DBSAVE+CPUSAVE_BOOKE_DEAR)(%r3)
	STORE	%r31, (PC_DBSAVE+CPUSAVE_BOOKE_ESR)(%r3)

	mfsrr0	%r30
	mfsrr1	%r31
	STORE	%r30, (PC_DBSAVE+CPUSAVE_SRR0)(%r3)
	STORE	%r31, (PC_DBSAVE+CPUSAVE_SRR1)(%r3)
	isync

	mfcr	%r30

/*
 * Now the kdb trap catching code.
 */
dbtrap:
	FRAME_SETUP(SPR_SPRG1, PC_DBSAVE, EXC_DEBUG)
/* Call C trap code: */
	GET_TOCBASE(%r2)
	addi	%r3, %r1, CALLSIZE
	bl	CNAME(db_trap_glue)
	TOC_RESTORE
	or.	%r3, %r3, %r3
	bne	dbleave
/* This wasn't for KDB, so switch to real trap: */
	b	trap_common

dbleave:
	FRAME_LEAVE(SPR_SRR0, SPR_SRR1)
	rfi
#endif /* KDB */

#ifdef SMP
ENTRY(tlb_lock)
	GET_CPUINFO(%r5)
	LOAD	%r5, PC_CURTHREAD(%r5)
1:	LOADX	%r4, 0, %r3
	CMPI	%r4, TLB_UNLOCKED
	bne	1b
	STOREX	%r5, 0, %r3
	bne-	1b
	isync
	msync
	blr

ENTRY(tlb_unlock)
	isync
	msync
	li	%r4, TLB_UNLOCKED
	STORE	%r4, 0(%r3)
	isync
	msync
	blr

/*
 * TLB miss spin locks. For each CPU we have a reservation granule (32 bytes);
 * only a single word from this granule will actually be used as a spin lock
 * for mutual exclusion between TLB miss handler and pmap layer that
 * manipulates page table contents.
 */
	.data
	.align	5
GLOBAL(tlb0_miss_locks)
	.space	RES_GRANULE * MAXCPU
#endif
