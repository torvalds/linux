/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sm3_base.h - core logic for SM3 implementations
 *
 * Copyright (C) 2017 ARM Limited or its affiliates.
 * Written by Gilad Ben-Yossef <gilad@benyossef.com>
 */

#include <crypto/internal/hash.h>
#include <crypto/sm3.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include <asm/unaligned.h>

typedef void (sm3_block_fn)(struct sm3_state *sst, u8 const *src, int blocks);

static inline int sm3_base_init(struct shash_desc *desc)
{
	struct sm3_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SM3_IVA;
	sctx->state[1] = SM3_IVB;
	sctx->state[2] = SM3_IVC;
	sctx->state[3] = SM3_IVD;
	sctx->state[4] = SM3_IVE;
	sctx->state[5] = SM3_IVF;
	sctx->state[6] = SM3_IVG;
	sctx->state[7] = SM3_IVH;
	sctx->count = 0;

	return 0;
}

static inline int sm3_base_do_update(struct shash_desc *desc,
				      const u8 *data,
				      unsigned int len,
				      sm3_block_fn *block_fn)
{
	struct sm3_state *sctx = shash_desc_ctx(desc);
	unsigned int partial = sctx->count % SM3_BLOCK_SIZE;

	sctx->count += len;

	if (unlikely((partial + len) >= SM3_BLOCK_SIZE)) {
		int blocks;

		if (partial) {
			int p = SM3_BLOCK_SIZE - partial;

			memcpy(sctx->buffer + partial, data, p);
			data += p;
			len -= p;

			block_fn(sctx, sctx->buffer, 1);
		}

		blocks = len / SM3_BLOCK_SIZE;
		len %= SM3_BLOCK_SIZE;

		if (blocks) {
			block_fn(sctx, data, blocks);
			data += blocks * SM3_BLOCK_SIZE;
		}
		partial = 0;
	}
	if (len)
		memcpy(sctx->buffer + partial, data, len);

	return 0;
}

static inline int sm3_base_do_finalize(struct shash_desc *desc,
					sm3_block_fn *block_fn)
{
	const int bit_offset = SM3_BLOCK_SIZE - sizeof(__be64);
	struct sm3_state *sctx = shash_desc_ctx(desc);
	__be64 *bits = (__be64 *)(sctx->buffer + bit_offset);
	unsigned int partial = sctx->count % SM3_BLOCK_SIZE;

	sctx->buffer[partial++] = 0x80;
	if (partial > bit_offset) {
		memset(sctx->buffer + partial, 0x0, SM3_BLOCK_SIZE - partial);
		partial = 0;

		block_fn(sctx, sctx->buffer, 1);
	}

	memset(sctx->buffer + partial, 0x0, bit_offset - partial);
	*bits = cpu_to_be64(sctx->count << 3);
	block_fn(sctx, sctx->buffer, 1);

	return 0;
}

static inline int sm3_base_finish(struct shash_desc *desc, u8 *out)
{
	struct sm3_state *sctx = shash_desc_ctx(desc);
	__be32 *digest = (__be32 *)out;
	int i;

	for (i = 0; i < SM3_DIGEST_SIZE / sizeof(__be32); i++)
		put_unaligned_be32(sctx->state[i], digest++);

	*sctx = (struct sm3_state){};
	return 0;
}
