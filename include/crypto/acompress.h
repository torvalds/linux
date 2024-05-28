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

#include <linux/atomic.h>
#include <linux/container_of.h>
#include <linux/crypto.h>

#define CRYPTO_ACOMP_ALLOC_OUTPUT	0x00000001
#define CRYPTO_ACOMP_DST_MAX		131072

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

/*
 * struct crypto_istat_compress - statistics for compress algorithm
 * @compress_cnt:	number of compress requests
 * @compress_tlen:	total data size handled by compress requests
 * @decompress_cnt:	number of decompress requests
 * @decompress_tlen:	total data size handled by decompress requests
 * @err_cnt:		number of error for compress requests
 */
struct crypto_istat_compress {
	atomic64_t compress_cnt;
	atomic64_t compress_tlen;
	atomic64_t decompress_cnt;
	atomic64_t decompress_tlen;
	atomic64_t err_cnt;
};

#ifdef CONFIG_CRYPTO_STATS
#define COMP_ALG_COMMON_STATS struct crypto_istat_compress stat;
#else
#define COMP_ALG_COMMON_STATS
#endif

#define COMP_ALG_COMMON {			\
	COMP_ALG_COMMON_STATS			\
						\
	struct crypto_alg base;			\
}
struct comp_alg_common COMP_ALG_COMMON;

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
/**
 * crypto_alloc_acomp_node() -- allocate ACOMPRESS tfm handle with desired NUMA node
 * @alg_name:	is the cra_name / name or cra_driver_name / driver name of the
 *		compression algorithm e.g. "deflate"
 * @type:	specifies the type of the algorithm
 * @mask:	specifies the mask for the algorithm
 * @node:	specifies the NUMA node the ZIP hardware belongs to
 *
 * Allocate a handle for a compression algorithm. Drivers should try to use
 * (de)compressors on the specified NUMA node.
 * The returned struct crypto_acomp is the handle that is required for any
 * subsequent API invocation for the compression operations.
 *
 * Return:	allocated handle in case of success; IS_ERR() is true in case
 *		of an error, PTR_ERR() returns the error code.
 */
struct crypto_acomp *crypto_alloc_acomp_node(const char *alg_name, u32 type,
					u32 mask, int node);

static inline struct crypto_tfm *crypto_acomp_tfm(struct crypto_acomp *tfm)
{
	return &tfm->base;
}

static inline struct comp_alg_common *__crypto_comp_alg_common(
	struct crypto_alg *alg)
{
	return container_of(alg, struct comp_alg_common, base);
}

static inline struct crypto_acomp *__crypto_acomp_tfm(struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_acomp, base);
}

static inline struct comp_alg_common *crypto_comp_alg_common(
	struct crypto_acomp *tfm)
{
	return __crypto_comp_alg_common(crypto_acomp_tfm(tfm)->__crt_alg);
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

static inline bool acomp_is_async(struct crypto_acomp *tfm)
{
	return crypto_comp_alg_common(tfm)->base.cra_flags &
	       CRYPTO_ALG_ASYNC;
}

static inline struct crypto_acomp *crypto_acomp_reqtfm(struct acomp_req *req)
{
	return __crypto_acomp_tfm(req->base.tfm);
}

/**
 * crypto_free_acomp() -- free ACOMPRESS tfm handle
 *
 * @tfm:	ACOMPRESS tfm handle allocated with crypto_alloc_acomp()
 *
 * If @tfm is a NULL or error pointer, this function does nothing.
 */
static inline void crypto_free_acomp(struct crypto_acomp *tfm)
{
	crypto_destroy_tfm(tfm, crypto_acomp_tfm(tfm));
}

static inline int crypto_has_acomp(const char *alg_name, u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_ACOMPRESS;
	mask |= CRYPTO_ALG_TYPE_ACOMPRESS_MASK;

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
	req->base.flags &= CRYPTO_ACOMP_ALLOC_OUTPUT;
	req->base.flags |= flgs & ~CRYPTO_ACOMP_ALLOC_OUTPUT;
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

	req->flags &= ~CRYPTO_ACOMP_ALLOC_OUTPUT;
	if (!req->dst)
		req->flags |= CRYPTO_ACOMP_ALLOC_OUTPUT;
}

static inline struct crypto_istat_compress *comp_get_stat(
	struct comp_alg_common *alg)
{
#ifdef CONFIG_CRYPTO_STATS
	return &alg->stat;
#else
	return NULL;
#endif
}

static inline int crypto_comp_errstat(struct comp_alg_common *alg, int err)
{
	if (!IS_ENABLED(CONFIG_CRYPTO_STATS))
		return err;

	if (err && err != -EINPROGRESS && err != -EBUSY)
		atomic64_inc(&comp_get_stat(alg)->err_cnt);

	return err;
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
	struct comp_alg_common *alg;

	alg = crypto_comp_alg_common(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_STATS)) {
		struct crypto_istat_compress *istat = comp_get_stat(alg);

		atomic64_inc(&istat->compress_cnt);
		atomic64_add(req->slen, &istat->compress_tlen);
	}

	return crypto_comp_errstat(alg, tfm->compress(req));
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
	struct comp_alg_common *alg;

	alg = crypto_comp_alg_common(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_STATS)) {
		struct crypto_istat_compress *istat = comp_get_stat(alg);

		atomic64_inc(&istat->decompress_cnt);
		atomic64_add(req->slen, &istat->decompress_tlen);
	}

	return crypto_comp_errstat(alg, tfm->decompress(req));
}

#endif
