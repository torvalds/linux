/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RNG: Random Number Generator  algorithms under the crypto API
 *
 * Copyright (c) 2008 Neil Horman <nhorman@tuxdriver.com>
 * Copyright (c) 2015 Herbert Xu <herbert@gondor.apana.org.au>
 */

#ifndef _CRYPTO_INTERNAL_RNG_H
#define _CRYPTO_INTERNAL_RNG_H

#include <crypto/algapi.h>
#include <crypto/rng.h>

int crypto_register_rng(struct rng_alg *alg);
void crypto_unregister_rng(struct rng_alg *alg);
int crypto_register_rngs(struct rng_alg *algs, int count);
void crypto_unregister_rngs(struct rng_alg *algs, int count);

#if defined(CONFIG_CRYPTO_RNG) || defined(CONFIG_CRYPTO_RNG_MODULE)
int crypto_del_default_rng(void);
#else
static inline int crypto_del_default_rng(void)
{
	return 0;
}
#endif

static inline void *crypto_rng_ctx(struct crypto_rng *tfm)
{
	return crypto_tfm_ctx(&tfm->base);
}

static inline void crypto_rng_set_entropy(struct crypto_rng *tfm,
					  const u8 *data, unsigned int len)
{
	crypto_rng_alg(tfm)->set_ent(tfm, data, len);
}

#endif
