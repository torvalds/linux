/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * DES & Triple DES EDE Cipher Algorithms.
 */

#ifndef __CRYPTO_DES_H
#define __CRYPTO_DES_H

#include <crypto/skcipher.h>
#include <linux/compiler.h>
#include <linux/fips.h>
#include <linux/string.h>

#define DES_KEY_SIZE		8
#define DES_EXPKEY_WORDS	32
#define DES_BLOCK_SIZE		8

#define DES3_EDE_KEY_SIZE	(3 * DES_KEY_SIZE)
#define DES3_EDE_EXPKEY_WORDS	(3 * DES_EXPKEY_WORDS)
#define DES3_EDE_BLOCK_SIZE	DES_BLOCK_SIZE

static inline int __des3_verify_key(u32 *flags, const u8 *key)
{
	int err = -EINVAL;
	u32 K[6];

	memcpy(K, key, DES3_EDE_KEY_SIZE);

	if (unlikely(!((K[0] ^ K[2]) | (K[1] ^ K[3])) ||
		     !((K[2] ^ K[4]) | (K[3] ^ K[5]))) &&
		     (fips_enabled ||
		      (*flags & CRYPTO_TFM_REQ_FORBID_WEAK_KEYS)))
		goto bad;

	if (unlikely(!((K[0] ^ K[4]) | (K[1] ^ K[5]))) && fips_enabled)
		goto bad;

	err = 0;

out:
	memzero_explicit(K, DES3_EDE_KEY_SIZE);

	return err;

bad:
	*flags |= CRYPTO_TFM_RES_WEAK_KEY;
	goto out;
}

static inline int des3_verify_key(struct crypto_skcipher *tfm, const u8 *key)
{
	u32 flags;
	int err;

	flags = crypto_skcipher_get_flags(tfm);
	err = __des3_verify_key(&flags, key);
	crypto_skcipher_set_flags(tfm, flags);
	return err;
}

extern unsigned long des_ekey(u32 *pe, const u8 *k);

extern int __des3_ede_setkey(u32 *expkey, u32 *flags, const u8 *key,
			     unsigned int keylen);

#endif /* __CRYPTO_DES_H */
