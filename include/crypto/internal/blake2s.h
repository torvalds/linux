/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef BLAKE2S_INTERNAL_H
#define BLAKE2S_INTERNAL_H

#include <crypto/blake2s.h>
#include <linux/string.h>

struct blake2s_tfm_ctx {
	u8 key[BLAKE2S_KEY_SIZE];
	unsigned int keylen;
};

void blake2s_compress_generic(struct blake2s_state *state,const u8 *block,
			      size_t nblocks, const u32 inc);

void blake2s_compress_arch(struct blake2s_state *state,const u8 *block,
			   size_t nblocks, const u32 inc);

bool blake2s_selftest(void);

static inline void blake2s_set_lastblock(struct blake2s_state *state)
{
	state->f[0] = -1;
}

typedef void (*blake2s_compress_t)(struct blake2s_state *state,
				   const u8 *block, size_t nblocks, u32 inc);

static inline void __blake2s_update(struct blake2s_state *state,
				    const u8 *in, size_t inlen,
				    blake2s_compress_t compress)
{
	const size_t fill = BLAKE2S_BLOCK_SIZE - state->buflen;

	if (unlikely(!inlen))
		return;
	if (inlen > fill) {
		memcpy(state->buf + state->buflen, in, fill);
		(*compress)(state, state->buf, 1, BLAKE2S_BLOCK_SIZE);
		state->buflen = 0;
		in += fill;
		inlen -= fill;
	}
	if (inlen > BLAKE2S_BLOCK_SIZE) {
		const size_t nblocks = DIV_ROUND_UP(inlen, BLAKE2S_BLOCK_SIZE);
		/* Hash one less (full) block than strictly possible */
		(*compress)(state, in, nblocks - 1, BLAKE2S_BLOCK_SIZE);
		in += BLAKE2S_BLOCK_SIZE * (nblocks - 1);
		inlen -= BLAKE2S_BLOCK_SIZE * (nblocks - 1);
	}
	memcpy(state->buf + state->buflen, in, inlen);
	state->buflen += inlen;
}

static inline void __blake2s_final(struct blake2s_state *state, u8 *out,
				   blake2s_compress_t compress)
{
	blake2s_set_lastblock(state);
	memset(state->buf + state->buflen, 0,
	       BLAKE2S_BLOCK_SIZE - state->buflen); /* Padding */
	(*compress)(state, state->buf, 1, state->buflen);
	cpu_to_le32_array(state->h, ARRAY_SIZE(state->h));
	memcpy(out, state->h, state->outlen);
}

#endif /* BLAKE2S_INTERNAL_H */
