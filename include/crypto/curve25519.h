/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef CURVE25519_H
#define CURVE25519_H

#include <crypto/algapi.h> // For crypto_memneq.
#include <linux/types.h>
#include <linux/random.h>

enum curve25519_lengths {
	CURVE25519_KEY_SIZE = 32
};

extern const u8 curve25519_null_point[];
extern const u8 curve25519_base_point[];

void curve25519_generic(u8 out[CURVE25519_KEY_SIZE],
			const u8 scalar[CURVE25519_KEY_SIZE],
			const u8 point[CURVE25519_KEY_SIZE]);

void curve25519_arch(u8 out[CURVE25519_KEY_SIZE],
		     const u8 scalar[CURVE25519_KEY_SIZE],
		     const u8 point[CURVE25519_KEY_SIZE]);

void curve25519_base_arch(u8 pub[CURVE25519_KEY_SIZE],
			  const u8 secret[CURVE25519_KEY_SIZE]);

static inline
bool __must_check curve25519(u8 mypublic[CURVE25519_KEY_SIZE],
			     const u8 secret[CURVE25519_KEY_SIZE],
			     const u8 basepoint[CURVE25519_KEY_SIZE])
{
	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_CURVE25519) &&
	    (!IS_ENABLED(CONFIG_CRYPTO_CURVE25519_X86) || IS_ENABLED(CONFIG_AS_ADX)))
		curve25519_arch(mypublic, secret, basepoint);
	else
		curve25519_generic(mypublic, secret, basepoint);
	return crypto_memneq(mypublic, curve25519_null_point,
			     CURVE25519_KEY_SIZE);
}

static inline bool
__must_check curve25519_generate_public(u8 pub[CURVE25519_KEY_SIZE],
					const u8 secret[CURVE25519_KEY_SIZE])
{
	if (unlikely(!crypto_memneq(secret, curve25519_null_point,
				    CURVE25519_KEY_SIZE)))
		return false;

	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_CURVE25519) &&
	    (!IS_ENABLED(CONFIG_CRYPTO_CURVE25519_X86) || IS_ENABLED(CONFIG_AS_ADX)))
		curve25519_base_arch(pub, secret);
	else
		curve25519_generic(pub, secret, curve25519_base_point);
	return crypto_memneq(pub, curve25519_null_point, CURVE25519_KEY_SIZE);
}

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
