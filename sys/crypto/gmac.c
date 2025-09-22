/*	$OpenBSD: gmac.c,v 1.10 2017/05/02 11:44:32 mikeb Exp $	*/

/*
 * Copyright (c) 2010 Mike Belopuhov
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This code implements the Message Authentication part of the
 * Galois/Counter Mode (as being described in the RFC 4543) using
 * the AES cipher.  FIPS SP 800-38D describes the algorithm details.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <crypto/aes.h>
#include <crypto/gmac.h>

void	ghash_gfmul(uint32_t *, uint32_t *, uint32_t *);
void	ghash_update_mi(GHASH_CTX *, uint8_t *, size_t);

/* Allow overriding with optimized MD function */
void	(*ghash_update)(GHASH_CTX *, uint8_t *, size_t) = ghash_update_mi;

/* Computes a block multiplication in the GF(2^128) */
void
ghash_gfmul(uint32_t *X, uint32_t *Y, uint32_t *product)
{
	uint32_t	v[4];
	uint32_t	z[4] = { 0, 0, 0, 0};
	uint8_t		*x = (uint8_t *)X;
	uint32_t	mask;
	int		i;

	v[0] = betoh32(Y[0]);
	v[1] = betoh32(Y[1]);
	v[2] = betoh32(Y[2]);
	v[3] = betoh32(Y[3]);

	for (i = 0; i < GMAC_BLOCK_LEN * 8; i++) {
		/* update Z */
		mask = !!(x[i >> 3] & (1 << (~i & 7)));
		mask = ~(mask - 1);
		z[0] ^= v[0] & mask;
		z[1] ^= v[1] & mask;
		z[2] ^= v[2] & mask;
		z[3] ^= v[3] & mask;

		/* update V */
		mask = ~((v[3] & 1) - 1);
		v[3] = (v[2] << 31) | (v[3] >> 1);
		v[2] = (v[1] << 31) | (v[2] >> 1);
		v[1] = (v[0] << 31) | (v[1] >> 1);
		v[0] = (v[0] >> 1) ^ (0xe1000000 & mask);
	}

	product[0] = htobe32(z[0]);
	product[1] = htobe32(z[1]);
	product[2] = htobe32(z[2]);
	product[3] = htobe32(z[3]);
}

void
ghash_update_mi(GHASH_CTX *ctx, uint8_t *X, size_t len)
{
	uint32_t	*x = (uint32_t *)X;
	uint32_t	*s = (uint32_t *)ctx->S;
	uint32_t	*y = (uint32_t *)ctx->Z;
	int		i;

	for (i = 0; i < len / GMAC_BLOCK_LEN; i++) {
		s[0] = y[0] ^ x[0];
		s[1] = y[1] ^ x[1];
		s[2] = y[2] ^ x[2];
		s[3] = y[3] ^ x[3];

		ghash_gfmul((uint32_t *)ctx->S, (uint32_t *)ctx->H,
		    (uint32_t *)ctx->S);

		y = s;
		x += 4;
	}

	bcopy(ctx->S, ctx->Z, GMAC_BLOCK_LEN);
}

#define AESCTR_NONCESIZE	4

void
AES_GMAC_Init(void *xctx)
{
	AES_GMAC_CTX	*ctx = xctx;

	bzero(ctx->ghash.H, GMAC_BLOCK_LEN);
	bzero(ctx->ghash.S, GMAC_BLOCK_LEN);
	bzero(ctx->ghash.Z, GMAC_BLOCK_LEN);
	bzero(ctx->J, GMAC_BLOCK_LEN);
}

void
AES_GMAC_Setkey(void *xctx, const uint8_t *key, uint16_t klen)
{
	AES_GMAC_CTX	*ctx = xctx;

	AES_Setkey(&ctx->K, key, klen - AESCTR_NONCESIZE);
	/* copy out salt to the counter block */
	bcopy(key + klen - AESCTR_NONCESIZE, ctx->J, AESCTR_NONCESIZE);
	/* prepare a hash subkey */
	AES_Encrypt(&ctx->K, ctx->ghash.H, ctx->ghash.H);
}

void
AES_GMAC_Reinit(void *xctx, const uint8_t *iv, uint16_t ivlen)
{
	AES_GMAC_CTX	*ctx = xctx;

	/* copy out IV to the counter block */
	bcopy(iv, ctx->J + AESCTR_NONCESIZE, ivlen);
}

int
AES_GMAC_Update(void *xctx, const uint8_t *data, uint16_t len)
{
	AES_GMAC_CTX	*ctx = xctx;
	uint32_t	blk[4] = { 0, 0, 0, 0 };
	int		plen;

	if (len > 0) {
		plen = len % GMAC_BLOCK_LEN;
		if (len >= GMAC_BLOCK_LEN)
			(*ghash_update)(&ctx->ghash, (uint8_t *)data,
			    len - plen);
		if (plen) {
			memcpy((uint8_t *)blk, (uint8_t *)data + (len - plen),
			    plen);
			(*ghash_update)(&ctx->ghash, (uint8_t *)blk,
			    GMAC_BLOCK_LEN);
		}
	}
	return (0);
}

void
AES_GMAC_Final(uint8_t digest[GMAC_DIGEST_LEN], void *xctx)
{
	AES_GMAC_CTX	*ctx = xctx;
	uint8_t		keystream[GMAC_BLOCK_LEN];
	int		i;

	/* do one round of GCTR */
	ctx->J[GMAC_BLOCK_LEN - 1] = 1;
	AES_Encrypt(&ctx->K, ctx->J, keystream);
	for (i = 0; i < GMAC_DIGEST_LEN; i++)
		digest[i] = ctx->ghash.S[i] ^ keystream[i];
	explicit_bzero(keystream, sizeof(keystream));
}
