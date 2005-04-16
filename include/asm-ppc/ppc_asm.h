/*
 * include/asm-ppc/ppc_asm.h
 *
 * Definitions used by various bits of low-level assembly code on PowerPC.
 *
 * Copyright (C) 1995-1999 Gary Thomas, Paul Mackerras, Cort Dougan.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>

/*
 * Macros for storing registers into and loading registers from
 * exception frames.
 */
#define SAVE_GPR(n, base)	stw	n,GPR0+4*(n)(base)
#define SAVE_2GPRS(n, base)	SAVE_GPR(n, base); SAVE_GPR(n+1, base)
#define SAVE_4GPRS(n, base)	SAVE_2GPRS(n, base); SAVE_2GPRS(n+2, base)
#define SAVE_8GPRS(n, base)	SAVE_4GPRS(n, base); SAVE_4GPRS(n+4, base)
#define SAVE_10GPRS(n, base)	SAVE_8GPRS(n, base); SAVE_2GPRS(n+8, base)
#define REST_GPR(n, base)	lwz	n,GPR0+4*(n)(base)
#define REST_2GPRS(n, base)	REST_GPR(n, base); REST_GPR(n+1, base)
#define REST_4GPRS(n, base)	REST_2GPRS(n, base); REST_2GPRS(n+2, base)
#define REST_8GPRS(n, base)	REST_4GPRS(n, base); REST_4GPRS(n+4, base)
#define REST_10GPRS(n, base)	REST_8GPRS(n, base); REST_2GPRS(n+8, base)

#define SAVE_NVGPRS(base)	SAVE_GPR(13, base); SAVE_8GPRS(14, base); \
				SAVE_10GPRS(22, base)
#define REST_NVGPRS(base)	REST_GPR(13, base); REST_8GPRS(14, base); \
				REST_10GPRS(22, base)

#define SAVE_FPR(n, base)	stfd	n,THREAD_FPR0+8*(n)(base)
#define SAVE_2FPRS(n, base)	SAVE_FPR(n, base); SAVE_FPR(n+1, base)
#define SAVE_4FPRS(n, base)	SAVE_2FPRS(n, base); SAVE_2FPRS(n+2, base)
#define SAVE_8FPRS(n, base)	SAVE_4FPRS(n, base); SAVE_4FPRS(n+4, base)
#define SAVE_16FPRS(n, base)	SAVE_8FPRS(n, base); SAVE_8FPRS(n+8, base)
#define SAVE_32FPRS(n, base)	SAVE_16FPRS(n, base); SAVE_16FPRS(n+16, base)
#define REST_FPR(n, base)	lfd	n,THREAD_FPR0+8*(n)(base)
#define REST_2FPRS(n, base)	REST_FPR(n, base); REST_FPR(n+1, base)
#define REST_4FPRS(n, base)	REST_2FPRS(n, base); REST_2FPRS(n+2, base)
#define REST_8FPRS(n, base)	REST_4FPRS(n, base); REST_4FPRS(n+4, base)
#define REST_16FPRS(n, base)	REST_8FPRS(n, base); REST_8FPRS(n+8, base)
#define REST_32FPRS(n, base)	REST_16FPRS(n, base); REST_16FPRS(n+16, base)

#define SAVE_VR(n,b,base)	li b,THREAD_VR0+(16*(n));  stvx n,b,base
#define SAVE_2VR(n,b,base)	SAVE_VR(n,b,base); SAVE_VR(n+1,b,base)
#define SAVE_4VR(n,b,base)	SAVE_2VR(n,b,base); SAVE_2VR(n+2,b,base)
#define SAVE_8VR(n,b,base)	SAVE_4VR(n,b,base); SAVE_4VR(n+4,b,base)
#define SAVE_16VR(n,b,base)	SAVE_8VR(n,b,base); SAVE_8VR(n+8,b,base)
#define SAVE_32VR(n,b,base)	SAVE_16VR(n,b,base); SAVE_16VR(n+16,b,base)
#define REST_VR(n,b,base)	li b,THREAD_VR0+(16*(n)); lvx n,b,base
#define REST_2VR(n,b,base)	REST_VR(n,b,base); REST_VR(n+1,b,base)
#define REST_4VR(n,b,base)	REST_2VR(n,b,base); REST_2VR(n+2,b,base)
#define REST_8VR(n,b,base)	REST_4VR(n,b,base); REST_4VR(n+4,b,base)
#define REST_16VR(n,b,base)	REST_8VR(n,b,base); REST_8VR(n+8,b,base)
#define REST_32VR(n,b,base)	REST_16VR(n,b,base); REST_16VR(n+16,b,base)

#define SAVE_EVR(n,s,base)	evmergehi s,s,n; stw s,THREAD_EVR0+4*(n)(base)
#define SAVE_2EVR(n,s,base)	SAVE_EVR(n,s,base); SAVE_EVR(n+1,s,base)
#define SAVE_4EVR(n,s,base)	SAVE_2EVR(n,s,base); SAVE_2EVR(n+2,s,base)
#define SAVE_8EVR(n,s,base)	SAVE_4EVR(n,s,base); SAVE_4EVR(n+4,s,base)
#define SAVE_16EVR(n,s,base)	SAVE_8EVR(n,s,base); SAVE_8EVR(n+8,s,base)
#define SAVE_32EVR(n,s,base)	SAVE_16EVR(n,s,base); SAVE_16EVR(n+16,s,base)

#define REST_EVR(n,s,base)	lwz s,THREAD_EVR0+4*(n)(base); evmergelo n,s,n
#define REST_2EVR(n,s,base)	REST_EVR(n,s,base); REST_EVR(n+1,s,base)
#define REST_4EVR(n,s,base)	REST_2EVR(n,s,base); REST_2EVR(n+2,s,base)
#define REST_8EVR(n,s,base)	REST_4EVR(n,s,base); REST_4EVR(n+4,s,base)
#define REST_16EVR(n,s,base)	REST_8EVR(n,s,base); REST_8EVR(n+8,s,base)
#define REST_32EVR(n,s,base)	REST_16EVR(n,s,base); REST_16EVR(n+16,s,base)

#ifdef CONFIG_PPC601_SYNC_FIX
#define SYNC				\
BEGIN_FTR_SECTION			\
	sync;				\
	isync;				\
END_FTR_SECTION_IFSET(CPU_FTR_601)
#define SYNC_601			\
BEGIN_FTR_SECTION			\
	sync;				\
END_FTR_SECTION_IFSET(CPU_FTR_601)
#define ISYNC_601			\
BEGIN_FTR_SECTION			\
	isync;				\
END_FTR_SECTION_IFSET(CPU_FTR_601)
#else
#define	SYNC
#define SYNC_601
#define ISYNC_601
#endif

#ifndef CONFIG_SMP
#define TLBSYNC
#else /* CONFIG_SMP */
/* tlbsync is not implemented on 601 */
#define TLBSYNC				\
BEGIN_FTR_SECTION			\
	tlbsync;			\
	sync;				\
END_FTR_SECTION_IFCLR(CPU_FTR_601)
#endif

/*
 * This instruction is not implemented on the PPC 603 or 601; however, on
 * the 403GCX and 405GP tlbia IS defined and tlbie is not.
 * All of these instructions exist in the 8xx, they have magical powers,
 * and they must be used.
 */

#if !defined(CONFIG_4xx) && !defined(CONFIG_8xx)
#define tlbia					\
	li	r4,1024;			\
	mtctr	r4;				\
	lis	r4,KERNELBASE@h;		\
0:	tlbie	r4;				\
	addi	r4,r4,0x1000;			\
	bdnz	0b
#endif

#ifdef CONFIG_BOOKE
#define tophys(rd,rs)				\
	addis	rd,rs,0

#define tovirt(rd,rs)				\
	addis	rd,rs,0

#else  /* CONFIG_BOOKE */
/*
 * On APUS (Amiga PowerPC cpu upgrade board), we don't know the
 * physical base address of RAM at compile time.
 */
#define tophys(rd,rs)				\
0:	addis	rd,rs,-KERNELBASE@h;		\
	.section ".vtop_fixup","aw";		\
	.align  1;				\
	.long   0b;				\
	.previous

#define tovirt(rd,rs)				\
0:	addis	rd,rs,KERNELBASE@h;		\
	.section ".ptov_fixup","aw";		\
	.align  1;				\
	.long   0b;				\
	.previous
#endif  /* CONFIG_BOOKE */

/*
 * On 64-bit cpus, we use the rfid instruction instead of rfi, but
 * we then have to make sure we preserve the top 32 bits except for
 * the 64-bit mode bit, which we clear.
 */
#ifdef CONFIG_PPC64BRIDGE
#define	FIX_SRR1(ra, rb)	\
	mr	rb,ra;		\
	mfmsr	ra;		\
	clrldi	ra,ra,1;		/* turn off 64-bit mode */ \
	rldimi	ra,rb,0,32
#define	RFI		.long	0x4c000024	/* rfid instruction */
#define MTMSRD(r)	.long	(0x7c000164 + ((r) << 21))	/* mtmsrd */
#define CLR_TOP32(r)	rlwinm	(r),(r),0,0,31	/* clear top 32 bits */

#else
#define FIX_SRR1(ra, rb)
#ifndef CONFIG_40x
#define	RFI		rfi
#else
#define RFI		rfi; b .	/* Prevent prefetch past rfi */
#endif
#define MTMSRD(r)	mtmsr	r
#define CLR_TOP32(r)
#endif /* CONFIG_PPC64BRIDGE */

#define RFMCI		.long 0x4c00004c	/* rfmci instruction */

#ifdef CONFIG_IBM405_ERR77
#define PPC405_ERR77(ra,rb)	dcbt	ra, rb;
#define	PPC405_ERR77_SYNC	sync;
#else
#define PPC405_ERR77(ra,rb)
#define PPC405_ERR77_SYNC
#endif

/* The boring bits... */

/* Condition Register Bit Fields */

#define	cr0	0
#define	cr1	1
#define	cr2	2
#define	cr3	3
#define	cr4	4
#define	cr5	5
#define	cr6	6
#define	cr7	7


/* General Purpose Registers (GPRs) */

#define	r0	0
#define	r1	1
#define	r2	2
#define	r3	3
#define	r4	4
#define	r5	5
#define	r6	6
#define	r7	7
#define	r8	8
#define	r9	9
#define	r10	10
#define	r11	11
#define	r12	12
#define	r13	13
#define	r14	14
#define	r15	15
#define	r16	16
#define	r17	17
#define	r18	18
#define	r19	19
#define	r20	20
#define	r21	21
#define	r22	22
#define	r23	23
#define	r24	24
#define	r25	25
#define	r26	26
#define	r27	27
#define	r28	28
#define	r29	29
#define	r30	30
#define	r31	31


/* Floating Point Registers (FPRs) */

#define	fr0	0
#define	fr1	1
#define	fr2	2
#define	fr3	3
#define	fr4	4
#define	fr5	5
#define	fr6	6
#define	fr7	7
#define	fr8	8
#define	fr9	9
#define	fr10	10
#define	fr11	11
#define	fr12	12
#define	fr13	13
#define	fr14	14
#define	fr15	15
#define	fr16	16
#define	fr17	17
#define	fr18	18
#define	fr19	19
#define	fr20	20
#define	fr21	21
#define	fr22	22
#define	fr23	23
#define	fr24	24
#define	fr25	25
#define	fr26	26
#define	fr27	27
#define	fr28	28
#define	fr29	29
#define	fr30	30
#define	fr31	31

#define	vr0	0
#define	vr1	1
#define	vr2	2
#define	vr3	3
#define	vr4	4
#define	vr5	5
#define	vr6	6
#define	vr7	7
#define	vr8	8
#define	vr9	9
#define	vr10	10
#define	vr11	11
#define	vr12	12
#define	vr13	13
#define	vr14	14
#define	vr15	15
#define	vr16	16
#define	vr17	17
#define	vr18	18
#define	vr19	19
#define	vr20	20
#define	vr21	21
#define	vr22	22
#define	vr23	23
#define	vr24	24
#define	vr25	25
#define	vr26	26
#define	vr27	27
#define	vr28	28
#define	vr29	29
#define	vr30	30
#define	vr31	31

#define	evr0	0
#define	evr1	1
#define	evr2	2
#define	evr3	3
#define	evr4	4
#define	evr5	5
#define	evr6	6
#define	evr7	7
#define	evr8	8
#define	evr9	9
#define	evr10	10
#define	evr11	11
#define	evr12	12
#define	evr13	13
#define	evr14	14
#define	evr15	15
#define	evr16	16
#define	evr17	17
#define	evr18	18
#define	evr19	19
#define	evr20	20
#define	evr21	21
#define	evr22	22
#define	evr23	23
#define	evr24	24
#define	evr25	25
#define	evr26	26
#define	evr27	27
#define	evr28	28
#define	evr29	29
#define	evr30	30
#define	evr31	31

/* some stab codes */
#define N_FUN	36
#define N_RSYM	64
#define N_SLINE	68
#define N_SO	100
