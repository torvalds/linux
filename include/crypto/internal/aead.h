/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AEAD: Authenticated Encryption with Associated Data
 * 
 * Copyright (c) 2007-2015 Herbert Xu <herbert@gondor.apana.org.au>
 */

#ifndef _CRYPTO_INTERNAL_AEAD_H
#define _CRYPTO_INTERNAL_AEAD_H

#include <crypto/aead.h>
#include <crypto/algapi.h>
#include <linux/stddef.h>
#include <linux/types.h>

struct rtattr;

struct aead_instance {
	void (*free)(struct aead_instance *inst);
	union {
		struct {
			char head[offsetof(struct aead_alg, base)];
			struct crypto_instance base;
		} s;
		struct aead_alg alg;
	};
};

struct crypto_aead_spawn {
	struct crypto_spawn base;
};

struct aead_queue {
	struct crypto_queue base;
};

static inline void *crypto_aead_ctx(struct crypto_aead *tfm)
{
	return crypto_tfm_ctx(&tfm->base);
}

static inline void *crypto_aead_ctx_dma(struct crypto_aead *tfm)
{
	return crypto_tfm_ctx_dma(&tfm->base);
}

static inline struct crypto_instance *aead_crypto_instance(
	struct aead_instance *inst)
{
	return container_of(&inst->alg.base, struct crypto_instance, alg);
}

static inline struct aead_instance *aead_instance(struct crypto_instance *inst)
{
	return container_of(&inst->alg, struct aead_instance, alg.base);
}

static inline struct aead_instance *aead_alg_instance(struct crypto_aead *aead)
{
	return aead_instance(crypto_tfm_alg_instance(&aead->base));
}

static inline void *aead_instance_ctx(struct aead_instance *inst)
{
	return crypto_instance_ctx(aead_crypto_instance(inst));
}

static inline void *aead_request_ctx(struct aead_request *req)
{
	return req->__ctx;
}

static inline void *aead_request_ctx_dma(struct aead_request *req)
{
	unsigned int align = crypto_dma_align();

	if (align <= crypto_tfm_ctx_alignment())
		align = 1;

	return PTR_ALIGN(aead_request_ctx(req), align);
}

static inline void aead_request_complete(struct aead_request *req, int err)
{
	req->base.complete(&req->base, err);
}

static inline u32 aead_request_flags(struct aead_request *req)
{
	return req->base.flags;
}

static inline struct aead_request *aead_request_cast(
	struct crypto_async_request *req)
{
	return container_of(req, struct aead_request, base);
}

int crypto_grab_aead(struct crypto_aead_spawn *spawn,
		     struct crypto_instance *inst,
		     const char *name, u32 type, u32 mask);

static inline void crypto_drop_aead(struct crypto_aead_spawn *spawn)
{
	crypto_drop_spawn(&spawn->base);
}

static inline struct aead_alg *crypto_spawn_aead_alg(
	struct crypto_aead_spawn *spawn)
{
	return container_of(spawn->base.alg, struct aead_alg, base);
}

static inline struct crypto_aead *crypto_spawn_aead(
	struct crypto_aead_spawn *spawn)
{
	return crypto_spawn_tfm2(&spawn->base);
}

static inline void crypto_aead_set_reqsize(struct crypto_aead *aead,
					   unsigned int reqsize)
{
	aead->reqsize = reqsize;
}

static inline void crypto_aead_set_reqsize_dma(struct crypto_aead *aead,
					       unsigned int reqsize)
{
	reqsize += crypto_dma_align() & ~(crypto_tfm_ctx_alignment() - 1);
	aead->reqsize = reqsize;
}

static inline void aead_init_queue(struct aead_queue *queue,
				   unsigned int max_qlen)
{
	crypto_init_queue(&queue->base, max_qlen);
}

static inline unsigned int crypto_aead_alg_chunksize(struct aead_alg *alg)
{
	return alg->chunksize;
}

/**
 * crypto_aead_chunksize() - obtain chunk size
 * @tfm: cipher handle
 *
 * The block size is set to one for ciphers such as CCM.  However,
 * you still need to provide incremental updates in multiples of
 * the underlying block size as the IV does not have sub-block
 * granularity.  This is known in this API as the chunk size.
 *
 * Return: chunk size in bytes
 */
static inline unsigned int crypto_aead_chunksize(struct crypto_aead *tfm)
{
	return crypto_aead_alg_chunksize(crypto_aead_alg(tfm));
}

int crypto_register_aead(struct aead_alg *alg);
void crypto_unregister_aead(struct aead_alg *alg);
int crypto_register_aeads(struct aead_alg *algs, int count);
void crypto_unregister_aeads(struct aead_alg *algs, int count);
int aead_register_instance(struct crypto_template *tmpl,
			   struct aead_instance *inst);

#endif	/* _CRYPTO_INTERNAL_AEAD_H */

