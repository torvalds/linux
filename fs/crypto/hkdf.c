// SPDX-License-Identifier: GPL-2.0
/*
 * This is used to derive keys from the fscrypt master keys.
 *
 * Copyright 2019 Google LLC
 */

#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <crypto/hkdf.h>

#include "fscrypt_private.h"

/*
 * HKDF supports any unkeyed cryptographic hash algorithm, but fscrypt uses
 * SHA-512 because it is well-established, secure, and reasonably efficient.
 *
 * HKDF-SHA256 was also considered, as its 256-bit security strength would be
 * sufficient here.  A 512-bit security strength is "nice to have", though.
 * Also, on 64-bit CPUs, SHA-512 is usually just as fast as SHA-256.  In the
 * common case of deriving an AES-256-XTS key (512 bits), that can result in
 * HKDF-SHA512 being much faster than HKDF-SHA256, as the longer digest size of
 * SHA-512 causes HKDF-Expand to only need to do one iteration rather than two.
 */
#define HKDF_HMAC_ALG		"hmac(sha512)"
#define HKDF_HASHLEN		SHA512_DIGEST_SIZE

/*
 * HKDF consists of two steps:
 *
 * 1. HKDF-Extract: extract a pseudorandom key of length HKDF_HASHLEN bytes from
 *    the input keying material and optional salt.
 * 2. HKDF-Expand: expand the pseudorandom key into output keying material of
 *    any length, parameterized by an application-specific info string.
 *
 * HKDF-Extract can be skipped if the input is already a pseudorandom key of
 * length HKDF_HASHLEN bytes.  However, cipher modes other than AES-256-XTS take
 * shorter keys, and we don't want to force users of those modes to provide
 * unnecessarily long master keys.  Thus fscrypt still does HKDF-Extract.  No
 * salt is used, since fscrypt master keys should already be pseudorandom and
 * there's no way to persist a random salt per master key from kernel mode.
 */

/*
 * Compute HKDF-Extract using the given master key as the input keying material,
 * and prepare an HMAC transform object keyed by the resulting pseudorandom key.
 *
 * Afterwards, the keyed HMAC transform object can be used for HKDF-Expand many
 * times without having to recompute HKDF-Extract each time.
 */
int fscrypt_init_hkdf(struct fscrypt_hkdf *hkdf, const u8 *master_key,
		      unsigned int master_key_size)
{
	struct crypto_shash *hmac_tfm;
	static const u8 default_salt[HKDF_HASHLEN];
	u8 prk[HKDF_HASHLEN];
	int err;

	hmac_tfm = crypto_alloc_shash(HKDF_HMAC_ALG, 0, 0);
	if (IS_ERR(hmac_tfm)) {
		fscrypt_err(NULL, "Error allocating " HKDF_HMAC_ALG ": %ld",
			    PTR_ERR(hmac_tfm));
		return PTR_ERR(hmac_tfm);
	}

	if (WARN_ON_ONCE(crypto_shash_digestsize(hmac_tfm) != sizeof(prk))) {
		err = -EINVAL;
		goto err_free_tfm;
	}

	err = hkdf_extract(hmac_tfm, master_key, master_key_size,
			   default_salt, HKDF_HASHLEN, prk);
	if (err)
		goto err_free_tfm;

	err = crypto_shash_setkey(hmac_tfm, prk, sizeof(prk));
	if (err)
		goto err_free_tfm;

	hkdf->hmac_tfm = hmac_tfm;
	goto out;

err_free_tfm:
	crypto_free_shash(hmac_tfm);
out:
	memzero_explicit(prk, sizeof(prk));
	return err;
}

/*
 * HKDF-Expand (RFC 5869 section 2.3).  This expands the pseudorandom key, which
 * was already keyed into 'hkdf->hmac_tfm' by fscrypt_init_hkdf(), into 'okmlen'
 * bytes of output keying material parameterized by the application-specific
 * 'info' of length 'infolen' bytes, prefixed by "fscrypt\0" and the 'context'
 * byte.  This is thread-safe and may be called by multiple threads in parallel.
 *
 * ('context' isn't part of the HKDF specification; it's just a prefix fscrypt
 * adds to its application-specific info strings to guarantee that it doesn't
 * accidentally repeat an info string when using HKDF for different purposes.)
 */
int fscrypt_hkdf_expand(const struct fscrypt_hkdf *hkdf, u8 context,
			const u8 *info, unsigned int infolen,
			u8 *okm, unsigned int okmlen)
{
	SHASH_DESC_ON_STACK(desc, hkdf->hmac_tfm);
	u8 *full_info;
	int err;

	full_info = kzalloc(infolen + 9, GFP_KERNEL);
	if (!full_info)
		return -ENOMEM;
	desc->tfm = hkdf->hmac_tfm;

	memcpy(full_info, "fscrypt\0", 8);
	full_info[8] = context;
	memcpy(full_info + 9, info, infolen);

	err = hkdf_expand(hkdf->hmac_tfm, full_info, infolen + 9,
			  okm, okmlen);
	kfree_sensitive(full_info);
	return err;
}

void fscrypt_destroy_hkdf(struct fscrypt_hkdf *hkdf)
{
	crypto_free_shash(hkdf->hmac_tfm);
}
