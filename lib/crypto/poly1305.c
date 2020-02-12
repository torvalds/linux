// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Poly1305 authenticator algorithm, RFC7539
 *
 * Copyright (C) 2015 Martin Willi
 *
 * Based on public domain code by Andrew Moon and Daniel J. Bernstein.
 */

#include <crypto/internal/poly1305.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/unaligned.h>

void poly1305_init_generic(struct poly1305_desc_ctx *desc, const u8 *key)
{
	poly1305_core_setkey(&desc->core_r, key);
	desc->s[0] = get_unaligned_le32(key + 16);
	desc->s[1] = get_unaligned_le32(key + 20);
	desc->s[2] = get_unaligned_le32(key + 24);
	desc->s[3] = get_unaligned_le32(key + 28);
	poly1305_core_init(&desc->h);
	desc->buflen = 0;
	desc->sset = true;
	desc->rset = 2;
}
EXPORT_SYMBOL_GPL(poly1305_init_generic);

void poly1305_update_generic(struct poly1305_desc_ctx *desc, const u8 *src,
			     unsigned int nbytes)
{
	unsigned int bytes;

	if (unlikely(desc->buflen)) {
		bytes = min(nbytes, POLY1305_BLOCK_SIZE - desc->buflen);
		memcpy(desc->buf + desc->buflen, src, bytes);
		src += bytes;
		nbytes -= bytes;
		desc->buflen += bytes;

		if (desc->buflen == POLY1305_BLOCK_SIZE) {
			poly1305_core_blocks(&desc->h, &desc->core_r, desc->buf,
					     1, 1);
			desc->buflen = 0;
		}
	}

	if (likely(nbytes >= POLY1305_BLOCK_SIZE)) {
		poly1305_core_blocks(&desc->h, &desc->core_r, src,
				     nbytes / POLY1305_BLOCK_SIZE, 1);
		src += nbytes - (nbytes % POLY1305_BLOCK_SIZE);
		nbytes %= POLY1305_BLOCK_SIZE;
	}

	if (unlikely(nbytes)) {
		desc->buflen = nbytes;
		memcpy(desc->buf, src, nbytes);
	}
}
EXPORT_SYMBOL_GPL(poly1305_update_generic);

void poly1305_final_generic(struct poly1305_desc_ctx *desc, u8 *dst)
{
	if (unlikely(desc->buflen)) {
		desc->buf[desc->buflen++] = 1;
		memset(desc->buf + desc->buflen, 0,
		       POLY1305_BLOCK_SIZE - desc->buflen);
		poly1305_core_blocks(&desc->h, &desc->core_r, desc->buf, 1, 0);
	}

	poly1305_core_emit(&desc->h, desc->s, dst);
	*desc = (struct poly1305_desc_ctx){};
}
EXPORT_SYMBOL_GPL(poly1305_final_generic);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
