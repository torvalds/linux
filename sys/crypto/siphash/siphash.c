/*-
 * Copyright (c) 2013 Andre Oppermann <andre@FreeBSD.org>
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
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

/*
 * SipHash is a family of PRFs SipHash-c-d where the integer parameters c and d
 * are the number of compression rounds and the number of finalization rounds.
 * A compression round is identical to a finalization round and this round
 * function is called SipRound.  Given a 128-bit key k and a (possibly empty)
 * byte string m, SipHash-c-d returns a 64-bit value SipHash-c-d(k; m).
 *
 * Implemented from the paper "SipHash: a fast short-input PRF", 2012.09.18,
 * by Jean-Philippe Aumasson and Daniel J. Bernstein,
 * Permanent Document ID b9a943a805fbfc6fde808af9fc0ecdfa
 * https://131002.net/siphash/siphash.pdf
 * https://131002.net/siphash/
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/libkern.h>
#include <sys/endian.h>

#include <crypto/siphash/siphash.h>

static void	SipRounds(SIPHASH_CTX *ctx, int final);

void
SipHash_InitX(SIPHASH_CTX *ctx, int rc, int rf)
{

	ctx->v[0] = 0x736f6d6570736575ull;
	ctx->v[1] = 0x646f72616e646f6dull;
	ctx->v[2] = 0x6c7967656e657261ull;
	ctx->v[3] = 0x7465646279746573ull;
	ctx->buf.b64 = 0;
	ctx->bytes = 0;
	ctx->buflen = 0;
	ctx->rounds_compr = rc;
	ctx->rounds_final = rf;
	ctx->initialized = 1;
}

void
SipHash_SetKey(SIPHASH_CTX *ctx, const uint8_t key[static SIPHASH_KEY_LENGTH])
{
	uint64_t k[2];

	KASSERT(ctx->v[0] == 0x736f6d6570736575ull &&
	    ctx->initialized == 1,
	    ("%s: context %p not properly initialized", __func__, ctx));

	k[0] = le64dec(&key[0]);
	k[1] = le64dec(&key[8]);

	ctx->v[0] ^= k[0];
	ctx->v[1] ^= k[1];
	ctx->v[2] ^= k[0];
	ctx->v[3] ^= k[1];

	ctx->initialized = 2;
}

static size_t
SipBuf(SIPHASH_CTX *ctx, const uint8_t **src, size_t len, int final)
{
	size_t x = 0;

	KASSERT((!final && len > 0) || (final && len == 0),
	    ("%s: invalid parameters", __func__));

	if (!final) {
		x = MIN(len, sizeof(ctx->buf.b64) - ctx->buflen);
		bcopy(*src, &ctx->buf.b8[ctx->buflen], x);
		ctx->buflen += x;
		*src += x;
	} else
		ctx->buf.b8[7] = (uint8_t)ctx->bytes;

	if (ctx->buflen == 8 || final) {
		ctx->v[3] ^= le64toh(ctx->buf.b64);
		SipRounds(ctx, 0);
		ctx->v[0] ^= le64toh(ctx->buf.b64);
		ctx->buf.b64 = 0;
		ctx->buflen = 0;
	}
	return (x);
}

void
SipHash_Update(SIPHASH_CTX *ctx, const void *src, size_t len)
{
	uint64_t m;
	const uint64_t *p;
	const uint8_t *s;
	size_t rem;

	KASSERT(ctx->initialized == 2,
	    ("%s: context %p not properly initialized", __func__, ctx));

	s = src;
	ctx->bytes += len;

	/*
	 * Push length smaller than block size into buffer or
	 * fill up the buffer if there is already something
	 * in it.
	 */
	if (ctx->buflen > 0 || len < 8)
		len -= SipBuf(ctx, &s, len, 0);
	if (len == 0)
		return;

	rem = len & 0x7;
	len >>= 3;

	/* Optimze for 64bit aligned/unaligned access. */
	if (((uintptr_t)s & 0x7) == 0) {
		for (p = (const uint64_t *)s; len > 0; len--, p++) {
			m = le64toh(*p);
			ctx->v[3] ^= m;
			SipRounds(ctx, 0);
			ctx->v[0] ^= m;
		}
		s = (const uint8_t *)p;
	} else {
		for (; len > 0; len--, s += 8) {
			m = le64dec(s);
			ctx->v[3] ^= m;
			SipRounds(ctx, 0);
			ctx->v[0] ^= m;
		}
	}

	/* Push remainder into buffer. */
	if (rem > 0)
		(void)SipBuf(ctx, &s, rem, 0);
}

void
SipHash_Final(uint8_t dst[static SIPHASH_DIGEST_LENGTH], SIPHASH_CTX *ctx)
{
	uint64_t r;

	KASSERT(ctx->initialized == 2,
	    ("%s: context %p not properly initialized", __func__, ctx));

	r = SipHash_End(ctx);
	le64enc(dst, r);
}

uint64_t
SipHash_End(SIPHASH_CTX *ctx)
{
	uint64_t r;

	KASSERT(ctx->initialized == 2,
	    ("%s: context %p not properly initialized", __func__, ctx));

	SipBuf(ctx, NULL, 0, 1);
	ctx->v[2] ^= 0xff;
	SipRounds(ctx, 1);
	r = (ctx->v[0] ^ ctx->v[1]) ^ (ctx->v[2] ^ ctx->v[3]);

	bzero(ctx, sizeof(*ctx));
	return (r);
}

uint64_t
SipHashX(SIPHASH_CTX *ctx, int rc, int rf,
    const uint8_t key[static SIPHASH_KEY_LENGTH], const void *src, size_t len)
{

	SipHash_InitX(ctx, rc, rf);
	SipHash_SetKey(ctx, key);
	SipHash_Update(ctx, src, len);

	return (SipHash_End(ctx));
}

#define SIP_ROTL(x, b)	(uint64_t)(((x) << (b)) | ( (x) >> (64 - (b))))

static void
SipRounds(SIPHASH_CTX *ctx, int final)
{
	int rounds;

	if (!final)
		rounds = ctx->rounds_compr;
	else
		rounds = ctx->rounds_final;

	while (rounds--) {
		ctx->v[0] += ctx->v[1];
		ctx->v[2] += ctx->v[3];
		ctx->v[1] = SIP_ROTL(ctx->v[1], 13);
		ctx->v[3] = SIP_ROTL(ctx->v[3], 16);

		ctx->v[1] ^= ctx->v[0];
		ctx->v[3] ^= ctx->v[2];
		ctx->v[0] = SIP_ROTL(ctx->v[0], 32);

		ctx->v[2] += ctx->v[1];
		ctx->v[0] += ctx->v[3];
		ctx->v[1] = SIP_ROTL(ctx->v[1], 17);
		ctx->v[3] = SIP_ROTL(ctx->v[3], 21);

		ctx->v[1] ^= ctx->v[2];
		ctx->v[3] ^= ctx->v[0];
		ctx->v[2] = SIP_ROTL(ctx->v[2], 32);
	}
}

