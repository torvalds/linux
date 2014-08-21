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

/*
 * Algorithms: modular crypto algorithm implementations, managed
 * via crypto_register_alg() and crypto_unregister_alg().
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

struct crypto_ablkcipher *crypto_alloc_ablkcipher(const char *alg_name,
						  u32 type, u32 mask);

static inline struct crypto_tfm *crypto_ablkcipher_tfm(
	struct crypto_ablkcipher *tfm)
{
	return &tfm->base;
}

static inline void crypto_free_ablkcipher(struct crypto_ablkcipher *tfm)
{
	crypto_free_tfm(crypto_ablkcipher_tfm(tfm));
}

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

static inline unsigned int crypto_ablkcipher_ivsize(
	struct crypto_ablkcipher *tfm)
{
	return crypto_ablkcipher_crt(tfm)->ivsize;
}

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

static inline int crypto_ablkcipher_setkey(struct crypto_ablkcipher *tfm,
					   const u8 *key, unsigned int keylen)
{
	struct ablkcipher_tfm *crt = crypto_ablkcipher_crt(tfm);

	return crt->setkey(crt->base, key, keylen);
}

static inline struct crypto_ablkcipher *crypto_ablkcipher_reqtfm(
	struct ablkcipher_request *req)
{
	return __crypto_ablkcipher_cast(req->base.tfm);
}

static inline int crypto_ablkcipher_encrypt(struct ablkcipher_request *req)
{
	struct ablkcipher_tfm *crt =
		crypto_ablkcipher_crt(crypto_ablkcipher_reqtfm(req));
	return crt->encrypt(req);
}

static inline int crypto_ablkcipher_decrypt(struct ablkcipher_request *req)
{
	struct ablkcipher_tfm *crt =
		crypto_ablkcipher_crt(crypto_ablkcipher_reqtfm(req));
	return crt->decrypt(req);
}

static inline unsigned int crypto_ablkcipher_reqsize(
	struct crypto_ablkcipher *tfm)
{
	return crypto_ablkcipher_crt(tfm)->reqsize;
}

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

static inline void ablkcipher_request_free(struct ablkcipher_request *req)
{
	kzfree(req);
}

static inline void ablkcipher_request_set_callback(
	struct ablkcipher_request *req,
	u32 flags, crypto_completion_t compl, void *data)
{
	req->base.complete = compl;
	req->base.data = data;
	req->base.flags = flags;
}

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

static inline struct crypto_aead *__crypto_aead_cast(struct crypto_tfm *tfm)
{
	return (struct crypto_aead *)tfm;
}

struct crypto_aead *crypto_alloc_aead(const char *alg_name, u32 type, u32 mask);

static inline struct crypto_tfm *crypto_aead_tfm(struct crypto_aead *tfm)
{
	return &tfm->base;
}

static inline void crypto_free_aead(struct crypto_aead *tfm)
{
	crypto_free_tfm(crypto_aead_tfm(tfm));
}

static inline struct aead_tfm *crypto_aead_crt(struct crypto_aead *tfm)
{
	return &crypto_aead_tfm(tfm)->crt_aead;
}

static inline unsigned int crypto_aead_ivsize(struct crypto_aead *tfm)
{
	return crypto_aead_crt(tfm)->ivsize;
}

static inline unsigned int crypto_aead_authsize(struct crypto_aead *tfm)
{
	return crypto_aead_crt(tfm)->authsize;
}

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

static inline int crypto_aead_setkey(struct crypto_aead *tfm, const u8 *key,
				     unsigned int keylen)
{
	struct aead_tfm *crt = crypto_aead_crt(tfm);

	return crt->setkey(crt->base, key, keylen);
}

int crypto_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize);

static inline struct crypto_aead *crypto_aead_reqtfm(struct aead_request *req)
{
	return __crypto_aead_cast(req->base.tfm);
}

static inline int crypto_aead_encrypt(struct aead_request *req)
{
	return crypto_aead_crt(crypto_aead_reqtfm(req))->encrypt(req);
}

static inline int crypto_aead_decrypt(struct aead_request *req)
{
	return crypto_aead_crt(crypto_aead_reqtfm(req))->decrypt(req);
}

static inline unsigned int crypto_aead_reqsize(struct crypto_aead *tfm)
{
	return crypto_aead_crt(tfm)->reqsize;
}

static inline void aead_request_set_tfm(struct aead_request *req,
					struct crypto_aead *tfm)
{
	req->base.tfm = crypto_aead_tfm(crypto_aead_crt(tfm)->base);
}

static inline struct aead_request *aead_request_alloc(struct crypto_aead *tfm,
						      gfp_t gfp)
{
	struct aead_request *req;

	req = kmalloc(sizeof(*req) + crypto_aead_reqsize(tfm), gfp);

	if (likely(req))
		aead_request_set_tfm(req, tfm);

	return req;
}

static inline void aead_request_free(struct aead_request *req)
{
	kzfree(req);
}

static inline void aead_request_set_callback(struct aead_request *req,
					     u32 flags,
					     crypto_completion_t compl,
					     void *data)
{
	req->base.complete = compl;
	req->base.data = data;
	req->base.flags = flags;
}

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

static inline void aead_request_set_assoc(struct aead_request *req,
					  struct scatterlist *assoc,
					  unsigned int assoclen)
{
	req->assoc = assoc;
	req->assoclen = assoclen;
}

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

static inline void crypto_free_blkcipher(struct crypto_blkcipher *tfm)
{
	crypto_free_tfm(crypto_blkcipher_tfm(tfm));
}

static inline int crypto_has_blkcipher(const char *alg_name, u32 type, u32 mask)
{
	type &= ~CRYPTO_ALG_TYPE_MASK;
	type |= CRYPTO_ALG_TYPE_BLKCIPHER;
	mask |= CRYPTO_ALG_TYPE_MASK;

	return crypto_has_alg(alg_name, type, mask);
}

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

static inline unsigned int crypto_blkcipher_ivsize(struct crypto_blkcipher *tfm)
{
	return crypto_blkcipher_alg(tfm)->ivsize;
}

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

static inline int crypto_blkcipher_setkey(struct crypto_blkcipher *tfm,
					  const u8 *key, unsigned int keylen)
{
	return crypto_blkcipher_crt(tfm)->setkey(crypto_blkcipher_tfm(tfm),
						 key, keylen);
}

static inline int crypto_blkcipher_encrypt(struct blkcipher_desc *desc,
					   struct scatterlist *dst,
					   struct scatterlist *src,
					   unsigned int nbytes)
{
	desc->info = crypto_blkcipher_crt(desc->tfm)->iv;
	return crypto_blkcipher_crt(desc->tfm)->encrypt(desc, dst, src, nbytes);
}

static inline int crypto_blkcipher_encrypt_iv(struct blkcipher_desc *desc,
					      struct scatterlist *dst,
					      struct scatterlist *src,
					      unsigned int nbytes)
{
	return crypto_blkcipher_crt(desc->tfm)->encrypt(desc, dst, src, nbytes);
}

static inline int crypto_blkcipher_decrypt(struct blkcipher_desc *desc,
					   struct scatterlist *dst,
					   struct scatterlist *src,
					   unsigned int nbytes)
{
	desc->info = crypto_blkcipher_crt(desc->tfm)->iv;
	return crypto_blkcipher_crt(desc->tfm)->decrypt(desc, dst, src, nbytes);
}

static inline int crypto_blkcipher_decrypt_iv(struct blkcipher_desc *desc,
					      struct scatterlist *dst,
					      struct scatterlist *src,
					      unsigned int nbytes)
{
	return crypto_blkcipher_crt(desc->tfm)->decrypt(desc, dst, src, nbytes);
}

static inline void crypto_blkcipher_set_iv(struct crypto_blkcipher *tfm,
					   const u8 *src, unsigned int len)
{
	memcpy(crypto_blkcipher_crt(tfm)->iv, src, len);
}

static inline void crypto_blkcipher_get_iv(struct crypto_blkcipher *tfm,
					   u8 *dst, unsigned int len)
{
	memcpy(dst, crypto_blkcipher_crt(tfm)->iv, len);
}

static inline struct crypto_cipher *__crypto_cipher_cast(struct crypto_tfm *tfm)
{
	return (struct crypto_cipher *)tfm;
}

static inline struct crypto_cipher *crypto_cipher_cast(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return __crypto_cipher_cast(tfm);
}

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

static inline void crypto_free_cipher(struct crypto_cipher *tfm)
{
	crypto_free_tfm(crypto_cipher_tfm(tfm));
}

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

static inline int crypto_cipher_setkey(struct crypto_cipher *tfm,
                                       const u8 *key, unsigned int keylen)
{
	return crypto_cipher_crt(tfm)->cit_setkey(crypto_cipher_tfm(tfm),
						  key, keylen);
}

static inline void crypto_cipher_encrypt_one(struct crypto_cipher *tfm,
					     u8 *dst, const u8 *src)
{
	crypto_cipher_crt(tfm)->cit_encrypt_one(crypto_cipher_tfm(tfm),
						dst, src);
}

static inline void crypto_cipher_decrypt_one(struct crypto_cipher *tfm,
					     u8 *dst, const u8 *src)
{
	crypto_cipher_crt(tfm)->cit_decrypt_one(crypto_cipher_tfm(tfm),
						dst, src);
}

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

static inline void crypto_free_hash(struct crypto_hash *tfm)
{
	crypto_free_tfm(crypto_hash_tfm(tfm));
}

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

static inline unsigned int crypto_hash_blocksize(struct crypto_hash *tfm)
{
	return crypto_tfm_alg_blocksize(crypto_hash_tfm(tfm));
}

static inline unsigned int crypto_hash_alignmask(struct crypto_hash *tfm)
{
	return crypto_tfm_alg_alignmask(crypto_hash_tfm(tfm));
}

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

static inline int crypto_hash_init(struct hash_desc *desc)
{
	return crypto_hash_crt(desc->tfm)->init(desc);
}

static inline int crypto_hash_update(struct hash_desc *desc,
				     struct scatterlist *sg,
				     unsigned int nbytes)
{
	return crypto_hash_crt(desc->tfm)->update(desc, sg, nbytes);
}

static inline int crypto_hash_final(struct hash_desc *desc, u8 *out)
{
	return crypto_hash_crt(desc->tfm)->final(desc, out);
}

static inline int crypto_hash_digest(struct hash_desc *desc,
				     struct scatterlist *sg,
				     unsigned int nbytes, u8 *out)
{
	return crypto_hash_crt(desc->tfm)->digest(desc, sg, nbytes, out);
}

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

