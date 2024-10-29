/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sha256_base.h - core logic for SHA-256 implementations
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#ifndef _CRYPTO_SHA256_BASE_H
#define _CRYPTO_SHA256_BASE_H

#include <asm/byteorder.h>
#include <linux/unaligned.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <linux/string.h>
#include <linux/types.h>

typedef void (sha256_block_fn)(struct sha256_state *sst, u8 const *src,
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

	sctx->count += len;

	if (unlikely((partial + len) >= SHA256_BLOCK_SIZE)) {
		int blocks;

		if (partial) {
			int p = SHA256_BLOCK_SIZE - partial;

			memcpy(sctx->buf + partial, data, p);
			data += p;
			len -= p;

			block_fn(sctx, sctx->buf, 1);
		}

		blocks = len / SHA256_BLOCK_SIZE;
		len %= SHA256_BLOCK_SIZE;

		if (blocks) {
			block_fn(sctx, data, blocks);
			data += blocks * SHA256_BLOCK_SIZE;
		}
		partial = 0;
	}
	if (len)
		memcpy(sctx->buf + partial, data, len);

	return 0;
}

static inline int sha256_base_do_update(struct shash_desc *desc,
					const u8 *data,
					unsigned int len,
					sha256_block_fn *block_fn)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	return lib_sha256_base_do_update(sctx, data, len, block_fn);
}

static inline int lib_sha256_base_do_finalize(struct sha256_state *sctx,
					      sha256_block_fn *block_fn)
{
	const int bit_offset = SHA256_BLOCK_SIZE - sizeof(__be64);
	__be64 *bits = (__be64 *)(sctx->buf + bit_offset);
	unsigned int partial = sctx->count % SHA256_BLOCK_SIZE;

	sctx->buf[partial++] = 0x80;
	if (partial > bit_offset) {
		memset(sctx->buf + partial, 0x0, SHA256_BLOCK_SIZE - partial);
		partial = 0;

		block_fn(sctx, sctx->buf, 1);
	}

	memset(sctx->buf + partial, 0x0, bit_offset - partial);
	*bits = cpu_to_be64(sctx->count << 3);
	block_fn(sctx, sctx->buf, 1);

	return 0;
}

static inline int sha256_base_do_finalize(struct shash_desc *desc,
					  sha256_block_fn *block_fn)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	return lib_sha256_base_do_finalize(sctx, block_fn);
}

static inline int lib_sha256_base_finish(struct sha256_state *sctx, u8 *out,
					 unsigned int digest_size)
{
	__be32 *digest = (__be32 *)out;
	int i;

	for (i = 0; digest_size > 0; i++, digest_size -= sizeof(__be32))
		put_unaligned_be32(sctx->state[i], digest++);

	memzero_explicit(sctx, sizeof(*sctx));
	return 0;
}

static inline int sha256_base_finish(struct shash_desc *desc, u8 *out)
{
	unsigned int digest_size = crypto_shash_digestsize(desc->tfm);
	struct sha256_state *sctx = shash_desc_ctx(desc);

	return lib_sha256_base_finish(sctx, out, digest_size);
}

#endif /* _CRYPTO_SHA256_BASE_H */
