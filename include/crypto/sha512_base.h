/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sha512_base.h - core logic for SHA-512 implementations
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#ifndef _CRYPTO_SHA512_BASE_H
#define _CRYPTO_SHA512_BASE_H

#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <linux/compiler.h>
#include <linux/math.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>

typedef void (sha512_block_fn)(struct sha512_state *sst, u8 const *src,
			       int blocks);

static inline int sha384_base_init(struct shash_desc *desc)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA384_H0;
	sctx->state[1] = SHA384_H1;
	sctx->state[2] = SHA384_H2;
	sctx->state[3] = SHA384_H3;
	sctx->state[4] = SHA384_H4;
	sctx->state[5] = SHA384_H5;
	sctx->state[6] = SHA384_H6;
	sctx->state[7] = SHA384_H7;
	sctx->count[0] = sctx->count[1] = 0;

	return 0;
}

static inline int sha512_base_init(struct shash_desc *desc)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA512_H0;
	sctx->state[1] = SHA512_H1;
	sctx->state[2] = SHA512_H2;
	sctx->state[3] = SHA512_H3;
	sctx->state[4] = SHA512_H4;
	sctx->state[5] = SHA512_H5;
	sctx->state[6] = SHA512_H6;
	sctx->state[7] = SHA512_H7;
	sctx->count[0] = sctx->count[1] = 0;

	return 0;
}

static inline int sha512_base_do_update_blocks(struct shash_desc *desc,
					       const u8 *data,
					       unsigned int len,
					       sha512_block_fn *block_fn)
{
	unsigned int remain = len - round_down(len, SHA512_BLOCK_SIZE);
	struct sha512_state *sctx = shash_desc_ctx(desc);

	len -= remain;
	sctx->count[0] += len;
	if (sctx->count[0] < len)
		sctx->count[1]++;
	block_fn(sctx, data, len / SHA512_BLOCK_SIZE);
	return remain;
}

static inline int sha512_base_do_finup(struct shash_desc *desc, const u8 *src,
				       unsigned int len,
				       sha512_block_fn *block_fn)
{
	unsigned int bit_offset = SHA512_BLOCK_SIZE / 8 - 2;
	struct sha512_state *sctx = shash_desc_ctx(desc);
	union {
		__be64 b64[SHA512_BLOCK_SIZE / 4];
		u8 u8[SHA512_BLOCK_SIZE * 2];
	} block = {};

	if (len >= SHA512_BLOCK_SIZE) {
		int remain;

		remain = sha512_base_do_update_blocks(desc, src, len, block_fn);
		src += len - remain;
		len = remain;
	}

	if (len >= bit_offset * 8)
		bit_offset += SHA512_BLOCK_SIZE / 8;
	memcpy(&block, src, len);
	block.u8[len] = 0x80;
	sctx->count[0] += len;
	block.b64[bit_offset] = cpu_to_be64(sctx->count[1] << 3 |
					    sctx->count[0] >> 61);
	block.b64[bit_offset + 1] = cpu_to_be64(sctx->count[0] << 3);
	block_fn(sctx, block.u8, (bit_offset + 2) * 8 / SHA512_BLOCK_SIZE);
	memzero_explicit(&block, sizeof(block));

	return 0;
}

static inline int sha512_base_finish(struct shash_desc *desc, u8 *out)
{
	unsigned int digest_size = crypto_shash_digestsize(desc->tfm);
	struct sha512_state *sctx = shash_desc_ctx(desc);
	__be64 *digest = (__be64 *)out;
	int i;

	for (i = 0; digest_size > 0; i++, digest_size -= sizeof(__be64))
		put_unaligned_be64(sctx->state[i], digest++);
	return 0;
}

void sha512_generic_block_fn(struct sha512_state *sst, u8 const *src,
			     int blocks);

#endif /* _CRYPTO_SHA512_BASE_H */
