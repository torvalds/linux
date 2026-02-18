/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for AES-CMAC, AES-XCBC-MAC, and AES-CBC-MAC
 *
 * Copyright 2026 Google LLC
 */
#ifndef _CRYPTO_AES_CBC_MACS_H
#define _CRYPTO_AES_CBC_MACS_H

#include <crypto/aes.h>

/**
 * struct aes_cmac_key - Prepared key for AES-CMAC or AES-XCBC-MAC
 * @aes: The AES key for cipher block chaining
 * @k_final: Finalization subkeys for the final block.
 *	     k_final[0] (CMAC K1, XCBC-MAC K2) is used if it's a full block.
 *	     k_final[1] (CMAC K2, XCBC-MAC K3) is used if it's a partial block.
 */
struct aes_cmac_key {
	struct aes_enckey aes;
	union {
		u8 b[AES_BLOCK_SIZE];
		__be64 w[2];
	} k_final[2];
};

/**
 * struct aes_cmac_ctx - Context for computing an AES-CMAC or AES-XCBC-MAC value
 * @key: Pointer to the key struct.  A pointer is used rather than a copy of the
 *	 struct, since the key struct size may be large.  It is assumed that the
 *	 key lives at least as long as the context.
 * @partial_len: Number of bytes that have been XOR'ed into @h since the last
 *		 AES encryption.  This is 0 if no data has been processed yet,
 *		 or between 1 and AES_BLOCK_SIZE inclusive otherwise.
 * @h: The current chaining value
 */
struct aes_cmac_ctx {
	const struct aes_cmac_key *key;
	size_t partial_len;
	u8 h[AES_BLOCK_SIZE];
};

/**
 * aes_cmac_preparekey() - Prepare a key for AES-CMAC
 * @key: (output) The key struct to initialize
 * @in_key: The raw AES key
 * @key_len: Length of the raw key in bytes.  The supported values are
 *	     AES_KEYSIZE_128, AES_KEYSIZE_192, and AES_KEYSIZE_256.
 *
 * Context: Any context.
 * Return: 0 on success or -EINVAL if the given key length is invalid.  No other
 *	   errors are possible, so callers that always pass a valid key length
 *	   don't need to check for errors.
 */
int aes_cmac_preparekey(struct aes_cmac_key *key, const u8 *in_key,
			size_t key_len);

/**
 * aes_xcbcmac_preparekey() - Prepare a key for AES-XCBC-MAC
 * @key: (output) The key struct to initialize
 * @in_key: The raw key.  As per the AES-XCBC-MAC specification (RFC 3566), this
 *	    is 128 bits, matching the internal use of AES-128.
 *
 * AES-XCBC-MAC and AES-CMAC are the same except for the key preparation.  After
 * that step, AES-XCBC-MAC is supported via the aes_cmac_* functions.
 *
 * New users should use AES-CMAC instead of AES-XCBC-MAC.
 *
 * Context: Any context.
 */
void aes_xcbcmac_preparekey(struct aes_cmac_key *key,
			    const u8 in_key[at_least AES_KEYSIZE_128]);

/**
 * aes_cmac_init() - Start computing an AES-CMAC or AES-XCBC-MAC value
 * @ctx: (output) The context to initialize
 * @key: The key to use.  Note that a pointer to the key is saved in the
 *	 context, so the key must live at least as long as the context.
 *
 * This supports both AES-CMAC and AES-XCBC-MAC.  Which one is done depends on
 * whether aes_cmac_preparekey() or aes_xcbcmac_preparekey() was called.
 */
static inline void aes_cmac_init(struct aes_cmac_ctx *ctx,
				 const struct aes_cmac_key *key)
{
	*ctx = (struct aes_cmac_ctx){ .key = key };
}

/**
 * aes_cmac_update() - Update an AES-CMAC or AES-XCBC-MAC context with more data
 * @ctx: The context to update; must have been initialized
 * @data: The message data
 * @data_len: The data length in bytes.  Doesn't need to be block-aligned.
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
void aes_cmac_update(struct aes_cmac_ctx *ctx, const u8 *data, size_t data_len);

/**
 * aes_cmac_final() - Finish computing an AES-CMAC or AES-XCBC-MAC value
 * @ctx: The context to finalize; must have been initialized
 * @out: (output) The resulting MAC
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void aes_cmac_final(struct aes_cmac_ctx *ctx, u8 out[at_least AES_BLOCK_SIZE]);

/**
 * aes_cmac() - Compute AES-CMAC or AES-XCBC-MAC in one shot
 * @key: The key to use
 * @data: The message data
 * @data_len: The data length in bytes
 * @out: (output) The resulting AES-CMAC or AES-XCBC-MAC value
 *
 * This supports both AES-CMAC and AES-XCBC-MAC.  Which one is done depends on
 * whether aes_cmac_preparekey() or aes_xcbcmac_preparekey() was called.
 *
 * Context: Any context.
 */
static inline void aes_cmac(const struct aes_cmac_key *key, const u8 *data,
			    size_t data_len, u8 out[at_least AES_BLOCK_SIZE])
{
	struct aes_cmac_ctx ctx;

	aes_cmac_init(&ctx, key);
	aes_cmac_update(&ctx, data, data_len);
	aes_cmac_final(&ctx, out);
}

/*
 * AES-CBC-MAC support.  This is provided only for use by the implementation of
 * AES-CCM.  It should have no other users.  Warning: unlike AES-CMAC and
 * AES-XCBC-MAC, AES-CBC-MAC isn't a secure MAC for variable-length messages.
 */
struct aes_cbcmac_ctx {
	const struct aes_enckey *key;
	size_t partial_len;
	u8 h[AES_BLOCK_SIZE];
};
static inline void aes_cbcmac_init(struct aes_cbcmac_ctx *ctx,
				   const struct aes_enckey *key)
{
	*ctx = (struct aes_cbcmac_ctx){ .key = key };
}
void aes_cbcmac_update(struct aes_cbcmac_ctx *ctx, const u8 *data,
		       size_t data_len);
void aes_cbcmac_final(struct aes_cbcmac_ctx *ctx,
		      u8 out[at_least AES_BLOCK_SIZE]);

#endif /* _CRYPTO_AES_CBC_MACS_H */
