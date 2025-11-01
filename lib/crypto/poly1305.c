// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Poly1305 authenticator algorithm, RFC7539
 *
 * Copyright (C) 2015 Martin Willi
 *
 * Based on public domain code by Andrew Moon and Daniel J. Bernstein.
 */

#include <crypto/internal/poly1305.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#ifdef CONFIG_CRYPTO_LIB_POLY1305_ARCH
#include "poly1305.h" /* $(SRCARCH)/poly1305.h */
#else
#define poly1305_block_init	poly1305_block_init_generic
#define poly1305_blocks		poly1305_blocks_generic
#define poly1305_emit		poly1305_emit_generic
#endif

void poly1305_init(struct poly1305_desc_ctx *desc,
		   const u8 key[POLY1305_KEY_SIZE])
{
	desc->s[0] = get_unaligned_le32(key + 16);
	desc->s[1] = get_unaligned_le32(key + 20);
	desc->s[2] = get_unaligned_le32(key + 24);
	desc->s[3] = get_unaligned_le32(key + 28);
	desc->buflen = 0;
	poly1305_block_init(&desc->state, key);
}
EXPORT_SYMBOL(poly1305_init);

void poly1305_update(struct poly1305_desc_ctx *desc,
		     const u8 *src, unsigned int nbytes)
{
	if (desc->buflen + nbytes >= POLY1305_BLOCK_SIZE) {
		unsigned int bulk_len;

		if (desc->buflen) {
			unsigned int l = POLY1305_BLOCK_SIZE - desc->buflen;

			memcpy(&desc->buf[desc->buflen], src, l);
			src += l;
			nbytes -= l;

			poly1305_blocks(&desc->state, desc->buf,
					POLY1305_BLOCK_SIZE, 1);
			desc->buflen = 0;
		}

		bulk_len = round_down(nbytes, POLY1305_BLOCK_SIZE);
		nbytes %= POLY1305_BLOCK_SIZE;

		if (bulk_len) {
			poly1305_blocks(&desc->state, src, bulk_len, 1);
			src += bulk_len;
		}
	}
	if (nbytes) {
		memcpy(&desc->buf[desc->buflen], src, nbytes);
		desc->buflen += nbytes;
	}
}
EXPORT_SYMBOL(poly1305_update);

void poly1305_final(struct poly1305_desc_ctx *desc, u8 *dst)
{
	if (unlikely(desc->buflen)) {
		desc->buf[desc->buflen++] = 1;
		memset(desc->buf + desc->buflen, 0,
		       POLY1305_BLOCK_SIZE - desc->buflen);
		poly1305_blocks(&desc->state, desc->buf, POLY1305_BLOCK_SIZE,
				0);
	}

	poly1305_emit(&desc->state.h, dst, desc->s);
	*desc = (struct poly1305_desc_ctx){};
}
EXPORT_SYMBOL(poly1305_final);

#ifdef poly1305_mod_init_arch
static int __init poly1305_mod_init(void)
{
	poly1305_mod_init_arch();
	return 0;
}
subsys_initcall(poly1305_mod_init);

static void __exit poly1305_mod_exit(void)
{
}
module_exit(poly1305_mod_exit);
#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Poly1305 authenticator algorithm, RFC7539");
