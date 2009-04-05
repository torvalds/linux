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

struct shash_desc {
	struct crypto_shash *tfm;
	u32 flags;

	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

struct shash_alg {
	int (*init)(struct shash_desc *desc);
	int (*reinit)(struct shash_desc *desc);
	int (*update)(struct shash_desc *desc, const u8 *data,
		      unsigned int len);
	int (*final)(struct shash_desc *desc, u8 *out);
	int (*finup)(struct shash_desc *desc, const u8 *data,
		     unsigned int len, u8 *out);
	int (*digest)(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out);
	int (*setkey)(struct crypto_shash *tfm, const u8 *key,
		      unsigned int keylen);

	unsigned int descsize;
	unsigned int digestsize;

	struct crypto_alg base;
};

struct crypto_ahash {
	struct crypto_tfm base;
};

struct crypto_shash {
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

static inline void *ahash_request_ctx(struct ahash_request *req)
{
	return req->__ctx;
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

static inline void crypto_ahash_export(struct ahash_request *req, u8 *out)
{
	memcpy(out, ahash_request_ctx(req),
	       crypto_ahash_reqsize(crypto_ahash_reqtfm(req)));
}

int crypto_ahash_import(struct ahash_request *req, const u8 *in);

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

struct crypto_shash *crypto_alloc_shash(const char *alg_name, u32 type,
					u32 mask);

static inline struct crypto_tfm *crypto_shash_tfm(struct crypto_shash *tfm)
{
	return &tfm->base;
}

static inline void crypto_free_shash(struct crypto_shash *tfm)
{
	crypto_destroy_tfm(tfm, crypto_shash_tfm(tfm));
}

static inline unsigned int crypto_shash_alignmask(
	struct crypto_shash *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_shash_tfm(tfm));
}

static inline unsigned int crypto_shash_blocksize(struct crypto_shash *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_shash_tfm(tfm));
}

static inline struct shash_alg *__crypto_shash_alg(struct crypto_alg *alg)
{
	return container_of(alg, struct shash_alg, base);
}

static inline struct shash_alg *crypto_shash_alg(struct crypto_shash *tfm)
{
	return __crypto_shash_alg(crypto_shash_tfm(tfm)->__crt_alg);
}

static inline unsigned int crypto_shash_digestsize(struct crypto_shash *tfm)
{
	return crypto_shash_alg(tfm)->digestsize;
}

static inline u32 crypto_shash_get_flags(struct crypto_shash *tfm)
{
	return crypto_tfm_get_flags(crypto_shash_tfm(tfm));
}

static inline void crypto_shash_set_flags(struct crypto_shash *tfm, u32 flags)
{
	crypto_tfm_set_flags(crypto_shash_tfm(tfm), flags);
}

static inline void crypto_shash_clear_flags(struct crypto_shash *tfm, u32 flags)
{
	crypto_tfm_clear_flags(crypto_shash_tfm(tfm), flags);
}

static inline unsigned int crypto_shash_descsize(struct crypto_shash *tfm)
{
	return crypto_shash_alg(tfm)->descsize;
}

static inline void *shash_desc_ctx(struct shash_desc *desc)
{
	return desc->__ctx;
}

int crypto_shash_setkey(struct crypto_shash *tfm, const u8 *key,
			unsigned int keylen);
int crypto_shash_digest(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out);

static inline void crypto_shash_export(struct shash_desc *desc, u8 *out)
{
	memcpy(out, shash_desc_ctx(desc), crypto_shash_descsize(desc->tfm));
}

int crypto_shash_import(struct shash_desc *desc, const u8 *in);

static inline int crypto_shash_init(struct shash_desc *desc)
{
	return crypto_shash_alg(desc->tfm)->init(desc);
}

int crypto_shash_update(struct shash_desc *desc, const u8 *data,
			unsigned int len);
int crypto_shash_final(struct shash_desc *desc, u8 *out);
int crypto_shash_finup(struct shash_desc *desc, const u8 *data,
		       unsigned int len, u8 *out);

#endif	/* _CRYPTO_HASH_H */
