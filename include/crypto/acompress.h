/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Asynchronous Compression operations
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Weigang Li <weigang.li@intel.com>
 *          Giovanni Cabiddu <giovanni.cabiddu@intel.com>
 */
#ifndef _CRYPTO_ACOMP_H
#define _CRYPTO_ACOMP_H
#include <linux/crypto.h>

#define CRYPTO_ACOMP_ALLOC_OUTPUT	0x00000001

/**
 * struct acomp_req - asynchronous (de)compression request
 *
 * @base:	Common attributes for asynchronous crypto requests
 * @src:	Source Data
 * @dst:	Destination data
 * @slen:	Size of the input buffer
 * @dlen:	Size of the output buffer and number of bytes produced
 * @flags:	Internal flags
 * @__ctx:	Start of private context data
 */
struct acomp_req {
	struct crypto_async_request base;
	struct scatterlist *src;
	struct scatterlist *dst;
	unsigned int slen;
	unsigned int dlen;
	u32 flags;
	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

/**
 * struct crypto_acomp - user-instantiated objects which encapsulate
 * algorithms and core processing logic
 *
 * @compress:		Function performs a compress operation
 * @decompress:		Function performs a de-compress operation
 * @dst_free:		Frees destination buffer if allocated inside the
 *			algorithm
 * @reqsize:		Context size for (de)compression requests
 * @base:		Common crypto API algorithm data structure
 */
struct crypto_acomp {
	int (*compress)(struct acomp_req *req);
	int (*decompress)(struct acomp_req *req);
	void (*dst_free)(struct scatterlist *dst);
	unsigned int reqsize;
	struct crypto_tfm base;
};

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
 */
struct acomp_alg {
	int (*compress)(struct acomp_req *req);
	int (*decompress)(struct acomp_req *req);
	void (*dst_free)(struct scatterlist *dst);
	int (*init)(struct crypto_acomp *tfm);
	void (*exit)(struct crypto_acomp *tfm);
	unsigned int reqsize;
	struct crypto_alg base;
};

/**
 * DOC: Asynchronous Compression API
 *
 * The Asynchronous Compression API is used with the algorithms of type
 * CRYPTO_ALG_TYPE_ACOMPRESS (listed as type "acomp" in /proc/crypto)
 */

/**
 * crypto_alloc_acomp() -- allocate ACOMPRESS tfm handle
 * @alg_name:	is the cra_name / name or cra_driver_name / driver name of the
 *		compression algorithm e.g. "deflate"
 * @type:	specifies the type of the algorithm
 * @mask:	specifies the mask for the algorithm
 *
 * Allocate a handle for a compression algorithm. The returned struct
 * crypto_acomp is the handle that is required for any subsequent
 * API invocation for the compression operations.
 *
 * Return:	allocated handle in case of success; IS_ERR() is true in case
 *		of an error, PTR_ERR() returns the error code.
 */
struct crypto_acomp *crypto_alloc_acomp(const char *alg_name, u32 type,
					u32 mask);

static inline struct crypto_tfm *crypto_acomp_tfm(struct crypto_acomp *tfm)
{
	return &tfm->base;
}

static inline struct acomp_alg *__crypto_acomp_alg(struct crypto_alg *alg)
{
	return container_of(alg, struct acomp_alg, base);
}

static inline struct crypto_acomp *__crypto_acomp_tfm(struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_acomp, base);
}

static inline struct acomp_alg *crypto_acomp_alg(struct crypto_acomp *tfm)
{
	return __crypto_acomp_alg(crypto_acomp_tfm(tfm)->__crt_alg);
}

static inline unsigned int crypto_acomp_reqsize(struct crypto_acomp *tfm)
{
	return tfm->reqsize;
}

static inline void acomp_request_set_tfm(struct acomp_req *req,
					 struct crypto_acomp *tfm)
{
	req->base.tfm = crypto_acomp_tfm(tfm);
}

static inline struct crypto_acomp *crypto_acomp_reqtfm(struct acomp_req *req)
{
	return __crypto_acomp_tfm(req->base.tfm);
}

/**
 * crypto_free_acomp() -- free ACOMPRESS tfm handle
 *
 * @tfm:	ACOMPRESS tfm handle allocated with crypto_alloc_acomp()
 */
static inline void crypto_free_acomp(struct crypto_acomp *tfm)
{
	crypto_destroy_tfm(tfm, crypto_acomp_tfm(tfm));
}

static inline int crypto_has_acomp(const char *alg_name, u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_ACOMPRESS;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return crypto_has_alg(alg_name, type, mask);
}

/**
 * acomp_request_alloc() -- allocates asynchronous (de)compression request
 *
 * @tfm:	ACOMPRESS tfm handle allocated with crypto_alloc_acomp()
 *
 * Return:	allocated handle in case of success or NULL in case of an error
 */
struct acomp_req *acomp_request_alloc(struct crypto_acomp *tfm);

/**
 * acomp_request_free() -- zeroize and free asynchronous (de)compression
 *			   request as well as the output buffer if allocated
 *			   inside the algorithm
 *
 * @req:	request to free
 */
void acomp_request_free(struct acomp_req *req);

/**
 * acomp_request_set_callback() -- Sets an asynchronous callback
 *
 * Callback will be called when an asynchronous operation on a given
 * request is finished.
 *
 * @req:	request that the callback will be set for
 * @flgs:	specify for instance if the operation may backlog
 * @cmlp:	callback which will be called
 * @data:	private data used by the caller
 */
static inline void acomp_request_set_callback(struct acomp_req *req,
					      u32 flgs,
					      crypto_completion_t cmpl,
					      void *data)
{
	req->base.complete = cmpl;
	req->base.data = data;
	req->base.flags = flgs;
}

/**
 * acomp_request_set_params() -- Sets request parameters
 *
 * Sets parameters required by an acomp operation
 *
 * @req:	asynchronous compress request
 * @src:	pointer to input buffer scatterlist
 * @dst:	pointer to output buffer scatterlist. If this is NULL, the
 *		acomp layer will allocate the output memory
 * @slen:	size of the input buffer
 * @dlen:	size of the output buffer. If dst is NULL, this can be used by
 *		the user to specify the maximum amount of memory to allocate
 */
static inline void acomp_request_set_params(struct acomp_req *req,
					    struct scatterlist *src,
					    struct scatterlist *dst,
					    unsigned int slen,
					    unsigned int dlen)
{
	req->src = src;
	req->dst = dst;
	req->slen = slen;
	req->dlen = dlen;

	if (!req->dst)
		req->flags |= CRYPTO_ACOMP_ALLOC_OUTPUT;
}

/**
 * crypto_acomp_compress() -- Invoke asynchronous compress operation
 *
 * Function invokes the asynchronous compress operation
 *
 * @req:	asynchronous compress request
 *
 * Return:	zero on success; error code in case of error
 */
static inline int crypto_acomp_compress(struct acomp_req *req)
{
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(req);
	struct crypto_alg *alg = tfm->base.__crt_alg;
	unsigned int slen = req->slen;
	int ret;

	crypto_stats_get(alg);
	ret = tfm->compress(req);
	crypto_stats_compress(slen, ret, alg);
	return ret;
}

/**
 * crypto_acomp_decompress() -- Invoke asynchronous decompress operation
 *
 * Function invokes the asynchronous decompress operation
 *
 * @req:	asynchronous compress request
 *
 * Return:	zero on success; error code in case of error
 */
static inline int crypto_acomp_decompress(struct acomp_req *req)
{
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(req);
	struct crypto_alg *alg = tfm->base.__crt_alg;
	unsigned int slen = req->slen;
	int ret;

	crypto_stats_get(alg);
	ret = tfm->decompress(req);
	crypto_stats_decompress(slen, ret, alg);
	return ret;
}

#endif
