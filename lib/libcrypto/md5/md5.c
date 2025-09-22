/* $OpenBSD: md5.c,v 1.25 2025/01/24 13:35:04 jsing Exp $ */
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

#include <openssl/md5.h>

#include "crypto_internal.h"

/* Ensure that MD5_LONG and uint32_t are equivalent size. */
CTASSERT(sizeof(MD5_LONG) == sizeof(uint32_t));

#ifdef MD5_ASM
void md5_block_data_order(MD5_CTX *c, const void *p, size_t num);
#endif

#ifndef MD5_ASM
static inline uint32_t
md5_F(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) | (~x & z);
}

static inline uint32_t
md5_G(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & z) | (y & ~z);
}

static inline uint32_t
md5_H(uint32_t x, uint32_t y, uint32_t z)
{
	return x ^ y ^ z;
}

static inline uint32_t
md5_I(uint32_t x, uint32_t y, uint32_t z)
{
	return y ^ (x | ~z);
}

static inline void
md5_round1(uint32_t *a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
    uint32_t t, uint32_t s)
{
	*a = b + crypto_rol_u32(*a + md5_F(b, c, d) + x + t, s);
}

static inline void
md5_round2(uint32_t *a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
    uint32_t t, uint32_t s)
{
	*a = b + crypto_rol_u32(*a + md5_G(b, c, d) + x + t, s);
}

static inline void
md5_round3(uint32_t *a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
    uint32_t t, uint32_t s)
{
	*a = b + crypto_rol_u32(*a + md5_H(b, c, d) + x + t, s);
}

static inline void
md5_round4(uint32_t *a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
    uint32_t t, uint32_t s)
{
	*a = b + crypto_rol_u32(*a + md5_I(b, c, d) + x + t, s);
}

static void
md5_block_data_order(MD5_CTX *c, const void *_in, size_t num)
{
	const uint8_t *in = _in;
	const MD5_LONG *in32;
	MD5_LONG A, B, C, D;
	MD5_LONG X0, X1, X2, X3, X4, X5, X6, X7,
	    X8, X9, X10, X11, X12, X13, X14, X15;

	while (num-- > 0) {
		A = c->A;
		B = c->B;
		C = c->C;
		D = c->D;

		if ((uintptr_t)in % 4 == 0) {
			/* Input is 32 bit aligned. */
			in32 = (const MD5_LONG *)in;
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
		in += MD5_CBLOCK;

		md5_round1(&A, B, C, D, X0, 0xd76aa478L, 7);
		md5_round1(&D, A, B, C, X1, 0xe8c7b756L, 12);
		md5_round1(&C, D, A, B, X2, 0x242070dbL, 17);
		md5_round1(&B, C, D, A, X3, 0xc1bdceeeL, 22);
		md5_round1(&A, B, C, D, X4, 0xf57c0fafL, 7);
		md5_round1(&D, A, B, C, X5, 0x4787c62aL, 12);
		md5_round1(&C, D, A, B, X6, 0xa8304613L, 17);
		md5_round1(&B, C, D, A, X7, 0xfd469501L, 22);
		md5_round1(&A, B, C, D, X8, 0x698098d8L, 7);
		md5_round1(&D, A, B, C, X9, 0x8b44f7afL, 12);
		md5_round1(&C, D, A, B, X10, 0xffff5bb1L, 17);
		md5_round1(&B, C, D, A, X11, 0x895cd7beL, 22);
		md5_round1(&A, B, C, D, X12, 0x6b901122L, 7);
		md5_round1(&D, A, B, C, X13, 0xfd987193L, 12);
		md5_round1(&C, D, A, B, X14, 0xa679438eL, 17);
		md5_round1(&B, C, D, A, X15, 0x49b40821L, 22);

		md5_round2(&A, B, C, D, X1, 0xf61e2562L, 5);
		md5_round2(&D, A, B, C, X6, 0xc040b340L, 9);
		md5_round2(&C, D, A, B, X11, 0x265e5a51L, 14);
		md5_round2(&B, C, D, A, X0, 0xe9b6c7aaL, 20);
		md5_round2(&A, B, C, D, X5, 0xd62f105dL, 5);
		md5_round2(&D, A, B, C, X10, 0x02441453L, 9);
		md5_round2(&C, D, A, B, X15, 0xd8a1e681L, 14);
		md5_round2(&B, C, D, A, X4, 0xe7d3fbc8L, 20);
		md5_round2(&A, B, C, D, X9, 0x21e1cde6L, 5);
		md5_round2(&D, A, B, C, X14, 0xc33707d6L, 9);
		md5_round2(&C, D, A, B, X3, 0xf4d50d87L, 14);
		md5_round2(&B, C, D, A, X8, 0x455a14edL, 20);
		md5_round2(&A, B, C, D, X13, 0xa9e3e905L, 5);
		md5_round2(&D, A, B, C, X2, 0xfcefa3f8L, 9);
		md5_round2(&C, D, A, B, X7, 0x676f02d9L, 14);
		md5_round2(&B, C, D, A, X12, 0x8d2a4c8aL, 20);

		md5_round3(&A, B, C, D, X5, 0xfffa3942L, 4);
		md5_round3(&D, A, B, C, X8, 0x8771f681L, 11);
		md5_round3(&C, D, A, B, X11, 0x6d9d6122L, 16);
		md5_round3(&B, C, D, A, X14, 0xfde5380cL, 23);
		md5_round3(&A, B, C, D, X1, 0xa4beea44L, 4);
		md5_round3(&D, A, B, C, X4, 0x4bdecfa9L, 11);
		md5_round3(&C, D, A, B, X7, 0xf6bb4b60L, 16);
		md5_round3(&B, C, D, A, X10, 0xbebfbc70L, 23);
		md5_round3(&A, B, C, D, X13, 0x289b7ec6L, 4);
		md5_round3(&D, A, B, C, X0, 0xeaa127faL, 11);
		md5_round3(&C, D, A, B, X3, 0xd4ef3085L, 16);
		md5_round3(&B, C, D, A, X6, 0x04881d05L, 23);
		md5_round3(&A, B, C, D, X9, 0xd9d4d039L, 4);
		md5_round3(&D, A, B, C, X12, 0xe6db99e5L, 11);
		md5_round3(&C, D, A, B, X15, 0x1fa27cf8L, 16);
		md5_round3(&B, C, D, A, X2, 0xc4ac5665L, 23);

		md5_round4(&A, B, C, D, X0, 0xf4292244L, 6);
		md5_round4(&D, A, B, C, X7, 0x432aff97L, 10);
		md5_round4(&C, D, A, B, X14, 0xab9423a7L, 15);
		md5_round4(&B, C, D, A, X5, 0xfc93a039L, 21);
		md5_round4(&A, B, C, D, X12, 0x655b59c3L, 6);
		md5_round4(&D, A, B, C, X3, 0x8f0ccc92L, 10);
		md5_round4(&C, D, A, B, X10, 0xffeff47dL, 15);
		md5_round4(&B, C, D, A, X1, 0x85845dd1L, 21);
		md5_round4(&A, B, C, D, X8, 0x6fa87e4fL, 6);
		md5_round4(&D, A, B, C, X15, 0xfe2ce6e0L, 10);
		md5_round4(&C, D, A, B, X6, 0xa3014314L, 15);
		md5_round4(&B, C, D, A, X13, 0x4e0811a1L, 21);
		md5_round4(&A, B, C, D, X4, 0xf7537e82L, 6);
		md5_round4(&D, A, B, C, X11, 0xbd3af235L, 10);
		md5_round4(&C, D, A, B, X2, 0x2ad7d2bbL, 15);
		md5_round4(&B, C, D, A, X9, 0xeb86d391L, 21);

		c->A += A;
		c->B += B;
		c->C += C;
		c->D += D;
	}
}
#endif

int
MD5_Init(MD5_CTX *c)
{
	memset(c, 0, sizeof(*c));

	c->A = 0x67452301UL;
	c->B = 0xefcdab89UL;
	c->C = 0x98badcfeUL;
	c->D = 0x10325476UL;

	return 1;
}
LCRYPTO_ALIAS(MD5_Init);

int
MD5_Update(MD5_CTX *c, const void *data_, size_t len)
{
	const unsigned char *data = data_;
	unsigned char *p;
	size_t n;

	if (len == 0)
		return 1;

	/* Update message bit counter. */
	crypto_add_u32dw_u64(&c->Nh, &c->Nl, (uint64_t)len << 3);

	n = c->num;
	if (n != 0) {
		p = (unsigned char *)c->data;

		if (len >= MD5_CBLOCK || len + n >= MD5_CBLOCK) {
			memcpy(p + n, data, MD5_CBLOCK - n);
			md5_block_data_order(c, p, 1);
			n = MD5_CBLOCK - n;
			data += n;
			len -= n;
			c->num = 0;
			memset(p, 0, MD5_CBLOCK);	/* keep it zeroed */
		} else {
			memcpy(p + n, data, len);
			c->num += (unsigned int)len;
			return 1;
		}
	}

	n = len/MD5_CBLOCK;
	if (n > 0) {
		md5_block_data_order(c, data, n);
		n *= MD5_CBLOCK;
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
LCRYPTO_ALIAS(MD5_Update);

void
MD5_Transform(MD5_CTX *c, const unsigned char *data)
{
	md5_block_data_order(c, data, 1);
}
LCRYPTO_ALIAS(MD5_Transform);

int
MD5_Final(unsigned char *md, MD5_CTX *c)
{
	unsigned char *p = (unsigned char *)c->data;
	size_t n = c->num;

	p[n] = 0x80; /* there is always room for one */
	n++;

	if (n > (MD5_CBLOCK - 8)) {
		memset(p + n, 0, MD5_CBLOCK - n);
		n = 0;
		md5_block_data_order(c, p, 1);
	}

	memset(p + n, 0, MD5_CBLOCK - 8 - n);
	c->data[MD5_LBLOCK - 2] = htole32(c->Nl);
	c->data[MD5_LBLOCK - 1] = htole32(c->Nh);

	md5_block_data_order(c, p, 1);
	c->num = 0;
	memset(p, 0, MD5_CBLOCK);

	crypto_store_htole32(&md[0 * 4], c->A);
	crypto_store_htole32(&md[1 * 4], c->B);
	crypto_store_htole32(&md[2 * 4], c->C);
	crypto_store_htole32(&md[3 * 4], c->D);

	return 1;
}
LCRYPTO_ALIAS(MD5_Final);

unsigned char *
MD5(const unsigned char *d, size_t n, unsigned char *md)
{
	MD5_CTX c;

	if (!MD5_Init(&c))
		return NULL;
	MD5_Update(&c, d, n);
	MD5_Final(md, &c);
	explicit_bzero(&c, sizeof(c));
	return (md);
}
LCRYPTO_ALIAS(MD5);
