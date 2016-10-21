/*
 * Asynchronous Compression operations
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Weigang Li <weigang.li@intel.com>
 *          Giovanni Cabiddu <giovanni.cabiddu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef _CRYPTO_ACOMP_INT_H
#define _CRYPTO_ACOMP_INT_H
#include <crypto/acompress.h>

/*
 * Transform internal helpers.
 */
static inline void *acomp_request_ctx(struct acomp_req *req)
{
	return req->__ctx;
}

static inline void *acomp_tfm_ctx(struct crypto_acomp *tfm)
{
	return tfm->base.__crt_ctx;
}

static inline void acomp_request_complete(struct acomp_req *req,
					  int err)
{
	req->base.complete(&req->base, err);
}

static inline const char *acomp_alg_name(struct crypto_acomp *tfm)
{
	return crypto_acomp_tfm(tfm)->__crt_alg->cra_name;
}

/**
 * crypto_register_acomp() -- Register asynchronous compression algorithm
 *
 * Function registers an implementation of an asynchronous
 * compression algorithm
 *
 * @alg:	algorithm definition
 *
 * Return:	zero on success; error code in case of error
 */
int crypto_register_acomp(struct acomp_alg *alg);

/**
 * crypto_unregister_acomp() -- Unregister asynchronous compression algorithm
 *
 * Function unregisters an implementation of an asynchronous
 * compression algorithm
 *
 * @alg:	algorithm definition
 *
 * Return:	zero on success; error code in case of error
 */
int crypto_unregister_acomp(struct acomp_alg *alg);

#endif
