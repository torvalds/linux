// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright 2025 Google LLC
 *
 * This is an implementation of the BLAKE2b hash and PRF functions.
 *
 * Information: https://blake2.net/
 */

#include <crypto/blake2b.h>
#include <linux/bug.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unroll.h>
#include <linux/types.h>

static const u8 blake2b_sigma[12][16] = {
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
	{ 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
	{ 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
	{ 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
	{ 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
	{ 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
	{ 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
	{ 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
	{ 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 }
};

static inline void blake2b_increment_counter(struct blake2b_ctx *ctx, u32 inc)
{
	ctx->t[0] += inc;
	ctx->t[1] += (ctx->t[0] < inc);
}

static void __maybe_unused
blake2b_compress_generic(struct blake2b_ctx *ctx,
			 const u8 *data, size_t nblocks, u32 inc)
{
	u64 m[16];
	u64 v[16];
	int i;

	WARN_ON(IS_ENABLED(DEBUG) &&
		(nblocks > 1 && inc != BLAKE2B_BLOCK_SIZE));

	while (nblocks > 0) {
		blake2b_increment_counter(ctx, inc);
		memcpy(m, data, BLAKE2B_BLOCK_SIZE);
		le64_to_cpu_array(m, ARRAY_SIZE(m));
		memcpy(v, ctx->h, 64);
		v[ 8] = BLAKE2B_IV0;
		v[ 9] = BLAKE2B_IV1;
		v[10] = BLAKE2B_IV2;
		v[11] = BLAKE2B_IV3;
		v[12] = BLAKE2B_IV4 ^ ctx->t[0];
		v[13] = BLAKE2B_IV5 ^ ctx->t[1];
		v[14] = BLAKE2B_IV6 ^ ctx->f[0];
		v[15] = BLAKE2B_IV7 ^ ctx->f[1];

#define G(r, i, a, b, c, d) do { \
	a += b + m[blake2b_sigma[r][2 * i + 0]]; \
	d = ror64(d ^ a, 32); \
	c += d; \
	b = ror64(b ^ c, 24); \
	a += b + m[blake2b_sigma[r][2 * i + 1]]; \
	d = ror64(d ^ a, 16); \
	c += d; \
	b = ror64(b ^ c, 63); \
} while (0)

#ifdef CONFIG_64BIT
		/*
		 * Unroll the rounds loop to enable constant-folding of the
		 * blake2b_sigma values.  Seems worthwhile on 64-bit kernels.
		 * Not worthwhile on 32-bit kernels because the code size is
		 * already so large there due to BLAKE2b using 64-bit words.
		 */
		unrolled_full
#endif
		for (int r = 0; r < 12; r++) {
			G(r, 0, v[0], v[4], v[8], v[12]);
			G(r, 1, v[1], v[5], v[9], v[13]);
			G(r, 2, v[2], v[6], v[10], v[14]);
			G(r, 3, v[3], v[7], v[11], v[15]);
			G(r, 4, v[0], v[5], v[10], v[15]);
			G(r, 5, v[1], v[6], v[11], v[12]);
			G(r, 6, v[2], v[7], v[8], v[13]);
			G(r, 7, v[3], v[4], v[9], v[14]);
		}
#undef G

		for (i = 0; i < 8; ++i)
			ctx->h[i] ^= v[i] ^ v[i + 8];

		data += BLAKE2B_BLOCK_SIZE;
		--nblocks;
	}
}

#ifdef CONFIG_CRYPTO_LIB_BLAKE2B_ARCH
#include "blake2b.h" /* $(SRCARCH)/blake2b.h */
#else
#define blake2b_compress blake2b_compress_generic
#endif

static inline void blake2b_set_lastblock(struct blake2b_ctx *ctx)
{
	ctx->f[0] = -1;
}

void blake2b_update(struct blake2b_ctx *ctx, const u8 *in, size_t inlen)
{
	const size_t fill = BLAKE2B_BLOCK_SIZE - ctx->buflen;

	if (unlikely(!inlen))
		return;
	if (inlen > fill) {
		memcpy(ctx->buf + ctx->buflen, in, fill);
		blake2b_compress(ctx, ctx->buf, 1, BLAKE2B_BLOCK_SIZE);
		ctx->buflen = 0;
		in += fill;
		inlen -= fill;
	}
	if (inlen > BLAKE2B_BLOCK_SIZE) {
		const size_t nblocks = DIV_ROUND_UP(inlen, BLAKE2B_BLOCK_SIZE);

		blake2b_compress(ctx, in, nblocks - 1, BLAKE2B_BLOCK_SIZE);
		in += BLAKE2B_BLOCK_SIZE * (nblocks - 1);
		inlen -= BLAKE2B_BLOCK_SIZE * (nblocks - 1);
	}
	memcpy(ctx->buf + ctx->buflen, in, inlen);
	ctx->buflen += inlen;
}
EXPORT_SYMBOL(blake2b_update);

void blake2b_final(struct blake2b_ctx *ctx, u8 *out)
{
	WARN_ON(IS_ENABLED(DEBUG) && !out);
	blake2b_set_lastblock(ctx);
	memset(ctx->buf + ctx->buflen, 0,
	       BLAKE2B_BLOCK_SIZE - ctx->buflen); /* Padding */
	blake2b_compress(ctx, ctx->buf, 1, ctx->buflen);
	cpu_to_le64_array(ctx->h, ARRAY_SIZE(ctx->h));
	memcpy(out, ctx->h, ctx->outlen);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL(blake2b_final);

#ifdef blake2b_mod_init_arch
static int __init blake2b_mod_init(void)
{
	blake2b_mod_init_arch();
	return 0;
}
subsys_initcall(blake2b_mod_init);

static void __exit blake2b_mod_exit(void)
{
}
module_exit(blake2b_mod_exit);
#endif

MODULE_DESCRIPTION("BLAKE2b hash function");
MODULE_LICENSE("GPL");
