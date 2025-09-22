/*	$OpenBSD: siphash.c,v 1.5 2018/01/05 19:05:09 mikeb Exp $ */

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

#include <sys/param.h>
#include <sys/systm.h>

#include <crypto/siphash.h>

static void	SipHash_CRounds(SIPHASH_CTX *, int);
static void	SipHash_Rounds(SIPHASH_CTX *, int);

void
SipHash_Init(SIPHASH_CTX *ctx, const SIPHASH_KEY *key)
{
	uint64_t k0, k1;

	k0 = lemtoh64(&key->k0);
	k1 = lemtoh64(&key->k1);

	ctx->v[0] = 0x736f6d6570736575ULL ^ k0;
	ctx->v[1] = 0x646f72616e646f6dULL ^ k1;
	ctx->v[2] = 0x6c7967656e657261ULL ^ k0;
	ctx->v[3] = 0x7465646279746573ULL ^ k1;

	memset(ctx->buf, 0, sizeof(ctx->buf));
	ctx->bytes = 0;
}

void
SipHash_Update(SIPHASH_CTX *ctx, int rc, int rf, const void *src, size_t len)
{
	const uint8_t *ptr = src;
	size_t left, used;

	if (len == 0)
		return;

	used = ctx->bytes % sizeof(ctx->buf);
	ctx->bytes += len;

	if (used > 0) {
		left = sizeof(ctx->buf) - used;

		if (len >= left) {
			memcpy(&ctx->buf[used], ptr, left);
			SipHash_CRounds(ctx, rc);
			len -= left;
			ptr += left;
		} else {
			memcpy(&ctx->buf[used], ptr, len);
			return;
		}
	}

	while (len >= sizeof(ctx->buf)) {
		memcpy(ctx->buf, ptr, sizeof(ctx->buf));
		SipHash_CRounds(ctx, rc);
		len -= sizeof(ctx->buf);
		ptr += sizeof(ctx->buf);
	}

	if (len > 0)
		memcpy(ctx->buf, ptr, len);
}

void
SipHash_Final(void *dst, SIPHASH_CTX *ctx, int rc, int rf)
{
	uint64_t r;

	htolem64(&r, SipHash_End(ctx, rc, rf));
	memcpy(dst, &r, sizeof r);
}

uint64_t
SipHash_End(SIPHASH_CTX *ctx, int rc, int rf)
{
	uint64_t r;
	size_t left, used;

	used = ctx->bytes % sizeof(ctx->buf);
	left = sizeof(ctx->buf) - used;
	memset(&ctx->buf[used], 0, left - 1);
	ctx->buf[7] = ctx->bytes;

	SipHash_CRounds(ctx, rc);
	ctx->v[2] ^= 0xff;
	SipHash_Rounds(ctx, rf);

	r = (ctx->v[0] ^ ctx->v[1]) ^ (ctx->v[2] ^ ctx->v[3]);
	explicit_bzero(ctx, sizeof(*ctx));
	return (r);
}

uint64_t
SipHash(const SIPHASH_KEY *key, int rc, int rf, const void *src, size_t len)
{
	SIPHASH_CTX ctx;

	SipHash_Init(&ctx, key);
	SipHash_Update(&ctx, rc, rf, src, len);
	return (SipHash_End(&ctx, rc, rf));
}

#define SIP_ROTL(x, b) ((x) << (b)) | ( (x) >> (64 - (b)))

static void
SipHash_Rounds(SIPHASH_CTX *ctx, int rounds)
{
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

static void
SipHash_CRounds(SIPHASH_CTX *ctx, int rounds)
{
	uint64_t m = lemtoh64((uint64_t *)ctx->buf);

	ctx->v[3] ^= m;
	SipHash_Rounds(ctx, rounds);
	ctx->v[0] ^= m;
}
