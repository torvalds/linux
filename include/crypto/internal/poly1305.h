/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for the Poly1305 algorithm
 */

#ifndef _CRYPTO_INTERNAL_POLY1305_H
#define _CRYPTO_INTERNAL_POLY1305_H

#include <asm/unaligned.h>
#include <linux/types.h>
#include <crypto/poly1305.h>

/*
 * Poly1305 core functions.  These implement the ε-almost-∆-universal hash
 * function underlying the Poly1305 MAC, i.e. they don't add an encrypted nonce
 * ("s key") at the end.  They also only support block-aligned inputs.
 */
void poly1305_core_setkey(struct poly1305_key *key, const u8 *raw_key);
static inline void poly1305_core_init(struct poly1305_state *state)
{
	*state = (struct poly1305_state){};
}

void poly1305_core_blocks(struct poly1305_state *state,
			  const struct poly1305_key *key, const void *src,
			  unsigned int nblocks, u32 hibit);
void poly1305_core_emit(const struct poly1305_state *state, void *dst);

/*
 * Poly1305 requires a unique key for each tag, which implies that we can't set
 * it on the tfm that gets accessed by multiple users simultaneously. Instead we
 * expect the key as the first 32 bytes in the update() call.
 */
static inline
unsigned int crypto_poly1305_setdesckey(struct poly1305_desc_ctx *dctx,
					const u8 *src, unsigned int srclen)
{
	if (!dctx->sset) {
		if (!dctx->rset && srclen >= POLY1305_BLOCK_SIZE) {
			poly1305_core_setkey(dctx->r, src);
			src += POLY1305_BLOCK_SIZE;
			srclen -= POLY1305_BLOCK_SIZE;
			dctx->rset = 1;
		}
		if (srclen >= POLY1305_BLOCK_SIZE) {
			dctx->s[0] = get_unaligned_le32(src +  0);
			dctx->s[1] = get_unaligned_le32(src +  4);
			dctx->s[2] = get_unaligned_le32(src +  8);
			dctx->s[3] = get_unaligned_le32(src + 12);
			src += POLY1305_BLOCK_SIZE;
			srclen -= POLY1305_BLOCK_SIZE;
			dctx->sset = true;
		}
	}
	return srclen;
}

#endif
