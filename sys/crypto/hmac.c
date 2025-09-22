/*	$OpenBSD: hmac.c,v 1.4 2016/09/19 18:09:40 tedu Exp $	*/

/*-
 * Copyright (c) 2008 Damien Bergamini <damien.bergamini@free.fr>
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
 * This code implements the HMAC algorithm described in RFC 2104 using
 * the MD5, SHA1 and SHA-256 hash functions.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <crypto/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/hmac.h>

void
HMAC_MD5_Init(HMAC_MD5_CTX *ctx, const u_int8_t *key, u_int key_len)
{
	u_int8_t k_ipad[MD5_BLOCK_LENGTH];
	int i;

	if (key_len > MD5_BLOCK_LENGTH) {
		MD5Init(&ctx->ctx);
		MD5Update(&ctx->ctx, key, key_len);
		MD5Final(ctx->key, &ctx->ctx);
		ctx->key_len = MD5_DIGEST_LENGTH;
	} else {
		bcopy(key, ctx->key, key_len);
		ctx->key_len = key_len;
	}

	bzero(k_ipad, MD5_BLOCK_LENGTH);
	memcpy(k_ipad, ctx->key, ctx->key_len);
	for (i = 0; i < MD5_BLOCK_LENGTH; i++)
		k_ipad[i] ^= 0x36;

	MD5Init(&ctx->ctx);
	MD5Update(&ctx->ctx, k_ipad, MD5_BLOCK_LENGTH);

	explicit_bzero(k_ipad, sizeof k_ipad);
}

void
HMAC_MD5_Update(HMAC_MD5_CTX *ctx, const u_int8_t *data, u_int len)
{
	MD5Update(&ctx->ctx, data, len);
}

void
HMAC_MD5_Final(u_int8_t digest[MD5_DIGEST_LENGTH], HMAC_MD5_CTX *ctx)
{
	u_int8_t k_opad[MD5_BLOCK_LENGTH];
	int i;

	MD5Final(digest, &ctx->ctx);

	bzero(k_opad, MD5_BLOCK_LENGTH);
	memcpy(k_opad, ctx->key, ctx->key_len);
	for (i = 0; i < MD5_BLOCK_LENGTH; i++)
		k_opad[i] ^= 0x5c;

	MD5Init(&ctx->ctx);
	MD5Update(&ctx->ctx, k_opad, MD5_BLOCK_LENGTH);
	MD5Update(&ctx->ctx, digest, MD5_DIGEST_LENGTH);
	MD5Final(digest, &ctx->ctx);

	explicit_bzero(k_opad, sizeof k_opad);
}

void
HMAC_SHA1_Init(HMAC_SHA1_CTX *ctx, const u_int8_t *key, u_int key_len)
{
	u_int8_t k_ipad[SHA1_BLOCK_LENGTH];
	int i;

	if (key_len > SHA1_BLOCK_LENGTH) {
		SHA1Init(&ctx->ctx);
		SHA1Update(&ctx->ctx, key, key_len);
		SHA1Final(ctx->key, &ctx->ctx);
		ctx->key_len = SHA1_DIGEST_LENGTH;
	} else {
		bcopy(key, ctx->key, key_len);
		ctx->key_len = key_len;
	}

	bzero(k_ipad, SHA1_BLOCK_LENGTH);
	memcpy(k_ipad, ctx->key, ctx->key_len);
	for (i = 0; i < SHA1_BLOCK_LENGTH; i++)
		k_ipad[i] ^= 0x36;

	SHA1Init(&ctx->ctx);
	SHA1Update(&ctx->ctx, k_ipad, SHA1_BLOCK_LENGTH);

	explicit_bzero(k_ipad, sizeof k_ipad);
}

void
HMAC_SHA1_Update(HMAC_SHA1_CTX *ctx, const u_int8_t *data, u_int len)
{
	SHA1Update(&ctx->ctx, data, len);
}

void
HMAC_SHA1_Final(u_int8_t digest[SHA1_DIGEST_LENGTH], HMAC_SHA1_CTX *ctx)
{
	u_int8_t k_opad[SHA1_BLOCK_LENGTH];
	int i;

	SHA1Final(digest, &ctx->ctx);

	bzero(k_opad, SHA1_BLOCK_LENGTH);
	memcpy(k_opad, ctx->key, ctx->key_len);
	for (i = 0; i < SHA1_BLOCK_LENGTH; i++)
		k_opad[i] ^= 0x5c;

	SHA1Init(&ctx->ctx);
	SHA1Update(&ctx->ctx, k_opad, SHA1_BLOCK_LENGTH);
	SHA1Update(&ctx->ctx, digest, SHA1_DIGEST_LENGTH);
	SHA1Final(digest, &ctx->ctx);

	explicit_bzero(k_opad, sizeof k_opad);
}

void
HMAC_SHA256_Init(HMAC_SHA256_CTX *ctx, const u_int8_t *key, u_int key_len)
{
	u_int8_t k_ipad[SHA256_BLOCK_LENGTH];
	int i;

	if (key_len > SHA256_BLOCK_LENGTH) {
		SHA256Init(&ctx->ctx);
		SHA256Update(&ctx->ctx, key, key_len);
		SHA256Final(ctx->key, &ctx->ctx);
		ctx->key_len = SHA256_DIGEST_LENGTH;
	} else {
		bcopy(key, ctx->key, key_len);
		ctx->key_len = key_len;
	}

	bzero(k_ipad, SHA256_BLOCK_LENGTH);
	memcpy(k_ipad, ctx->key, ctx->key_len);
	for (i = 0; i < SHA256_BLOCK_LENGTH; i++)
		k_ipad[i] ^= 0x36;

	SHA256Init(&ctx->ctx);
	SHA256Update(&ctx->ctx, k_ipad, SHA256_BLOCK_LENGTH);

	explicit_bzero(k_ipad, sizeof k_ipad);
}

void
HMAC_SHA256_Update(HMAC_SHA256_CTX *ctx, const u_int8_t *data, u_int len)
{
	SHA256Update(&ctx->ctx, data, len);
}

void
HMAC_SHA256_Final(u_int8_t digest[SHA256_DIGEST_LENGTH], HMAC_SHA256_CTX *ctx)
{
	u_int8_t k_opad[SHA256_BLOCK_LENGTH];
	int i;

	SHA256Final(digest, &ctx->ctx);

	bzero(k_opad, SHA256_BLOCK_LENGTH);
	memcpy(k_opad, ctx->key, ctx->key_len);
	for (i = 0; i < SHA256_BLOCK_LENGTH; i++)
		k_opad[i] ^= 0x5c;

	SHA256Init(&ctx->ctx);
	SHA256Update(&ctx->ctx, k_opad, SHA256_BLOCK_LENGTH);
	SHA256Update(&ctx->ctx, digest, SHA256_DIGEST_LENGTH);
	SHA256Final(digest, &ctx->ctx);

	explicit_bzero(k_opad, sizeof k_opad);
}
