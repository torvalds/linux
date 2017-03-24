/*
 * Shared crypto simd helpers
 */

#ifndef _CRYPTO_INTERNAL_SIMD_H
#define _CRYPTO_INTERNAL_SIMD_H

struct simd_skcipher_alg;

struct simd_skcipher_alg *simd_skcipher_create_compat(const char *algname,
						      const char *drvname,
						      const char *basename);
struct simd_skcipher_alg *simd_skcipher_create(const char *algname,
					       const char *basename);
void simd_skcipher_free(struct simd_skcipher_alg *alg);

#endif /* _CRYPTO_INTERNAL_SIMD_H */
