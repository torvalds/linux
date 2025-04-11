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

#define ACOMP_REQUEST_ON_STACK(name, tfm) \
        char __##name##_req[sizeof(struct acomp_req) + \
                            MAX_SYNC_COMP_REQSIZE] CRYPTO_MINALIGN_ATTR; \
        struct acomp_req *name = acomp_request_on_stack_init( \
                __##name##_req, (tfm), 0, true)

/**
 * struct acomp_alg - asynchronous compression algorithm
 *
 * @compress:	Function performs a compress operation
 * @decompress:	Function performs a de-compress operation
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
 * @stream:	Per-cpu memory for algorithm
 * @calg:	Cmonn algorithm data structure shared with scomp
 */
struct acomp_alg {
	int (*compress)(struct acomp_req *req);
	int (*decompress)(struct acomp_req *req);
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

static inline bool acomp_request_chained(struct acomp_req *req)
{
	return crypto_request_chained(&req->base);
}

static inline bool acomp_request_issg(struct acomp_req *req)
{
	return !(req->base.flags & (CRYPTO_ACOMP_REQ_SRC_VIRT |
				    CRYPTO_ACOMP_REQ_DST_VIRT |
				    CRYPTO_ACOMP_REQ_SRC_FOLIO |
				    CRYPTO_ACOMP_REQ_DST_FOLIO));
}

static inline bool acomp_request_src_isvirt(struct acomp_req *req)
{
	return req->base.flags & CRYPTO_ACOMP_REQ_SRC_VIRT;
}

static inline bool acomp_request_dst_isvirt(struct acomp_req *req)
{
	return req->base.flags & CRYPTO_ACOMP_REQ_DST_VIRT;
}

static inline bool acomp_request_isvirt(struct acomp_req *req)
{
	return req->base.flags & (CRYPTO_ACOMP_REQ_SRC_VIRT |
				  CRYPTO_ACOMP_REQ_DST_VIRT);
}

static inline bool acomp_request_src_isnondma(struct acomp_req *req)
{
	return req->base.flags & CRYPTO_ACOMP_REQ_SRC_NONDMA;
}

static inline bool acomp_request_dst_isnondma(struct acomp_req *req)
{
	return req->base.flags & CRYPTO_ACOMP_REQ_DST_NONDMA;
}

static inline bool acomp_request_isnondma(struct acomp_req *req)
{
	return req->base.flags & (CRYPTO_ACOMP_REQ_SRC_NONDMA |
				  CRYPTO_ACOMP_REQ_DST_NONDMA);
}

static inline bool acomp_request_src_isfolio(struct acomp_req *req)
{
	return req->base.flags & CRYPTO_ACOMP_REQ_SRC_FOLIO;
}

static inline bool acomp_request_dst_isfolio(struct acomp_req *req)
{
	return req->base.flags & CRYPTO_ACOMP_REQ_DST_FOLIO;
}

static inline bool crypto_acomp_req_chain(struct crypto_acomp *tfm)
{
	return crypto_tfm_req_chain(&tfm->base);
}

#endif
