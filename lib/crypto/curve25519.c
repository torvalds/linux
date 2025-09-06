// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This is an implementation of the Curve25519 ECDH algorithm, using either
 * a 32-bit implementation or a 64-bit implementation with 128-bit integers,
 * depending on what is supported by the target compiler.
 *
 * Information: https://cr.yp.to/ecdh.html
 */

#include <crypto/curve25519.h>
#include <crypto/utils.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/module.h>

bool __must_check
curve25519(u8 mypublic[CURVE25519_KEY_SIZE],
	   const u8 secret[CURVE25519_KEY_SIZE],
	   const u8 basepoint[CURVE25519_KEY_SIZE])
{
	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_CURVE25519))
		curve25519_arch(mypublic, secret, basepoint);
	else
		curve25519_generic(mypublic, secret, basepoint);
	return crypto_memneq(mypublic, curve25519_null_point,
			     CURVE25519_KEY_SIZE);
}
EXPORT_SYMBOL(curve25519);

bool __must_check
curve25519_generate_public(u8 pub[CURVE25519_KEY_SIZE],
			   const u8 secret[CURVE25519_KEY_SIZE])
{
	if (unlikely(!crypto_memneq(secret, curve25519_null_point,
				    CURVE25519_KEY_SIZE)))
		return false;

	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_CURVE25519))
		curve25519_base_arch(pub, secret);
	else
		curve25519_generic(pub, secret, curve25519_base_point);
	return crypto_memneq(pub, curve25519_null_point, CURVE25519_KEY_SIZE);
}
EXPORT_SYMBOL(curve25519_generate_public);

static int __init curve25519_init(void)
{
	return 0;
}

static void __exit curve25519_exit(void)
{
}

module_init(curve25519_init);
module_exit(curve25519_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Curve25519 scalar multiplication");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
