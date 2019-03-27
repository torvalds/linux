/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from ghashv8-armx.pl. */
#include "arm_arch.h"

#if __ARM_MAX_ARCH__>=7
.text
.fpu	neon
.code	32
#undef	__thumb2__
.globl	gcm_init_v8
.type	gcm_init_v8,%function
.align	4
gcm_init_v8:
	vld1.64	{q9},[r1]		@ load input H
	vmov.i8	q11,#0xe1
	vshl.i64	q11,q11,#57		@ 0xc2.0
	vext.8	q3,q9,q9,#8
	vshr.u64	q10,q11,#63
	vdup.32	q9,d18[1]
	vext.8	q8,q10,q11,#8		@ t0=0xc2....01
	vshr.u64	q10,q3,#63
	vshr.s32	q9,q9,#31		@ broadcast carry bit
	vand	q10,q10,q8
	vshl.i64	q3,q3,#1
	vext.8	q10,q10,q10,#8
	vand	q8,q8,q9
	vorr	q3,q3,q10		@ H<<<=1
	veor	q12,q3,q8		@ twisted H
	vst1.64	{q12},[r0]!		@ store Htable[0]

	@ calculate H^2
	vext.8	q8,q12,q12,#8		@ Karatsuba pre-processing
.byte	0xa8,0x0e,0xa8,0xf2	@ pmull q0,q12,q12
	veor	q8,q8,q12
.byte	0xa9,0x4e,0xa9,0xf2	@ pmull2 q2,q12,q12
.byte	0xa0,0x2e,0xa0,0xf2	@ pmull q1,q8,q8

	vext.8	q9,q0,q2,#8		@ Karatsuba post-processing
	veor	q10,q0,q2
	veor	q1,q1,q9
	veor	q1,q1,q10
.byte	0x26,0x4e,0xe0,0xf2	@ pmull q10,q0,q11		@ 1st phase

	vmov	d4,d3		@ Xh|Xm - 256-bit result
	vmov	d3,d0		@ Xm is rotated Xl
	veor	q0,q1,q10

	vext.8	q10,q0,q0,#8		@ 2nd phase
.byte	0x26,0x0e,0xa0,0xf2	@ pmull q0,q0,q11
	veor	q10,q10,q2
	veor	q14,q0,q10

	vext.8	q9,q14,q14,#8		@ Karatsuba pre-processing
	veor	q9,q9,q14
	vext.8	q13,q8,q9,#8		@ pack Karatsuba pre-processed
	vst1.64	{q13,q14},[r0]!	@ store Htable[1..2]
	bx	lr
.size	gcm_init_v8,.-gcm_init_v8
.globl	gcm_gmult_v8
.type	gcm_gmult_v8,%function
.align	4
gcm_gmult_v8:
	vld1.64	{q9},[r0]		@ load Xi
	vmov.i8	q11,#0xe1
	vld1.64	{q12,q13},[r1]	@ load twisted H, ...
	vshl.u64	q11,q11,#57
#ifndef __ARMEB__
	vrev64.8	q9,q9
#endif
	vext.8	q3,q9,q9,#8

.byte	0x86,0x0e,0xa8,0xf2	@ pmull q0,q12,q3		@ H.lo·Xi.lo
	veor	q9,q9,q3		@ Karatsuba pre-processing
.byte	0x87,0x4e,0xa9,0xf2	@ pmull2 q2,q12,q3		@ H.hi·Xi.hi
.byte	0xa2,0x2e,0xaa,0xf2	@ pmull q1,q13,q9		@ (H.lo+H.hi)·(Xi.lo+Xi.hi)

	vext.8	q9,q0,q2,#8		@ Karatsuba post-processing
	veor	q10,q0,q2
	veor	q1,q1,q9
	veor	q1,q1,q10
.byte	0x26,0x4e,0xe0,0xf2	@ pmull q10,q0,q11		@ 1st phase of reduction

	vmov	d4,d3		@ Xh|Xm - 256-bit result
	vmov	d3,d0		@ Xm is rotated Xl
	veor	q0,q1,q10

	vext.8	q10,q0,q0,#8		@ 2nd phase of reduction
.byte	0x26,0x0e,0xa0,0xf2	@ pmull q0,q0,q11
	veor	q10,q10,q2
	veor	q0,q0,q10

#ifndef __ARMEB__
	vrev64.8	q0,q0
#endif
	vext.8	q0,q0,q0,#8
	vst1.64	{q0},[r0]		@ write out Xi

	bx	lr
.size	gcm_gmult_v8,.-gcm_gmult_v8
.globl	gcm_ghash_v8
.type	gcm_ghash_v8,%function
.align	4
gcm_ghash_v8:
	vstmdb	sp!,{d8,d9,d10,d11,d12,d13,d14,d15}		@ 32-bit ABI says so
	vld1.64	{q0},[r0]		@ load [rotated] Xi
						@ "[rotated]" means that
						@ loaded value would have
						@ to be rotated in order to
						@ make it appear as in
						@ algorithm specification
	subs	r3,r3,#32		@ see if r3 is 32 or larger
	mov	r12,#16		@ r12 is used as post-
						@ increment for input pointer;
						@ as loop is modulo-scheduled
						@ r12 is zeroed just in time
						@ to preclude overstepping
						@ inp[len], which means that
						@ last block[s] are actually
						@ loaded twice, but last
						@ copy is not processed
	vld1.64	{q12,q13},[r1]!	@ load twisted H, ..., H^2
	vmov.i8	q11,#0xe1
	vld1.64	{q14},[r1]
	moveq	r12,#0			@ is it time to zero r12?
	vext.8	q0,q0,q0,#8		@ rotate Xi
	vld1.64	{q8},[r2]!	@ load [rotated] I[0]
	vshl.u64	q11,q11,#57		@ compose 0xc2.0 constant
#ifndef __ARMEB__
	vrev64.8	q8,q8
	vrev64.8	q0,q0
#endif
	vext.8	q3,q8,q8,#8		@ rotate I[0]
	blo	.Lodd_tail_v8		@ r3 was less than 32
	vld1.64	{q9},[r2],r12	@ load [rotated] I[1]
#ifndef __ARMEB__
	vrev64.8	q9,q9
#endif
	vext.8	q7,q9,q9,#8
	veor	q3,q3,q0		@ I[i]^=Xi
.byte	0x8e,0x8e,0xa8,0xf2	@ pmull q4,q12,q7		@ H·Ii+1
	veor	q9,q9,q7		@ Karatsuba pre-processing
.byte	0x8f,0xce,0xa9,0xf2	@ pmull2 q6,q12,q7
	b	.Loop_mod2x_v8

.align	4
.Loop_mod2x_v8:
	vext.8	q10,q3,q3,#8
	subs	r3,r3,#32		@ is there more data?
.byte	0x86,0x0e,0xac,0xf2	@ pmull q0,q14,q3		@ H^2.lo·Xi.lo
	movlo	r12,#0			@ is it time to zero r12?

.byte	0xa2,0xae,0xaa,0xf2	@ pmull q5,q13,q9
	veor	q10,q10,q3		@ Karatsuba pre-processing
.byte	0x87,0x4e,0xad,0xf2	@ pmull2 q2,q14,q3		@ H^2.hi·Xi.hi
	veor	q0,q0,q4		@ accumulate
.byte	0xa5,0x2e,0xab,0xf2	@ pmull2 q1,q13,q10		@ (H^2.lo+H^2.hi)·(Xi.lo+Xi.hi)
	vld1.64	{q8},[r2],r12	@ load [rotated] I[i+2]

	veor	q2,q2,q6
	moveq	r12,#0			@ is it time to zero r12?
	veor	q1,q1,q5

	vext.8	q9,q0,q2,#8		@ Karatsuba post-processing
	veor	q10,q0,q2
	veor	q1,q1,q9
	vld1.64	{q9},[r2],r12	@ load [rotated] I[i+3]
#ifndef __ARMEB__
	vrev64.8	q8,q8
#endif
	veor	q1,q1,q10
.byte	0x26,0x4e,0xe0,0xf2	@ pmull q10,q0,q11		@ 1st phase of reduction

#ifndef __ARMEB__
	vrev64.8	q9,q9
#endif
	vmov	d4,d3		@ Xh|Xm - 256-bit result
	vmov	d3,d0		@ Xm is rotated Xl
	vext.8	q7,q9,q9,#8
	vext.8	q3,q8,q8,#8
	veor	q0,q1,q10
.byte	0x8e,0x8e,0xa8,0xf2	@ pmull q4,q12,q7		@ H·Ii+1
	veor	q3,q3,q2		@ accumulate q3 early

	vext.8	q10,q0,q0,#8		@ 2nd phase of reduction
.byte	0x26,0x0e,0xa0,0xf2	@ pmull q0,q0,q11
	veor	q3,q3,q10
	veor	q9,q9,q7		@ Karatsuba pre-processing
	veor	q3,q3,q0
.byte	0x8f,0xce,0xa9,0xf2	@ pmull2 q6,q12,q7
	bhs	.Loop_mod2x_v8		@ there was at least 32 more bytes

	veor	q2,q2,q10
	vext.8	q3,q8,q8,#8		@ re-construct q3
	adds	r3,r3,#32		@ re-construct r3
	veor	q0,q0,q2		@ re-construct q0
	beq	.Ldone_v8		@ is r3 zero?
.Lodd_tail_v8:
	vext.8	q10,q0,q0,#8
	veor	q3,q3,q0		@ inp^=Xi
	veor	q9,q8,q10		@ q9 is rotated inp^Xi

.byte	0x86,0x0e,0xa8,0xf2	@ pmull q0,q12,q3		@ H.lo·Xi.lo
	veor	q9,q9,q3		@ Karatsuba pre-processing
.byte	0x87,0x4e,0xa9,0xf2	@ pmull2 q2,q12,q3		@ H.hi·Xi.hi
.byte	0xa2,0x2e,0xaa,0xf2	@ pmull q1,q13,q9		@ (H.lo+H.hi)·(Xi.lo+Xi.hi)

	vext.8	q9,q0,q2,#8		@ Karatsuba post-processing
	veor	q10,q0,q2
	veor	q1,q1,q9
	veor	q1,q1,q10
.byte	0x26,0x4e,0xe0,0xf2	@ pmull q10,q0,q11		@ 1st phase of reduction

	vmov	d4,d3		@ Xh|Xm - 256-bit result
	vmov	d3,d0		@ Xm is rotated Xl
	veor	q0,q1,q10

	vext.8	q10,q0,q0,#8		@ 2nd phase of reduction
.byte	0x26,0x0e,0xa0,0xf2	@ pmull q0,q0,q11
	veor	q10,q10,q2
	veor	q0,q0,q10

.Ldone_v8:
#ifndef __ARMEB__
	vrev64.8	q0,q0
#endif
	vext.8	q0,q0,q0,#8
	vst1.64	{q0},[r0]		@ write out Xi

	vldmia	sp!,{d8,d9,d10,d11,d12,d13,d14,d15}		@ 32-bit ABI says so
	bx	lr
.size	gcm_ghash_v8,.-gcm_ghash_v8
.byte	71,72,65,83,72,32,102,111,114,32,65,82,77,118,56,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	2
#endif
