/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sha256_base.h - core logic for SHA-256 implementations
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#ifndef _CRYPTO_SHA256_BASE_H
#define _CRYPTO_SHA256_BASE_H

#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <linux/math.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>

typedef void (sha256_block_fn)(struct crypto_sha256_state *sst, u8 const *src,
			       int blocks);

static inline int sha224_base_init(struct shash_desc *desc)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	sha224_init(sctx);
	return 0;
}

static inline int sha256_base_init(struct shash_desc *desc)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	sha256_init(sctx);
	return 0;
}

static inline int lib_sha256_base_do_update(struct sha256_state *sctx,
					    const u8 *data,
					    unsigned int len,
					    sha256_block_fn *block_fn)
{
	unsigned int partial = sctx->count % SHA256_BLOCK_SIZE;
	struct crypto_sha256_state *state = (void *)sctx;

	sctx->count += len;

	if (unlikely((partial + len) >= SHA256_BLOCK_SIZE)) {
		int blocks;

		if (partial) {
			int p = SHA256_BLOCK_SIZE - partial;

			memcpy(sctx->buf + partial, data, p);
			data += p;
			len -= p;

			block_fn(state, sctx->buf, 1);
		}

		blocks = len / SHA256_BLOCK_SIZE;
		len %= SHA256_BLOCK_SIZE;

		if (blocks) {
			block_fn(state, data, blocks);
			data += blocks * SHA256_BLOCK_SIZE;
		}
		partial = 0;
	}
	if (len)
		memcpy(sctx->buf + partial, data, len);

	return 0;
}

static inline int lib_sha256_base_do_update_blocks(
	struct crypto_sha256_state *sctx, const u8 *data, unsigned int len,
	sha256_block_fn *block_fn)
{
	unsigned int remain = len - round_down(len, SHA256_BLOCK_SIZE);

	sctx->count += len - remain;
	block_fn(sctx, data, len / SHA256_BLOCK_SIZE);
	return remain;
}

static inline int sha256_base_do_update_blocks(
	struct shash_desc *desc, const u8 *data, unsigned int len,
	sha256_block_fn *block_fn)
{
	return lib_sha256_base_do_update_blocks(shash_desc_ctx(desc), data,
						len, block_fn);
}

static inline int lib_sha256_base_do_finup(struct crypto_sha256_state *sctx,
					   const u8 *src, unsigned int len,
					   sha256_block_fn *block_fn)
{
	unsigned int bit_offset = SHA256_BLOCK_SIZE / 8 - 1;
	union {
		__be64 b64[SHA256_BLOCK_SIZE / 4];
		u8 u8[SHA256_BLOCK_SIZE * 2];
	} block = {};

	if (len >= bit_offset * 8)
		bit_offset += SHA256_BLOCK_SIZE / 8;
	memcpy(&block, src, len);
	block.u8[len] = 0x80;
	sctx->count += len;
	block.b64[bit_offset] = cpu_to_be64(sctx->count << 3);
	block_fn(sctx, block.u8, (bit_offset + 1) * 8 / SHA256_BLOCK_SIZE);
	memzero_explicit(&block, sizeof(block));

	return 0;
}

static inline int sha256_base_do_finup(struct shash_desc *desc,
				       const u8 *src, unsigned int len,
				       sha256_block_fn *block_fn)
{
	struct crypto_sha256_state *sctx = shash_desc_ctx(desc);

	if (len >= SHA256_BLOCK_SIZE) {
		int remain;

		remain = lib_sha256_base_do_update_blocks(sctx, src, len,
							  block_fn);
		src += len - remain;
		len = remain;
	}
	return lib_sha256_base_do_finup(sctx, src, len, block_fn);
}

static inline int lib_sha256_base_do_finalize(struct sha256_state *sctx,
					      sha256_block_fn *block_fn)
{
	unsigned int partial = sctx->count % SHA256_BLOCK_SIZE;
	struct crypto_sha256_state *state = (void *)sctx;

	sctx->count -= partial;
	return lib_sha256_base_do_finup(state, sctx->buf, partial, block_fn);
}

static inline int sha256_base_do_finalize(struct shash_desc *desc,
					  sha256_block_fn *block_fn)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	return lib_sha256_base_do_finalize(sctx, block_fn);
}

static inline int __sha256_base_finish(u32 state[SHA256_DIGEST_SIZE / 4],
				       u8 *out, unsigned int digest_size)
{
	__be32 *digest = (__be32 *)out;
	int i;

	for (i = 0; digest_size > 0; i++, digest_size -= sizeof(__be32))
		put_unaligned_be32(state[i], digest++);
	return 0;
}

static inline void lib_sha256_base_finish(struct sha256_state *sctx, u8 *out,
					  unsigned int digest_size)
{
	__sha256_base_finish(sctx->state, out, digest_size);
	memzero_explicit(sctx, sizeof(*sctx));
}

static inline int sha256_base_finish(struct shash_desc *desc, u8 *out)
{
	unsigned int digest_size = crypto_shash_digestsize(desc->tfm);
	struct crypto_sha256_state *sctx = shash_desc_ctx(desc);

	return __sha256_base_finish(sctx->state, out, digest_size);
}

void sha256_transform_blocks(struct crypto_sha256_state *sst,
			     const u8 *input, int blocks);

#endif /* _CRYPTO_SHA256_BASE_H */
