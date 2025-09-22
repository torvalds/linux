/* $OpenBSD: sha1.c,v 1.16 2025/02/14 12:01:58 jsing Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/crypto.h>
#include <openssl/sha.h>

#include "crypto_internal.h"

#if !defined(OPENSSL_NO_SHA1) && !defined(OPENSSL_NO_SHA)

/* Ensure that SHA_LONG and uint32_t are equivalent sizes. */
CTASSERT(sizeof(SHA_LONG) == sizeof(uint32_t));

void sha1_block_data_order(SHA_CTX *ctx, const void *p, size_t num);
void sha1_block_generic(SHA_CTX *ctx, const void *p, size_t num);

#ifndef HAVE_SHA1_BLOCK_GENERIC
static inline SHA_LONG
Ch(SHA_LONG x, SHA_LONG y, SHA_LONG z)
{
	return (x & y) ^ (~x & z);
}

static inline SHA_LONG
Parity(SHA_LONG x, SHA_LONG y, SHA_LONG z)
{
	return x ^ y ^ z;
}

static inline SHA_LONG
Maj(SHA_LONG x, SHA_LONG y, SHA_LONG z)
{
	return (x & y) ^ (x & z) ^ (y & z);
}

static inline void
sha1_msg_schedule_update(SHA_LONG *W0, SHA_LONG W2, SHA_LONG W8, SHA_LONG W13)
{
	*W0 = crypto_rol_u32(W13 ^ W8 ^ W2 ^ *W0, 1);
}

static inline void
sha1_round1(SHA_LONG *a, SHA_LONG *b, SHA_LONG *c, SHA_LONG *d, SHA_LONG *e,
    SHA_LONG Wt)
{
	SHA_LONG Kt, T;

	Kt = 0x5a827999UL;
	T = crypto_rol_u32(*a, 5) + Ch(*b, *c, *d) + *e + Kt + Wt;

	*e = *d;
	*d = *c;
	*c = crypto_rol_u32(*b, 30);
	*b = *a;
	*a = T;
}

static inline void
sha1_round2(SHA_LONG *a, SHA_LONG *b, SHA_LONG *c, SHA_LONG *d, SHA_LONG *e,
    SHA_LONG Wt)
{
	SHA_LONG Kt, T;

	Kt = 0x6ed9eba1UL;
	T = crypto_rol_u32(*a, 5) + Parity(*b, *c, *d) + *e + Kt + Wt;

	*e = *d;
	*d = *c;
	*c = crypto_rol_u32(*b, 30);
	*b = *a;
	*a = T;
}

static inline void
sha1_round3(SHA_LONG *a, SHA_LONG *b, SHA_LONG *c, SHA_LONG *d, SHA_LONG *e,
    SHA_LONG Wt)
{
	SHA_LONG Kt, T;

	Kt = 0x8f1bbcdcUL;
	T = crypto_rol_u32(*a, 5) + Maj(*b, *c, *d) + *e + Kt + Wt;

	*e = *d;
	*d = *c;
	*c = crypto_rol_u32(*b, 30);
	*b = *a;
	*a = T;
}

static inline void
sha1_round4(SHA_LONG *a, SHA_LONG *b, SHA_LONG *c, SHA_LONG *d, SHA_LONG *e,
    SHA_LONG Wt)
{
	SHA_LONG Kt, T;

	Kt = 0xca62c1d6UL;
	T = crypto_rol_u32(*a, 5) + Parity(*b, *c, *d) + *e + Kt + Wt;

	*e = *d;
	*d = *c;
	*c = crypto_rol_u32(*b, 30);
	*b = *a;
	*a = T;
}

void
sha1_block_generic(SHA_CTX *ctx, const void *_in, size_t num)
{
	const uint8_t *in = _in;
	const SHA_LONG *in32;
	unsigned int a, b, c, d, e;
	unsigned int X0, X1, X2, X3, X4, X5, X6, X7,
	    X8, X9, X10, X11, X12, X13, X14, X15;

	while (num--) {
		a = ctx->h0;
		b = ctx->h1;
		c = ctx->h2;
		d = ctx->h3;
		e = ctx->h4;

		if ((size_t)in % 4 == 0) {
			/* Input is 32 bit aligned. */
			in32 = (const SHA_LONG *)in;
			X0 = be32toh(in32[0]);
			X1 = be32toh(in32[1]);
			X2 = be32toh(in32[2]);
			X3 = be32toh(in32[3]);
			X4 = be32toh(in32[4]);
			X5 = be32toh(in32[5]);
			X6 = be32toh(in32[6]);
			X7 = be32toh(in32[7]);
			X8 = be32toh(in32[8]);
			X9 = be32toh(in32[9]);
			X10 = be32toh(in32[10]);
			X11 = be32toh(in32[11]);
			X12 = be32toh(in32[12]);
			X13 = be32toh(in32[13]);
			X14 = be32toh(in32[14]);
			X15 = be32toh(in32[15]);
		} else {
			/* Input is not 32 bit aligned. */
			X0 = crypto_load_be32toh(&in[0 * 4]);
			X1 = crypto_load_be32toh(&in[1 * 4]);
			X2 = crypto_load_be32toh(&in[2 * 4]);
			X3 = crypto_load_be32toh(&in[3 * 4]);
			X4 = crypto_load_be32toh(&in[4 * 4]);
			X5 = crypto_load_be32toh(&in[5 * 4]);
			X6 = crypto_load_be32toh(&in[6 * 4]);
			X7 = crypto_load_be32toh(&in[7 * 4]);
			X8 = crypto_load_be32toh(&in[8 * 4]);
			X9 = crypto_load_be32toh(&in[9 * 4]);
			X10 = crypto_load_be32toh(&in[10 * 4]);
			X11 = crypto_load_be32toh(&in[11 * 4]);
			X12 = crypto_load_be32toh(&in[12 * 4]);
			X13 = crypto_load_be32toh(&in[13 * 4]);
			X14 = crypto_load_be32toh(&in[14 * 4]);
			X15 = crypto_load_be32toh(&in[15 * 4]);
		}
		in += SHA_CBLOCK;

		sha1_round1(&a, &b, &c, &d, &e, X0);
		sha1_round1(&a, &b, &c, &d, &e, X1);
		sha1_round1(&a, &b, &c, &d, &e, X2);
		sha1_round1(&a, &b, &c, &d, &e, X3);
		sha1_round1(&a, &b, &c, &d, &e, X4);
		sha1_round1(&a, &b, &c, &d, &e, X5);
		sha1_round1(&a, &b, &c, &d, &e, X6);
		sha1_round1(&a, &b, &c, &d, &e, X7);
		sha1_round1(&a, &b, &c, &d, &e, X8);
		sha1_round1(&a, &b, &c, &d, &e, X9);
		sha1_round1(&a, &b, &c, &d, &e, X10);
		sha1_round1(&a, &b, &c, &d, &e, X11);
		sha1_round1(&a, &b, &c, &d, &e, X12);
		sha1_round1(&a, &b, &c, &d, &e, X13);
		sha1_round1(&a, &b, &c, &d, &e, X14);
		sha1_round1(&a, &b, &c, &d, &e, X15);

		sha1_msg_schedule_update(&X0, X2, X8, X13);
		sha1_msg_schedule_update(&X1, X3, X9, X14);
		sha1_msg_schedule_update(&X2, X4, X10, X15);
		sha1_msg_schedule_update(&X3, X5, X11, X0);
		sha1_msg_schedule_update(&X4, X6, X12, X1);
		sha1_msg_schedule_update(&X5, X7, X13, X2);
		sha1_msg_schedule_update(&X6, X8, X14, X3);
		sha1_msg_schedule_update(&X7, X9, X15, X4);
		sha1_msg_schedule_update(&X8, X10, X0, X5);
		sha1_msg_schedule_update(&X9, X11, X1, X6);
		sha1_msg_schedule_update(&X10, X12, X2, X7);
		sha1_msg_schedule_update(&X11, X13, X3, X8);
		sha1_msg_schedule_update(&X12, X14, X4, X9);
		sha1_msg_schedule_update(&X13, X15, X5, X10);
		sha1_msg_schedule_update(&X14, X0, X6, X11);
		sha1_msg_schedule_update(&X15, X1, X7, X12);

		sha1_round1(&a, &b, &c, &d, &e, X0);
		sha1_round1(&a, &b, &c, &d, &e, X1);
		sha1_round1(&a, &b, &c, &d, &e, X2);
		sha1_round1(&a, &b, &c, &d, &e, X3);
		sha1_round2(&a, &b, &c, &d, &e, X4);
		sha1_round2(&a, &b, &c, &d, &e, X5);
		sha1_round2(&a, &b, &c, &d, &e, X6);
		sha1_round2(&a, &b, &c, &d, &e, X7);
		sha1_round2(&a, &b, &c, &d, &e, X8);
		sha1_round2(&a, &b, &c, &d, &e, X9);
		sha1_round2(&a, &b, &c, &d, &e, X10);
		sha1_round2(&a, &b, &c, &d, &e, X11);
		sha1_round2(&a, &b, &c, &d, &e, X12);
		sha1_round2(&a, &b, &c, &d, &e, X13);
		sha1_round2(&a, &b, &c, &d, &e, X14);
		sha1_round2(&a, &b, &c, &d, &e, X15);

		sha1_msg_schedule_update(&X0, X2, X8, X13);
		sha1_msg_schedule_update(&X1, X3, X9, X14);
		sha1_msg_schedule_update(&X2, X4, X10, X15);
		sha1_msg_schedule_update(&X3, X5, X11, X0);
		sha1_msg_schedule_update(&X4, X6, X12, X1);
		sha1_msg_schedule_update(&X5, X7, X13, X2);
		sha1_msg_schedule_update(&X6, X8, X14, X3);
		sha1_msg_schedule_update(&X7, X9, X15, X4);
		sha1_msg_schedule_update(&X8, X10, X0, X5);
		sha1_msg_schedule_update(&X9, X11, X1, X6);
		sha1_msg_schedule_update(&X10, X12, X2, X7);
		sha1_msg_schedule_update(&X11, X13, X3, X8);
		sha1_msg_schedule_update(&X12, X14, X4, X9);
		sha1_msg_schedule_update(&X13, X15, X5, X10);
		sha1_msg_schedule_update(&X14, X0, X6, X11);
		sha1_msg_schedule_update(&X15, X1, X7, X12);

		sha1_round2(&a, &b, &c, &d, &e, X0);
		sha1_round2(&a, &b, &c, &d, &e, X1);
		sha1_round2(&a, &b, &c, &d, &e, X2);
		sha1_round2(&a, &b, &c, &d, &e, X3);
		sha1_round2(&a, &b, &c, &d, &e, X4);
		sha1_round2(&a, &b, &c, &d, &e, X5);
		sha1_round2(&a, &b, &c, &d, &e, X6);
		sha1_round2(&a, &b, &c, &d, &e, X7);
		sha1_round3(&a, &b, &c, &d, &e, X8);
		sha1_round3(&a, &b, &c, &d, &e, X9);
		sha1_round3(&a, &b, &c, &d, &e, X10);
		sha1_round3(&a, &b, &c, &d, &e, X11);
		sha1_round3(&a, &b, &c, &d, &e, X12);
		sha1_round3(&a, &b, &c, &d, &e, X13);
		sha1_round3(&a, &b, &c, &d, &e, X14);
		sha1_round3(&a, &b, &c, &d, &e, X15);

		sha1_msg_schedule_update(&X0, X2, X8, X13);
		sha1_msg_schedule_update(&X1, X3, X9, X14);
		sha1_msg_schedule_update(&X2, X4, X10, X15);
		sha1_msg_schedule_update(&X3, X5, X11, X0);
		sha1_msg_schedule_update(&X4, X6, X12, X1);
		sha1_msg_schedule_update(&X5, X7, X13, X2);
		sha1_msg_schedule_update(&X6, X8, X14, X3);
		sha1_msg_schedule_update(&X7, X9, X15, X4);
		sha1_msg_schedule_update(&X8, X10, X0, X5);
		sha1_msg_schedule_update(&X9, X11, X1, X6);
		sha1_msg_schedule_update(&X10, X12, X2, X7);
		sha1_msg_schedule_update(&X11, X13, X3, X8);
		sha1_msg_schedule_update(&X12, X14, X4, X9);
		sha1_msg_schedule_update(&X13, X15, X5, X10);
		sha1_msg_schedule_update(&X14, X0, X6, X11);
		sha1_msg_schedule_update(&X15, X1, X7, X12);

		sha1_round3(&a, &b, &c, &d, &e, X0);
		sha1_round3(&a, &b, &c, &d, &e, X1);
		sha1_round3(&a, &b, &c, &d, &e, X2);
		sha1_round3(&a, &b, &c, &d, &e, X3);
		sha1_round3(&a, &b, &c, &d, &e, X4);
		sha1_round3(&a, &b, &c, &d, &e, X5);
		sha1_round3(&a, &b, &c, &d, &e, X6);
		sha1_round3(&a, &b, &c, &d, &e, X7);
		sha1_round3(&a, &b, &c, &d, &e, X8);
		sha1_round3(&a, &b, &c, &d, &e, X9);
		sha1_round3(&a, &b, &c, &d, &e, X10);
		sha1_round3(&a, &b, &c, &d, &e, X11);
		sha1_round4(&a, &b, &c, &d, &e, X12);
		sha1_round4(&a, &b, &c, &d, &e, X13);
		sha1_round4(&a, &b, &c, &d, &e, X14);
		sha1_round4(&a, &b, &c, &d, &e, X15);

		sha1_msg_schedule_update(&X0, X2, X8, X13);
		sha1_msg_schedule_update(&X1, X3, X9, X14);
		sha1_msg_schedule_update(&X2, X4, X10, X15);
		sha1_msg_schedule_update(&X3, X5, X11, X0);
		sha1_msg_schedule_update(&X4, X6, X12, X1);
		sha1_msg_schedule_update(&X5, X7, X13, X2);
		sha1_msg_schedule_update(&X6, X8, X14, X3);
		sha1_msg_schedule_update(&X7, X9, X15, X4);
		sha1_msg_schedule_update(&X8, X10, X0, X5);
		sha1_msg_schedule_update(&X9, X11, X1, X6);
		sha1_msg_schedule_update(&X10, X12, X2, X7);
		sha1_msg_schedule_update(&X11, X13, X3, X8);
		sha1_msg_schedule_update(&X12, X14, X4, X9);
		sha1_msg_schedule_update(&X13, X15, X5, X10);
		sha1_msg_schedule_update(&X14, X0, X6, X11);
		sha1_msg_schedule_update(&X15, X1, X7, X12);

		sha1_round4(&a, &b, &c, &d, &e, X0);
		sha1_round4(&a, &b, &c, &d, &e, X1);
		sha1_round4(&a, &b, &c, &d, &e, X2);
		sha1_round4(&a, &b, &c, &d, &e, X3);
		sha1_round4(&a, &b, &c, &d, &e, X4);
		sha1_round4(&a, &b, &c, &d, &e, X5);
		sha1_round4(&a, &b, &c, &d, &e, X6);
		sha1_round4(&a, &b, &c, &d, &e, X7);
		sha1_round4(&a, &b, &c, &d, &e, X8);
		sha1_round4(&a, &b, &c, &d, &e, X9);
		sha1_round4(&a, &b, &c, &d, &e, X10);
		sha1_round4(&a, &b, &c, &d, &e, X11);
		sha1_round4(&a, &b, &c, &d, &e, X12);
		sha1_round4(&a, &b, &c, &d, &e, X13);
		sha1_round4(&a, &b, &c, &d, &e, X14);
		sha1_round4(&a, &b, &c, &d, &e, X15);

		ctx->h0 += a;
		ctx->h1 += b;
		ctx->h2 += c;
		ctx->h3 += d;
		ctx->h4 += e;
	}
}
#endif

#ifndef HAVE_SHA1_BLOCK_DATA_ORDER
void
sha1_block_data_order(SHA_CTX *ctx, const void *_in, size_t num)
{
	sha1_block_generic(ctx, _in, num);
}
#endif

int
SHA1_Init(SHA_CTX *c)
{
	memset(c, 0, sizeof(*c));

	c->h0 = 0x67452301UL;
	c->h1 = 0xefcdab89UL;
	c->h2 = 0x98badcfeUL;
	c->h3 = 0x10325476UL;
	c->h4 = 0xc3d2e1f0UL;

	return 1;
}
LCRYPTO_ALIAS(SHA1_Init);

int
SHA1_Update(SHA_CTX *c, const void *data_, size_t len)
{
	const unsigned char *data = data_;
	unsigned char *p;
	SHA_LONG l;
	size_t n;

	if (len == 0)
		return 1;

	l = (c->Nl + (((SHA_LONG)len) << 3))&0xffffffffUL;
	/* 95-05-24 eay Fixed a bug with the overflow handling, thanks to
	 * Wei Dai <weidai@eskimo.com> for pointing it out. */
	if (l < c->Nl) /* overflow */
		c->Nh++;
	c->Nh+=(SHA_LONG)(len>>29);	/* might cause compiler warning on 16-bit */
	c->Nl = l;

	n = c->num;
	if (n != 0) {
		p = (unsigned char *)c->data;

		if (len >= SHA_CBLOCK || len + n >= SHA_CBLOCK) {
			memcpy(p + n, data, SHA_CBLOCK - n);
			sha1_block_data_order(c, p, 1);
			n = SHA_CBLOCK - n;
			data += n;
			len -= n;
			c->num = 0;
			memset(p,0,SHA_CBLOCK);	/* keep it zeroed */
		} else {
			memcpy(p + n, data, len);
			c->num += (unsigned int)len;
			return 1;
		}
	}

	n = len/SHA_CBLOCK;
	if (n > 0) {
		sha1_block_data_order(c, data, n);
		n    *= SHA_CBLOCK;
		data += n;
		len -= n;
	}

	if (len != 0) {
		p = (unsigned char *)c->data;
		c->num = (unsigned int)len;
		memcpy(p, data, len);
	}
	return 1;
}
LCRYPTO_ALIAS(SHA1_Update);

void
SHA1_Transform(SHA_CTX *c, const unsigned char *data)
{
	sha1_block_data_order(c, data, 1);
}
LCRYPTO_ALIAS(SHA1_Transform);

int
SHA1_Final(unsigned char *md, SHA_CTX *c)
{
	unsigned char *p = (unsigned char *)c->data;
	size_t n = c->num;

	p[n] = 0x80; /* there is always room for one */
	n++;

	if (n > (SHA_CBLOCK - 8)) {
		memset(p + n, 0, SHA_CBLOCK - n);
		n = 0;
		sha1_block_data_order(c, p, 1);
	}

	memset(p + n, 0, SHA_CBLOCK - 8 - n);
	c->data[SHA_LBLOCK - 2] = htobe32(c->Nh);
	c->data[SHA_LBLOCK - 1] = htobe32(c->Nl);

	sha1_block_data_order(c, p, 1);
	c->num = 0;
	memset(p, 0, SHA_CBLOCK);

	crypto_store_htobe32(&md[0 * 4], c->h0);
	crypto_store_htobe32(&md[1 * 4], c->h1);
	crypto_store_htobe32(&md[2 * 4], c->h2);
	crypto_store_htobe32(&md[3 * 4], c->h3);
	crypto_store_htobe32(&md[4 * 4], c->h4);

	return 1;
}
LCRYPTO_ALIAS(SHA1_Final);

unsigned char *
SHA1(const unsigned char *d, size_t n, unsigned char *md)
{
	SHA_CTX c;

	if (!SHA1_Init(&c))
		return NULL;
	SHA1_Update(&c, d, n);
	SHA1_Final(md, &c);

	explicit_bzero(&c, sizeof(c));

	return (md);
}
LCRYPTO_ALIAS(SHA1);

#endif
