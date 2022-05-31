/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Helper functions for BLAKE2s implementations.
 * Keep this in sync with the corresponding BLAKE2b header.
 */

#ifndef _CRYPTO_INTERNAL_BLAKE2S_H
#define _CRYPTO_INTERNAL_BLAKE2S_H

#include <crypto/blake2s.h>
#include <crypto/internal/hash.h>
#include <linux/string.h>

void blake2s_compress_generic(struct blake2s_state *state, const u8 *block,
			      size_t nblocks, const u32 inc);

void blake2s_compress(struct blake2s_state *state, const u8 *block,
		      size_t nblocks, const u32 inc);

bool blake2s_selftest(void);

static inline void blake2s_set_lastblock(struct blake2s_state *state)
{
	state->f[0] = -1;
}

/* Helper functions for BLAKE2s shared by the library and shash APIs */

static __always_inline void
__blake2s_update(struct blake2s_state *state, const u8 *in, size_t inlen,
		 bool force_generic)
{
	const size_t fill = BLAKE2S_BLOCK_SIZE - state->buflen;

	if (unlikely(!inlen))
		return;
	if (inlen > fill) {
		memcpy(state->buf + state->buflen, in, fill);
		if (force_generic)
			blake2s_compress_generic(state, state->buf, 1,
						 BLAKE2S_BLOCK_SIZE);
		else
			blake2s_compress(state, state->buf, 1,
					 BLAKE2S_BLOCK_SIZE);
		state->buflen = 0;
		in += fill;
		inlen -= fill;
	}
	if (inlen > BLAKE2S_BLOCK_SIZE) {
		const size_t nblocks = DIV_ROUND_UP(inlen, BLAKE2S_BLOCK_SIZE);
		/* Hash one less (full) block than strictly possible */
		if (force_generic)
			blake2s_compress_generic(state, in, nblocks - 1,
						 BLAKE2S_BLOCK_SIZE);
		else
			blake2s_compress(state, in, nblocks - 1,
					 BLAKE2S_BLOCK_SIZE);
		in += BLAKE2S_BLOCK_SIZE * (nblocks - 1);
		inlen -= BLAKE2S_BLOCK_SIZE * (nblocks - 1);
	}
	memcpy(state->buf + state->buflen, in, inlen);
	state->buflen += inlen;
}

static __always_inline void
__blake2s_final(struct blake2s_state *state, u8 *out, bool force_generic)
{
	blake2s_set_lastblock(state);
	memset(state->buf + state->buflen, 0,
	       BLAKE2S_BLOCK_SIZE - state->buflen); /* Padding */
	if (force_generic)
		blake2s_compress_generic(state, state->buf, 1, state->buflen);
	else
		blake2s_compress(state, state->buf, 1, state->buflen);
	cpu_to_le32_array(state->h, ARRAY_SIZE(state->h));
	memcpy(out, state->h, state->outlen);
}

/* Helper functions for shash implementations of BLAKE2s */

struct blake2s_tfm_ctx {
	u8 key[BLAKE2S_KEY_SIZE];
	unsigned int keylen;
};

static inline int crypto_blake2s_setkey(struct crypto_shash *tfm,
					const u8 *key, unsigned int keylen)
{
	struct blake2s_tfm_ctx *tctx = crypto_shash_ctx(tfm);

	if (keylen == 0 || keylen > BLAKE2S_KEY_SIZE)
		return -EINVAL;

	memcpy(tctx->key, key, keylen);
	tctx->keylen = keylen;

	return 0;
}

static inline int crypto_blake2s_init(struct shash_desc *desc)
{
	const struct blake2s_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct blake2s_state *state = shash_desc_ctx(desc);
	unsigned int outlen = crypto_shash_digestsize(desc->tfm);

	__blake2s_init(state, outlen, tctx->key, tctx->keylen);
	return 0;
}

static inline int crypto_blake2s_update(struct shash_desc *desc,
					const u8 *in, unsigned int inlen,
					bool force_generic)
{
	struct blake2s_state *state = shash_desc_ctx(desc);

	__blake2s_update(state, in, inlen, force_generic);
	return 0;
}

static inline int crypto_blake2s_final(struct shash_desc *desc, u8 *out,
				       bool force_generic)
{
	struct blake2s_state *state = shash_desc_ctx(desc);

	__blake2s_final(state, out, force_generic);
	return 0;
}

#endif /* _CRYPTO_INTERNAL_BLAKE2S_H */
