/*-
 * Copyright 2005 Colin Percival
 * Copyright (c) 2015 Allan Jude <allanjude@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/types.h>

#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif

#include "sha512.h"
#include "sha512t.h"
#include "sha384.h"

#if BYTE_ORDER == BIG_ENDIAN

/* Copy a vector of big-endian uint64_t into a vector of bytes */
#define be64enc_vect(dst, src, len)	\
	memcpy((void *)dst, (const void *)src, (size_t)len)

/* Copy a vector of bytes into a vector of big-endian uint64_t */
#define be64dec_vect(dst, src, len)	\
	memcpy((void *)dst, (const void *)src, (size_t)len)

#else /* BYTE_ORDER != BIG_ENDIAN */

/*
 * Encode a length len/4 vector of (uint64_t) into a length len vector of
 * (unsigned char) in big-endian form.  Assumes len is a multiple of 8.
 */
static void
be64enc_vect(unsigned char *dst, const uint64_t *src, size_t len)
{
	size_t i;

	for (i = 0; i < len / 8; i++)
		be64enc(dst + i * 8, src[i]);
}

/*
 * Decode a big-endian length len vector of (unsigned char) into a length
 * len/4 vector of (uint64_t).  Assumes len is a multiple of 8.
 */
static void
be64dec_vect(uint64_t *dst, const unsigned char *src, size_t len)
{
	size_t i;

	for (i = 0; i < len / 8; i++)
		dst[i] = be64dec(src + i * 8);
}

#endif /* BYTE_ORDER != BIG_ENDIAN */

/* SHA512 round constants. */
static const uint64_t K[80] = {
	0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
	0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
	0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
	0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
	0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
	0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
	0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
	0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
	0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
	0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
	0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
	0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
	0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
	0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
	0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
	0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
	0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
	0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
	0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
	0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
	0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
	0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
	0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
	0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
	0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
	0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
	0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
	0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
	0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
	0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
	0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
	0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
	0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
	0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
	0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
	0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
	0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
	0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
	0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
	0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

/* Elementary functions used by SHA512 */
#define Ch(x, y, z)	((x & (y ^ z)) ^ z)
#define Maj(x, y, z)	((x & (y | z)) | (y & z))
#define SHR(x, n)	(x >> n)
#define ROTR(x, n)	((x >> n) | (x << (64 - n)))
#define S0(x)		(ROTR(x, 28) ^ ROTR(x, 34) ^ ROTR(x, 39))
#define S1(x)		(ROTR(x, 14) ^ ROTR(x, 18) ^ ROTR(x, 41))
#define s0(x)		(ROTR(x, 1) ^ ROTR(x, 8) ^ SHR(x, 7))
#define s1(x)		(ROTR(x, 19) ^ ROTR(x, 61) ^ SHR(x, 6))

/* SHA512 round function */
#define RND(a, b, c, d, e, f, g, h, k)			\
	h += S1(e) + Ch(e, f, g) + k;			\
	d += h;						\
	h += S0(a) + Maj(a, b, c);

/* Adjusted round function for rotating state */
#define RNDr(S, W, i, ii)			\
	RND(S[(80 - i) % 8], S[(81 - i) % 8],	\
	    S[(82 - i) % 8], S[(83 - i) % 8],	\
	    S[(84 - i) % 8], S[(85 - i) % 8],	\
	    S[(86 - i) % 8], S[(87 - i) % 8],	\
	    W[i + ii] + K[i + ii])

/* Message schedule computation */
#define MSCH(W, ii, i)				\
	W[i + ii + 16] = s1(W[i + ii + 14]) + W[i + ii + 9] + s0(W[i + ii + 1]) + W[i + ii]

/*
 * SHA512 block compression function.  The 512-bit state is transformed via
 * the 512-bit input block to produce a new state.
 */
static void
SHA512_Transform(uint64_t * state, const unsigned char block[SHA512_BLOCK_LENGTH])
{
	uint64_t W[80];
	uint64_t S[8];
	int i;

	/* 1. Prepare the first part of the message schedule W. */
	be64dec_vect(W, block, SHA512_BLOCK_LENGTH);

	/* 2. Initialize working variables. */
	memcpy(S, state, SHA512_DIGEST_LENGTH);

	/* 3. Mix. */
	for (i = 0; i < 80; i += 16) {
		RNDr(S, W, 0, i);
		RNDr(S, W, 1, i);
		RNDr(S, W, 2, i);
		RNDr(S, W, 3, i);
		RNDr(S, W, 4, i);
		RNDr(S, W, 5, i);
		RNDr(S, W, 6, i);
		RNDr(S, W, 7, i);
		RNDr(S, W, 8, i);
		RNDr(S, W, 9, i);
		RNDr(S, W, 10, i);
		RNDr(S, W, 11, i);
		RNDr(S, W, 12, i);
		RNDr(S, W, 13, i);
		RNDr(S, W, 14, i);
		RNDr(S, W, 15, i);

		if (i == 64)
			break;
		MSCH(W, 0, i);
		MSCH(W, 1, i);
		MSCH(W, 2, i);
		MSCH(W, 3, i);
		MSCH(W, 4, i);
		MSCH(W, 5, i);
		MSCH(W, 6, i);
		MSCH(W, 7, i);
		MSCH(W, 8, i);
		MSCH(W, 9, i);
		MSCH(W, 10, i);
		MSCH(W, 11, i);
		MSCH(W, 12, i);
		MSCH(W, 13, i);
		MSCH(W, 14, i);
		MSCH(W, 15, i);
	}

	/* 4. Mix local working variables into global state */
	for (i = 0; i < 8; i++)
		state[i] += S[i];
}

static unsigned char PAD[SHA512_BLOCK_LENGTH] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Add padding and terminating bit-count. */
static void
SHA512_Pad(SHA512_CTX * ctx)
{
	size_t r;

	/* Figure out how many bytes we have buffered. */
	r = (ctx->count[1] >> 3) & 0x7f;

	/* Pad to 112 mod 128, transforming if we finish a block en route. */
	if (r < 112) {
		/* Pad to 112 mod 128. */
		memcpy(&ctx->buf[r], PAD, 112 - r);
	} else {
		/* Finish the current block and mix. */
		memcpy(&ctx->buf[r], PAD, 128 - r);
		SHA512_Transform(ctx->state, ctx->buf);

		/* The start of the final block is all zeroes. */
		memset(&ctx->buf[0], 0, 112);
	}

	/* Add the terminating bit-count. */
	be64enc_vect(&ctx->buf[112], ctx->count, 16);

	/* Mix in the final block. */
	SHA512_Transform(ctx->state, ctx->buf);
}

/* SHA-512 initialization.  Begins a SHA-512 operation. */
void
SHA512_Init(SHA512_CTX * ctx)
{

	/* Zero bits processed so far */
	ctx->count[0] = ctx->count[1] = 0;

	/* Magic initialization constants */
	ctx->state[0] = 0x6a09e667f3bcc908ULL;
	ctx->state[1] = 0xbb67ae8584caa73bULL;
	ctx->state[2] = 0x3c6ef372fe94f82bULL;
	ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
	ctx->state[4] = 0x510e527fade682d1ULL;
	ctx->state[5] = 0x9b05688c2b3e6c1fULL;
	ctx->state[6] = 0x1f83d9abfb41bd6bULL;
	ctx->state[7] = 0x5be0cd19137e2179ULL;
}

/* Add bytes into the hash */
void
SHA512_Update(SHA512_CTX * ctx, const void *in, size_t len)
{
	uint64_t bitlen[2];
	uint64_t r;
	const unsigned char *src = in;

	/* Number of bytes left in the buffer from previous updates */
	r = (ctx->count[1] >> 3) & 0x7f;

	/* Convert the length into a number of bits */
	bitlen[1] = ((uint64_t)len) << 3;
	bitlen[0] = ((uint64_t)len) >> 61;

	/* Update number of bits */
	if ((ctx->count[1] += bitlen[1]) < bitlen[1])
		ctx->count[0]++;
	ctx->count[0] += bitlen[0];

	/* Handle the case where we don't need to perform any transforms */
	if (len < SHA512_BLOCK_LENGTH - r) {
		memcpy(&ctx->buf[r], src, len);
		return;
	}

	/* Finish the current block */
	memcpy(&ctx->buf[r], src, SHA512_BLOCK_LENGTH - r);
	SHA512_Transform(ctx->state, ctx->buf);
	src += SHA512_BLOCK_LENGTH - r;
	len -= SHA512_BLOCK_LENGTH - r;

	/* Perform complete blocks */
	while (len >= SHA512_BLOCK_LENGTH) {
		SHA512_Transform(ctx->state, src);
		src += SHA512_BLOCK_LENGTH;
		len -= SHA512_BLOCK_LENGTH;
	}

	/* Copy left over data into buffer */
	memcpy(ctx->buf, src, len);
}

/*
 * SHA-512 finalization.  Pads the input data, exports the hash value,
 * and clears the context state.
 */
void
SHA512_Final(unsigned char digest[static SHA512_DIGEST_LENGTH], SHA512_CTX *ctx)
{

	/* Add padding */
	SHA512_Pad(ctx);

	/* Write the hash */
	be64enc_vect(digest, ctx->state, SHA512_DIGEST_LENGTH);

	/* Clear the context state */
	explicit_bzero(ctx, sizeof(*ctx));
}

/*** SHA-512t: *********************************************************/
/*
 * the SHA512t transforms are identical to SHA512 so reuse the existing function
 */
void
SHA512_224_Init(SHA512_CTX * ctx)
{

	/* Zero bits processed so far */
	ctx->count[0] = ctx->count[1] = 0;

	/* Magic initialization constants */
	ctx->state[0] = 0x8c3d37c819544da2ULL;
	ctx->state[1] = 0x73e1996689dcd4d6ULL;
	ctx->state[2] = 0x1dfab7ae32ff9c82ULL;
	ctx->state[3] = 0x679dd514582f9fcfULL;
	ctx->state[4] = 0x0f6d2b697bd44da8ULL;
	ctx->state[5] = 0x77e36f7304c48942ULL;
	ctx->state[6] = 0x3f9d85a86a1d36c8ULL;
	ctx->state[7] = 0x1112e6ad91d692a1ULL;
}

void
SHA512_224_Update(SHA512_CTX * ctx, const void *in, size_t len)
{

	SHA512_Update(ctx, in, len);
}

void
SHA512_224_Final(unsigned char digest[static SHA512_224_DIGEST_LENGTH], SHA512_CTX * ctx)
{

	/* Add padding */
	SHA512_Pad(ctx);

	/* Write the hash */
	be64enc_vect(digest, ctx->state, SHA512_224_DIGEST_LENGTH);

	/* Clear the context state */
	explicit_bzero(ctx, sizeof(*ctx));
}

void
SHA512_256_Init(SHA512_CTX * ctx)
{

	/* Zero bits processed so far */
	ctx->count[0] = ctx->count[1] = 0;

	/* Magic initialization constants */
	ctx->state[0] = 0x22312194fc2bf72cULL;
	ctx->state[1] = 0x9f555fa3c84c64c2ULL;
	ctx->state[2] = 0x2393b86b6f53b151ULL;
	ctx->state[3] = 0x963877195940eabdULL;
	ctx->state[4] = 0x96283ee2a88effe3ULL;
	ctx->state[5] = 0xbe5e1e2553863992ULL;
	ctx->state[6] = 0x2b0199fc2c85b8aaULL;
	ctx->state[7] = 0x0eb72ddc81c52ca2ULL;
}

void
SHA512_256_Update(SHA512_CTX * ctx, const void *in, size_t len)
{

	SHA512_Update(ctx, in, len);
}

void
SHA512_256_Final(unsigned char digest[static SHA512_256_DIGEST_LENGTH], SHA512_CTX * ctx)
{

	/* Add padding */
	SHA512_Pad(ctx);

	/* Write the hash */
	be64enc_vect(digest, ctx->state, SHA512_256_DIGEST_LENGTH);

	/* Clear the context state */
	explicit_bzero(ctx, sizeof(*ctx));
}

/*** SHA-384: *********************************************************/
/*
 * the SHA384 and SHA512 transforms are identical, so SHA384 is skipped
 */

/* SHA-384 initialization.  Begins a SHA-384 operation. */
void
SHA384_Init(SHA384_CTX * ctx)
{

	/* Zero bits processed so far */
	ctx->count[0] = ctx->count[1] = 0;

	/* Magic initialization constants */
	ctx->state[0] = 0xcbbb9d5dc1059ed8ULL;
	ctx->state[1] = 0x629a292a367cd507ULL;
	ctx->state[2] = 0x9159015a3070dd17ULL;
	ctx->state[3] = 0x152fecd8f70e5939ULL;
	ctx->state[4] = 0x67332667ffc00b31ULL;
	ctx->state[5] = 0x8eb44a8768581511ULL;
	ctx->state[6] = 0xdb0c2e0d64f98fa7ULL;
	ctx->state[7] = 0x47b5481dbefa4fa4ULL;
}

/* Add bytes into the SHA-384 hash */
void
SHA384_Update(SHA384_CTX * ctx, const void *in, size_t len)
{

	SHA512_Update((SHA512_CTX *)ctx, in, len);
}

/*
 * SHA-384 finalization.  Pads the input data, exports the hash value,
 * and clears the context state.
 */
void
SHA384_Final(unsigned char digest[static SHA384_DIGEST_LENGTH], SHA384_CTX *ctx)
{

	/* Add padding */
	SHA512_Pad((SHA512_CTX *)ctx);

	/* Write the hash */
	be64enc_vect(digest, ctx->state, SHA384_DIGEST_LENGTH);

	/* Clear the context state */
	explicit_bzero(ctx, sizeof(*ctx));
}

#ifdef WEAK_REFS
/* When building libmd, provide weak references. Note: this is not
   activated in the context of compiling these sources for internal
   use in libcrypt.
 */
#undef SHA512_Init
__weak_reference(_libmd_SHA512_Init, SHA512_Init);
#undef SHA512_Update
__weak_reference(_libmd_SHA512_Update, SHA512_Update);
#undef SHA512_Final
__weak_reference(_libmd_SHA512_Final, SHA512_Final);
#undef SHA512_Transform
__weak_reference(_libmd_SHA512_Transform, SHA512_Transform);

#undef SHA512_224_Init
__weak_reference(_libmd_SHA512_224_Init, SHA512_224_Init);
#undef SHA512_224_Update
__weak_reference(_libmd_SHA512_224_Update, SHA512_224_Update);
#undef SHA512_224_Final
__weak_reference(_libmd_SHA512_224_Final, SHA512_224_Final);

#undef SHA512_256_Init
__weak_reference(_libmd_SHA512_256_Init, SHA512_256_Init);
#undef SHA512_256_Update
__weak_reference(_libmd_SHA512_256_Update, SHA512_256_Update);
#undef SHA512_256_Final
__weak_reference(_libmd_SHA512_256_Final, SHA512_256_Final);

#undef SHA384_Init
__weak_reference(_libmd_SHA384_Init, SHA384_Init);
#undef SHA384_Update
__weak_reference(_libmd_SHA384_Update, SHA384_Update);
#undef SHA384_Final
__weak_reference(_libmd_SHA384_Final, SHA384_Final);
#endif
