/*
 * Public Key Encryption
 *
 * Copyright (c) 2015, Intel Corporation
 * Authors: Tadeusz Struk <tadeusz.struk@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef _CRYPTO_AKCIPHER_INT_H
#define _CRYPTO_AKCIPHER_INT_H
#include <crypto/akcipher.h>

/*
 * Transform internal helpers.
 */
static inline void *akcipher_request_ctx(struct akcipher_request *req)
{
	return req->__ctx;
}

static inline void *akcipher_tfm_ctx(struct crypto_akcipher *tfm)
{
	return tfm->base.__crt_ctx;
}

static inline void akcipher_request_complete(struct akcipher_request *req,
					     int err)
{
	req->base.complete(&req->base, err);
}

static inline const char *akcipher_alg_name(struct crypto_akcipher *tfm)
{
	return crypto_akcipher_tfm(tfm)->__crt_alg->cra_name;
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
#endif
