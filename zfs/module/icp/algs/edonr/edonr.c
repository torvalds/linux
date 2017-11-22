/*
 * IDI,NTNU
 *
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Copyright (C) 2009, 2010, Jorn Amundsen <jorn.amundsen@ntnu.no>
 * Tweaked Edon-R implementation for SUPERCOP, based on NIST API.
 *
 * $Id: edonr.c 517 2013-02-17 20:34:39Z joern $
 */
/*
 * Portions copyright (c) 2013, Saso Kiselkov, All rights reserved
 */

/* determine where we can get bcopy/bzero declarations */
#ifdef	_KERNEL
#include <sys/systm.h>
#else
#include <strings.h>
#endif
#include <sys/edonr.h>
#include <sys/debug.h>

/* big endian support, provides no-op's if run on little endian hosts */
#include "edonr_byteorder.h"

#define	hashState224(x)	((x)->pipe->p256)
#define	hashState256(x)	((x)->pipe->p256)
#define	hashState384(x)	((x)->pipe->p512)
#define	hashState512(x)	((x)->pipe->p512)

/* shift and rotate shortcuts */
#define	shl(x, n)	((x) << n)
#define	shr(x, n)	((x) >> n)

#define	rotl32(x, n)	(((x) << (n)) | ((x) >> (32 - (n))))
#define	rotr32(x, n)	(((x) >> (n)) | ((x) << (32 - (n))))

#define	rotl64(x, n)	(((x) << (n)) | ((x) >> (64 - (n))))
#define	rotr64(x, n)	(((x) >> (n)) | ((x) << (64 - (n))))

#if !defined(__C99_RESTRICT)
#define	restrict	/* restrict */
#endif

#define	EDONR_VALID_HASHBITLEN(x) \
	((x) == 512 || (x) == 384 || (x) == 256 || (x) == 224)

/* EdonR224 initial double chaining pipe */
static const uint32_t i224p2[16] = {
	0x00010203ul, 0x04050607ul, 0x08090a0bul, 0x0c0d0e0ful,
	0x10111213ul, 0x14151617ul, 0x18191a1bul, 0x1c1d1e1ful,
	0x20212223ul, 0x24252627ul, 0x28292a2bul, 0x2c2d2e2ful,
	0x30313233ul, 0x34353637ul, 0x38393a3bul, 0x3c3d3e3ful,
};

/* EdonR256 initial double chaining pipe */
static const uint32_t i256p2[16] = {
	0x40414243ul, 0x44454647ul, 0x48494a4bul, 0x4c4d4e4ful,
	0x50515253ul, 0x54555657ul, 0x58595a5bul, 0x5c5d5e5ful,
	0x60616263ul, 0x64656667ul, 0x68696a6bul, 0x6c6d6e6ful,
	0x70717273ul, 0x74757677ul, 0x78797a7bul, 0x7c7d7e7ful,
};

/* EdonR384 initial double chaining pipe */
static const uint64_t i384p2[16] = {
	0x0001020304050607ull, 0x08090a0b0c0d0e0full,
	0x1011121314151617ull, 0x18191a1b1c1d1e1full,
	0x2021222324252627ull, 0x28292a2b2c2d2e2full,
	0x3031323334353637ull, 0x38393a3b3c3d3e3full,
	0x4041424344454647ull, 0x48494a4b4c4d4e4full,
	0x5051525354555657ull, 0x58595a5b5c5d5e5full,
	0x6061626364656667ull, 0x68696a6b6c6d6e6full,
	0x7071727374757677ull, 0x78797a7b7c7d7e7full
};

/* EdonR512 initial double chaining pipe */
static const uint64_t i512p2[16] = {
	0x8081828384858687ull, 0x88898a8b8c8d8e8full,
	0x9091929394959697ull, 0x98999a9b9c9d9e9full,
	0xa0a1a2a3a4a5a6a7ull, 0xa8a9aaabacadaeafull,
	0xb0b1b2b3b4b5b6b7ull, 0xb8b9babbbcbdbebfull,
	0xc0c1c2c3c4c5c6c7ull, 0xc8c9cacbcccdcecfull,
	0xd0d1d2d3d4d5d6d7ull, 0xd8d9dadbdcdddedfull,
	0xe0e1e2e3e4e5e6e7ull, 0xe8e9eaebecedeeefull,
	0xf0f1f2f3f4f5f6f7ull, 0xf8f9fafbfcfdfeffull
};

/*
 * First Latin Square
 * 0   7   1   3   2   4   6   5
 * 4   1   7   6   3   0   5   2
 * 7   0   4   2   5   3   1   6
 * 1   4   0   5   6   2   7   3
 * 2   3   6   7   1   5   0   4
 * 5   2   3   1   7   6   4   0
 * 3   6   5   0   4   7   2   1
 * 6   5   2   4   0   1   3   7
 */
#define	LS1_256(c, x0, x1, x2, x3, x4, x5, x6, x7)			\
{									\
	uint32_t x04, x17, x23, x56, x07, x26;				\
	x04 = x0+x4, x17 = x1+x7, x07 = x04+x17;			\
	s0 = c + x07 + x2;						\
	s1 = rotl32(x07 + x3, 4);					\
	s2 = rotl32(x07 + x6, 8);					\
	x23 = x2 + x3;							\
	s5 = rotl32(x04 + x23 + x5, 22);				\
	x56 = x5 + x6;							\
	s6 = rotl32(x17 + x56 + x0, 24);				\
	x26 = x23+x56;							\
	s3 = rotl32(x26 + x7, 13);					\
	s4 = rotl32(x26 + x1, 17);					\
	s7 = rotl32(x26 + x4, 29);					\
}

#define	LS1_512(c, x0, x1, x2, x3, x4, x5, x6, x7)			\
{									\
	uint64_t x04, x17, x23, x56, x07, x26;				\
	x04 = x0+x4, x17 = x1+x7, x07 = x04+x17;			\
	s0 = c + x07 + x2;						\
	s1 = rotl64(x07 + x3, 5);					\
	s2 = rotl64(x07 + x6, 15);					\
	x23 = x2 + x3;							\
	s5 = rotl64(x04 + x23 + x5, 40);				\
	x56 = x5 + x6;							\
	s6 = rotl64(x17 + x56 + x0, 50);				\
	x26 = x23+x56;							\
	s3 = rotl64(x26 + x7, 22);					\
	s4 = rotl64(x26 + x1, 31);					\
	s7 = rotl64(x26 + x4, 59);					\
}

/*
 * Second Orthogonal Latin Square
 * 0   4   2   3   1   6   5   7
 * 7   6   3   2   5   4   1   0
 * 5   3   1   6   0   2   7   4
 * 1   0   5   4   3   7   2   6
 * 2   1   0   7   4   5   6   3
 * 3   5   7   0   6   1   4   2
 * 4   7   6   1   2   0   3   5
 * 6   2   4   5   7   3   0   1
 */
#define	LS2_256(c, y0, y1, y2, y3, y4, y5, y6, y7)			\
{									\
	uint32_t y01, y25, y34, y67, y04, y05, y27, y37;		\
	y01 = y0+y1, y25 = y2+y5, y05 = y01+y25;			\
	t0  = ~c + y05 + y7;						\
	t2 = rotl32(y05 + y3, 9);					\
	y34 = y3+y4, y04 = y01+y34;					\
	t1 = rotl32(y04 + y6, 5);					\
	t4 = rotl32(y04 + y5, 15);					\
	y67 = y6+y7, y37 = y34+y67;					\
	t3 = rotl32(y37 + y2, 11);					\
	t7 = rotl32(y37 + y0, 27);					\
	y27 = y25+y67;							\
	t5 = rotl32(y27 + y4, 20);					\
	t6 = rotl32(y27 + y1, 25);					\
}

#define	LS2_512(c, y0, y1, y2, y3, y4, y5, y6, y7)			\
{									\
	uint64_t y01, y25, y34, y67, y04, y05, y27, y37;		\
	y01 = y0+y1, y25 = y2+y5, y05 = y01+y25;			\
	t0  = ~c + y05 + y7;						\
	t2 = rotl64(y05 + y3, 19);					\
	y34 = y3+y4, y04 = y01+y34;					\
	t1 = rotl64(y04 + y6, 10);					\
	t4 = rotl64(y04 + y5, 36);					\
	y67 = y6+y7, y37 = y34+y67;					\
	t3 = rotl64(y37 + y2, 29);					\
	t7 = rotl64(y37 + y0, 55);					\
	y27 = y25+y67;							\
	t5 = rotl64(y27 + y4, 44);					\
	t6 = rotl64(y27 + y1, 48);					\
}

#define	quasi_exform256(r0, r1, r2, r3, r4, r5, r6, r7)			\
{									\
	uint32_t s04, s17, s23, s56, t01, t25, t34, t67;		\
	s04 = s0 ^ s4, t01 = t0 ^ t1;					\
	r0 = (s04 ^ s1) + (t01 ^ t5);					\
	t67 = t6 ^ t7;							\
	r1 = (s04 ^ s7) + (t2 ^ t67);					\
	s23 = s2 ^ s3;							\
	r7 = (s23 ^ s5) + (t4 ^ t67);					\
	t34 = t3 ^ t4;							\
	r3 = (s23 ^ s4) + (t0 ^ t34);					\
	s56 = s5 ^ s6;							\
	r5 = (s3 ^ s56) + (t34 ^ t6);					\
	t25 = t2 ^ t5;							\
	r6 = (s2 ^ s56) + (t25 ^ t7);					\
	s17 = s1 ^ s7;							\
	r4 = (s0 ^ s17) + (t1 ^ t25);					\
	r2 = (s17 ^ s6) + (t01 ^ t3);					\
}

#define	quasi_exform512(r0, r1, r2, r3, r4, r5, r6, r7)			\
{									\
	uint64_t s04, s17, s23, s56, t01, t25, t34, t67;		\
	s04 = s0 ^ s4, t01 = t0 ^ t1;					\
	r0 = (s04 ^ s1) + (t01 ^ t5);					\
	t67 = t6 ^ t7;							\
	r1 = (s04 ^ s7) + (t2 ^ t67);					\
	s23 = s2 ^ s3;							\
	r7 = (s23 ^ s5) + (t4 ^ t67);					\
	t34 = t3 ^ t4;							\
	r3 = (s23 ^ s4) + (t0 ^ t34);					\
	s56 = s5 ^ s6;							\
	r5 = (s3 ^ s56) + (t34 ^ t6);					\
	t25 = t2 ^ t5;							\
	r6 = (s2 ^ s56) + (t25 ^ t7);					\
	s17 = s1 ^ s7;							\
	r4 = (s0 ^ s17) + (t1 ^ t25);					\
	r2 = (s17 ^ s6) + (t01 ^ t3);					\
}

static size_t
Q256(size_t bitlen, const uint32_t *data, uint32_t *restrict p)
{
	size_t bl;

	for (bl = bitlen; bl >= EdonR256_BLOCK_BITSIZE;
	    bl -= EdonR256_BLOCK_BITSIZE, data += 16) {
		uint32_t s0, s1, s2, s3, s4, s5, s6, s7, t0, t1, t2, t3, t4,
		    t5, t6, t7;
		uint32_t p0, p1, p2, p3, p4, p5, p6, p7, q0, q1, q2, q3, q4,
		    q5, q6, q7;
		const uint32_t defix = 0xaaaaaaaa;
#if defined(MACHINE_IS_BIG_ENDIAN)
		uint32_t swp0, swp1, swp2, swp3, swp4, swp5, swp6, swp7, swp8,
		    swp9, swp10, swp11, swp12, swp13, swp14, swp15;
#define	d(j)	swp ## j
#define	s32(j)	ld_swap32((uint32_t *)data + j, swp ## j)
#else
#define	d(j)	data[j]
#endif

		/* First row of quasigroup e-transformations */
#if defined(MACHINE_IS_BIG_ENDIAN)
		s32(8);
		s32(9);
		s32(10);
		s32(11);
		s32(12);
		s32(13);
		s32(14);
		s32(15);
#endif
		LS1_256(defix, d(15), d(14), d(13), d(12), d(11), d(10), d(9),
		    d(8));
#if defined(MACHINE_IS_BIG_ENDIAN)
		s32(0);
		s32(1);
		s32(2);
		s32(3);
		s32(4);
		s32(5);
		s32(6);
		s32(7);
#undef s32
#endif
		LS2_256(defix, d(0), d(1), d(2), d(3), d(4), d(5), d(6), d(7));
		quasi_exform256(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_256(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_256(defix, d(8), d(9), d(10), d(11), d(12), d(13), d(14),
		    d(15));
		quasi_exform256(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Second row of quasigroup e-transformations */
		LS1_256(defix, p[8], p[9], p[10], p[11], p[12], p[13], p[14],
		    p[15]);
		LS2_256(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		quasi_exform256(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_256(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_256(defix, q0, q1, q2, q3, q4, q5, q6, q7);
		quasi_exform256(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Third row of quasigroup e-transformations */
		LS1_256(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_256(defix, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
		quasi_exform256(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_256(defix, q0, q1, q2, q3, q4, q5, q6, q7);
		LS2_256(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		quasi_exform256(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Fourth row of quasigroup e-transformations */
		LS1_256(defix, d(7), d(6), d(5), d(4), d(3), d(2), d(1), d(0));
		LS2_256(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		quasi_exform256(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_256(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_256(defix, q0, q1, q2, q3, q4, q5, q6, q7);
		quasi_exform256(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Edon-R tweak on the original SHA-3 Edon-R submission. */
		p[0] ^= d(8) ^ p0;
		p[1] ^= d(9) ^ p1;
		p[2] ^= d(10) ^ p2;
		p[3] ^= d(11) ^ p3;
		p[4] ^= d(12) ^ p4;
		p[5] ^= d(13) ^ p5;
		p[6] ^= d(14) ^ p6;
		p[7] ^= d(15) ^ p7;
		p[8] ^= d(0) ^ q0;
		p[9] ^= d(1) ^ q1;
		p[10] ^= d(2) ^ q2;
		p[11] ^= d(3) ^ q3;
		p[12] ^= d(4) ^ q4;
		p[13] ^= d(5) ^ q5;
		p[14] ^= d(6) ^ q6;
		p[15] ^= d(7) ^ q7;
	}

#undef d
	return (bitlen - bl);
}

/*
 * Why is this #pragma here?
 *
 * Checksum functions like this one can go over the stack frame size check
 * Linux imposes on 32-bit platforms (-Wframe-larger-than=1024).  We can
 * safely ignore the compiler error since we know that in ZoL, that
 * the function will be called from a worker thread that won't be using
 * much stack.  The only function that goes over the 1k limit is Q512(),
 * which only goes over it by a hair (1248 bytes on ARM32).
 */
#include <sys/isa_defs.h>	/* for _ILP32 */
#ifdef _ILP32   /* We're 32-bit, assume small stack frames */
#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

#if defined(__IBMC__) && defined(_AIX) && defined(__64BIT__)
static inline size_t
#else
static size_t
#endif
Q512(size_t bitlen, const uint64_t *data, uint64_t *restrict p)
{
	size_t bl;

	for (bl = bitlen; bl >= EdonR512_BLOCK_BITSIZE;
	    bl -= EdonR512_BLOCK_BITSIZE, data += 16) {
		uint64_t s0, s1, s2, s3, s4, s5, s6, s7, t0, t1, t2, t3, t4,
		    t5, t6, t7;
		uint64_t p0, p1, p2, p3, p4, p5, p6, p7, q0, q1, q2, q3, q4,
		    q5, q6, q7;
		const uint64_t defix = 0xaaaaaaaaaaaaaaaaull;
#if defined(MACHINE_IS_BIG_ENDIAN)
		uint64_t swp0, swp1, swp2, swp3, swp4, swp5, swp6, swp7, swp8,
		    swp9, swp10, swp11, swp12, swp13, swp14, swp15;
#define	d(j)	swp##j
#define	s64(j)	ld_swap64((uint64_t *)data+j, swp##j)
#else
#define	d(j)	data[j]
#endif

		/* First row of quasigroup e-transformations */
#if defined(MACHINE_IS_BIG_ENDIAN)
		s64(8);
		s64(9);
		s64(10);
		s64(11);
		s64(12);
		s64(13);
		s64(14);
		s64(15);
#endif
		LS1_512(defix, d(15), d(14), d(13), d(12), d(11), d(10), d(9),
		    d(8));
#if defined(MACHINE_IS_BIG_ENDIAN)
		s64(0);
		s64(1);
		s64(2);
		s64(3);
		s64(4);
		s64(5);
		s64(6);
		s64(7);
#undef s64
#endif
		LS2_512(defix, d(0), d(1), d(2), d(3), d(4), d(5), d(6), d(7));
		quasi_exform512(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_512(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_512(defix, d(8), d(9), d(10), d(11), d(12), d(13), d(14),
		    d(15));
		quasi_exform512(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Second row of quasigroup e-transformations */
		LS1_512(defix, p[8], p[9], p[10], p[11], p[12], p[13], p[14],
		    p[15]);
		LS2_512(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		quasi_exform512(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_512(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_512(defix, q0, q1, q2, q3, q4, q5, q6, q7);
		quasi_exform512(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Third row of quasigroup e-transformations */
		LS1_512(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_512(defix, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
		quasi_exform512(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_512(defix, q0, q1, q2, q3, q4, q5, q6, q7);
		LS2_512(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		quasi_exform512(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Fourth row of quasigroup e-transformations */
		LS1_512(defix, d(7), d(6), d(5), d(4), d(3), d(2), d(1), d(0));
		LS2_512(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		quasi_exform512(p0, p1, p2, p3, p4, p5, p6, p7);

		LS1_512(defix, p0, p1, p2, p3, p4, p5, p6, p7);
		LS2_512(defix, q0, q1, q2, q3, q4, q5, q6, q7);
		quasi_exform512(q0, q1, q2, q3, q4, q5, q6, q7);

		/* Edon-R tweak on the original SHA-3 Edon-R submission. */
		p[0] ^= d(8) ^ p0;
		p[1] ^= d(9) ^ p1;
		p[2] ^= d(10) ^ p2;
		p[3] ^= d(11) ^ p3;
		p[4] ^= d(12) ^ p4;
		p[5] ^= d(13) ^ p5;
		p[6] ^= d(14) ^ p6;
		p[7] ^= d(15) ^ p7;
		p[8] ^= d(0) ^ q0;
		p[9] ^= d(1) ^ q1;
		p[10] ^= d(2) ^ q2;
		p[11] ^= d(3) ^ q3;
		p[12] ^= d(4) ^ q4;
		p[13] ^= d(5) ^ q5;
		p[14] ^= d(6) ^ q6;
		p[15] ^= d(7) ^ q7;
	}

#undef d
	return (bitlen - bl);
}

void
EdonRInit(EdonRState *state, size_t hashbitlen)
{
	ASSERT(EDONR_VALID_HASHBITLEN(hashbitlen));
	switch (hashbitlen) {
	case 224:
		state->hashbitlen = 224;
		state->bits_processed = 0;
		state->unprocessed_bits = 0;
		bcopy(i224p2, hashState224(state)->DoublePipe,
		    16 * sizeof (uint32_t));
		break;

	case 256:
		state->hashbitlen = 256;
		state->bits_processed = 0;
		state->unprocessed_bits = 0;
		bcopy(i256p2, hashState256(state)->DoublePipe,
		    16 * sizeof (uint32_t));
		break;

	case 384:
		state->hashbitlen = 384;
		state->bits_processed = 0;
		state->unprocessed_bits = 0;
		bcopy(i384p2, hashState384(state)->DoublePipe,
		    16 * sizeof (uint64_t));
		break;

	case 512:
		state->hashbitlen = 512;
		state->bits_processed = 0;
		state->unprocessed_bits = 0;
		bcopy(i512p2, hashState224(state)->DoublePipe,
		    16 * sizeof (uint64_t));
		break;
	}
}


void
EdonRUpdate(EdonRState *state, const uint8_t *data, size_t databitlen)
{
	uint32_t *data32;
	uint64_t *data64;

	size_t bits_processed;

	ASSERT(EDONR_VALID_HASHBITLEN(state->hashbitlen));
	switch (state->hashbitlen) {
	case 224:
	case 256:
		if (state->unprocessed_bits > 0) {
			/* LastBytes = databitlen / 8 */
			int LastBytes = (int)databitlen >> 3;

			ASSERT(state->unprocessed_bits + databitlen <=
			    EdonR256_BLOCK_SIZE * 8);

			bcopy(data, hashState256(state)->LastPart
			    + (state->unprocessed_bits >> 3), LastBytes);
			state->unprocessed_bits += (int)databitlen;
			databitlen = state->unprocessed_bits;
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			data32 = (uint32_t *)hashState256(state)->LastPart;
		} else
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			data32 = (uint32_t *)data;

		bits_processed = Q256(databitlen, data32,
		    hashState256(state)->DoublePipe);
		state->bits_processed += bits_processed;
		databitlen -= bits_processed;
		state->unprocessed_bits = (int)databitlen;
		if (databitlen > 0) {
			/* LastBytes = Ceil(databitlen / 8) */
			int LastBytes =
			    ((~(((-(int)databitlen) >> 3) & 0x01ff)) +
			    1) & 0x01ff;

			data32 += bits_processed >> 5;	/* byte size update */
			bcopy(data32, hashState256(state)->LastPart, LastBytes);
		}
		break;

	case 384:
	case 512:
		if (state->unprocessed_bits > 0) {
			/* LastBytes = databitlen / 8 */
			int LastBytes = (int)databitlen >> 3;

			ASSERT(state->unprocessed_bits + databitlen <=
			    EdonR512_BLOCK_SIZE * 8);

			bcopy(data, hashState512(state)->LastPart
			    + (state->unprocessed_bits >> 3), LastBytes);
			state->unprocessed_bits += (int)databitlen;
			databitlen = state->unprocessed_bits;
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			data64 = (uint64_t *)hashState512(state)->LastPart;
		} else
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			data64 = (uint64_t *)data;

		bits_processed = Q512(databitlen, data64,
		    hashState512(state)->DoublePipe);
		state->bits_processed += bits_processed;
		databitlen -= bits_processed;
		state->unprocessed_bits = (int)databitlen;
		if (databitlen > 0) {
			/* LastBytes = Ceil(databitlen / 8) */
			int LastBytes =
			    ((~(((-(int)databitlen) >> 3) & 0x03ff)) +
			    1) & 0x03ff;

			data64 += bits_processed >> 6;	/* byte size update */
			bcopy(data64, hashState512(state)->LastPart, LastBytes);
		}
		break;
	}
}

void
EdonRFinal(EdonRState *state, uint8_t *hashval)
{
	uint32_t *data32;
	uint64_t *data64, num_bits;

	size_t databitlen;
	int LastByte, PadOnePosition;

	num_bits = state->bits_processed + state->unprocessed_bits;
	ASSERT(EDONR_VALID_HASHBITLEN(state->hashbitlen));
	switch (state->hashbitlen) {
	case 224:
	case 256:
		LastByte = (int)state->unprocessed_bits >> 3;
		PadOnePosition = 7 - (state->unprocessed_bits & 0x07);
		hashState256(state)->LastPart[LastByte] =
		    (hashState256(state)->LastPart[LastByte]
		    & (0xff << (PadOnePosition + 1))) ^
		    (0x01 << PadOnePosition);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		data64 = (uint64_t *)hashState256(state)->LastPart;

		if (state->unprocessed_bits < 448) {
			(void) memset((hashState256(state)->LastPart) +
			    LastByte + 1, 0x00,
			    EdonR256_BLOCK_SIZE - LastByte - 9);
			databitlen = EdonR256_BLOCK_SIZE * 8;
#if defined(MACHINE_IS_BIG_ENDIAN)
			st_swap64(num_bits, data64 + 7);
#else
			data64[7] = num_bits;
#endif
		} else {
			(void) memset((hashState256(state)->LastPart) +
			    LastByte + 1, 0x00,
			    EdonR256_BLOCK_SIZE * 2 - LastByte - 9);
			databitlen = EdonR256_BLOCK_SIZE * 16;
#if defined(MACHINE_IS_BIG_ENDIAN)
			st_swap64(num_bits, data64 + 15);
#else
			data64[15] = num_bits;
#endif
		}

		/* LINTED E_BAD_PTR_CAST_ALIGN */
		data32 = (uint32_t *)hashState256(state)->LastPart;
		state->bits_processed += Q256(databitlen, data32,
		    hashState256(state)->DoublePipe);
		break;

	case 384:
	case 512:
		LastByte = (int)state->unprocessed_bits >> 3;
		PadOnePosition = 7 - (state->unprocessed_bits & 0x07);
		hashState512(state)->LastPart[LastByte] =
		    (hashState512(state)->LastPart[LastByte]
		    & (0xff << (PadOnePosition + 1))) ^
		    (0x01 << PadOnePosition);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		data64 = (uint64_t *)hashState512(state)->LastPart;

		if (state->unprocessed_bits < 960) {
			(void) memset((hashState512(state)->LastPart) +
			    LastByte + 1, 0x00,
			    EdonR512_BLOCK_SIZE - LastByte - 9);
			databitlen = EdonR512_BLOCK_SIZE * 8;
#if defined(MACHINE_IS_BIG_ENDIAN)
			st_swap64(num_bits, data64 + 15);
#else
			data64[15] = num_bits;
#endif
		} else {
			(void) memset((hashState512(state)->LastPart) +
			    LastByte + 1, 0x00,
			    EdonR512_BLOCK_SIZE * 2 - LastByte - 9);
			databitlen = EdonR512_BLOCK_SIZE * 16;
#if defined(MACHINE_IS_BIG_ENDIAN)
			st_swap64(num_bits, data64 + 31);
#else
			data64[31] = num_bits;
#endif
		}

		state->bits_processed += Q512(databitlen, data64,
		    hashState512(state)->DoublePipe);
		break;
	}

	switch (state->hashbitlen) {
	case 224: {
#if defined(MACHINE_IS_BIG_ENDIAN)
		uint32_t *d32 = (uint32_t *)hashval;
		uint32_t *s32 = hashState224(state)->DoublePipe + 9;
		int j;

		for (j = 0; j < EdonR224_DIGEST_SIZE >> 2; j++)
			st_swap32(s32[j], d32 + j);
#else
		bcopy(hashState256(state)->DoublePipe + 9, hashval,
		    EdonR224_DIGEST_SIZE);
#endif
		break;
	}
	case 256: {
#if defined(MACHINE_IS_BIG_ENDIAN)
		uint32_t *d32 = (uint32_t *)hashval;
		uint32_t *s32 = hashState224(state)->DoublePipe + 8;
		int j;

		for (j = 0; j < EdonR256_DIGEST_SIZE >> 2; j++)
			st_swap32(s32[j], d32 + j);
#else
		bcopy(hashState256(state)->DoublePipe + 8, hashval,
		    EdonR256_DIGEST_SIZE);
#endif
		break;
	}
	case 384: {
#if defined(MACHINE_IS_BIG_ENDIAN)
		uint64_t *d64 = (uint64_t *)hashval;
		uint64_t *s64 = hashState384(state)->DoublePipe + 10;
		int j;

		for (j = 0; j < EdonR384_DIGEST_SIZE >> 3; j++)
			st_swap64(s64[j], d64 + j);
#else
		bcopy(hashState384(state)->DoublePipe + 10, hashval,
		    EdonR384_DIGEST_SIZE);
#endif
		break;
	}
	case 512: {
#if defined(MACHINE_IS_BIG_ENDIAN)
		uint64_t *d64 = (uint64_t *)hashval;
		uint64_t *s64 = hashState512(state)->DoublePipe + 8;
		int j;

		for (j = 0; j < EdonR512_DIGEST_SIZE >> 3; j++)
			st_swap64(s64[j], d64 + j);
#else
		bcopy(hashState512(state)->DoublePipe + 8, hashval,
		    EdonR512_DIGEST_SIZE);
#endif
		break;
	}
	}
}


void
EdonRHash(size_t hashbitlen, const uint8_t *data, size_t databitlen,
    uint8_t *hashval)
{
	EdonRState state;

	EdonRInit(&state, hashbitlen);
	EdonRUpdate(&state, data, databitlen);
	EdonRFinal(&state, hashval);
}

#ifdef _KERNEL
EXPORT_SYMBOL(EdonRInit);
EXPORT_SYMBOL(EdonRUpdate);
EXPORT_SYMBOL(EdonRHash);
EXPORT_SYMBOL(EdonRFinal);
#endif
