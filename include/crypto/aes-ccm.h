/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AES-CCM authenticated encryption and decryption
 *
 * Copyright 2026 Google LLC
 */
#ifndef _CRYPTO_AES_CCM_H
#define _CRYPTO_AES_CCM_H

#include <crypto/aes.h>

/**
 * struct aes_ccm_key - A key prepared for AES-CCM encryption and decryption
 */
struct aes_ccm_key {
	/* private: */
	struct aes_enckey aes;
	size_t authtag_len; /* Length of authentication tags in bytes */
};

/**
 * struct aes_ccm_ctx - Context for incrementally en/decrypting a message
 */
struct aes_ccm_ctx {
	/* private: */
	/*
	 * Pointer to the key, which is assumed to live at least as long as this
	 * struct.
	 */
	const struct aes_ccm_key *key;
	/*
	 * The current CBC-MAC chaining value.  When not on a block boundary,
	 * the partial block has been XOR'ed into this.  The number of partial
	 * bytes is 'partial_len'.
	 */
	u8 mac[AES_BLOCK_SIZE] __aligned(__alignof__(__be64));
	/* The current counter, a 128-bit big endian value */
	u8 ctr[AES_BLOCK_SIZE] __aligned(__alignof__(__be64));
	/* Buffered keystream for partial block updates */
	u8 keystream[AES_BLOCK_SIZE] __aligned(__alignof__(__be64));
	/* Encrypted counter of 0.  This gets XOR'ed with the tag at the end. */
	u8 s0[AES_BLOCK_SIZE] __aligned(__alignof__(__be64));
	/* Number of associated data bytes remaining to be provided */
	u64 ad_remaining;
	/* Number of en/decrypted data bytes remaining to be provided */
	u64 data_remaining;
	/* Current partial block length, 0 <= partial_len < AES_BLOCK_SIZE */
	u32 partial_len;
	/* True if associated data padding has been done */
	bool ad_padded;
};

/**
 * aes_ccm_preparekey() - Prepare a key for AES-CCM encryption and decryption
 * @key: (output) The key structure to initialize
 * @in_key: The raw AES-CCM key
 * @key_len: Length of the raw key in bytes: 16, 24, or 32
 * @authtag_len: Length of the authentication tag in bytes:
 *		 4, 6, 8, 10, 12, 14, or 16.  16 is recommended.
 *
 * Users should use memzero_explicit() to zeroize the key struct at the end of
 * its lifetime.  (But if this function fails, zeroization is unnecessary.)
 *
 * Context: Any context.
 * Return:
 * * 0 on success
 * * -EINVAL if either of the lengths is invalid
 */
int __must_check aes_ccm_preparekey(struct aes_ccm_key *key, const u8 *in_key,
				    size_t key_len, size_t authtag_len);

/**
 * aes_ccm_encrypt() - Encrypt a message with AES-CCM
 * @dst: The destination ciphertext data.  Can be in-place or out-of-place.
 *	 For other overlaps the behavior is unspecified.
 * @src: The source plaintext data
 * @data_len: Length of plaintext in bytes (and ciphertext excluding the tag):
 *	      at most 2^(120 - (8 * @nonce_len)) - 1
 * @authtag: The output authentication tag.  Length is the authtag_len that was
 *	     passed to aes_ccm_preparekey().  Usually protocols using AES-CCM
 *	     put the tag at the end of the ciphertext, in which case this should
 *	     be set to @dst + @data_len and @dst must have room for the tag.
 * @ad: The associated data
 * @ad_len: Length of associated data in bytes
 * @nonce: The nonce.  All (key, nonce) pairs used MUST be distinct.
 * @nonce_len: Length of the nonce in bytes: between 7 and 13 inclusive
 * @key: The key, already prepared using aes_ccm_preparekey()
 *
 * Context: Any context.
 * Return:
 * * 0 on success
 * * -EINVAL if @nonce_len is invalid
 * * -EOVERFLOW if @data_len is too large for the selected @nonce_len
 */
int __must_check aes_ccm_encrypt(u8 *dst, const u8 *src, size_t data_len,
				 u8 *authtag, const u8 *ad, size_t ad_len,
				 const u8 *nonce, size_t nonce_len,
				 const struct aes_ccm_key *key);

/**
 * aes_ccm_decrypt() - Decrypt a message with AES-CCM
 * @dst: The destination plaintext data.  Can be in-place or out-of-place.
 *	 For other overlaps the behavior is unspecified.
 * @src: The source ciphertext data
 * @data_len: Length of plaintext in bytes (and ciphertext excluding the tag):
 *	      at most 2^(120 - (8 * @nonce_len)) - 1
 * @authtag: The stored authentication tag.  Length is the authtag_len that was
 *	     passed to aes_ccm_preparekey().  Usually protocols using AES-CCM
 *	     put the tag at the end of the ciphertext, in which case this should
 *	     be set to @src + @data_len and @src must have room for the tag.
 * @ad: The associated data
 * @ad_len: Length of associated data in bytes
 * @nonce: The nonce
 * @nonce_len: Length of the nonce in bytes: between 7 and 13 inclusive
 * @key: The key, already prepared using aes_ccm_preparekey()
 *
 * Context: Any context.
 * Return:
 * * 0 on success.  This is the only case where any decrypted or associated data
 *   can be used.
 * * -EBADMSG if the message is inauthentic
 * * -EINVAL if @nonce_len is invalid
 * * -EOVERFLOW if @data_len is too large for the selected @nonce_len
 */
int __must_check aes_ccm_decrypt(u8 *dst, const u8 *src, size_t data_len,
				 const u8 *authtag, const u8 *ad, size_t ad_len,
				 const u8 *nonce, size_t nonce_len,
				 const struct aes_ccm_key *key);

/**
 * aes_ccm_init() - Initialize context for incremental AES-CCM encryption or
 *		    decryption
 * @ctx: The context to initialize
 * @data_len: Length of the en/decrypted data that will be provided in bytes:
 *	      at most 2^(120 - (8 * @nonce_len)) - 1
 * @ad_len: Length of the associated data that will be provided in bytes
 * @nonce: The nonce.  All (key, nonce) pairs used for encryption MUST be
 *	   distinct.
 * @nonce_len: Length of the nonce in bytes: between 7 and 13 inclusive
 * @key: The key, already prepared using aes_ccm_preparekey().  Note that a
 *	 pointer to the key is saved in the context, so the key must live at
 *	 least as long as the context.
 *
 * Unlike AES-GCM, AES-CCM requires the total lengths of the associated data and
 * the en/decrypted data to be known during initialization.  Callers MUST ensure
 * that these lengths are correct.
 *
 * If this function returns success, the context should be zeroized at the end
 * of its lifetime.  Normally that happens in aes_ccm_encrypt_final() or
 * aes_ccm_decrypt_final(), but callers that abandon a context without
 * finalizing it should explicitly zeroize it.
 *
 * IMPORTANT: Callers that are decrypting MUST NOT assume that any decrypted or
 * associated data is authentic until the authentication tag has been verified.
 * This incremental API is provided solely to support callers that can't
 * efficiently use the one-shot functions due to using a nonlinear data layout.
 *
 * For incremental AES-CCM encryption, use:
 *
 * 1. aes_ccm_init()
 * 2. aes_ccm_auth_update() (any number of times)
 * 3. aes_ccm_encrypt_update() (any number of times)
 * 4. aes_ccm_encrypt_final()
 *
 * For incremental AES-CCM decryption, use:
 *
 * 1. aes_ccm_init()
 * 2. aes_ccm_auth_update() (any number of times)
 * 3. aes_ccm_decrypt_update() (any number of times)
 * 4. aes_ccm_decrypt_final()
 *
 * Context: Any context.
 * Return:
 * * 0 on success
 * * -EINVAL if @nonce_len is invalid
 * * -EOVERFLOW if @data_len is too large for the selected @nonce_len
 */
int __must_check aes_ccm_init(struct aes_ccm_ctx *ctx, u64 data_len, u64 ad_len,
			      const u8 *nonce, size_t nonce_len,
			      const struct aes_ccm_key *key);

/**
 * aes_ccm_auth_update() - Incrementally process AES-CCM associated data
 * @ctx: An AES-CCM context
 * @ad: The associated data
 * @len: Length of the associated data in bytes
 *
 * IMPORTANT: Callers MUST NOT assume that any decrypted or associated data is
 * authentic until the authentication tag has been verified.
 *
 * The total length of the associated data (over all calls to this function)
 * MUST match the ad_len that was passed to aes_ccm_init().
 *
 * Context: Any context.
 */
void aes_ccm_auth_update(struct aes_ccm_ctx *ctx, const u8 *ad, size_t len);

/**
 * aes_ccm_encrypt_update() - Incrementally encrypt data with AES-CCM
 * @ctx: An AES-CCM context
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source plaintext data
 * @len: Number of bytes to encrypt
 *
 * This can be called only after all associated data has been processed.
 *
 * The total length of the encrypted data (over all calls to this function) MUST
 * match the data_len that was passed to aes_ccm_init().
 *
 * Context: Any context.
 */
void aes_ccm_encrypt_update(struct aes_ccm_ctx *ctx, u8 *dst, const u8 *src,
			    size_t len);

/**
 * aes_ccm_decrypt_update() - Incrementally decrypt data with AES-CCM
 * @ctx: An AES-CCM context
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source ciphertext data (not including auth tag)
 * @len: Number of bytes to decrypt
 *
 * This can be called only after all associated data has been processed.
 *
 * The total length of the decrypted data (over all calls to this function) MUST
 * match the data_len that was passed to aes_ccm_init().
 *
 * IMPORTANT: Callers MUST NOT assume that any decrypted or associated data is
 * authentic until the authentication tag has been verified.
 *
 * Context: Any context.
 */
void aes_ccm_decrypt_update(struct aes_ccm_ctx *ctx, u8 *dst, const u8 *src,
			    size_t len);

/**
 * aes_ccm_encrypt_final() - Finish encrypting a message with AES-CCM
 * @ctx: An AES-CCM context
 * @authtag: The output authentication tag.  Length is the authtag_len that was
 *	     passed to aes_ccm_preparekey().
 *
 * This also zeroizes @ctx, so the caller doesn't need to do it.
 *
 * Context: Any context.
 */
void aes_ccm_encrypt_final(struct aes_ccm_ctx *ctx, u8 *authtag);

/**
 * aes_ccm_decrypt_final() - Finish decrypting a message with AES-CCM
 * @ctx: An AES-CCM context
 * @authtag: The stored authentication tag.  Length is the authtag_len that was
 *	     passed to aes_ccm_preparekey().
 *
 * This also zeroizes @ctx, so the caller doesn't need to do it.
 *
 * Context: Any context.
 * Return:
 * * 0 on success.  This is the only case where any decrypted or associated data
 *   can be used.
 * * -EBADMSG if the message is inauthentic
 */
int __must_check aes_ccm_decrypt_final(struct aes_ccm_ctx *ctx,
				       const u8 *authtag);

#endif /* _CRYPTO_AES_CCM_H */
