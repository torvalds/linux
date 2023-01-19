/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Key-agreement Protocol Primitives (KPP)
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 */
#ifndef _CRYPTO_KPP_INT_H
#define _CRYPTO_KPP_INT_H
#include <crypto/kpp.h>
#include <crypto/algapi.h>

/**
 * struct kpp_instance - KPP template instance
 * @free: Callback getting invoked upon instance destruction. Must be set.
 * @s: Internal. Generic crypto core instance state properly layout
 *     to alias with @alg as needed.
 * @alg: The &struct kpp_alg implementation provided by the instance.
 */
struct kpp_instance {
	void (*free)(struct kpp_instance *inst);
	union {
		struct {
			char head[offsetof(struct kpp_alg, base)];
			struct crypto_instance base;
		} s;
		struct kpp_alg alg;
	};
};

/**
 * struct crypto_kpp_spawn - KPP algorithm spawn
 * @base: Internal. Generic crypto core spawn state.
 *
 * Template instances can get a hold on some inner KPP algorithm by
 * binding a &struct crypto_kpp_spawn via
 * crypto_grab_kpp(). Transforms may subsequently get instantiated
 * from the referenced inner &struct kpp_alg by means of
 * crypto_spawn_kpp().
 */
struct crypto_kpp_spawn {
	struct crypto_spawn base;
};

/*
 * Transform internal helpers.
 */
static inline void *kpp_request_ctx(struct kpp_request *req)
{
	return req->__ctx;
}

static inline void *kpp_request_ctx_dma(struct kpp_request *req)
{
	unsigned int align = crypto_dma_align();

	if (align <= crypto_tfm_ctx_alignment())
		align = 1;

	return PTR_ALIGN(kpp_request_ctx(req), align);
}

static inline void kpp_set_reqsize(struct crypto_kpp *kpp,
				   unsigned int reqsize)
{
	kpp->reqsize = reqsize;
}

static inline void kpp_set_reqsize_dma(struct crypto_kpp *kpp,
				       unsigned int reqsize)
{
	reqsize += crypto_dma_align() & ~(crypto_tfm_ctx_alignment() - 1);
	kpp->reqsize = reqsize;
}

static inline void *kpp_tfm_ctx(struct crypto_kpp *tfm)
{
	return crypto_tfm_ctx(&tfm->base);
}

static inline void *kpp_tfm_ctx_dma(struct crypto_kpp *tfm)
{
	return crypto_tfm_ctx_dma(&tfm->base);
}

static inline void kpp_request_complete(struct kpp_request *req, int err)
{
	req->base.complete(&req->base, err);
}

static inline const char *kpp_alg_name(struct crypto_kpp *tfm)
{
	return crypto_kpp_tfm(tfm)->__crt_alg->cra_name;
}

/*
 * Template instance internal helpers.
 */
/**
 * kpp_crypto_instance() - Cast a &struct kpp_instance to the corresponding
 *                         generic &struct crypto_instance.
 * @inst: Pointer to the &struct kpp_instance to be cast.
 * Return: A pointer to the &struct crypto_instance embedded in @inst.
 */
static inline struct crypto_instance *kpp_crypto_instance(
	struct kpp_instance *inst)
{
	return &inst->s.base;
}

/**
 * kpp_instance() - Cast a generic &struct crypto_instance to the corresponding
 *                  &struct kpp_instance.
 * @inst: Pointer to the &struct crypto_instance to be cast.
 * Return: A pointer to the &struct kpp_instance @inst is embedded in.
 */
static inline struct kpp_instance *kpp_instance(struct crypto_instance *inst)
{
	return container_of(inst, struct kpp_instance, s.base);
}

/**
 * kpp_alg_instance() - Get the &struct kpp_instance a given KPP transform has
 *                      been instantiated from.
 * @kpp: The KPP transform instantiated from some &struct kpp_instance.
 * Return: The &struct kpp_instance associated with @kpp.
 */
static inline struct kpp_instance *kpp_alg_instance(struct crypto_kpp *kpp)
{
	return kpp_instance(crypto_tfm_alg_instance(&kpp->base));
}

/**
 * kpp_instance_ctx() - Get a pointer to a &struct kpp_instance's implementation
 *                      specific context data.
 * @inst: The &struct kpp_instance whose context data to access.
 *
 * A KPP template implementation may allocate extra memory beyond the
 * end of a &struct kpp_instance instantiated from &crypto_template.create().
 * This function provides a means to obtain a pointer to this area.
 *
 * Return: A pointer to the implementation specific context data.
 */
static inline void *kpp_instance_ctx(struct kpp_instance *inst)
{
	return crypto_instance_ctx(kpp_crypto_instance(inst));
}

/*
 * KPP algorithm (un)registration functions.
 */
/**
 * crypto_register_kpp() -- Register key-agreement protocol primitives algorithm
 *
 * Function registers an implementation of a key-agreement protocol primitive
 * algorithm
 *
 * @alg:	algorithm definition
 *
 * Return: zero on success; error code in case of error
 */
int crypto_register_kpp(struct kpp_alg *alg);

/**
 * crypto_unregister_kpp() -- Unregister key-agreement protocol primitive
 * algorithm
 *
 * Function unregisters an implementation of a key-agreement protocol primitive
 * algorithm
 *
 * @alg:	algorithm definition
 */
void crypto_unregister_kpp(struct kpp_alg *alg);

/**
 * kpp_register_instance() - Register a KPP template instance.
 * @tmpl: The instantiating template.
 * @inst: The KPP template instance to be registered.
 * Return: %0 on success, negative error code otherwise.
 */
int kpp_register_instance(struct crypto_template *tmpl,
			  struct kpp_instance *inst);

/*
 * KPP spawn related functions.
 */
/**
 * crypto_grab_kpp() - Look up a KPP algorithm and bind a spawn to it.
 * @spawn: The KPP spawn to bind.
 * @inst: The template instance owning @spawn.
 * @name: The KPP algorithm name to look up.
 * @type: The type bitset to pass on to the lookup.
 * @mask: The mask bismask to pass on to the lookup.
 * Return: %0 on success, a negative error code otherwise.
 */
int crypto_grab_kpp(struct crypto_kpp_spawn *spawn,
		    struct crypto_instance *inst,
		    const char *name, u32 type, u32 mask);

/**
 * crypto_drop_kpp() - Release a spawn previously bound via crypto_grab_kpp().
 * @spawn: The spawn to release.
 */
static inline void crypto_drop_kpp(struct crypto_kpp_spawn *spawn)
{
	crypto_drop_spawn(&spawn->base);
}

/**
 * crypto_spawn_kpp_alg() - Get the algorithm a KPP spawn has been bound to.
 * @spawn: The spawn to get the referenced &struct kpp_alg for.
 *
 * This function as well as the returned result are safe to use only
 * after @spawn has been successfully bound via crypto_grab_kpp() and
 * up to until the template instance owning @spawn has either been
 * registered successfully or the spawn has been released again via
 * crypto_drop_spawn().
 *
 * Return: A pointer to the &struct kpp_alg referenced from the spawn.
 */
static inline struct kpp_alg *crypto_spawn_kpp_alg(
	struct crypto_kpp_spawn *spawn)
{
	return container_of(spawn->base.alg, struct kpp_alg, base);
}

/**
 * crypto_spawn_kpp() - Create a transform from a KPP spawn.
 * @spawn: The spawn previously bound to some &struct kpp_alg via
 *         crypto_grab_kpp().
 *
 * Once a &struct crypto_kpp_spawn has been successfully bound to a
 * &struct kpp_alg via crypto_grab_kpp(), transforms for the latter
 * may get instantiated from the former by means of this function.
 *
 * Return: A pointer to the freshly created KPP transform on success
 * or an ``ERR_PTR()`` otherwise.
 */
static inline struct crypto_kpp *crypto_spawn_kpp(
	struct crypto_kpp_spawn *spawn)
{
	return crypto_spawn_tfm2(&spawn->base);
}

#endif
