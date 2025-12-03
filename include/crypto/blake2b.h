/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef _CRYPTO_BLAKE2B_H
#define _CRYPTO_BLAKE2B_H

#include <linux/bug.h>
#include <linux/types.h>
#include <linux/string.h>

enum blake2b_lengths {
	BLAKE2B_BLOCK_SIZE = 128,
	BLAKE2B_HASH_SIZE = 64,
	BLAKE2B_KEY_SIZE = 64,

	BLAKE2B_160_HASH_SIZE = 20,
	BLAKE2B_256_HASH_SIZE = 32,
	BLAKE2B_384_HASH_SIZE = 48,
	BLAKE2B_512_HASH_SIZE = 64,
};

/**
 * struct blake2b_ctx - Context for hashing a message with BLAKE2b
 * @h: compression function state
 * @t: block counter
 * @f: finalization indicator
 * @buf: partial block buffer; 'buflen' bytes are valid
 * @buflen: number of bytes buffered in @buf
 * @outlen: length of output hash value in bytes, at most BLAKE2B_HASH_SIZE
 */
struct blake2b_ctx {
	/* 'h', 't', and 'f' are used in assembly code, so keep them as-is. */
	u64 h[8];
	u64 t[2];
	u64 f[2];
	u8 buf[BLAKE2B_BLOCK_SIZE];
	unsigned int buflen;
	unsigned int outlen;
};

enum blake2b_iv {
	BLAKE2B_IV0 = 0x6A09E667F3BCC908ULL,
	BLAKE2B_IV1 = 0xBB67AE8584CAA73BULL,
	BLAKE2B_IV2 = 0x3C6EF372FE94F82BULL,
	BLAKE2B_IV3 = 0xA54FF53A5F1D36F1ULL,
	BLAKE2B_IV4 = 0x510E527FADE682D1ULL,
	BLAKE2B_IV5 = 0x9B05688C2B3E6C1FULL,
	BLAKE2B_IV6 = 0x1F83D9ABFB41BD6BULL,
	BLAKE2B_IV7 = 0x5BE0CD19137E2179ULL,
};

static inline void __blake2b_init(struct blake2b_ctx *ctx, size_t outlen,
				  const void *key, size_t keylen)
{
	ctx->h[0] = BLAKE2B_IV0 ^ (0x01010000 | keylen << 8 | outlen);
	ctx->h[1] = BLAKE2B_IV1;
	ctx->h[2] = BLAKE2B_IV2;
	ctx->h[3] = BLAKE2B_IV3;
	ctx->h[4] = BLAKE2B_IV4;
	ctx->h[5] = BLAKE2B_IV5;
	ctx->h[6] = BLAKE2B_IV6;
	ctx->h[7] = BLAKE2B_IV7;
	ctx->t[0] = 0;
	ctx->t[1] = 0;
	ctx->f[0] = 0;
	ctx->f[1] = 0;
	ctx->buflen = 0;
	ctx->outlen = outlen;
	if (keylen) {
		memcpy(ctx->buf, key, keylen);
		memset(&ctx->buf[keylen], 0, BLAKE2B_BLOCK_SIZE - keylen);
		ctx->buflen = BLAKE2B_BLOCK_SIZE;
	}
}

/**
 * blake2b_init() - Initialize a BLAKE2b context for a new message (unkeyed)
 * @ctx: the context to initialize
 * @outlen: length of output hash value in bytes, at most BLAKE2B_HASH_SIZE
 *
 * Context: Any context.
 */
static inline void blake2b_init(struct blake2b_ctx *ctx, size_t outlen)
{
	__blake2b_init(ctx, outlen, NULL, 0);
}

/**
 * blake2b_init_key() - Initialize a BLAKE2b context for a new message (keyed)
 * @ctx: the context to initialize
 * @outlen: length of output hash value in bytes, at most BLAKE2B_HASH_SIZE
 * @key: the key
 * @keylen: the key length in bytes, at most BLAKE2B_KEY_SIZE
 *
 * Context: Any context.
 */
static inline void blake2b_init_key(struct blake2b_ctx *ctx, size_t outlen,
				    const void *key, size_t keylen)
{
	WARN_ON(IS_ENABLED(DEBUG) && (!outlen || outlen > BLAKE2B_HASH_SIZE ||
		!key || !keylen || keylen > BLAKE2B_KEY_SIZE));

	__blake2b_init(ctx, outlen, key, keylen);
}

/**
 * blake2b_update() - Update a BLAKE2b context with message data
 * @ctx: the context to update; must have been initialized
 * @in: the message data
 * @inlen: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
void blake2b_update(struct blake2b_ctx *ctx, const u8 *in, size_t inlen);

/**
 * blake2b_final() - Finish computing a BLAKE2b hash
 * @ctx: the context to finalize; must have been initialized
 * @out: (output) the resulting BLAKE2b hash.  Its length will be equal to the
 *	 @outlen that was passed to blake2b_init() or blake2b_init_key().
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void blake2b_final(struct blake2b_ctx *ctx, u8 *out);

/**
 * blake2b() - Compute BLAKE2b hash in one shot
 * @key: the key, or NULL for an unkeyed hash
 * @keylen: the key length in bytes (at most BLAKE2B_KEY_SIZE), or 0 for an
 *	    unkeyed hash
 * @in: the message data
 * @inlen: the data length in bytes
 * @out: (output) the resulting BLAKE2b hash, with length @outlen
 * @outlen: length of output hash value in bytes, at most BLAKE2B_HASH_SIZE
 *
 * Context: Any context.
 */
static inline void blake2b(const u8 *key, size_t keylen,
			   const u8 *in, size_t inlen,
			   u8 *out, size_t outlen)
{
	struct blake2b_ctx ctx;

	WARN_ON(IS_ENABLED(DEBUG) && ((!in && inlen > 0) || !out || !outlen ||
		outlen > BLAKE2B_HASH_SIZE || keylen > BLAKE2B_KEY_SIZE ||
		(!key && keylen)));

	__blake2b_init(&ctx, outlen, key, keylen);
	blake2b_update(&ctx, in, inlen);
	blake2b_final(&ctx, out);
}

#endif /* _CRYPTO_BLAKE2B_H */
