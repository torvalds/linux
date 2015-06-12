/*
 * Scatterlist Cryptographic API.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 David S. Miller (davem@redhat.com)
 * Copyright (c) 2005 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * Portions derived from Cryptoapi, by Alexander Kjeldaas <astor@fast.no>
 * and Nettle, by Niels Möller.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#ifndef _LINUX_CRYPTO_H
#define _LINUX_CRYPTO_H

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

/*
 * Autoloaded crypto modules should only use a prefixed name to avoid allowing
 * arbitrary modules to be loaded. Loading from userspace may still need the
 * unprefixed names, so retains those aliases as well.
 * This uses __MODULE_INFO directly instead of MODULE_ALIAS because pre-4.3
 * gcc (e.g. avr32 toolchain) uses __LINE__ for uniqueness, and this macro
 * expands twice on the same line. Instead, use a separate base name for the
 * alias.
 */
#define MODULE_ALIAS_CRYPTO(name)	\
		__MODULE_INFO(alias, alias_userspace, name);	\
		__MODULE_INFO(alias, alias_crypto, "crypto-" name)

/*
 * Algorithm masks and types.
 */
#define CRYPTO_ALG_TYPE_MASK		0x0000000f
#define CRYPTO_ALG_TYPE_CIPHER		0x00000001
#define CRYPTO_ALG_TYPE_COMPRESS	0x00000002
#define CRYPTO_ALG_TYPE_AEAD		0x00000003
#define CRYPTO_ALG_TYPE_BLKCIPHER	0x00000004
#define CRYPTO_ALG_TYPE_ABLKCIPHER	0x00000005
#define CRYPTO_ALG_TYPE_GIVCIPHER	0x00000006
#define CRYPTO_ALG_TYPE_DIGEST		0x00000008
#define CRYPTO_ALG_TYPE_HASH		0x00000008
#define CRYPTO_ALG_TYPE_SHASH		0x00000009
#define CRYPTO_ALG_TYPE_AHASH		0x0000000a
#define CRYPTO_ALG_TYPE_RNG		0x0000000c
#define CRYPTO_ALG_TYPE_PCOMPRESS	0x0000000f

#define CRYPTO_ALG_TYPE_HASH_MASK	0x0000000e
#define CRYPTO_ALG_TYPE_AHASH_MASK	0x0000000c
#define CRYPTO_ALG_TYPE_BLKCIPHER_MASK	0x0000000c

#define CRYPTO_ALG_LARVAL		0x00000010
#define CRYPTO_ALG_DEAD			0x00000020
#define CRYPTO_ALG_DYING		0x00000040
#define CRYPTO_ALG_ASYNC		0x00000080

/*
 * Set this bit if and only if the algorithm requires another algorithm of
 * the same type to handle corner cases.
 */
#define CRYPTO_ALG_NEED_FALLBACK	0x00000100

/*
 * This bit is set for symmetric key ciphers that have already been wrapped
 * with a generic IV generator to prevent them from being wrapped again.
 */
#define CRYPTO_ALG_GENIV		0x00000200

/*
 * Set if the algorithm has passed automated run-time testing.  Note that
 * if there is no run-time testing for a given algorithm it is considered
 * to have passed.
 */

#define CRYPTO_ALG_TESTED		0x00000400

/*
 * Set if the algorithm is an instance that is build from templates.
 */
#define CRYPTO_ALG_INSTANCE		0x00000800

/* Set this bit if the algorithm provided is hardware accelerated but
 * not available to userspace via instruction set or so.
 */
#define CRYPTO_ALG_KERN_DRIVER_ONLY	0x00001000

/*
 * Mark a cipher as a service implementation only usable by another
 * cipher and never by a normal user of the kernel crypto API
 */
#define CRYPTO_ALG_INTERNAL		0x00002000

/*
 * Transform masks and values (for crt_flags).
 */
#define CRYPTO_TFM_REQ_MASK		0x000fff00
#define CRYPTO_TFM_RES_MASK		0xfff00000

#define CRYPTO_TFM_REQ_WEAK_KEY		0x00000100
#define CRYPTO_TFM_REQ_MAY_SLEEP	0x00000200
#define CRYPTO_TFM_REQ_MAY_BACKLOG	0x00000400
#define CRYPTO_TFM_RES_WEAK_KEY		0x00100000
#define CRYPTO_TFM_RES_BAD_KEY_LEN   	0x00200000
#define CRYPTO_TFM_RES_BAD_KEY_SCHED 	0x00400000
#define CRYPTO_TFM_RES_BAD_BLOCK_LEN 	0x00800000
#define CRYPTO_TFM_RES_BAD_FLAGS 	0x01000000

/*
 * Miscellaneous stuff.
 */
#define CRYPTO_MAX_ALG_NAME		64

/*
 * The macro CRYPTO_MINALIGN_ATTR (along with the void * type in the actual
 * declaration) is used to ensure that the crypto_tfm context structure is
 * aligned correctly for the given architecture so that there are no alignment
 * faults for C data types.  In particular, this is required on platforms such
 * as arm where pointers are 32-bit aligned but there are data types such as
 * u64 which require 64-bit alignment.
 */
#define CRYPTO_MINALIGN ARCH_KMALLOC_MINALIGN

#define CRYPTO_MINALIGN_ATTR __attribute__ ((__aligned__(CRYPTO_MINALIGN)))

struct scatterlist;
struct crypto_ablkcipher;
struct crypto_async_request;
struct crypto_aead;
struct crypto_blkcipher;
struct crypto_hash;
struct crypto_rng;
struct crypto_tfm;
struct crypto_type;
struct aead_givcrypt_request;
struct skcipher_givcrypt_request;

typedef void (*crypto_completion_t)(struct crypto_async_request *req, int err);

/**
 * DOC: Block Cipher Context Data Structures
 *
 * These data structures define the operating context for each block cipher
 * type.
 */

struct crypto_async_request {
	struct list_head list;
	crypto_completion_t complete;
	void *data;
	struct crypto_tfm *tfm;

	u32 flags;
};

struct ablkcipher_request {
	struct crypto_async_request base;

	unsigned int nbytes;

	void *info;

	struct scatterlist *src;
	struct scatterlist *dst;

	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

/**
 *	struct aead_request - AEAD request
 *	@base: Common attributes for async crypto requests
 *	@assoclen: Length in bytes of associated data for authentication
 *	@cryptlen: Length of data to be encrypted or decrypted
 *	@iv: Initialisation vector
 *	@assoc: Associated data
 *	@src: Source data
 *	@dst: Destination data
 *	@__ctx: Start of private context data
 */
struct aead_request {
	struct crypto_async_request base;

	unsigned int assoclen;
	unsigned int cryptlen;

	u8 *iv;

	struct scatterlist *assoc;
	struct scatterlist *src;
	struct scatterlist *dst;

	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

struct blkcipher_desc {
	struct crypto_blkcipher *tfm;
	void *info;
	u32 flags;
};

struct cipher_desc {
	struct crypto_tfm *tfm;
	void (*crfn)(struct crypto_tfm *tfm, u8 *dst, const u8 *src);
	unsigned int (*prfn)(const struct cipher_desc *desc, u8 *dst,
			     const u8 *src, unsigned int nbytes);
	void *info;
};

struct hash_desc {
	struct crypto_hash *tfm;
	u32 flags;
};

/**
 * DOC: Block Cipher Algorithm Definitions
 *
 * These data structures define modular crypto algorithm implementations,
 * managed via crypto_register_alg() and crypto_unregister_alg().
 */

/**
 * struct ablkcipher_alg - asynchronous block cipher definition
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
 * @givencrypt: Update the IV for encryption. With this function, a cipher
 *	        implementation may provide the function on how to update the IV
 *	        for encryption.
 * @givdecrypt: Update the IV for decryption. This is the reverse of
 *	        @givencrypt .
 * @geniv: The transformation implementation may use an "IV generator" provided
 *	   by the kernel crypto API. Several use cases have a predefined
 *	   approach how IVs are to be updated. For such use cases, the kernel
 *	   crypto API provides ready-to-use implementations that can be
 *	   referenced with this variable.
 * @ivsize: IV size applicable for transformation. The consumer must provide an
 *	    IV of exactly that size to perform the encrypt or decrypt operation.
 *
 * All fields except @givencrypt , @givdecrypt , @geniv and @ivsize are
 * mandatory and must be filled.
 */
struct ablkcipher_alg {
	int (*setkey)(struct crypto_ablkcipher *tfm, const u8 *key,
	              unsigned int keylen);
	int (*encrypt)(struct ablkcipher_request *req);
	int (*decrypt)(struct ablkcipher_request *req);
	int (*givencrypt)(struct skcipher_givcrypt_request *req);
	int (*givdecrypt)(struct skcipher_givcrypt_request *req);

	const char *geniv;

	unsigned int min_keysize;
	unsigned int max_keysize;
	unsigned int ivsize;
};

/**
 * struct aead_alg - AEAD cipher definition
 * @maxauthsize: Set the maximum authentication tag size supported by the
 *		 transformation. A transformation may support smaller tag sizes.
 *		 As the authentication tag is a message digest to ensure the
 *		 integrity of the encrypted data, a consumer typically wants the
 *		 largest authentication tag possible as defined by this
 *		 variable.
 * @setauthsize: Set authentication size for the AEAD transformation. This
 *		 function is used to specify the consumer requested size of the
 * 		 authentication tag to be either generated by the transformation
 *		 during encryption or the size of the authentication tag to be
 *		 supplied during the decryption operation. This function is also
 *		 responsible for checking the authentication tag size for
 *		 validity.
 * @setkey: see struct ablkcipher_alg
 * @encrypt: see struct ablkcipher_alg
 * @decrypt: see struct ablkcipher_alg
 * @givencrypt: see struct ablkcipher_alg
 * @givdecrypt: see struct ablkcipher_alg
 * @geniv: see struct ablkcipher_alg
 * @ivsize: see struct ablkcipher_alg
 *
 * All fields except @givencrypt , @givdecrypt , @geniv and @ivsize are
 * mandatory and must be filled.
 */
struct aead_alg {
	int (*setkey)(struct crypto_aead *tfm, const u8 *key,
	              unsigned int keylen);
	int (*setauthsize)(struct crypto_aead *tfm, unsigned int authsize);
	int (*encrypt)(struct aead_request *req);
	int (*decrypt)(struct aead_request *req);
	int (*givencrypt)(struct aead_givcrypt_request *req);
	int (*givdecrypt)(struct aead_givcrypt_request *req);

	const char *geniv;

	unsigned int ivsize;
	unsigned int maxauthsize;
};

/**
 * struct blkcipher_alg - synchronous block cipher definition
 * @min_keysize: see struct ablkcipher_alg
 * @max_keysize: see struct ablkcipher_alg
 * @setkey: see struct ablkcipher_alg
 * @encrypt: see struct ablkcipher_alg
 * @decrypt: see struct ablkcipher_alg
 * @geniv: see struct ablkcipher_alg
 * @ivsize: see struct ablkcipher_alg
 *
 * All fields except @geniv and @ivsize are mandatory and must be filled.
 */
struct blkcipher_alg {
	int (*setkey)(struct crypto_tfm *tfm, const u8 *key,
	              unsigned int keylen);
	int (*encrypt)(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes);
	int (*decrypt)(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes);

	const char *geniv;

	unsigned int min_keysize;
	unsigned int max_keysize;
	unsigned int ivsize;
};

/**
 * struct cipher_alg - single-block symmetric ciphers definition
 * @cia_min_keysize: Minimum key size supported by the transformation. This is
 *		     the smallest key length supported by this transformation
 *		     algorithm. This must be set to one of the pre-defined
 *		     values as this is not hardware specific. Possible values
 *		     for this field can be found via git grep "_MIN_KEY_SIZE"
 *		     include/crypto/
 * @cia_max_keysize: Maximum key size supported by the transformation. This is
 *		    the largest key length supported by this transformation
 *		    algorithm. This must be set to one of the pre-defined values
 *		    as this is not hardware specific. Possible values for this
 *		    field can be found via git grep "_MAX_KEY_SIZE"
 *		    include/crypto/
 * @cia_setkey: Set key for the transformation. This function is used to either
 *	        program a supplied key into the hardware or store the key in the
 *	        transformation context for programming it later. Note that this
 *	        function does modify the transformation context. This function
 *	        can be called multiple times during the existence of the
 *	        transformation object, so one must make sure the key is properly
 *	        reprogrammed into the hardware. This function is also
 *	        responsible for checking the key length for validity.
 * @cia_encrypt: Encrypt a single block. This function is used to encrypt a
 *		 single block of data, which must be @cra_blocksize big. This
 *		 always operates on a full @cra_blocksize and it is not possible
 *		 to encrypt a block of smaller size. The supplied buffers must
 *		 therefore also be at least of @cra_blocksize size. Both the
 *		 input and output buffers are always aligned to @cra_alignmask.
 *		 In case either of the input or output buffer supplied by user
 *		 of the crypto API is not aligned to @cra_alignmask, the crypto
 *		 API will re-align the buffers. The re-alignment means that a
 *		 new buffer will be allocated, the data will be copied into the
 *		 new buffer, then the processing will happen on the new buffer,
 *		 then the data will be copied back into the original buffer and
 *		 finally the new buffer will be freed. In case a software
 *		 fallback was put in place in the @cra_init call, this function
 *		 might need to use the fallback if the algorithm doesn't support
 *		 all of the key sizes. In case the key was stored in
 *		 transformation context, the key might need to be re-programmed
 *		 into the hardware in this function. This function shall not
 *		 modify the transformation context, as this function may be
 *		 called in parallel with the same transformation object.
 * @cia_decrypt: Decrypt a single block. This is a reverse counterpart to
 *		 @cia_encrypt, and the conditions are exactly the same.
 *
 * All fields are mandatory and must be filled.
 */
struct cipher_alg {
	unsigned int cia_min_keysize;
	unsigned int cia_max_keysize;
	int (*cia_setkey)(struct crypto_tfm *tfm, const u8 *key,
	                  unsigned int keylen);
	void (*cia_encrypt)(struct crypto_tfm *tfm, u8 *dst, const u8 *src);
	void (*cia_decrypt)(struct crypto_tfm *tfm, u8 *dst, const u8 *src);
};

struct compress_alg {
	int (*coa_compress)(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen);
	int (*coa_decompress)(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen);
};

/**
 * struct rng_alg - random number generator definition
 * @rng_make_random: The function defined by this variable obtains a random
 *		     number. The random number generator transform must generate
 *		     the random number out of the context provided with this
 *		     call.
 * @rng_reset: Reset of the random number generator by clearing the entire state.
 *	       With the invocation of this function call, the random number
 *             generator shall completely reinitialize its state. If the random
 *	       number generator requires a seed for setting up a new state,
 *	       the seed must be provided by the consumer while invoking this
 *	       function. The required size of the seed is defined with
 *	       @seedsize .
 * @seedsize: The seed size required for a random number generator
 *	      initialization defined with this variable. Some random number
 *	      generators like the SP800-90A DRBG does not require a seed as the
 *	      seeding is implemented internally without the need of support by
 *	      the consumer. In this case, the seed size is set to zero.
 */
struct rng_alg {
	int (*rng_make_random)(struct crypto_rng *tfm, u8 *rdata,
			       unsigned int dlen);
	int (*rng_reset)(struct crypto_rng *tfm, u8 *seed, unsigned int slen);

	unsigned int seedsize;
};


#define cra_ablkcipher	cra_u.ablkcipher
#define cra_aead	cra_u.aead
#define cra_blkcipher	cra_u.blkcipher
#define cra_cipher	cra_u.cipher
#define cra_compress	cra_u.compress
#define cra_rng		cra_u.rng

/**
 * struct crypto_alg - definition of a cryptograpic cipher algorithm
 * @cra_flags: Flags describing this transformation. See include/linux/crypto.h
 *	       CRYPTO_ALG_* flags for the flags which go in here. Those are
 *	       used for fine-tuning the description of the transformation
 *	       algorithm.
 * @cra_blocksize: Minimum block size of this transformation. The size in bytes
 *		   of the smallest possible unit which can be transformed with
 *		   this algorithm. The users must respect this value.
 *		   In case of HASH transformation, it is possible for a smaller
 *		   block than @cra_blocksize to be passed to the crypto API for
 *		   transformation, in case of any other transformation type, an
 * 		   error will be returned upon any attempt to transform smaller
 *		   than @cra_blocksize chunks.
 * @cra_ctxsize: Size of the operational context of the transformation. This
 *		 value informs the kernel crypto API about the memory size
 *		 needed to be allocated for the transformation context.
 * @cra_alignmask: Alignment mask for the input and output data buffer. The data
 *		   buffer containing the input data for the algorithm must be
 *		   aligned to this alignment mask. The data buffer for the
 *		   output data must be aligned to this alignment mask. Note that
 *		   the Crypto API will do the re-alignment in software, but
 *		   only under special conditions and there is a performance hit.
 *		   The re-alignment happens at these occasions for different
 *		   @cra_u types: cipher -- For both input data and output data
 *		   buffer; ahash -- For output hash destination buf; shash --
 *		   For output hash destination buf.
 *		   This is needed on hardware which is flawed by design and
 *		   cannot pick data from arbitrary addresses.
 * @cra_priority: Priority of this transformation implementation. In case
 *		  multiple transformations with same @cra_name are available to
 *		  the Crypto API, the kernel will use the one with highest
 *		  @cra_priority.
 * @cra_name: Generic name (usable by multiple implementations) of the
 *	      transformation algorithm. This is the name of the transformation
 *	      itself. This field is used by the kernel when looking up the
 *	      providers of particular transformation.
 * @cra_driver_name: Unique name of the transformation provider. This is the
 *		     name of the provider of the transformation. This can be any
 *		     arbitrary value, but in the usual case, this contains the
 *		     name of the chip or provider and the name of the
 *		     transformation algorithm.
 * @cra_type: Type of the cryptographic transformation. This is a pointer to
 *	      struct crypto_type, which implements callbacks common for all
 *	      trasnformation types. There are multiple options:
 *	      &crypto_blkcipher_type, &crypto_ablkcipher_type,
 *	      &crypto_ahash_type, &crypto_aead_type, &crypto_rng_type.
 *	      This field might be empty. In that case, there are no common
 *	      callbacks. This is the case for: cipher, compress, shash.
 * @cra_u: Callbacks implementing the transformation. This is a union of
 *	   multiple structures. Depending on the type of transformation selected
 *	   by @cra_type and @cra_flags above, the associated structure must be
 *	   filled with callbacks. This field might be empty. This is the case
 *	   for ahash, shash.
 * @cra_init: Initialize the cryptographic transformation object. This function
 *	      is used to initialize the cryptographic transformation object.
 *	      This function is called only once at the instantiation time, right
 *	      after the transformation context was allocated. In case the
 *	      cryptographic hardware has some special requirements which need to
 *	      be handled by software, this function shall check for the precise
 *	      requirement of the transformation and put any software fallbacks
 *	      in place.
 * @cra_exit: Deinitialize the cryptographic transformation object. This is a
 *	      counterpart to @cra_init, used to remove various changes set in
 *	      @cra_init.
 * @cra_module: Owner of this transformation implementation. Set to THIS_MODULE
 * @cra_list: internally used
 * @cra_users: internally used
 * @cra_refcnt: internally used
 * @cra_destroy: internally used
 *
 * The struct crypto_alg describes a generic Crypto API algorithm and is common
 * for all of the transformations. Any variable not documented here shall not
 * be used by a cipher implementation as it is internal to the Crypto API.
 */
struct crypto_alg {
	struct list_head cra_list;
	struct list_head cra_users;

	u32 cra_flags;
	unsigned int cra_blocksize;
	unsigned int cra_ctxsize;
	unsigned int cra_alignmask;

	int cra_priority;
	atomic_t cra_refcnt;

	char cra_name[CRYPTO_MAX_ALG_NAME];
	char cra_driver_name[CRYPTO_MAX_ALG_NAME];

	const struct crypto_type *cra_type;

	union {
		struct ablkcipher_alg ablkcipher;
		struct aead_alg aead;
		struct blkcipher_alg blkcipher;
		struct cipher_alg cipher;
		struct compress_alg compress;
		struct rng_alg rng;
	} cra_u;

	int (*cra_init)(struct crypto_tfm *tfm);
	void (*cra_exit)(struct crypto_tfm *tfm);
	void (*cra_destroy)(struct crypto_alg *alg);
	
	struct module *cra_module;
};

/*
 * Algorithm registration interface.
 */
int crypto_register_alg(struct crypto_alg *alg);
int crypto_unregister_alg(struct crypto_alg *alg);
int crypto_register_algs(struct crypto_alg *algs, int count);
int crypto_unregister_algs(struct crypto_alg *algs, int count);

/*
 * Algorithm query interface.
 */
int crypto_has_alg(const char *name, u32 type, u32 mask);

/*
 * Transforms: user-instantiated objects which encapsulate algorithms
 * and core processing logic.  Managed via crypto_alloc_*() and
 * crypto_free_*(), as well as the various helpers below.
 */

struct ablkcipher_tfm {
	int (*setkey)(struct crypto_ablkcipher *tfm, const u8 *key,
	              unsigned int keylen);
	int (*encrypt)(struct ablkcipher_request *req);
	int (*decrypt)(struct ablkcipher_request *req);
	int (*givencrypt)(struct skcipher_givcrypt_request *req);
	int (*givdecrypt)(struct skcipher_givcrypt_request *req);

	struct crypto_ablkcipher *base;

	unsigned int ivsize;
	unsigned int reqsize;
};

struct aead_tfm {
	int (*setkey)(struct crypto_aead *tfm, const u8 *key,
	              unsigned int keylen);
	int (*encrypt)(struct aead_request *req);
	int (*decrypt)(struct aead_request *req);
	int (*givencrypt)(struct aead_givcrypt_request *req);
	int (*givdecrypt)(struct aead_givcrypt_request *req);

	struct crypto_aead *base;

	unsigned int ivsize;
	unsigned int authsize;
	unsigned int reqsize;
};

struct blkcipher_tfm {
	void *iv;
	int (*setkey)(struct crypto_tfm *tfm, const u8 *key,
		      unsigned int keylen);
	int (*encrypt)(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes);
	int (*decrypt)(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes);
};

struct cipher_tfm {
	int (*cit_setkey)(struct crypto_tfm *tfm,
	                  const u8 *key, unsigned int keylen);
	void (*cit_encrypt_one)(struct crypto_tfm *tfm, u8 *dst, const u8 *src);
	void (*cit_decrypt_one)(struct crypto_tfm *tfm, u8 *dst, const u8 *src);
};

struct hash_tfm {
	int (*init)(struct hash_desc *desc);
	int (*update)(struct hash_desc *desc,
		      struct scatterlist *sg, unsigned int nsg);
	int (*final)(struct hash_desc *desc, u8 *out);
	int (*digest)(struct hash_desc *desc, struct scatterlist *sg,
		      unsigned int nsg, u8 *out);
	int (*setkey)(struct crypto_hash *tfm, const u8 *key,
		      unsigned int keylen);
	unsigned int digestsize;
};

struct compress_tfm {
	int (*cot_compress)(struct crypto_tfm *tfm,
	                    const u8 *src, unsigned int slen,
	                    u8 *dst, unsigned int *dlen);
	int (*cot_decompress)(struct crypto_tfm *tfm,
	                      const u8 *src, unsigned int slen,
	                      u8 *dst, unsigned int *dlen);
};

struct rng_tfm {
	int (*rng_gen_random)(struct crypto_rng *tfm, u8 *rdata,
			      unsigned int dlen);
	int (*rng_reset)(struct crypto_rng *tfm, u8 *seed, unsigned int slen);
};

#define crt_ablkcipher	crt_u.ablkcipher
#define crt_aead	crt_u.aead
#define crt_blkcipher	crt_u.blkcipher
#define crt_cipher	crt_u.cipher
#define crt_hash	crt_u.hash
#define crt_compress	crt_u.compress
#define crt_rng		crt_u.rng

struct crypto_tfm {

	u32 crt_flags;
	
	union {
		struct ablkcipher_tfm ablkcipher;
		struct aead_tfm aead;
		struct blkcipher_tfm blkcipher;
		struct cipher_tfm cipher;
		struct hash_tfm hash;
		struct compress_tfm compress;
		struct rng_tfm rng;
	} crt_u;

	void (*exit)(struct crypto_tfm *tfm);
	
	struct crypto_alg *__crt_alg;

	void *__crt_ctx[] CRYPTO_MINALIGN_ATTR;
};

struct crypto_ablkcipher {
	struct crypto_tfm base;
};

struct crypto_aead {
	struct crypto_tfm base;
};

struct crypto_blkcipher {
	struct crypto_tfm base;
};

struct crypto_cipher {
	struct crypto_tfm base;
};

struct crypto_comp {
	struct crypto_tfm base;
};

struct crypto_hash {
	struct crypto_tfm base;
};

struct crypto_rng {
	struct crypto_tfm base;
};

enum {
	CRYPTOA_UNSPEC,
	CRYPTOA_ALG,
	CRYPTOA_TYPE,
	CRYPTOA_U32,
	__CRYPTOA_MAX,
};

#define CRYPTOA_MAX (__CRYPTOA_MAX - 1)

/* Maximum number of (rtattr) parameters for each template. */
#define CRYPTO_MAX_ATTRS 32

struct crypto_attr_alg {
	char name[CRYPTO_MAX_ALG_NAME];
};

struct crypto_attr_type {
	u32 type;
	u32 mask;
};

struct crypto_attr_u32 {
	u32 num;
};

/* 
 * Transform user interface.
 */
 
struct crypto_tfm *crypto_alloc_base(const char *alg_name, u32 type, u32 mask);
void crypto_destroy_tfm(void *mem, struct crypto_tfm *tfm);

static inline void crypto_free_tfm(struct crypto_tfm *tfm)
{
	return crypto_destroy_tfm(tfm, tfm);
}

int alg_test(const char *driver, const char *alg, u32 type, u32 mask);

/*
 * Transform helpers which query the underlying algorithm.
 */
static inline const char *crypto_tfm_alg_name(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_name;
}

static inline const char *crypto_tfm_alg_driver_name(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_driver_name;
}

static inline int crypto_tfm_alg_priority(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_priority;
}

static inline u32 crypto_tfm_alg_type(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_flags & CRYPTO_ALG_TYPE_MASK;
}

static inline unsigned int crypto_tfm_alg_blocksize(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_blocksize;
}

static inline unsigned int crypto_tfm_alg_alignmask(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_alignmask;
}

static inline u32 crypto_tfm_get_flags(struct crypto_tfm *tfm)
{
	return tfm->crt_flags;
}

static inline void crypto_tfm_set_flags(struct crypto_tfm *tfm, u32 flags)
{
	tfm->crt_flags |= flags;
}

static inline void crypto_tfm_clear_flags(struct crypto_tfm *tfm, u32 flags)
{
	tfm->crt_flags &= ~flags;
}

static inline void *crypto_tfm_ctx(struct crypto_tfm *tfm)
{
	return tfm->__crt_ctx;
}

static inline unsigned int crypto_tfm_ctx_alignment(void)
{
	struct crypto_tfm *tfm;
	return __alignof__(tfm->__crt_ctx);
}

/*
 * API wrappers.
 */
static inline struct crypto_ablkcipher *__crypto_ablkcipher_cast(
	struct crypto_tfm *tfm)
{
	return (struct crypto_ablkcipher *)tfm;
}

static inline u32 crypto_skcipher_type(u32 type)
{
	type &= ~(CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_GENIV);
	type |= CRYPTO_ALG_TYPE_BLKCIPHER;
	return type;
}

static inline u32 crypto_skcipher_mask(u32 mask)
{
	mask &= ~(CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_GENIV);
	mask |= CRYPTO_ALG_TYPE_BLKCIPHER_MASK;
	return mask;
}

/**
 * DOC: Asynchronous Block Cipher API
 *
 * Asynchronous block cipher API is used with the ciphers of type
 * CRYPTO_ALG_TYPE_ABLKCIPHER (listed as type "ablkcipher" in /proc/crypto).
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
 * information is given by filling in the ablkcipher_request data structure.
 *
 * For the asynchronous block cipher API, the state is maintained with the tfm
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

/**
 * crypto_alloc_ablkcipher() - allocate asynchronous block cipher handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      ablkcipher cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Allocate a cipher handle for an ablkcipher. The returned struct
 * crypto_ablkcipher is the cipher handle that is required for any subsequent
 * API invocation for that ablkcipher.
 *
 * Return: allocated cipher handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
struct crypto_ablkcipher *crypto_alloc_ablkcipher(const char *alg_name,
						  u32 type, u32 mask);

static inline struct crypto_tfm *crypto_ablkcipher_tfm(
	struct crypto_ablkcipher *tfm)
{
	return &tfm->base;
}

/**
 * crypto_free_ablkcipher() - zeroize and free cipher handle
 * @tfm: cipher handle to be freed
 */
static inline void crypto_free_ablkcipher(struct crypto_ablkcipher *tfm)
{
	crypto_free_tfm(crypto_ablkcipher_tfm(tfm));
}

/**
 * crypto_has_ablkcipher() - Search for the availability of an ablkcipher.
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      ablkcipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Return: true when the ablkcipher is known to the kernel crypto API; false
 *	   otherwise
 */
static inline int crypto_has_ablkcipher(const char *alg_name, u32 type,
					u32 mask)
{
	return crypto_has_alg(alg_name, crypto_skcipher_type(type),
			      crypto_skcipher_mask(mask));
}

static inline struct ablkcipher_tfm *crypto_ablkcipher_crt(
	struct crypto_ablkcipher *tfm)
{
	return &crypto_ablkcipher_tfm(tfm)->crt_ablkcipher;
}

/**
 * crypto_ablkcipher_ivsize() - obtain IV size
 * @tfm: cipher handle
 *
 * The size of the IV for the ablkcipher referenced by the cipher handle is
 * returned. This IV size may be zero if the cipher does not need an IV.
 *
 * Return: IV size in bytes
 */
static inline unsigned int crypto_ablkcipher_ivsize(
	struct crypto_ablkcipher *tfm)
{
	return crypto_ablkcipher_crt(tfm)->ivsize;
}

/**
 * crypto_ablkcipher_blocksize() - obtain block size of cipher
 * @tfm: cipher handle
 *
 * The block size for the ablkcipher referenced with the cipher handle is
 * returned. The caller may use that information to allocate appropriate
 * memory for the data returned by the encryption or decryption operation
 *
 * Return: block size of cipher
 */
static inline unsigned int crypto_ablkcipher_blocksize(
	struct crypto_ablkcipher *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_ablkcipher_tfm(tfm));
}

static inline unsigned int crypto_ablkcipher_alignmask(
	struct crypto_ablkcipher *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_ablkcipher_tfm(tfm));
}

static inline u32 crypto_ablkcipher_get_flags(struct crypto_ablkcipher *tfm)
{
	return crypto_tfm_get_flags(crypto_ablkcipher_tfm(tfm));
}

static inline void crypto_ablkcipher_set_flags(struct crypto_ablkcipher *tfm,
					       u32 flags)
{
	crypto_tfm_set_flags(crypto_ablkcipher_tfm(tfm), flags);
}

static inline void crypto_ablkcipher_clear_flags(struct crypto_ablkcipher *tfm,
						 u32 flags)
{
	crypto_tfm_clear_flags(crypto_ablkcipher_tfm(tfm), flags);
}

/**
 * crypto_ablkcipher_setkey() - set key for cipher
 * @tfm: cipher handle
 * @key: buffer holding the key
 * @keylen: length of the key in bytes
 *
 * The caller provided key is set for the ablkcipher referenced by the cipher
 * handle.
 *
 * Note, the key length determines the cipher type. Many block ciphers implement
 * different cipher modes depending on the key size, such as AES-128 vs AES-192
 * vs. AES-256. When providing a 16 byte key for an AES cipher handle, AES-128
 * is performed.
 *
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
static inline int crypto_ablkcipher_setkey(struct crypto_ablkcipher *tfm,
					   const u8 *key, unsigned int keylen)
{
	struct ablkcipher_tfm *crt = crypto_ablkcipher_crt(tfm);

	return crt->setkey(crt->base, key, keylen);
}

/**
 * crypto_ablkcipher_reqtfm() - obtain cipher handle from request
 * @req: ablkcipher_request out of which the cipher handle is to be obtained
 *
 * Return the crypto_ablkcipher handle when furnishing an ablkcipher_request
 * data structure.
 *
 * Return: crypto_ablkcipher handle
 */
static inline struct crypto_ablkcipher *crypto_ablkcipher_reqtfm(
	struct ablkcipher_request *req)
{
	return __crypto_ablkcipher_cast(req->base.tfm);
}

/**
 * crypto_ablkcipher_encrypt() - encrypt plaintext
 * @req: reference to the ablkcipher_request handle that holds all information
 *	 needed to perform the cipher operation
 *
 * Encrypt plaintext data using the ablkcipher_request handle. That data
 * structure and how it is filled with data is discussed with the
 * ablkcipher_request_* functions.
 *
 * Return: 0 if the cipher operation was successful; < 0 if an error occurred
 */
static inline int crypto_ablkcipher_encrypt(struct ablkcipher_request *req)
{
	struct ablkcipher_tfm *crt =
		crypto_ablkcipher_crt(crypto_ablkcipher_reqtfm(req));
	return crt->encrypt(req);
}

/**
 * crypto_ablkcipher_decrypt() - decrypt ciphertext
 * @req: reference to the ablkcipher_request handle that holds all information
 *	 needed to perform the cipher operation
 *
 * Decrypt ciphertext data using the ablkcipher_request handle. That data
 * structure and how it is filled with data is discussed with the
 * ablkcipher_request_* functions.
 *
 * Return: 0 if the cipher operation was successful; < 0 if an error occurred
 */
static inline int crypto_ablkcipher_decrypt(struct ablkcipher_request *req)
{
	struct ablkcipher_tfm *crt =
		crypto_ablkcipher_crt(crypto_ablkcipher_reqtfm(req));
	return crt->decrypt(req);
}

/**
 * DOC: Asynchronous Cipher Request Handle
 *
 * The ablkcipher_request data structure contains all pointers to data
 * required for the asynchronous cipher operation. This includes the cipher
 * handle (which can be used by multiple ablkcipher_request instances), pointer
 * to plaintext and ciphertext, asynchronous callback function, etc. It acts
 * as a handle to the ablkcipher_request_* API calls in a similar way as
 * ablkcipher handle to the crypto_ablkcipher_* API calls.
 */

/**
 * crypto_ablkcipher_reqsize() - obtain size of the request data structure
 * @tfm: cipher handle
 *
 * Return: number of bytes
 */
static inline unsigned int crypto_ablkcipher_reqsize(
	struct crypto_ablkcipher *tfm)
{
	return crypto_ablkcipher_crt(tfm)->reqsize;
}

/**
 * ablkcipher_request_set_tfm() - update cipher handle reference in request
 * @req: request handle to be modified
 * @tfm: cipher handle that shall be added to the request handle
 *
 * Allow the caller to replace the existing ablkcipher handle in the request
 * data structure with a different one.
 */
static inline void ablkcipher_request_set_tfm(
	struct ablkcipher_request *req, struct crypto_ablkcipher *tfm)
{
	req->base.tfm = crypto_ablkcipher_tfm(crypto_ablkcipher_crt(tfm)->base);
}

static inline struct ablkcipher_request *ablkcipher_request_cast(
	struct crypto_async_request *req)
{
	return container_of(req, struct ablkcipher_request, base);
}

/**
 * ablkcipher_request_alloc() - allocate request data structure
 * @tfm: cipher handle to be registered with the request
 * @gfp: memory allocation flag that is handed to kmalloc by the API call.
 *
 * Allocate the request data structure that must be used with the ablkcipher
 * encrypt and decrypt API calls. During the allocation, the provided ablkcipher
 * handle is registered in the request data structure.
 *
 * Return: allocated request handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
static inline struct ablkcipher_request *ablkcipher_request_alloc(
	struct crypto_ablkcipher *tfm, gfp_t gfp)
{
	struct ablkcipher_request *req;

	req = kmalloc(sizeof(struct ablkcipher_request) +
		      crypto_ablkcipher_reqsize(tfm), gfp);

	if (likely(req))
		ablkcipher_request_set_tfm(req, tfm);

	return req;
}

/**
 * ablkcipher_request_free() - zeroize and free request data structure
 * @req: request data structure cipher handle to be freed
 */
static inline void ablkcipher_request_free(struct ablkcipher_request *req)
{
	kzfree(req);
}

/**
 * ablkcipher_request_set_callback() - set asynchronous callback function
 * @req: request handle
 * @flags: specify zero or an ORing of the flags
 *         CRYPTO_TFM_REQ_MAY_BACKLOG the request queue may back log and
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
 * The callback function is registered with the ablkcipher_request handle and
 * must comply with the following template
 *
 *	void callback_function(struct crypto_async_request *req, int error)
 */
static inline void ablkcipher_request_set_callback(
	struct ablkcipher_request *req,
	u32 flags, crypto_completion_t compl, void *data)
{
	req->base.complete = compl;
	req->base.data = data;
	req->base.flags = flags;
}

/**
 * ablkcipher_request_set_crypt() - set data buffers
 * @req: request handle
 * @src: source scatter / gather list
 * @dst: destination scatter / gather list
 * @nbytes: number of bytes to process from @src
 * @iv: IV for the cipher operation which must comply with the IV size defined
 *      by crypto_ablkcipher_ivsize
 *
 * This function allows setting of the source data and destination data
 * scatter / gather lists.
 *
 * For encryption, the source is treated as the plaintext and the
 * destination is the ciphertext. For a decryption operation, the use is
 * reversed - the source is the ciphertext and the destination is the plaintext.
 */
static inline void ablkcipher_request_set_crypt(
	struct ablkcipher_request *req,
	struct scatterlist *src, struct scatterlist *dst,
	unsigned int nbytes, void *iv)
{
	req->src = src;
	req->dst = dst;
	req->nbytes = nbytes;
	req->info = iv;
}

/**
 * DOC: Authenticated Encryption With Associated Data (AEAD) Cipher API
 *
 * The AEAD cipher API is used with the ciphers of type CRYPTO_ALG_TYPE_AEAD
 * (listed as type "aead" in /proc/crypto)
 *
 * The most prominent examples for this type of encryption is GCM and CCM.
 * However, the kernel supports other types of AEAD ciphers which are defined
 * with the following cipher string:
 *
 *	authenc(keyed message digest, block cipher)
 *
 * For example: authenc(hmac(sha256), cbc(aes))
 *
 * The example code provided for the asynchronous block cipher operation
 * applies here as well. Naturally all *ablkcipher* symbols must be exchanged
 * the *aead* pendants discussed in the following. In addtion, for the AEAD
 * operation, the aead_request_set_assoc function must be used to set the
 * pointer to the associated data memory location before performing the
 * encryption or decryption operation. In case of an encryption, the associated
 * data memory is filled during the encryption operation. For decryption, the
 * associated data memory must contain data that is used to verify the integrity
 * of the decrypted data. Another deviation from the asynchronous block cipher
 * operation is that the caller should explicitly check for -EBADMSG of the
 * crypto_aead_decrypt. That error indicates an authentication error, i.e.
 * a breach in the integrity of the message. In essence, that -EBADMSG error
 * code is the key bonus an AEAD cipher has over "standard" block chaining
 * modes.
 */

static inline struct crypto_aead *__crypto_aead_cast(struct crypto_tfm *tfm)
{
	return (struct crypto_aead *)tfm;
}

/**
 * crypto_alloc_aead() - allocate AEAD cipher handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	     AEAD cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Allocate a cipher handle for an AEAD. The returned struct
 * crypto_aead is the cipher handle that is required for any subsequent
 * API invocation for that AEAD.
 *
 * Return: allocated cipher handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
struct crypto_aead *crypto_alloc_aead(const char *alg_name, u32 type, u32 mask);

static inline struct crypto_tfm *crypto_aead_tfm(struct crypto_aead *tfm)
{
	return &tfm->base;
}

/**
 * crypto_free_aead() - zeroize and free aead handle
 * @tfm: cipher handle to be freed
 */
static inline void crypto_free_aead(struct crypto_aead *tfm)
{
	crypto_free_tfm(crypto_aead_tfm(tfm));
}

static inline struct aead_tfm *crypto_aead_crt(struct crypto_aead *tfm)
{
	return &crypto_aead_tfm(tfm)->crt_aead;
}

/**
 * crypto_aead_ivsize() - obtain IV size
 * @tfm: cipher handle
 *
 * The size of the IV for the aead referenced by the cipher handle is
 * returned. This IV size may be zero if the cipher does not need an IV.
 *
 * Return: IV size in bytes
 */
static inline unsigned int crypto_aead_ivsize(struct crypto_aead *tfm)
{
	return crypto_aead_crt(tfm)->ivsize;
}

/**
 * crypto_aead_authsize() - obtain maximum authentication data size
 * @tfm: cipher handle
 *
 * The maximum size of the authentication data for the AEAD cipher referenced
 * by the AEAD cipher handle is returned. The authentication data size may be
 * zero if the cipher implements a hard-coded maximum.
 *
 * The authentication data may also be known as "tag value".
 *
 * Return: authentication data size / tag size in bytes
 */
static inline unsigned int crypto_aead_authsize(struct crypto_aead *tfm)
{
	return crypto_aead_crt(tfm)->authsize;
}

/**
 * crypto_aead_blocksize() - obtain block size of cipher
 * @tfm: cipher handle
 *
 * The block size for the AEAD referenced with the cipher handle is returned.
 * The caller may use that information to allocate appropriate memory for the
 * data returned by the encryption or decryption operation
 *
 * Return: block size of cipher
 */
static inline unsigned int crypto_aead_blocksize(struct crypto_aead *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_aead_tfm(tfm));
}

static inline unsigned int crypto_aead_alignmask(struct crypto_aead *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_aead_tfm(tfm));
}

static inline u32 crypto_aead_get_flags(struct crypto_aead *tfm)
{
	return crypto_tfm_get_flags(crypto_aead_tfm(tfm));
}

static inline void crypto_aead_set_flags(struct crypto_aead *tfm, u32 flags)
{
	crypto_tfm_set_flags(crypto_aead_tfm(tfm), flags);
}

static inline void crypto_aead_clear_flags(struct crypto_aead *tfm, u32 flags)
{
	crypto_tfm_clear_flags(crypto_aead_tfm(tfm), flags);
}

/**
 * crypto_aead_setkey() - set key for cipher
 * @tfm: cipher handle
 * @key: buffer holding the key
 * @keylen: length of the key in bytes
 *
 * The caller provided key is set for the AEAD referenced by the cipher
 * handle.
 *
 * Note, the key length determines the cipher type. Many block ciphers implement
 * different cipher modes depending on the key size, such as AES-128 vs AES-192
 * vs. AES-256. When providing a 16 byte key for an AES cipher handle, AES-128
 * is performed.
 *
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
static inline int crypto_aead_setkey(struct crypto_aead *tfm, const u8 *key,
				     unsigned int keylen)
{
	struct aead_tfm *crt = crypto_aead_crt(tfm);

	return crt->setkey(crt->base, key, keylen);
}

/**
 * crypto_aead_setauthsize() - set authentication data size
 * @tfm: cipher handle
 * @authsize: size of the authentication data / tag in bytes
 *
 * Set the authentication data size / tag size. AEAD requires an authentication
 * tag (or MAC) in addition to the associated data.
 *
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
int crypto_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize);

static inline struct crypto_aead *crypto_aead_reqtfm(struct aead_request *req)
{
	return __crypto_aead_cast(req->base.tfm);
}

/**
 * crypto_aead_encrypt() - encrypt plaintext
 * @req: reference to the aead_request handle that holds all information
 *	 needed to perform the cipher operation
 *
 * Encrypt plaintext data using the aead_request handle. That data structure
 * and how it is filled with data is discussed with the aead_request_*
 * functions.
 *
 * IMPORTANT NOTE The encryption operation creates the authentication data /
 *		  tag. That data is concatenated with the created ciphertext.
 *		  The ciphertext memory size is therefore the given number of
 *		  block cipher blocks + the size defined by the
 *		  crypto_aead_setauthsize invocation. The caller must ensure
 *		  that sufficient memory is available for the ciphertext and
 *		  the authentication tag.
 *
 * Return: 0 if the cipher operation was successful; < 0 if an error occurred
 */
static inline int crypto_aead_encrypt(struct aead_request *req)
{
	return crypto_aead_crt(crypto_aead_reqtfm(req))->encrypt(req);
}

/**
 * crypto_aead_decrypt() - decrypt ciphertext
 * @req: reference to the ablkcipher_request handle that holds all information
 *	 needed to perform the cipher operation
 *
 * Decrypt ciphertext data using the aead_request handle. That data structure
 * and how it is filled with data is discussed with the aead_request_*
 * functions.
 *
 * IMPORTANT NOTE The caller must concatenate the ciphertext followed by the
 *		  authentication data / tag. That authentication data / tag
 *		  must have the size defined by the crypto_aead_setauthsize
 *		  invocation.
 *
 *
 * Return: 0 if the cipher operation was successful; -EBADMSG: The AEAD
 *	   cipher operation performs the authentication of the data during the
 *	   decryption operation. Therefore, the function returns this error if
 *	   the authentication of the ciphertext was unsuccessful (i.e. the
 *	   integrity of the ciphertext or the associated data was violated);
 *	   < 0 if an error occurred.
 */
static inline int crypto_aead_decrypt(struct aead_request *req)
{
	if (req->cryptlen < crypto_aead_authsize(crypto_aead_reqtfm(req)))
		return -EINVAL;

	return crypto_aead_crt(crypto_aead_reqtfm(req))->decrypt(req);
}

/**
 * DOC: Asynchronous AEAD Request Handle
 *
 * The aead_request data structure contains all pointers to data required for
 * the AEAD cipher operation. This includes the cipher handle (which can be
 * used by multiple aead_request instances), pointer to plaintext and
 * ciphertext, asynchronous callback function, etc. It acts as a handle to the
 * aead_request_* API calls in a similar way as AEAD handle to the
 * crypto_aead_* API calls.
 */

/**
 * crypto_aead_reqsize() - obtain size of the request data structure
 * @tfm: cipher handle
 *
 * Return: number of bytes
 */
static inline unsigned int crypto_aead_reqsize(struct crypto_aead *tfm)
{
	return crypto_aead_crt(tfm)->reqsize;
}

/**
 * aead_request_set_tfm() - update cipher handle reference in request
 * @req: request handle to be modified
 * @tfm: cipher handle that shall be added to the request handle
 *
 * Allow the caller to replace the existing aead handle in the request
 * data structure with a different one.
 */
static inline void aead_request_set_tfm(struct aead_request *req,
					struct crypto_aead *tfm)
{
	req->base.tfm = crypto_aead_tfm(crypto_aead_crt(tfm)->base);
}

/**
 * aead_request_alloc() - allocate request data structure
 * @tfm: cipher handle to be registered with the request
 * @gfp: memory allocation flag that is handed to kmalloc by the API call.
 *
 * Allocate the request data structure that must be used with the AEAD
 * encrypt and decrypt API calls. During the allocation, the provided aead
 * handle is registered in the request data structure.
 *
 * Return: allocated request handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
static inline struct aead_request *aead_request_alloc(struct crypto_aead *tfm,
						      gfp_t gfp)
{
	struct aead_request *req;

	req = kmalloc(sizeof(*req) + crypto_aead_reqsize(tfm), gfp);

	if (likely(req))
		aead_request_set_tfm(req, tfm);

	return req;
}

/**
 * aead_request_free() - zeroize and free request data structure
 * @req: request data structure cipher handle to be freed
 */
static inline void aead_request_free(struct aead_request *req)
{
	kzfree(req);
}

/**
 * aead_request_set_callback() - set asynchronous callback function
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
 * Setting the callback function that is triggered once the cipher operation
 * completes
 *
 * The callback function is registered with the aead_request handle and
 * must comply with the following template
 *
 *	void callback_function(struct crypto_async_request *req, int error)
 */
static inline void aead_request_set_callback(struct aead_request *req,
					     u32 flags,
					     crypto_completion_t compl,
					     void *data)
{
	req->base.complete = compl;
	req->base.data = data;
	req->base.flags = flags;
}

/**
 * aead_request_set_crypt - set data buffers
 * @req: request handle
 * @src: source scatter / gather list
 * @dst: destination scatter / gather list
 * @cryptlen: number of bytes to process from @src
 * @iv: IV for the cipher operation which must comply with the IV size defined
 *      by crypto_aead_ivsize()
 *
 * Setting the source data and destination data scatter / gather lists.
 *
 * For encryption, the source is treated as the plaintext and the
 * destination is the ciphertext. For a decryption operation, the use is
 * reversed - the source is the ciphertext and the destination is the plaintext.
 *
 * IMPORTANT NOTE AEAD requires an authentication tag (MAC). For decryption,
 *		  the caller must concatenate the ciphertext followed by the
 *		  authentication tag and provide the entire data stream to the
 *		  decryption operation (i.e. the data length used for the
 *		  initialization of the scatterlist and the data length for the
 *		  decryption operation is identical). For encryption, however,
 *		  the authentication tag is created while encrypting the data.
 *		  The destination buffer must hold sufficient space for the
 *		  ciphertext and the authentication tag while the encryption
 *		  invocation must only point to the plaintext data size. The
 *		  following code snippet illustrates the memory usage
 *		  buffer = kmalloc(ptbuflen + (enc ? authsize : 0));
 *		  sg_init_one(&sg, buffer, ptbuflen + (enc ? authsize : 0));
 *		  aead_request_set_crypt(req, &sg, &sg, ptbuflen, iv);
 */
static inline void aead_request_set_crypt(struct aead_request *req,
					  struct scatterlist *src,
					  struct scatterlist *dst,
					  unsigned int cryptlen, u8 *iv)
{
	req->src = src;
	req->dst = dst;
	req->cryptlen = cryptlen;
	req->iv = iv;
}

/**
 * aead_request_set_assoc() - set the associated data scatter / gather list
 * @req: request handle
 * @assoc: associated data scatter / gather list
 * @assoclen: number of bytes to process from @assoc
 *
 * For encryption, the memory is filled with the associated data. For
 * decryption, the memory must point to the associated data.
 */
static inline void aead_request_set_assoc(struct aead_request *req,
					  struct scatterlist *assoc,
					  unsigned int assoclen)
{
	req->assoc = assoc;
	req->assoclen = assoclen;
}

/**
 * DOC: Synchronous Block Cipher API
 *
 * The synchronous block cipher API is used with the ciphers of type
 * CRYPTO_ALG_TYPE_BLKCIPHER (listed as type "blkcipher" in /proc/crypto)
 *
 * Synchronous calls, have a context in the tfm. But since a single tfm can be
 * used in multiple calls and in parallel, this info should not be changeable
 * (unless a lock is used). This applies, for example, to the symmetric key.
 * However, the IV is changeable, so there is an iv field in blkcipher_tfm
 * structure for synchronous blkcipher api. So, its the only state info that can
 * be kept for synchronous calls without using a big lock across a tfm.
 *
 * The block cipher API allows the use of a complete cipher, i.e. a cipher
 * consisting of a template (a block chaining mode) and a single block cipher
 * primitive (e.g. AES).
 *
 * The plaintext data buffer and the ciphertext data buffer are pointed to
 * by using scatter/gather lists. The cipher operation is performed
 * on all segments of the provided scatter/gather lists.
 *
 * The kernel crypto API supports a cipher operation "in-place" which means that
 * the caller may provide the same scatter/gather list for the plaintext and
 * cipher text. After the completion of the cipher operation, the plaintext
 * data is replaced with the ciphertext data in case of an encryption and vice
 * versa for a decryption. The caller must ensure that the scatter/gather lists
 * for the output data point to sufficiently large buffers, i.e. multiples of
 * the block size of the cipher.
 */

static inline struct crypto_blkcipher *__crypto_blkcipher_cast(
	struct crypto_tfm *tfm)
{
	return (struct crypto_blkcipher *)tfm;
}

static inline struct crypto_blkcipher *crypto_blkcipher_cast(
	struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_BLKCIPHER);
	return __crypto_blkcipher_cast(tfm);
}

/**
 * crypto_alloc_blkcipher() - allocate synchronous block cipher handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      blkcipher cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Allocate a cipher handle for a block cipher. The returned struct
 * crypto_blkcipher is the cipher handle that is required for any subsequent
 * API invocation for that block cipher.
 *
 * Return: allocated cipher handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
static inline struct crypto_blkcipher *crypto_alloc_blkcipher(
	const char *alg_name, u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_BLKCIPHER;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return __crypto_blkcipher_cast(crypto_alloc_base(alg_name, type, mask));
}

static inline struct crypto_tfm *crypto_blkcipher_tfm(
	struct crypto_blkcipher *tfm)
{
	return &tfm->base;
}

/**
 * crypto_free_blkcipher() - zeroize and free the block cipher handle
 * @tfm: cipher handle to be freed
 */
static inline void crypto_free_blkcipher(struct crypto_blkcipher *tfm)
{
	crypto_free_tfm(crypto_blkcipher_tfm(tfm));
}

/**
 * crypto_has_blkcipher() - Search for the availability of a block cipher
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      block cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Return: true when the block cipher is known to the kernel crypto API; false
 *	   otherwise
 */
static inline int crypto_has_blkcipher(const char *alg_name, u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_BLKCIPHER;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return crypto_has_alg(alg_name, type, mask);
}

/**
 * crypto_blkcipher_name() - return the name / cra_name from the cipher handle
 * @tfm: cipher handle
 *
 * Return: The character string holding the name of the cipher
 */
static inline const char *crypto_blkcipher_name(struct crypto_blkcipher *tfm)
{
	return crypto_tfm_alg_name(crypto_blkcipher_tfm(tfm));
}

static inline struct blkcipher_tfm *crypto_blkcipher_crt(
	struct crypto_blkcipher *tfm)
{
	return &crypto_blkcipher_tfm(tfm)->crt_blkcipher;
}

static inline struct blkcipher_alg *crypto_blkcipher_alg(
	struct crypto_blkcipher *tfm)
{
	return &crypto_blkcipher_tfm(tfm)->__crt_alg->cra_blkcipher;
}

/**
 * crypto_blkcipher_ivsize() - obtain IV size
 * @tfm: cipher handle
 *
 * The size of the IV for the block cipher referenced by the cipher handle is
 * returned. This IV size may be zero if the cipher does not need an IV.
 *
 * Return: IV size in bytes
 */
static inline unsigned int crypto_blkcipher_ivsize(struct crypto_blkcipher *tfm)
{
	return crypto_blkcipher_alg(tfm)->ivsize;
}

/**
 * crypto_blkcipher_blocksize() - obtain block size of cipher
 * @tfm: cipher handle
 *
 * The block size for the block cipher referenced with the cipher handle is
 * returned. The caller may use that information to allocate appropriate
 * memory for the data returned by the encryption or decryption operation.
 *
 * Return: block size of cipher
 */
static inline unsigned int crypto_blkcipher_blocksize(
	struct crypto_blkcipher *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_blkcipher_tfm(tfm));
}

static inline unsigned int crypto_blkcipher_alignmask(
	struct crypto_blkcipher *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_blkcipher_tfm(tfm));
}

static inline u32 crypto_blkcipher_get_flags(struct crypto_blkcipher *tfm)
{
	return crypto_tfm_get_flags(crypto_blkcipher_tfm(tfm));
}

static inline void crypto_blkcipher_set_flags(struct crypto_blkcipher *tfm,
					      u32 flags)
{
	crypto_tfm_set_flags(crypto_blkcipher_tfm(tfm), flags);
}

static inline void crypto_blkcipher_clear_flags(struct crypto_blkcipher *tfm,
						u32 flags)
{
	crypto_tfm_clear_flags(crypto_blkcipher_tfm(tfm), flags);
}

/**
 * crypto_blkcipher_setkey() - set key for cipher
 * @tfm: cipher handle
 * @key: buffer holding the key
 * @keylen: length of the key in bytes
 *
 * The caller provided key is set for the block cipher referenced by the cipher
 * handle.
 *
 * Note, the key length determines the cipher type. Many block ciphers implement
 * different cipher modes depending on the key size, such as AES-128 vs AES-192
 * vs. AES-256. When providing a 16 byte key for an AES cipher handle, AES-128
 * is performed.
 *
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
static inline int crypto_blkcipher_setkey(struct crypto_blkcipher *tfm,
					  const u8 *key, unsigned int keylen)
{
	return crypto_blkcipher_crt(tfm)->setkey(crypto_blkcipher_tfm(tfm),
						 key, keylen);
}

/**
 * crypto_blkcipher_encrypt() - encrypt plaintext
 * @desc: reference to the block cipher handle with meta data
 * @dst: scatter/gather list that is filled by the cipher operation with the
 *	ciphertext
 * @src: scatter/gather list that holds the plaintext
 * @nbytes: number of bytes of the plaintext to encrypt.
 *
 * Encrypt plaintext data using the IV set by the caller with a preceding
 * call of crypto_blkcipher_set_iv.
 *
 * The blkcipher_desc data structure must be filled by the caller and can
 * reside on the stack. The caller must fill desc as follows: desc.tfm is filled
 * with the block cipher handle; desc.flags is filled with either
 * CRYPTO_TFM_REQ_MAY_SLEEP or 0.
 *
 * Return: 0 if the cipher operation was successful; < 0 if an error occurred
 */
static inline int crypto_blkcipher_encrypt(struct blkcipher_desc *desc,
					   struct scatterlist *dst,
					   struct scatterlist *src,
					   unsigned int nbytes)
{
	desc->info = crypto_blkcipher_crt(desc->tfm)->iv;
	return crypto_blkcipher_crt(desc->tfm)->encrypt(desc, dst, src, nbytes);
}

/**
 * crypto_blkcipher_encrypt_iv() - encrypt plaintext with dedicated IV
 * @desc: reference to the block cipher handle with meta data
 * @dst: scatter/gather list that is filled by the cipher operation with the
 *	ciphertext
 * @src: scatter/gather list that holds the plaintext
 * @nbytes: number of bytes of the plaintext to encrypt.
 *
 * Encrypt plaintext data with the use of an IV that is solely used for this
 * cipher operation. Any previously set IV is not used.
 *
 * The blkcipher_desc data structure must be filled by the caller and can
 * reside on the stack. The caller must fill desc as follows: desc.tfm is filled
 * with the block cipher handle; desc.info is filled with the IV to be used for
 * the current operation; desc.flags is filled with either
 * CRYPTO_TFM_REQ_MAY_SLEEP or 0.
 *
 * Return: 0 if the cipher operation was successful; < 0 if an error occurred
 */
static inline int crypto_blkcipher_encrypt_iv(struct blkcipher_desc *desc,
					      struct scatterlist *dst,
					      struct scatterlist *src,
					      unsigned int nbytes)
{
	return crypto_blkcipher_crt(desc->tfm)->encrypt(desc, dst, src, nbytes);
}

/**
 * crypto_blkcipher_decrypt() - decrypt ciphertext
 * @desc: reference to the block cipher handle with meta data
 * @dst: scatter/gather list that is filled by the cipher operation with the
 *	plaintext
 * @src: scatter/gather list that holds the ciphertext
 * @nbytes: number of bytes of the ciphertext to decrypt.
 *
 * Decrypt ciphertext data using the IV set by the caller with a preceding
 * call of crypto_blkcipher_set_iv.
 *
 * The blkcipher_desc data structure must be filled by the caller as documented
 * for the crypto_blkcipher_encrypt call above.
 *
 * Return: 0 if the cipher operation was successful; < 0 if an error occurred
 *
 */
static inline int crypto_blkcipher_decrypt(struct blkcipher_desc *desc,
					   struct scatterlist *dst,
					   struct scatterlist *src,
					   unsigned int nbytes)
{
	desc->info = crypto_blkcipher_crt(desc->tfm)->iv;
	return crypto_blkcipher_crt(desc->tfm)->decrypt(desc, dst, src, nbytes);
}

/**
 * crypto_blkcipher_decrypt_iv() - decrypt ciphertext with dedicated IV
 * @desc: reference to the block cipher handle with meta data
 * @dst: scatter/gather list that is filled by the cipher operation with the
 *	plaintext
 * @src: scatter/gather list that holds the ciphertext
 * @nbytes: number of bytes of the ciphertext to decrypt.
 *
 * Decrypt ciphertext data with the use of an IV that is solely used for this
 * cipher operation. Any previously set IV is not used.
 *
 * The blkcipher_desc data structure must be filled by the caller as documented
 * for the crypto_blkcipher_encrypt_iv call above.
 *
 * Return: 0 if the cipher operation was successful; < 0 if an error occurred
 */
static inline int crypto_blkcipher_decrypt_iv(struct blkcipher_desc *desc,
					      struct scatterlist *dst,
					      struct scatterlist *src,
					      unsigned int nbytes)
{
	return crypto_blkcipher_crt(desc->tfm)->decrypt(desc, dst, src, nbytes);
}

/**
 * crypto_blkcipher_set_iv() - set IV for cipher
 * @tfm: cipher handle
 * @src: buffer holding the IV
 * @len: length of the IV in bytes
 *
 * The caller provided IV is set for the block cipher referenced by the cipher
 * handle.
 */
static inline void crypto_blkcipher_set_iv(struct crypto_blkcipher *tfm,
					   const u8 *src, unsigned int len)
{
	memcpy(crypto_blkcipher_crt(tfm)->iv, src, len);
}

/**
 * crypto_blkcipher_get_iv() - obtain IV from cipher
 * @tfm: cipher handle
 * @dst: buffer filled with the IV
 * @len: length of the buffer dst
 *
 * The caller can obtain the IV set for the block cipher referenced by the
 * cipher handle and store it into the user-provided buffer. If the buffer
 * has an insufficient space, the IV is truncated to fit the buffer.
 */
static inline void crypto_blkcipher_get_iv(struct crypto_blkcipher *tfm,
					   u8 *dst, unsigned int len)
{
	memcpy(dst, crypto_blkcipher_crt(tfm)->iv, len);
}

/**
 * DOC: Single Block Cipher API
 *
 * The single block cipher API is used with the ciphers of type
 * CRYPTO_ALG_TYPE_CIPHER (listed as type "cipher" in /proc/crypto).
 *
 * Using the single block cipher API calls, operations with the basic cipher
 * primitive can be implemented. These cipher primitives exclude any block
 * chaining operations including IV handling.
 *
 * The purpose of this single block cipher API is to support the implementation
 * of templates or other concepts that only need to perform the cipher operation
 * on one block at a time. Templates invoke the underlying cipher primitive
 * block-wise and process either the input or the output data of these cipher
 * operations.
 */

static inline struct crypto_cipher *__crypto_cipher_cast(struct crypto_tfm *tfm)
{
	return (struct crypto_cipher *)tfm;
}

static inline struct crypto_cipher *crypto_cipher_cast(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return __crypto_cipher_cast(tfm);
}

/**
 * crypto_alloc_cipher() - allocate single block cipher handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	     single block cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Allocate a cipher handle for a single block cipher. The returned struct
 * crypto_cipher is the cipher handle that is required for any subsequent API
 * invocation for that single block cipher.
 *
 * Return: allocated cipher handle in case of success; IS_ERR() is true in case
 *	   of an error, PTR_ERR() returns the error code.
 */
static inline struct crypto_cipher *crypto_alloc_cipher(const char *alg_name,
							u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_CIPHER;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return __crypto_cipher_cast(crypto_alloc_base(alg_name, type, mask));
}

static inline struct crypto_tfm *crypto_cipher_tfm(struct crypto_cipher *tfm)
{
	return &tfm->base;
}

/**
 * crypto_free_cipher() - zeroize and free the single block cipher handle
 * @tfm: cipher handle to be freed
 */
static inline void crypto_free_cipher(struct crypto_cipher *tfm)
{
	crypto_free_tfm(crypto_cipher_tfm(tfm));
}

/**
 * crypto_has_cipher() - Search for the availability of a single block cipher
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	     single block cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Return: true when the single block cipher is known to the kernel crypto API;
 *	   false otherwise
 */
static inline int crypto_has_cipher(const char *alg_name, u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_CIPHER;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return crypto_has_alg(alg_name, type, mask);
}

static inline struct cipher_tfm *crypto_cipher_crt(struct crypto_cipher *tfm)
{
	return &crypto_cipher_tfm(tfm)->crt_cipher;
}

/**
 * crypto_cipher_blocksize() - obtain block size for cipher
 * @tfm: cipher handle
 *
 * The block size for the single block cipher referenced with the cipher handle
 * tfm is returned. The caller may use that information to allocate appropriate
 * memory for the data returned by the encryption or decryption operation
 *
 * Return: block size of cipher
 */
static inline unsigned int crypto_cipher_blocksize(struct crypto_cipher *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_cipher_tfm(tfm));
}

static inline unsigned int crypto_cipher_alignmask(struct crypto_cipher *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_cipher_tfm(tfm));
}

static inline u32 crypto_cipher_get_flags(struct crypto_cipher *tfm)
{
	return crypto_tfm_get_flags(crypto_cipher_tfm(tfm));
}

static inline void crypto_cipher_set_flags(struct crypto_cipher *tfm,
					   u32 flags)
{
	crypto_tfm_set_flags(crypto_cipher_tfm(tfm), flags);
}

static inline void crypto_cipher_clear_flags(struct crypto_cipher *tfm,
					     u32 flags)
{
	crypto_tfm_clear_flags(crypto_cipher_tfm(tfm), flags);
}

/**
 * crypto_cipher_setkey() - set key for cipher
 * @tfm: cipher handle
 * @key: buffer holding the key
 * @keylen: length of the key in bytes
 *
 * The caller provided key is set for the single block cipher referenced by the
 * cipher handle.
 *
 * Note, the key length determines the cipher type. Many block ciphers implement
 * different cipher modes depending on the key size, such as AES-128 vs AES-192
 * vs. AES-256. When providing a 16 byte key for an AES cipher handle, AES-128
 * is performed.
 *
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
static inline int crypto_cipher_setkey(struct crypto_cipher *tfm,
                                       const u8 *key, unsigned int keylen)
{
	return crypto_cipher_crt(tfm)->cit_setkey(crypto_cipher_tfm(tfm),
						  key, keylen);
}

/**
 * crypto_cipher_encrypt_one() - encrypt one block of plaintext
 * @tfm: cipher handle
 * @dst: points to the buffer that will be filled with the ciphertext
 * @src: buffer holding the plaintext to be encrypted
 *
 * Invoke the encryption operation of one block. The caller must ensure that
 * the plaintext and ciphertext buffers are at least one block in size.
 */
static inline void crypto_cipher_encrypt_one(struct crypto_cipher *tfm,
					     u8 *dst, const u8 *src)
{
	crypto_cipher_crt(tfm)->cit_encrypt_one(crypto_cipher_tfm(tfm),
						dst, src);
}

/**
 * crypto_cipher_decrypt_one() - decrypt one block of ciphertext
 * @tfm: cipher handle
 * @dst: points to the buffer that will be filled with the plaintext
 * @src: buffer holding the ciphertext to be decrypted
 *
 * Invoke the decryption operation of one block. The caller must ensure that
 * the plaintext and ciphertext buffers are at least one block in size.
 */
static inline void crypto_cipher_decrypt_one(struct crypto_cipher *tfm,
					     u8 *dst, const u8 *src)
{
	crypto_cipher_crt(tfm)->cit_decrypt_one(crypto_cipher_tfm(tfm),
						dst, src);
}

/**
 * DOC: Synchronous Message Digest API
 *
 * The synchronous message digest API is used with the ciphers of type
 * CRYPTO_ALG_TYPE_HASH (listed as type "hash" in /proc/crypto)
 */

static inline struct crypto_hash *__crypto_hash_cast(struct crypto_tfm *tfm)
{
	return (struct crypto_hash *)tfm;
}

static inline struct crypto_hash *crypto_hash_cast(struct crypto_tfm *tfm)
{
	BUG_ON((crypto_tfm_alg_type(tfm) ^ CRYPTO_ALG_TYPE_HASH) &
	       CRYPTO_ALG_TYPE_HASH_MASK);
	return __crypto_hash_cast(tfm);
}

/**
 * crypto_alloc_hash() - allocate synchronous message digest handle
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      message digest cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Allocate a cipher handle for a message digest. The returned struct
 * crypto_hash is the cipher handle that is required for any subsequent
 * API invocation for that message digest.
 *
 * Return: allocated cipher handle in case of success; IS_ERR() is true in case
 * of an error, PTR_ERR() returns the error code.
 */
static inline struct crypto_hash *crypto_alloc_hash(const char *alg_name,
						    u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	mask &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_HASH;
	mask |= CRYPTO_ALG_TYPE_HASH_MASK;

	return __crypto_hash_cast(crypto_alloc_base(alg_name, type, mask));
}

static inline struct crypto_tfm *crypto_hash_tfm(struct crypto_hash *tfm)
{
	return &tfm->base;
}

/**
 * crypto_free_hash() - zeroize and free message digest handle
 * @tfm: cipher handle to be freed
 */
static inline void crypto_free_hash(struct crypto_hash *tfm)
{
	crypto_free_tfm(crypto_hash_tfm(tfm));
}

/**
 * crypto_has_hash() - Search for the availability of a message digest
 * @alg_name: is the cra_name / name or cra_driver_name / driver name of the
 *	      message digest cipher
 * @type: specifies the type of the cipher
 * @mask: specifies the mask for the cipher
 *
 * Return: true when the message digest cipher is known to the kernel crypto
 *	   API; false otherwise
 */
static inline int crypto_has_hash(const char *alg_name, u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	mask &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_HASH;
	mask |= CRYPTO_ALG_TYPE_HASH_MASK;

	return crypto_has_alg(alg_name, type, mask);
}

static inline struct hash_tfm *crypto_hash_crt(struct crypto_hash *tfm)
{
	return &crypto_hash_tfm(tfm)->crt_hash;
}

/**
 * crypto_hash_blocksize() - obtain block size for message digest
 * @tfm: cipher handle
 *
 * The block size for the message digest cipher referenced with the cipher
 * handle is returned.
 *
 * Return: block size of cipher
 */
static inline unsigned int crypto_hash_blocksize(struct crypto_hash *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_hash_tfm(tfm));
}

static inline unsigned int crypto_hash_alignmask(struct crypto_hash *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_hash_tfm(tfm));
}

/**
 * crypto_hash_digestsize() - obtain message digest size
 * @tfm: cipher handle
 *
 * The size for the message digest created by the message digest cipher
 * referenced with the cipher handle is returned.
 *
 * Return: message digest size
 */
static inline unsigned int crypto_hash_digestsize(struct crypto_hash *tfm)
{
	return crypto_hash_crt(tfm)->digestsize;
}

static inline u32 crypto_hash_get_flags(struct crypto_hash *tfm)
{
	return crypto_tfm_get_flags(crypto_hash_tfm(tfm));
}

static inline void crypto_hash_set_flags(struct crypto_hash *tfm, u32 flags)
{
	crypto_tfm_set_flags(crypto_hash_tfm(tfm), flags);
}

static inline void crypto_hash_clear_flags(struct crypto_hash *tfm, u32 flags)
{
	crypto_tfm_clear_flags(crypto_hash_tfm(tfm), flags);
}

/**
 * crypto_hash_init() - (re)initialize message digest handle
 * @desc: cipher request handle that to be filled by caller --
 *	  desc.tfm is filled with the hash cipher handle;
 *	  desc.flags is filled with either CRYPTO_TFM_REQ_MAY_SLEEP or 0.
 *
 * The call (re-)initializes the message digest referenced by the hash cipher
 * request handle. Any potentially existing state created by previous
 * operations is discarded.
 *
 * Return: 0 if the message digest initialization was successful; < 0 if an
 *	   error occurred
 */
static inline int crypto_hash_init(struct hash_desc *desc)
{
	return crypto_hash_crt(desc->tfm)->init(desc);
}

/**
 * crypto_hash_update() - add data to message digest for processing
 * @desc: cipher request handle
 * @sg: scatter / gather list pointing to the data to be added to the message
 *      digest
 * @nbytes: number of bytes to be processed from @sg
 *
 * Updates the message digest state of the cipher handle pointed to by the
 * hash cipher request handle with the input data pointed to by the
 * scatter/gather list.
 *
 * Return: 0 if the message digest update was successful; < 0 if an error
 *	   occurred
 */
static inline int crypto_hash_update(struct hash_desc *desc,
				     struct scatterlist *sg,
				     unsigned int nbytes)
{
	return crypto_hash_crt(desc->tfm)->update(desc, sg, nbytes);
}

/**
 * crypto_hash_final() - calculate message digest
 * @desc: cipher request handle
 * @out: message digest output buffer -- The caller must ensure that the out
 *	 buffer has a sufficient size (e.g. by using the crypto_hash_digestsize
 *	 function).
 *
 * Finalize the message digest operation and create the message digest
 * based on all data added to the cipher handle. The message digest is placed
 * into the output buffer.
 *
 * Return: 0 if the message digest creation was successful; < 0 if an error
 *	   occurred
 */
static inline int crypto_hash_final(struct hash_desc *desc, u8 *out)
{
	return crypto_hash_crt(desc->tfm)->final(desc, out);
}

/**
 * crypto_hash_digest() - calculate message digest for a buffer
 * @desc: see crypto_hash_final()
 * @sg: see crypto_hash_update()
 * @nbytes:  see crypto_hash_update()
 * @out: see crypto_hash_final()
 *
 * This function is a "short-hand" for the function calls of crypto_hash_init,
 * crypto_hash_update and crypto_hash_final. The parameters have the same
 * meaning as discussed for those separate three functions.
 *
 * Return: 0 if the message digest creation was successful; < 0 if an error
 *	   occurred
 */
static inline int crypto_hash_digest(struct hash_desc *desc,
				     struct scatterlist *sg,
				     unsigned int nbytes, u8 *out)
{
	return crypto_hash_crt(desc->tfm)->digest(desc, sg, nbytes, out);
}

/**
 * crypto_hash_setkey() - set key for message digest
 * @hash: cipher handle
 * @key: buffer holding the key
 * @keylen: length of the key in bytes
 *
 * The caller provided key is set for the message digest cipher. The cipher
 * handle must point to a keyed hash in order for this function to succeed.
 *
 * Return: 0 if the setting of the key was successful; < 0 if an error occurred
 */
static inline int crypto_hash_setkey(struct crypto_hash *hash,
				     const u8 *key, unsigned int keylen)
{
	return crypto_hash_crt(hash)->setkey(hash, key, keylen);
}

static inline struct crypto_comp *__crypto_comp_cast(struct crypto_tfm *tfm)
{
	return (struct crypto_comp *)tfm;
}

static inline struct crypto_comp *crypto_comp_cast(struct crypto_tfm *tfm)
{
	BUG_ON((crypto_tfm_alg_type(tfm) ^ CRYPTO_ALG_TYPE_COMPRESS) &
	       CRYPTO_ALG_TYPE_MASK);
	return __crypto_comp_cast(tfm);
}

static inline struct crypto_comp *crypto_alloc_comp(const char *alg_name,
						    u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_COMPRESS;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return __crypto_comp_cast(crypto_alloc_base(alg_name, type, mask));
}

static inline struct crypto_tfm *crypto_comp_tfm(struct crypto_comp *tfm)
{
	return &tfm->base;
}

static inline void crypto_free_comp(struct crypto_comp *tfm)
{
	crypto_free_tfm(crypto_comp_tfm(tfm));
}

static inline int crypto_has_comp(const char *alg_name, u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_COMPRESS;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return crypto_has_alg(alg_name, type, mask);
}

static inline const char *crypto_comp_name(struct crypto_comp *tfm)
{
	return crypto_tfm_alg_name(crypto_comp_tfm(tfm));
}

static inline struct compress_tfm *crypto_comp_crt(struct crypto_comp *tfm)
{
	return &crypto_comp_tfm(tfm)->crt_compress;
}

static inline int crypto_comp_compress(struct crypto_comp *tfm,
                                       const u8 *src, unsigned int slen,
                                       u8 *dst, unsigned int *dlen)
{
	return crypto_comp_crt(tfm)->cot_compress(crypto_comp_tfm(tfm),
						  src, slen, dst, dlen);
}

static inline int crypto_comp_decompress(struct crypto_comp *tfm,
                                         const u8 *src, unsigned int slen,
                                         u8 *dst, unsigned int *dlen)
{
	return crypto_comp_crt(tfm)->cot_decompress(crypto_comp_tfm(tfm),
						    src, slen, dst, dlen);
}

#endif	/* _LINUX_CRYPTO_H */

