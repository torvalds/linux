/* $OpenBSD: ccm128.c,v 1.12 2025/05/18 09:21:29 bcook Exp $ */
/* ====================================================================
 * Copyright (c) 2011 The OpenSSL Project.  All rights reserved.
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
 */

#include <string.h>

#include <openssl/crypto.h>

#include "modes_local.h"

/* First you setup M and L parameters and pass the key schedule.
 * This is called once per session setup... */
void
CRYPTO_ccm128_init(CCM128_CONTEXT *ctx,
    unsigned int M, unsigned int L, void *key, block128_f block)
{
	memset(ctx->nonce.c, 0, sizeof(ctx->nonce.c));
	ctx->nonce.c[0] = ((uint8_t)(L - 1) & 7) | (uint8_t)(((M - 2)/2) & 7) << 3;
	ctx->blocks = 0;
	ctx->block = block;
	ctx->key = key;
}
LCRYPTO_ALIAS(CRYPTO_ccm128_init);

/* !!! Following interfaces are to be called *once* per packet !!! */

/* Then you setup per-message nonce and pass the length of the message */
int
CRYPTO_ccm128_setiv(CCM128_CONTEXT *ctx,
    const unsigned char *nonce, size_t nlen, size_t mlen)
{
	unsigned int L = ctx->nonce.c[0] & 7;	/* the L parameter */

	if (nlen < (14 - L))
		return -1;		/* nonce is too short */

	if (sizeof(mlen) == 8 && L >= 3) {
		ctx->nonce.c[8] = (uint8_t)(mlen >> (56 % (sizeof(mlen)*8)));
		ctx->nonce.c[9] = (uint8_t)(mlen >> (48 % (sizeof(mlen)*8)));
		ctx->nonce.c[10] = (uint8_t)(mlen >> (40 % (sizeof(mlen)*8)));
		ctx->nonce.c[11] = (uint8_t)(mlen >> (32 % (sizeof(mlen)*8)));
	} else
		ctx->nonce.u[1] = 0;

	ctx->nonce.c[12] = (uint8_t)(mlen >> 24);
	ctx->nonce.c[13] = (uint8_t)(mlen >> 16);
	ctx->nonce.c[14] = (uint8_t)(mlen >> 8);
	ctx->nonce.c[15] = (uint8_t)mlen;

	ctx->nonce.c[0] &= ~0x40;	/* clear Adata flag */
	memcpy(&ctx->nonce.c[1], nonce, 14 - L);

	return 0;
}
LCRYPTO_ALIAS(CRYPTO_ccm128_setiv);

/* Then you pass additional authentication data, this is optional */
void
CRYPTO_ccm128_aad(CCM128_CONTEXT *ctx,
    const unsigned char *aad, size_t alen)
{
	unsigned int i;
	block128_f block = ctx->block;

	if (alen == 0)
		return;

	ctx->nonce.c[0] |= 0x40;	/* set Adata flag */
	(*block)(ctx->nonce.c, ctx->cmac.c, ctx->key),
	    ctx->blocks++;

	if (alen < (0x10000 - 0x100)) {
		ctx->cmac.c[0] ^= (uint8_t)(alen >> 8);
		ctx->cmac.c[1] ^= (uint8_t)alen;
		i = 2;
	} else if (sizeof(alen) == 8 &&
	    alen >= (size_t)1 << (32 % (sizeof(alen)*8))) {
		ctx->cmac.c[0] ^= 0xFF;
		ctx->cmac.c[1] ^= 0xFF;
		ctx->cmac.c[2] ^= (uint8_t)(alen >> (56 % (sizeof(alen)*8)));
		ctx->cmac.c[3] ^= (uint8_t)(alen >> (48 % (sizeof(alen)*8)));
		ctx->cmac.c[4] ^= (uint8_t)(alen >> (40 % (sizeof(alen)*8)));
		ctx->cmac.c[5] ^= (uint8_t)(alen >> (32 % (sizeof(alen)*8)));
		ctx->cmac.c[6] ^= (uint8_t)(alen >> 24);
		ctx->cmac.c[7] ^= (uint8_t)(alen >> 16);
		ctx->cmac.c[8] ^= (uint8_t)(alen >> 8);
		ctx->cmac.c[9] ^= (uint8_t)alen;
		i = 10;
	} else {
		ctx->cmac.c[0] ^= 0xFF;
		ctx->cmac.c[1] ^= 0xFE;
		ctx->cmac.c[2] ^= (uint8_t)(alen >> 24);
		ctx->cmac.c[3] ^= (uint8_t)(alen >> 16);
		ctx->cmac.c[4] ^= (uint8_t)(alen >> 8);
		ctx->cmac.c[5] ^= (uint8_t)alen;
		i = 6;
	}

	do {
		for (; i < 16 && alen; ++i, ++aad, --alen)
			ctx->cmac.c[i] ^= *aad;
		(*block)(ctx->cmac.c, ctx->cmac.c, ctx->key),
		    ctx->blocks++;
		i = 0;
	} while (alen);
}
LCRYPTO_ALIAS(CRYPTO_ccm128_aad);

/* Finally you encrypt or decrypt the message */

/* counter part of nonce may not be larger than L*8 bits,
 * L is not larger than 8, therefore 64-bit counter... */
static void
ctr64_inc(unsigned char *counter)
{
	unsigned int n = 8;
	uint8_t c;

	counter += 8;
	do {
		--n;
		c = counter[n];
		++c;
		counter[n] = c;
		if (c)
			return;
	} while (n);
}

int
CRYPTO_ccm128_encrypt(CCM128_CONTEXT *ctx,
    const unsigned char *inp, unsigned char *out,
    size_t len)
{
	size_t		 n;
	unsigned int	 i, L;
	unsigned char	 flags0 = ctx->nonce.c[0];
	block128_f	 block = ctx->block;
	void		*key = ctx->key;
	union {
		uint64_t u[2];
		uint8_t c[16];
	} scratch;

	if (!(flags0 & 0x40))
		(*block)(ctx->nonce.c, ctx->cmac.c, key),
		    ctx->blocks++;

	ctx->nonce.c[0] = L = flags0 & 7;
	for (n = 0, i = 15 - L; i < 15; ++i) {
		n |= ctx->nonce.c[i];
		ctx->nonce.c[i] = 0;
		n <<= 8;
	}
	n |= ctx->nonce.c[15];	/* reconstructed length */
	ctx->nonce.c[15] = 1;

	if (n != len)
		return -1;	/* length mismatch */

	ctx->blocks += ((len + 15) >> 3)|1;
	if (ctx->blocks > (U64(1) << 61))
		return -2; /* too much data */

	while (len >= 16) {
#ifdef __STRICT_ALIGNMENT
		union {
			uint64_t u[2];
			uint8_t c[16];
		} temp;

		memcpy(temp.c, inp, 16);
		ctx->cmac.u[0] ^= temp.u[0];
		ctx->cmac.u[1] ^= temp.u[1];
#else
		ctx->cmac.u[0] ^= ((uint64_t *)inp)[0];
		ctx->cmac.u[1] ^= ((uint64_t *)inp)[1];
#endif
		(*block)(ctx->cmac.c, ctx->cmac.c, key);
		(*block)(ctx->nonce.c, scratch.c, key);
		ctr64_inc(ctx->nonce.c);
#ifdef __STRICT_ALIGNMENT
		temp.u[0] ^= scratch.u[0];
		temp.u[1] ^= scratch.u[1];
		memcpy(out, temp.c, 16);
#else
		((uint64_t *)out)[0] = scratch.u[0] ^ ((uint64_t *)inp)[0];
		((uint64_t *)out)[1] = scratch.u[1] ^ ((uint64_t *)inp)[1];
#endif
		inp += 16;
		out += 16;
		len -= 16;
	}

	if (len) {
		for (i = 0; i < len; ++i)
			ctx->cmac.c[i] ^= inp[i];
		(*block)(ctx->cmac.c, ctx->cmac.c, key);
		(*block)(ctx->nonce.c, scratch.c, key);
		for (i = 0; i < len; ++i)
			out[i] = scratch.c[i] ^ inp[i];
	}

	for (i = 15 - L; i < 16; ++i)
		ctx->nonce.c[i] = 0;

	(*block)(ctx->nonce.c, scratch.c, key);
	ctx->cmac.u[0] ^= scratch.u[0];
	ctx->cmac.u[1] ^= scratch.u[1];

	ctx->nonce.c[0] = flags0;

	return 0;
}
LCRYPTO_ALIAS(CRYPTO_ccm128_encrypt);

int
CRYPTO_ccm128_decrypt(CCM128_CONTEXT *ctx,
    const unsigned char *inp, unsigned char *out,
    size_t len)
{
	size_t		 n;
	unsigned int	 i, L;
	unsigned char	 flags0 = ctx->nonce.c[0];
	block128_f	 block = ctx->block;
	void		*key = ctx->key;
	union {
		uint64_t u[2];
		uint8_t c[16];
	} scratch;

	if (!(flags0 & 0x40))
		(*block)(ctx->nonce.c, ctx->cmac.c, key);

	ctx->nonce.c[0] = L = flags0 & 7;
	for (n = 0, i = 15 - L; i < 15; ++i) {
		n |= ctx->nonce.c[i];
		ctx->nonce.c[i] = 0;
		n <<= 8;
	}
	n |= ctx->nonce.c[15];	/* reconstructed length */
	ctx->nonce.c[15] = 1;

	if (n != len)
		return -1;

	while (len >= 16) {
#ifdef __STRICT_ALIGNMENT
		union {
			uint64_t u[2];
			uint8_t c[16];
		} temp;
#endif
		(*block)(ctx->nonce.c, scratch.c, key);
		ctr64_inc(ctx->nonce.c);
#ifdef __STRICT_ALIGNMENT
		memcpy(temp.c, inp, 16);
		ctx->cmac.u[0] ^= (scratch.u[0] ^= temp.u[0]);
		ctx->cmac.u[1] ^= (scratch.u[1] ^= temp.u[1]);
		memcpy(out, scratch.c, 16);
#else
		ctx->cmac.u[0] ^= (((uint64_t *)out)[0] = scratch.u[0] ^
		    ((uint64_t *)inp)[0]);
		ctx->cmac.u[1] ^= (((uint64_t *)out)[1] = scratch.u[1] ^
		    ((uint64_t *)inp)[1]);
#endif
		(*block)(ctx->cmac.c, ctx->cmac.c, key);

		inp += 16;
		out += 16;
		len -= 16;
	}

	if (len) {
		(*block)(ctx->nonce.c, scratch.c, key);
		for (i = 0; i < len; ++i)
			ctx->cmac.c[i] ^= (out[i] = scratch.c[i] ^ inp[i]);
		(*block)(ctx->cmac.c, ctx->cmac.c, key);
	}

	for (i = 15 - L; i < 16; ++i)
		ctx->nonce.c[i] = 0;

	(*block)(ctx->nonce.c, scratch.c, key);
	ctx->cmac.u[0] ^= scratch.u[0];
	ctx->cmac.u[1] ^= scratch.u[1];

	ctx->nonce.c[0] = flags0;

	return 0;
}
LCRYPTO_ALIAS(CRYPTO_ccm128_decrypt);

static void
ctr64_add(unsigned char *counter, size_t inc)
{
	size_t n = 8, val = 0;

	counter += 8;
	do {
		--n;
		val += counter[n] + (inc & 0xff);
		counter[n] = (unsigned char)val;
		val >>= 8;	/* carry bit */
		inc >>= 8;
	} while (n && (inc || val));
}

int
CRYPTO_ccm128_encrypt_ccm64(CCM128_CONTEXT *ctx,
    const unsigned char *inp, unsigned char *out,
    size_t len, ccm128_f stream)
{
	size_t		 n;
	unsigned int	 i, L;
	unsigned char	 flags0 = ctx->nonce.c[0];
	block128_f	 block = ctx->block;
	void		*key = ctx->key;
	union {
		uint64_t u[2];
		uint8_t c[16];
	} scratch;

	if (!(flags0 & 0x40))
		(*block)(ctx->nonce.c, ctx->cmac.c, key),
		    ctx->blocks++;

	ctx->nonce.c[0] = L = flags0 & 7;
	for (n = 0, i = 15 - L; i < 15; ++i) {
		n |= ctx->nonce.c[i];
		ctx->nonce.c[i] = 0;
		n <<= 8;
	}
	n |= ctx->nonce.c[15];	/* reconstructed length */
	ctx->nonce.c[15] = 1;

	if (n != len)
		return -1;	/* length mismatch */

	ctx->blocks += ((len + 15) >> 3)|1;
	if (ctx->blocks > (U64(1) << 61))
		return -2; /* too much data */

	if ((n = len/16)) {
		(*stream)(inp, out, n, key, ctx->nonce.c, ctx->cmac.c);
		n *= 16;
		inp += n;
		out += n;
		len -= n;
		if (len)
			ctr64_add(ctx->nonce.c, n/16);
	}

	if (len) {
		for (i = 0; i < len; ++i)
			ctx->cmac.c[i] ^= inp[i];
		(*block)(ctx->cmac.c, ctx->cmac.c, key);
		(*block)(ctx->nonce.c, scratch.c, key);
		for (i = 0; i < len; ++i)
			out[i] = scratch.c[i] ^ inp[i];
	}

	for (i = 15 - L; i < 16; ++i)
		ctx->nonce.c[i] = 0;

	(*block)(ctx->nonce.c, scratch.c, key);
	ctx->cmac.u[0] ^= scratch.u[0];
	ctx->cmac.u[1] ^= scratch.u[1];

	ctx->nonce.c[0] = flags0;

	return 0;
}
LCRYPTO_ALIAS(CRYPTO_ccm128_encrypt_ccm64);

int
CRYPTO_ccm128_decrypt_ccm64(CCM128_CONTEXT *ctx,
    const unsigned char *inp, unsigned char *out,
    size_t len, ccm128_f stream)
{
	size_t		 n;
	unsigned int	 i, L;
	unsigned char	 flags0 = ctx->nonce.c[0];
	block128_f	 block = ctx->block;
	void		*key = ctx->key;
	union {
		uint64_t u[2];
		uint8_t c[16];
	} scratch;

	if (!(flags0 & 0x40))
		(*block)(ctx->nonce.c, ctx->cmac.c, key);

	ctx->nonce.c[0] = L = flags0 & 7;
	for (n = 0, i = 15 - L; i < 15; ++i) {
		n |= ctx->nonce.c[i];
		ctx->nonce.c[i] = 0;
		n <<= 8;
	}
	n |= ctx->nonce.c[15];	/* reconstructed length */
	ctx->nonce.c[15] = 1;

	if (n != len)
		return -1;

	if ((n = len/16)) {
		(*stream)(inp, out, n, key, ctx->nonce.c, ctx->cmac.c);
		n *= 16;
		inp += n;
		out += n;
		len -= n;
		if (len)
			ctr64_add(ctx->nonce.c, n/16);
	}

	if (len) {
		(*block)(ctx->nonce.c, scratch.c, key);
		for (i = 0; i < len; ++i)
			ctx->cmac.c[i] ^= (out[i] = scratch.c[i] ^ inp[i]);
		(*block)(ctx->cmac.c, ctx->cmac.c, key);
	}

	for (i = 15 - L; i < 16; ++i)
		ctx->nonce.c[i] = 0;

	(*block)(ctx->nonce.c, scratch.c, key);
	ctx->cmac.u[0] ^= scratch.u[0];
	ctx->cmac.u[1] ^= scratch.u[1];

	ctx->nonce.c[0] = flags0;

	return 0;
}
LCRYPTO_ALIAS(CRYPTO_ccm128_decrypt_ccm64);

size_t
CRYPTO_ccm128_tag(CCM128_CONTEXT *ctx, unsigned char *tag, size_t len)
{
	unsigned int M = (ctx->nonce.c[0] >> 3) & 7;	/* the M parameter */

	M *= 2;
	M += 2;
	if (len != M)
		return 0;
	memcpy(tag, ctx->cmac.c, M);
	return M;
}
LCRYPTO_ALIAS(CRYPTO_ccm128_tag);
