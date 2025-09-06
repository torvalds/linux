// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This is an implementation of the Curve25519 ECDH algorithm, using either an
 * architecture-optimized implementation or a generic implementation. The
 * generic implementation is either 32-bit, or 64-bit with 128-bit integers,
 * depending on what is supported by the target compiler.
 *
 * Information: https://cr.yp.to/ecdh.html
 */

#include <crypto/curve25519.h>
#include <crypto/utils.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/module.h>

static const u8 curve25519_null_point[CURVE25519_KEY_SIZE] __aligned(32) = { 0 };
static const u8 curve25519_base_point[CURVE25519_KEY_SIZE] __aligned(32) = { 9 };

#ifdef CONFIG_CRYPTO_LIB_CURVE25519_ARCH
#include "curve25519.h" /* $(SRCARCH)/curve25519.h */
#else
static void curve25519_arch(u8 mypublic[CURVE25519_KEY_SIZE],
			    const u8 secret[CURVE25519_KEY_SIZE],
			    const u8 basepoint[CURVE25519_KEY_SIZE])
{
	curve25519_generic(mypublic, secret, basepoint);
}

static void curve25519_base_arch(u8 pub[CURVE25519_KEY_SIZE],
				 const u8 secret[CURVE25519_KEY_SIZE])
{
	curve25519_generic(pub, secret, curve25519_base_point);
}
#endif

bool __must_check
curve25519(u8 mypublic[CURVE25519_KEY_SIZE],
	   const u8 secret[CURVE25519_KEY_SIZE],
	   const u8 basepoint[CURVE25519_KEY_SIZE])
{
	curve25519_arch(mypublic, secret, basepoint);
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
	curve25519_base_arch(pub, secret);
	return crypto_memneq(pub, curve25519_null_point, CURVE25519_KEY_SIZE);
}
EXPORT_SYMBOL(curve25519_generate_public);

#ifdef curve25519_mod_init_arch
static int __init curve25519_mod_init(void)
{
	curve25519_mod_init_arch();
	return 0;
}
subsys_initcall(curve25519_mod_init);

static void __exit curve25519_mod_exit(void)
{
}
module_exit(curve25519_mod_exit);
#endif

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Curve25519 algorithm");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
