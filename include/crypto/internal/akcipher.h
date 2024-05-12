/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Public Key Encryption
 *
 * Copyright (c) 2015, Intel Corporation
 * Authors: Tadeusz Struk <tadeusz.struk@intel.com>
 */
#ifndef _CRYPTO_AKCIPHER_INT_H
#define _CRYPTO_AKCIPHER_INT_H
#include <crypto/akcipher.h>
#include <crypto/algapi.h>

struct akcipher_instance {
	void (*free)(struct akcipher_instance *inst);
	union {
		struct {
			char head[offsetof(struct akcipher_alg, base)];
			struct crypto_instance base;
		} s;
		struct akcipher_alg alg;
	};
};

struct crypto_akcipher_spawn {
	struct crypto_spawn base;
};

/*
 * Transform internal helpers.
 */
static inline void *akcipher_request_ctx(struct akcipher_request *req)
{
	return req->__ctx;
}

static inline void *akcipher_request_ctx_dma(struct akcipher_request *req)
{
	unsigned int align = crypto_dma_align();

	if (align <= crypto_tfm_ctx_alignment())
		align = 1;

	return PTR_ALIGN(akcipher_request_ctx(req), align);
}

static inline void akcipher_set_reqsize(struct crypto_akcipher *akcipher,
					unsigned int reqsize)
{
	akcipher->reqsize = reqsize;
}

static inline void akcipher_set_reqsize_dma(struct crypto_akcipher *akcipher,
					    unsigned int reqsize)
{
	reqsize += crypto_dma_align() & ~(crypto_tfm_ctx_alignment() - 1);
	akcipher->reqsize = reqsize;
}

static inline void *akcipher_tfm_ctx(struct crypto_akcipher *tfm)
{
	return crypto_tfm_ctx(&tfm->base);
}

static inline void *akcipher_tfm_ctx_dma(struct crypto_akcipher *tfm)
{
	return crypto_tfm_ctx_dma(&tfm->base);
}

static inline void akcipher_request_complete(struct akcipher_request *req,
					     int err)
{
	crypto_request_complete(&req->base, err);
}

static inline const char *akcipher_alg_name(struct crypto_akcipher *tfm)
{
	return crypto_akcipher_tfm(tfm)->__crt_alg->cra_name;
}

static inline struct crypto_instance *akcipher_crypto_instance(
		struct akcipher_instance *inst)
{
	return container_of(&inst->alg.base, struct crypto_instance, alg);
}

static inline struct akcipher_instance *akcipher_instance(
		struct crypto_instance *inst)
{
	return container_of(&inst->alg, struct akcipher_instance, alg.base);
}

static inline struct akcipher_instance *akcipher_alg_instance(
		struct crypto_akcipher *akcipher)
{
	return akcipher_instance(crypto_tfm_alg_instance(&akcipher->base));
}

static inline void *akcipher_instance_ctx(struct akcipher_instance *inst)
{
	return crypto_instance_ctx(akcipher_crypto_instance(inst));
}

int crypto_grab_akcipher(struct crypto_akcipher_spawn *spawn,
			 struct crypto_instance *inst,
			 const char *name, u32 type, u32 mask);

static inline struct crypto_akcipher *crypto_spawn_akcipher(
		struct crypto_akcipher_spawn *spawn)
{
	return crypto_spawn_tfm2(&spawn->base);
}

static inline void crypto_drop_akcipher(struct crypto_akcipher_spawn *spawn)
{
	crypto_drop_spawn(&spawn->base);
}

static inline struct akcipher_alg *crypto_spawn_akcipher_alg(
		struct crypto_akcipher_spawn *spawn)
{
	return container_of(spawn->base.alg, struct akcipher_alg, base);
}

/**
 * crypto_register_akcipher() -- Register public key algorithm
 *
 * Function registers an implementation of a public key verify algorithm
 *
 * @alg:	algorithm definition
 *
 * Return: zero on success; error code in case of error
 */
int crypto_register_akcipher(struct akcipher_alg *alg);

/**
 * crypto_unregister_akcipher() -- Unregister public key algorithm
 *
 * Function unregisters an implementation of a public key verify algorithm
 *
 * @alg:	algorithm definition
 */
void crypto_unregister_akcipher(struct akcipher_alg *alg);

/**
 * akcipher_register_instance() -- Unregister public key template instance
 *
 * Function registers an implementation of an asymmetric key algorithm
 * created from a template
 *
 * @tmpl:	the template from which the algorithm was created
 * @inst:	the template instance
 */
int akcipher_register_instance(struct crypto_template *tmpl,
		struct akcipher_instance *inst);
#endif
