/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sm3_base.h - core logic for SM3 implementations
 *
 * Copyright (C) 2017 ARM Limited or its affiliates.
 * Written by Gilad Ben-Yossef <gilad@benyossef.com>
 */

#ifndef _CRYPTO_SM3_BASE_H
#define _CRYPTO_SM3_BASE_H

#include <crypto/internal/hash.h>
#include <crypto/sm3.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>

typedef void (sm3_block_fn)(struct sm3_state *sst, u8 const *src, int blocks);

static inline int sm3_base_init(struct shash_desc *desc)
{
	sm3_init(shash_desc_ctx(desc));
	return 0;
}

static inline int sm3_base_do_update_blocks(struct shash_desc *desc,
					    const u8 *data, unsigned int len,
					    sm3_block_fn *block_fn)
{
	unsigned int remain = len - round_down(len, SM3_BLOCK_SIZE);
	struct sm3_state *sctx = shash_desc_ctx(desc);

	sctx->count += len - remain;
	block_fn(sctx, data, len / SM3_BLOCK_SIZE);
	return remain;
}

static inline int sm3_base_do_finup(struct shash_desc *desc,
				    const u8 *src, unsigned int len,
				    sm3_block_fn *block_fn)
{
	unsigned int bit_offset = SM3_BLOCK_SIZE / 8 - 1;
	struct sm3_state *sctx = shash_desc_ctx(desc);
	union {
		__be64 b64[SM3_BLOCK_SIZE / 4];
		u8 u8[SM3_BLOCK_SIZE * 2];
	} block = {};

	if (len >= SM3_BLOCK_SIZE) {
		int remain;

		remain = sm3_base_do_update_blocks(desc, src, len, block_fn);
		src += len - remain;
		len = remain;
	}

	if (len >= bit_offset * 8)
		bit_offset += SM3_BLOCK_SIZE / 8;
	memcpy(&block, src, len);
	block.u8[len] = 0x80;
	sctx->count += len;
	block.b64[bit_offset] = cpu_to_be64(sctx->count << 3);
	block_fn(sctx, block.u8, (bit_offset + 1) * 8 / SM3_BLOCK_SIZE);
	memzero_explicit(&block, sizeof(block));

	return 0;
}

static inline int sm3_base_finish(struct shash_desc *desc, u8 *out)
{
	struct sm3_state *sctx = shash_desc_ctx(desc);
	__be32 *digest = (__be32 *)out;
	int i;

	for (i = 0; i < SM3_DIGEST_SIZE / sizeof(__be32); i++)
		put_unaligned_be32(sctx->state[i], digest++);
	return 0;
}

#endif /* _CRYPTO_SM3_BASE_H */
