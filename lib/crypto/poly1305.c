// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Poly1305 authenticator algorithm, RFC7539
 *
 * Copyright (C) 2015 Martin Willi
 *
 * Based on public domain code by Andrew Moon and Daniel J. Bernstein.
 */

#include <crypto/internal/blockhash.h>
#include <crypto/internal/poly1305.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

void poly1305_init(struct poly1305_desc_ctx *desc,
		   const u8 key[POLY1305_KEY_SIZE])
{
	desc->s[0] = get_unaligned_le32(key + 16);
	desc->s[1] = get_unaligned_le32(key + 20);
	desc->s[2] = get_unaligned_le32(key + 24);
	desc->s[3] = get_unaligned_le32(key + 28);
	desc->buflen = 0;
	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_POLY1305))
		poly1305_block_init_arch(&desc->state, key);
	else
		poly1305_block_init_generic(&desc->state, key);
}
EXPORT_SYMBOL(poly1305_init);

static inline void poly1305_blocks(struct poly1305_block_state *state,
				   const u8 *src, unsigned int len)
{
	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_POLY1305))
		poly1305_blocks_arch(state, src, len, 1);
	else
		poly1305_blocks_generic(state, src, len, 1);
}

void poly1305_update(struct poly1305_desc_ctx *desc,
		     const u8 *src, unsigned int nbytes)
{
	desc->buflen = BLOCK_HASH_UPDATE(poly1305_blocks, &desc->state,
					 src, nbytes, POLY1305_BLOCK_SIZE,
					 desc->buf, desc->buflen);
}
EXPORT_SYMBOL(poly1305_update);

void poly1305_final(struct poly1305_desc_ctx *desc, u8 *dst)
{
	if (unlikely(desc->buflen)) {
		desc->buf[desc->buflen++] = 1;
		memset(desc->buf + desc->buflen, 0,
		       POLY1305_BLOCK_SIZE - desc->buflen);
		if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_POLY1305))
			poly1305_blocks_arch(&desc->state, desc->buf,
					     POLY1305_BLOCK_SIZE, 0);
		else
			poly1305_blocks_generic(&desc->state, desc->buf,
						POLY1305_BLOCK_SIZE, 0);
	}

	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_POLY1305))
		poly1305_emit_arch(&desc->state.h, dst, desc->s);
	else
		poly1305_emit_generic(&desc->state.h, dst, desc->s);
	*desc = (struct poly1305_desc_ctx){};
}
EXPORT_SYMBOL(poly1305_final);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("Poly1305 authenticator algorithm, RFC7539");
