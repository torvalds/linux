/*
 * Symmetric key ciphers.
 * 
 * Copyright (c) 2007-2015 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#ifndef _CRYPTO_SKCIPHER_H
#define _CRYPTO_SKCIPHER_H

#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/slab.h>

/**
 *	struct skcipher_request - Symmetric key cipher request
 *	@cryptlen: Number of bytes to encrypt or decrypt
 *	@iv: Initialisation Vector
 *	@src: Source SG list
 *	@dst: Destination SG list
 *	@base: Underlying async request request
 *	@__ctx: Start of private context data
 */
struct skcipher_request {
	unsigned int cryptlen;

	u8 *iv;

	struct scatterlist *src;
	struct scatterlist *dst;

	struct crypto_async_request base;

	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

/**
 *	struct skcipher_givcrypt_request - Crypto request with IV generation
 *	@seq: Sequence number for IV generation
 *	@giv: Space for generated IV
 *	@creq: The crypto request itself
 */
struct skcipher_givcrypt_request {
	u64 seq;
	u8 *giv;

	struct ablkcipher_request creq;
};

struct crypto_skcipher {
	int (*setkey)(struct crypto_skcipher *tfm, const u8 *key,
	              unsigned int keylen);
	int (*encrypt)(struct skcipher_request *req);
	int (*decrypt)(struct skcipher_request *req);

	unsigned int ivsize;
	unsigned int reqsize;
	unsigned int keysize;

	struct crypto_tfm base;
};

/**
 * struct skcipher_alg - symmetric key cipher definition
 * @min_keysize: Minimum key size supported by the transformation. This is the
 *		 smallest key length supported by this transformation algorithm.
 *		 This must be set to one of the pre-defined values as this is
 *		 not hardware specific. Possible values for this field can be
 *		 found via git grep "_MIN_KEY_SIZE" include/crypto/
 * @max_keysize: Maximum key size supported by the transformation. This is the
 *		 largest key length supported by this transformation algorithm.
 *		 This must be set to one of the pre-defined values as this is
 *		 not hardware specific. Possible values for this field can be
 *		 found via git grep "_MAX_KEY_SIZE" include/crypto/
 * @setkey: Set key for the transformation. This function is used to either
 *	    program a supplied key into the hardware or store the key in the
 *	    transformation context for programming it later. Note that this
 *	    function does modify the transformation context. This function can
 *	    be called multiple times during the existence of the transformation
 *	    object, so one must make sure the key is properly reprogrammed into
 *	    the hardware. This function is also responsible for checking the key
 *	    length for validity. In case a software fallback was put in place in
 *	    the @cra_init call, this function might need to use the fallback if
 *	    the algorithm doesn't support all of the key sizes.
 * @encrypt: Encrypt a scatterlist of blocks. This function is used to encrypt
 *	     the supplied scatterlist containing the blocks of data. The crypto
 *	     API consumer is responsible for aligning the entries of the
 *	     scatterlist properly and making sure the chunks are correctly
 *	     sized. In case a software fallback was put in place in the
 *	     @cra_init call, this function might need to use the fallback if
 *	     the algorithm doesn't support all of the key sizes. In case the
 *	     key was stored in transformation context, the key might need to be
 *	     re-programmed into the hardware in this function. This function
 *	     shall not modify the transformation context, as this function may
 *	     be called in parallel with the same transformation object.
 * @decrypt: Decrypt a single block. This is a reverse counterpart to @encrypt
 *	     and the conditions are exactly the same.
 * @init: Initialize the cryptographic transformation object. This function
 *	  is used to initialize the cryptographic transformation object.
 *	  This function is called only once at the instantiation time, right
 *	  after the transformation context was allocated. In case the
 *	  cryptographic hardware has some special requirements which need to
 *	  be handled by software, this function shall check for the precise
 *	  requirement of the transformation and put any software fallbacks
 *	  in place.
 * @exit: Deinitialize the cryptographic transformation object. This is a
 *	  counterpart to @init, used to remove various changes set in
 *	  @init.
 * @ivsize: IV size applicable for transformation. The consumer must provide an
 *	    IV of exactly that size to perform the encrypt or decrypt operation.
 * @chunksize: Equal to the block size except for stream ciphers such as
 *	       CTR where it is set to the underlying block size.
 * @walksize: Equal to the chunk size except in cases where the algorithm is
 * 	      considerably more efficient if it can operate on multiple chunks
 * 	      in parallel. Should be a multiple of chunksize.
 * @base: Definition of a generic crypto algorithm.
 *
 * All fields except @ivsize are mandatory and must be filled.
 */
struct skcipher_alg {
	int (*setkey)(struct crypto_skcipher *tfm, const u8 *key,
	              unsigned int keylen);
	int (*encrypt)(struct skcipher_request *req);
	int (*decrypt)(struct skcipher_request *req);
	int (*init)(struct crypto_skcipher *tfm);
	void (*exit)(struct crypto_skcipher *tfm);

	unsigned int min_keysize;
	unsigned int max_keysize;
	unsigned int ivsize;
	unsigned int chunksize;
	unsigned int walksize;

	struct crypto_alg base;
};

#define SKCIPHER_REQUEST_ON_STACK(name, tfm) \
	char __##name##_desc[sizeof(struct skcipher_request) + \
		crypto_skcipher_reqsize(tfm)] CRYPTO_MINALIGN_ATTR; \
	struct skcipher_request *name = (void *)__##name##_desc

/**
 * DOC: Symmetric Key Cipher API
 *
 * Symmetric key cipher API is used with the ciphers of type
 * CRYPTO_ALG_TYPE_SKCIPHER (listed as type "skcipher" in /proc/crypto).
 *
 * Asynchronous cipher operations imply that the function invocation for a
 * cipher request returns immediately before the completion of the operation.
 * The cipher request is scheduled as a separate kernel thread and therefore
 * load-balanced on the different CPUs via the process scheduler. To allow
 * the kernel crypto API to inform the caller about the completion of a cipher
 * request, the caller must provide a callback function. That function is
 * invoked with the cipher handle when the request completes.
 *
 * To support the asynchronous operation, additional information than just the
 * cipher handle must be supplied to the kernel crypto API. That additional
 * information is given by filling in the skcipher_request data structure.
 *
 * For the symmetric key cipher API, the state is maintained with the tfm
 * cipher handle. A single tfm can be used across multiple calls and in
 * parallel. For asynchronous block cipher calls, context data supplied and
 * only used by the caller can be referenced the request data structure in
 * addition to the IV used for the cipher request. The maintenance of such
 * state information would be important for a crypto driver implementer to
 * have, because when calling the callback function upon completion of the
 * cipher operation, that callback function may need some information about
 * which operation just finished if it invoked multiple in parallel. This
 * state information is unused by the kernel crypto API.
 */

static inline struct crypto_skcipher *__crypto_skcipher_cast(
	struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_skcipher, base);
}

/**
 * crypto_alloc_skcipher() - allocate symmetric key cipher handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      skcipher cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Allocate a cipher handle for an skcipher. The returned struct
 * crypto_skcipher is the cipher handle that is required for any subsequent
 * API invocation for that skcipher.
 *
 * Return: allocated cipher handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
struct crypto_skcipher *crypto_alloc_skcipher(const char *alg_name,
					      u32 type, u32 mask);

static inline struct crypto_tfm *crypto_skcipher_tfm(
	struct crypto_skcipher *tfm)
{
	return &tfm->base;
}

/**
 * crypto_free_skcipher() - zeroize and free cipher handle
 * @tfm: cipher handle to be freed
 */
static inline void crypto_free_skcipher(struct crypto_skcipher *tfm)
{
	crypto_destroy_tfm(tfm, crypto_skcipher_tfm(tfm));
}

/**
 * crypto_has_skcipher() - Search for the availability of an skcipher.
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      skcipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Return: true when the skcipher is known to the kernel crypto API; false
 *	   otherwise
 */
static inline int crypto_has_skcipher(const char *alg_name, u32 type,
					u32 mask)
{
	return crypto_has_alg(alg_name, crypto_skcipher_type(type),
			      crypto_skcipher_mask(mask));
}

/**
 * crypto_has_skcipher2() - Search for the availability of an skcipher.
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      skcipher
 * @type: specifies the type of the skcipher
 * @mask: specifies the mask for the skcipher
 *
 * Return: true when the skcipher is known to the kernel crypto API; false
 *	   otherwise
 */
int crypto_has_skcipher2(const char *alg_name, u32 type, u32 mask);

static inline const char *crypto_skcipher_driver_name(
	struct crypto_skcipher *tfm)
{
	return crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm));
}

static inline struct skcipher_alg *crypto_skcipher_alg(
	struct crypto_skcipher *tfm)
{
	return container_of(crypto_skcipher_tfm(tfm)->__crt_alg,
			    struct skcipher_alg, base);
}

static inline unsigned int crypto_skcipher_alg_ivsize(struct skcipher_alg *alg)
{
	if ((alg->base.cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_BLKCIPHER)
		return alg->base.cra_blkcipher.ivsize;

	if (alg->base.cra_ablkcipher.encrypt)
		return alg->base.cra_ablkcipher.ivsize;

	return alg->ivsize;
}

/**
 * crypto_skcipher_ivsize() - obtain IV size
 * @tfm: cipher handle
 *
 * The size of the IV for the skcipher referenced by the cipher handle is
 * returned. This IV size may be zero if the cipher does not need an IV.
 *
 * Return: IV size in bytes
 */
static inline unsigned int crypto_skcipher_ivsize(struct crypto_skcipher *tfm)
{
	return tfm->ivsize;
}

static inline unsigned int crypto_skcipher_alg_chunksize(
	struct skcipher_alg *alg)
{
	if ((alg->base.cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_BLKCIPHER)
		return alg->base.cra_blocksize;

	if (alg->base.cra_ablkcipher.encrypt)
		return alg->base.cra_blocksize;

	return alg->chunksize;
}

static inline unsigned int crypto_skcipher_alg_walksize(
	struct skcipher_alg *alg)
{
	if ((alg->base.cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_BLKCIPHER)
		return alg->base.cra_blocksize;

	if (alg->base.cra_ablkcipher.encrypt)
		return alg->base.cra_blocksize;

	return alg->walksize;
}

/**
 * crypto_skcipher_chunksize() - obtain chunk size
 * @tfm: cipher handle
 *
 * The block size is set to one for ciphers such as CTR.  However,
 * you still need to provide incremental updates in multiples of
 * the underlying block size as the IV does not have sub-block
 * granularity.  This is known in this API as the chunk size.
 *
 * Return: chunk size in bytes
 */
static inline unsigned int crypto_skcipher_chunksize(
	struct crypto_skcipher *tfm)
{
	return crypto_skcipher_alg_chunksize(crypto_skcipher_alg(tfm));
}

/**
 * crypto_skcipher_walksize() - obtain walk size
 * @tfm: cipher handle
 *
 * In some cases, algorithms can only perform optimally when operating on
 * multiple blocks in parallel. This is reflected by the walksize, which
 * must be a multiple of the chunksize (or equal if the concern does not
 * apply)
 *
 * Return: walk size in bytes
 */
static inline unsigned int crypto_skcipher_walksize(
	struct crypto_skcipher *tfm)
{
	return crypto_skcipher_alg_walksize(crypto_skcipher_alg(tfm));
}

/**
 * crypto_skcipher_blocksize() - obtain block size of cipher
 * @tfm: cipher handle
 *
 * The block size for the skcipher referenced with the cipher handle is
 * returned. The caller may use that information to allocate appropriate
 * memory for the data returned by the encryption or decryption operation
 *
 * Return: block size of cipher
 */
static inline unsigned int crypto_skcipher_blocksize(
	struct crypto_skcipher *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_skcipher_tfm(tfm));
}

static inline unsigned int crypto_skcipher_alignmask(
	struct crypto_skcipher *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_skcipher_tfm(tfm));
}

static inline u32 crypto_skcipher_get_flags(struct crypto_skcipher *tfm)
{
	return crypto_tfm_get_flags(crypto_skcipher_tfm(tfm));
}

static inline void crypto_skcipher_set_flags(struct crypto_skcipher *tfm,
					       u32 flags)
{
	crypto_tfm_set_flags(crypto_skcipher_tfm(tfm), flags);
}

static inline void crypto_skcipher_clear_flags(struct crypto_skcipher *tfm,
						 u32 flags)
{
	crypto_tfm_clear_flags(crypto_skcipher_tfm(tfm), flags);
}

/**
 * crypto_skcipher_setkey() - set key for cipher
 * @tfm: cipher handle
 * @key: buffer holding the key
 * @keylen: length of the key in bytes
 *
 * The caller provided key is set for the skcipher referenced by the cipher
 * handle.
 *
 * Note, the key length determines the cipher type. Many block ciphers implement
 * different cipher modes depending on the key size, such as AES-128 vs AES-192
 * vs. AES-256. When providing a 16 byte key for an AES cipher handle, AES-128
 * is performed.
 *
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
static inline int crypto_skcipher_setkey(struct crypto_skcipher *tfm,
					 const u8 *key, unsigned int keylen)
{
	return tfm->setkey(tfm, key, keylen);
}

static inline unsigned int crypto_skcipher_default_keysize(
	struct crypto_skcipher *tfm)
{
	return tfm->keysize;
}

/**
 * crypto_skcipher_reqtfm() - obtain cipher handle from request
 * @req: skcipher_request out of which the cipher handle is to be obtained
 *
 * Return the crypto_skcipher handle when furnishing an skcipher_request
 * data structure.
 *
 * Return: crypto_skcipher handle
 */
static inline struct crypto_skcipher *crypto_skcipher_reqtfm(
	struct skcipher_request *req)
{
	return __crypto_skcipher_cast(req->base.tfm);
}

/**
 * crypto_skcipher_encrypt() - encrypt plaintext
 * @req: reference to the skcipher_request handle that holds all information
 *	 needed to perform the cipher operation
 *
 * Encrypt plaintext data using the skcipher_request handle. That data
 * structure and how it is filled with data is discussed with the
 * skcipher_request_* functions.
 *
 * Return: 0 if the cipher operation was successful; < 0 if an error occurred
 */
static inline int crypto_skcipher_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);

	if (crypto_skcipher_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return tfm->encrypt(req);
}

/**
 * crypto_skcipher_decrypt() - decrypt ciphertext
 * @req: reference to the skcipher_request handle that holds all information
 *	 needed to perform the cipher operation
 *
 * Decrypt ciphertext data using the skcipher_request handle. That data
 * structure and how it is filled with data is discussed with the
 * skcipher_request_* functions.
 *
 * Return: 0 if the cipher operation was successful; < 0 if an error occurred
 */
static inline int crypto_skcipher_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);

	if (crypto_skcipher_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return tfm->decrypt(req);
}

/**
 * DOC: Symmetric Key Cipher Request Handle
 *
 * The skcipher_request data structure contains all pointers to data
 * required for the symmetric key cipher operation. This includes the cipher
 * handle (which can be used by multiple skcipher_request instances), pointer
 * to plaintext and ciphertext, asynchronous callback function, etc. It acts
 * as a handle to the skcipher_request_* API calls in a similar way as
 * skcipher handle to the crypto_skcipher_* API calls.
 */

/**
 * crypto_skcipher_reqsize() - obtain size of the request data structure
 * @tfm: cipher handle
 *
 * Return: number of bytes
 */
static inline unsigned int crypto_skcipher_reqsize(struct crypto_skcipher *tfm)
{
	return tfm->reqsize;
}

/**
 * skcipher_request_set_tfm() - update cipher handle reference in request
 * @req: request handle to be modified
 * @tfm: cipher handle that shall be added to the request handle
 *
 * Allow the caller to replace the existing skcipher handle in the request
 * data structure with a different one.
 */
static inline void skcipher_request_set_tfm(struct skcipher_request *req,
					    struct crypto_skcipher *tfm)
{
	req->base.tfm = crypto_skcipher_tfm(tfm);
}

static inline struct skcipher_request *skcipher_request_cast(
	struct crypto_async_request *req)
{
	return container_of(req, struct skcipher_request, base);
}

/**
 * skcipher_request_alloc() - allocate request data structure
 * @tfm: cipher handle to be registered with the request
 * @gfp: memory allocation flag that is handed to kmalloc by the API call.
 *
 * Allocate the request data structure that must be used with the skcipher
 * encrypt and decrypt API calls. During the allocation, the provided skcipher
 * handle is registered in the request data structure.
 *
 * Return: allocated request handle in case of success, or NULL if out of memory
 */
static inline struct skcipher_request *skcipher_request_alloc(
	struct crypto_skcipher *tfm, gfp_t gfp)
{
	struct skcipher_request *req;

	req = kmalloc(sizeof(struct skcipher_request) +
		      crypto_skcipher_reqsize(tfm), gfp);

	if (likely(req))
		skcipher_request_set_tfm(req, tfm);

	return req;
}

/**
 * skcipher_request_free() - zeroize and free request data structure
 * @req: request data structure cipher handle to be freed
 */
static inline void skcipher_request_free(struct skcipher_request *req)
{
	kzfree(req);
}

static inline void skcipher_request_zero(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);

	memzero_explicit(req, sizeof(*req) + crypto_skcipher_reqsize(tfm));
}

/**
 * skcipher_request_set_callback() - set asynchronous callback function
 * @req: request handle
 * @flags: specify zero or an ORing of the flags
 *	   CRYPTO_TFM_REQ_MAY_BACKLOG the request queue may back log and
 *	   increase the wait queue beyond the initial maximum size;
 *	   CRYPTO_TFM_REQ_MAY_SLEEP the request processing may sleep
 * @compl: callback function pointer to be registered with the request handle
 * @data: The data pointer refers to memory that is not used by the kernel
 *	  crypto API, but provided to the callback function for it to use. Here,
 *	  the caller can provide a reference to memory the callback function can
 *	  operate on. As the callback function is invoked asynchronously to the
 *	  related functionality, it may need to access data structures of the
 *	  related functionality which can be referenced using this pointer. The
 *	  callback function can access the memory via the "data" field in the
 *	  crypto_async_request data structure provided to the callback function.
 *
 * This function allows setting the callback function that is triggered once the
 * cipher operation completes.
 *
 * The callback function is registered with the skcipher_request handle and
 * must comply with the following template::
 *
 *	void callback_function(struct crypto_async_request *req, int error)
 */
static inline void skcipher_request_set_callback(struct skcipher_request *req,
						 u32 flags,
						 crypto_completion_t compl,
						 void *data)
{
	req->base.complete = compl;
	req->base.data = data;
	req->base.flags = flags;
}

/**
 * skcipher_request_set_crypt() - set data buffers
 * @req: request handle
 * @src: source scatter / gather list
 * @dst: destination scatter / gather list
 * @cryptlen: number of bytes to process from @src
 * @iv: IV for the cipher operation which must comply with the IV size defined
 *      by crypto_skcipher_ivsize
 *
 * This function allows setting of the source data and destination data
 * scatter / gather lists.
 *
 * For encryption, the source is treated as the plaintext and the
 * destination is the ciphertext. For a decryption operation, the use is
 * reversed - the source is the ciphertext and the destination is the plaintext.
 */
static inline void skcipher_request_set_crypt(
	struct skcipher_request *req,
	struct scatterlist *src, struct scatterlist *dst,
	unsigned int cryptlen, void *iv)
{
	req->src = src;
	req->dst = dst;
	req->cryptlen = cryptlen;
	req->iv = iv;
}

#endif	/* _CRYPTO_SKCIPHER_H */

