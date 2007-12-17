/*
 * Symmetric key ciphers.
 * 
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#ifndef _CRYPTO_INTERNAL_SKCIPHER_H
#define _CRYPTO_INTERNAL_SKCIPHER_H

#include <crypto/algapi.h>

struct crypto_skcipher_spawn {
	struct crypto_spawn base;
};

static inline void crypto_set_skcipher_spawn(
	struct crypto_skcipher_spawn *spawn, struct crypto_instance *inst)
{
	crypto_set_spawn(&spawn->base, inst);
}

int crypto_grab_skcipher(struct crypto_skcipher_spawn *spawn, const char *name,
			 u32 type, u32 mask);

static inline void crypto_drop_skcipher(struct crypto_skcipher_spawn *spawn)
{
	crypto_drop_spawn(&spawn->base);
}

static inline struct crypto_alg *crypto_skcipher_spawn_alg(
	struct crypto_skcipher_spawn *spawn)
{
	return spawn->base.alg;
}

static inline struct crypto_ablkcipher *crypto_spawn_skcipher(
	struct crypto_skcipher_spawn *spawn)
{
	return __crypto_ablkcipher_cast(
		crypto_spawn_tfm(&spawn->base, crypto_skcipher_type(0),
				 crypto_skcipher_mask(0)));
}

#endif	/* _CRYPTO_INTERNAL_SKCIPHER_H */

