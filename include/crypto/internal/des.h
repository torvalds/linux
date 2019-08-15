/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DES & Triple DES EDE key verification helpers
 */

#ifndef __CRYPTO_INTERNAL_DES_H
#define __CRYPTO_INTERNAL_DES_H

#include <linux/crypto.h>
#include <linux/fips.h>
#include <crypto/des.h>
#include <crypto/aead.h>
#include <crypto/skcipher.h>

/**
 * crypto_des_verify_key - Check whether a DES key is weak
 * @tfm: the crypto algo
 * @key: the key buffer
 *
 * Returns -EINVAL if the key is weak and the crypto TFM does not permit weak
 * keys. Otherwise, 0 is returned.
 *
 * It is the job of the caller to ensure that the size of the key equals
 * DES_KEY_SIZE.
 */
static inline int crypto_des_verify_key(struct crypto_tfm *tfm, const u8 *key)
{
	u32 tmp[DES_EXPKEY_WORDS];
	int err = 0;

	if (!(crypto_tfm_get_flags(tfm) & CRYPTO_TFM_REQ_FORBID_WEAK_KEYS))
		return 0;

	if (!des_ekey(tmp, key)) {
		crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_WEAK_KEY);
		err = -EINVAL;
	}

	memzero_explicit(tmp, sizeof(tmp));
	return err;
}

/*
 * RFC2451:
 *
 *   For DES-EDE3, there is no known need to reject weak or
 *   complementation keys.  Any weakness is obviated by the use of
 *   multiple keys.
 *
 *   However, if the first two or last two independent 64-bit keys are
 *   equal (k1 == k2 or k2 == k3), then the DES3 operation is simply the
 *   same as DES.  Implementers MUST reject keys that exhibit this
 *   property.
 *
 */

/**
 * crypto_des3_ede_verify_key - Check whether a DES3-EDE key is weak
 * @tfm: the crypto algo
 * @key: the key buffer
 *
 * Returns -EINVAL if the key is weak and the crypto TFM does not permit weak
 * keys or when running in FIPS mode. Otherwise, 0 is returned. Note that some
 * keys are rejected in FIPS mode even if weak keys are permitted by the TFM
 * flags.
 *
 * It is the job of the caller to ensure that the size of the key equals
 * DES3_EDE_KEY_SIZE.
 */
static inline int crypto_des3_ede_verify_key(struct crypto_tfm *tfm,
					     const u8 *key)
{
	int err = -EINVAL;
	u32 K[6];

	memcpy(K, key, DES3_EDE_KEY_SIZE);

	if ((!((K[0] ^ K[2]) | (K[1] ^ K[3])) ||
	     !((K[2] ^ K[4]) | (K[3] ^ K[5]))) &&
	    (fips_enabled || (crypto_tfm_get_flags(tfm) &
		              CRYPTO_TFM_REQ_FORBID_WEAK_KEYS)))
		goto bad;

	if ((!((K[0] ^ K[4]) | (K[1] ^ K[5]))) && fips_enabled)
		goto bad;

	err = 0;
out:
	memzero_explicit(K, DES3_EDE_KEY_SIZE);
	return err;

bad:
	crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_WEAK_KEY);
	goto out;
}

static inline int verify_skcipher_des_key(struct crypto_skcipher *tfm,
					  const u8 *key)
{
	return crypto_des_verify_key(crypto_skcipher_tfm(tfm), key);
}

static inline int verify_skcipher_des3_key(struct crypto_skcipher *tfm,
					   const u8 *key)
{
	return crypto_des3_ede_verify_key(crypto_skcipher_tfm(tfm), key);
}

static inline int verify_ablkcipher_des_key(struct crypto_ablkcipher *tfm,
					    const u8 *key)
{
	return crypto_des_verify_key(crypto_ablkcipher_tfm(tfm), key);
}

static inline int verify_ablkcipher_des3_key(struct crypto_ablkcipher *tfm,
					     const u8 *key)
{
	return crypto_des3_ede_verify_key(crypto_ablkcipher_tfm(tfm), key);
}

static inline int verify_aead_des_key(struct crypto_aead *tfm, const u8 *key,
				      int keylen)
{
	if (keylen != DES_KEY_SIZE) {
		crypto_aead_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	return crypto_des_verify_key(crypto_aead_tfm(tfm), key);
}

static inline int verify_aead_des3_key(struct crypto_aead *tfm, const u8 *key,
				       int keylen)
{
	if (keylen != DES3_EDE_KEY_SIZE) {
		crypto_aead_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	return crypto_des3_ede_verify_key(crypto_aead_tfm(tfm), key);
}

#endif /* __CRYPTO_INTERNAL_DES_H */
