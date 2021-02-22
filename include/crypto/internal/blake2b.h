/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Helper functions for BLAKE2b implementations.
 * Keep this in sync with the corresponding BLAKE2s header.
 */

#ifndef _CRYPTO_INTERNAL_BLAKE2B_H
#define _CRYPTO_INTERNAL_BLAKE2B_H

#include <crypto/blake2b.h>
#include <crypto/internal/hash.h>
#include <linux/string.h>

void blake2b_compress_generic(struct blake2b_state *state,
			      const u8 *block, size_t nblocks, u32 inc);

static inline void blake2b_set_lastblock(struct blake2b_state *state)
{
	state->f[0] = -1;
}

typedef void (*blake2b_compress_t)(struct blake2b_state *state,
				   const u8 *block, size_t nblocks, u32 inc);

static inline void __blake2b_update(struct blake2b_state *state,
				    const u8 *in, size_t inlen,
				    blake2b_compress_t compress)
{
	const size_t fill = BLAKE2B_BLOCK_SIZE - state->buflen;

	if (unlikely(!inlen))
		return;
	if (inlen > fill) {
		memcpy(state->buf + state->buflen, in, fill);
		(*compress)(state, state->buf, 1, BLAKE2B_BLOCK_SIZE);
		state->buflen = 0;
		in += fill;
		inlen -= fill;
	}
	if (inlen > BLAKE2B_BLOCK_SIZE) {
		const size_t nblocks = DIV_ROUND_UP(inlen, BLAKE2B_BLOCK_SIZE);
		/* Hash one less (full) block than strictly possible */
		(*compress)(state, in, nblocks - 1, BLAKE2B_BLOCK_SIZE);
		in += BLAKE2B_BLOCK_SIZE * (nblocks - 1);
		inlen -= BLAKE2B_BLOCK_SIZE * (nblocks - 1);
	}
	memcpy(state->buf + state->buflen, in, inlen);
	state->buflen += inlen;
}

static inline void __blake2b_final(struct blake2b_state *state, u8 *out,
				   blake2b_compress_t compress)
{
	int i;

	blake2b_set_lastblock(state);
	memset(state->buf + state->buflen, 0,
	       BLAKE2B_BLOCK_SIZE - state->buflen); /* Padding */
	(*compress)(state, state->buf, 1, state->buflen);
	for (i = 0; i < ARRAY_SIZE(state->h); i++)
		__cpu_to_le64s(&state->h[i]);
	memcpy(out, state->h, state->outlen);
}

/* Helper functions for shash implementations of BLAKE2b */

struct blake2b_tfm_ctx {
	u8 key[BLAKE2B_KEY_SIZE];
	unsigned int keylen;
};

static inline int crypto_blake2b_setkey(struct crypto_shash *tfm,
					const u8 *key, unsigned int keylen)
{
	struct blake2b_tfm_ctx *tctx = crypto_shash_ctx(tfm);

	if (keylen == 0 || keylen > BLAKE2B_KEY_SIZE)
		return -EINVAL;

	memcpy(tctx->key, key, keylen);
	tctx->keylen = keylen;

	return 0;
}

static inline int crypto_blake2b_init(struct shash_desc *desc)
{
	const struct blake2b_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct blake2b_state *state = shash_desc_ctx(desc);
	unsigned int outlen = crypto_shash_digestsize(desc->tfm);

	__blake2b_init(state, outlen, tctx->key, tctx->keylen);
	return 0;
}

static inline int crypto_blake2b_update(struct shash_desc *desc,
					const u8 *in, unsigned int inlen,
					blake2b_compress_t compress)
{
	struct blake2b_state *state = shash_desc_ctx(desc);

	__blake2b_update(state, in, inlen, compress);
	return 0;
}

static inline int crypto_blake2b_final(struct shash_desc *desc, u8 *out,
				       blake2b_compress_t compress)
{
	struct blake2b_state *state = shash_desc_ctx(desc);

	__blake2b_final(state, out, compress);
	return 0;
}

#endif /* _CRYPTO_INTERNAL_BLAKE2B_H */
