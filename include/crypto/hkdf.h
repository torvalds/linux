/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HKDF: HMAC-based Key Derivation Function (HKDF), RFC 5869
 *
 * Extracted from fs/crypto/hkdf.c, which has
 * Copyright 2019 Google LLC
 */

#ifndef _CRYPTO_HKDF_H
#define _CRYPTO_HKDF_H

#include <crypto/hash.h>

int hkdf_extract(struct crypto_shash *hmac_tfm, const u8 *ikm,
		 unsigned int ikmlen, const u8 *salt, unsigned int saltlen,
		 u8 *prk);
int hkdf_expand(struct crypto_shash *hmac_tfm,
		const u8 *info, unsigned int infolen,
		u8 *okm, unsigned int okmlen);
#endif
