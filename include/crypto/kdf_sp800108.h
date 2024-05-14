/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (C) 2021, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _CRYPTO_KDF108_H
#define _CRYPTO_KDF108_H

#include <crypto/hash.h>
#include <linux/uio.h>

/**
 * Counter KDF generate operation according to SP800-108 section 5.1
 * as well as SP800-56A section 5.8.1 (Single-step KDF).
 *
 * @kmd Keyed message digest whose key was set with crypto_kdf108_setkey or
 *	unkeyed message digest
 * @info optional context and application specific information - this may be
 *	 NULL
 * @info_vec number of optional context/application specific information entries
 * @dst destination buffer that the caller already allocated
 * @dlen length of the destination buffer - the KDF derives that amount of
 *	 bytes.
 *
 * To comply with SP800-108, the caller must provide Label || 0x00 || Context
 * in the info parameter.
 *
 * @return 0 on success, < 0 on error
 */
int crypto_kdf108_ctr_generate(struct crypto_shash *kmd,
			       const struct kvec *info, unsigned int info_nvec,
			       u8 *dst, unsigned int dlen);

/**
 * Counter KDF setkey operation
 *
 * @kmd Keyed message digest allocated by the caller. The key should not have
 *	been set.
 * @key Seed key to be used to initialize the keyed message digest context.
 * @keylen This length of the key buffer.
 * @ikm The SP800-108 KDF does not support IKM - this parameter must be NULL
 * @ikmlen This parameter must be 0.
 *
 * According to SP800-108 section 7.2, the seed key must be at least as large as
 * the message digest size of the used keyed message digest. This limitation
 * is enforced by the implementation.
 *
 * SP800-108 allows the use of either a HMAC or a hash primitive. When
 * the caller intends to use a hash primitive, the call to
 * crypto_kdf108_setkey is not required and the key derivation operation can
 * immediately performed using crypto_kdf108_ctr_generate after allocating
 * a handle.
 *
 * @return 0 on success, < 0 on error
 */
int crypto_kdf108_setkey(struct crypto_shash *kmd,
			 const u8 *key, size_t keylen,
			 const u8 *ikm, size_t ikmlen);

#endif /* _CRYPTO_KDF108_H */
