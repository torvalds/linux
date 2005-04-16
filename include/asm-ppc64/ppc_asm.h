/*
 * arch/ppc64/kernel/ppc_asm.h
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

#ifndef _PPC64_PPC_ASM_H
#define _PPC64_PPC_ASM_H
/*
 * Macros for storing registers into and loading registers from
 * exception frames.
 */
#define SAVE_GPR(n, base)	std	n,GPR0+8*(n)(base)
#define SAVE_2GPRS(n, base)	SAVE_GPR(n, base); SAVE_GPR(n+1, base)
#define SAVE_4GPRS(n, base)	SAVE_2GPRS(n, base); SAVE_2GPRS(n+2, base)
#define SAVE_8GPRS(n, base)	SAVE_4GPRS(n, base); SAVE_4GPRS(n+4, base)
#define SAVE_10GPRS(n, base)	SAVE_8GPRS(n, base); SAVE_2GPRS(n+8, base)
#define REST_GPR(n, base)	ld	n,GPR0+8*(n)(base)
#define REST_2GPRS(n, base)	REST_GPR(n, base); REST_GPR(n+1, base)
#define REST_4GPRS(n, base)	REST_2GPRS(n, base); REST_2GPRS(n+2, base)
#define REST_8GPRS(n, base)	REST_4GPRS(n, base); REST_4GPRS(n+4, base)
#define REST_10GPRS(n, base)	REST_8GPRS(n, base); REST_2GPRS(n+8, base)

#define SAVE_NVGPRS(base)	SAVE_8GPRS(14, base); SAVE_10GPRS(22, base)
#define REST_NVGPRS(base)	REST_8GPRS(14, base); REST_10GPRS(22, base)

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
#define SAVE_2VRS(n,b,base)	SAVE_VR(n,b,base); SAVE_VR(n+1,b,base)
#define SAVE_4VRS(n,b,base)	SAVE_2VRS(n,b,base); SAVE_2VRS(n+2,b,base)
#define SAVE_8VRS(n,b,base)	SAVE_4VRS(n,b,base); SAVE_4VRS(n+4,b,base)
#define SAVE_16VRS(n,b,base)	SAVE_8VRS(n,b,base); SAVE_8VRS(n+8,b,base)
#define SAVE_32VRS(n,b,base)	SAVE_16VRS(n,b,base); SAVE_16VRS(n+16,b,base)
#define REST_VR(n,b,base)	li b,THREAD_VR0+(16*(n)); lvx n,b,base
#define REST_2VRS(n,b,base)	REST_VR(n,b,base); REST_VR(n+1,b,base)
#define REST_4VRS(n,b,base)	REST_2VRS(n,b,base); REST_2VRS(n+2,b,base)
#define REST_8VRS(n,b,base)	REST_4VRS(n,b,base); REST_4VRS(n+4,b,base)
#define REST_16VRS(n,b,base)	REST_8VRS(n,b,base); REST_8VRS(n+8,b,base)
#define REST_32VRS(n,b,base)	REST_16VRS(n,b,base); REST_16VRS(n+16,b,base)

/* Macros to adjust thread priority for Iseries hardware multithreading */
#define HMT_LOW		or 1,1,1
#define HMT_MEDIUM	or 2,2,2
#define HMT_HIGH	or 3,3,3

/* Insert the high 32 bits of the MSR into what will be the new
   MSR (via SRR1 and rfid)  This preserves the MSR.SF and MSR.ISF
   bits. */

#define FIX_SRR1(ra, rb)	\
	mr	rb,ra;		\
	mfmsr	ra;		\
	rldimi	ra,rb,0,32

#define CLR_TOP32(r)	rlwinm	(r),(r),0,0,31	/* clear top 32 bits */

/* 
 * LOADADDR( rn, name )
 *   loads the address of 'name' into 'rn'
 *
 * LOADBASE( rn, name )
 *   loads the address (less the low 16 bits) of 'name' into 'rn'
 *   suitable for base+disp addressing
 */
#define LOADADDR(rn,name) \
	lis	rn,name##@highest;	\
	ori	rn,rn,name##@higher;	\
	rldicr	rn,rn,32,31;		\
	oris	rn,rn,name##@h;		\
	ori	rn,rn,name##@l

#define LOADBASE(rn,name) \
	lis	rn,name@highest;	\
	ori	rn,rn,name@higher;	\
	rldicr	rn,rn,32,31;		\
	oris	rn,rn,name@ha


#define SET_REG_TO_CONST(reg, value)	         	\
	lis     reg,(((value)>>48)&0xFFFF);             \
	ori     reg,reg,(((value)>>32)&0xFFFF);         \
	rldicr  reg,reg,32,31;                          \
	oris    reg,reg,(((value)>>16)&0xFFFF);         \
	ori     reg,reg,((value)&0xFFFF);

#define SET_REG_TO_LABEL(reg, label)	         	\
	lis     reg,(label)@highest;                    \
	ori     reg,reg,(label)@higher;                 \
	rldicr  reg,reg,32,31;                          \
	oris    reg,reg,(label)@h;                      \
	ori     reg,reg,(label)@l;


/* PPPBBB - DRENG  If KERNELBASE is always 0xC0...,
 * Then we can easily do this with one asm insn. -Peter
 */
#define tophys(rd,rs)                           \
        lis     rd,((KERNELBASE>>48)&0xFFFF);   \
        rldicr  rd,rd,32,31;                    \
        sub     rd,rs,rd

#define tovirt(rd,rs)                           \
        lis     rd,((KERNELBASE>>48)&0xFFFF);   \
        rldicr  rd,rd,32,31;                    \
        add     rd,rs,rd

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

#endif /* _PPC64_PPC_ASM_H */
