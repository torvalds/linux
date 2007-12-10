/*
 * AEAD: Authenticated Encryption with Associated Data
 * 
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#ifndef _CRYPTO_AEAD_H
#define _CRYPTO_AEAD_H

#include <linux/crypto.h>
#include <linux/kernel.h>

/**
 *	struct aead_givcrypt_request - AEAD request with IV generation
 *	@seq: Sequence number for IV generation
 *	@giv: Space for generated IV
 *	@areq: The AEAD request itself
 */
struct aead_givcrypt_request {
	u64 seq;
	u8 *giv;

	struct aead_request areq;
};

static inline struct crypto_aead *aead_givcrypt_reqtfm(
	struct aead_givcrypt_request *req)
{
	return crypto_aead_reqtfm(&req->areq);
}

#endif	/* _CRYPTO_AEAD_H */
