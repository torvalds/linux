/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CRYPTO_XTS_H
#define _CRYPTO_XTS_H

#include <crypto/b128ops.h>
#include <crypto/internal/skcipher.h>
#include <linux/fips.h>

#define XTS_BLOCK_SIZE 16

static inline int xts_check_key(struct crypto_tfm *tfm,
				const u8 *key, unsigned int keylen)
{
	/*
	 * key consists of keys of equal size concatenated, therefore
	 * the length must be even.
	 */
	if (keylen % 2)
		return -EINVAL;

	/* ensure that the AES and tweak key are not identical */
	if (fips_enabled && !crypto_memneq(key, key + (keylen / 2), keylen / 2))
		return -EINVAL;

	return 0;
}

static inline int xts_verify_key(struct crypto_skcipher *tfm,
				 const u8 *key, unsigned int keylen)
{
	/*
	 * key consists of keys of equal size concatenated, therefore
	 * the length must be even.
	 */
	if (keylen % 2)
		return -EINVAL;

	/*
	 * In FIPS mode only a combined key length of either 256 or
	 * 512 bits is allowed, c.f. FIPS 140-3 IG C.I.
	 */
	if (fips_enabled && keylen != 32 && keylen != 64)
		return -EINVAL;

	/* ensure that the AES and tweak key are not identical */
	if ((fips_enabled || (crypto_skcipher_get_flags(tfm) &
			      CRYPTO_TFM_REQ_FORBID_WEAK_KEYS)) &&
	    !crypto_memneq(key, key + (keylen / 2), keylen / 2))
		return -EINVAL;

	return 0;
}

#endif  /* _CRYPTO_XTS_H */
