/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sha512_base.h - core logic for SHA-512 implementations
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#ifndef _CRYPTO_SHA512_BASE_H
#define _CRYPTO_SHA512_BASE_H

#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#include <linux/crypto.h>
#include <linux/module.h>

#include <asm/unaligned.h>

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

static inline int sha512_base_do_update(struct shash_desc *desc,
					const u8 *data,
					unsigned int len,
					sha512_block_fn *block_fn)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);
	unsigned int partial = sctx->count[0] % SHA512_BLOCK_SIZE;

	sctx->count[0] += len;
	if (sctx->count[0] < len)
		sctx->count[1]++;

	if (unlikely((partial + len) >= SHA512_BLOCK_SIZE)) {
		int blocks;

		if (partial) {
			int p = SHA512_BLOCK_SIZE - partial;

			memcpy(sctx->buf + partial, data, p);
			data += p;
			len -= p;

			block_fn(sctx, sctx->buf, 1);
		}

		blocks = len / SHA512_BLOCK_SIZE;
		len %= SHA512_BLOCK_SIZE;

		if (blocks) {
			block_fn(sctx, data, blocks);
			data += blocks * SHA512_BLOCK_SIZE;
		}
		partial = 0;
	}
	if (len)
		memcpy(sctx->buf + partial, data, len);

	return 0;
}

static inline int sha512_base_do_finalize(struct shash_desc *desc,
					  sha512_block_fn *block_fn)
{
	const int bit_offset = SHA512_BLOCK_SIZE - sizeof(__be64[2]);
	struct sha512_state *sctx = shash_desc_ctx(desc);
	__be64 *bits = (__be64 *)(sctx->buf + bit_offset);
	unsigned int partial = sctx->count[0] % SHA512_BLOCK_SIZE;

	sctx->buf[partial++] = 0x80;
	if (partial > bit_offset) {
		memset(sctx->buf + partial, 0x0, SHA512_BLOCK_SIZE - partial);
		partial = 0;

		block_fn(sctx, sctx->buf, 1);
	}

	memset(sctx->buf + partial, 0x0, bit_offset - partial);
	bits[0] = cpu_to_be64(sctx->count[1] << 3 | sctx->count[0] >> 61);
	bits[1] = cpu_to_be64(sctx->count[0] << 3);
	block_fn(sctx, sctx->buf, 1);

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

	*sctx = (struct sha512_state){};
	return 0;
}

#endif /* _CRYPTO_SHA512_BASE_H */
