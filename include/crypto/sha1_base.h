/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sha1_base.h - core logic for SHA-1 implementations
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#ifndef _CRYPTO_SHA1_BASE_H
#define _CRYPTO_SHA1_BASE_H

#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <linux/math.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>

typedef void (sha1_block_fn)(struct sha1_state *sst, u8 const *src, int blocks);

static inline int sha1_base_init(struct shash_desc *desc)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA1_H0;
	sctx->state[1] = SHA1_H1;
	sctx->state[2] = SHA1_H2;
	sctx->state[3] = SHA1_H3;
	sctx->state[4] = SHA1_H4;
	sctx->count = 0;

	return 0;
}

static inline int sha1_base_do_update_blocks(struct shash_desc *desc,
					     const u8 *data,
					     unsigned int len,
					     sha1_block_fn *block_fn)
{
	unsigned int remain = len - round_down(len, SHA1_BLOCK_SIZE);
	struct sha1_state *sctx = shash_desc_ctx(desc);

	sctx->count += len - remain;
	block_fn(sctx, data, len / SHA1_BLOCK_SIZE);
	return remain;
}

static inline int sha1_base_do_finup(struct shash_desc *desc,
				     const u8 *src, unsigned int len,
				     sha1_block_fn *block_fn)
{
	unsigned int bit_offset = SHA1_BLOCK_SIZE / 8 - 1;
	struct sha1_state *sctx = shash_desc_ctx(desc);
	union {
		__be64 b64[SHA1_BLOCK_SIZE / 4];
		u8 u8[SHA1_BLOCK_SIZE * 2];
	} block = {};

	if (len >= bit_offset * 8)
		bit_offset += SHA1_BLOCK_SIZE / 8;
	memcpy(&block, src, len);
	block.u8[len] = 0x80;
	sctx->count += len;
	block.b64[bit_offset] = cpu_to_be64(sctx->count << 3);
	block_fn(sctx, block.u8, (bit_offset + 1) * 8 / SHA1_BLOCK_SIZE);
	memzero_explicit(&block, sizeof(block));

	return 0;
}

static inline int sha1_base_finish(struct shash_desc *desc, u8 *out)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);
	__be32 *digest = (__be32 *)out;
	int i;

	for (i = 0; i < SHA1_DIGEST_SIZE / sizeof(__be32); i++)
		put_unaligned_be32(sctx->state[i], digest++);

	return 0;
}

#endif /* _CRYPTO_SHA1_BASE_H */
