/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef _CRYPTO_BLAKE2S_H
#define _CRYPTO_BLAKE2S_H

#include <string.h>
#include <linux/types.h>

#define BLAKE2S_BLOCK_SIZE 64

struct blake2s_ctx {
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

static inline void blake2s_init(struct blake2s_ctx *ctx, size_t outlen)
{
	__blake2s_init(ctx, outlen, NULL, 0);
}

static inline void blake2s_init_key(struct blake2s_ctx *ctx, size_t outlen,
				    const void *key, size_t keylen)
{
	__blake2s_init(ctx, outlen, key, keylen);
}

void blake2s_update(struct blake2s_ctx *ctx, const u8 *in, size_t inlen);

void blake2s_final(struct blake2s_ctx *ctx, u8 *out);

#endif /* _CRYPTO_BLAKE2S_H */
