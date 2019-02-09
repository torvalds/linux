/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared crypto simd helpers
 */

#ifndef _CRYPTO_INTERNAL_SIMD_H
#define _CRYPTO_INTERNAL_SIMD_H

struct simd_skcipher_alg;
struct skcipher_alg;

struct simd_skcipher_alg *simd_skcipher_create_compat(const char *algname,
						      const char *drvname,
						      const char *basename);
struct simd_skcipher_alg *simd_skcipher_create(const char *algname,
					       const char *basename);
void simd_skcipher_free(struct simd_skcipher_alg *alg);

int simd_register_skciphers_compat(struct skcipher_alg *algs, int count,
				   struct simd_skcipher_alg **simd_algs);

void simd_unregister_skciphers(struct skcipher_alg *algs, int count,
			       struct simd_skcipher_alg **simd_algs);

#endif /* _CRYPTO_INTERNAL_SIMD_H */
