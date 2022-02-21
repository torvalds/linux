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

/*
 * Transform internal helpers.
 */
static inline void *kpp_request_ctx(struct kpp_request *req)
{
	return req->__ctx;
}

static inline void *kpp_tfm_ctx(struct crypto_kpp *tfm)
{
	return tfm->base.__crt_ctx;
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

#endif
