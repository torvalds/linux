/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef _CRYPTO_BLAKE2S_H
#define _CRYPTO_BLAKE2S_H

#include <linux/bug.h>
#include <linux/kconfig.h>
#include <linux/types.h>
#include <linux/string.h>

enum blake2s_lengths {
	BLAKE2S_BLOCK_SIZE = 64,
	BLAKE2S_HASH_SIZE = 32,
	BLAKE2S_KEY_SIZE = 32,

	BLAKE2S_128_HASH_SIZE = 16,
	BLAKE2S_160_HASH_SIZE = 20,
	BLAKE2S_224_HASH_SIZE = 28,
	BLAKE2S_256_HASH_SIZE = 32,
};

/**
 * struct blake2s_ctx - Context for hashing a message with BLAKE2s
 * @h: compression function state
 * @t: block counter
 * @f: finalization indicator
 * @buf: partial block buffer; 'buflen' bytes are valid
 * @buflen: number of bytes buffered in @buf
 * @outlen: length of output hash value in bytes, at most BLAKE2S_HASH_SIZE
 */
struct blake2s_ctx {
	/* 'h', 't', and 'f' are used in assembly code, so keep them as-is. */
	u32 h[8];
	u32 t[2];
	u32 f[2];
	u8 buf[BLAKE2S_BLOCK_SIZE];
	unsigned int buflen;
	unsigned int outlen;
};

enum blake2s_iv {
	BLAKE2S_IV0 = 0x6A09E667UL,
	BLAKE2S_IV1 = 0xBB67AE85UL,
	BLAKE2S_IV2 = 0x3C6EF372UL,
	BLAKE2S_IV3 = 0xA54FF53AUL,
	BLAKE2S_IV4 = 0x510E527FUL,
	BLAKE2S_IV5 = 0x9B05688CUL,
	BLAKE2S_IV6 = 0x1F83D9ABUL,
	BLAKE2S_IV7 = 0x5BE0CD19UL,
};

static inline void __blake2s_init(struct blake2s_ctx *ctx, size_t outlen,
				  const void *key, size_t keylen)
{
	ctx->h[0] = BLAKE2S_IV0 ^ (0x01010000 | keylen << 8 | outlen);
	ctx->h[1] = BLAKE2S_IV1;
	ctx->h[2] = BLAKE2S_IV2;
	ctx->h[3] = BLAKE2S_IV3;
	ctx->h[4] = BLAKE2S_IV4;
	ctx->h[5] = BLAKE2S_IV5;
	ctx->h[6] = BLAKE2S_IV6;
	ctx->h[7] = BLAKE2S_IV7;
	ctx->t[0] = 0;
	ctx->t[1] = 0;
	ctx->f[0] = 0;
	ctx->f[1] = 0;
	ctx->buflen = 0;
	ctx->outlen = outlen;
	if (keylen) {
		memcpy(ctx->buf, key, keylen);
		memset(&ctx->buf[keylen], 0, BLAKE2S_BLOCK_SIZE - keylen);
		ctx->buflen = BLAKE2S_BLOCK_SIZE;
	}
}

/**
 * blake2s_init() - Initialize a BLAKE2s context for a new message (unkeyed)
 * @ctx: the context to initialize
 * @outlen: length of output hash value in bytes, at most BLAKE2S_HASH_SIZE
 *
 * Context: Any context.
 */
static inline void blake2s_init(struct blake2s_ctx *ctx, size_t outlen)
{
	__blake2s_init(ctx, outlen, NULL, 0);
}

/**
 * blake2s_init_key() - Initialize a BLAKE2s context for a new message (keyed)
 * @ctx: the context to initialize
 * @outlen: length of output hash value in bytes, at most BLAKE2S_HASH_SIZE
 * @key: the key
 * @keylen: the key length in bytes, at most BLAKE2S_KEY_SIZE
 *
 * Context: Any context.
 */
static inline void blake2s_init_key(struct blake2s_ctx *ctx, size_t outlen,
				    const void *key, size_t keylen)
{
	WARN_ON(IS_ENABLED(DEBUG) && (!outlen || outlen > BLAKE2S_HASH_SIZE ||
		!key || !keylen || keylen > BLAKE2S_KEY_SIZE));

	__blake2s_init(ctx, outlen, key, keylen);
}

/**
 * blake2s_update() - Update a BLAKE2s context with message data
 * @ctx: the context to update; must have been initialized
 * @in: the message data
 * @inlen: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
void blake2s_update(struct blake2s_ctx *ctx, const u8 *in, size_t inlen);

/**
 * blake2s_final() - Finish computing a BLAKE2s hash
 * @ctx: the context to finalize; must have been initialized
 * @out: (output) the resulting BLAKE2s hash.  Its length will be equal to the
 *	 @outlen that was passed to blake2s_init() or blake2s_init_key().
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void blake2s_final(struct blake2s_ctx *ctx, u8 *out);

/**
 * blake2s() - Compute BLAKE2s hash in one shot
 * @key: the key, or NULL for an unkeyed hash
 * @keylen: the key length in bytes (at most BLAKE2S_KEY_SIZE), or 0 for an
 *	    unkeyed hash
 * @in: the message data
 * @inlen: the data length in bytes
 * @out: (output) the resulting BLAKE2s hash, with length @outlen
 * @outlen: length of output hash value in bytes, at most BLAKE2S_HASH_SIZE
 *
 * Context: Any context.
 */
static inline void blake2s(const u8 *key, size_t keylen,
			   const u8 *in, size_t inlen,
			   u8 *out, size_t outlen)
{
	struct blake2s_ctx ctx;

	WARN_ON(IS_ENABLED(DEBUG) && ((!in && inlen > 0) || !out || !outlen ||
		outlen > BLAKE2S_HASH_SIZE || keylen > BLAKE2S_KEY_SIZE ||
		(!key && keylen)));

	__blake2s_init(&ctx, outlen, key, keylen);
	blake2s_update(&ctx, in, inlen);
	blake2s_final(&ctx, out);
}

#endif /* _CRYPTO_BLAKE2S_H */
