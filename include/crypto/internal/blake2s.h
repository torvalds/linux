/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef BLAKE2S_INTERNAL_H
#define BLAKE2S_INTERNAL_H

#include <crypto/blake2s.h>

struct blake2s_tfm_ctx {
	u8 key[BLAKE2S_KEY_SIZE];
	unsigned int keylen;
};

void blake2s_compress_generic(struct blake2s_state *state,const u8 *block,
			      size_t nblocks, const u32 inc);

void blake2s_compress_arch(struct blake2s_state *state,const u8 *block,
			   size_t nblocks, const u32 inc);

static inline void blake2s_set_lastblock(struct blake2s_state *state)
{
	state->f[0] = -1;
}

#endif /* BLAKE2S_INTERNAL_H */
