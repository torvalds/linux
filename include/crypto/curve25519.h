/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef CURVE25519_H
#define CURVE25519_H

#include <linux/types.h>
#include <linux/random.h>

enum curve25519_lengths {
	CURVE25519_KEY_SIZE = 32
};

void curve25519_generic(u8 out[CURVE25519_KEY_SIZE],
			const u8 scalar[CURVE25519_KEY_SIZE],
			const u8 point[CURVE25519_KEY_SIZE]);

bool __must_check curve25519(u8 mypublic[CURVE25519_KEY_SIZE],
			     const u8 secret[CURVE25519_KEY_SIZE],
			     const u8 basepoint[CURVE25519_KEY_SIZE]);

bool __must_check curve25519_generate_public(u8 pub[CURVE25519_KEY_SIZE],
					     const u8 secret[CURVE25519_KEY_SIZE]);

static inline void curve25519_clamp_secret(u8 secret[CURVE25519_KEY_SIZE])
{
	secret[0] &= 248;
	secret[31] = (secret[31] & 127) | 64;
}

static inline void curve25519_generate_secret(u8 secret[CURVE25519_KEY_SIZE])
{
	get_random_bytes_wait(secret, CURVE25519_KEY_SIZE);
	curve25519_clamp_secret(secret);
}

#endif /* CURVE25519_H */
