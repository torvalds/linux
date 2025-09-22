/* $OpenBSD: md4.c,v 1.18 2024/06/01 07:36:16 tb Exp $ */
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/md4.h>

#include "crypto_internal.h"

/* Ensure that MD4_LONG and uint32_t are equivalent size. */
CTASSERT(sizeof(MD4_LONG) == sizeof(uint32_t));

static inline uint32_t
md4_f(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) | (~x & z);
}

static inline uint32_t
md4_g(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) | (x & z) | (y & z);
}

static inline uint32_t
md4_h(uint32_t x, uint32_t y, uint32_t z)
{
	return x ^ y ^ z;
}

static inline void
md4_round1(uint32_t *a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
    uint32_t s)
{
	*a = crypto_rol_u32(*a + md4_f(b, c, d) + x, s);
}

static inline void
md4_round2(uint32_t *a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
    uint32_t s)
{
	*a = crypto_rol_u32(*a + md4_g(b, c, d) + x + 0x5a827999UL, s);
}

static inline void
md4_round3(uint32_t *a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
    uint32_t s)
{
	*a = crypto_rol_u32(*a + md4_h(b, c, d) + x + 0x6ed9eba1UL, s);
}

static void
md4_block_data_order(MD4_CTX *c, const void *_in, size_t num)
{
	const uint8_t *in = _in;
	const MD4_LONG *in32;
	unsigned int A, B, C, D;
	unsigned int X0, X1, X2, X3, X4, X5, X6, X7,
	    X8, X9, X10, X11, X12, X13, X14, X15;

	while (num-- > 0) {
		A = c->A;
		B = c->B;
		C = c->C;
		D = c->D;

		if ((uintptr_t)in % 4 == 0) {
			/* Input is 32 bit aligned. */
			in32 = (const MD4_LONG *)in;
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
		in += MD4_CBLOCK;

		md4_round1(&A, B, C, D, X0, 3);
		md4_round1(&D, A, B, C, X1, 7);
		md4_round1(&C, D, A, B, X2, 11);
		md4_round1(&B, C, D, A, X3, 19);
		md4_round1(&A, B, C, D, X4, 3);
		md4_round1(&D, A, B, C, X5, 7);
		md4_round1(&C, D, A, B, X6, 11);
		md4_round1(&B, C, D, A, X7, 19);
		md4_round1(&A, B, C, D, X8, 3);
		md4_round1(&D, A, B, C, X9, 7);
		md4_round1(&C, D, A, B, X10, 11);
		md4_round1(&B, C, D, A, X11, 19);
		md4_round1(&A, B, C, D, X12, 3);
		md4_round1(&D, A, B, C, X13, 7);
		md4_round1(&C, D, A, B, X14, 11);
		md4_round1(&B, C, D, A, X15, 19);

		/* Round 1 */
		md4_round2(&A, B, C, D, X0, 3);
		md4_round2(&D, A, B, C, X4, 5);
		md4_round2(&C, D, A, B, X8, 9);
		md4_round2(&B, C, D, A, X12, 13);
		md4_round2(&A, B, C, D, X1, 3);
		md4_round2(&D, A, B, C, X5, 5);
		md4_round2(&C, D, A, B, X9, 9);
		md4_round2(&B, C, D, A, X13, 13);
		md4_round2(&A, B, C, D, X2, 3);
		md4_round2(&D, A, B, C, X6, 5);
		md4_round2(&C, D, A, B, X10, 9);
		md4_round2(&B, C, D, A, X14, 13);
		md4_round2(&A, B, C, D, X3, 3);
		md4_round2(&D, A, B, C, X7, 5);
		md4_round2(&C, D, A, B, X11, 9);
		md4_round2(&B, C, D, A, X15, 13);

		/* Round 2 */
		md4_round3(&A, B, C, D, X0, 3);
		md4_round3(&D, A, B, C, X8, 9);
		md4_round3(&C, D, A, B, X4, 11);
		md4_round3(&B, C, D, A, X12, 15);
		md4_round3(&A, B, C, D, X2, 3);
		md4_round3(&D, A, B, C, X10, 9);
		md4_round3(&C, D, A, B, X6, 11);
		md4_round3(&B, C, D, A, X14, 15);
		md4_round3(&A, B, C, D, X1, 3);
		md4_round3(&D, A, B, C, X9, 9);
		md4_round3(&C, D, A, B, X5, 11);
		md4_round3(&B, C, D, A, X13, 15);
		md4_round3(&A, B, C, D, X3, 3);
		md4_round3(&D, A, B, C, X11, 9);
		md4_round3(&C, D, A, B, X7, 11);
		md4_round3(&B, C, D, A, X15, 15);

		c->A += A;
		c->B += B;
		c->C += C;
		c->D += D;
	}
}

int
MD4_Init(MD4_CTX *c)
{
	memset(c, 0, sizeof(*c));

	c->A = 0x67452301UL;
	c->B = 0xefcdab89UL;
	c->C = 0x98badcfeUL;
	c->D = 0x10325476UL;

	return 1;
}
LCRYPTO_ALIAS(MD4_Init);

int
MD4_Update(MD4_CTX *c, const void *data_, size_t len)
{
	const unsigned char *data = data_;
	unsigned char *p;
	MD4_LONG l;
	size_t n;

	if (len == 0)
		return 1;

	l = (c->Nl + (((MD4_LONG)len) << 3))&0xffffffffUL;
	/* 95-05-24 eay Fixed a bug with the overflow handling, thanks to
	 * Wei Dai <weidai@eskimo.com> for pointing it out. */
	if (l < c->Nl) /* overflow */
		c->Nh++;
	c->Nh+=(MD4_LONG)(len>>29);	/* might cause compiler warning on 16-bit */
	c->Nl = l;

	n = c->num;
	if (n != 0) {
		p = (unsigned char *)c->data;

		if (len >= MD4_CBLOCK || len + n >= MD4_CBLOCK) {
			memcpy (p + n, data, MD4_CBLOCK - n);
			md4_block_data_order (c, p, 1);
			n = MD4_CBLOCK - n;
			data += n;
			len -= n;
			c->num = 0;
			memset(p, 0, MD4_CBLOCK);	/* keep it zeroed */
		} else {
			memcpy(p + n, data, len);
			c->num += (unsigned int)len;
			return 1;
		}
	}

	n = len / MD4_CBLOCK;
	if (n > 0) {
		md4_block_data_order(c, data, n);
		n    *= MD4_CBLOCK;
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
LCRYPTO_ALIAS(MD4_Update);

void
MD4_Transform(MD4_CTX *c, const unsigned char *data)
{
	md4_block_data_order(c, data, 1);
}
LCRYPTO_ALIAS(MD4_Transform);

int
MD4_Final(unsigned char *md, MD4_CTX *c)
{
	unsigned char *p = (unsigned char *)c->data;
	size_t n = c->num;

	p[n] = 0x80; /* there is always room for one */
	n++;

	if (n > (MD4_CBLOCK - 8)) {
		memset(p + n, 0, MD4_CBLOCK - n);
		n = 0;
		md4_block_data_order(c, p, 1);
	}

	memset(p + n, 0, MD4_CBLOCK - 8 - n);
	c->data[MD4_LBLOCK - 2] = htole32(c->Nl);
	c->data[MD4_LBLOCK - 1] = htole32(c->Nh);

	md4_block_data_order(c, p, 1);
	c->num = 0;
	memset(p, 0, MD4_CBLOCK);

	crypto_store_htole32(&md[0 * 4], c->A);
	crypto_store_htole32(&md[1 * 4], c->B);
	crypto_store_htole32(&md[2 * 4], c->C);
	crypto_store_htole32(&md[3 * 4], c->D);

	return 1;
}
LCRYPTO_ALIAS(MD4_Final);

unsigned char *
MD4(const unsigned char *d, size_t n, unsigned char *md)
{
	MD4_CTX c;

	if (!MD4_Init(&c))
		return NULL;
	MD4_Update(&c, d, n);
	MD4_Final(md, &c);
	explicit_bzero(&c, sizeof(c));
	return (md);
}
LCRYPTO_ALIAS(MD4);
