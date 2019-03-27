/*	$OpenBSD: rmd160.c,v 1.3 2001/09/26 21:40:13 markus Exp $	*/
/*-
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Preneel, Bosselaers, Dobbertin, "The Cryptographic Hash Function RIPEMD-160",
 * RSA Laboratories, CryptoBytes, Volume 3, Number 2, Autumn 1997,
 * ftp://ftp.rsasecurity.com/pub/cryptobytes/crypto3n2.pdf
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <opencrypto/rmd160.h>

#define PUT_64BIT_LE(cp, value) do { \
	(cp)[7] = (value) >> 56; \
	(cp)[6] = (value) >> 48; \
	(cp)[5] = (value) >> 40; \
	(cp)[4] = (value) >> 32; \
	(cp)[3] = (value) >> 24; \
	(cp)[2] = (value) >> 16; \
	(cp)[1] = (value) >> 8; \
	(cp)[0] = (value); } while (0)

#define PUT_32BIT_LE(cp, value) do { \
	(cp)[3] = (value) >> 24; \
	(cp)[2] = (value) >> 16; \
	(cp)[1] = (value) >> 8; \
	(cp)[0] = (value); } while (0)

#define	H0	0x67452301U
#define	H1	0xEFCDAB89U
#define	H2	0x98BADCFEU
#define	H3	0x10325476U
#define	H4	0xC3D2E1F0U

#define	K0	0x00000000U
#define	K1	0x5A827999U
#define	K2	0x6ED9EBA1U
#define	K3	0x8F1BBCDCU
#define	K4	0xA953FD4EU

#define	KK0	0x50A28BE6U
#define	KK1	0x5C4DD124U
#define	KK2	0x6D703EF3U
#define	KK3	0x7A6D76E9U
#define	KK4	0x00000000U

/* rotate x left n bits.  */
#define ROL(n, x) (((x) << (n)) | ((x) >> (32-(n))))

#define F0(x, y, z) ((x) ^ (y) ^ (z))
#define F1(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define F2(x, y, z) (((x) | (~y)) ^ (z))
#define F3(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define F4(x, y, z) ((x) ^ ((y) | (~z)))

#define R(a, b, c, d, e, Fj, Kj, sj, rj) \
	do { \
		a = ROL(sj, a + Fj(b,c,d) + X(rj) + Kj) + e; \
		c = ROL(10, c); \
	} while(0)

#define X(i)	x[i]

static u_char PADDING[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void
RMD160Init(RMD160_CTX *ctx)
{
	ctx->count = 0;
	ctx->state[0] = H0;
	ctx->state[1] = H1;
	ctx->state[2] = H2;
	ctx->state[3] = H3;
	ctx->state[4] = H4;
}

void
RMD160Update(RMD160_CTX *ctx, const u_char *input, u_int32_t len)
{
	u_int32_t have, off, need;

	have = (ctx->count/8) % 64;
	need = 64 - have;
	ctx->count += 8 * len;
	off = 0;

	if (len >= need) {
		if (have) {
			memcpy(ctx->buffer + have, input, need);
			RMD160Transform(ctx->state, ctx->buffer);
			off = need;
			have = 0;
		}
		/* now the buffer is empty */
		while (off + 64 <= len) {
			RMD160Transform(ctx->state, input+off);
			off += 64;
		}
	}
	if (off < len)
		memcpy(ctx->buffer + have, input+off, len-off);
}

void
RMD160Final(u_char digest[20], RMD160_CTX *ctx)
{
	int i;
	u_char size[8];
	u_int32_t padlen;

	PUT_64BIT_LE(size, ctx->count);

	/*
	 * pad to 64 byte blocks, at least one byte from PADDING plus 8 bytes
	 * for the size
	 */
	padlen = 64 - ((ctx->count/8) % 64);
	if (padlen < 1 + 8)
		padlen += 64;
	RMD160Update(ctx, PADDING, padlen - 8);		/* padlen - 8 <= 64 */
	RMD160Update(ctx, size, 8);

	if (digest != NULL)
		for (i = 0; i < 5; i++)
			PUT_32BIT_LE(digest + i*4, ctx->state[i]);

	memset(ctx, 0, sizeof (*ctx));
}

void
RMD160Transform(u_int32_t state[5], const u_char block[64])
{
	u_int32_t a, b, c, d, e, aa, bb, cc, dd, ee, t, x[16];

#if BYTE_ORDER == LITTLE_ENDIAN
	memcpy(x, block, 64);
#else
	int i;

	for (i = 0; i < 16; i++)
		x[i] = bswap32(*(const u_int32_t*)(block+i*4));
#endif

	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];

	/* Round 1 */
	R(a, b, c, d, e, F0, K0, 11,  0);
	R(e, a, b, c, d, F0, K0, 14,  1);
	R(d, e, a, b, c, F0, K0, 15,  2);
	R(c, d, e, a, b, F0, K0, 12,  3);
	R(b, c, d, e, a, F0, K0,  5,  4);
	R(a, b, c, d, e, F0, K0,  8,  5);
	R(e, a, b, c, d, F0, K0,  7,  6);
	R(d, e, a, b, c, F0, K0,  9,  7);
	R(c, d, e, a, b, F0, K0, 11,  8);
	R(b, c, d, e, a, F0, K0, 13,  9);
	R(a, b, c, d, e, F0, K0, 14, 10);
	R(e, a, b, c, d, F0, K0, 15, 11);
	R(d, e, a, b, c, F0, K0,  6, 12);
	R(c, d, e, a, b, F0, K0,  7, 13);
	R(b, c, d, e, a, F0, K0,  9, 14);
	R(a, b, c, d, e, F0, K0,  8, 15); /* #15 */
	/* Round 2 */
	R(e, a, b, c, d, F1, K1,  7,  7);
	R(d, e, a, b, c, F1, K1,  6,  4);
	R(c, d, e, a, b, F1, K1,  8, 13);
	R(b, c, d, e, a, F1, K1, 13,  1);
	R(a, b, c, d, e, F1, K1, 11, 10);
	R(e, a, b, c, d, F1, K1,  9,  6);
	R(d, e, a, b, c, F1, K1,  7, 15);
	R(c, d, e, a, b, F1, K1, 15,  3);
	R(b, c, d, e, a, F1, K1,  7, 12);
	R(a, b, c, d, e, F1, K1, 12,  0);
	R(e, a, b, c, d, F1, K1, 15,  9);
	R(d, e, a, b, c, F1, K1,  9,  5);
	R(c, d, e, a, b, F1, K1, 11,  2);
	R(b, c, d, e, a, F1, K1,  7, 14);
	R(a, b, c, d, e, F1, K1, 13, 11);
	R(e, a, b, c, d, F1, K1, 12,  8); /* #31 */
	/* Round 3 */
	R(d, e, a, b, c, F2, K2, 11,  3);
	R(c, d, e, a, b, F2, K2, 13, 10);
	R(b, c, d, e, a, F2, K2,  6, 14);
	R(a, b, c, d, e, F2, K2,  7,  4);
	R(e, a, b, c, d, F2, K2, 14,  9);
	R(d, e, a, b, c, F2, K2,  9, 15);
	R(c, d, e, a, b, F2, K2, 13,  8);
	R(b, c, d, e, a, F2, K2, 15,  1);
	R(a, b, c, d, e, F2, K2, 14,  2);
	R(e, a, b, c, d, F2, K2,  8,  7);
	R(d, e, a, b, c, F2, K2, 13,  0);
	R(c, d, e, a, b, F2, K2,  6,  6);
	R(b, c, d, e, a, F2, K2,  5, 13);
	R(a, b, c, d, e, F2, K2, 12, 11);
	R(e, a, b, c, d, F2, K2,  7,  5);
	R(d, e, a, b, c, F2, K2,  5, 12); /* #47 */
	/* Round 4 */
	R(c, d, e, a, b, F3, K3, 11,  1);
	R(b, c, d, e, a, F3, K3, 12,  9);
	R(a, b, c, d, e, F3, K3, 14, 11);
	R(e, a, b, c, d, F3, K3, 15, 10);
	R(d, e, a, b, c, F3, K3, 14,  0);
	R(c, d, e, a, b, F3, K3, 15,  8);
	R(b, c, d, e, a, F3, K3,  9, 12);
	R(a, b, c, d, e, F3, K3,  8,  4);
	R(e, a, b, c, d, F3, K3,  9, 13);
	R(d, e, a, b, c, F3, K3, 14,  3);
	R(c, d, e, a, b, F3, K3,  5,  7);
	R(b, c, d, e, a, F3, K3,  6, 15);
	R(a, b, c, d, e, F3, K3,  8, 14);
	R(e, a, b, c, d, F3, K3,  6,  5);
	R(d, e, a, b, c, F3, K3,  5,  6);
	R(c, d, e, a, b, F3, K3, 12,  2); /* #63 */
	/* Round 5 */
	R(b, c, d, e, a, F4, K4,  9,  4);
	R(a, b, c, d, e, F4, K4, 15,  0);
	R(e, a, b, c, d, F4, K4,  5,  5);
	R(d, e, a, b, c, F4, K4, 11,  9);
	R(c, d, e, a, b, F4, K4,  6,  7);
	R(b, c, d, e, a, F4, K4,  8, 12);
	R(a, b, c, d, e, F4, K4, 13,  2);
	R(e, a, b, c, d, F4, K4, 12, 10);
	R(d, e, a, b, c, F4, K4,  5, 14);
	R(c, d, e, a, b, F4, K4, 12,  1);
	R(b, c, d, e, a, F4, K4, 13,  3);
	R(a, b, c, d, e, F4, K4, 14,  8);
	R(e, a, b, c, d, F4, K4, 11, 11);
	R(d, e, a, b, c, F4, K4,  8,  6);
	R(c, d, e, a, b, F4, K4,  5, 15);
	R(b, c, d, e, a, F4, K4,  6, 13); /* #79 */

	aa = a ; bb = b; cc = c; dd = d; ee = e;

	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];

	/* Parallel round 1 */
	R(a, b, c, d, e, F4, KK0,  8,  5);
	R(e, a, b, c, d, F4, KK0,  9, 14);
	R(d, e, a, b, c, F4, KK0,  9,  7);
	R(c, d, e, a, b, F4, KK0, 11,  0);
	R(b, c, d, e, a, F4, KK0, 13,  9);
	R(a, b, c, d, e, F4, KK0, 15,  2);
	R(e, a, b, c, d, F4, KK0, 15, 11);
	R(d, e, a, b, c, F4, KK0,  5,  4);
	R(c, d, e, a, b, F4, KK0,  7, 13);
	R(b, c, d, e, a, F4, KK0,  7,  6);
	R(a, b, c, d, e, F4, KK0,  8, 15);
	R(e, a, b, c, d, F4, KK0, 11,  8);
	R(d, e, a, b, c, F4, KK0, 14,  1);
	R(c, d, e, a, b, F4, KK0, 14, 10);
	R(b, c, d, e, a, F4, KK0, 12,  3);
	R(a, b, c, d, e, F4, KK0,  6, 12); /* #15 */
	/* Parallel round 2 */
	R(e, a, b, c, d, F3, KK1,  9,  6);
	R(d, e, a, b, c, F3, KK1, 13, 11);
	R(c, d, e, a, b, F3, KK1, 15,  3);
	R(b, c, d, e, a, F3, KK1,  7,  7);
	R(a, b, c, d, e, F3, KK1, 12,  0);
	R(e, a, b, c, d, F3, KK1,  8, 13);
	R(d, e, a, b, c, F3, KK1,  9,  5);
	R(c, d, e, a, b, F3, KK1, 11, 10);
	R(b, c, d, e, a, F3, KK1,  7, 14);
	R(a, b, c, d, e, F3, KK1,  7, 15);
	R(e, a, b, c, d, F3, KK1, 12,  8);
	R(d, e, a, b, c, F3, KK1,  7, 12);
	R(c, d, e, a, b, F3, KK1,  6,  4);
	R(b, c, d, e, a, F3, KK1, 15,  9);
	R(a, b, c, d, e, F3, KK1, 13,  1);
	R(e, a, b, c, d, F3, KK1, 11,  2); /* #31 */
	/* Parallel round 3 */
	R(d, e, a, b, c, F2, KK2,  9, 15);
	R(c, d, e, a, b, F2, KK2,  7,  5);
	R(b, c, d, e, a, F2, KK2, 15,  1);
	R(a, b, c, d, e, F2, KK2, 11,  3);
	R(e, a, b, c, d, F2, KK2,  8,  7);
	R(d, e, a, b, c, F2, KK2,  6, 14);
	R(c, d, e, a, b, F2, KK2,  6,  6);
	R(b, c, d, e, a, F2, KK2, 14,  9);
	R(a, b, c, d, e, F2, KK2, 12, 11);
	R(e, a, b, c, d, F2, KK2, 13,  8);
	R(d, e, a, b, c, F2, KK2,  5, 12);
	R(c, d, e, a, b, F2, KK2, 14,  2);
	R(b, c, d, e, a, F2, KK2, 13, 10);
	R(a, b, c, d, e, F2, KK2, 13,  0);
	R(e, a, b, c, d, F2, KK2,  7,  4);
	R(d, e, a, b, c, F2, KK2,  5, 13); /* #47 */
	/* Parallel round 4 */
	R(c, d, e, a, b, F1, KK3, 15,  8);
	R(b, c, d, e, a, F1, KK3,  5,  6);
	R(a, b, c, d, e, F1, KK3,  8,  4);
	R(e, a, b, c, d, F1, KK3, 11,  1);
	R(d, e, a, b, c, F1, KK3, 14,  3);
	R(c, d, e, a, b, F1, KK3, 14, 11);
	R(b, c, d, e, a, F1, KK3,  6, 15);
	R(a, b, c, d, e, F1, KK3, 14,  0);
	R(e, a, b, c, d, F1, KK3,  6,  5);
	R(d, e, a, b, c, F1, KK3,  9, 12);
	R(c, d, e, a, b, F1, KK3, 12,  2);
	R(b, c, d, e, a, F1, KK3,  9, 13);
	R(a, b, c, d, e, F1, KK3, 12,  9);
	R(e, a, b, c, d, F1, KK3,  5,  7);
	R(d, e, a, b, c, F1, KK3, 15, 10);
	R(c, d, e, a, b, F1, KK3,  8, 14); /* #63 */
	/* Parallel round 5 */
	R(b, c, d, e, a, F0, KK4,  8, 12);
	R(a, b, c, d, e, F0, KK4,  5, 15);
	R(e, a, b, c, d, F0, KK4, 12, 10);
	R(d, e, a, b, c, F0, KK4,  9,  4);
	R(c, d, e, a, b, F0, KK4, 12,  1);
	R(b, c, d, e, a, F0, KK4,  5,  5);
	R(a, b, c, d, e, F0, KK4, 14,  8);
	R(e, a, b, c, d, F0, KK4,  6,  7);
	R(d, e, a, b, c, F0, KK4,  8,  6);
	R(c, d, e, a, b, F0, KK4, 13,  2);
	R(b, c, d, e, a, F0, KK4,  6, 13);
	R(a, b, c, d, e, F0, KK4,  5, 14);
	R(e, a, b, c, d, F0, KK4, 15,  0);
	R(d, e, a, b, c, F0, KK4, 13,  3);
	R(c, d, e, a, b, F0, KK4, 11,  9);
	R(b, c, d, e, a, F0, KK4, 11, 11); /* #79 */

	t =        state[1] + cc + d;
	state[1] = state[2] + dd + e;
	state[2] = state[3] + ee + a;
	state[3] = state[4] + aa + b;
	state[4] = state[0] + bb + c;
	state[0] = t;
}
