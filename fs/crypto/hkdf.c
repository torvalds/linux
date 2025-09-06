// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of HKDF ("HMAC-based Extract-and-Expand Key Derivation
 * Function"), aka RFC 5869.  See also the original paper (Krawczyk 2010):
 * "Cryptographic Extraction and Key Derivation: The HKDF Scheme".
 *
 * This is used to derive keys from the fscrypt master keys (or from the
 * "software secrets" which hardware derives from the fscrypt master keys, in
 * the case that the fscrypt master keys are hardware-wrapped keys).
 *
 * Copyright 2019 Google LLC
 */

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
 * Compute HKDF-Extract using 'master_key' as the input keying material, and
 * prepare the resulting HMAC key in 'hkdf'.  Afterwards, 'hkdf' can be used for
 * HKDF-Expand many times without having to recompute HKDF-Extract each time.
 */
void fscrypt_init_hkdf(struct hmac_sha512_key *hkdf, const u8 *master_key,
		       unsigned int master_key_size)
{
	static const u8 default_salt[HKDF_HASHLEN];
	u8 prk[HKDF_HASHLEN];

	hmac_sha512_usingrawkey(default_salt, sizeof(default_salt),
				master_key, master_key_size, prk);
	hmac_sha512_preparekey(hkdf, prk, sizeof(prk));
	memzero_explicit(prk, sizeof(prk));
}

/*
 * HKDF-Expand (RFC 5869 section 2.3).  Expand the HMAC key 'hkdf' into 'okmlen'
 * bytes of output keying material parameterized by the application-specific
 * 'info' of length 'infolen' bytes, prefixed by "fscrypt\0" and the 'context'
 * byte.  This is thread-safe and may be called by multiple threads in parallel.
 *
 * ('context' isn't part of the HKDF specification; it's just a prefix fscrypt
 * adds to its application-specific info strings to guarantee that it doesn't
 * accidentally repeat an info string when using HKDF for different purposes.)
 */
void fscrypt_hkdf_expand(const struct hmac_sha512_key *hkdf, u8 context,
			 const u8 *info, unsigned int infolen,
			 u8 *okm, unsigned int okmlen)
{
	struct hmac_sha512_ctx ctx;
	u8 counter = 1;
	u8 tmp[HKDF_HASHLEN];

	WARN_ON_ONCE(okmlen > 255 * HKDF_HASHLEN);

	for (unsigned int i = 0; i < okmlen; i += HKDF_HASHLEN) {
		hmac_sha512_init(&ctx, hkdf);
		if (i != 0)
			hmac_sha512_update(&ctx, &okm[i - HKDF_HASHLEN],
					   HKDF_HASHLEN);
		hmac_sha512_update(&ctx, "fscrypt\0", 8);
		hmac_sha512_update(&ctx, &context, 1);
		hmac_sha512_update(&ctx, info, infolen);
		hmac_sha512_update(&ctx, &counter, 1);
		if (okmlen - i < HKDF_HASHLEN) {
			hmac_sha512_final(&ctx, tmp);
			memcpy(&okm[i], tmp, okmlen - i);
			memzero_explicit(tmp, sizeof(tmp));
		} else {
			hmac_sha512_final(&ctx, &okm[i]);
		}
		counter++;
	}
}
