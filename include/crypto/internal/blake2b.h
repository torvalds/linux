/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Helper functions for BLAKE2b implementations.
 * Keep this in sync with the corresponding BLAKE2s header.
 */

#ifndef _CRYPTO_INTERNAL_BLAKE2B_H
#define _CRYPTO_INTERNAL_BLAKE2B_H

#include <asm/byteorder.h>
#include <crypto/blake2b.h>
#include <crypto/internal/hash.h>
#include <linux/array_size.h>
#include <linux/compiler.h>
#include <linux/build_bug.h>
#include <linux/errno.h>
#include <linux/math.h>
#include <linux/string.h>
#include <linux/types.h>

static inline void blake2b_set_lastblock(struct blake2b_state *state)
{
	state->f[0] = -1;
	state->f[1] = 0;
}

static inline void blake2b_set_nonlast(struct blake2b_state *state)
{
	state->f[0] = 0;
	state->f[1] = 0;
}

typedef void (*blake2b_compress_t)(struct blake2b_state *state,
				   const u8 *block, size_t nblocks, u32 inc);

/* Helper functions for shash implementations of BLAKE2b */

struct blake2b_tfm_ctx {
	u8 key[BLAKE2B_BLOCK_SIZE];
	unsigned int keylen;
};

static inline int crypto_blake2b_setkey(struct crypto_shash *tfm,
					const u8 *key, unsigned int keylen)
{
	struct blake2b_tfm_ctx *tctx = crypto_shash_ctx(tfm);

	if (keylen > BLAKE2B_KEY_SIZE)
		return -EINVAL;

	BUILD_BUG_ON(BLAKE2B_KEY_SIZE > BLAKE2B_BLOCK_SIZE);

	memcpy(tctx->key, key, keylen);
	memset(tctx->key + keylen, 0, BLAKE2B_BLOCK_SIZE - keylen);
	tctx->keylen = keylen;

	return 0;
}

static inline int crypto_blake2b_init(struct shash_desc *desc)
{
	const struct blake2b_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct blake2b_state *state = shash_desc_ctx(desc);
	unsigned int outlen = crypto_shash_digestsize(desc->tfm);

	__blake2b_init(state, outlen, tctx->keylen);
	return tctx->keylen ?
	       crypto_shash_update(desc, tctx->key, BLAKE2B_BLOCK_SIZE) : 0;
}

static inline int crypto_blake2b_update_bo(struct shash_desc *desc,
					   const u8 *in, unsigned int inlen,
					   blake2b_compress_t compress)
{
	struct blake2b_state *state = shash_desc_ctx(desc);

	blake2b_set_nonlast(state);
	compress(state, in, inlen / BLAKE2B_BLOCK_SIZE, BLAKE2B_BLOCK_SIZE);
	return inlen - round_down(inlen, BLAKE2B_BLOCK_SIZE);
}

static inline int crypto_blake2b_finup(struct shash_desc *desc, const u8 *in,
				       unsigned int inlen, u8 *out,
				       blake2b_compress_t compress)
{
	struct blake2b_state *state = shash_desc_ctx(desc);
	u8 buf[BLAKE2B_BLOCK_SIZE];
	int i;

	memcpy(buf, in, inlen);
	memset(buf + inlen, 0, BLAKE2B_BLOCK_SIZE - inlen);
	blake2b_set_lastblock(state);
	compress(state, buf, 1, inlen);
	for (i = 0; i < ARRAY_SIZE(state->h); i++)
		__cpu_to_le64s(&state->h[i]);
	memcpy(out, state->h, crypto_shash_digestsize(desc->tfm));
	memzero_explicit(buf, sizeof(buf));
	return 0;
}

#endif /* _CRYPTO_INTERNAL_BLAKE2B_H */
