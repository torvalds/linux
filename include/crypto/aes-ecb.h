/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AES-ECB unauthenticated encryption and decryption
 *
 * Copyright 2026 Google LLC
 */
#ifndef _CRYPTO_AES_ECB_H
#define _CRYPTO_AES_ECB_H

#include <crypto/aes.h>

/**
 * aes_ecb_encrypt() - Encrypt data using AES-ECB
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source data
 * @len: Number of bytes to encrypt.  Must be a multiple of AES_BLOCK_SIZE.
 * @key: The key, already prepared using aes_preparekey() or aes_prepareenckey()
 *
 * ECB mode is insecure by itself.  This function exists only for compatibility
 * with legacy protocols and for internal use by other modes.
 *
 * This supports incremental encryption, but the length of each chunk must be a
 * multiple of AES_BLOCK_SIZE.
 *
 * Context: Any context.
 */
void aes_ecb_encrypt(u8 *dst, const u8 *src, size_t len, aes_encrypt_arg key);

/**
 * aes_ecb_decrypt() - Decrypt data using AES-ECB
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source data
 * @len: Number of bytes to decrypt.  Must be a multiple of AES_BLOCK_SIZE.
 * @key: The key, already prepared using aes_preparekey()
 *
 * ECB mode is insecure by itself.  This function exists only for compatibility
 * with legacy protocols and for internal use by other modes.
 *
 * This supports incremental decryption, but the length of each chunk must be a
 * multiple of AES_BLOCK_SIZE.
 *
 * Context: Any context.
 */
void aes_ecb_decrypt(u8 *dst, const u8 *src, size_t len,
		     const struct aes_key *key);

#endif /* _CRYPTO_AES_ECB_H */
