/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Public Key Encryption
 *
 * Copyright (c) 2015, Intel Corporation
 * Authors: Tadeusz Struk <tadeusz.struk@intel.com>
 */
#ifndef _CRYPTO_AKCIPHER_H
#define _CRYPTO_AKCIPHER_H

#include <linux/atomic.h>
#include <linux/crypto.h>

/**
 * struct akcipher_request - public key request
 *
 * @base:	Common attributes for async crypto requests
 * @src:	Source data
 *		For verify op this is signature + digest, in that case
 *		total size of @src is @src_len + @dst_len.
 * @dst:	Destination data (Should be NULL for verify op)
 * @src_len:	Size of the input buffer
 *		For verify op it's size of signature part of @src, this part
 *		is supposed to be operated by cipher.
 * @dst_len:	Size of @dst buffer (for all ops except verify).
 *		It needs to be at least	as big as the expected result
 *		depending on the operation.
 *		After operation it will be updated with the actual size of the
 *		result.
 *		In case of error where the dst sgl size was insufficient,
 *		it will be updated to the size required for the operation.
 *		For verify op this is size of digest part in @src.
 * @__ctx:	Start of private context data
 */
struct akcipher_request {
	struct crypto_async_request base;
	struct scatterlist *src;
	struct scatterlist *dst;
	unsigned int src_len;
	unsigned int dst_len;
	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

/**
 * struct crypto_akcipher - user-instantiated objects which encapsulate
 * algorithms and core processing logic
 *
 * @reqsize:	Request context size required by algorithm implementation
 * @base:	Common crypto API algorithm data structure
 */
struct crypto_akcipher {
	unsigned int reqsize;

	struct crypto_tfm base;
};

/*
 * struct crypto_istat_akcipher - statistics for akcipher algorithm
 * @encrypt_cnt:	number of encrypt requests
 * @encrypt_tlen:	total data size handled by encrypt requests
 * @decrypt_cnt:	number of decrypt requests
 * @decrypt_tlen:	total data size handled by decrypt requests
 * @verify_cnt:		number of verify operation
 * @sign_cnt:		number of sign requests
 * @err_cnt:		number of error for akcipher requests
 */
struct crypto_istat_akcipher {
	atomic64_t encrypt_cnt;
	atomic64_t encrypt_tlen;
	atomic64_t decrypt_cnt;
	atomic64_t decrypt_tlen;
	atomic64_t verify_cnt;
	atomic64_t sign_cnt;
	atomic64_t err_cnt;
};

/**
 * struct akcipher_alg - generic public key algorithm
 *
 * @sign:	Function performs a sign operation as defined by public key
 *		algorithm. In case of error, where the dst_len was insufficient,
 *		the req->dst_len will be updated to the size required for the
 *		operation
 * @verify:	Function performs a complete verify operation as defined by
 *		public key algorithm, returning verification status. Requires
 *		digest value as input parameter.
 * @encrypt:	Function performs an encrypt operation as defined by public key
 *		algorithm. In case of error, where the dst_len was insufficient,
 *		the req->dst_len will be updated to the size required for the
 *		operation
 * @decrypt:	Function performs a decrypt operation as defined by public key
 *		algorithm. In case of error, where the dst_len was insufficient,
 *		the req->dst_len will be updated to the size required for the
 *		operation
 * @set_pub_key: Function invokes the algorithm specific set public key
 *		function, which knows how to decode and interpret
 *		the BER encoded public key and parameters
 * @set_priv_key: Function invokes the algorithm specific set private key
 *		function, which knows how to decode and interpret
 *		the BER encoded private key and parameters
 * @max_size:	Function returns dest buffer size required for a given key.
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
 * @stat:	Statistics for akcipher algorithm
 *
 * @base:	Common crypto API algorithm data structure
 */
struct akcipher_alg {
	int (*sign)(struct akcipher_request *req);
	int (*verify)(struct akcipher_request *req);
	int (*encrypt)(struct akcipher_request *req);
	int (*decrypt)(struct akcipher_request *req);
	int (*set_pub_key)(struct crypto_akcipher *tfm, const void *key,
			   unsigned int keylen);
	int (*set_priv_key)(struct crypto_akcipher *tfm, const void *key,
			    unsigned int keylen);
	unsigned int (*max_size)(struct crypto_akcipher *tfm);
	int (*init)(struct crypto_akcipher *tfm);
	void (*exit)(struct crypto_akcipher *tfm);

#ifdef CONFIG_CRYPTO_STATS
	struct crypto_istat_akcipher stat;
#endif

	struct crypto_alg base;
};

/**
 * DOC: Generic Public Key API
 *
 * The Public Key API is used with the algorithms of type
 * CRYPTO_ALG_TYPE_AKCIPHER (listed as type "akcipher" in /proc/crypto)
 */

/**
 * crypto_alloc_akcipher() - allocate AKCIPHER tfm handle
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
	return tfm->reqsize;
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
 * crypto_free_akcipher() - free AKCIPHER tfm handle
 *
 * @tfm: AKCIPHER tfm handle allocated with crypto_alloc_akcipher()
 *
 * If @tfm is a NULL or error pointer, this function does nothing.
 */
static inline void crypto_free_akcipher(struct crypto_akcipher *tfm)
{
	crypto_destroy_tfm(tfm, crypto_akcipher_tfm(tfm));
}

/**
 * akcipher_request_alloc() - allocates public key request
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
 * akcipher_request_free() - zeroize and free public key request
 *
 * @req:	request to free
 */
static inline void akcipher_request_free(struct akcipher_request *req)
{
	kfree_sensitive(req);
}

/**
 * akcipher_request_set_callback() - Sets an asynchronous callback.
 *
 * Callback will be called when an asynchronous operation on a given
 * request is finished.
 *
 * @req:	request that the callback will be set for
 * @flgs:	specify for instance if the operation may backlog
 * @cmpl:	callback which will be called
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
 * akcipher_request_set_crypt() - Sets request parameters
 *
 * Sets parameters required by crypto operation
 *
 * @req:	public key request
 * @src:	ptr to input scatter list
 * @dst:	ptr to output scatter list or NULL for verify op
 * @src_len:	size of the src input scatter list to be processed
 * @dst_len:	size of the dst output scatter list or size of signature
 *		portion in @src for verify op
 */
static inline void akcipher_request_set_crypt(struct akcipher_request *req,
					      struct scatterlist *src,
					      struct scatterlist *dst,
					      unsigned int src_len,
					      unsigned int dst_len)
{
	req->src = src;
	req->dst = dst;
	req->src_len = src_len;
	req->dst_len = dst_len;
}

/**
 * crypto_akcipher_maxsize() - Get len for output buffer
 *
 * Function returns the dest buffer size required for a given key.
 * Function assumes that the key is already set in the transformation. If this
 * function is called without a setkey or with a failed setkey, you will end up
 * in a NULL dereference.
 *
 * @tfm:	AKCIPHER tfm handle allocated with crypto_alloc_akcipher()
 */
static inline unsigned int crypto_akcipher_maxsize(struct crypto_akcipher *tfm)
{
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);

	return alg->max_size(tfm);
}

static inline struct crypto_istat_akcipher *akcipher_get_stat(
	struct akcipher_alg *alg)
{
#ifdef CONFIG_CRYPTO_STATS
	return &alg->stat;
#else
	return NULL;
#endif
}

static inline int crypto_akcipher_errstat(struct akcipher_alg *alg, int err)
{
	if (!IS_ENABLED(CONFIG_CRYPTO_STATS))
		return err;

	if (err && err != -EINPROGRESS && err != -EBUSY)
		atomic64_inc(&akcipher_get_stat(alg)->err_cnt);

	return err;
}

/**
 * crypto_akcipher_encrypt() - Invoke public key encrypt operation
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

	if (IS_ENABLED(CONFIG_CRYPTO_STATS)) {
		struct crypto_istat_akcipher *istat = akcipher_get_stat(alg);

		atomic64_inc(&istat->encrypt_cnt);
		atomic64_add(req->src_len, &istat->encrypt_tlen);
	}

	return crypto_akcipher_errstat(alg, alg->encrypt(req));
}

/**
 * crypto_akcipher_decrypt() - Invoke public key decrypt operation
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

	if (IS_ENABLED(CONFIG_CRYPTO_STATS)) {
		struct crypto_istat_akcipher *istat = akcipher_get_stat(alg);

		atomic64_inc(&istat->decrypt_cnt);
		atomic64_add(req->src_len, &istat->decrypt_tlen);
	}

	return crypto_akcipher_errstat(alg, alg->decrypt(req));
}

/**
 * crypto_akcipher_sign() - Invoke public key sign operation
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

	if (IS_ENABLED(CONFIG_CRYPTO_STATS))
		atomic64_inc(&akcipher_get_stat(alg)->sign_cnt);

	return crypto_akcipher_errstat(alg, alg->sign(req));
}

/**
 * crypto_akcipher_verify() - Invoke public key signature verification
 *
 * Function invokes the specific public key signature verification operation
 * for a given public key algorithm.
 *
 * @req:	asymmetric key request
 *
 * Note: req->dst should be NULL, req->src should point to SG of size
 * (req->src_size + req->dst_size), containing signature (of req->src_size
 * length) with appended digest (of req->dst_size length).
 *
 * Return: zero on verification success; error code in case of error.
 */
static inline int crypto_akcipher_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_STATS))
		atomic64_inc(&akcipher_get_stat(alg)->verify_cnt);

	return crypto_akcipher_errstat(alg, alg->verify(req));
}

/**
 * crypto_akcipher_set_pub_key() - Invoke set public key operation
 *
 * Function invokes the algorithm specific set key function, which knows
 * how to decode and interpret the encoded key and parameters
 *
 * @tfm:	tfm handle
 * @key:	BER encoded public key, algo OID, paramlen, BER encoded
 *		parameters
 * @keylen:	length of the key (not including other data)
 *
 * Return: zero on success; error code in case of error
 */
static inline int crypto_akcipher_set_pub_key(struct crypto_akcipher *tfm,
					      const void *key,
					      unsigned int keylen)
{
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);

	return alg->set_pub_key(tfm, key, keylen);
}

/**
 * crypto_akcipher_set_priv_key() - Invoke set private key operation
 *
 * Function invokes the algorithm specific set key function, which knows
 * how to decode and interpret the encoded key and parameters
 *
 * @tfm:	tfm handle
 * @key:	BER encoded private key, algo OID, paramlen, BER encoded
 *		parameters
 * @keylen:	length of the key (not including other data)
 *
 * Return: zero on success; error code in case of error
 */
static inline int crypto_akcipher_set_priv_key(struct crypto_akcipher *tfm,
					       const void *key,
					       unsigned int keylen)
{
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);

	return alg->set_priv_key(tfm, key, keylen);
}
#endif
