/* $OpenBSD: ripemd.c,v 1.19 2024/06/01 07:36:16 tb Exp $ */
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/crypto.h>
#include <openssl/ripemd.h>

#include "crypto_internal.h"

/* Ensure that SHA_LONG and uint32_t are equivalent sizes. */
CTASSERT(sizeof(RIPEMD160_LONG) == sizeof(uint32_t));

#if 0
#define F1(x,y,z)	 ((x)^(y)^(z))
#define F2(x,y,z)	(((x)&(y))|((~x)&z))
#define F3(x,y,z)	(((x)|(~y))^(z))
#define F4(x,y,z)	(((x)&(z))|((y)&(~(z))))
#define F5(x,y,z)	 ((x)^((y)|(~(z))))
#else
/*
 * Transformed F2 and F4 are courtesy of Wei Dai <weidai@eskimo.com>
 */
#define F1(x,y,z)	((x) ^ (y) ^ (z))
#define F2(x,y,z)	((((y) ^ (z)) & (x)) ^ (z))
#define F3(x,y,z)	(((~(y)) | (x)) ^ (z))
#define F4(x,y,z)	((((x) ^ (y)) & (z)) ^ (y))
#define F5(x,y,z)	(((~(z)) | (y)) ^ (x))
#endif

#define KL0 0x00000000L
#define KL1 0x5A827999L
#define KL2 0x6ED9EBA1L
#define KL3 0x8F1BBCDCL
#define KL4 0xA953FD4EL

#define KR0 0x50A28BE6L
#define KR1 0x5C4DD124L
#define KR2 0x6D703EF3L
#define KR3 0x7A6D76E9L
#define KR4 0x00000000L

#define RIP1(a,b,c,d,e,w,s) { \
	a+=F1(b,c,d)+w; \
        a=crypto_rol_u32(a,s)+e; \
        c=crypto_rol_u32(c,10); }

#define RIP2(a,b,c,d,e,w,s,K) { \
	a+=F2(b,c,d)+w+K; \
        a=crypto_rol_u32(a,s)+e; \
        c=crypto_rol_u32(c,10); }

#define RIP3(a,b,c,d,e,w,s,K) { \
	a+=F3(b,c,d)+w+K; \
        a=crypto_rol_u32(a,s)+e; \
        c=crypto_rol_u32(c,10); }

#define RIP4(a,b,c,d,e,w,s,K) { \
	a+=F4(b,c,d)+w+K; \
        a=crypto_rol_u32(a,s)+e; \
        c=crypto_rol_u32(c,10); }

#define RIP5(a,b,c,d,e,w,s,K) { \
	a+=F5(b,c,d)+w+K; \
        a=crypto_rol_u32(a,s)+e; \
        c=crypto_rol_u32(c,10); }

static void
ripemd160_block_data_order(RIPEMD160_CTX *ctx, const void *_in, size_t num)
{
	const uint8_t *in = _in;
	const RIPEMD160_LONG *in32;
	unsigned int A, B, C, D, E;
	unsigned int a, b, c, d, e;
	unsigned int X0, X1, X2, X3, X4, X5, X6, X7,
	    X8, X9, X10, X11, X12, X13, X14, X15;

	for (; num--; ) {
		A = ctx->A;
		B = ctx->B;
		C = ctx->C;
		D = ctx->D;
		E = ctx->E;

		if ((uintptr_t)in % 4 == 0) {
			/* Input is 32 bit aligned. */
			in32 = (const RIPEMD160_LONG *)in;
			X0 = le32toh(in32[0]);
			X1 = le32toh(in32[1]);
			X2 = le32toh(in32[2]);
			X3 = le32toh(in32[3]);
			X4 = le32toh(in32[4]);
			X5 = le32toh(in32[5]);
			X6 = le32toh(in32[6]);
			X7 = le32toh(in32[7]);
			X8 = le32toh(in32[8]);
			X9 = le32toh(in32[9]);
			X10 = le32toh(in32[10]);
			X11 = le32toh(in32[11]);
			X12 = le32toh(in32[12]);
			X13 = le32toh(in32[13]);
			X14 = le32toh(in32[14]);
			X15 = le32toh(in32[15]);
		} else {
			/* Input is not 32 bit aligned. */
			X0 = crypto_load_le32toh(&in[0 * 4]);
			X1 = crypto_load_le32toh(&in[1 * 4]);
			X2 = crypto_load_le32toh(&in[2 * 4]);
			X3 = crypto_load_le32toh(&in[3 * 4]);
			X4 = crypto_load_le32toh(&in[4 * 4]);
			X5 = crypto_load_le32toh(&in[5 * 4]);
			X6 = crypto_load_le32toh(&in[6 * 4]);
			X7 = crypto_load_le32toh(&in[7 * 4]);
			X8 = crypto_load_le32toh(&in[8 * 4]);
			X9 = crypto_load_le32toh(&in[9 * 4]);
			X10 = crypto_load_le32toh(&in[10 * 4]);
			X11 = crypto_load_le32toh(&in[11 * 4]);
			X12 = crypto_load_le32toh(&in[12 * 4]);
			X13 = crypto_load_le32toh(&in[13 * 4]);
			X14 = crypto_load_le32toh(&in[14 * 4]);
			X15 = crypto_load_le32toh(&in[15 * 4]);
		}
		in += RIPEMD160_CBLOCK;

		RIP1(A, B, C, D, E, X0, 11);
		RIP1(E, A, B, C, D, X1, 14);
		RIP1(D, E, A, B, C, X2, 15);
		RIP1(C, D, E, A, B, X3, 12);
		RIP1(B, C, D, E, A, X4, 5);
		RIP1(A, B, C, D, E, X5, 8);
		RIP1(E, A, B, C, D, X6, 7);
		RIP1(D, E, A, B, C, X7, 9);
		RIP1(C, D, E, A, B, X8, 11);
		RIP1(B, C, D, E, A, X9, 13);
		RIP1(A, B, C, D, E, X10, 14);
		RIP1(E, A, B, C, D, X11, 15);
		RIP1(D, E, A, B, C, X12, 6);
		RIP1(C, D, E, A, B, X13, 7);
		RIP1(B, C, D, E, A, X14, 9);
		RIP1(A, B, C, D, E, X15, 8);

		RIP2(E, A, B, C, D, X7, 7, KL1);
		RIP2(D, E, A, B, C, X4, 6, KL1);
		RIP2(C, D, E, A, B, X13, 8, KL1);
		RIP2(B, C, D, E, A, X1, 13, KL1);
		RIP2(A, B, C, D, E, X10, 11, KL1);
		RIP2(E, A, B, C, D, X6, 9, KL1);
		RIP2(D, E, A, B, C, X15, 7, KL1);
		RIP2(C, D, E, A, B, X3, 15, KL1);
		RIP2(B, C, D, E, A, X12, 7, KL1);
		RIP2(A, B, C, D, E, X0, 12, KL1);
		RIP2(E, A, B, C, D, X9, 15, KL1);
		RIP2(D, E, A, B, C, X5, 9, KL1);
		RIP2(C, D, E, A, B, X2, 11, KL1);
		RIP2(B, C, D, E, A, X14, 7, KL1);
		RIP2(A, B, C, D, E, X11, 13, KL1);
		RIP2(E, A, B, C, D, X8, 12, KL1);

		RIP3(D, E, A, B, C, X3, 11, KL2);
		RIP3(C, D, E, A, B, X10, 13, KL2);
		RIP3(B, C, D, E, A, X14, 6, KL2);
		RIP3(A, B, C, D, E, X4, 7, KL2);
		RIP3(E, A, B, C, D, X9, 14, KL2);
		RIP3(D, E, A, B, C, X15, 9, KL2);
		RIP3(C, D, E, A, B, X8, 13, KL2);
		RIP3(B, C, D, E, A, X1, 15, KL2);
		RIP3(A, B, C, D, E, X2, 14, KL2);
		RIP3(E, A, B, C, D, X7, 8, KL2);
		RIP3(D, E, A, B, C, X0, 13, KL2);
		RIP3(C, D, E, A, B, X6, 6, KL2);
		RIP3(B, C, D, E, A, X13, 5, KL2);
		RIP3(A, B, C, D, E, X11, 12, KL2);
		RIP3(E, A, B, C, D, X5, 7, KL2);
		RIP3(D, E, A, B, C, X12, 5, KL2);

		RIP4(C, D, E, A, B, X1, 11, KL3);
		RIP4(B, C, D, E, A, X9, 12, KL3);
		RIP4(A, B, C, D, E, X11, 14, KL3);
		RIP4(E, A, B, C, D, X10, 15, KL3);
		RIP4(D, E, A, B, C, X0, 14, KL3);
		RIP4(C, D, E, A, B, X8, 15, KL3);
		RIP4(B, C, D, E, A, X12, 9, KL3);
		RIP4(A, B, C, D, E, X4, 8, KL3);
		RIP4(E, A, B, C, D, X13, 9, KL3);
		RIP4(D, E, A, B, C, X3, 14, KL3);
		RIP4(C, D, E, A, B, X7, 5, KL3);
		RIP4(B, C, D, E, A, X15, 6, KL3);
		RIP4(A, B, C, D, E, X14, 8, KL3);
		RIP4(E, A, B, C, D, X5, 6, KL3);
		RIP4(D, E, A, B, C, X6, 5, KL3);
		RIP4(C, D, E, A, B, X2, 12, KL3);

		RIP5(B, C, D, E, A, X4, 9, KL4);
		RIP5(A, B, C, D, E, X0, 15, KL4);
		RIP5(E, A, B, C, D, X5, 5, KL4);
		RIP5(D, E, A, B, C, X9, 11, KL4);
		RIP5(C, D, E, A, B, X7, 6, KL4);
		RIP5(B, C, D, E, A, X12, 8, KL4);
		RIP5(A, B, C, D, E, X2, 13, KL4);
		RIP5(E, A, B, C, D, X10, 12, KL4);
		RIP5(D, E, A, B, C, X14, 5, KL4);
		RIP5(C, D, E, A, B, X1, 12, KL4);
		RIP5(B, C, D, E, A, X3, 13, KL4);
		RIP5(A, B, C, D, E, X8, 14, KL4);
		RIP5(E, A, B, C, D, X11, 11, KL4);
		RIP5(D, E, A, B, C, X6, 8, KL4);
		RIP5(C, D, E, A, B, X15, 5, KL4);
		RIP5(B, C, D, E, A, X13, 6, KL4);

		a = A;
		b = B;
		c = C;
		d = D;
		e = E;
		/* Do other half */
		A = ctx->A;
		B = ctx->B;
		C = ctx->C;
		D = ctx->D;
		E = ctx->E;

		RIP5(A, B, C, D, E, X5, 8, KR0);
		RIP5(E, A, B, C, D, X14, 9, KR0);
		RIP5(D, E, A, B, C, X7, 9, KR0);
		RIP5(C, D, E, A, B, X0, 11, KR0);
		RIP5(B, C, D, E, A, X9, 13, KR0);
		RIP5(A, B, C, D, E, X2, 15, KR0);
		RIP5(E, A, B, C, D, X11, 15, KR0);
		RIP5(D, E, A, B, C, X4, 5, KR0);
		RIP5(C, D, E, A, B, X13, 7, KR0);
		RIP5(B, C, D, E, A, X6, 7, KR0);
		RIP5(A, B, C, D, E, X15, 8, KR0);
		RIP5(E, A, B, C, D, X8, 11, KR0);
		RIP5(D, E, A, B, C, X1, 14, KR0);
		RIP5(C, D, E, A, B, X10, 14, KR0);
		RIP5(B, C, D, E, A, X3, 12, KR0);
		RIP5(A, B, C, D, E, X12, 6, KR0);

		RIP4(E, A, B, C, D, X6, 9, KR1);
		RIP4(D, E, A, B, C, X11, 13, KR1);
		RIP4(C, D, E, A, B, X3, 15, KR1);
		RIP4(B, C, D, E, A, X7, 7, KR1);
		RIP4(A, B, C, D, E, X0, 12, KR1);
		RIP4(E, A, B, C, D, X13, 8, KR1);
		RIP4(D, E, A, B, C, X5, 9, KR1);
		RIP4(C, D, E, A, B, X10, 11, KR1);
		RIP4(B, C, D, E, A, X14, 7, KR1);
		RIP4(A, B, C, D, E, X15, 7, KR1);
		RIP4(E, A, B, C, D, X8, 12, KR1);
		RIP4(D, E, A, B, C, X12, 7, KR1);
		RIP4(C, D, E, A, B, X4, 6, KR1);
		RIP4(B, C, D, E, A, X9, 15, KR1);
		RIP4(A, B, C, D, E, X1, 13, KR1);
		RIP4(E, A, B, C, D, X2, 11, KR1);

		RIP3(D, E, A, B, C, X15, 9, KR2);
		RIP3(C, D, E, A, B, X5, 7, KR2);
		RIP3(B, C, D, E, A, X1, 15, KR2);
		RIP3(A, B, C, D, E, X3, 11, KR2);
		RIP3(E, A, B, C, D, X7, 8, KR2);
		RIP3(D, E, A, B, C, X14, 6, KR2);
		RIP3(C, D, E, A, B, X6, 6, KR2);
		RIP3(B, C, D, E, A, X9, 14, KR2);
		RIP3(A, B, C, D, E, X11, 12, KR2);
		RIP3(E, A, B, C, D, X8, 13, KR2);
		RIP3(D, E, A, B, C, X12, 5, KR2);
		RIP3(C, D, E, A, B, X2, 14, KR2);
		RIP3(B, C, D, E, A, X10, 13, KR2);
		RIP3(A, B, C, D, E, X0, 13, KR2);
		RIP3(E, A, B, C, D, X4, 7, KR2);
		RIP3(D, E, A, B, C, X13, 5, KR2);

		RIP2(C, D, E, A, B, X8, 15, KR3);
		RIP2(B, C, D, E, A, X6, 5, KR3);
		RIP2(A, B, C, D, E, X4, 8, KR3);
		RIP2(E, A, B, C, D, X1, 11, KR3);
		RIP2(D, E, A, B, C, X3, 14, KR3);
		RIP2(C, D, E, A, B, X11, 14, KR3);
		RIP2(B, C, D, E, A, X15, 6, KR3);
		RIP2(A, B, C, D, E, X0, 14, KR3);
		RIP2(E, A, B, C, D, X5, 6, KR3);
		RIP2(D, E, A, B, C, X12, 9, KR3);
		RIP2(C, D, E, A, B, X2, 12, KR3);
		RIP2(B, C, D, E, A, X13, 9, KR3);
		RIP2(A, B, C, D, E, X9, 12, KR3);
		RIP2(E, A, B, C, D, X7, 5, KR3);
		RIP2(D, E, A, B, C, X10, 15, KR3);
		RIP2(C, D, E, A, B, X14, 8, KR3);

		RIP1(B, C, D, E, A, X12, 8);
		RIP1(A, B, C, D, E, X15, 5);
		RIP1(E, A, B, C, D, X10, 12);
		RIP1(D, E, A, B, C, X4, 9);
		RIP1(C, D, E, A, B, X1, 12);
		RIP1(B, C, D, E, A, X5, 5);
		RIP1(A, B, C, D, E, X8, 14);
		RIP1(E, A, B, C, D, X7, 6);
		RIP1(D, E, A, B, C, X6, 8);
		RIP1(C, D, E, A, B, X2, 13);
		RIP1(B, C, D, E, A, X13, 6);
		RIP1(A, B, C, D, E, X14, 5);
		RIP1(E, A, B, C, D, X0, 15);
		RIP1(D, E, A, B, C, X3, 13);
		RIP1(C, D, E, A, B, X9, 11);
		RIP1(B, C, D, E, A, X11, 11);

		D = ctx->B + c + D;
		ctx->B = ctx->C + d + E;
		ctx->C = ctx->D + e + A;
		ctx->D = ctx->E + a + B;
		ctx->E = ctx->A + b + C;
		ctx->A = D;
	}
}

int
RIPEMD160_Init(RIPEMD160_CTX *c)
{
	memset(c, 0, sizeof(*c));

	c->A = 0x67452301UL;
	c->B = 0xEFCDAB89UL;
	c->C = 0x98BADCFEUL;
	c->D = 0x10325476UL;
	c->E = 0xC3D2E1F0UL;

	return 1;
}
LCRYPTO_ALIAS(RIPEMD160_Init);

int
RIPEMD160_Update(RIPEMD160_CTX *c, const void *data_, size_t len)
{
	const unsigned char *data = data_;
	unsigned char *p;
	RIPEMD160_LONG l;
	size_t n;

	if (len == 0)
		return 1;

	l = (c->Nl + (((RIPEMD160_LONG)len) << 3))&0xffffffffUL;
	/* 95-05-24 eay Fixed a bug with the overflow handling, thanks to
	 * Wei Dai <weidai@eskimo.com> for pointing it out. */
	if (l < c->Nl) /* overflow */
		c->Nh++;
	c->Nh+=(RIPEMD160_LONG)(len>>29);	/* might cause compiler warning on 16-bit */
	c->Nl = l;

	n = c->num;
	if (n != 0) {
		p = (unsigned char *)c->data;

		if (len >= RIPEMD160_CBLOCK || len + n >= RIPEMD160_CBLOCK) {
			memcpy(p + n, data, RIPEMD160_CBLOCK - n);
			ripemd160_block_data_order(c, p, 1);
			n = RIPEMD160_CBLOCK - n;
			data += n;
			len -= n;
			c->num = 0;
			memset(p, 0, RIPEMD160_CBLOCK);	/* keep it zeroed */
		} else {
			memcpy(p + n, data, len);
			c->num += (unsigned int)len;
			return 1;
		}
	}

	n = len/RIPEMD160_CBLOCK;
	if (n > 0) {
		ripemd160_block_data_order(c, data, n);
		n    *= RIPEMD160_CBLOCK;
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
LCRYPTO_ALIAS(RIPEMD160_Update);

void
RIPEMD160_Transform(RIPEMD160_CTX *c, const unsigned char *data)
{
	ripemd160_block_data_order(c, data, 1);
}
LCRYPTO_ALIAS(RIPEMD160_Transform);

int
RIPEMD160_Final(unsigned char *md, RIPEMD160_CTX *c)
{
	unsigned char *p = (unsigned char *)c->data;
	size_t n = c->num;

	p[n] = 0x80; /* there is always room for one */
	n++;

	if (n > (RIPEMD160_CBLOCK - 8)) {
		memset(p + n, 0, RIPEMD160_CBLOCK - n);
		n = 0;
		ripemd160_block_data_order(c, p, 1);
	}

	memset(p + n, 0, RIPEMD160_CBLOCK - 8 - n);
	c->data[RIPEMD160_LBLOCK - 2] = htole32(c->Nl);
	c->data[RIPEMD160_LBLOCK - 1] = htole32(c->Nh);

	ripemd160_block_data_order(c, p, 1);
	c->num = 0;
	memset(p, 0, RIPEMD160_CBLOCK);

	crypto_store_htole32(&md[0 * 4], c->A);
	crypto_store_htole32(&md[1 * 4], c->B);
	crypto_store_htole32(&md[2 * 4], c->C);
	crypto_store_htole32(&md[3 * 4], c->D);
	crypto_store_htole32(&md[4 * 4], c->E);

	return 1;
}
LCRYPTO_ALIAS(RIPEMD160_Final);

unsigned char *
RIPEMD160(const unsigned char *d, size_t n, unsigned char *md)
{
	RIPEMD160_CTX c;

	if (!RIPEMD160_Init(&c))
		return NULL;
	RIPEMD160_Update(&c, d, n);
	RIPEMD160_Final(md, &c);
	explicit_bzero(&c, sizeof(c));
	return (md);
}
LCRYPTO_ALIAS(RIPEMD160);
