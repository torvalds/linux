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

#include <asm/atomic.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

/*
 * Algorithm masks and types.
 */
#define CRYPTO_ALG_TYPE_MASK		0x0000000f
#define CRYPTO_ALG_TYPE_CIPHER		0x00000001
#define CRYPTO_ALG_TYPE_DIGEST		0x00000002
#define CRYPTO_ALG_TYPE_HASH		0x00000003
#define CRYPTO_ALG_TYPE_BLKCIPHER	0x00000004
#define CRYPTO_ALG_TYPE_COMPRESS	0x00000005

#define CRYPTO_ALG_TYPE_HASH_MASK	0x0000000e

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
 * Transform masks and values (for crt_flags).
 */
#define CRYPTO_TFM_MODE_MASK		0x000000ff
#define CRYPTO_TFM_REQ_MASK		0x000fff00
#define CRYPTO_TFM_RES_MASK		0xfff00000

#define CRYPTO_TFM_MODE_ECB		0x00000001
#define CRYPTO_TFM_MODE_CBC		0x00000002
#define CRYPTO_TFM_MODE_CFB		0x00000004
#define CRYPTO_TFM_MODE_CTR		0x00000008

#define CRYPTO_TFM_REQ_WEAK_KEY		0x00000100
#define CRYPTO_TFM_REQ_MAY_SLEEP	0x00000200
#define CRYPTO_TFM_RES_WEAK_KEY		0x00100000
#define CRYPTO_TFM_RES_BAD_KEY_LEN   	0x00200000
#define CRYPTO_TFM_RES_BAD_KEY_SCHED 	0x00400000
#define CRYPTO_TFM_RES_BAD_BLOCK_LEN 	0x00800000
#define CRYPTO_TFM_RES_BAD_FLAGS 	0x01000000

/*
 * Miscellaneous stuff.
 */
#define CRYPTO_UNSPEC			0
#define CRYPTO_MAX_ALG_NAME		64

#define CRYPTO_DIR_ENCRYPT		1
#define CRYPTO_DIR_DECRYPT		0

/*
 * The macro CRYPTO_MINALIGN_ATTR (along with the void * type in the actual
 * declaration) is used to ensure that the crypto_tfm context structure is
 * aligned correctly for the given architecture so that there are no alignment
 * faults for C data types.  In particular, this is required on platforms such
 * as arm where pointers are 32-bit aligned but there are data types such as
 * u64 which require 64-bit alignment.
 */
#if defined(ARCH_KMALLOC_MINALIGN)
#define CRYPTO_MINALIGN ARCH_KMALLOC_MINALIGN
#elif defined(ARCH_SLAB_MINALIGN)
#define CRYPTO_MINALIGN ARCH_SLAB_MINALIGN
#endif

#ifdef CRYPTO_MINALIGN
#define CRYPTO_MINALIGN_ATTR __attribute__ ((__aligned__(CRYPTO_MINALIGN)))
#else
#define CRYPTO_MINALIGN_ATTR
#endif

struct scatterlist;
struct crypto_blkcipher;
struct crypto_hash;
struct crypto_tfm;
struct crypto_type;

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
struct blkcipher_alg {
	int (*setkey)(struct crypto_tfm *tfm, const u8 *key,
	              unsigned int keylen);
	int (*encrypt)(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes);
	int (*decrypt)(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes);

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

	unsigned int (*cia_encrypt_ecb)(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes) __deprecated;
	unsigned int (*cia_decrypt_ecb)(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes) __deprecated;
	unsigned int (*cia_encrypt_cbc)(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes) __deprecated;
	unsigned int (*cia_decrypt_cbc)(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes) __deprecated;
};

struct digest_alg {
	unsigned int dia_digestsize;
	void (*dia_init)(struct crypto_tfm *tfm);
	void (*dia_update)(struct crypto_tfm *tfm, const u8 *data,
			   unsigned int len);
	void (*dia_final)(struct crypto_tfm *tfm, u8 *out);
	int (*dia_setkey)(struct crypto_tfm *tfm, const u8 *key,
	                  unsigned int keylen);
};

struct hash_alg {
	int (*init)(struct hash_desc *desc);
	int (*update)(struct hash_desc *desc, struct scatterlist *sg,
		      unsigned int nbytes);
	int (*final)(struct hash_desc *desc, u8 *out);
	int (*digest)(struct hash_desc *desc, struct scatterlist *sg,
		      unsigned int nbytes, u8 *out);
	int (*setkey)(struct crypto_hash *tfm, const u8 *key,
		      unsigned int keylen);

	unsigned int digestsize;
};

struct compress_alg {
	int (*coa_compress)(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen);
	int (*coa_decompress)(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen);
};

#define cra_blkcipher	cra_u.blkcipher
#define cra_cipher	cra_u.cipher
#define cra_digest	cra_u.digest
#define cra_hash	cra_u.hash
#define cra_compress	cra_u.compress

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
		struct blkcipher_alg blkcipher;
		struct cipher_alg cipher;
		struct digest_alg digest;
		struct hash_alg hash;
		struct compress_alg compress;
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

/*
 * Algorithm query interface.
 */
#ifdef CONFIG_CRYPTO
int crypto_has_alg(const char *name, u32 type, u32 mask);
#else
static inline int crypto_alg_available(const char *name, u32 flags)
{
	return 0;
}

static inline int crypto_has_alg(const char *name, u32 type, u32 mask)
{
	return 0;
}
#endif

/*
 * Transforms: user-instantiated objects which encapsulate algorithms
 * and core processing logic.  Managed via crypto_alloc_*() and
 * crypto_free_*(), as well as the various helpers below.
 */

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
	void *cit_iv;
	unsigned int cit_ivsize;
	u32 cit_mode;
	int (*cit_setkey)(struct crypto_tfm *tfm,
	                  const u8 *key, unsigned int keylen);
	int (*cit_encrypt)(struct crypto_tfm *tfm,
			   struct scatterlist *dst,
			   struct scatterlist *src,
			   unsigned int nbytes);
	int (*cit_encrypt_iv)(struct crypto_tfm *tfm,
	                      struct scatterlist *dst,
	                      struct scatterlist *src,
	                      unsigned int nbytes, u8 *iv);
	int (*cit_decrypt)(struct crypto_tfm *tfm,
			   struct scatterlist *dst,
			   struct scatterlist *src,
			   unsigned int nbytes);
	int (*cit_decrypt_iv)(struct crypto_tfm *tfm,
			   struct scatterlist *dst,
			   struct scatterlist *src,
			   unsigned int nbytes, u8 *iv);
	void (*cit_xor_block)(u8 *dst, const u8 *src);
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

#define crt_blkcipher	crt_u.blkcipher
#define crt_cipher	crt_u.cipher
#define crt_hash	crt_u.hash
#define crt_compress	crt_u.compress

struct crypto_tfm {

	u32 crt_flags;
	
	union {
		struct blkcipher_tfm blkcipher;
		struct cipher_tfm cipher;
		struct hash_tfm hash;
		struct compress_tfm compress;
	} crt_u;
	
	struct crypto_alg *__crt_alg;

	void *__crt_ctx[] CRYPTO_MINALIGN_ATTR;
};

#define crypto_cipher crypto_tfm
#define crypto_comp crypto_tfm

struct crypto_blkcipher {
	struct crypto_tfm base;
};

struct crypto_hash {
	struct crypto_tfm base;
};

enum {
	CRYPTOA_UNSPEC,
	CRYPTOA_ALG,
};

struct crypto_attr_alg {
	char name[CRYPTO_MAX_ALG_NAME];
};

/* 
 * Transform user interface.
 */
 
struct crypto_tfm *crypto_alloc_tfm(const char *alg_name, u32 tfm_flags);
struct crypto_tfm *crypto_alloc_base(const char *alg_name, u32 type, u32 mask);
void crypto_free_tfm(struct crypto_tfm *tfm);

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

static inline const char *crypto_tfm_alg_modname(struct crypto_tfm *tfm)
{
	return module_name(tfm->__crt_alg->cra_module);
}

static inline u32 crypto_tfm_alg_type(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_flags & CRYPTO_ALG_TYPE_MASK;
}

static unsigned int crypto_tfm_alg_min_keysize(struct crypto_tfm *tfm)
	__deprecated;
static inline unsigned int crypto_tfm_alg_min_keysize(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->__crt_alg->cra_cipher.cia_min_keysize;
}

static unsigned int crypto_tfm_alg_max_keysize(struct crypto_tfm *tfm)
	__deprecated;
static inline unsigned int crypto_tfm_alg_max_keysize(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->__crt_alg->cra_cipher.cia_max_keysize;
}

static unsigned int crypto_tfm_alg_ivsize(struct crypto_tfm *tfm) __deprecated;
static inline unsigned int crypto_tfm_alg_ivsize(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_ivsize;
}

static inline unsigned int crypto_tfm_alg_blocksize(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_blocksize;
}

static inline unsigned int crypto_tfm_alg_digestsize(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	return tfm->__crt_alg->cra_digest.dia_digestsize;
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
	return tfm;
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

static int crypto_cipher_encrypt(struct crypto_tfm *tfm,
				 struct scatterlist *dst,
				 struct scatterlist *src,
				 unsigned int nbytes) __deprecated;
static inline int crypto_cipher_encrypt(struct crypto_tfm *tfm,
                                        struct scatterlist *dst,
                                        struct scatterlist *src,
                                        unsigned int nbytes)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_encrypt(tfm, dst, src, nbytes);
}                                        

static int crypto_cipher_encrypt_iv(struct crypto_tfm *tfm,
				    struct scatterlist *dst,
				    struct scatterlist *src,
				    unsigned int nbytes, u8 *iv) __deprecated;
static inline int crypto_cipher_encrypt_iv(struct crypto_tfm *tfm,
                                           struct scatterlist *dst,
                                           struct scatterlist *src,
                                           unsigned int nbytes, u8 *iv)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_encrypt_iv(tfm, dst, src, nbytes, iv);
}                                        

static int crypto_cipher_decrypt(struct crypto_tfm *tfm,
				 struct scatterlist *dst,
				 struct scatterlist *src,
				 unsigned int nbytes) __deprecated;
static inline int crypto_cipher_decrypt(struct crypto_tfm *tfm,
                                        struct scatterlist *dst,
                                        struct scatterlist *src,
                                        unsigned int nbytes)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_decrypt(tfm, dst, src, nbytes);
}

static int crypto_cipher_decrypt_iv(struct crypto_tfm *tfm,
				    struct scatterlist *dst,
				    struct scatterlist *src,
				    unsigned int nbytes, u8 *iv) __deprecated;
static inline int crypto_cipher_decrypt_iv(struct crypto_tfm *tfm,
                                           struct scatterlist *dst,
                                           struct scatterlist *src,
                                           unsigned int nbytes, u8 *iv)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_decrypt_iv(tfm, dst, src, nbytes, iv);
}

static void crypto_cipher_set_iv(struct crypto_tfm *tfm,
				 const u8 *src, unsigned int len) __deprecated;
static inline void crypto_cipher_set_iv(struct crypto_tfm *tfm,
                                        const u8 *src, unsigned int len)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	memcpy(tfm->crt_cipher.cit_iv, src, len);
}

static void crypto_cipher_get_iv(struct crypto_tfm *tfm,
				 u8 *dst, unsigned int len) __deprecated;
static inline void crypto_cipher_get_iv(struct crypto_tfm *tfm,
                                        u8 *dst, unsigned int len)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	memcpy(dst, tfm->crt_cipher.cit_iv, len);
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
	return tfm;
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
	return crypto_comp_crt(tfm)->cot_compress(tfm, src, slen, dst, dlen);
}

static inline int crypto_comp_decompress(struct crypto_comp *tfm,
                                         const u8 *src, unsigned int slen,
                                         u8 *dst, unsigned int *dlen)
{
	return crypto_comp_crt(tfm)->cot_decompress(tfm, src, slen, dst, dlen);
}

#endif	/* _LINUX_CRYPTO_H */

