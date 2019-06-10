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

#endif
