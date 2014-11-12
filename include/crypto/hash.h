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

struct crypto_ahash;

/**
 * DOC: Message Digest Algorithm Definitions
 *
 * These data structures define modular message digest algorithm
 * implementations, managed via crypto_register_ahash(),
 * crypto_register_shash(), crypto_unregister_ahash() and
 * crypto_unregister_shash().
 */

/**
 * struct hash_alg_common - define properties of message digest
 * @digestsize: Size of the result of the transformation. A buffer of this size
 *	        must be available to the @final and @finup calls, so they can
 *	        store the resulting hash into it. For various predefined sizes,
 *	        search include/crypto/ using
 *	        git grep _DIGEST_SIZE include/crypto.
 * @statesize: Size of the block for partial state of the transformation. A
 *	       buffer of this size must be passed to the @export function as it
 *	       will save the partial state of the transformation into it. On the
 *	       other side, the @import function will load the state from a
 *	       buffer of this size as well.
 */
struct hash_alg_common {
	unsigned int digestsize;
	unsigned int statesize;

	struct crypto_alg base;
};

struct ahash_request {
	struct crypto_async_request base;

	unsigned int nbytes;
	struct scatterlist *src;
	u8 *result;

	/* This field may only be used by the ahash API code. */
	void *priv;

	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

/**
 * struct ahash_alg - asynchronous message digest definition
 * @init: Initialize the transformation context. Intended only to initialize the
 *	  state of the HASH transformation at the begining. This shall fill in
 *	  the internal structures used during the entire duration of the whole
 *	  transformation. No data processing happens at this point.
 * @update: Push a chunk of data into the driver for transformation. This
 *	   function actually pushes blocks of data from upper layers into the
 *	   driver, which then passes those to the hardware as seen fit. This
 *	   function must not finalize the HASH transformation by calculating the
 *	   final message digest as this only adds more data into the
 *	   transformation. This function shall not modify the transformation
 *	   context, as this function may be called in parallel with the same
 *	   transformation object. Data processing can happen synchronously
 *	   [SHASH] or asynchronously [AHASH] at this point.
 * @final: Retrieve result from the driver. This function finalizes the
 *	   transformation and retrieves the resulting hash from the driver and
 *	   pushes it back to upper layers. No data processing happens at this
 *	   point.
 * @finup: Combination of @update and @final. This function is effectively a
 *	   combination of @update and @final calls issued in sequence. As some
 *	   hardware cannot do @update and @final separately, this callback was
 *	   added to allow such hardware to be used at least by IPsec. Data
 *	   processing can happen synchronously [SHASH] or asynchronously [AHASH]
 *	   at this point.
 * @digest: Combination of @init and @update and @final. This function
 *	    effectively behaves as the entire chain of operations, @init,
 *	    @update and @final issued in sequence. Just like @finup, this was
 *	    added for hardware which cannot do even the @finup, but can only do
 *	    the whole transformation in one run. Data processing can happen
 *	    synchronously [SHASH] or asynchronously [AHASH] at this point.
 * @setkey: Set optional key used by the hashing algorithm. Intended to push
 *	    optional key used by the hashing algorithm from upper layers into
 *	    the driver. This function can store the key in the transformation
 *	    context or can outright program it into the hardware. In the former
 *	    case, one must be careful to program the key into the hardware at
 *	    appropriate time and one must be careful that .setkey() can be
 *	    called multiple times during the existence of the transformation
 *	    object. Not  all hashing algorithms do implement this function as it
 *	    is only needed for keyed message digests. SHAx/MDx/CRCx do NOT
 *	    implement this function. HMAC(MDx)/HMAC(SHAx)/CMAC(AES) do implement
 *	    this function. This function must be called before any other of the
 *	    @init, @update, @final, @finup, @digest is called. No data
 *	    processing happens at this point.
 * @export: Export partial state of the transformation. This function dumps the
 *	    entire state of the ongoing transformation into a provided block of
 *	    data so it can be @import 'ed back later on. This is useful in case
 *	    you want to save partial result of the transformation after
 *	    processing certain amount of data and reload this partial result
 *	    multiple times later on for multiple re-use. No data processing
 *	    happens at this point.
 * @import: Import partial state of the transformation. This function loads the
 *	    entire state of the ongoing transformation from a provided block of
 *	    data so the transformation can continue from this point onward. No
 *	    data processing happens at this point.
 */
struct ahash_alg {
	int (*init)(struct ahash_request *req);
	int (*update)(struct ahash_request *req);
	int (*final)(struct ahash_request *req);
	int (*finup)(struct ahash_request *req);
	int (*digest)(struct ahash_request *req);
	int (*export)(struct ahash_request *req, void *out);
	int (*import)(struct ahash_request *req, const void *in);
	int (*setkey)(struct crypto_ahash *tfm, const u8 *key,
		      unsigned int keylen);

	struct hash_alg_common halg;
};

struct shash_desc {
	struct crypto_shash *tfm;
	u32 flags;

	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

#define SHASH_DESC_ON_STACK(shash, ctx)				  \
	char __##shash##_desc[sizeof(struct shash_desc) +	  \
		crypto_shash_descsize(ctx)] CRYPTO_MINALIGN_ATTR; \
	struct shash_desc *shash = (struct shash_desc *)__##shash##_desc

/**
 * struct shash_alg - synchronous message digest definition
 * @init: see struct ahash_alg
 * @update: see struct ahash_alg
 * @final: see struct ahash_alg
 * @finup: see struct ahash_alg
 * @digest: see struct ahash_alg
 * @export: see struct ahash_alg
 * @import: see struct ahash_alg
 * @setkey: see struct ahash_alg
 * @digestsize: see struct ahash_alg
 * @statesize: see struct ahash_alg
 * @dedcsize: Size of the operational state for the message digest. This state
 * 	      size is the memory size that needs to be allocated for
 *	      shash_desc.__ctx
 * @base: internally used
 */
struct shash_alg {
	int (*init)(struct shash_desc *desc);
	int (*update)(struct shash_desc *desc, const u8 *data,
		      unsigned int len);
	int (*final)(struct shash_desc *desc, u8 *out);
	int (*finup)(struct shash_desc *desc, const u8 *data,
		     unsigned int len, u8 *out);
	int (*digest)(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out);
	int (*export)(struct shash_desc *desc, void *out);
	int (*import)(struct shash_desc *desc, const void *in);
	int (*setkey)(struct crypto_shash *tfm, const u8 *key,
		      unsigned int keylen);

	unsigned int descsize;

	/* These fields must match hash_alg_common. */
	unsigned int digestsize
		__attribute__ ((aligned(__alignof__(struct hash_alg_common))));
	unsigned int statesize;

	struct crypto_alg base;
};

struct crypto_ahash {
	int (*init)(struct ahash_request *req);
	int (*update)(struct ahash_request *req);
	int (*final)(struct ahash_request *req);
	int (*finup)(struct ahash_request *req);
	int (*digest)(struct ahash_request *req);
	int (*export)(struct ahash_request *req, void *out);
	int (*import)(struct ahash_request *req, const void *in);
	int (*setkey)(struct crypto_ahash *tfm, const u8 *key,
		      unsigned int keylen);

	unsigned int reqsize;
	struct crypto_tfm base;
};

struct crypto_shash {
	unsigned int descsize;
	struct crypto_tfm base;
};

static inline struct crypto_ahash *__crypto_ahash_cast(struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_ahash, base);
}

struct crypto_ahash *crypto_alloc_ahash(const char *alg_name, u32 type,
					u32 mask);

static inline struct crypto_tfm *crypto_ahash_tfm(struct crypto_ahash *tfm)
{
	return &tfm->base;
}

static inline void crypto_free_ahash(struct crypto_ahash *tfm)
{
	crypto_destroy_tfm(tfm, crypto_ahash_tfm(tfm));
}

static inline unsigned int crypto_ahash_alignmask(
	struct crypto_ahash *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_ahash_tfm(tfm));
}

static inline struct hash_alg_common *__crypto_hash_alg_common(
	struct crypto_alg *alg)
{
	return container_of(alg, struct hash_alg_common, base);
}

static inline struct hash_alg_common *crypto_hash_alg_common(
	struct crypto_ahash *tfm)
{
	return __crypto_hash_alg_common(crypto_ahash_tfm(tfm)->__crt_alg);
}

static inline unsigned int crypto_ahash_digestsize(struct crypto_ahash *tfm)
{
	return crypto_hash_alg_common(tfm)->digestsize;
}

static inline unsigned int crypto_ahash_statesize(struct crypto_ahash *tfm)
{
	return crypto_hash_alg_common(tfm)->statesize;
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
	return tfm->reqsize;
}

static inline void *ahash_request_ctx(struct ahash_request *req)
{
	return req->__ctx;
}

int crypto_ahash_setkey(struct crypto_ahash *tfm, const u8 *key,
			unsigned int keylen);
int crypto_ahash_finup(struct ahash_request *req);
int crypto_ahash_final(struct ahash_request *req);
int crypto_ahash_digest(struct ahash_request *req);

static inline int crypto_ahash_export(struct ahash_request *req, void *out)
{
	return crypto_ahash_reqtfm(req)->export(req, out);
}

static inline int crypto_ahash_import(struct ahash_request *req, const void *in)
{
	return crypto_ahash_reqtfm(req)->import(req, in);
}

static inline int crypto_ahash_init(struct ahash_request *req)
{
	return crypto_ahash_reqtfm(req)->init(req);
}

static inline int crypto_ahash_update(struct ahash_request *req)
{
	return crypto_ahash_reqtfm(req)->update(req);
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
	kzfree(req);
}

static inline struct ahash_request *ahash_request_cast(
	struct crypto_async_request *req)
{
	return container_of(req, struct ahash_request, base);
}

static inline void ahash_request_set_callback(struct ahash_request *req,
					      u32 flags,
					      crypto_completion_t compl,
					      void *data)
{
	req->base.complete = compl;
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

static inline unsigned int crypto_shash_statesize(struct crypto_shash *tfm)
{
	return crypto_shash_alg(tfm)->statesize;
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
	return tfm->descsize;
}

static inline void *shash_desc_ctx(struct shash_desc *desc)
{
	return desc->__ctx;
}

int crypto_shash_setkey(struct crypto_shash *tfm, const u8 *key,
			unsigned int keylen);
int crypto_shash_digest(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out);

static inline int crypto_shash_export(struct shash_desc *desc, void *out)
{
	return crypto_shash_alg(desc->tfm)->export(desc, out);
}

static inline int crypto_shash_import(struct shash_desc *desc, const void *in)
{
	return crypto_shash_alg(desc->tfm)->import(desc, in);
}

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
