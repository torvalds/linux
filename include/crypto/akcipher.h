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
#ifndef _CRYPTO_AKCIPHER_H
#define _CRYPTO_AKCIPHER_H
#include <linux/crypto.h>

/**
 * struct akcipher_request - public key request
 *
 * @base:	Common attributes for async crypto requests
 * @src:	Pointer to memory containing the input parameters
 *		The format of the parameter(s) is expeted to be Octet String
 * @dst:	Pointer to memory whare the result will be stored
 * @src_len:	Size of the input parameter
 * @dst_len:	Size of the output buffer. It needs to be at leaset
 *		as big as the expected result depending	on the operation
 *		After operation it will be updated with the acctual size of the
 *		result. In case of error, where the dst_len was insufficient,
 *		it will be updated to the size required for the operation.
 * @__ctx:	Start of private context data
 */
struct akcipher_request {
	struct crypto_async_request base;
	void *src;
	void *dst;
	unsigned int src_len;
	unsigned int dst_len;
	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

/**
 * struct crypto_akcipher - user-instantiated objects which encapsulate
 * algorithms and core processing logic
 *
 * @base:	Common crypto API algorithm data structure
 */
struct crypto_akcipher {
	struct crypto_tfm base;
};

/**
 * struct akcipher_alg - generic public key algorithm
 *
 * @sign:	Function performs a sign operation as defined by public key
 *		algorithm. In case of error, where the dst_len was insufficient,
 *		the req->dst_len will be updated to the size required for the
 *		operation
 * @verify:	Function performs a sign operation as defined by public key
 *		algorithm. In case of error, where the dst_len was insufficient,
 *		the req->dst_len will be updated to the size required for the
 *		operation
 * @encrypt:	Function performs an encrytp operation as defined by public key
 *		algorithm. In case of error, where the dst_len was insufficient,
 *		the req->dst_len will be updated to the size required for the
 *		operation
 * @decrypt:	Function performs a decrypt operation as defined by public key
 *		algorithm. In case of error, where the dst_len was insufficient,
 *		the req->dst_len will be updated to the size required for the
 *		operation
 * @setkey:	Function invokes the algorithm specific set key function, which
 *		knows how to decode and interpret the BER encoded key
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
 * @reqsize:	Request context size required by algorithm implementation
 * @base:	Common crypto API algorithm data structure
 */
struct akcipher_alg {
	int (*sign)(struct akcipher_request *req);
	int (*verify)(struct akcipher_request *req);
	int (*encrypt)(struct akcipher_request *req);
	int (*decrypt)(struct akcipher_request *req);
	int (*setkey)(struct crypto_akcipher *tfm, const void *key,
		      unsigned int keylen);
	int (*init)(struct crypto_akcipher *tfm);
	void (*exit)(struct crypto_akcipher *tfm);

	unsigned int reqsize;
	struct crypto_alg base;
};

/**
 * DOC: Generic Public Key API
 *
 * The Public Key API is used with the algorithms of type
 * CRYPTO_ALG_TYPE_AKCIPHER (listed as type "akcipher" in /proc/crypto)
 */

/**
 * crypto_alloc_akcipher() -- allocate AKCIPHER tfm handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      public key algorithm e.g. "rsa"
 * @type: specifies the type of the algorithm
 * @mask: specifies the mask for the algorithm
 *
 * Allocate a handle for public key algorithm. The returned struct
 * crypto_akcipher is the handle that is required for any subsequent
 * API invocation for the public key operations.
 *
 * Return: allocated handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
struct crypto_akcipher *crypto_alloc_akcipher(const char *alg_name, u32 type,
					      u32 mask);

static inline struct crypto_tfm *crypto_akcipher_tfm(
	struct crypto_akcipher *tfm)
{
	return &tfm->base;
}

static inline struct akcipher_alg *__crypto_akcipher_alg(struct crypto_alg *alg)
{
	return container_of(alg, struct akcipher_alg, base);
}

static inline struct crypto_akcipher *__crypto_akcipher_tfm(
	struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_akcipher, base);
}

static inline struct akcipher_alg *crypto_akcipher_alg(
	struct crypto_akcipher *tfm)
{
	return __crypto_akcipher_alg(crypto_akcipher_tfm(tfm)->__crt_alg);
}

static inline unsigned int crypto_akcipher_reqsize(struct crypto_akcipher *tfm)
{
	return crypto_akcipher_alg(tfm)->reqsize;
}

static inline void akcipher_request_set_tfm(struct akcipher_request *req,
					    struct crypto_akcipher *tfm)
{
	req->base.tfm = crypto_akcipher_tfm(tfm);
}

static inline struct crypto_akcipher *crypto_akcipher_reqtfm(
	struct akcipher_request *req)
{
	return __crypto_akcipher_tfm(req->base.tfm);
}

/**
 * crypto_free_akcipher() -- free AKCIPHER tfm handle
 *
 * @tfm: AKCIPHER tfm handle allocated with crypto_alloc_akcipher()
 */
static inline void crypto_free_akcipher(struct crypto_akcipher *tfm)
{
	crypto_destroy_tfm(tfm, crypto_akcipher_tfm(tfm));
}

/**
 * akcipher_request_alloc() -- allocates public key request
 *
 * @tfm:	AKCIPHER tfm handle allocated with crypto_alloc_akcipher()
 * @gfp:	allocation flags
 *
 * Return: allocated handle in case of success or NULL in case of an error.
 */
static inline struct akcipher_request *akcipher_request_alloc(
	struct crypto_akcipher *tfm, gfp_t gfp)
{
	struct akcipher_request *req;

	req = kmalloc(sizeof(*req) + crypto_akcipher_reqsize(tfm), gfp);
	if (likely(req))
		akcipher_request_set_tfm(req, tfm);

	return req;
}

/**
 * akcipher_request_free() -- zeroize and free public key request
 *
 * @req:	request to free
 */
static inline void akcipher_request_free(struct akcipher_request *req)
{
	kzfree(req);
}

/**
 * akcipher_request_set_callback() -- Sets an asynchronous callback.
 *
 * Callback will be called when an asynchronous operation on a given
 * request is finished.
 *
 * @req:	request that the callback will be set for
 * @flgs:	specify for instance if the operation may backlog
 * @cmlp:	callback which will be called
 * @data:	private data used by the caller
 */
static inline void akcipher_request_set_callback(struct akcipher_request *req,
						 u32 flgs,
						 crypto_completion_t cmpl,
						 void *data)
{
	req->base.complete = cmpl;
	req->base.data = data;
	req->base.flags = flgs;
}

/**
 * akcipher_request_set_crypt() -- Sets reqest parameters
 *
 * Sets parameters required by crypto operation
 *
 * @req:	public key request
 * @src:	ptr to input parameter
 * @dst:	ptr of output parameter
 * @src_len:	size of the input buffer
 * @dst_len:	size of the output buffer. It will be updated by the
 *		implementation to reflect the acctual size of the result
 */
static inline void akcipher_request_set_crypt(struct akcipher_request *req,
					      void *src, void *dst,
					      unsigned int src_len,
					      unsigned int dst_len)
{
	req->src = src;
	req->dst = dst;
	req->src_len = src_len;
	req->dst_len = dst_len;
}

/**
 * crypto_akcipher_encrypt() -- Invoke public key encrypt operation
 *
 * Function invokes the specific public key encrypt operation for a given
 * public key algorithm
 *
 * @req:	asymmetric key request
 *
 * Return: zero on success; error code in case of error
 */
static inline int crypto_akcipher_encrypt(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);

	return alg->encrypt(req);
}

/**
 * crypto_akcipher_decrypt() -- Invoke public key decrypt operation
 *
 * Function invokes the specific public key decrypt operation for a given
 * public key algorithm
 *
 * @req:	asymmetric key request
 *
 * Return: zero on success; error code in case of error
 */
static inline int crypto_akcipher_decrypt(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);

	return alg->decrypt(req);
}

/**
 * crypto_akcipher_sign() -- Invoke public key sign operation
 *
 * Function invokes the specific public key sign operation for a given
 * public key algorithm
 *
 * @req:	asymmetric key request
 *
 * Return: zero on success; error code in case of error
 */
static inline int crypto_akcipher_sign(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);

	return alg->sign(req);
}

/**
 * crypto_akcipher_verify() -- Invoke public key verify operation
 *
 * Function invokes the specific public key verify operation for a given
 * public key algorithm
 *
 * @req:	asymmetric key request
 *
 * Return: zero on success; error code in case of error
 */
static inline int crypto_akcipher_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);

	return alg->verify(req);
}

/**
 * crypto_akcipher_setkey() -- Invoke public key setkey operation
 *
 * Function invokes the algorithm specific set key function, which knows
 * how to decode and interpret the encoded key
 *
 * @tfm:	tfm handle
 * @key:	BER encoded private or public key
 * @keylen:	length of the key
 *
 * Return: zero on success; error code in case of error
 */
static inline int crypto_akcipher_setkey(struct crypto_akcipher *tfm, void *key,
					 unsigned int keylen)
{
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);

	return alg->setkey(tfm, key, keylen);
}
#endif
