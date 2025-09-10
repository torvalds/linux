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

void poly1305_block_init_generic(struct poly1305_block_state *desc,
				 const u8 raw_key[POLY1305_BLOCK_SIZE])
{
	poly1305_core_init(&desc->h);
	poly1305_core_setkey(&desc->core_r, raw_key);
}
EXPORT_SYMBOL_GPL(poly1305_block_init_generic);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("Poly1305 algorithm (generic implementation)");
