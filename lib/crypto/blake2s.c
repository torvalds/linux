// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This is an implementation of the BLAKE2s hash and PRF functions.
 *
 * Information: https://blake2.net/
 *
 */

#include <crypto/internal/blake2s.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bug.h>

void blake2s_update(struct blake2s_state *state, const u8 *in, size_t inlen)
{
	__blake2s_update(state, in, inlen, blake2s_compress);
}
EXPORT_SYMBOL(blake2s_update);

void blake2s_final(struct blake2s_state *state, u8 *out)
{
	WARN_ON(IS_ENABLED(DEBUG) && !out);
	__blake2s_final(state, out, blake2s_compress);
	memzero_explicit(state, sizeof(*state));
}
EXPORT_SYMBOL(blake2s_final);

static int __init blake2s_mod_init(void)
{
	if (!IS_ENABLED(CONFIG_CRYPTO_MANAGER_DISABLE_TESTS) &&
	    WARN_ON(!blake2s_selftest()))
		return -ENODEV;
	return 0;
}

static void __exit blake2s_mod_exit(void)
{
}

module_init(blake2s_mod_init);
module_exit(blake2s_mod_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BLAKE2s hash function");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
