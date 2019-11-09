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
	struct des_ctx tmp;
	int err;

	err = des_expand_key(&tmp, key, DES_KEY_SIZE);
	if (err == -ENOKEY) {
		if (crypto_tfm_get_flags(tfm) & CRYPTO_TFM_REQ_FORBID_WEAK_KEYS)
			err = -EINVAL;
		else
			err = 0;
	}

	if (err)
		crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_WEAK_KEY);

	memzero_explicit(&tmp, sizeof(tmp));
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
static inline int des3_ede_verify_key(const u8 *key, unsigned int key_len,
				      bool check_weak)
{
	int ret = fips_enabled ? -EINVAL : -ENOKEY;
	u32 K[6];

	memcpy(K, key, DES3_EDE_KEY_SIZE);

	if ((!((K[0] ^ K[2]) | (K[1] ^ K[3])) ||
	     !((K[2] ^ K[4]) | (K[3] ^ K[5]))) &&
	    (fips_enabled || check_weak))
		goto bad;

	if ((!((K[0] ^ K[4]) | (K[1] ^ K[5]))) && fips_enabled)
		goto bad;

	ret = 0;
bad:
	memzero_explicit(K, DES3_EDE_KEY_SIZE);

	return ret;
}

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
	int err;

	err = des3_ede_verify_key(key, DES3_EDE_KEY_SIZE,
				  crypto_tfm_get_flags(tfm) &
				  CRYPTO_TFM_REQ_FORBID_WEAK_KEYS);
	if (err)
		crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_WEAK_KEY);
	return err;
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
