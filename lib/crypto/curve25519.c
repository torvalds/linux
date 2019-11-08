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
#include <linux/module.h>
#include <linux/init.h>

const u8 curve25519_null_point[CURVE25519_KEY_SIZE] __aligned(32) = { 0 };
const u8 curve25519_base_point[CURVE25519_KEY_SIZE] __aligned(32) = { 9 };

EXPORT_SYMBOL(curve25519_null_point);
EXPORT_SYMBOL(curve25519_base_point);
EXPORT_SYMBOL(curve25519_generic);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Curve25519 scalar multiplication");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
