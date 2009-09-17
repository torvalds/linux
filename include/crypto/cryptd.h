/*
 * Software async crypto daemon
 */

#ifndef _CRYPTO_CRYPT_H
#define _CRYPTO_CRYPT_H

#include <linux/crypto.h>
#include <linux/kernel.h>
#include <crypto/hash.h>

struct cryptd_ablkcipher {
	struct crypto_ablkcipher base;
};

static inline struct cryptd_ablkcipher *__cryptd_ablkcipher_cast(
	struct crypto_ablkcipher *tfm)
{
	return (struct cryptd_ablkcipher *)tfm;
}

/* alg_name should be algorithm to be cryptd-ed */
struct cryptd_ablkcipher *cryptd_alloc_ablkcipher(const char *alg_name,
						  u32 type, u32 mask);
struct crypto_blkcipher *cryptd_ablkcipher_child(struct cryptd_ablkcipher *tfm);
void cryptd_free_ablkcipher(struct cryptd_ablkcipher *tfm);

struct cryptd_ahash {
	struct crypto_ahash base;
};

static inline struct cryptd_ahash *__cryptd_ahash_cast(
	struct crypto_ahash *tfm)
{
	return (struct cryptd_ahash *)tfm;
}

/* alg_name should be algorithm to be cryptd-ed */
struct cryptd_ahash *cryptd_alloc_ahash(const char *alg_name,
					u32 type, u32 mask);
struct crypto_shash *cryptd_ahash_child(struct cryptd_ahash *tfm);
void cryptd_free_ahash(struct cryptd_ahash *tfm);

#endif
