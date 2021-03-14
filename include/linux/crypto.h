/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Scatterlist Cryptographic API.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 David S. Miller (davem@redhat.com)
 * Copyright (c) 2005 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * Portions derived from Cryptoapi, by Alexander Kjeldaas <astor@fast.no>
 * and Nettle, by Niels Möller.
 */
#ifndef _LINUX_CRYPTO_H
#define _LINUX_CRYPTO_H

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/completion.h>

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
#define CRYPTO_ALG_TYPE_SKCIPHER	0x00000005
#define CRYPTO_ALG_TYPE_KPP		0x00000008
#define CRYPTO_ALG_TYPE_ACOMPRESS	0x0000000a
#define CRYPTO_ALG_TYPE_SCOMPRESS	0x0000000b
#define CRYPTO_ALG_TYPE_RNG		0x0000000c
#define CRYPTO_ALG_TYPE_AKCIPHER	0x0000000d
#define CRYPTO_ALG_TYPE_HASH		0x0000000e
#define CRYPTO_ALG_TYPE_SHASH		0x0000000e
#define CRYPTO_ALG_TYPE_AHASH		0x0000000f

#define CRYPTO_ALG_TYPE_HASH_MASK	0x0000000e
#define CRYPTO_ALG_TYPE_AHASH_MASK	0x0000000e
#define CRYPTO_ALG_TYPE_ACOMPRESS_MASK	0x0000000e

#define CRYPTO_ALG_LARVAL		0x00000010
#define CRYPTO_ALG_DEAD			0x00000020
#define CRYPTO_ALG_DYING		0x00000040
#define CRYPTO_ALG_ASYNC		0x00000080

/*
 * Set if the algorithm (or an algorithm which it uses) requires another
 * algorithm of the same type to handle corner cases.
 */
#define CRYPTO_ALG_NEED_FALLBACK	0x00000100

/*
 * Set if the algorithm has passed automated run-time testing.  Note that
 * if there is no run-time testing for a given algorithm it is considered
 * to have passed.
 */

#define CRYPTO_ALG_TESTED		0x00000400

/*
 * Set if the algorithm is an instance that is built from templates.
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
 * Set if the algorithm has a ->setkey() method but can be used without
 * calling it first, i.e. there is a default key.
 */
#define CRYPTO_ALG_OPTIONAL_KEY		0x00004000

/*
 * Don't trigger module loading
 */
#define CRYPTO_NOLOAD			0x00008000

/*
 * The algorithm may allocate memory during request processing, i.e. during
 * encryption, decryption, or hashing.  Users can request an algorithm with this
 * flag unset if they can't handle memory allocation failures.
 *
 * This flag is currently only implemented for algorithms of type "skcipher",
 * "aead", "ahash", "shash", and "cipher".  Algorithms of other types might not
 * have this flag set even if they allocate memory.
 *
 * In some edge cases, algorithms can allocate memory regardless of this flag.
 * To avoid these cases, users must obey the following usage constraints:
 *    skcipher:
 *	- The IV buffer and all scatterlist elements must be aligned to the
 *	  algorithm's alignmask.
 *	- If the data were to be divided into chunks of size
 *	  crypto_skcipher_walksize() (with any remainder going at the end), no
 *	  chunk can cross a page boundary or a scatterlist element boundary.
 *    aead:
 *	- The IV buffer and all scatterlist elements must be aligned to the
 *	  algorithm's alignmask.
 *	- The first scatterlist element must contain all the associated data,
 *	  and its pages must be !PageHighMem.
 *	- If the plaintext/ciphertext were to be divided into chunks of size
 *	  crypto_aead_walksize() (with the remainder going at the end), no chunk
 *	  can cross a page boundary or a scatterlist element boundary.
 *    ahash:
 *	- The result buffer must be aligned to the algorithm's alignmask.
 *	- crypto_ahash_finup() must not be used unless the algorithm implements
 *	  ->finup() natively.
 */
#define CRYPTO_ALG_ALLOCATES_MEMORY	0x00010000

/*
 * Transform masks and values (for crt_flags).
 */
#define CRYPTO_TFM_NEED_KEY		0x00000001

#define CRYPTO_TFM_REQ_MASK		0x000fff00
#define CRYPTO_TFM_REQ_FORBID_WEAK_KEYS	0x00000100
#define CRYPTO_TFM_REQ_MAY_SLEEP	0x00000200
#define CRYPTO_TFM_REQ_MAY_BACKLOG	0x00000400

/*
 * Miscellaneous stuff.
 */
#define CRYPTO_MAX_ALG_NAME		128

/*
 * The macro CRYPTO_MINALIGN_ATTR (along with the void * type in the actual
 * declaration) is used to ensure that the crypto_tfm context structure is
 * aligned correctly for the given architecture so that there are no alignment
 * faults for C data types.  On architectures that support non-cache coherent
 * DMA, such as ARM or arm64, it also takes into account the minimal alignment
 * that is required to ensure that the context struct member does not share any
 * cachelines with the rest of the struct. This is needed to ensure that cache
 * maintenance for non-coherent DMA (cache invalidation in particular) does not
 * affect data that may be accessed by the CPU concurrently.
 */
#define CRYPTO_MINALIGN ARCH_KMALLOC_MINALIGN

#define CRYPTO_MINALIGN_ATTR __attribute__ ((__aligned__(CRYPTO_MINALIGN)))

struct scatterlist;
struct crypto_async_request;
struct crypto_tfm;
struct crypto_type;

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

/**
 * DOC: Block Cipher Algorithm Definitions
 *
 * These data structures define modular crypto algorithm implementations,
 * managed via crypto_register_alg() and crypto_unregister_alg().
 */

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

/**
 * struct compress_alg - compression/decompression algorithm
 * @coa_compress: Compress a buffer of specified length, storing the resulting
 *		  data in the specified buffer. Return the length of the
 *		  compressed data in dlen.
 * @coa_decompress: Decompress the source buffer, storing the uncompressed
 *		    data in the specified buffer. The length of the data is
 *		    returned in dlen.
 *
 * All fields are mandatory.
 */
struct compress_alg {
	int (*coa_compress)(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen);
	int (*coa_decompress)(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen);
};

#ifdef CONFIG_CRYPTO_STATS
/*
 * struct crypto_istat_aead - statistics for AEAD algorithm
 * @encrypt_cnt:	number of encrypt requests
 * @encrypt_tlen:	total data size handled by encrypt requests
 * @decrypt_cnt:	number of decrypt requests
 * @decrypt_tlen:	total data size handled by decrypt requests
 * @err_cnt:		number of error for AEAD requests
 */
struct crypto_istat_aead {
	atomic64_t encrypt_cnt;
	atomic64_t encrypt_tlen;
	atomic64_t decrypt_cnt;
	atomic64_t decrypt_tlen;
	atomic64_t err_cnt;
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

/*
 * struct crypto_istat_cipher - statistics for cipher algorithm
 * @encrypt_cnt:	number of encrypt requests
 * @encrypt_tlen:	total data size handled by encrypt requests
 * @decrypt_cnt:	number of decrypt requests
 * @decrypt_tlen:	total data size handled by decrypt requests
 * @err_cnt:		number of error for cipher requests
 */
struct crypto_istat_cipher {
	atomic64_t encrypt_cnt;
	atomic64_t encrypt_tlen;
	atomic64_t decrypt_cnt;
	atomic64_t decrypt_tlen;
	atomic64_t err_cnt;
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

/*
 * struct crypto_istat_hash - statistics for has algorithm
 * @hash_cnt:		number of hash requests
 * @hash_tlen:		total data size hashed
 * @err_cnt:		number of error for hash requests
 */
struct crypto_istat_hash {
	atomic64_t hash_cnt;
	atomic64_t hash_tlen;
	atomic64_t err_cnt;
};

/*
 * struct crypto_istat_kpp - statistics for KPP algorithm
 * @setsecret_cnt:		number of setsecrey operation
 * @generate_public_key_cnt:	number of generate_public_key operation
 * @compute_shared_secret_cnt:	number of compute_shared_secret operation
 * @err_cnt:			number of error for KPP requests
 */
struct crypto_istat_kpp {
	atomic64_t setsecret_cnt;
	atomic64_t generate_public_key_cnt;
	atomic64_t compute_shared_secret_cnt;
	atomic64_t err_cnt;
};

/*
 * struct crypto_istat_rng: statistics for RNG algorithm
 * @generate_cnt:	number of RNG generate requests
 * @generate_tlen:	total data size of generated data by the RNG
 * @seed_cnt:		number of times the RNG was seeded
 * @err_cnt:		number of error for RNG requests
 */
struct crypto_istat_rng {
	atomic64_t generate_cnt;
	atomic64_t generate_tlen;
	atomic64_t seed_cnt;
	atomic64_t err_cnt;
};
#endif /* CONFIG_CRYPTO_STATS */

#define cra_cipher	cra_u.cipher
#define cra_compress	cra_u.compress

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
 *	      transformation types. There are multiple options, such as
 *	      &crypto_skcipher_type, &crypto_ahash_type, &crypto_rng_type.
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
 * @cra_u.cipher: Union member which contains a single-block symmetric cipher
 *		  definition. See @struct @cipher_alg.
 * @cra_u.compress: Union member which contains a (de)compression algorithm.
 *		    See @struct @compress_alg.
 * @cra_module: Owner of this transformation implementation. Set to THIS_MODULE
 * @cra_list: internally used
 * @cra_users: internally used
 * @cra_refcnt: internally used
 * @cra_destroy: internally used
 *
 * @stats: union of all possible crypto_istat_xxx structures
 * @stats.aead:		statistics for AEAD algorithm
 * @stats.akcipher:	statistics for akcipher algorithm
 * @stats.cipher:	statistics for cipher algorithm
 * @stats.compress:	statistics for compress algorithm
 * @stats.hash:		statistics for hash algorithm
 * @stats.rng:		statistics for rng algorithm
 * @stats.kpp:		statistics for KPP algorithm
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
	refcount_t cra_refcnt;

	char cra_name[CRYPTO_MAX_ALG_NAME];
	char cra_driver_name[CRYPTO_MAX_ALG_NAME];

	const struct crypto_type *cra_type;

	union {
		struct cipher_alg cipher;
		struct compress_alg compress;
	} cra_u;

	int (*cra_init)(struct crypto_tfm *tfm);
	void (*cra_exit)(struct crypto_tfm *tfm);
	void (*cra_destroy)(struct crypto_alg *alg);
	
	struct module *cra_module;

#ifdef CONFIG_CRYPTO_STATS
	union {
		struct crypto_istat_aead aead;
		struct crypto_istat_akcipher akcipher;
		struct crypto_istat_cipher cipher;
		struct crypto_istat_compress compress;
		struct crypto_istat_hash hash;
		struct crypto_istat_rng rng;
		struct crypto_istat_kpp kpp;
	} stats;
#endif /* CONFIG_CRYPTO_STATS */

} CRYPTO_MINALIGN_ATTR;

#ifdef CONFIG_CRYPTO_STATS
void crypto_stats_init(struct crypto_alg *alg);
void crypto_stats_get(struct crypto_alg *alg);
void crypto_stats_aead_encrypt(unsigned int cryptlen, struct crypto_alg *alg, int ret);
void crypto_stats_aead_decrypt(unsigned int cryptlen, struct crypto_alg *alg, int ret);
void crypto_stats_ahash_update(unsigned int nbytes, int ret, struct crypto_alg *alg);
void crypto_stats_ahash_final(unsigned int nbytes, int ret, struct crypto_alg *alg);
void crypto_stats_akcipher_encrypt(unsigned int src_len, int ret, struct crypto_alg *alg);
void crypto_stats_akcipher_decrypt(unsigned int src_len, int ret, struct crypto_alg *alg);
void crypto_stats_akcipher_sign(int ret, struct crypto_alg *alg);
void crypto_stats_akcipher_verify(int ret, struct crypto_alg *alg);
void crypto_stats_compress(unsigned int slen, int ret, struct crypto_alg *alg);
void crypto_stats_decompress(unsigned int slen, int ret, struct crypto_alg *alg);
void crypto_stats_kpp_set_secret(struct crypto_alg *alg, int ret);
void crypto_stats_kpp_generate_public_key(struct crypto_alg *alg, int ret);
void crypto_stats_kpp_compute_shared_secret(struct crypto_alg *alg, int ret);
void crypto_stats_rng_seed(struct crypto_alg *alg, int ret);
void crypto_stats_rng_generate(struct crypto_alg *alg, unsigned int dlen, int ret);
void crypto_stats_skcipher_encrypt(unsigned int cryptlen, int ret, struct crypto_alg *alg);
void crypto_stats_skcipher_decrypt(unsigned int cryptlen, int ret, struct crypto_alg *alg);
#else
static inline void crypto_stats_init(struct crypto_alg *alg)
{}
static inline void crypto_stats_get(struct crypto_alg *alg)
{}
static inline void crypto_stats_aead_encrypt(unsigned int cryptlen, struct crypto_alg *alg, int ret)
{}
static inline void crypto_stats_aead_decrypt(unsigned int cryptlen, struct crypto_alg *alg, int ret)
{}
static inline void crypto_stats_ahash_update(unsigned int nbytes, int ret, struct crypto_alg *alg)
{}
static inline void crypto_stats_ahash_final(unsigned int nbytes, int ret, struct crypto_alg *alg)
{}
static inline void crypto_stats_akcipher_encrypt(unsigned int src_len, int ret, struct crypto_alg *alg)
{}
static inline void crypto_stats_akcipher_decrypt(unsigned int src_len, int ret, struct crypto_alg *alg)
{}
static inline void crypto_stats_akcipher_sign(int ret, struct crypto_alg *alg)
{}
static inline void crypto_stats_akcipher_verify(int ret, struct crypto_alg *alg)
{}
static inline void crypto_stats_compress(unsigned int slen, int ret, struct crypto_alg *alg)
{}
static inline void crypto_stats_decompress(unsigned int slen, int ret, struct crypto_alg *alg)
{}
static inline void crypto_stats_kpp_set_secret(struct crypto_alg *alg, int ret)
{}
static inline void crypto_stats_kpp_generate_public_key(struct crypto_alg *alg, int ret)
{}
static inline void crypto_stats_kpp_compute_shared_secret(struct crypto_alg *alg, int ret)
{}
static inline void crypto_stats_rng_seed(struct crypto_alg *alg, int ret)
{}
static inline void crypto_stats_rng_generate(struct crypto_alg *alg, unsigned int dlen, int ret)
{}
static inline void crypto_stats_skcipher_encrypt(unsigned int cryptlen, int ret, struct crypto_alg *alg)
{}
static inline void crypto_stats_skcipher_decrypt(unsigned int cryptlen, int ret, struct crypto_alg *alg)
{}
#endif
/*
 * A helper struct for waiting for completion of async crypto ops
 */
struct crypto_wait {
	struct completion completion;
	int err;
};

/*
 * Macro for declaring a crypto op async wait object on stack
 */
#define DECLARE_CRYPTO_WAIT(_wait) \
	struct crypto_wait _wait = { \
		COMPLETION_INITIALIZER_ONSTACK((_wait).completion), 0 }

/*
 * Async ops completion helper functioons
 */
void crypto_req_done(struct crypto_async_request *req, int err);

static inline int crypto_wait_req(int err, struct crypto_wait *wait)
{
	switch (err) {
	case -EINPROGRESS:
	case -EBUSY:
		wait_for_completion(&wait->completion);
		reinit_completion(&wait->completion);
		err = wait->err;
		break;
	}

	return err;
}

static inline void crypto_init_wait(struct crypto_wait *wait)
{
	init_completion(&wait->completion);
}

/*
 * Algorithm registration interface.
 */
int crypto_register_alg(struct crypto_alg *alg);
void crypto_unregister_alg(struct crypto_alg *alg);
int crypto_register_algs(struct crypto_alg *algs, int count);
void crypto_unregister_algs(struct crypto_alg *algs, int count);

/*
 * Algorithm query interface.
 */
int crypto_has_alg(const char *name, u32 type, u32 mask);

/*
 * Transforms: user-instantiated objects which encapsulate algorithms
 * and core processing logic.  Managed via crypto_alloc_*() and
 * crypto_free_*(), as well as the various helpers below.
 */

struct crypto_tfm {

	u32 crt_flags;

	int node;
	
	void (*exit)(struct crypto_tfm *tfm);
	
	struct crypto_alg *__crt_alg;

	void *__crt_ctx[] CRYPTO_MINALIGN_ATTR;
};

struct crypto_comp {
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

static inline struct crypto_comp *__crypto_comp_cast(struct crypto_tfm *tfm)
{
	return (struct crypto_comp *)tfm;
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

int crypto_comp_compress(struct crypto_comp *tfm,
			 const u8 *src, unsigned int slen,
			 u8 *dst, unsigned int *dlen);

int crypto_comp_decompress(struct crypto_comp *tfm,
			   const u8 *src, unsigned int slen,
			   u8 *dst, unsigned int *dlen);

#endif	/* _LINUX_CRYPTO_H */

