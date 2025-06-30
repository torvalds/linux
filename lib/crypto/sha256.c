// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-224, SHA-256, HMAC-SHA224, and HMAC-SHA256 library functions
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2014 Red Hat Inc.
 */

#include <crypto/hmac.h>
#include <crypto/internal/blockhash.h>
#include <crypto/internal/sha2.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/wordpart.h>

static const struct sha256_block_state sha224_iv = {
	.h = {
		SHA224_H0, SHA224_H1, SHA224_H2, SHA224_H3,
		SHA224_H4, SHA224_H5, SHA224_H6, SHA224_H7,
	},
};

static const struct sha256_block_state sha256_iv = {
	.h = {
		SHA256_H0, SHA256_H1, SHA256_H2, SHA256_H3,
		SHA256_H4, SHA256_H5, SHA256_H6, SHA256_H7,
	},
};

/*
 * If __DISABLE_EXPORTS is defined, then this file is being compiled for a
 * pre-boot environment.  In that case, ignore the kconfig options, pull the
 * generic code into the same translation unit, and use that only.
 */
#ifdef __DISABLE_EXPORTS
#include "sha256-generic.c"
#endif

static inline bool sha256_purgatory(void)
{
	return __is_defined(__DISABLE_EXPORTS);
}

static inline void sha256_blocks(struct sha256_block_state *state,
				 const u8 *data, size_t nblocks)
{
	sha256_choose_blocks(state->h, data, nblocks, sha256_purgatory(), false);
}

static void __sha256_init(struct __sha256_ctx *ctx,
			  const struct sha256_block_state *iv,
			  u64 initial_bytecount)
{
	ctx->state = *iv;
	ctx->bytecount = initial_bytecount;
}

void sha224_init(struct sha224_ctx *ctx)
{
	__sha256_init(&ctx->ctx, &sha224_iv, 0);
}
EXPORT_SYMBOL_GPL(sha224_init);

void sha256_init(struct sha256_ctx *ctx)
{
	__sha256_init(&ctx->ctx, &sha256_iv, 0);
}
EXPORT_SYMBOL_GPL(sha256_init);

void __sha256_update(struct __sha256_ctx *ctx, const u8 *data, size_t len)
{
	size_t partial = ctx->bytecount % SHA256_BLOCK_SIZE;

	ctx->bytecount += len;
	BLOCK_HASH_UPDATE_BLOCKS(sha256_blocks, &ctx->state, data, len,
				 SHA256_BLOCK_SIZE, ctx->buf, partial);
}
EXPORT_SYMBOL(__sha256_update);

static void __sha256_final(struct __sha256_ctx *ctx,
			   u8 *out, size_t digest_size)
{
	u64 bitcount = ctx->bytecount << 3;
	size_t partial = ctx->bytecount % SHA256_BLOCK_SIZE;

	ctx->buf[partial++] = 0x80;
	if (partial > SHA256_BLOCK_SIZE - 8) {
		memset(&ctx->buf[partial], 0, SHA256_BLOCK_SIZE - partial);
		sha256_blocks(&ctx->state, ctx->buf, 1);
		partial = 0;
	}
	memset(&ctx->buf[partial], 0, SHA256_BLOCK_SIZE - 8 - partial);
	*(__be64 *)&ctx->buf[SHA256_BLOCK_SIZE - 8] = cpu_to_be64(bitcount);
	sha256_blocks(&ctx->state, ctx->buf, 1);

	for (size_t i = 0; i < digest_size; i += 4)
		put_unaligned_be32(ctx->state.h[i / 4], out + i);
}

void sha224_final(struct sha224_ctx *ctx, u8 out[SHA224_DIGEST_SIZE])
{
	__sha256_final(&ctx->ctx, out, SHA224_DIGEST_SIZE);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL(sha224_final);

void sha256_final(struct sha256_ctx *ctx, u8 out[SHA256_DIGEST_SIZE])
{
	__sha256_final(&ctx->ctx, out, SHA256_DIGEST_SIZE);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL(sha256_final);

void sha224(const u8 *data, size_t len, u8 out[SHA224_DIGEST_SIZE])
{
	struct sha224_ctx ctx;

	sha224_init(&ctx);
	sha224_update(&ctx, data, len);
	sha224_final(&ctx, out);
}
EXPORT_SYMBOL(sha224);

void sha256(const u8 *data, size_t len, u8 out[SHA256_DIGEST_SIZE])
{
	struct sha256_ctx ctx;

	sha256_init(&ctx);
	sha256_update(&ctx, data, len);
	sha256_final(&ctx, out);
}
EXPORT_SYMBOL(sha256);

/* pre-boot environment (as indicated by __DISABLE_EXPORTS) doesn't need HMAC */
#ifndef __DISABLE_EXPORTS
static void __hmac_sha256_preparekey(struct __hmac_sha256_key *key,
				     const u8 *raw_key, size_t raw_key_len,
				     const struct sha256_block_state *iv)
{
	union {
		u8 b[SHA256_BLOCK_SIZE];
		unsigned long w[SHA256_BLOCK_SIZE / sizeof(unsigned long)];
	} derived_key = { 0 };

	if (unlikely(raw_key_len > SHA256_BLOCK_SIZE)) {
		if (iv == &sha224_iv)
			sha224(raw_key, raw_key_len, derived_key.b);
		else
			sha256(raw_key, raw_key_len, derived_key.b);
	} else {
		memcpy(derived_key.b, raw_key, raw_key_len);
	}

	for (size_t i = 0; i < ARRAY_SIZE(derived_key.w); i++)
		derived_key.w[i] ^= REPEAT_BYTE(HMAC_IPAD_VALUE);
	key->istate = *iv;
	sha256_blocks(&key->istate, derived_key.b, 1);

	for (size_t i = 0; i < ARRAY_SIZE(derived_key.w); i++)
		derived_key.w[i] ^= REPEAT_BYTE(HMAC_OPAD_VALUE ^
						HMAC_IPAD_VALUE);
	key->ostate = *iv;
	sha256_blocks(&key->ostate, derived_key.b, 1);

	memzero_explicit(&derived_key, sizeof(derived_key));
}

void hmac_sha224_preparekey(struct hmac_sha224_key *key,
			    const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha256_preparekey(&key->key, raw_key, raw_key_len, &sha224_iv);
}
EXPORT_SYMBOL_GPL(hmac_sha224_preparekey);

void hmac_sha256_preparekey(struct hmac_sha256_key *key,
			    const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha256_preparekey(&key->key, raw_key, raw_key_len, &sha256_iv);
}
EXPORT_SYMBOL_GPL(hmac_sha256_preparekey);

void __hmac_sha256_init(struct __hmac_sha256_ctx *ctx,
			const struct __hmac_sha256_key *key)
{
	__sha256_init(&ctx->sha_ctx, &key->istate, SHA256_BLOCK_SIZE);
	ctx->ostate = key->ostate;
}
EXPORT_SYMBOL_GPL(__hmac_sha256_init);

static void __hmac_sha256_final(struct __hmac_sha256_ctx *ctx,
				u8 *out, size_t digest_size)
{
	/* Generate the padded input for the outer hash in ctx->sha_ctx.buf. */
	__sha256_final(&ctx->sha_ctx, ctx->sha_ctx.buf, digest_size);
	memset(&ctx->sha_ctx.buf[digest_size], 0,
	       SHA256_BLOCK_SIZE - digest_size);
	ctx->sha_ctx.buf[digest_size] = 0x80;
	*(__be32 *)&ctx->sha_ctx.buf[SHA256_BLOCK_SIZE - 4] =
		cpu_to_be32(8 * (SHA256_BLOCK_SIZE + digest_size));

	/* Compute the outer hash, which gives the HMAC value. */
	sha256_blocks(&ctx->ostate, ctx->sha_ctx.buf, 1);
	for (size_t i = 0; i < digest_size; i += 4)
		put_unaligned_be32(ctx->ostate.h[i / 4], out + i);

	memzero_explicit(ctx, sizeof(*ctx));
}

void hmac_sha224_final(struct hmac_sha224_ctx *ctx,
		       u8 out[SHA224_DIGEST_SIZE])
{
	__hmac_sha256_final(&ctx->ctx, out, SHA224_DIGEST_SIZE);
}
EXPORT_SYMBOL_GPL(hmac_sha224_final);

void hmac_sha256_final(struct hmac_sha256_ctx *ctx,
		       u8 out[SHA256_DIGEST_SIZE])
{
	__hmac_sha256_final(&ctx->ctx, out, SHA256_DIGEST_SIZE);
}
EXPORT_SYMBOL_GPL(hmac_sha256_final);

void hmac_sha224(const struct hmac_sha224_key *key,
		 const u8 *data, size_t data_len, u8 out[SHA224_DIGEST_SIZE])
{
	struct hmac_sha224_ctx ctx;

	hmac_sha224_init(&ctx, key);
	hmac_sha224_update(&ctx, data, data_len);
	hmac_sha224_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha224);

void hmac_sha256(const struct hmac_sha256_key *key,
		 const u8 *data, size_t data_len, u8 out[SHA256_DIGEST_SIZE])
{
	struct hmac_sha256_ctx ctx;

	hmac_sha256_init(&ctx, key);
	hmac_sha256_update(&ctx, data, data_len);
	hmac_sha256_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha256);

void hmac_sha224_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			     const u8 *data, size_t data_len,
			     u8 out[SHA224_DIGEST_SIZE])
{
	struct hmac_sha224_key key;

	hmac_sha224_preparekey(&key, raw_key, raw_key_len);
	hmac_sha224(&key, data, data_len, out);

	memzero_explicit(&key, sizeof(key));
}
EXPORT_SYMBOL_GPL(hmac_sha224_usingrawkey);

void hmac_sha256_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			     const u8 *data, size_t data_len,
			     u8 out[SHA256_DIGEST_SIZE])
{
	struct hmac_sha256_key key;

	hmac_sha256_preparekey(&key, raw_key, raw_key_len);
	hmac_sha256(&key, data, data_len, out);

	memzero_explicit(&key, sizeof(key));
}
EXPORT_SYMBOL_GPL(hmac_sha256_usingrawkey);
#endif /* !__DISABLE_EXPORTS */

MODULE_DESCRIPTION("SHA-224, SHA-256, HMAC-SHA224, and HMAC-SHA256 library functions");
MODULE_LICENSE("GPL");
