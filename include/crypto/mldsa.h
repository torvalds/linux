/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Support for verifying ML-DSA signatures
 *
 * Copyright 2025 Google LLC
 */
#ifndef _CRYPTO_MLDSA_H
#define _CRYPTO_MLDSA_H

#include <linux/types.h>

/* Identifier for an ML-DSA parameter set */
enum mldsa_alg {
	MLDSA44, /* ML-DSA-44 */
	MLDSA65, /* ML-DSA-65 */
	MLDSA87, /* ML-DSA-87 */
};

/* Lengths of ML-DSA public keys and signatures in bytes */
#define MLDSA44_PUBLIC_KEY_SIZE 1312
#define MLDSA65_PUBLIC_KEY_SIZE 1952
#define MLDSA87_PUBLIC_KEY_SIZE 2592
#define MLDSA44_SIGNATURE_SIZE 2420
#define MLDSA65_SIGNATURE_SIZE 3309
#define MLDSA87_SIGNATURE_SIZE 4627

/**
 * mldsa_verify() - Verify an ML-DSA signature
 * @alg: The ML-DSA parameter set to use
 * @sig: The signature
 * @sig_len: Length of the signature in bytes.  Should match the
 *	     MLDSA*_SIGNATURE_SIZE constant associated with @alg,
 *	     otherwise -EBADMSG will be returned.
 * @msg: The message
 * @msg_len: Length of the message in bytes
 * @pk: The public key
 * @pk_len: Length of the public key in bytes.  Should match the
 *	    MLDSA*_PUBLIC_KEY_SIZE constant associated with @alg,
 *	    otherwise -EBADMSG will be returned.
 *
 * This verifies a signature using pure ML-DSA with the specified parameter set.
 * The context string is assumed to be empty.  This corresponds to FIPS 204
 * Algorithm 3 "ML-DSA.Verify" with the ctx parameter set to the empty string
 * and the lengths of the signature and key given explicitly by the caller.
 *
 * Context: Might sleep
 *
 * Return:
 * * 0 if the signature is valid
 * * -EBADMSG if the signature and/or public key is malformed
 * * -EKEYREJECTED if the signature is invalid but otherwise well-formed
 * * -ENOMEM if out of memory so the validity of the signature is unknown
 */
int mldsa_verify(enum mldsa_alg alg, const u8 *sig, size_t sig_len,
		 const u8 *msg, size_t msg_len, const u8 *pk, size_t pk_len);

#if IS_ENABLED(CONFIG_CRYPTO_LIB_MLDSA_KUNIT_TEST)
/* Internal function, exposed only for unit testing */
s32 mldsa_use_hint(u8 h, s32 r, s32 gamma2);
#endif

#endif /* _CRYPTO_MLDSA_H */
