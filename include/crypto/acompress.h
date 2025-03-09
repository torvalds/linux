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
#include <linux/compiler_types.h>
#include <linux/container_of.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

#define CRYPTO_ACOMP_ALLOC_OUTPUT	0x00000001

/* Set this bit if source is virtual address instead of SG list. */
#define CRYPTO_ACOMP_REQ_SRC_VIRT	0x00000002

/* Set this bit for if virtual address source cannot be used for DMA. */
#define CRYPTO_ACOMP_REQ_SRC_NONDMA	0x00000004

/* Set this bit if destination is virtual address instead of SG list. */
#define CRYPTO_ACOMP_REQ_DST_VIRT	0x00000008

/* Set this bit for if virtual address destination cannot be used for DMA. */
#define CRYPTO_ACOMP_REQ_DST_NONDMA	0x00000010

#define CRYPTO_ACOMP_DST_MAX		131072

struct acomp_req;

struct acomp_req_chain {
	struct list_head head;
	struct acomp_req *req0;
	struct acomp_req *cur;
	int (*op)(struct acomp_req *req);
	crypto_completion_t compl;
	void *data;
	struct scatterlist ssg;
	struct scatterlist dsg;
	const u8 *src;
	u8 *dst;
};

/**
 * struct acomp_req - asynchronous (de)compression request
 *
 * @base:	Common attributes for asynchronous crypto requests
 * @src:	Source Data
 * @dst:	Destination data
 * @slen:	Size of the input buffer
 * @dlen:	Size of the output buffer and number of bytes produced
 * @chain:	Private API code data, do not use
 * @__ctx:	Start of private context data
 */
struct acomp_req {
	struct crypto_async_request base;
	union {
		struct scatterlist *src;
		const u8 *svirt;
	};
	union {
		struct scatterlist *dst;
		u8 *dvirt;
	};
	unsigned int slen;
	unsigned int dlen;

	struct acomp_req_chain chain;

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

struct crypto_acomp_stream {
	spinlock_t lock;
	void *ctx;
};

#define COMP_ALG_COMMON {			\
	struct crypto_alg base;			\
	struct crypto_acomp_stream __percpu *stream;	\
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
static inline struct acomp_req *acomp_request_alloc_noprof(struct crypto_acomp *tfm)
{
	struct acomp_req *req;

	req = kzalloc_noprof(sizeof(*req) + crypto_acomp_reqsize(tfm), GFP_KERNEL);
	if (likely(req))
		acomp_request_set_tfm(req, tfm);
	return req;
}
#define acomp_request_alloc(...)	alloc_hooks(acomp_request_alloc_noprof(__VA_ARGS__))

/**
 * acomp_request_free() -- zeroize and free asynchronous (de)compression
 *			   request as well as the output buffer if allocated
 *			   inside the algorithm
 *
 * @req:	request to free
 */
static inline void acomp_request_free(struct acomp_req *req)
{
	kfree_sensitive(req);
}

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
	u32 keep = CRYPTO_ACOMP_ALLOC_OUTPUT | CRYPTO_ACOMP_REQ_SRC_VIRT |
		   CRYPTO_ACOMP_REQ_SRC_NONDMA | CRYPTO_ACOMP_REQ_DST_VIRT |
		   CRYPTO_ACOMP_REQ_DST_NONDMA;

	req->base.complete = cmpl;
	req->base.data = data;
	req->base.flags &= keep;
	req->base.flags |= flgs & ~keep;

	crypto_reqchain_init(&req->base);
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

	req->base.flags &= ~(CRYPTO_ACOMP_ALLOC_OUTPUT |
			     CRYPTO_ACOMP_REQ_SRC_VIRT |
			     CRYPTO_ACOMP_REQ_SRC_NONDMA |
			     CRYPTO_ACOMP_REQ_DST_VIRT |
			     CRYPTO_ACOMP_REQ_DST_NONDMA);
	if (!req->dst)
		req->base.flags |= CRYPTO_ACOMP_ALLOC_OUTPUT;
}

/**
 * acomp_request_set_src_sg() -- Sets source scatterlist
 *
 * Sets source scatterlist required by an acomp operation.
 *
 * @req:	asynchronous compress request
 * @src:	pointer to input buffer scatterlist
 * @slen:	size of the input buffer
 */
static inline void acomp_request_set_src_sg(struct acomp_req *req,
					    struct scatterlist *src,
					    unsigned int slen)
{
	req->src = src;
	req->slen = slen;

	req->base.flags &= ~CRYPTO_ACOMP_REQ_SRC_NONDMA;
	req->base.flags &= ~CRYPTO_ACOMP_REQ_SRC_VIRT;
}

/**
 * acomp_request_set_src_dma() -- Sets DMA source virtual address
 *
 * Sets source virtual address required by an acomp operation.
 * The address must be usable for DMA.
 *
 * @req:	asynchronous compress request
 * @src:	virtual address pointer to input buffer
 * @slen:	size of the input buffer
 */
static inline void acomp_request_set_src_dma(struct acomp_req *req,
					     const u8 *src, unsigned int slen)
{
	req->svirt = src;
	req->slen = slen;

	req->base.flags &= ~CRYPTO_ACOMP_REQ_SRC_NONDMA;
	req->base.flags |= CRYPTO_ACOMP_REQ_SRC_VIRT;
}

/**
 * acomp_request_set_src_nondma() -- Sets non-DMA source virtual address
 *
 * Sets source virtual address required by an acomp operation.
 * The address can not be used for DMA.
 *
 * @req:	asynchronous compress request
 * @src:	virtual address pointer to input buffer
 * @slen:	size of the input buffer
 */
static inline void acomp_request_set_src_nondma(struct acomp_req *req,
						const u8 *src,
						unsigned int slen)
{
	req->svirt = src;
	req->slen = slen;

	req->base.flags |= CRYPTO_ACOMP_REQ_SRC_NONDMA;
	req->base.flags |= CRYPTO_ACOMP_REQ_SRC_VIRT;
}

/**
 * acomp_request_set_dst_sg() -- Sets destination scatterlist
 *
 * Sets destination scatterlist required by an acomp operation.
 *
 * @req:	asynchronous compress request
 * @dst:	pointer to output buffer scatterlist
 * @dlen:	size of the output buffer
 */
static inline void acomp_request_set_dst_sg(struct acomp_req *req,
					    struct scatterlist *dst,
					    unsigned int dlen)
{
	req->dst = dst;
	req->dlen = dlen;

	req->base.flags &= ~CRYPTO_ACOMP_REQ_DST_NONDMA;
	req->base.flags &= ~CRYPTO_ACOMP_REQ_DST_VIRT;
}

/**
 * acomp_request_set_dst_dma() -- Sets DMA destination virtual address
 *
 * Sets destination virtual address required by an acomp operation.
 * The address must be usable for DMA.
 *
 * @req:	asynchronous compress request
 * @dst:	virtual address pointer to output buffer
 * @dlen:	size of the output buffer
 */
static inline void acomp_request_set_dst_dma(struct acomp_req *req,
					     u8 *dst, unsigned int dlen)
{
	req->dvirt = dst;
	req->dlen = dlen;

	req->base.flags &= ~CRYPTO_ACOMP_ALLOC_OUTPUT;
	req->base.flags &= ~CRYPTO_ACOMP_REQ_DST_NONDMA;
	req->base.flags |= CRYPTO_ACOMP_REQ_DST_VIRT;
}

/**
 * acomp_request_set_dst_nondma() -- Sets non-DMA destination virtual address
 *
 * Sets destination virtual address required by an acomp operation.
 * The address can not be used for DMA.
 *
 * @req:	asynchronous compress request
 * @dst:	virtual address pointer to output buffer
 * @dlen:	size of the output buffer
 */
static inline void acomp_request_set_dst_nondma(struct acomp_req *req,
						u8 *dst, unsigned int dlen)
{
	req->dvirt = dst;
	req->dlen = dlen;

	req->base.flags &= ~CRYPTO_ACOMP_ALLOC_OUTPUT;
	req->base.flags |= CRYPTO_ACOMP_REQ_DST_NONDMA;
	req->base.flags |= CRYPTO_ACOMP_REQ_DST_VIRT;
}

static inline void acomp_request_chain(struct acomp_req *req,
				       struct acomp_req *head)
{
	crypto_request_chain(&req->base, &head->base);
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
int crypto_acomp_compress(struct acomp_req *req);

/**
 * crypto_acomp_decompress() -- Invoke asynchronous decompress operation
 *
 * Function invokes the asynchronous decompress operation
 *
 * @req:	asynchronous compress request
 *
 * Return:	zero on success; error code in case of error
 */
int crypto_acomp_decompress(struct acomp_req *req);

#endif
