/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for the Poly1305 algorithm
 */

#ifndef _CRYPTO_INTERNAL_POLY1305_H
#define _CRYPTO_INTERNAL_POLY1305_H

#include <crypto/poly1305.h>
#include <linux/types.h>

/*
 * Poly1305 core functions.  These only accept whole blocks; the caller must
 * handle any needed block buffering and padding.  'hibit' must be 1 for any
 * full blocks, or 0 for the final block if it had to be padded.  If 'nonce' is
 * non-NULL, then it's added at the end to compute the Poly1305 MAC.  Otherwise,
 * only the ε-almost-∆-universal hash function (not the full MAC) is computed.
 */

void poly1305_core_setkey(struct poly1305_core_key *key,
			  const u8 raw_key[POLY1305_BLOCK_SIZE]);
static inline void poly1305_core_init(struct poly1305_state *state)
{
	*state = (struct poly1305_state){};
}

void poly1305_core_blocks(struct poly1305_state *state,
			  const struct poly1305_core_key *key, const void *src,
			  unsigned int nblocks, u32 hibit);
void poly1305_core_emit(const struct poly1305_state *state, const u32 nonce[4],
			void *dst);

static inline void
poly1305_block_init_generic(struct poly1305_block_state *desc,
			    const u8 raw_key[POLY1305_BLOCK_SIZE])
{
	poly1305_core_init(&desc->h);
	poly1305_core_setkey(&desc->core_r, raw_key);
}

static inline void poly1305_blocks_generic(struct poly1305_block_state *state,
					   const u8 *src, unsigned int len,
					   u32 padbit)
{
	poly1305_core_blocks(&state->h, &state->core_r, src,
			     len / POLY1305_BLOCK_SIZE, padbit);
}

static inline void poly1305_emit_generic(const struct poly1305_state *state,
					 u8 digest[POLY1305_DIGEST_SIZE],
					 const u32 nonce[4])
{
	poly1305_core_emit(state, nonce, digest);
}

#endif
