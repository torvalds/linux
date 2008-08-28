/*
 * Hash: Hash algorithms under the crypto API
 * 
 * Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#ifndef _CRYPTO_HASH_H
#define _CRYPTO_HASH_H

#include <linux/crypto.h>

struct crypto_ahash {
	struct crypto_tfm base;
};

static inline struct crypto_ahash *__crypto_ahash_cast(struct crypto_tfm *tfm)
{
	return (struct crypto_ahash *)tfm;
}

static inline struct crypto_ahash *crypto_alloc_ahash(const char *alg_name,
						      u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	mask &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_AHASH;
	mask |= CRYPTO_ALG_TYPE_AHASH_MASK;

	return __crypto_ahash_cast(crypto_alloc_base(alg_name, type, mask));
}

static inline struct crypto_tfm *crypto_ahash_tfm(struct crypto_ahash *tfm)
{
	return &tfm->base;
}

static inline void crypto_free_ahash(struct crypto_ahash *tfm)
{
	crypto_free_tfm(crypto_ahash_tfm(tfm));
}

static inline unsigned int crypto_ahash_alignmask(
	struct crypto_ahash *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_ahash_tfm(tfm));
}

static inline struct ahash_tfm *crypto_ahash_crt(struct crypto_ahash *tfm)
{
	return &crypto_ahash_tfm(tfm)->crt_ahash;
}

static inline unsigned int crypto_ahash_digestsize(struct crypto_ahash *tfm)
{
	return crypto_ahash_crt(tfm)->digestsize;
}

static inline u32 crypto_ahash_get_flags(struct crypto_ahash *tfm)
{
	return crypto_tfm_get_flags(crypto_ahash_tfm(tfm));
}

static inline void crypto_ahash_set_flags(struct crypto_ahash *tfm, u32 flags)
{
	crypto_tfm_set_flags(crypto_ahash_tfm(tfm), flags);
}

static inline void crypto_ahash_clear_flags(struct crypto_ahash *tfm, u32 flags)
{
	crypto_tfm_clear_flags(crypto_ahash_tfm(tfm), flags);
}

static inline struct crypto_ahash *crypto_ahash_reqtfm(
	struct ahash_request *req)
{
	return __crypto_ahash_cast(req->base.tfm);
}

static inline unsigned int crypto_ahash_reqsize(struct crypto_ahash *tfm)
{
	return crypto_ahash_crt(tfm)->reqsize;
}

static inline int crypto_ahash_setkey(struct crypto_ahash *tfm,
				      const u8 *key, unsigned int keylen)
{
	struct ahash_tfm *crt = crypto_ahash_crt(tfm);

	return crt->setkey(tfm, key, keylen);
}

static inline int crypto_ahash_digest(struct ahash_request *req)
{
	struct ahash_tfm *crt = crypto_ahash_crt(crypto_ahash_reqtfm(req));
	return crt->digest(req);
}

static inline int crypto_ahash_init(struct ahash_request *req)
{
	struct ahash_tfm *crt = crypto_ahash_crt(crypto_ahash_reqtfm(req));
	return crt->init(req);
}

static inline int crypto_ahash_update(struct ahash_request *req)
{
	struct ahash_tfm *crt = crypto_ahash_crt(crypto_ahash_reqtfm(req));
	return crt->update(req);
}

static inline int crypto_ahash_final(struct ahash_request *req)
{
	struct ahash_tfm *crt = crypto_ahash_crt(crypto_ahash_reqtfm(req));
	return crt->final(req);
}

static inline void ahash_request_set_tfm(struct ahash_request *req,
					 struct crypto_ahash *tfm)
{
	req->base.tfm = crypto_ahash_tfm(tfm);
}

static inline struct ahash_request *ahash_request_alloc(
	struct crypto_ahash *tfm, gfp_t gfp)
{
	struct ahash_request *req;

	req = kmalloc(sizeof(struct ahash_request) +
		      crypto_ahash_reqsize(tfm), gfp);

	if (likely(req))
		ahash_request_set_tfm(req, tfm);

	return req;
}

static inline void ahash_request_free(struct ahash_request *req)
{
	kfree(req);
}

static inline struct ahash_request *ahash_request_cast(
	struct crypto_async_request *req)
{
	return container_of(req, struct ahash_request, base);
}

static inline void ahash_request_set_callback(struct ahash_request *req,
					      u32 flags,
					      crypto_completion_t complete,
					      void *data)
{
	req->base.complete = complete;
	req->base.data = data;
	req->base.flags = flags;
}

static inline void ahash_request_set_crypt(struct ahash_request *req,
					   struct scatterlist *src, u8 *result,
					   unsigned int nbytes)
{
	req->src = src;
	req->nbytes = nbytes;
	req->result = result;
}

#endif	/* _CRYPTO_HASH_H */
