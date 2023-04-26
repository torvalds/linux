/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RNG: Random Number Generator  algorithms under the crypto API
 *
 * Copyright (c) 2008 Neil Horman <nhorman@tuxdriver.com>
 * Copyright (c) 2015 Herbert Xu <herbert@gondor.apana.org.au>
 */

#ifndef _CRYPTO_RNG_H
#define _CRYPTO_RNG_H

#include <linux/atomic.h>
#include <linux/container_of.h>
#include <linux/crypto.h>

struct crypto_rng;

/*
 * struct crypto_istat_rng: statistics for RNG algorithm
 * @generate_cnt:	number of RNG generate requests
 * @generate_tlen:	total data size of generated data by the RNG
 * @seed_cnt:		number of times the RNG was seeded
 * @err_cnt:		number of error for RNG requests
 */
struct crypto_istat_rng {
	atomic64_t generate_cnt;
	atomic64_t generate_tlen;
	atomic64_t seed_cnt;
	atomic64_t err_cnt;
};

/**
 * struct rng_alg - random number generator definition
 *
 * @generate:	The function defined by this variable obtains a
 *		random number. The random number generator transform
 *		must generate the random number out of the context
 *		provided with this call, plus any additional data
 *		if provided to the call.
 * @seed:	Seed or reseed the random number generator.  With the
 *		invocation of this function call, the random number
 *		generator shall become ready for generation.  If the
 *		random number generator requires a seed for setting
 *		up a new state, the seed must be provided by the
 *		consumer while invoking this function. The required
 *		size of the seed is defined with @seedsize .
 * @set_ent:	Set entropy that would otherwise be obtained from
 *		entropy source.  Internal use only.
 * @stat:	Statistics for rng algorithm
 * @seedsize:	The seed size required for a random number generator
 *		initialization defined with this variable. Some
 *		random number generators does not require a seed
 *		as the seeding is implemented internally without
 *		the need of support by the consumer. In this case,
 *		the seed size is set to zero.
 * @base:	Common crypto API algorithm data structure.
 */
struct rng_alg {
	int (*generate)(struct crypto_rng *tfm,
			const u8 *src, unsigned int slen,
			u8 *dst, unsigned int dlen);
	int (*seed)(struct crypto_rng *tfm, const u8 *seed, unsigned int slen);
	void (*set_ent)(struct crypto_rng *tfm, const u8 *data,
			unsigned int len);

#ifdef CONFIG_CRYPTO_STATS
	struct crypto_istat_rng stat;
#endif

	unsigned int seedsize;

	struct crypto_alg base;
};

struct crypto_rng {
	struct crypto_tfm base;
};

extern struct crypto_rng *crypto_default_rng;

int crypto_get_default_rng(void);
void crypto_put_default_rng(void);

/**
 * DOC: Random number generator API
 *
 * The random number generator API is used with the ciphers of type
 * CRYPTO_ALG_TYPE_RNG (listed as type "rng" in /proc/crypto)
 */

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
struct crypto_rng *crypto_alloc_rng(const char *alg_name, u32 type, u32 mask);

static inline struct crypto_tfm *crypto_rng_tfm(struct crypto_rng *tfm)
{
	return &tfm->base;
}

static inline struct rng_alg *__crypto_rng_alg(struct crypto_alg *alg)
{
	return container_of(alg, struct rng_alg, base);
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
	return __crypto_rng_alg(crypto_rng_tfm(tfm)->__crt_alg);
}

/**
 * crypto_free_rng() - zeroize and free RNG handle
 * @tfm: cipher handle to be freed
 *
 * If @tfm is a NULL or error pointer, this function does nothing.
 */
static inline void crypto_free_rng(struct crypto_rng *tfm)
{
	crypto_destroy_tfm(tfm, crypto_rng_tfm(tfm));
}

static inline struct crypto_istat_rng *rng_get_stat(struct rng_alg *alg)
{
#ifdef CONFIG_CRYPTO_STATS
	return &alg->stat;
#else
	return NULL;
#endif
}

static inline int crypto_rng_errstat(struct rng_alg *alg, int err)
{
	if (!IS_ENABLED(CONFIG_CRYPTO_STATS))
		return err;

	if (err && err != -EINPROGRESS && err != -EBUSY)
		atomic64_inc(&rng_get_stat(alg)->err_cnt);

	return err;
}

/**
 * crypto_rng_generate() - get random number
 * @tfm: cipher handle
 * @src: Input buffer holding additional data, may be NULL
 * @slen: Length of additional data
 * @dst: output buffer holding the random numbers
 * @dlen: length of the output buffer
 *
 * This function fills the caller-allocated buffer with random
 * numbers using the random number generator referenced by the
 * cipher handle.
 *
 * Return: 0 function was successful; < 0 if an error occurred
 */
static inline int crypto_rng_generate(struct crypto_rng *tfm,
				      const u8 *src, unsigned int slen,
				      u8 *dst, unsigned int dlen)
{
	struct rng_alg *alg = crypto_rng_alg(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_STATS)) {
		struct crypto_istat_rng *istat = rng_get_stat(alg);

		atomic64_inc(&istat->generate_cnt);
		atomic64_add(dlen, &istat->generate_tlen);
	}

	return crypto_rng_errstat(alg,
				  alg->generate(tfm, src, slen, dst, dlen));
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
 * Return: 0 function was successful; < 0 if an error occurred
 */
static inline int crypto_rng_get_bytes(struct crypto_rng *tfm,
				       u8 *rdata, unsigned int dlen)
{
	return crypto_rng_generate(tfm, NULL, 0, rdata, dlen);
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
int crypto_rng_reset(struct crypto_rng *tfm, const u8 *seed,
		     unsigned int slen);

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
