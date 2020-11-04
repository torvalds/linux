/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Hash: Hash algorithms under the crypto API
 * 
 * Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 */

#ifndef _CRYPTO_HASH_H
#define _CRYPTO_HASH_H

#include <linux/crypto.h>
#include <linux/string.h>

struct crypto_ahash;

/**
 * DOC: Message Digest Algorithm Definitions
 *
 * These data structures define modular message digest algorithm
 * implementations, managed via crypto_register_ahash(),
 * crypto_register_shash(), crypto_unregister_ahash() and
 * crypto_unregister_shash().
 */

/**
 * struct hash_alg_common - define properties of message digest
 * @digestsize: Size of the result of the transformation. A buffer of this size
 *	        must be available to the @final and @finup calls, so they can
 *	        store the resulting hash into it. For various predefined sizes,
 *	        search include/crypto/ using
 *	        git grep _DIGEST_SIZE include/crypto.
 * @statesize: Size of the block for partial state of the transformation. A
 *	       buffer of this size must be passed to the @export function as it
 *	       will save the partial state of the transformation into it. On the
 *	       other side, the @import function will load the state from a
 *	       buffer of this size as well.
 * @base: Start of data structure of cipher algorithm. The common data
 *	  structure of crypto_alg contains information common to all ciphers.
 *	  The hash_alg_common data structure now adds the hash-specific
 *	  information.
 */
struct hash_alg_common {
	unsigned int digestsize;
	unsigned int statesize;

	struct crypto_alg base;
};

struct ahash_request {
	struct crypto_async_request base;

	unsigned int nbytes;
	struct scatterlist *src;
	u8 *result;

	/* This field may only be used by the ahash API code. */
	void *priv;

	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

/**
 * struct ahash_alg - asynchronous message digest definition
 * @init: **[mandatory]** Initialize the transformation context. Intended only to initialize the
 *	  state of the HASH transformation at the beginning. This shall fill in
 *	  the internal structures used during the entire duration of the whole
 *	  transformation. No data processing happens at this point. Driver code
 *	  implementation must not use req->result.
 * @update: **[mandatory]** Push a chunk of data into the driver for transformation. This
 *	   function actually pushes blocks of data from upper layers into the
 *	   driver, which then passes those to the hardware as seen fit. This
 *	   function must not finalize the HASH transformation by calculating the
 *	   final message digest as this only adds more data into the
 *	   transformation. This function shall not modify the transformation
 *	   context, as this function may be called in parallel with the same
 *	   transformation object. Data processing can happen synchronously
 *	   [SHASH] or asynchronously [AHASH] at this point. Driver must not use
 *	   req->result.
 * @final: **[mandatory]** Retrieve result from the driver. This function finalizes the
 *	   transformation and retrieves the resulting hash from the driver and
 *	   pushes it back to upper layers. No data processing happens at this
 *	   point unless hardware requires it to finish the transformation
 *	   (then the data buffered by the device driver is processed).
 * @finup: **[optional]** Combination of @update and @final. This function is effectively a
 *	   combination of @update and @final calls issued in sequence. As some
 *	   hardware cannot do @update and @final separately, this callback was
 *	   added to allow such hardware to be used at least by IPsec. Data
 *	   processing can happen synchronously [SHASH] or asynchronously [AHASH]
 *	   at this point.
 * @digest: Combination of @init and @update and @final. This function
 *	    effectively behaves as the entire chain of operations, @init,
 *	    @update and @final issued in sequence. Just like @finup, this was
 *	    added for hardware which cannot do even the @finup, but can only do
 *	    the whole transformation in one run. Data processing can happen
 *	    synchronously [SHASH] or asynchronously [AHASH] at this point.
 * @setkey: Set optional key used by the hashing algorithm. Intended to push
 *	    optional key used by the hashing algorithm from upper layers into
 *	    the driver. This function can store the key in the transformation
 *	    context or can outright program it into the hardware. In the former
 *	    case, one must be careful to program the key into the hardware at
 *	    appropriate time and one must be careful that .setkey() can be
 *	    called multiple times during the existence of the transformation
 *	    object. Not  all hashing algorithms do implement this function as it
 *	    is only needed for keyed message digests. SHAx/MDx/CRCx do NOT
 *	    implement this function. HMAC(MDx)/HMAC(SHAx)/CMAC(AES) do implement
 *	    this function. This function must be called before any other of the
 *	    @init, @update, @final, @finup, @digest is called. No data
 *	    processing happens at this point.
 * @export: Export partial state of the transformation. This function dumps the
 *	    entire state of the ongoing transformation into a provided block of
 *	    data so it can be @import 'ed back later on. This is useful in case
 *	    you want to save partial result of the transformation after
 *	    processing certain amount of data and reload this partial result
 *	    multiple times later on for multiple re-use. No data processing
 *	    happens at this point. Driver must not use req->result.
 * @import: Import partial state of the transformation. This function loads the
 *	    entire state of the ongoing transformation from a provided block of
 *	    data so the transformation can continue from this point onward. No
 *	    data processing happens at this point. Driver must not use
 *	    req->result.
 * @init_tfm: Initialize the cryptographic transformation object.
 *	      This function is called only once at the instantiation
 *	      time, right after the transformation context was
 *	      allocated. In case the cryptographic hardware has
 *	      some special requirements which need to be handled
 *	      by software, this function shall check for the precise
 *	      requirement of the transformation and put any software
 *	      fallbacks in place.
 * @exit_tfm: Deinitialize the cryptographic transformation object.
 *	      This is a counterpart to @init_tfm, used to remove
 *	      various changes set in @init_tfm.
 * @halg: see struct hash_alg_common
 */
struct ahash_alg {
	int (*init)(struct ahash_request *req);
	int (*update)(struct ahash_request *req);
	int (*final)(struct ahash_request *req);
	int (*finup)(struct ahash_request *req);
	int (*digest)(struct ahash_request *req);
	int (*export)(struct ahash_request *req, void *out);
	int (*import)(struct ahash_request *req, const void *in);
	int (*setkey)(struct crypto_ahash *tfm, const u8 *key,
		      unsigned int keylen);
	int (*init_tfm)(struct crypto_ahash *tfm);
	void (*exit_tfm)(struct crypto_ahash *tfm);

	struct hash_alg_common halg;
};

struct shash_desc {
	struct crypto_shash *tfm;
	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

#define HASH_MAX_DIGESTSIZE	 64

/*
 * Worst case is hmac(sha3-224-generic).  Its context is a nested 'shash_desc'
 * containing a 'struct sha3_state'.
 */
#define HASH_MAX_DESCSIZE	(sizeof(struct shash_desc) + 360)

#define HASH_MAX_STATESIZE	512

#define SHASH_DESC_ON_STACK(shash, ctx)				  \
	char __##shash##_desc[sizeof(struct shash_desc) +	  \
		HASH_MAX_DESCSIZE] CRYPTO_MINALIGN_ATTR; \
	struct shash_desc *shash = (struct shash_desc *)__##shash##_desc

/**
 * struct shash_alg - synchronous message digest definition
 * @init: see struct ahash_alg
 * @update: see struct ahash_alg
 * @final: see struct ahash_alg
 * @finup: see struct ahash_alg
 * @digest: see struct ahash_alg
 * @export: see struct ahash_alg
 * @import: see struct ahash_alg
 * @setkey: see struct ahash_alg
 * @init_tfm: Initialize the cryptographic transformation object.
 *	      This function is called only once at the instantiation
 *	      time, right after the transformation context was
 *	      allocated. In case the cryptographic hardware has
 *	      some special requirements which need to be handled
 *	      by software, this function shall check for the precise
 *	      requirement of the transformation and put any software
 *	      fallbacks in place.
 * @exit_tfm: Deinitialize the cryptographic transformation object.
 *	      This is a counterpart to @init_tfm, used to remove
 *	      various changes set in @init_tfm.
 * @digestsize: see struct ahash_alg
 * @statesize: see struct ahash_alg
 * @descsize: Size of the operational state for the message digest. This state
 * 	      size is the memory size that needs to be allocated for
 *	      shash_desc.__ctx
 * @base: internally used
 */
struct shash_alg {
	int (*init)(struct shash_desc *desc);
	int (*update)(struct shash_desc *desc, const u8 *data,
		      unsigned int len);
	int (*final)(struct shash_desc *desc, u8 *out);
	int (*finup)(struct shash_desc *desc, const u8 *data,
		     unsigned int len, u8 *out);
	int (*digest)(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out);
	int (*export)(struct shash_desc *desc, void *out);
	int (*import)(struct shash_desc *desc, const void *in);
	int (*setkey)(struct crypto_shash *tfm, const u8 *key,
		      unsigned int keylen);
	int (*init_tfm)(struct crypto_shash *tfm);
	void (*exit_tfm)(struct crypto_shash *tfm);

	unsigned int descsize;

	/* These fields must match hash_alg_common. */
	unsigned int digestsize
		__attribute__ ((aligned(__alignof__(struct hash_alg_common))));
	unsigned int statesize;

	struct crypto_alg base;
};

struct crypto_ahash {
	int (*init)(struct ahash_request *req);
	int (*update)(struct ahash_request *req);
	int (*final)(struct ahash_request *req);
	int (*finup)(struct ahash_request *req);
	int (*digest)(struct ahash_request *req);
	int (*export)(struct ahash_request *req, void *out);
	int (*import)(struct ahash_request *req, const void *in);
	int (*setkey)(struct crypto_ahash *tfm, const u8 *key,
		      unsigned int keylen);

	unsigned int reqsize;
	struct crypto_tfm base;
};

struct crypto_shash {
	unsigned int descsize;
	struct crypto_tfm base;
};

/**
 * DOC: Asynchronous Message Digest API
 *
 * The asynchronous message digest API is used with the ciphers of type
 * CRYPTO_ALG_TYPE_AHASH (listed as type "ahash" in /proc/crypto)
 *
 * The asynchronous cipher operation discussion provided for the
 * CRYPTO_ALG_TYPE_SKCIPHER API applies here as well.
 */

static inline struct crypto_ahash *__crypto_ahash_cast(struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_ahash, base);
}

/**
 * crypto_alloc_ahash() - allocate ahash cipher handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      ahash cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Allocate a cipher handle for an ahash. The returned struct
 * crypto_ahash is the cipher handle that is required for any subsequent
 * API invocation for that ahash.
 *
 * Return: allocated cipher handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
struct crypto_ahash *crypto_alloc_ahash(const char *alg_name, u32 type,
					u32 mask);

static inline struct crypto_tfm *crypto_ahash_tfm(struct crypto_ahash *tfm)
{
	return &tfm->base;
}

/**
 * crypto_free_ahash() - zeroize and free the ahash handle
 * @tfm: cipher handle to be freed
 */
static inline void crypto_free_ahash(struct crypto_ahash *tfm)
{
	crypto_destroy_tfm(tfm, crypto_ahash_tfm(tfm));
}

/**
 * crypto_has_ahash() - Search for the availability of an ahash.
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      ahash
 * @type: specifies the type of the ahash
 * @mask: specifies the mask for the ahash
 *
 * Return: true when the ahash is known to the kernel crypto API; false
 *	   otherwise
 */
int crypto_has_ahash(const char *alg_name, u32 type, u32 mask);

static inline const char *crypto_ahash_alg_name(struct crypto_ahash *tfm)
{
	return crypto_tfm_alg_name(crypto_ahash_tfm(tfm));
}

static inline const char *crypto_ahash_driver_name(struct crypto_ahash *tfm)
{
	return crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
}

static inline unsigned int crypto_ahash_alignmask(
	struct crypto_ahash *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_ahash_tfm(tfm));
}

/**
 * crypto_ahash_blocksize() - obtain block size for cipher
 * @tfm: cipher handle
 *
 * The block size for the message digest cipher referenced with the cipher
 * handle is returned.
 *
 * Return: block size of cipher
 */
static inline unsigned int crypto_ahash_blocksize(struct crypto_ahash *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
}

static inline struct hash_alg_common *__crypto_hash_alg_common(
	struct crypto_alg *alg)
{
	return container_of(alg, struct hash_alg_common, base);
}

static inline struct hash_alg_common *crypto_hash_alg_common(
	struct crypto_ahash *tfm)
{
	return __crypto_hash_alg_common(crypto_ahash_tfm(tfm)->__crt_alg);
}

/**
 * crypto_ahash_digestsize() - obtain message digest size
 * @tfm: cipher handle
 *
 * The size for the message digest created by the message digest cipher
 * referenced with the cipher handle is returned.
 *
 *
 * Return: message digest size of cipher
 */
static inline unsigned int crypto_ahash_digestsize(struct crypto_ahash *tfm)
{
	return crypto_hash_alg_common(tfm)->digestsize;
}

/**
 * crypto_ahash_statesize() - obtain size of the ahash state
 * @tfm: cipher handle
 *
 * Return the size of the ahash state. With the crypto_ahash_export()
 * function, the caller can export the state into a buffer whose size is
 * defined with this function.
 *
 * Return: size of the ahash state
 */
static inline unsigned int crypto_ahash_statesize(struct crypto_ahash *tfm)
{
	return crypto_hash_alg_common(tfm)->statesize;
}

static inline u32 crypto_ahash_get_flags(struct crypto_ahash *tfm)
{
	return crypto_tfm_get_flags(crypto_ahash_tfm(tfm));
}

static inline void crypto_ahash_set_flags(struct crypto_ahash *tfm, u32 flags)
{
	crypto_tfm_set_flags(crypto_ahash_tfm(tfm), flags);
}

static inline void crypto_ahash_clear_flags(struct crypto_ahash *tfm, u32 flags)
{
	crypto_tfm_clear_flags(crypto_ahash_tfm(tfm), flags);
}

/**
 * crypto_ahash_reqtfm() - obtain cipher handle from request
 * @req: asynchronous request handle that contains the reference to the ahash
 *	 cipher handle
 *
 * Return the ahash cipher handle that is registered with the asynchronous
 * request handle ahash_request.
 *
 * Return: ahash cipher handle
 */
static inline struct crypto_ahash *crypto_ahash_reqtfm(
	struct ahash_request *req)
{
	return __crypto_ahash_cast(req->base.tfm);
}

/**
 * crypto_ahash_reqsize() - obtain size of the request data structure
 * @tfm: cipher handle
 *
 * Return: size of the request data
 */
static inline unsigned int crypto_ahash_reqsize(struct crypto_ahash *tfm)
{
	return tfm->reqsize;
}

static inline void *ahash_request_ctx(struct ahash_request *req)
{
	return req->__ctx;
}

/**
 * crypto_ahash_setkey - set key for cipher handle
 * @tfm: cipher handle
 * @key: buffer holding the key
 * @keylen: length of the key in bytes
 *
 * The caller provided key is set for the ahash cipher. The cipher
 * handle must point to a keyed hash in order for this function to succeed.
 *
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
int crypto_ahash_setkey(struct crypto_ahash *tfm, const u8 *key,
			unsigned int keylen);

/**
 * crypto_ahash_finup() - update and finalize message digest
 * @req: reference to the ahash_request handle that holds all information
 *	 needed to perform the cipher operation
 *
 * This function is a "short-hand" for the function calls of
 * crypto_ahash_update and crypto_ahash_final. The parameters have the same
 * meaning as discussed for those separate functions.
 *
 * Return: see crypto_ahash_final()
 */
int crypto_ahash_finup(struct ahash_request *req);

/**
 * crypto_ahash_final() - calculate message digest
 * @req: reference to the ahash_request handle that holds all information
 *	 needed to perform the cipher operation
 *
 * Finalize the message digest operation and create the message digest
 * based on all data added to the cipher handle. The message digest is placed
 * into the output buffer registered with the ahash_request handle.
 *
 * Return:
 * 0		if the message digest was successfully calculated;
 * -EINPROGRESS	if data is feeded into hardware (DMA) or queued for later;
 * -EBUSY	if queue is full and request should be resubmitted later;
 * other < 0	if an error occurred
 */
int crypto_ahash_final(struct ahash_request *req);

/**
 * crypto_ahash_digest() - calculate message digest for a buffer
 * @req: reference to the ahash_request handle that holds all information
 *	 needed to perform the cipher operation
 *
 * This function is a "short-hand" for the function calls of crypto_ahash_init,
 * crypto_ahash_update and crypto_ahash_final. The parameters have the same
 * meaning as discussed for those separate three functions.
 *
 * Return: see crypto_ahash_final()
 */
int crypto_ahash_digest(struct ahash_request *req);

/**
 * crypto_ahash_export() - extract current message digest state
 * @req: reference to the ahash_request handle whose state is exported
 * @out: output buffer of sufficient size that can hold the hash state
 *
 * This function exports the hash state of the ahash_request handle into the
 * caller-allocated output buffer out which must have sufficient size (e.g. by
 * calling crypto_ahash_statesize()).
 *
 * Return: 0 if the export was successful; < 0 if an error occurred
 */
static inline int crypto_ahash_export(struct ahash_request *req, void *out)
{
	return crypto_ahash_reqtfm(req)->export(req, out);
}

/**
 * crypto_ahash_import() - import message digest state
 * @req: reference to ahash_request handle the state is imported into
 * @in: buffer holding the state
 *
 * This function imports the hash state into the ahash_request handle from the
 * input buffer. That buffer should have been generated with the
 * crypto_ahash_export function.
 *
 * Return: 0 if the import was successful; < 0 if an error occurred
 */
static inline int crypto_ahash_import(struct ahash_request *req, const void *in)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return tfm->import(req, in);
}

/**
 * crypto_ahash_init() - (re)initialize message digest handle
 * @req: ahash_request handle that already is initialized with all necessary
 *	 data using the ahash_request_* API functions
 *
 * The call (re-)initializes the message digest referenced by the ahash_request
 * handle. Any potentially existing state created by previous operations is
 * discarded.
 *
 * Return: see crypto_ahash_final()
 */
static inline int crypto_ahash_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return tfm->init(req);
}

/**
 * crypto_ahash_update() - add data to message digest for processing
 * @req: ahash_request handle that was previously initialized with the
 *	 crypto_ahash_init call.
 *
 * Updates the message digest state of the &ahash_request handle. The input data
 * is pointed to by the scatter/gather list registered in the &ahash_request
 * handle
 *
 * Return: see crypto_ahash_final()
 */
static inline int crypto_ahash_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct crypto_alg *alg = tfm->base.__crt_alg;
	unsigned int nbytes = req->nbytes;
	int ret;

	crypto_stats_get(alg);
	ret = crypto_ahash_reqtfm(req)->update(req);
	crypto_stats_ahash_update(nbytes, ret, alg);
	return ret;
}

/**
 * DOC: Asynchronous Hash Request Handle
 *
 * The &ahash_request data structure contains all pointers to data
 * required for the asynchronous cipher operation. This includes the cipher
 * handle (which can be used by multiple &ahash_request instances), pointer
 * to plaintext and the message digest output buffer, asynchronous callback
 * function, etc. It acts as a handle to the ahash_request_* API calls in a
 * similar way as ahash handle to the crypto_ahash_* API calls.
 */

/**
 * ahash_request_set_tfm() - update cipher handle reference in request
 * @req: request handle to be modified
 * @tfm: cipher handle that shall be added to the request handle
 *
 * Allow the caller to replace the existing ahash handle in the request
 * data structure with a different one.
 */
static inline void ahash_request_set_tfm(struct ahash_request *req,
					 struct crypto_ahash *tfm)
{
	req->base.tfm = crypto_ahash_tfm(tfm);
}

/**
 * ahash_request_alloc() - allocate request data structure
 * @tfm: cipher handle to be registered with the request
 * @gfp: memory allocation flag that is handed to kmalloc by the API call.
 *
 * Allocate the request data structure that must be used with the ahash
 * message digest API calls. During
 * the allocation, the provided ahash handle
 * is registered in the request data structure.
 *
 * Return: allocated request handle in case of success, or NULL if out of memory
 */
static inline struct ahash_request *ahash_request_alloc(
	struct crypto_ahash *tfm, gfp_t gfp)
{
	struct ahash_request *req;

	req = kmalloc(sizeof(struct ahash_request) +
		      crypto_ahash_reqsize(tfm), gfp);

	if (likely(req))
		ahash_request_set_tfm(req, tfm);

	return req;
}

/**
 * ahash_request_free() - zeroize and free the request data structure
 * @req: request data structure cipher handle to be freed
 */
static inline void ahash_request_free(struct ahash_request *req)
{
	kfree_sensitive(req);
}

static inline void ahash_request_zero(struct ahash_request *req)
{
	memzero_explicit(req, sizeof(*req) +
			      crypto_ahash_reqsize(crypto_ahash_reqtfm(req)));
}

static inline struct ahash_request *ahash_request_cast(
	struct crypto_async_request *req)
{
	return container_of(req, struct ahash_request, base);
}

/**
 * ahash_request_set_callback() - set asynchronous callback function
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
 *	  &crypto_async_request data structure provided to the callback function.
 *
 * This function allows setting the callback function that is triggered once
 * the cipher operation completes.
 *
 * The callback function is registered with the &ahash_request handle and
 * must comply with the following template::
 *
 *	void callback_function(struct crypto_async_request *req, int error)
 */
static inline void ahash_request_set_callback(struct ahash_request *req,
					      u32 flags,
					      crypto_completion_t compl,
					      void *data)
{
	req->base.complete = compl;
	req->base.data = data;
	req->base.flags = flags;
}

/**
 * ahash_request_set_crypt() - set data buffers
 * @req: ahash_request handle to be updated
 * @src: source scatter/gather list
 * @result: buffer that is filled with the message digest -- the caller must
 *	    ensure that the buffer has sufficient space by, for example, calling
 *	    crypto_ahash_digestsize()
 * @nbytes: number of bytes to process from the source scatter/gather list
 *
 * By using this call, the caller references the source scatter/gather list.
 * The source scatter/gather list points to the data the message digest is to
 * be calculated for.
 */
static inline void ahash_request_set_crypt(struct ahash_request *req,
					   struct scatterlist *src, u8 *result,
					   unsigned int nbytes)
{
	req->src = src;
	req->nbytes = nbytes;
	req->result = result;
}

/**
 * DOC: Synchronous Message Digest API
 *
 * The synchronous message digest API is used with the ciphers of type
 * CRYPTO_ALG_TYPE_SHASH (listed as type "shash" in /proc/crypto)
 *
 * The message digest API is able to maintain state information for the
 * caller.
 *
 * The synchronous message digest API can store user-related context in its
 * shash_desc request data structure.
 */

/**
 * crypto_alloc_shash() - allocate message digest handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      message digest cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Allocate a cipher handle for a message digest. The returned &struct
 * crypto_shash is the cipher handle that is required for any subsequent
 * API invocation for that message digest.
 *
 * Return: allocated cipher handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
struct crypto_shash *crypto_alloc_shash(const char *alg_name, u32 type,
					u32 mask);

static inline struct crypto_tfm *crypto_shash_tfm(struct crypto_shash *tfm)
{
	return &tfm->base;
}

/**
 * crypto_free_shash() - zeroize and free the message digest handle
 * @tfm: cipher handle to be freed
 */
static inline void crypto_free_shash(struct crypto_shash *tfm)
{
	crypto_destroy_tfm(tfm, crypto_shash_tfm(tfm));
}

static inline const char *crypto_shash_alg_name(struct crypto_shash *tfm)
{
	return crypto_tfm_alg_name(crypto_shash_tfm(tfm));
}

static inline const char *crypto_shash_driver_name(struct crypto_shash *tfm)
{
	return crypto_tfm_alg_driver_name(crypto_shash_tfm(tfm));
}

static inline unsigned int crypto_shash_alignmask(
	struct crypto_shash *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_shash_tfm(tfm));
}

/**
 * crypto_shash_blocksize() - obtain block size for cipher
 * @tfm: cipher handle
 *
 * The block size for the message digest cipher referenced with the cipher
 * handle is returned.
 *
 * Return: block size of cipher
 */
static inline unsigned int crypto_shash_blocksize(struct crypto_shash *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_shash_tfm(tfm));
}

static inline struct shash_alg *__crypto_shash_alg(struct crypto_alg *alg)
{
	return container_of(alg, struct shash_alg, base);
}

static inline struct shash_alg *crypto_shash_alg(struct crypto_shash *tfm)
{
	return __crypto_shash_alg(crypto_shash_tfm(tfm)->__crt_alg);
}

/**
 * crypto_shash_digestsize() - obtain message digest size
 * @tfm: cipher handle
 *
 * The size for the message digest created by the message digest cipher
 * referenced with the cipher handle is returned.
 *
 * Return: digest size of cipher
 */
static inline unsigned int crypto_shash_digestsize(struct crypto_shash *tfm)
{
	return crypto_shash_alg(tfm)->digestsize;
}

static inline unsigned int crypto_shash_statesize(struct crypto_shash *tfm)
{
	return crypto_shash_alg(tfm)->statesize;
}

static inline u32 crypto_shash_get_flags(struct crypto_shash *tfm)
{
	return crypto_tfm_get_flags(crypto_shash_tfm(tfm));
}

static inline void crypto_shash_set_flags(struct crypto_shash *tfm, u32 flags)
{
	crypto_tfm_set_flags(crypto_shash_tfm(tfm), flags);
}

static inline void crypto_shash_clear_flags(struct crypto_shash *tfm, u32 flags)
{
	crypto_tfm_clear_flags(crypto_shash_tfm(tfm), flags);
}

/**
 * crypto_shash_descsize() - obtain the operational state size
 * @tfm: cipher handle
 *
 * The size of the operational state the cipher needs during operation is
 * returned for the hash referenced with the cipher handle. This size is
 * required to calculate the memory requirements to allow the caller allocating
 * sufficient memory for operational state.
 *
 * The operational state is defined with struct shash_desc where the size of
 * that data structure is to be calculated as
 * sizeof(struct shash_desc) + crypto_shash_descsize(alg)
 *
 * Return: size of the operational state
 */
static inline unsigned int crypto_shash_descsize(struct crypto_shash *tfm)
{
	return tfm->descsize;
}

static inline void *shash_desc_ctx(struct shash_desc *desc)
{
	return desc->__ctx;
}

/**
 * crypto_shash_setkey() - set key for message digest
 * @tfm: cipher handle
 * @key: buffer holding the key
 * @keylen: length of the key in bytes
 *
 * The caller provided key is set for the keyed message digest cipher. The
 * cipher handle must point to a keyed message digest cipher in order for this
 * function to succeed.
 *
 * Context: Any context.
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
int crypto_shash_setkey(struct crypto_shash *tfm, const u8 *key,
			unsigned int keylen);

/**
 * crypto_shash_digest() - calculate message digest for buffer
 * @desc: see crypto_shash_final()
 * @data: see crypto_shash_update()
 * @len: see crypto_shash_update()
 * @out: see crypto_shash_final()
 *
 * This function is a "short-hand" for the function calls of crypto_shash_init,
 * crypto_shash_update and crypto_shash_final. The parameters have the same
 * meaning as discussed for those separate three functions.
 *
 * Context: Any context.
 * Return: 0 if the message digest creation was successful; < 0 if an error
 *	   occurred
 */
int crypto_shash_digest(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out);

/**
 * crypto_shash_tfm_digest() - calculate message digest for buffer
 * @tfm: hash transformation object
 * @data: see crypto_shash_update()
 * @len: see crypto_shash_update()
 * @out: see crypto_shash_final()
 *
 * This is a simplified version of crypto_shash_digest() for users who don't
 * want to allocate their own hash descriptor (shash_desc).  Instead,
 * crypto_shash_tfm_digest() takes a hash transformation object (crypto_shash)
 * directly, and it allocates a hash descriptor on the stack internally.
 * Note that this stack allocation may be fairly large.
 *
 * Context: Any context.
 * Return: 0 on success; < 0 if an error occurred.
 */
int crypto_shash_tfm_digest(struct crypto_shash *tfm, const u8 *data,
			    unsigned int len, u8 *out);

/**
 * crypto_shash_export() - extract operational state for message digest
 * @desc: reference to the operational state handle whose state is exported
 * @out: output buffer of sufficient size that can hold the hash state
 *
 * This function exports the hash state of the operational state handle into the
 * caller-allocated output buffer out which must have sufficient size (e.g. by
 * calling crypto_shash_descsize).
 *
 * Context: Any context.
 * Return: 0 if the export creation was successful; < 0 if an error occurred
 */
static inline int crypto_shash_export(struct shash_desc *desc, void *out)
{
	return crypto_shash_alg(desc->tfm)->export(desc, out);
}

/**
 * crypto_shash_import() - import operational state
 * @desc: reference to the operational state handle the state imported into
 * @in: buffer holding the state
 *
 * This function imports the hash state into the operational state handle from
 * the input buffer. That buffer should have been generated with the
 * crypto_ahash_export function.
 *
 * Context: Any context.
 * Return: 0 if the import was successful; < 0 if an error occurred
 */
static inline int crypto_shash_import(struct shash_desc *desc, const void *in)
{
	struct crypto_shash *tfm = desc->tfm;

	if (crypto_shash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return crypto_shash_alg(tfm)->import(desc, in);
}

/**
 * crypto_shash_init() - (re)initialize message digest
 * @desc: operational state handle that is already filled
 *
 * The call (re-)initializes the message digest referenced by the
 * operational state handle. Any potentially existing state created by
 * previous operations is discarded.
 *
 * Context: Any context.
 * Return: 0 if the message digest initialization was successful; < 0 if an
 *	   error occurred
 */
static inline int crypto_shash_init(struct shash_desc *desc)
{
	struct crypto_shash *tfm = desc->tfm;

	if (crypto_shash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return crypto_shash_alg(tfm)->init(desc);
}

/**
 * crypto_shash_update() - add data to message digest for processing
 * @desc: operational state handle that is already initialized
 * @data: input data to be added to the message digest
 * @len: length of the input data
 *
 * Updates the message digest state of the operational state handle.
 *
 * Context: Any context.
 * Return: 0 if the message digest update was successful; < 0 if an error
 *	   occurred
 */
int crypto_shash_update(struct shash_desc *desc, const u8 *data,
			unsigned int len);

/**
 * crypto_shash_final() - calculate message digest
 * @desc: operational state handle that is already filled with data
 * @out: output buffer filled with the message digest
 *
 * Finalize the message digest operation and create the message digest
 * based on all data added to the cipher handle. The message digest is placed
 * into the output buffer. The caller must ensure that the output buffer is
 * large enough by using crypto_shash_digestsize.
 *
 * Context: Any context.
 * Return: 0 if the message digest creation was successful; < 0 if an error
 *	   occurred
 */
int crypto_shash_final(struct shash_desc *desc, u8 *out);

/**
 * crypto_shash_finup() - calculate message digest of buffer
 * @desc: see crypto_shash_final()
 * @data: see crypto_shash_update()
 * @len: see crypto_shash_update()
 * @out: see crypto_shash_final()
 *
 * This function is a "short-hand" for the function calls of
 * crypto_shash_update and crypto_shash_final. The parameters have the same
 * meaning as discussed for those separate functions.
 *
 * Context: Any context.
 * Return: 0 if the message digest creation was successful; < 0 if an error
 *	   occurred
 */
int crypto_shash_finup(struct shash_desc *desc, const u8 *data,
		       unsigned int len, u8 *out);

static inline void shash_desc_zero(struct shash_desc *desc)
{
	memzero_explicit(desc,
			 sizeof(*desc) + crypto_shash_descsize(desc->tfm));
}

#endif	/* _CRYPTO_HASH_H */
