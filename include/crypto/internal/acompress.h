/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Asynchronous Compression operations
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Weigang Li <weigang.li@intel.com>
 *          Giovanni Cabiddu <giovanni.cabiddu@intel.com>
 */
#ifndef _CRYPTO_ACOMP_INT_H
#define _CRYPTO_ACOMP_INT_H

#include <crypto/acompress.h>
#include <crypto/algapi.h>

/**
 * struct acomp_alg - asynchronous compression algorithm
 *
 * @compress:	Function performs a compress operation
 * @decompress:	Function performs a de-compress operation
 * @dst_free:	Frees destination buffer if allocated inside the algorithm
 * @init:	Initialize the cryptographic transformation object.
 *		This function is used to initialize the cryptographic
 *		transformation object. This function is called only once at
 *		the instantiation time, right after the transformation context
 *		was allocated. In case the cryptographic hardware has some
 *		special requirements which need to be handled by software, this
 *		function shall check for the precise requirement of the
 *		transformation and put any software fallbacks in place.
 * @exit:	Deinitialize the cryptographic transformation object. This is a
 *		counterpart to @init, used to remove various changes set in
 *		@init.
 *
 * @reqsize:	Context size for (de)compression requests
 * @base:	Common crypto API algorithm data structure
 * @calg:	Cmonn algorithm data structure shared with scomp
 */
struct acomp_alg {
	int (*compress)(struct acomp_req *req);
	int (*decompress)(struct acomp_req *req);
	void (*dst_free)(struct scatterlist *dst);
	int (*init)(struct crypto_acomp *tfm);
	void (*exit)(struct crypto_acomp *tfm);

	unsigned int reqsize;

	union {
		struct COMP_ALG_COMMON;
		struct comp_alg_common calg;
	};
};

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
	crypto_request_complete(&req->base, err);
}

static inline struct acomp_req *__acomp_request_alloc_noprof(struct crypto_acomp *tfm)
{
	struct acomp_req *req;

	req = kzalloc_noprof(sizeof(*req) + crypto_acomp_reqsize(tfm), GFP_KERNEL);
	if (likely(req))
		acomp_request_set_tfm(req, tfm);
	return req;
}
#define __acomp_request_alloc(...)	alloc_hooks(__acomp_request_alloc_noprof(__VA_ARGS__))

static inline void __acomp_request_free(struct acomp_req *req)
{
	kfree_sensitive(req);
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
 */
void crypto_unregister_acomp(struct acomp_alg *alg);

int crypto_register_acomps(struct acomp_alg *algs, int count);
void crypto_unregister_acomps(struct acomp_alg *algs, int count);

#endif
