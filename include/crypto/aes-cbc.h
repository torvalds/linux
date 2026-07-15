/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AES-CBC and AES-CBC-CTS unauthenticated encryption and decryption
 *
 * Copyright 2026 Google LLC
 */
#ifndef _CRYPTO_AES_CBC_H
#define _CRYPTO_AES_CBC_H

#include <crypto/aes.h>

/**
 * aes_cbc_encrypt() - Encrypt data using AES-CBC
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source data
 * @len: Number of bytes to encrypt.  Must be a multiple of AES_BLOCK_SIZE.
 * @iv: The initialization vector.  It is updated with the next value, i.e. the
 *	last ciphertext block (or left unchanged if @len == 0).
 * @key: The key, already prepared using aes_preparekey() or aes_prepareenckey()
 *
 * This supports incremental encryption.  The length of each chunk must be a
 * multiple of AES_BLOCK_SIZE, and the updated @iv must be passed in each time.
 *
 * Context: Any context.
 */
void aes_cbc_encrypt(u8 *dst, const u8 *src, size_t len,
		     u8 iv[at_least AES_BLOCK_SIZE], aes_encrypt_arg key);

/**
 * aes_cbc_decrypt() - Decrypt data using AES-CBC
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source data
 * @len: Number of bytes to decrypt.  Must be a multiple of AES_BLOCK_SIZE.
 * @iv: The initialization vector.  It is updated with the next value, i.e. the
 *	last ciphertext block (or left unchanged if @len == 0).
 * @key: The key, already prepared using aes_preparekey()
 *
 * This supports incremental decryption.  The length of each chunk must be a
 * multiple of AES_BLOCK_SIZE, and the updated @iv must be passed in each time.
 *
 * Context: Any context.
 */
void aes_cbc_decrypt(u8 *dst, const u8 *src, size_t len,
		     u8 iv[at_least AES_BLOCK_SIZE], const struct aes_key *key);

/**
 * aes_cbc_cts_encrypt() - Encrypt data using AES-CBC-CTS (CS3 variant)
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source data
 * @len: Number of bytes to encrypt, at least AES_BLOCK_SIZE
 * @iv: The initialization vector, clobbered by this function
 * @key: The key, already prepared using aes_preparekey() or aes_prepareenckey()
 *
 * Context: Any context.
 */
void aes_cbc_cts_encrypt(u8 *dst, const u8 *src, size_t len,
			 u8 iv[at_least AES_BLOCK_SIZE], aes_encrypt_arg key);

/**
 * aes_cbc_cts_decrypt() - Decrypt data using AES-CBC-CTS (CS3 variant)
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source data
 * @len: Number of bytes to decrypt, at least AES_BLOCK_SIZE
 * @iv: The initialization vector, clobbered by this function
 * @key: The key, already prepared using aes_preparekey()
 *
 * Context: Any context.
 */
void aes_cbc_cts_decrypt(u8 *dst, const u8 *src, size_t len,
			 u8 iv[at_least AES_BLOCK_SIZE],
			 const struct aes_key *key);

#endif /* _CRYPTO_AES_CBC_H */
