/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SHA-1 implementation for PowerPC.
 *
 * Copyright (C) 2005 Paul Mackerras <paulus@samba.org>
 */

#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/asm-compat.h>

#ifdef __BIG_ENDIAN__
#define LWZ(rt, d, ra)	\
	lwz	rt,d(ra)
#else
#define LWZ(rt, d, ra)	\
	li	rt,d;	\
	lwbrx	rt,rt,ra
#endif

/*
 * We roll the registers for T, A, B, C, D, E around on each
 * iteration; T on iteration t is A on iteration t+1, and so on.
 * We use registers 7 - 12 for this.
 */
#define RT(t)	((((t)+5)%6)+7)
#define RA(t)	((((t)+4)%6)+7)
#define RB(t)	((((t)+3)%6)+7)
#define RC(t)	((((t)+2)%6)+7)
#define RD(t)	((((t)+1)%6)+7)
#define RE(t)	((((t)+0)%6)+7)

/* We use registers 16 - 31 for the W values */
#define W(t)	(((t)%16)+16)

#define LOADW(t)				\
	LWZ(W(t),(t)*4,r4)

#define STEPD0_LOAD(t)				\
	andc	r0,RD(t),RB(t);		\
	and	r6,RB(t),RC(t);		\
	rotlwi	RT(t),RA(t),5;			\
	or	r6,r6,r0;			\
	add	r0,RE(t),r15;			\
	add	RT(t),RT(t),r6;		\
	add	r14,r0,W(t);			\
	LWZ(W((t)+4),((t)+4)*4,r4);	\
	rotlwi	RB(t),RB(t),30;			\
	add	RT(t),RT(t),r14

#define STEPD0_UPDATE(t)			\
	and	r6,RB(t),RC(t);		\
	andc	r0,RD(t),RB(t);		\
	rotlwi	RT(t),RA(t),5;			\
	rotlwi	RB(t),RB(t),30;			\
	or	r6,r6,r0;			\
	add	r0,RE(t),r15;			\
	xor	r5,W((t)+4-3),W((t)+4-8);		\
	add	RT(t),RT(t),r6;		\
	xor	W((t)+4),W((t)+4-16),W((t)+4-14);	\
	add	r0,r0,W(t);			\
	xor	W((t)+4),W((t)+4),r5;			\
	add	RT(t),RT(t),r0;		\
	rotlwi	W((t)+4),W((t)+4),1

#define STEPD1(t)				\
	xor	r6,RB(t),RC(t);		\
	rotlwi	RT(t),RA(t),5;			\
	rotlwi	RB(t),RB(t),30;			\
	xor	r6,r6,RD(t);			\
	add	r0,RE(t),r15;			\
	add	RT(t),RT(t),r6;		\
	add	r0,r0,W(t);			\
	add	RT(t),RT(t),r0

#define STEPD1_UPDATE(t)				\
	xor	r6,RB(t),RC(t);		\
	rotlwi	RT(t),RA(t),5;			\
	rotlwi	RB(t),RB(t),30;			\
	xor	r6,r6,RD(t);			\
	add	r0,RE(t),r15;			\
	xor	r5,W((t)+4-3),W((t)+4-8);		\
	add	RT(t),RT(t),r6;		\
	xor	W((t)+4),W((t)+4-16),W((t)+4-14);	\
	add	r0,r0,W(t);			\
	xor	W((t)+4),W((t)+4),r5;			\
	add	RT(t),RT(t),r0;		\
	rotlwi	W((t)+4),W((t)+4),1

#define STEPD2_UPDATE(t)			\
	and	r6,RB(t),RC(t);		\
	and	r0,RB(t),RD(t);		\
	rotlwi	RT(t),RA(t),5;			\
	or	r6,r6,r0;			\
	rotlwi	RB(t),RB(t),30;			\
	and	r0,RC(t),RD(t);		\
	xor	r5,W((t)+4-3),W((t)+4-8);	\
	or	r6,r6,r0;			\
	xor	W((t)+4),W((t)+4-16),W((t)+4-14);	\
	add	r0,RE(t),r15;			\
	add	RT(t),RT(t),r6;		\
	add	r0,r0,W(t);			\
	xor	W((t)+4),W((t)+4),r5;		\
	add	RT(t),RT(t),r0;		\
	rotlwi	W((t)+4),W((t)+4),1

#define STEP0LD4(t)				\
	STEPD0_LOAD(t);				\
	STEPD0_LOAD((t)+1);			\
	STEPD0_LOAD((t)+2);			\
	STEPD0_LOAD((t)+3)

#define STEPUP4(t, fn)				\
	STEP##fn##_UPDATE(t);			\
	STEP##fn##_UPDATE((t)+1);		\
	STEP##fn##_UPDATE((t)+2);		\
	STEP##fn##_UPDATE((t)+3)

#define STEPUP20(t, fn)				\
	STEPUP4(t, fn);				\
	STEPUP4((t)+4, fn);			\
	STEPUP4((t)+8, fn);			\
	STEPUP4((t)+12, fn);			\
	STEPUP4((t)+16, fn)

_GLOBAL(powerpc_sha_transform)
	PPC_STLU r1,-INT_FRAME_SIZE(r1)
	SAVE_GPRS(14, 31, r1)

	/* Load up A - E */
	lwz	RA(0),0(r3)	/* A */
	lwz	RB(0),4(r3)	/* B */
	lwz	RC(0),8(r3)	/* C */
	lwz	RD(0),12(r3)	/* D */
	lwz	RE(0),16(r3)	/* E */

	LOADW(0)
	LOADW(1)
	LOADW(2)
	LOADW(3)

	lis	r15,0x5a82	/* K0-19 */
	ori	r15,r15,0x7999
	STEP0LD4(0)
	STEP0LD4(4)
	STEP0LD4(8)
	STEPUP4(12, D0)
	STEPUP4(16, D0)

	lis	r15,0x6ed9	/* K20-39 */
	ori	r15,r15,0xeba1
	STEPUP20(20, D1)

	lis	r15,0x8f1b	/* K40-59 */
	ori	r15,r15,0xbcdc
	STEPUP20(40, D2)

	lis	r15,0xca62	/* K60-79 */
	ori	r15,r15,0xc1d6
	STEPUP4(60, D1)
	STEPUP4(64, D1)
	STEPUP4(68, D1)
	STEPUP4(72, D1)
	lwz	r20,16(r3)
	STEPD1(76)
	lwz	r19,12(r3)
	STEPD1(77)
	lwz	r18,8(r3)
	STEPD1(78)
	lwz	r17,4(r3)
	STEPD1(79)

	lwz	r16,0(r3)
	add	r20,RE(80),r20
	add	RD(0),RD(80),r19
	add	RC(0),RC(80),r18
	add	RB(0),RB(80),r17
	add	RA(0),RA(80),r16
	mr	RE(0),r20
	stw	RA(0),0(r3)
	stw	RB(0),4(r3)
	stw	RC(0),8(r3)
	stw	RD(0),12(r3)
	stw	RE(0),16(r3)

	REST_GPRS(14, 31, r1)
	addi	r1,r1,INT_FRAME_SIZE
	blr
