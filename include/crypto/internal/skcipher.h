/*
 * Symmetric key ciphers.
 * 
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#ifndef _CRYPTO_INTERNAL_SKCIPHER_H
#define _CRYPTO_INTERNAL_SKCIPHER_H

#include <crypto/algapi.h>
#include <crypto/skcipher.h>
#include <linux/list.h>
#include <linux/types.h>

struct aead_request;
struct rtattr;

struct skcipher_instance {
	void (*free)(struct skcipher_instance *inst);
	union {
		struct {
			char head[offsetof(struct skcipher_alg, base)];
			struct crypto_instance base;
		} s;
		struct skcipher_alg alg;
	};
};

struct crypto_skcipher_spawn {
	struct crypto_spawn base;
};

struct skcipher_walk {
	union {
		struct {
			struct page *page;
			unsigned long offset;
		} phys;

		struct {
			u8 *page;
			void *addr;
		} virt;
	} src, dst;

	struct scatter_walk in;
	unsigned int nbytes;

	struct scatter_walk out;
	unsigned int total;

	struct list_head buffers;

	u8 *page;
	u8 *buffer;
	u8 *oiv;
	void *iv;

	unsigned int ivsize;

	int flags;
	unsigned int blocksize;
	unsigned int stride;
	unsigned int alignmask;
};

static inline struct crypto_instance *skcipher_crypto_instance(
	struct skcipher_instance *inst)
{
	return &inst->s.base;
}

static inline struct skcipher_instance *skcipher_alg_instance(
	struct crypto_skcipher *skcipher)
{
	return container_of(crypto_skcipher_alg(skcipher),
			    struct skcipher_instance, alg);
}

static inline void *skcipher_instance_ctx(struct skcipher_instance *inst)
{
	return crypto_instance_ctx(skcipher_crypto_instance(inst));
}

static inline void skcipher_request_complete(struct skcipher_request *req, int err)
{
	req->base.complete(&req->base, err);
}

static inline void crypto_set_skcipher_spawn(
	struct crypto_skcipher_spawn *spawn, struct crypto_instance *inst)
{
	crypto_set_spawn(&spawn->base, inst);
}

int crypto_grab_skcipher(struct crypto_skcipher_spawn *spawn, const char *name,
			 u32 type, u32 mask);

static inline void crypto_drop_skcipher(struct crypto_skcipher_spawn *spawn)
{
	crypto_drop_spawn(&spawn->base);
}

static inline struct skcipher_alg *crypto_skcipher_spawn_alg(
	struct crypto_skcipher_spawn *spawn)
{
	return container_of(spawn->base.alg, struct skcipher_alg, base);
}

static inline struct skcipher_alg *crypto_spawn_skcipher_alg(
	struct crypto_skcipher_spawn *spawn)
{
	return crypto_skcipher_spawn_alg(spawn);
}

static inline struct crypto_skcipher *crypto_spawn_skcipher(
	struct crypto_skcipher_spawn *spawn)
{
	return crypto_spawn_tfm2(&spawn->base);
}

static inline void crypto_skcipher_set_reqsize(
	struct crypto_skcipher *skcipher, unsigned int reqsize)
{
	skcipher->reqsize = reqsize;
}

int crypto_register_skcipher(struct skcipher_alg *alg);
void crypto_unregister_skcipher(struct skcipher_alg *alg);
int crypto_register_skciphers(struct skcipher_alg *algs, int count);
void crypto_unregister_skciphers(struct skcipher_alg *algs, int count);
int skcipher_register_instance(struct crypto_template *tmpl,
			       struct skcipher_instance *inst);

int skcipher_walk_done(struct skcipher_walk *walk, int err);
int skcipher_walk_virt(struct skcipher_walk *walk,
		       struct skcipher_request *req,
		       bool atomic);
void skcipher_walk_atomise(struct skcipher_walk *walk);
int skcipher_walk_async(struct skcipher_walk *walk,
			struct skcipher_request *req);
int skcipher_walk_aead(struct skcipher_walk *walk, struct aead_request *req,
		       bool atomic);
int skcipher_walk_aead_encrypt(struct skcipher_walk *walk,
			       struct aead_request *req, bool atomic);
int skcipher_walk_aead_decrypt(struct skcipher_walk *walk,
			       struct aead_request *req, bool atomic);
void skcipher_walk_complete(struct skcipher_walk *walk, int err);

static inline void ablkcipher_request_complete(struct ablkcipher_request *req,
					       int err)
{
	req->base.complete(&req->base, err);
}

static inline u32 ablkcipher_request_flags(struct ablkcipher_request *req)
{
	return req->base.flags;
}

static inline void *crypto_skcipher_ctx(struct crypto_skcipher *tfm)
{
	return crypto_tfm_ctx(&tfm->base);
}

static inline void *skcipher_request_ctx(struct skcipher_request *req)
{
	return req->__ctx;
}

static inline u32 skcipher_request_flags(struct skcipher_request *req)
{
	return req->base.flags;
}

static inline unsigned int crypto_skcipher_alg_min_keysize(
	struct skcipher_alg *alg)
{
	if ((alg->base.cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_BLKCIPHER)
		return alg->base.cra_blkcipher.min_keysize;

	if (alg->base.cra_ablkcipher.encrypt)
		return alg->base.cra_ablkcipher.min_keysize;

	return alg->min_keysize;
}

static inline unsigned int crypto_skcipher_alg_max_keysize(
	struct skcipher_alg *alg)
{
	if ((alg->base.cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_BLKCIPHER)
		return alg->base.cra_blkcipher.max_keysize;

	if (alg->base.cra_ablkcipher.encrypt)
		return alg->base.cra_ablkcipher.max_keysize;

	return alg->max_keysize;
}

static inline unsigned int crypto_skcipher_alg_chunksize(
	struct skcipher_alg *alg)
{
	if ((alg->base.cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_BLKCIPHER)
		return alg->base.cra_blocksize;

	if (alg->base.cra_ablkcipher.encrypt)
		return alg->base.cra_blocksize;

	return alg->chunksize;
}

static inline unsigned int crypto_skcipher_alg_walksize(
	struct skcipher_alg *alg)
{
	if ((alg->base.cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_BLKCIPHER)
		return alg->base.cra_blocksize;

	if (alg->base.cra_ablkcipher.encrypt)
		return alg->base.cra_blocksize;

	return alg->walksize;
}

/**
 * crypto_skcipher_chunksize() - obtain chunk size
 * @tfm: cipher handle
 *
 * The block size is set to one for ciphers such as CTR.  However,
 * you still need to provide incremental updates in multiples of
 * the underlying block size as the IV does not have sub-block
 * granularity.  This is known in this API as the chunk size.
 *
 * Return: chunk size in bytes
 */
static inline unsigned int crypto_skcipher_chunksize(
	struct crypto_skcipher *tfm)
{
	return crypto_skcipher_alg_chunksize(crypto_skcipher_alg(tfm));
}

/**
 * crypto_skcipher_walksize() - obtain walk size
 * @tfm: cipher handle
 *
 * In some cases, algorithms can only perform optimally when operating on
 * multiple blocks in parallel. This is reflected by the walksize, which
 * must be a multiple of the chunksize (or equal if the concern does not
 * apply)
 *
 * Return: walk size in bytes
 */
static inline unsigned int crypto_skcipher_walksize(
	struct crypto_skcipher *tfm)
{
	return crypto_skcipher_alg_walksize(crypto_skcipher_alg(tfm));
}

/* Helpers for simple block cipher modes of operation */
struct skcipher_ctx_simple {
	struct crypto_cipher *cipher;	/* underlying block cipher */
};
static inline struct crypto_cipher *
skcipher_cipher_simple(struct crypto_skcipher *tfm)
{
	struct skcipher_ctx_simple *ctx = crypto_skcipher_ctx(tfm);

	return ctx->cipher;
}
struct skcipher_instance *
skcipher_alloc_instance_simple(struct crypto_template *tmpl, struct rtattr **tb,
			       struct crypto_alg **cipher_alg_ret);

#endif	/* _CRYPTO_INTERNAL_SKCIPHER_H */

