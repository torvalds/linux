/*
 * Key-agreement Protocol Primitives (KPP)
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef _CRYPTO_KPP_
#define _CRYPTO_KPP_
#include <linux/crypto.h>

/**
 * struct kpp_request
 *
 * @base:	Common attributes for async crypto requests
 * @src:	Source data
 * @dst:	Destination data
 * @src_len:	Size of the input buffer
 * @dst_len:	Size of the output buffer. It needs to be at least
 *		as big as the expected result depending	on the operation
 *		After operation it will be updated with the actual size of the
 *		result. In case of error where the dst sgl size was insufficient,
 *		it will be updated to the size required for the operation.
 * @__ctx:	Start of private context data
 */
struct kpp_request {
	struct crypto_async_request base;
	struct scatterlist *src;
	struct scatterlist *dst;
	unsigned int src_len;
	unsigned int dst_len;
	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

/**
 * struct crypto_kpp - user-instantiated object which encapsulate
 * algorithms and core processing logic
 *
 * @base:	Common crypto API algorithm data structure
 */
struct crypto_kpp {
	struct crypto_tfm base;
};

/**
 * struct kpp_alg - generic key-agreement protocol primitives
 *
 * @set_secret:		Function invokes the protocol specific function to
 *			store the secret private key along with parameters.
 *			The implementation knows how to decode the buffer
 * @generate_public_key: Function generate the public key to be sent to the
 *			counterpart. In case of error, where output is not big
 *			enough req->dst_len will be updated to the size
 *			required
 * @compute_shared_secret: Function compute the shared secret as defined by
 *			the algorithm. The result is given back to the user.
 *			In case of error, where output is not big enough,
 *			req->dst_len will be updated to the size required
 * @max_size:		Function returns the size of the output buffer
 * @init:		Initialize the object. This is called only once at
 *			instantiation time. In case the cryptographic hardware
 *			needs to be initialized. Software fallback should be
 *			put in place here.
 * @exit:		Undo everything @init did.
 *
 * @reqsize:		Request context size required by algorithm
 *			implementation
 * @base:		Common crypto API algorithm data structure
 */
struct kpp_alg {
	int (*set_secret)(struct crypto_kpp *tfm, const void *buffer,
			  unsigned int len);
	int (*generate_public_key)(struct kpp_request *req);
	int (*compute_shared_secret)(struct kpp_request *req);

	unsigned int (*max_size)(struct crypto_kpp *tfm);

	int (*init)(struct crypto_kpp *tfm);
	void (*exit)(struct crypto_kpp *tfm);

	unsigned int reqsize;
	struct crypto_alg base;
};

/**
 * DOC: Generic Key-agreement Protocol Primitives API
 *
 * The KPP API is used with the algorithm type
 * CRYPTO_ALG_TYPE_KPP (listed as type "kpp" in /proc/crypto)
 */

/**
 * crypto_alloc_kpp() - allocate KPP tfm handle
 * @alg_name: is the name of the kpp algorithm (e.g. "dh", "ecdh")
 * @type: specifies the type of the algorithm
 * @mask: specifies the mask for the algorithm
 *
 * Allocate a handle for kpp algorithm. The returned struct crypto_kpp
 * is required for any following API invocation
 *
 * Return: allocated handle in case of success; IS_ERR() is true in case of
 *	   an error, PTR_ERR() returns the error code.
 */
struct crypto_kpp *crypto_alloc_kpp(const char *alg_name, u32 type, u32 mask);

static inline struct crypto_tfm *crypto_kpp_tfm(struct crypto_kpp *tfm)
{
	return &tfm->base;
}

static inline struct kpp_alg *__crypto_kpp_alg(struct crypto_alg *alg)
{
	return container_of(alg, struct kpp_alg, base);
}

static inline struct crypto_kpp *__crypto_kpp_tfm(struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_kpp, base);
}

static inline struct kpp_alg *crypto_kpp_alg(struct crypto_kpp *tfm)
{
	return __crypto_kpp_alg(crypto_kpp_tfm(tfm)->__crt_alg);
}

static inline unsigned int crypto_kpp_reqsize(struct crypto_kpp *tfm)
{
	return crypto_kpp_alg(tfm)->reqsize;
}

static inline void kpp_request_set_tfm(struct kpp_request *req,
				       struct crypto_kpp *tfm)
{
	req->base.tfm = crypto_kpp_tfm(tfm);
}

static inline struct crypto_kpp *crypto_kpp_reqtfm(struct kpp_request *req)
{
	return __crypto_kpp_tfm(req->base.tfm);
}

static inline u32 crypto_kpp_get_flags(struct crypto_kpp *tfm)
{
	return crypto_tfm_get_flags(crypto_kpp_tfm(tfm));
}

static inline void crypto_kpp_set_flags(struct crypto_kpp *tfm, u32 flags)
{
	crypto_tfm_set_flags(crypto_kpp_tfm(tfm), flags);
}

/**
 * crypto_free_kpp() - free KPP tfm handle
 *
 * @tfm: KPP tfm handle allocated with crypto_alloc_kpp()
 */
static inline void crypto_free_kpp(struct crypto_kpp *tfm)
{
	crypto_destroy_tfm(tfm, crypto_kpp_tfm(tfm));
}

/**
 * kpp_request_alloc() - allocates kpp request
 *
 * @tfm:	KPP tfm handle allocated with crypto_alloc_kpp()
 * @gfp:	allocation flags
 *
 * Return: allocated handle in case of success or NULL in case of an error.
 */
static inline struct kpp_request *kpp_request_alloc(struct crypto_kpp *tfm,
						    gfp_t gfp)
{
	struct kpp_request *req;

	req = kmalloc(sizeof(*req) + crypto_kpp_reqsize(tfm), gfp);
	if (likely(req))
		kpp_request_set_tfm(req, tfm);

	return req;
}

/**
 * kpp_request_free() - zeroize and free kpp request
 *
 * @req:	request to free
 */
static inline void kpp_request_free(struct kpp_request *req)
{
	kzfree(req);
}

/**
 * kpp_request_set_callback() - Sets an asynchronous callback.
 *
 * Callback will be called when an asynchronous operation on a given
 * request is finished.
 *
 * @req:	request that the callback will be set for
 * @flgs:	specify for instance if the operation may backlog
 * @cmpl:	callback which will be called
 * @data:	private data used by the caller
 */
static inline void kpp_request_set_callback(struct kpp_request *req,
					    u32 flgs,
					    crypto_completion_t cmpl,
					    void *data)
{
	req->base.complete = cmpl;
	req->base.data = data;
	req->base.flags = flgs;
}

/**
 * kpp_request_set_input() - Sets input buffer
 *
 * Sets parameters required by generate_public_key
 *
 * @req:	kpp request
 * @input:	ptr to input scatter list
 * @input_len:	size of the input scatter list
 */
static inline void kpp_request_set_input(struct kpp_request *req,
					 struct scatterlist *input,
					 unsigned int input_len)
{
	req->src = input;
	req->src_len = input_len;
}

/**
 * kpp_request_set_output() - Sets output buffer
 *
 * Sets parameters required by kpp operation
 *
 * @req:	kpp request
 * @output:	ptr to output scatter list
 * @output_len:	size of the output scatter list
 */
static inline void kpp_request_set_output(struct kpp_request *req,
					  struct scatterlist *output,
					  unsigned int output_len)
{
	req->dst = output;
	req->dst_len = output_len;
}

enum {
	CRYPTO_KPP_SECRET_TYPE_UNKNOWN,
	CRYPTO_KPP_SECRET_TYPE_DH,
	CRYPTO_KPP_SECRET_TYPE_ECDH,
};

/**
 * struct kpp_secret - small header for packing secret buffer
 *
 * @type:	define type of secret. Each kpp type will define its own
 * @len:	specify the len of the secret, include the header, that
 *		follows the struct
 */
struct kpp_secret {
	unsigned short type;
	unsigned short len;
};

/**
 * crypto_kpp_set_secret() - Invoke kpp operation
 *
 * Function invokes the specific kpp operation for a given alg.
 *
 * @tfm:	tfm handle
 * @buffer:	Buffer holding the packet representation of the private
 *		key. The structure of the packet key depends on the particular
 *		KPP implementation. Packing and unpacking helpers are provided
 *		for ECDH and DH (see the respective header files for those
 *		implementations).
 * @len:	Length of the packet private key buffer.
 *
 * Return: zero on success; error code in case of error
 */
static inline int crypto_kpp_set_secret(struct crypto_kpp *tfm,
					const void *buffer, unsigned int len)
{
	struct kpp_alg *alg = crypto_kpp_alg(tfm);

	return alg->set_secret(tfm, buffer, len);
}

/**
 * crypto_kpp_generate_public_key() - Invoke kpp operation
 *
 * Function invokes the specific kpp operation for generating the public part
 * for a given kpp algorithm.
 *
 * To generate a private key, the caller should use a random number generator.
 * The output of the requested length serves as the private key.
 *
 * @req:	kpp key request
 *
 * Return: zero on success; error code in case of error
 */
static inline int crypto_kpp_generate_public_key(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct kpp_alg *alg = crypto_kpp_alg(tfm);

	return alg->generate_public_key(req);
}

/**
 * crypto_kpp_compute_shared_secret() - Invoke kpp operation
 *
 * Function invokes the specific kpp operation for computing the shared secret
 * for a given kpp algorithm.
 *
 * @req:	kpp key request
 *
 * Return: zero on success; error code in case of error
 */
static inline int crypto_kpp_compute_shared_secret(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct kpp_alg *alg = crypto_kpp_alg(tfm);

	return alg->compute_shared_secret(req);
}

/**
 * crypto_kpp_maxsize() - Get len for output buffer
 *
 * Function returns the output buffer size required for a given key.
 * Function assumes that the key is already set in the transformation. If this
 * function is called without a setkey or with a failed setkey, you will end up
 * in a NULL dereference.
 *
 * @tfm:	KPP tfm handle allocated with crypto_alloc_kpp()
 */
static inline unsigned int crypto_kpp_maxsize(struct crypto_kpp *tfm)
{
	struct kpp_alg *alg = crypto_kpp_alg(tfm);

	return alg->max_size(tfm);
}

#endif
