/*
 * RNG: Random Number Generator  algorithms under the crypto API
 *
 * Copyright (c) 2008 Neil Horman <nhorman@tuxdriver.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef _CRYPTO_INTERNAL_RNG_H
#define _CRYPTO_INTERNAL_RNG_H

#include <crypto/algapi.h>
#include <crypto/rng.h>

extern const struct crypto_type crypto_rng_type;

static inline void *crypto_rng_ctx(struct crypto_rng *tfm)
{
	return crypto_tfm_ctx(&tfm->base);
}

#endif
