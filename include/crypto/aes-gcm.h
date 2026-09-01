/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AES-GCM authenticated encryption and decryption
 *
 * Copyright 2026 Google LLC
 */
#ifndef _CRYPTO_AES_GCM_H
#define _CRYPTO_AES_GCM_H

#include <crypto/aes.h>
#include <crypto/gcm.h>
#include <crypto/gf128hash.h>

/**
 * struct aes_gcm_key - A key prepared for AES-GCM encryption and decryption
 */
struct aes_gcm_key {
	/* private: */
	struct aes_enckey aes;
	struct ghash_key ghash;
	size_t authtag_len; /* Length of authentication tags in bytes */
};

/**
 * struct aes_gcm_ctx - Context for incrementally en/decrypting a message
 */
struct aes_gcm_ctx {
	/* private: */
	/*
	 * Pointer to the key, which is assumed to live at least as long as this
	 * struct.
	 */
	const struct aes_gcm_key *key;
	/* The current GHASH context */
	struct ghash_ctx ghash;
	/*
	 * The current counter.  This can be viewed as either a 128-bit big
	 * endian counter, or as a 96-bit nonce followed by a 32-bit big endian
	 * counter; it doesn't matter, since the last 32-bit word starts at 1,
	 * and AES-GCM is undefined for messages that would overflow that part.
	 * In practice this means that code optimized for AES-GCM can just
	 * increment the last 32-bit word (wrapping at 2^32), but when needed it
	 * can still call AES-CTR code that does a 128-bit increment.
	 *
	 * 'long' alignment is for crypto_xor() to work more efficiently.
	 */
	union {
		u8 ctr[AES_BLOCK_SIZE];
		__be32 ctr32[AES_BLOCK_SIZE / 4];
	} __aligned(__alignof__(long));
	/* Buffered keystream for partial block updates */
	u8 keystream[AES_BLOCK_SIZE] __aligned(__alignof__(long));
	/* Encrypted counter of 1.  This gets XOR'ed with the tag at the end. */
	u8 j0_enc[AES_BLOCK_SIZE] __aligned(__alignof__(long));
	/* Number of associated data bytes processed so far */
	u64 ad_len;
	/* Number of en/decrypted bytes processed so far */
	u64 data_len;
};

/**
 * aes_gcm_preparekey() - Prepare a key for AES-GCM encryption and decryption
 * @key: (output) The key structure to initialize
 * @in_key: The raw AES-GCM key
 * @key_len: Length of the raw key in bytes: 16, 24, or 32
 * @authtag_len: Length of the authentication tag in bytes:
 *		 4, 8, 12, 13, 14, 15, or 16.  16 is recommended.
 *
 * Users should use memzero_explicit() to zeroize the key struct at the end of
 * its lifetime.  (But if this function fails, zeroization is unnecessary.)
 *
 * Context: Any context.
 * Return:
 * * 0 on success
 * * -EINVAL if either of the lengths is invalid
 */
int __must_check aes_gcm_preparekey(struct aes_gcm_key *key, const u8 *in_key,
				    size_t key_len, size_t authtag_len);

/**
 * aes_gcm_encrypt() - Encrypt a message with AES-GCM
 * @dst: The destination ciphertext data.  Can be in-place or out-of-place.
 *	 For other overlaps the behavior is unspecified.
 * @src: The source plaintext data
 * @data_len: Length of plaintext in bytes (and ciphertext excluding the tag):
 *	      at most 2^36 - 32
 * @authtag: The output authentication tag.  Length is the authtag_len that was
 *	     passed to aes_gcm_preparekey().  Usually protocols using AES-GCM
 *	     put the tag at the end of the ciphertext, in which case this should
 *	     be set to @dst + @data_len and @dst must have room for the tag.
 * @ad: The associated data
 * @ad_len: Length of associated data in bytes: at most 2^61 - 1
 * @nonce: The 12-byte nonce.  All (key, nonce) pairs used MUST be distinct.
 * @key: The key, already prepared using aes_gcm_preparekey()
 *
 * For AES-GMAC (i.e., AES-GCM without any data en/decrypted), use dst=NULL,
 * src=NULL, and data_len=0 to generate the AES-GMAC value.
 *
 * Context: Any context.
 */
void aes_gcm_encrypt(u8 *dst, const u8 *src, size_t data_len, u8 *authtag,
		     const u8 *ad, size_t ad_len, const u8 nonce[at_least 12],
		     const struct aes_gcm_key *key);

/**
 * aes_gcm_decrypt() - Decrypt a message with AES-GCM
 * @dst: The destination plaintext data.  Can be in-place or out-of-place.
 *	 For other overlaps the behavior is unspecified.
 * @src: The source ciphertext data
 * @data_len: Length of plaintext in bytes (and ciphertext excluding the tag):
 *	      at most 2^36 - 32
 * @authtag: The stored authentication tag.  Length is the authtag_len that was
 *	     passed to aes_gcm_preparekey().  Usually protocols using AES-GCM
 *	     put the tag at the end of the ciphertext, in which case this should
 *	     be set to @src + @data_len and @src must have room for the tag.
 * @ad: The associated data
 * @ad_len: Length of associated data in bytes: at most 2^61 - 1
 * @nonce: The 12-byte nonce
 * @key: The key, already prepared using aes_gcm_preparekey()
 *
 * For AES-GMAC (i.e., AES-GCM without any data en/decrypted), use dst=NULL,
 * src=NULL, and data_len=0 to verify the AES-GMAC value.
 *
 * Context: Any context.
 * Return:
 * * 0 on success.  This is the only case where any decrypted or associated data
 *   can be used.
 * * -EBADMSG if the message is inauthentic
 */
int __must_check aes_gcm_decrypt(u8 *dst, const u8 *src, size_t data_len,
				 const u8 *authtag, const u8 *ad, size_t ad_len,
				 const u8 nonce[at_least 12],
				 const struct aes_gcm_key *key);

/**
 * aes_gcm_init() - Initialize context for incremental AES-GCM encryption or
 *		    decryption, or for AES-GMAC computation
 * @ctx: The context to initialize
 * @nonce: The 12-byte nonce.  All (key, nonce) pairs used for encryption or MAC
 *	   generation MUST be distinct.
 * @key: The key, already prepared using aes_gcm_preparekey().  Note that a
 *	 pointer to the key is saved in the context, so the key must live at
 *	 least as long as the context.
 *
 * The context should be zeroized at the end of its lifetime.  Normally that
 * happens in aes_gcm_encrypt_final() or aes_gcm_decrypt_final(), but callers
 * that abandon a context without finalizing it should explicitly zeroize it.
 *
 * IMPORTANT: Callers that are decrypting data or computing a GMAC value for
 * verification MUST NOT assume that any decrypted or associated data is
 * authentic until the authentication tag has been verified.  This incremental
 * API is provided solely to support callers that can't efficiently use the
 * one-shot functions due to using a nonlinear data layout.
 *
 * For incremental AES-GCM encryption, use:
 *
 * 1. aes_gcm_init()
 * 2. aes_gcm_auth_update() (any number of times)
 * 3. aes_gcm_encrypt_update() (any number of times)
 * 4. aes_gcm_encrypt_final()
 *
 * For incremental AES-GCM decryption, use:
 *
 * 1. aes_gcm_init()
 * 2. aes_gcm_auth_update() (any number of times)
 * 3. aes_gcm_decrypt_update() (any number of times)
 * 4. aes_gcm_decrypt_final()
 *
 * AES-GMAC is just AES-GCM with zero bytes en/decrypted.  For incremental
 * AES-GMAC computation, use:
 *
 * 1. aes_gcm_init()
 * 2. aes_gcm_auth_update() (any number of times)
 * 3. aes_gcm_encrypt_final() to return the computed tag to the caller, or
 *    aes_gcm_decrypt_final() to directly verify the computed tag
 *
 * Context: Any context.
 */
void aes_gcm_init(struct aes_gcm_ctx *ctx, const u8 nonce[at_least 12],
		  const struct aes_gcm_key *key);

/**
 * aes_gcm_auth_update() - Incrementally process AES-GCM associated data
 * @ctx: An AES-GCM context
 * @ad: The associated data
 * @len: Number of bytes provided.  The caller must ensure that the total
 *	 associated data length doesn't exceed GCM's limit of 2^61 - 1.
 *
 * IMPORTANT: Callers MUST NOT assume that any decrypted or associated data is
 * authentic until the authentication tag has been verified.
 *
 * Context: Any context.
 */
void aes_gcm_auth_update(struct aes_gcm_ctx *ctx, const u8 *ad, size_t len);

/**
 * aes_gcm_encrypt_update() - Incrementally encrypt data with AES-GCM
 * @ctx: An AES-GCM context
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source plaintext data
 * @len: Number of bytes to encrypt.  The caller must ensure that the total
 *	 number of bytes encrypted doesn't exceed GCM's limit of 2^36 - 32.
 *
 * This can be called only after all associated data has been processed.
 *
 * Context: Any context.
 */
void aes_gcm_encrypt_update(struct aes_gcm_ctx *ctx, u8 *dst, const u8 *src,
			    size_t len);

/**
 * aes_gcm_decrypt_update() - Incrementally decrypt data with AES-GCM
 * @ctx: An AES-GCM context
 * @dst: The destination buffer.  Can be in-place or out-of-place.  For other
 *	 overlaps the behavior is unspecified.
 * @src: The source ciphertext data (not including auth tag)
 * @len: Number of bytes to decrypt.  The caller must ensure that the total
 *	 number of bytes decrypted doesn't exceed GCM's limit of 2^36 - 32.
 *
 * This can be called only after all associated data has been processed.
 *
 * IMPORTANT: Callers MUST NOT assume that any decrypted or associated data is
 * authentic until the authentication tag has been verified.
 *
 * Context: Any context.
 */
void aes_gcm_decrypt_update(struct aes_gcm_ctx *ctx, u8 *dst, const u8 *src,
			    size_t len);

/**
 * aes_gcm_encrypt_final() - Finish encrypting a message with AES-GCM
 * @ctx: An AES-GCM context
 * @authtag: The output authentication tag.  Length is the authtag_len that was
 *	     passed to aes_gcm_preparekey().
 *
 * This also zeroizes @ctx, so the caller doesn't need to do it.
 *
 * Context: Any context.
 */
void aes_gcm_encrypt_final(struct aes_gcm_ctx *ctx, u8 *authtag);

/**
 * aes_gcm_decrypt_final() - Finish decrypting a message with AES-GCM
 * @ctx: An AES-GCM context
 * @authtag: The stored authentication tag.  Length is the authtag_len that was
 *	     passed to aes_gcm_preparekey().
 *
 * This also zeroizes @ctx, so the caller doesn't need to do it.
 *
 * Context: Any context.
 * Return:
 * * 0 on success.  This is the only case where any decrypted or associated data
 *   can be used.
 * * -EBADMSG if the message is inauthentic
 */
int __must_check aes_gcm_decrypt_final(struct aes_gcm_ctx *ctx,
				       const u8 *authtag);

#endif /* _CRYPTO_AES_GCM_H */
