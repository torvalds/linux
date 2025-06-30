// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-256, as specified in
 * http://csrc.nist.gov/groups/STM/cavp/documents/shs/sha256-384-512.pdf
 *
 * SHA-256 code by Jean-Luc Cooke <jlcooke@certainkey.com>.
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2014 Red Hat Inc.
 */

#include <crypto/internal/blockhash.h>
#include <crypto/internal/sha2.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

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

MODULE_DESCRIPTION("SHA-256 Algorithm");
MODULE_LICENSE("GPL");
