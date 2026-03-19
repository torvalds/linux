/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Accelerated GHASH implementation with NEON vmull.p8 instructions.
 *
 * Copyright (C) 2015 - 2017 Linaro Ltd.
 * Copyright (C) 2023 Google LLC. <ardb@google.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.fpu		neon

	SHASH		.req	q0
	T1		.req	q1
	XL		.req	q2
	XM		.req	q3
	XH		.req	q4
	IN1		.req	q4

	SHASH_L		.req	d0
	SHASH_H		.req	d1
	T1_L		.req	d2
	T1_H		.req	d3
	XL_L		.req	d4
	XL_H		.req	d5
	XM_L		.req	d6
	XM_H		.req	d7
	XH_L		.req	d8

	t0l		.req	d10
	t0h		.req	d11
	t1l		.req	d12
	t1h		.req	d13
	t2l		.req	d14
	t2h		.req	d15
	t3l		.req	d16
	t3h		.req	d17
	t4l		.req	d18
	t4h		.req	d19

	t0q		.req	q5
	t1q		.req	q6
	t2q		.req	q7
	t3q		.req	q8
	t4q		.req	q9

	s1l		.req	d20
	s1h		.req	d21
	s2l		.req	d22
	s2h		.req	d23
	s3l		.req	d24
	s3h		.req	d25
	s4l		.req	d26
	s4h		.req	d27

	SHASH2_p8	.req	d28

	k16		.req	d29
	k32		.req	d30
	k48		.req	d31

	T2		.req	q7

	.text

	/*
	 * This implementation of 64x64 -> 128 bit polynomial multiplication
	 * using vmull.p8 instructions (8x8 -> 16) is taken from the paper
	 * "Fast Software Polynomial Multiplication on ARM Processors Using
	 * the NEON Engine" by Danilo Camara, Conrado Gouvea, Julio Lopez and
	 * Ricardo Dahab (https://hal.inria.fr/hal-01506572)
	 *
	 * It has been slightly tweaked for in-order performance, and to allow
	 * 'rq' to overlap with 'ad' or 'bd'.
	 */
	.macro		__pmull_p8, rq, ad, bd, b1=t4l, b2=t3l, b3=t4l, b4=t3l
	vext.8		t0l, \ad, \ad, #1	@ A1
	.ifc		\b1, t4l
	vext.8		t4l, \bd, \bd, #1	@ B1
	.endif
	vmull.p8	t0q, t0l, \bd		@ F = A1*B
	vext.8		t1l, \ad, \ad, #2	@ A2
	vmull.p8	t4q, \ad, \b1		@ E = A*B1
	.ifc		\b2, t3l
	vext.8		t3l, \bd, \bd, #2	@ B2
	.endif
	vmull.p8	t1q, t1l, \bd		@ H = A2*B
	vext.8		t2l, \ad, \ad, #3	@ A3
	vmull.p8	t3q, \ad, \b2		@ G = A*B2
	veor		t0q, t0q, t4q		@ L = E + F
	.ifc		\b3, t4l
	vext.8		t4l, \bd, \bd, #3	@ B3
	.endif
	vmull.p8	t2q, t2l, \bd		@ J = A3*B
	veor		t0l, t0l, t0h		@ t0 = (L) (P0 + P1) << 8
	veor		t1q, t1q, t3q		@ M = G + H
	.ifc		\b4, t3l
	vext.8		t3l, \bd, \bd, #4	@ B4
	.endif
	vmull.p8	t4q, \ad, \b3		@ I = A*B3
	veor		t1l, t1l, t1h		@ t1 = (M) (P2 + P3) << 16
	vmull.p8	t3q, \ad, \b4		@ K = A*B4
	vand		t0h, t0h, k48
	vand		t1h, t1h, k32
	veor		t2q, t2q, t4q		@ N = I + J
	veor		t0l, t0l, t0h
	veor		t1l, t1l, t1h
	veor		t2l, t2l, t2h		@ t2 = (N) (P4 + P5) << 24
	vand		t2h, t2h, k16
	veor		t3l, t3l, t3h		@ t3 = (K) (P6 + P7) << 32
	vmov.i64	t3h, #0
	vext.8		t0q, t0q, t0q, #15
	veor		t2l, t2l, t2h
	vext.8		t1q, t1q, t1q, #14
	vmull.p8	\rq, \ad, \bd		@ D = A*B
	vext.8		t2q, t2q, t2q, #13
	vext.8		t3q, t3q, t3q, #12
	veor		t0q, t0q, t1q
	veor		t2q, t2q, t3q
	veor		\rq, \rq, t0q
	veor		\rq, \rq, t2q
	.endm

	.macro		__pmull_reduce_p8
	veor		XL_H, XL_H, XM_L
	veor		XH_L, XH_L, XM_H

	vshl.i64	T1, XL, #57
	vshl.i64	T2, XL, #62
	veor		T1, T1, T2
	vshl.i64	T2, XL, #63
	veor		T1, T1, T2
	veor		XL_H, XL_H, T1_L
	veor		XH_L, XH_L, T1_H

	vshr.u64	T1, XL, #1
	veor		XH, XH, XL
	veor		XL, XL, T1
	vshr.u64	T1, T1, #6
	vshr.u64	XL, XL, #1
	.endm

	.macro		vrev64_if_be	a
#ifdef CONFIG_CPU_BIG_ENDIAN
	vrev64.8	\a, \a
#endif
	.endm

	.macro		ghash_update
	vld1.64		{XL}, [r1]
	vrev64_if_be	XL

0:
	vld1.8		{T1}, [r2]!
	subs		r0, r0, #1

	/* multiply XL by SHASH in GF(2^128) */
	vrev64.8	T1, T1

	vext.8		IN1, T1, T1, #8
	veor		T1_L, T1_L, XL_H
	veor		XL, XL, IN1

	__pmull_p8	XH, XL_H, SHASH_H, s1h, s2h, s3h, s4h	@ a1 * b1
	veor		T1, T1, XL
	__pmull_p8	XL, XL_L, SHASH_L, s1l, s2l, s3l, s4l	@ a0 * b0
	__pmull_p8	XM, T1_L, SHASH2_p8			@ (a1+a0)(b1+b0)

	veor		T1, XL, XH
	veor		XM, XM, T1

	__pmull_reduce_p8

	veor		T1, T1, XH
	veor		XL, XL, T1

	bne		0b
	.endm

	/*
	 * void pmull_ghash_update_p8(size_t blocks, struct polyval_elem *dg,
	 *			      const u8 *src,
	 *			      const struct polyval_elem *h)
	 */
ENTRY(pmull_ghash_update_p8)
	vld1.64		{SHASH}, [r3]
	vrev64_if_be	SHASH
	veor		SHASH2_p8, SHASH_L, SHASH_H

	vext.8		s1l, SHASH_L, SHASH_L, #1
	vext.8		s2l, SHASH_L, SHASH_L, #2
	vext.8		s3l, SHASH_L, SHASH_L, #3
	vext.8		s4l, SHASH_L, SHASH_L, #4
	vext.8		s1h, SHASH_H, SHASH_H, #1
	vext.8		s2h, SHASH_H, SHASH_H, #2
	vext.8		s3h, SHASH_H, SHASH_H, #3
	vext.8		s4h, SHASH_H, SHASH_H, #4

	vmov.i64	k16, #0xffff
	vmov.i64	k32, #0xffffffff
	vmov.i64	k48, #0xffffffffffff

	ghash_update
	vrev64_if_be	XL
	vst1.64		{XL}, [r1]

	bx		lr
ENDPROC(pmull_ghash_update_p8)
