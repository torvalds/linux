/* $OpenBSD: sha512.c,v 1.43 2025/02/14 12:01:58 jsing Exp $ */
/* ====================================================================
 * Copyright (c) 1998-2011 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 */

#include <endian.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/crypto.h>
#include <openssl/sha.h>

#include "crypto_internal.h"
#include "sha_internal.h"

#if !defined(OPENSSL_NO_SHA) && !defined(OPENSSL_NO_SHA512)

/* Ensure that SHA_LONG64 and uint64_t are equivalent. */
CTASSERT(sizeof(SHA_LONG64) == sizeof(uint64_t));

void sha512_block_data_order(SHA512_CTX *ctx, const void *in, size_t num);
void sha512_block_generic(SHA512_CTX *ctx, const void *in, size_t num);

#ifndef HAVE_SHA512_BLOCK_GENERIC
static const SHA_LONG64 K512[80] = {
	U64(0x428a2f98d728ae22), U64(0x7137449123ef65cd),
	U64(0xb5c0fbcfec4d3b2f), U64(0xe9b5dba58189dbbc),
	U64(0x3956c25bf348b538), U64(0x59f111f1b605d019),
	U64(0x923f82a4af194f9b), U64(0xab1c5ed5da6d8118),
	U64(0xd807aa98a3030242), U64(0x12835b0145706fbe),
	U64(0x243185be4ee4b28c), U64(0x550c7dc3d5ffb4e2),
	U64(0x72be5d74f27b896f), U64(0x80deb1fe3b1696b1),
	U64(0x9bdc06a725c71235), U64(0xc19bf174cf692694),
	U64(0xe49b69c19ef14ad2), U64(0xefbe4786384f25e3),
	U64(0x0fc19dc68b8cd5b5), U64(0x240ca1cc77ac9c65),
	U64(0x2de92c6f592b0275), U64(0x4a7484aa6ea6e483),
	U64(0x5cb0a9dcbd41fbd4), U64(0x76f988da831153b5),
	U64(0x983e5152ee66dfab), U64(0xa831c66d2db43210),
	U64(0xb00327c898fb213f), U64(0xbf597fc7beef0ee4),
	U64(0xc6e00bf33da88fc2), U64(0xd5a79147930aa725),
	U64(0x06ca6351e003826f), U64(0x142929670a0e6e70),
	U64(0x27b70a8546d22ffc), U64(0x2e1b21385c26c926),
	U64(0x4d2c6dfc5ac42aed), U64(0x53380d139d95b3df),
	U64(0x650a73548baf63de), U64(0x766a0abb3c77b2a8),
	U64(0x81c2c92e47edaee6), U64(0x92722c851482353b),
	U64(0xa2bfe8a14cf10364), U64(0xa81a664bbc423001),
	U64(0xc24b8b70d0f89791), U64(0xc76c51a30654be30),
	U64(0xd192e819d6ef5218), U64(0xd69906245565a910),
	U64(0xf40e35855771202a), U64(0x106aa07032bbd1b8),
	U64(0x19a4c116b8d2d0c8), U64(0x1e376c085141ab53),
	U64(0x2748774cdf8eeb99), U64(0x34b0bcb5e19b48a8),
	U64(0x391c0cb3c5c95a63), U64(0x4ed8aa4ae3418acb),
	U64(0x5b9cca4f7763e373), U64(0x682e6ff3d6b2b8a3),
	U64(0x748f82ee5defb2fc), U64(0x78a5636f43172f60),
	U64(0x84c87814a1f0ab72), U64(0x8cc702081a6439ec),
	U64(0x90befffa23631e28), U64(0xa4506cebde82bde9),
	U64(0xbef9a3f7b2c67915), U64(0xc67178f2e372532b),
	U64(0xca273eceea26619c), U64(0xd186b8c721c0c207),
	U64(0xeada7dd6cde0eb1e), U64(0xf57d4f7fee6ed178),
	U64(0x06f067aa72176fba), U64(0x0a637dc5a2c898a6),
	U64(0x113f9804bef90dae), U64(0x1b710b35131c471b),
	U64(0x28db77f523047d84), U64(0x32caab7b40c72493),
	U64(0x3c9ebe0a15c9bebc), U64(0x431d67c49c100d4c),
	U64(0x4cc5d4becb3e42b6), U64(0x597f299cfc657e2a),
	U64(0x5fcb6fab3ad6faec), U64(0x6c44198c4a475817),
};

static inline SHA_LONG64
Sigma0(SHA_LONG64 x)
{
	return crypto_ror_u64(x, 28) ^ crypto_ror_u64(x, 34) ^
	    crypto_ror_u64(x, 39);
}

static inline SHA_LONG64
Sigma1(SHA_LONG64 x)
{
	return crypto_ror_u64(x, 14) ^ crypto_ror_u64(x, 18) ^
	    crypto_ror_u64(x, 41);
}

static inline SHA_LONG64
sigma0(SHA_LONG64 x)
{
	return crypto_ror_u64(x, 1) ^ crypto_ror_u64(x, 8) ^ (x >> 7);
}

static inline SHA_LONG64
sigma1(SHA_LONG64 x)
{
	return crypto_ror_u64(x, 19) ^ crypto_ror_u64(x, 61) ^ (x >> 6);
}

static inline SHA_LONG64
Ch(SHA_LONG64 x, SHA_LONG64 y, SHA_LONG64 z)
{
	return (x & y) ^ (~x & z);
}

static inline SHA_LONG64
Maj(SHA_LONG64 x, SHA_LONG64 y, SHA_LONG64 z)
{
	return (x & y) ^ (x & z) ^ (y & z);
}

static inline void
sha512_msg_schedule_update(SHA_LONG64 *W0, SHA_LONG64 W1,
    SHA_LONG64 W9, SHA_LONG64 W14)
{
	*W0 = sigma1(W14) + W9 + sigma0(W1) + *W0;
}

static inline void
sha512_round(SHA_LONG64 *a, SHA_LONG64 *b, SHA_LONG64 *c, SHA_LONG64 *d,
    SHA_LONG64 *e, SHA_LONG64 *f, SHA_LONG64 *g, SHA_LONG64 *h,
    SHA_LONG64 Kt, SHA_LONG64 Wt)
{
	SHA_LONG64 T1, T2;

	T1 = *h + Sigma1(*e) + Ch(*e, *f, *g) + Kt + Wt;
	T2 = Sigma0(*a) + Maj(*a, *b, *c);

	*h = *g;
	*g = *f;
	*f = *e;
	*e = *d + T1;
	*d = *c;
	*c = *b;
	*b = *a;
	*a = T1 + T2;
}

void
sha512_block_generic(SHA512_CTX *ctx, const void *_in, size_t num)
{
	const uint8_t *in = _in;
	const SHA_LONG64 *in64;
	SHA_LONG64 a, b, c, d, e, f, g, h;
	SHA_LONG64 X[16];
	int i;

	while (num--) {
		a = ctx->h[0];
		b = ctx->h[1];
		c = ctx->h[2];
		d = ctx->h[3];
		e = ctx->h[4];
		f = ctx->h[5];
		g = ctx->h[6];
		h = ctx->h[7];

		if ((size_t)in % sizeof(SHA_LONG64) == 0) {
			/* Input is 64 bit aligned. */
			in64 = (const SHA_LONG64 *)in;
			X[0] = be64toh(in64[0]);
			X[1] = be64toh(in64[1]);
			X[2] = be64toh(in64[2]);
			X[3] = be64toh(in64[3]);
			X[4] = be64toh(in64[4]);
			X[5] = be64toh(in64[5]);
			X[6] = be64toh(in64[6]);
			X[7] = be64toh(in64[7]);
			X[8] = be64toh(in64[8]);
			X[9] = be64toh(in64[9]);
			X[10] = be64toh(in64[10]);
			X[11] = be64toh(in64[11]);
			X[12] = be64toh(in64[12]);
			X[13] = be64toh(in64[13]);
			X[14] = be64toh(in64[14]);
			X[15] = be64toh(in64[15]);
		} else {
			/* Input is not 64 bit aligned. */
			X[0] = crypto_load_be64toh(&in[0 * 8]);
			X[1] = crypto_load_be64toh(&in[1 * 8]);
			X[2] = crypto_load_be64toh(&in[2 * 8]);
			X[3] = crypto_load_be64toh(&in[3 * 8]);
			X[4] = crypto_load_be64toh(&in[4 * 8]);
			X[5] = crypto_load_be64toh(&in[5 * 8]);
			X[6] = crypto_load_be64toh(&in[6 * 8]);
			X[7] = crypto_load_be64toh(&in[7 * 8]);
			X[8] = crypto_load_be64toh(&in[8 * 8]);
			X[9] = crypto_load_be64toh(&in[9 * 8]);
			X[10] = crypto_load_be64toh(&in[10 * 8]);
			X[11] = crypto_load_be64toh(&in[11 * 8]);
			X[12] = crypto_load_be64toh(&in[12 * 8]);
			X[13] = crypto_load_be64toh(&in[13 * 8]);
			X[14] = crypto_load_be64toh(&in[14 * 8]);
			X[15] = crypto_load_be64toh(&in[15 * 8]);
		}
		in += SHA512_CBLOCK;

		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[0], X[0]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[1], X[1]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[2], X[2]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[3], X[3]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[4], X[4]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[5], X[5]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[6], X[6]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[7], X[7]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[8], X[8]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[9], X[9]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[10], X[10]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[11], X[11]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[12], X[12]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[13], X[13]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[14], X[14]);
		sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[15], X[15]);

		for (i = 16; i < 80; i += 16) {
			sha512_msg_schedule_update(&X[0], X[1], X[9], X[14]);
			sha512_msg_schedule_update(&X[1], X[2], X[10], X[15]);
			sha512_msg_schedule_update(&X[2], X[3], X[11], X[0]);
			sha512_msg_schedule_update(&X[3], X[4], X[12], X[1]);
			sha512_msg_schedule_update(&X[4], X[5], X[13], X[2]);
			sha512_msg_schedule_update(&X[5], X[6], X[14], X[3]);
			sha512_msg_schedule_update(&X[6], X[7], X[15], X[4]);
			sha512_msg_schedule_update(&X[7], X[8], X[0], X[5]);
			sha512_msg_schedule_update(&X[8], X[9], X[1], X[6]);
			sha512_msg_schedule_update(&X[9], X[10], X[2], X[7]);
			sha512_msg_schedule_update(&X[10], X[11], X[3], X[8]);
			sha512_msg_schedule_update(&X[11], X[12], X[4], X[9]);
			sha512_msg_schedule_update(&X[12], X[13], X[5], X[10]);
			sha512_msg_schedule_update(&X[13], X[14], X[6], X[11]);
			sha512_msg_schedule_update(&X[14], X[15], X[7], X[12]);
			sha512_msg_schedule_update(&X[15], X[0], X[8], X[13]);

			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 0], X[0]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 1], X[1]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 2], X[2]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 3], X[3]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 4], X[4]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 5], X[5]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 6], X[6]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 7], X[7]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 8], X[8]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 9], X[9]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 10], X[10]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 11], X[11]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 12], X[12]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 13], X[13]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 14], X[14]);
			sha512_round(&a, &b, &c, &d, &e, &f, &g, &h, K512[i + 15], X[15]);
		}

		ctx->h[0] += a;
		ctx->h[1] += b;
		ctx->h[2] += c;
		ctx->h[3] += d;
		ctx->h[4] += e;
		ctx->h[5] += f;
		ctx->h[6] += g;
		ctx->h[7] += h;
	}
}
#endif

#ifndef HAVE_SHA512_BLOCK_DATA_ORDER
void
sha512_block_data_order(SHA512_CTX *ctx, const void *_in, size_t num)
{
	sha512_block_generic(ctx, _in, num);
}
#endif

int
SHA384_Init(SHA512_CTX *c)
{
	memset(c, 0, sizeof(*c));

	c->h[0] = U64(0xcbbb9d5dc1059ed8);
	c->h[1] = U64(0x629a292a367cd507);
	c->h[2] = U64(0x9159015a3070dd17);
	c->h[3] = U64(0x152fecd8f70e5939);
	c->h[4] = U64(0x67332667ffc00b31);
	c->h[5] = U64(0x8eb44a8768581511);
	c->h[6] = U64(0xdb0c2e0d64f98fa7);
	c->h[7] = U64(0x47b5481dbefa4fa4);

	c->md_len = SHA384_DIGEST_LENGTH;

	return 1;
}
LCRYPTO_ALIAS(SHA384_Init);

int
SHA384_Update(SHA512_CTX *c, const void *data, size_t len)
{
	return SHA512_Update(c, data, len);
}
LCRYPTO_ALIAS(SHA384_Update);

int
SHA384_Final(unsigned char *md, SHA512_CTX *c)
{
	return SHA512_Final(md, c);
}
LCRYPTO_ALIAS(SHA384_Final);

unsigned char *
SHA384(const unsigned char *d, size_t n, unsigned char *md)
{
	SHA512_CTX c;

	SHA384_Init(&c);
	SHA512_Update(&c, d, n);
	SHA512_Final(md, &c);

	explicit_bzero(&c, sizeof(c));

	return (md);
}
LCRYPTO_ALIAS(SHA384);

int
SHA512_Init(SHA512_CTX *c)
{
	memset(c, 0, sizeof(*c));

	c->h[0] = U64(0x6a09e667f3bcc908);
	c->h[1] = U64(0xbb67ae8584caa73b);
	c->h[2] = U64(0x3c6ef372fe94f82b);
	c->h[3] = U64(0xa54ff53a5f1d36f1);
	c->h[4] = U64(0x510e527fade682d1);
	c->h[5] = U64(0x9b05688c2b3e6c1f);
	c->h[6] = U64(0x1f83d9abfb41bd6b);
	c->h[7] = U64(0x5be0cd19137e2179);

	c->md_len = SHA512_DIGEST_LENGTH;

	return 1;
}
LCRYPTO_ALIAS(SHA512_Init);

void
SHA512_Transform(SHA512_CTX *c, const unsigned char *data)
{
	sha512_block_data_order(c, data, 1);
}
LCRYPTO_ALIAS(SHA512_Transform);

int
SHA512_Update(SHA512_CTX *c, const void *_data, size_t len)
{
	const unsigned char *data = _data;
	unsigned char *p = c->u.p;
	SHA_LONG64 l;

	if (len == 0)
		return 1;

	l = (c->Nl + (((SHA_LONG64)len) << 3))&U64(0xffffffffffffffff);
	if (l < c->Nl)
		c->Nh++;
	if (sizeof(len) >= 8)
		c->Nh += (((SHA_LONG64)len) >> 61);
	c->Nl = l;

	if (c->num != 0) {
		size_t n = sizeof(c->u) - c->num;

		if (len < n) {
			memcpy(p + c->num, data, len);
			c->num += (unsigned int)len;
			return 1;
		} else{
			memcpy(p + c->num, data, n);
			c->num = 0;
			len -= n;
			data += n;
			sha512_block_data_order(c, p, 1);
		}
	}

	if (len >= sizeof(c->u)) {
		sha512_block_data_order(c, data, len/sizeof(c->u));
		data += len;
		len %= sizeof(c->u);
		data -= len;
	}

	if (len != 0) {
		memcpy(p, data, len);
		c->num = (int)len;
	}

	return 1;
}
LCRYPTO_ALIAS(SHA512_Update);

int
SHA512_Final(unsigned char *md, SHA512_CTX *c)
{
	unsigned char *p = (unsigned char *)c->u.p;
	size_t n = c->num;

	p[n]=0x80;	/* There always is a room for one */
	n++;
	if (n > (sizeof(c->u) - 16)) {
		memset(p + n, 0, sizeof(c->u) - n);
		n = 0;
		sha512_block_data_order(c, p, 1);
	}

	memset(p + n, 0, sizeof(c->u) - 16 - n);
	c->u.d[SHA_LBLOCK - 2] = htobe64(c->Nh);
	c->u.d[SHA_LBLOCK - 1] = htobe64(c->Nl);

	sha512_block_data_order(c, p, 1);

	if (md == NULL)
		return 0;

	/* Let compiler decide if it's appropriate to unroll... */
	switch (c->md_len) {
	case SHA512_224_DIGEST_LENGTH:
		for (n = 0; n < SHA512_224_DIGEST_LENGTH/8; n++) {
			crypto_store_htobe64(md, c->h[n]);
			md += 8;
		}
		crypto_store_htobe32(md, c->h[n] >> 32);
		break;
	case SHA512_256_DIGEST_LENGTH:
		for (n = 0; n < SHA512_256_DIGEST_LENGTH/8; n++) {
			crypto_store_htobe64(md, c->h[n]);
			md += 8;
		}
		break;
	case SHA384_DIGEST_LENGTH:
		for (n = 0; n < SHA384_DIGEST_LENGTH/8; n++) {
			crypto_store_htobe64(md, c->h[n]);
			md += 8;
		}
		break;
	case SHA512_DIGEST_LENGTH:
		for (n = 0; n < SHA512_DIGEST_LENGTH/8; n++) {
			crypto_store_htobe64(md, c->h[n]);
			md += 8;
		}
		break;
	default:
		return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(SHA512_Final);

unsigned char *
SHA512(const unsigned char *d, size_t n, unsigned char *md)
{
	SHA512_CTX c;

	SHA512_Init(&c);
	SHA512_Update(&c, d, n);
	SHA512_Final(md, &c);

	explicit_bzero(&c, sizeof(c));

	return (md);
}
LCRYPTO_ALIAS(SHA512);

int
SHA512_224_Init(SHA512_CTX *c)
{
	memset(c, 0, sizeof(*c));

	/* FIPS 180-4 section 5.3.6.1. */
	c->h[0] = U64(0x8c3d37c819544da2);
	c->h[1] = U64(0x73e1996689dcd4d6);
	c->h[2] = U64(0x1dfab7ae32ff9c82);
	c->h[3] = U64(0x679dd514582f9fcf);
	c->h[4] = U64(0x0f6d2b697bd44da8);
	c->h[5] = U64(0x77e36f7304c48942);
	c->h[6] = U64(0x3f9d85a86a1d36c8);
	c->h[7] = U64(0x1112e6ad91d692a1);

	c->md_len = SHA512_224_DIGEST_LENGTH;

	return 1;
}

int
SHA512_224_Update(SHA512_CTX *c, const void *data, size_t len)
{
	return SHA512_Update(c, data, len);
}

int
SHA512_224_Final(unsigned char *md, SHA512_CTX *c)
{
	return SHA512_Final(md, c);
}

int
SHA512_256_Init(SHA512_CTX *c)
{
	memset(c, 0, sizeof(*c));

	/* FIPS 180-4 section 5.3.6.2. */
	c->h[0] = U64(0x22312194fc2bf72c);
	c->h[1] = U64(0x9f555fa3c84c64c2);
	c->h[2] = U64(0x2393b86b6f53b151);
	c->h[3] = U64(0x963877195940eabd);
	c->h[4] = U64(0x96283ee2a88effe3);
	c->h[5] = U64(0xbe5e1e2553863992);
	c->h[6] = U64(0x2b0199fc2c85b8aa);
	c->h[7] = U64(0x0eb72ddc81c52ca2);

	c->md_len = SHA512_256_DIGEST_LENGTH;

	return 1;
}

int
SHA512_256_Update(SHA512_CTX *c, const void *data, size_t len)
{
	return SHA512_Update(c, data, len);
}

int
SHA512_256_Final(unsigned char *md, SHA512_CTX *c)
{
	return SHA512_Final(md, c);
}

#endif /* !OPENSSL_NO_SHA512 */
