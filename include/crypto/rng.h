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

#ifndef _CRYPTO_RNG_H
#define _CRYPTO_RNG_H

#include <linux/crypto.h>

extern struct crypto_rng *crypto_default_rng;

int crypto_get_default_rng(void);
void crypto_put_default_rng(void);

/**
 * DOC: Random number generator API
 *
 * The random number generator API is used with the ciphers of type
 * CRYPTO_ALG_TYPE_RNG (listed as type "rng" in /proc/crypto)
 */

static inline struct crypto_rng *__crypto_rng_cast(struct crypto_tfm *tfm)
{
	return (struct crypto_rng *)tfm;
}

/**
 * crypto_alloc_rng() -- allocate RNG handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      message digest cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Allocate a cipher handle for a random number generator. The returned struct
 * crypto_rng is the cipher handle that is required for any subsequent
 * API invocation for that random number generator.
 *
 * For all random number generators, this call creates a new private copy of
 * the random number generator that does not share a state with other
 * instances. The only exception is the "krng" random number generator which
 * is a kernel crypto API use case for the get_random_bytes() function of the
 * /dev/random driver.
 *
 * Return: allocated cipher handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
static inline struct crypto_rng *crypto_alloc_rng(const char *alg_name,
						  u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_RNG;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return __crypto_rng_cast(crypto_alloc_base(alg_name, type, mask));
}

static inline struct crypto_tfm *crypto_rng_tfm(struct crypto_rng *tfm)
{
	return &tfm->base;
}

/**
 * crypto_rng_alg - obtain name of RNG
 * @tfm: cipher handle
 *
 * Return the generic name (cra_name) of the initialized random number generator
 *
 * Return: generic name string
 */
static inline struct rng_alg *crypto_rng_alg(struct crypto_rng *tfm)
{
	return &crypto_rng_tfm(tfm)->__crt_alg->cra_rng;
}

static inline struct rng_tfm *crypto_rng_crt(struct crypto_rng *tfm)
{
	return &crypto_rng_tfm(tfm)->crt_rng;
}

/**
 * crypto_free_rng() - zeroize and free RNG handle
 * @tfm: cipher handle to be freed
 */
static inline void crypto_free_rng(struct crypto_rng *tfm)
{
	crypto_free_tfm(crypto_rng_tfm(tfm));
}

/**
 * crypto_rng_get_bytes() - get random number
 * @tfm: cipher handle
 * @rdata: output buffer holding the random numbers
 * @dlen: length of the output buffer
 *
 * This function fills the caller-allocated buffer with random numbers using the
 * random number generator referenced by the cipher handle.
 *
 * Return: > 0 function was successful and returns the number of generated
 *	   bytes; < 0 if an error occurred
 */
static inline int crypto_rng_get_bytes(struct crypto_rng *tfm,
				       u8 *rdata, unsigned int dlen)
{
	return crypto_rng_crt(tfm)->rng_gen_random(tfm, rdata, dlen);
}

/**
 * crypto_rng_reset() - re-initialize the RNG
 * @tfm: cipher handle
 * @seed: seed input data
 * @slen: length of the seed input data
 *
 * The reset function completely re-initializes the random number generator
 * referenced by the cipher handle by clearing the current state. The new state
 * is initialized with the caller provided seed or automatically, depending
 * on the random number generator type (the ANSI X9.31 RNG requires
 * caller-provided seed, the SP800-90A DRBGs perform an automatic seeding).
 * The seed is provided as a parameter to this function call. The provided seed
 * should have the length of the seed size defined for the random number
 * generator as defined by crypto_rng_seedsize.
 *
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
static inline int crypto_rng_reset(struct crypto_rng *tfm,
				   u8 *seed, unsigned int slen)
{
	return crypto_rng_crt(tfm)->rng_reset(tfm, seed, slen);
}

/**
 * crypto_rng_seedsize() - obtain seed size of RNG
 * @tfm: cipher handle
 *
 * The function returns the seed size for the random number generator
 * referenced by the cipher handle. This value may be zero if the random
 * number generator does not implement or require a reseeding. For example,
 * the SP800-90A DRBGs implement an automated reseeding after reaching a
 * pre-defined threshold.
 *
 * Return: seed size for the random number generator
 */
static inline int crypto_rng_seedsize(struct crypto_rng *tfm)
{
	return crypto_rng_alg(tfm)->seedsize;
}

#endif
