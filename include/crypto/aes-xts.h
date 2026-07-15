/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AES-XTS unauthenticated encryption and decryption
 *
 * Copyright 2026 Google LLC
 */
#ifndef _CRYPTO_AES_XTS_H
#define _CRYPTO_AES_XTS_H

#include <crypto/aes.h>
#include <crypto/xts.h>

/**
 * struct aes_xts_key - A key prepared for AES-XTS encryption and decryption
 *
 * Note that (depending on the architecture) this typically is around 768 bytes,
 * which makes it a bit too large to allocate on the stack in most cases.
 */
struct aes_xts_key {
	/* private: */
	struct aes_key main_key;
	struct aes_enckey tweak_key;
};

/**
 * aes_xts_preparekey() - Prepare a key for AES-XTS encryption and decryption
 * @key: (output) The key structure to initialize
 * @in_key: The raw AES-XTS key
 * @key_len: Length of the raw key in bytes
 * @flags: Optional flag XTS_FORBID_WEAK_KEYS to forbid keys whose two halves
 *	   are the same.
 *
 * Users should use memzero_explicit() to zeroize the key struct at the end of
 * its lifetime.  (But if this function fails, zeroization is unnecessary.)
 *
 * Context: Any context.
 * Return:
 * * 0 on success
 * * -EINVAL if the key is rejected because its length isn't 32, 64, or (when
 *   FIPS mode isn't enabled) 48; or because its two halves are the same and
 *   either XTS_FORBID_WEAK_KEYS is given or FIPS mode is enabled.
 */
int __must_check aes_xts_preparekey(struct aes_xts_key *key, const u8 *in_key,
				    size_t key_len, int flags);

/**
 * aes_xts_encrypt() - Encrypt data using AES-XTS
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source data
 * @len: Number of bytes to encrypt.  On non-final calls it must be a nonzero
 *	 multiple of AES_BLOCK_SIZE.  On the final call it can be any value >=
 *	 AES_BLOCK_SIZE, i.e. ciphertext stealing is supported.
 * @tweak: The tweak.  It is updated with the next value, unless @len isn't a
 *	   multiple of AES_BLOCK_SIZE in which case the value is unspecified.
 * @key: The key, already prepared using aes_xts_preparekey()
 * @cont: %false to begin encrypting a new message (do the tweak encryption);
 *	  %true to continue encrypting a message (skip tweak encryption)
 *
 * This supports both one-shot and incremental encryption.  On the first call,
 * pass @cont = %false.  On any later calls, pass @cont = %true and the updated
 * @tweak; all earlier @len must have been multiples of AES_BLOCK_SIZE.
 *
 * Context: Any context.
 */
void aes_xts_encrypt(u8 *dst, const u8 *src, size_t len,
		     u8 tweak[at_least AES_BLOCK_SIZE],
		     const struct aes_xts_key *key, bool cont);

/**
 * aes_xts_decrypt() - Decrypt data using AES-XTS
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source data
 * @len: Number of bytes to decrypt.  On non-final calls it must be a nonzero
 *	 multiple of AES_BLOCK_SIZE.  On the final call it can be any value >=
 *	 AES_BLOCK_SIZE, i.e. ciphertext stealing is supported.
 * @tweak: The tweak.  It is updated with the next value, unless @len isn't a
 *	   multiple of AES_BLOCK_SIZE in which case the value is unspecified.
 * @key: The key, already prepared using aes_xts_preparekey()
 * @cont: %false to begin decrypting a new message (do the tweak encryption);
 *	  %true to continue decrypting a message (skip tweak encryption)
 *
 * This supports both one-shot and incremental decryption.  On the first call,
 * pass @cont = %false.  On any later calls, pass @cont = %true and the updated
 * @tweak; all earlier @len must have been multiples of AES_BLOCK_SIZE.
 *
 * Context: Any context.
 */
void aes_xts_decrypt(u8 *dst, const u8 *src, size_t len,
		     u8 tweak[at_least AES_BLOCK_SIZE],
		     const struct aes_xts_key *key, bool cont);

#endif /* _CRYPTO_AES_XTS_H */
