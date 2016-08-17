/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * The basic framework for this code came from the reference
 * implementation for MD5.  That implementation is Copyright (C)
 * 1991-2, RSA Data Security, Inc. Created 1991. All rights reserved.
 *
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 *
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD5 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 *
 * NOTE: Cleaned-up and optimized, version of SHA1, based on the FIPS 180-1
 * standard, available at http://www.itl.nist.gov/fipspubs/fip180-1.htm
 * Not as fast as one would like -- further optimizations are encouraged
 * and appreciated.
 */

#include <sys/zfs_context.h>
#include <sha1/sha1.h>
#include <sha1/sha1_consts.h>

#ifdef _LITTLE_ENDIAN
#include <sys/byteorder.h>
#define	HAVE_HTONL
#endif

#define	_RESTRICT_KYWD

static void Encode(uint8_t *, const uint32_t *, size_t);

#if	defined(__sparc)

#define	SHA1_TRANSFORM(ctx, in) \
	SHA1Transform((ctx)->state[0], (ctx)->state[1], (ctx)->state[2], \
		(ctx)->state[3], (ctx)->state[4], (ctx), (in))

static void SHA1Transform(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
	SHA1_CTX *, const uint8_t *);

#elif	defined(__amd64)

#define	SHA1_TRANSFORM(ctx, in) sha1_block_data_order((ctx), (in), 1)
#define	SHA1_TRANSFORM_BLOCKS(ctx, in, num) sha1_block_data_order((ctx), \
		(in), (num))

void sha1_block_data_order(SHA1_CTX *ctx, const void *inpp, size_t num_blocks);

#else

#define	SHA1_TRANSFORM(ctx, in) SHA1Transform((ctx), (in))

static void SHA1Transform(SHA1_CTX *, const uint8_t *);

#endif


static uint8_t PADDING[64] = { 0x80, /* all zeros */ };

/*
 * F, G, and H are the basic SHA1 functions.
 */
#define	F(b, c, d)	(((b) & (c)) | ((~b) & (d)))
#define	G(b, c, d)	((b) ^ (c) ^ (d))
#define	H(b, c, d)	(((b) & (c)) | (((b)|(c)) & (d)))

/*
 * ROTATE_LEFT rotates x left n bits.
 */

#if	defined(__GNUC__) && defined(_LP64)
static __inline__ uint64_t
ROTATE_LEFT(uint64_t value, uint32_t n)
{
	uint32_t t32;

	t32 = (uint32_t)value;
	return ((t32 << n) | (t32 >> (32 - n)));
}

#else

#define	ROTATE_LEFT(x, n)	\
	(((x) << (n)) | ((x) >> ((sizeof (x) * NBBY)-(n))))

#endif


/*
 * SHA1Init()
 *
 * purpose: initializes the sha1 context and begins and sha1 digest operation
 *   input: SHA1_CTX *	: the context to initializes.
 *  output: void
 */

void
SHA1Init(SHA1_CTX *ctx)
{
	ctx->count[0] = ctx->count[1] = 0;

	/*
	 * load magic initialization constants. Tell lint
	 * that these constants are unsigned by using U.
	 */

	ctx->state[0] = 0x67452301U;
	ctx->state[1] = 0xefcdab89U;
	ctx->state[2] = 0x98badcfeU;
	ctx->state[3] = 0x10325476U;
	ctx->state[4] = 0xc3d2e1f0U;
}

void
SHA1Update(SHA1_CTX *ctx, const void *inptr, size_t input_len)
{
	uint32_t i, buf_index, buf_len;
	const uint8_t *input = inptr;
#if defined(__amd64)
	uint32_t	block_count;
#endif	/* __amd64 */

	/* check for noop */
	if (input_len == 0)
		return;

	/* compute number of bytes mod 64 */
	buf_index = (ctx->count[1] >> 3) & 0x3F;

	/* update number of bits */
	if ((ctx->count[1] += (input_len << 3)) < (input_len << 3))
		ctx->count[0]++;

	ctx->count[0] += (input_len >> 29);

	buf_len = 64 - buf_index;

	/* transform as many times as possible */
	i = 0;
	if (input_len >= buf_len) {

		/*
		 * general optimization:
		 *
		 * only do initial bcopy() and SHA1Transform() if
		 * buf_index != 0.  if buf_index == 0, we're just
		 * wasting our time doing the bcopy() since there
		 * wasn't any data left over from a previous call to
		 * SHA1Update().
		 */

		if (buf_index) {
			bcopy(input, &ctx->buf_un.buf8[buf_index], buf_len);
			SHA1_TRANSFORM(ctx, ctx->buf_un.buf8);
			i = buf_len;
		}

#if !defined(__amd64)
		for (; i + 63 < input_len; i += 64)
			SHA1_TRANSFORM(ctx, &input[i]);
#else
		block_count = (input_len - i) >> 6;
		if (block_count > 0) {
			SHA1_TRANSFORM_BLOCKS(ctx, &input[i], block_count);
			i += block_count << 6;
		}
#endif	/* !__amd64 */

		/*
		 * general optimization:
		 *
		 * if i and input_len are the same, return now instead
		 * of calling bcopy(), since the bcopy() in this case
		 * will be an expensive nop.
		 */

		if (input_len == i)
			return;

		buf_index = 0;
	}

	/* buffer remaining input */
	bcopy(&input[i], &ctx->buf_un.buf8[buf_index], input_len - i);
}

/*
 * SHA1Final()
 *
 * purpose: ends an sha1 digest operation, finalizing the message digest and
 *          zeroing the context.
 *   input: uchar_t *	: A buffer to store the digest.
 *			: The function actually uses void* because many
 *			: callers pass things other than uchar_t here.
 *          SHA1_CTX *  : the context to finalize, save, and zero
 *  output: void
 */

void
SHA1Final(void *digest, SHA1_CTX *ctx)
{
	uint8_t		bitcount_be[sizeof (ctx->count)];
	uint32_t	index = (ctx->count[1] >> 3) & 0x3f;

	/* store bit count, big endian */
	Encode(bitcount_be, ctx->count, sizeof (bitcount_be));

	/* pad out to 56 mod 64 */
	SHA1Update(ctx, PADDING, ((index < 56) ? 56 : 120) - index);

	/* append length (before padding) */
	SHA1Update(ctx, bitcount_be, sizeof (bitcount_be));

	/* store state in digest */
	Encode(digest, ctx->state, sizeof (ctx->state));

	/* zeroize sensitive information */
	bzero(ctx, sizeof (*ctx));
}


#if !defined(__amd64)

typedef uint32_t sha1word;

/*
 * sparc optimization:
 *
 * on the sparc, we can load big endian 32-bit data easily.  note that
 * special care must be taken to ensure the address is 32-bit aligned.
 * in the interest of speed, we don't check to make sure, since
 * careful programming can guarantee this for us.
 */

#if	defined(_BIG_ENDIAN)
#define	LOAD_BIG_32(addr)	(*(uint32_t *)(addr))

#elif	defined(HAVE_HTONL)
#define	LOAD_BIG_32(addr) htonl(*((uint32_t *)(addr)))

#else
/* little endian -- will work on big endian, but slowly */
#define	LOAD_BIG_32(addr)	\
	(((addr)[0] << 24) | ((addr)[1] << 16) | ((addr)[2] << 8) | (addr)[3])
#endif	/* _BIG_ENDIAN */

/*
 * SHA1Transform()
 */
#if	defined(W_ARRAY)
#define	W(n) w[n]
#else	/* !defined(W_ARRAY) */
#define	W(n) w_ ## n
#endif	/* !defined(W_ARRAY) */

#if	defined(__sparc)


/*
 * sparc register window optimization:
 *
 * `a', `b', `c', `d', and `e' are passed into SHA1Transform
 * explicitly since it increases the number of registers available to
 * the compiler.  under this scheme, these variables can be held in
 * %i0 - %i4, which leaves more local and out registers available.
 *
 * purpose: sha1 transformation -- updates the digest based on `block'
 *   input: uint32_t	: bytes  1 -  4 of the digest
 *          uint32_t	: bytes  5 -  8 of the digest
 *          uint32_t	: bytes  9 - 12 of the digest
 *          uint32_t	: bytes 12 - 16 of the digest
 *          uint32_t	: bytes 16 - 20 of the digest
 *          SHA1_CTX *	: the context to update
 *          uint8_t [64]: the block to use to update the digest
 *  output: void
 */


void
SHA1Transform(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
    SHA1_CTX *ctx, const uint8_t blk[64])
{
	/*
	 * sparc optimization:
	 *
	 * while it is somewhat counter-intuitive, on sparc, it is
	 * more efficient to place all the constants used in this
	 * function in an array and load the values out of the array
	 * than to manually load the constants.  this is because
	 * setting a register to a 32-bit value takes two ops in most
	 * cases: a `sethi' and an `or', but loading a 32-bit value
	 * from memory only takes one `ld' (or `lduw' on v9).  while
	 * this increases memory usage, the compiler can find enough
	 * other things to do while waiting to keep the pipeline does
	 * not stall.  additionally, it is likely that many of these
	 * constants are cached so that later accesses do not even go
	 * out to the bus.
	 *
	 * this array is declared `static' to keep the compiler from
	 * having to bcopy() this array onto the stack frame of
	 * SHA1Transform() each time it is called -- which is
	 * unacceptably expensive.
	 *
	 * the `const' is to ensure that callers are good citizens and
	 * do not try to munge the array.  since these routines are
	 * going to be called from inside multithreaded kernelland,
	 * this is a good safety check. -- `sha1_consts' will end up in
	 * .rodata.
	 *
	 * unfortunately, loading from an array in this manner hurts
	 * performance under Intel.  So, there is a macro,
	 * SHA1_CONST(), used in SHA1Transform(), that either expands to
	 * a reference to this array, or to the actual constant,
	 * depending on what platform this code is compiled for.
	 */


	static const uint32_t sha1_consts[] = {
		SHA1_CONST_0, SHA1_CONST_1, SHA1_CONST_2, SHA1_CONST_3
	};


	/*
	 * general optimization:
	 *
	 * use individual integers instead of using an array.  this is a
	 * win, although the amount it wins by seems to vary quite a bit.
	 */


	uint32_t	w_0, w_1, w_2,  w_3,  w_4,  w_5,  w_6,  w_7;
	uint32_t	w_8, w_9, w_10, w_11, w_12, w_13, w_14, w_15;


	/*
	 * sparc optimization:
	 *
	 * if `block' is already aligned on a 4-byte boundary, use
	 * LOAD_BIG_32() directly.  otherwise, bcopy() into a
	 * buffer that *is* aligned on a 4-byte boundary and then do
	 * the LOAD_BIG_32() on that buffer.  benchmarks have shown
	 * that using the bcopy() is better than loading the bytes
	 * individually and doing the endian-swap by hand.
	 *
	 * even though it's quite tempting to assign to do:
	 *
	 * blk = bcopy(ctx->buf_un.buf32, blk, sizeof (ctx->buf_un.buf32));
	 *
	 * and only have one set of LOAD_BIG_32()'s, the compiler
	 * *does not* like that, so please resist the urge.
	 */


	if ((uintptr_t)blk & 0x3) {		/* not 4-byte aligned? */
		bcopy(blk, ctx->buf_un.buf32,  sizeof (ctx->buf_un.buf32));
		w_15 = LOAD_BIG_32(ctx->buf_un.buf32 + 15);
		w_14 = LOAD_BIG_32(ctx->buf_un.buf32 + 14);
		w_13 = LOAD_BIG_32(ctx->buf_un.buf32 + 13);
		w_12 = LOAD_BIG_32(ctx->buf_un.buf32 + 12);
		w_11 = LOAD_BIG_32(ctx->buf_un.buf32 + 11);
		w_10 = LOAD_BIG_32(ctx->buf_un.buf32 + 10);
		w_9  = LOAD_BIG_32(ctx->buf_un.buf32 +  9);
		w_8  = LOAD_BIG_32(ctx->buf_un.buf32 +  8);
		w_7  = LOAD_BIG_32(ctx->buf_un.buf32 +  7);
		w_6  = LOAD_BIG_32(ctx->buf_un.buf32 +  6);
		w_5  = LOAD_BIG_32(ctx->buf_un.buf32 +  5);
		w_4  = LOAD_BIG_32(ctx->buf_un.buf32 +  4);
		w_3  = LOAD_BIG_32(ctx->buf_un.buf32 +  3);
		w_2  = LOAD_BIG_32(ctx->buf_un.buf32 +  2);
		w_1  = LOAD_BIG_32(ctx->buf_un.buf32 +  1);
		w_0  = LOAD_BIG_32(ctx->buf_un.buf32 +  0);
	} else {
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_15 = LOAD_BIG_32(blk + 60);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_14 = LOAD_BIG_32(blk + 56);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_13 = LOAD_BIG_32(blk + 52);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_12 = LOAD_BIG_32(blk + 48);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_11 = LOAD_BIG_32(blk + 44);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_10 = LOAD_BIG_32(blk + 40);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_9  = LOAD_BIG_32(blk + 36);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_8  = LOAD_BIG_32(blk + 32);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_7  = LOAD_BIG_32(blk + 28);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_6  = LOAD_BIG_32(blk + 24);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_5  = LOAD_BIG_32(blk + 20);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_4  = LOAD_BIG_32(blk + 16);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_3  = LOAD_BIG_32(blk + 12);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_2  = LOAD_BIG_32(blk +  8);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_1  = LOAD_BIG_32(blk +  4);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		w_0  = LOAD_BIG_32(blk +  0);
	}
#else	/* !defined(__sparc) */

void /* CSTYLED */
SHA1Transform(SHA1_CTX *ctx, const uint8_t blk[64])
{
	/* CSTYLED */
	sha1word a = ctx->state[0];
	sha1word b = ctx->state[1];
	sha1word c = ctx->state[2];
	sha1word d = ctx->state[3];
	sha1word e = ctx->state[4];

#if	defined(W_ARRAY)
	sha1word	w[16];
#else	/* !defined(W_ARRAY) */
	sha1word	w_0, w_1, w_2,  w_3,  w_4,  w_5,  w_6,  w_7;
	sha1word	w_8, w_9, w_10, w_11, w_12, w_13, w_14, w_15;
#endif	/* !defined(W_ARRAY) */

	W(0)  = LOAD_BIG_32((void *)(blk +  0));
	W(1)  = LOAD_BIG_32((void *)(blk +  4));
	W(2)  = LOAD_BIG_32((void *)(blk +  8));
	W(3)  = LOAD_BIG_32((void *)(blk + 12));
	W(4)  = LOAD_BIG_32((void *)(blk + 16));
	W(5)  = LOAD_BIG_32((void *)(blk + 20));
	W(6)  = LOAD_BIG_32((void *)(blk + 24));
	W(7)  = LOAD_BIG_32((void *)(blk + 28));
	W(8)  = LOAD_BIG_32((void *)(blk + 32));
	W(9)  = LOAD_BIG_32((void *)(blk + 36));
	W(10) = LOAD_BIG_32((void *)(blk + 40));
	W(11) = LOAD_BIG_32((void *)(blk + 44));
	W(12) = LOAD_BIG_32((void *)(blk + 48));
	W(13) = LOAD_BIG_32((void *)(blk + 52));
	W(14) = LOAD_BIG_32((void *)(blk + 56));
	W(15) = LOAD_BIG_32((void *)(blk + 60));

#endif /* !defined(__sparc) */

	/*
	 * general optimization:
	 *
	 * even though this approach is described in the standard as
	 * being slower algorithmically, it is 30-40% faster than the
	 * "faster" version under SPARC, because this version has more
	 * of the constraints specified at compile-time and uses fewer
	 * variables (and therefore has better register utilization)
	 * than its "speedier" brother.  (i've tried both, trust me)
	 *
	 * for either method given in the spec, there is an "assignment"
	 * phase where the following takes place:
	 *
	 *	tmp = (main_computation);
	 *	e = d; d = c; c = rotate_left(b, 30); b = a; a = tmp;
	 *
	 * we can make the algorithm go faster by not doing this work,
	 * but just pretending that `d' is now `e', etc. this works
	 * really well and obviates the need for a temporary variable.
	 * however, we still explicitly perform the rotate action,
	 * since it is cheaper on SPARC to do it once than to have to
	 * do it over and over again.
	 */

	/* round 1 */
	e = ROTATE_LEFT(a, 5) + F(b, c, d) + e + W(0) + SHA1_CONST(0); /* 0 */
	b = ROTATE_LEFT(b, 30);

	d = ROTATE_LEFT(e, 5) + F(a, b, c) + d + W(1) + SHA1_CONST(0); /* 1 */
	a = ROTATE_LEFT(a, 30);

	c = ROTATE_LEFT(d, 5) + F(e, a, b) + c + W(2) + SHA1_CONST(0); /* 2 */
	e = ROTATE_LEFT(e, 30);

	b = ROTATE_LEFT(c, 5) + F(d, e, a) + b + W(3) + SHA1_CONST(0); /* 3 */
	d = ROTATE_LEFT(d, 30);

	a = ROTATE_LEFT(b, 5) + F(c, d, e) + a + W(4) + SHA1_CONST(0); /* 4 */
	c = ROTATE_LEFT(c, 30);

	e = ROTATE_LEFT(a, 5) + F(b, c, d) + e + W(5) + SHA1_CONST(0); /* 5 */
	b = ROTATE_LEFT(b, 30);

	d = ROTATE_LEFT(e, 5) + F(a, b, c) + d + W(6) + SHA1_CONST(0); /* 6 */
	a = ROTATE_LEFT(a, 30);

	c = ROTATE_LEFT(d, 5) + F(e, a, b) + c + W(7) + SHA1_CONST(0); /* 7 */
	e = ROTATE_LEFT(e, 30);

	b = ROTATE_LEFT(c, 5) + F(d, e, a) + b + W(8) + SHA1_CONST(0); /* 8 */
	d = ROTATE_LEFT(d, 30);

	a = ROTATE_LEFT(b, 5) + F(c, d, e) + a + W(9) + SHA1_CONST(0); /* 9 */
	c = ROTATE_LEFT(c, 30);

	e = ROTATE_LEFT(a, 5) + F(b, c, d) + e + W(10) + SHA1_CONST(0); /* 10 */
	b = ROTATE_LEFT(b, 30);

	d = ROTATE_LEFT(e, 5) + F(a, b, c) + d + W(11) + SHA1_CONST(0); /* 11 */
	a = ROTATE_LEFT(a, 30);

	c = ROTATE_LEFT(d, 5) + F(e, a, b) + c + W(12) + SHA1_CONST(0); /* 12 */
	e = ROTATE_LEFT(e, 30);

	b = ROTATE_LEFT(c, 5) + F(d, e, a) + b + W(13) + SHA1_CONST(0); /* 13 */
	d = ROTATE_LEFT(d, 30);

	a = ROTATE_LEFT(b, 5) + F(c, d, e) + a + W(14) + SHA1_CONST(0); /* 14 */
	c = ROTATE_LEFT(c, 30);

	e = ROTATE_LEFT(a, 5) + F(b, c, d) + e + W(15) + SHA1_CONST(0); /* 15 */
	b = ROTATE_LEFT(b, 30);

	W(0) = ROTATE_LEFT((W(13) ^ W(8) ^ W(2) ^ W(0)), 1);		/* 16 */
	d = ROTATE_LEFT(e, 5) + F(a, b, c) + d + W(0) + SHA1_CONST(0);
	a = ROTATE_LEFT(a, 30);

	W(1) = ROTATE_LEFT((W(14) ^ W(9) ^ W(3) ^ W(1)), 1);		/* 17 */
	c = ROTATE_LEFT(d, 5) + F(e, a, b) + c + W(1) + SHA1_CONST(0);
	e = ROTATE_LEFT(e, 30);

	W(2) = ROTATE_LEFT((W(15) ^ W(10) ^ W(4) ^ W(2)), 1);	/* 18 */
	b = ROTATE_LEFT(c, 5) + F(d, e, a) + b + W(2) + SHA1_CONST(0);
	d = ROTATE_LEFT(d, 30);

	W(3) = ROTATE_LEFT((W(0) ^ W(11) ^ W(5) ^ W(3)), 1);		/* 19 */
	a = ROTATE_LEFT(b, 5) + F(c, d, e) + a + W(3) + SHA1_CONST(0);
	c = ROTATE_LEFT(c, 30);

	/* round 2 */
	W(4) = ROTATE_LEFT((W(1) ^ W(12) ^ W(6) ^ W(4)), 1);		/* 20 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(4) + SHA1_CONST(1);
	b = ROTATE_LEFT(b, 30);

	W(5) = ROTATE_LEFT((W(2) ^ W(13) ^ W(7) ^ W(5)), 1);		/* 21 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(5) + SHA1_CONST(1);
	a = ROTATE_LEFT(a, 30);

	W(6) = ROTATE_LEFT((W(3) ^ W(14) ^ W(8) ^ W(6)), 1);		/* 22 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(6) + SHA1_CONST(1);
	e = ROTATE_LEFT(e, 30);

	W(7) = ROTATE_LEFT((W(4) ^ W(15) ^ W(9) ^ W(7)), 1);		/* 23 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(7) + SHA1_CONST(1);
	d = ROTATE_LEFT(d, 30);

	W(8) = ROTATE_LEFT((W(5) ^ W(0) ^ W(10) ^ W(8)), 1);		/* 24 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(8) + SHA1_CONST(1);
	c = ROTATE_LEFT(c, 30);

	W(9) = ROTATE_LEFT((W(6) ^ W(1) ^ W(11) ^ W(9)), 1);		/* 25 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(9) + SHA1_CONST(1);
	b = ROTATE_LEFT(b, 30);

	W(10) = ROTATE_LEFT((W(7) ^ W(2) ^ W(12) ^ W(10)), 1);	/* 26 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(10) + SHA1_CONST(1);
	a = ROTATE_LEFT(a, 30);

	W(11) = ROTATE_LEFT((W(8) ^ W(3) ^ W(13) ^ W(11)), 1);	/* 27 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(11) + SHA1_CONST(1);
	e = ROTATE_LEFT(e, 30);

	W(12) = ROTATE_LEFT((W(9) ^ W(4) ^ W(14) ^ W(12)), 1);	/* 28 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(12) + SHA1_CONST(1);
	d = ROTATE_LEFT(d, 30);

	W(13) = ROTATE_LEFT((W(10) ^ W(5) ^ W(15) ^ W(13)), 1);	/* 29 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(13) + SHA1_CONST(1);
	c = ROTATE_LEFT(c, 30);

	W(14) = ROTATE_LEFT((W(11) ^ W(6) ^ W(0) ^ W(14)), 1);	/* 30 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(14) + SHA1_CONST(1);
	b = ROTATE_LEFT(b, 30);

	W(15) = ROTATE_LEFT((W(12) ^ W(7) ^ W(1) ^ W(15)), 1);	/* 31 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(15) + SHA1_CONST(1);
	a = ROTATE_LEFT(a, 30);

	W(0) = ROTATE_LEFT((W(13) ^ W(8) ^ W(2) ^ W(0)), 1);		/* 32 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(0) + SHA1_CONST(1);
	e = ROTATE_LEFT(e, 30);

	W(1) = ROTATE_LEFT((W(14) ^ W(9) ^ W(3) ^ W(1)), 1);		/* 33 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(1) + SHA1_CONST(1);
	d = ROTATE_LEFT(d, 30);

	W(2) = ROTATE_LEFT((W(15) ^ W(10) ^ W(4) ^ W(2)), 1);	/* 34 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(2) + SHA1_CONST(1);
	c = ROTATE_LEFT(c, 30);

	W(3) = ROTATE_LEFT((W(0) ^ W(11) ^ W(5) ^ W(3)), 1);		/* 35 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(3) + SHA1_CONST(1);
	b = ROTATE_LEFT(b, 30);

	W(4) = ROTATE_LEFT((W(1) ^ W(12) ^ W(6) ^ W(4)), 1);		/* 36 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(4) + SHA1_CONST(1);
	a = ROTATE_LEFT(a, 30);

	W(5) = ROTATE_LEFT((W(2) ^ W(13) ^ W(7) ^ W(5)), 1);		/* 37 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(5) + SHA1_CONST(1);
	e = ROTATE_LEFT(e, 30);

	W(6) = ROTATE_LEFT((W(3) ^ W(14) ^ W(8) ^ W(6)), 1);		/* 38 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(6) + SHA1_CONST(1);
	d = ROTATE_LEFT(d, 30);

	W(7) = ROTATE_LEFT((W(4) ^ W(15) ^ W(9) ^ W(7)), 1);		/* 39 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(7) + SHA1_CONST(1);
	c = ROTATE_LEFT(c, 30);

	/* round 3 */
	W(8) = ROTATE_LEFT((W(5) ^ W(0) ^ W(10) ^ W(8)), 1);		/* 40 */
	e = ROTATE_LEFT(a, 5) + H(b, c, d) + e + W(8) + SHA1_CONST(2);
	b = ROTATE_LEFT(b, 30);

	W(9) = ROTATE_LEFT((W(6) ^ W(1) ^ W(11) ^ W(9)), 1);		/* 41 */
	d = ROTATE_LEFT(e, 5) + H(a, b, c) + d + W(9) + SHA1_CONST(2);
	a = ROTATE_LEFT(a, 30);

	W(10) = ROTATE_LEFT((W(7) ^ W(2) ^ W(12) ^ W(10)), 1);	/* 42 */
	c = ROTATE_LEFT(d, 5) + H(e, a, b) + c + W(10) + SHA1_CONST(2);
	e = ROTATE_LEFT(e, 30);

	W(11) = ROTATE_LEFT((W(8) ^ W(3) ^ W(13) ^ W(11)), 1);	/* 43 */
	b = ROTATE_LEFT(c, 5) + H(d, e, a) + b + W(11) + SHA1_CONST(2);
	d = ROTATE_LEFT(d, 30);

	W(12) = ROTATE_LEFT((W(9) ^ W(4) ^ W(14) ^ W(12)), 1);	/* 44 */
	a = ROTATE_LEFT(b, 5) + H(c, d, e) + a + W(12) + SHA1_CONST(2);
	c = ROTATE_LEFT(c, 30);

	W(13) = ROTATE_LEFT((W(10) ^ W(5) ^ W(15) ^ W(13)), 1);	/* 45 */
	e = ROTATE_LEFT(a, 5) + H(b, c, d) + e + W(13) + SHA1_CONST(2);
	b = ROTATE_LEFT(b, 30);

	W(14) = ROTATE_LEFT((W(11) ^ W(6) ^ W(0) ^ W(14)), 1);	/* 46 */
	d = ROTATE_LEFT(e, 5) + H(a, b, c) + d + W(14) + SHA1_CONST(2);
	a = ROTATE_LEFT(a, 30);

	W(15) = ROTATE_LEFT((W(12) ^ W(7) ^ W(1) ^ W(15)), 1);	/* 47 */
	c = ROTATE_LEFT(d, 5) + H(e, a, b) + c + W(15) + SHA1_CONST(2);
	e = ROTATE_LEFT(e, 30);

	W(0) = ROTATE_LEFT((W(13) ^ W(8) ^ W(2) ^ W(0)), 1);		/* 48 */
	b = ROTATE_LEFT(c, 5) + H(d, e, a) + b + W(0) + SHA1_CONST(2);
	d = ROTATE_LEFT(d, 30);

	W(1) = ROTATE_LEFT((W(14) ^ W(9) ^ W(3) ^ W(1)), 1);		/* 49 */
	a = ROTATE_LEFT(b, 5) + H(c, d, e) + a + W(1) + SHA1_CONST(2);
	c = ROTATE_LEFT(c, 30);

	W(2) = ROTATE_LEFT((W(15) ^ W(10) ^ W(4) ^ W(2)), 1);	/* 50 */
	e = ROTATE_LEFT(a, 5) + H(b, c, d) + e + W(2) + SHA1_CONST(2);
	b = ROTATE_LEFT(b, 30);

	W(3) = ROTATE_LEFT((W(0) ^ W(11) ^ W(5) ^ W(3)), 1);		/* 51 */
	d = ROTATE_LEFT(e, 5) + H(a, b, c) + d + W(3) + SHA1_CONST(2);
	a = ROTATE_LEFT(a, 30);

	W(4) = ROTATE_LEFT((W(1) ^ W(12) ^ W(6) ^ W(4)), 1);		/* 52 */
	c = ROTATE_LEFT(d, 5) + H(e, a, b) + c + W(4) + SHA1_CONST(2);
	e = ROTATE_LEFT(e, 30);

	W(5) = ROTATE_LEFT((W(2) ^ W(13) ^ W(7) ^ W(5)), 1);		/* 53 */
	b = ROTATE_LEFT(c, 5) + H(d, e, a) + b + W(5) + SHA1_CONST(2);
	d = ROTATE_LEFT(d, 30);

	W(6) = ROTATE_LEFT((W(3) ^ W(14) ^ W(8) ^ W(6)), 1);		/* 54 */
	a = ROTATE_LEFT(b, 5) + H(c, d, e) + a + W(6) + SHA1_CONST(2);
	c = ROTATE_LEFT(c, 30);

	W(7) = ROTATE_LEFT((W(4) ^ W(15) ^ W(9) ^ W(7)), 1);		/* 55 */
	e = ROTATE_LEFT(a, 5) + H(b, c, d) + e + W(7) + SHA1_CONST(2);
	b = ROTATE_LEFT(b, 30);

	W(8) = ROTATE_LEFT((W(5) ^ W(0) ^ W(10) ^ W(8)), 1);		/* 56 */
	d = ROTATE_LEFT(e, 5) + H(a, b, c) + d + W(8) + SHA1_CONST(2);
	a = ROTATE_LEFT(a, 30);

	W(9) = ROTATE_LEFT((W(6) ^ W(1) ^ W(11) ^ W(9)), 1);		/* 57 */
	c = ROTATE_LEFT(d, 5) + H(e, a, b) + c + W(9) + SHA1_CONST(2);
	e = ROTATE_LEFT(e, 30);

	W(10) = ROTATE_LEFT((W(7) ^ W(2) ^ W(12) ^ W(10)), 1);	/* 58 */
	b = ROTATE_LEFT(c, 5) + H(d, e, a) + b + W(10) + SHA1_CONST(2);
	d = ROTATE_LEFT(d, 30);

	W(11) = ROTATE_LEFT((W(8) ^ W(3) ^ W(13) ^ W(11)), 1);	/* 59 */
	a = ROTATE_LEFT(b, 5) + H(c, d, e) + a + W(11) + SHA1_CONST(2);
	c = ROTATE_LEFT(c, 30);

	/* round 4 */
	W(12) = ROTATE_LEFT((W(9) ^ W(4) ^ W(14) ^ W(12)), 1);	/* 60 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(12) + SHA1_CONST(3);
	b = ROTATE_LEFT(b, 30);

	W(13) = ROTATE_LEFT((W(10) ^ W(5) ^ W(15) ^ W(13)), 1);	/* 61 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(13) + SHA1_CONST(3);
	a = ROTATE_LEFT(a, 30);

	W(14) = ROTATE_LEFT((W(11) ^ W(6) ^ W(0) ^ W(14)), 1);	/* 62 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(14) + SHA1_CONST(3);
	e = ROTATE_LEFT(e, 30);

	W(15) = ROTATE_LEFT((W(12) ^ W(7) ^ W(1) ^ W(15)), 1);	/* 63 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(15) + SHA1_CONST(3);
	d = ROTATE_LEFT(d, 30);

	W(0) = ROTATE_LEFT((W(13) ^ W(8) ^ W(2) ^ W(0)), 1);		/* 64 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(0) + SHA1_CONST(3);
	c = ROTATE_LEFT(c, 30);

	W(1) = ROTATE_LEFT((W(14) ^ W(9) ^ W(3) ^ W(1)), 1);		/* 65 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(1) + SHA1_CONST(3);
	b = ROTATE_LEFT(b, 30);

	W(2) = ROTATE_LEFT((W(15) ^ W(10) ^ W(4) ^ W(2)), 1);	/* 66 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(2) + SHA1_CONST(3);
	a = ROTATE_LEFT(a, 30);

	W(3) = ROTATE_LEFT((W(0) ^ W(11) ^ W(5) ^ W(3)), 1);		/* 67 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(3) + SHA1_CONST(3);
	e = ROTATE_LEFT(e, 30);

	W(4) = ROTATE_LEFT((W(1) ^ W(12) ^ W(6) ^ W(4)), 1);		/* 68 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(4) + SHA1_CONST(3);
	d = ROTATE_LEFT(d, 30);

	W(5) = ROTATE_LEFT((W(2) ^ W(13) ^ W(7) ^ W(5)), 1);		/* 69 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(5) + SHA1_CONST(3);
	c = ROTATE_LEFT(c, 30);

	W(6) = ROTATE_LEFT((W(3) ^ W(14) ^ W(8) ^ W(6)), 1);		/* 70 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(6) + SHA1_CONST(3);
	b = ROTATE_LEFT(b, 30);

	W(7) = ROTATE_LEFT((W(4) ^ W(15) ^ W(9) ^ W(7)), 1);		/* 71 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(7) + SHA1_CONST(3);
	a = ROTATE_LEFT(a, 30);

	W(8) = ROTATE_LEFT((W(5) ^ W(0) ^ W(10) ^ W(8)), 1);		/* 72 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(8) + SHA1_CONST(3);
	e = ROTATE_LEFT(e, 30);

	W(9) = ROTATE_LEFT((W(6) ^ W(1) ^ W(11) ^ W(9)), 1);		/* 73 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(9) + SHA1_CONST(3);
	d = ROTATE_LEFT(d, 30);

	W(10) = ROTATE_LEFT((W(7) ^ W(2) ^ W(12) ^ W(10)), 1);	/* 74 */
	a = ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(10) + SHA1_CONST(3);
	c = ROTATE_LEFT(c, 30);

	W(11) = ROTATE_LEFT((W(8) ^ W(3) ^ W(13) ^ W(11)), 1);	/* 75 */
	e = ROTATE_LEFT(a, 5) + G(b, c, d) + e + W(11) + SHA1_CONST(3);
	b = ROTATE_LEFT(b, 30);

	W(12) = ROTATE_LEFT((W(9) ^ W(4) ^ W(14) ^ W(12)), 1);	/* 76 */
	d = ROTATE_LEFT(e, 5) + G(a, b, c) + d + W(12) + SHA1_CONST(3);
	a = ROTATE_LEFT(a, 30);

	W(13) = ROTATE_LEFT((W(10) ^ W(5) ^ W(15) ^ W(13)), 1);	/* 77 */
	c = ROTATE_LEFT(d, 5) + G(e, a, b) + c + W(13) + SHA1_CONST(3);
	e = ROTATE_LEFT(e, 30);

	W(14) = ROTATE_LEFT((W(11) ^ W(6) ^ W(0) ^ W(14)), 1);	/* 78 */
	b = ROTATE_LEFT(c, 5) + G(d, e, a) + b + W(14) + SHA1_CONST(3);
	d = ROTATE_LEFT(d, 30);

	W(15) = ROTATE_LEFT((W(12) ^ W(7) ^ W(1) ^ W(15)), 1);	/* 79 */

	ctx->state[0] += ROTATE_LEFT(b, 5) + G(c, d, e) + a + W(15) +
	    SHA1_CONST(3);
	ctx->state[1] += b;
	ctx->state[2] += ROTATE_LEFT(c, 30);
	ctx->state[3] += d;
	ctx->state[4] += e;

	/* zeroize sensitive information */
	W(0) = W(1) = W(2) = W(3) = W(4) = W(5) = W(6) = W(7) = W(8) = 0;
	W(9) = W(10) = W(11) = W(12) = W(13) = W(14) = W(15) = 0;
}
#endif	/* !__amd64 */


/*
 * Encode()
 *
 * purpose: to convert a list of numbers from little endian to big endian
 *   input: uint8_t *	: place to store the converted big endian numbers
 *	    uint32_t *	: place to get numbers to convert from
 *          size_t	: the length of the input in bytes
 *  output: void
 */

static void
Encode(uint8_t *_RESTRICT_KYWD output, const uint32_t *_RESTRICT_KYWD input,
    size_t len)
{
	size_t		i, j;

#if defined(__sparc)
	if (IS_P2ALIGNED(output, sizeof (uint32_t))) {
		for (i = 0, j = 0; j < len; i++, j += 4) {
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			*((uint32_t *)(output + j)) = input[i];
		}
	} else {
#endif /* little endian -- will work on big endian, but slowly */

		for (i = 0, j = 0; j < len; i++, j += 4) {
			output[j]	= (input[i] >> 24) & 0xff;
			output[j + 1]	= (input[i] >> 16) & 0xff;
			output[j + 2]	= (input[i] >>  8) & 0xff;
			output[j + 3]	= input[i] & 0xff;
		}
#if defined(__sparc)
	}
#endif
}
