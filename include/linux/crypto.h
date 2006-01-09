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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/string.h>
#include <asm/page.h>

/*
 * Algorithm masks and types.
 */
#define CRYPTO_ALG_TYPE_MASK		0x000000ff
#define CRYPTO_ALG_TYPE_CIPHER		0x00000001
#define CRYPTO_ALG_TYPE_DIGEST		0x00000002
#define CRYPTO_ALG_TYPE_COMPRESS	0x00000004

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

struct scatterlist;
struct crypto_tfm;

struct cipher_desc {
	struct crypto_tfm *tfm;
	void (*crfn)(void *ctx, u8 *dst, const u8 *src);
	unsigned int (*prfn)(const struct cipher_desc *desc, u8 *dst,
			     const u8 *src, unsigned int nbytes);
	void *info;
};

/*
 * Algorithms: modular crypto algorithm implementations, managed
 * via crypto_register_alg() and crypto_unregister_alg().
 */
struct cipher_alg {
	unsigned int cia_min_keysize;
	unsigned int cia_max_keysize;
	int (*cia_setkey)(void *ctx, const u8 *key,
	                  unsigned int keylen, u32 *flags);
	void (*cia_encrypt)(void *ctx, u8 *dst, const u8 *src);
	void (*cia_decrypt)(void *ctx, u8 *dst, const u8 *src);

	unsigned int (*cia_encrypt_ecb)(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes);
	unsigned int (*cia_decrypt_ecb)(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes);
	unsigned int (*cia_encrypt_cbc)(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes);
	unsigned int (*cia_decrypt_cbc)(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes);
};

struct digest_alg {
	unsigned int dia_digestsize;
	void (*dia_init)(void *ctx);
	void (*dia_update)(void *ctx, const u8 *data, unsigned int len);
	void (*dia_final)(void *ctx, u8 *out);
	int (*dia_setkey)(void *ctx, const u8 *key,
	                  unsigned int keylen, u32 *flags);
};

struct compress_alg {
	int (*coa_init)(void *ctx);
	void (*coa_exit)(void *ctx);
	int (*coa_compress)(void *ctx, const u8 *src, unsigned int slen,
	                    u8 *dst, unsigned int *dlen);
	int (*coa_decompress)(void *ctx, const u8 *src, unsigned int slen,
	                      u8 *dst, unsigned int *dlen);
};

#define cra_cipher	cra_u.cipher
#define cra_digest	cra_u.digest
#define cra_compress	cra_u.compress

struct crypto_alg {
	struct list_head cra_list;
	u32 cra_flags;
	unsigned int cra_blocksize;
	unsigned int cra_ctxsize;
	unsigned int cra_alignmask;

	int cra_priority;

	const char cra_name[CRYPTO_MAX_ALG_NAME];
	const char cra_driver_name[CRYPTO_MAX_ALG_NAME];

	union {
		struct cipher_alg cipher;
		struct digest_alg digest;
		struct compress_alg compress;
	} cra_u;
	
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
int crypto_alg_available(const char *name, u32 flags);
#else
static inline int crypto_alg_available(const char *name, u32 flags)
{
	return 0;
}
#endif

/*
 * Transforms: user-instantiated objects which encapsulate algorithms
 * and core processing logic.  Managed via crypto_alloc_tfm() and
 * crypto_free_tfm(), as well as the various helpers below.
 */

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
};

struct digest_tfm {
	void (*dit_init)(struct crypto_tfm *tfm);
	void (*dit_update)(struct crypto_tfm *tfm,
	                   struct scatterlist *sg, unsigned int nsg);
	void (*dit_final)(struct crypto_tfm *tfm, u8 *out);
	void (*dit_digest)(struct crypto_tfm *tfm, struct scatterlist *sg,
	                   unsigned int nsg, u8 *out);
	int (*dit_setkey)(struct crypto_tfm *tfm,
	                  const u8 *key, unsigned int keylen);
#ifdef CONFIG_CRYPTO_HMAC
	void *dit_hmac_block;
#endif
};

struct compress_tfm {
	int (*cot_compress)(struct crypto_tfm *tfm,
	                    const u8 *src, unsigned int slen,
	                    u8 *dst, unsigned int *dlen);
	int (*cot_decompress)(struct crypto_tfm *tfm,
	                      const u8 *src, unsigned int slen,
	                      u8 *dst, unsigned int *dlen);
};

#define crt_cipher	crt_u.cipher
#define crt_digest	crt_u.digest
#define crt_compress	crt_u.compress

struct crypto_tfm {

	u32 crt_flags;
	
	union {
		struct cipher_tfm cipher;
		struct digest_tfm digest;
		struct compress_tfm compress;
	} crt_u;
	
	struct crypto_alg *__crt_alg;
};

/* 
 * Transform user interface.
 */
 
/*
 * crypto_alloc_tfm() will first attempt to locate an already loaded algorithm.
 * If that fails and the kernel supports dynamically loadable modules, it
 * will then attempt to load a module of the same name or alias.  A refcount
 * is grabbed on the algorithm which is then associated with the new transform.
 *
 * crypto_free_tfm() frees up the transform and any associated resources,
 * then drops the refcount on the associated algorithm.
 */
struct crypto_tfm *crypto_alloc_tfm(const char *alg_name, u32 tfm_flags);
void crypto_free_tfm(struct crypto_tfm *tfm);

/*
 * Transform helpers which query the underlying algorithm.
 */
static inline const char *crypto_tfm_alg_name(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_name;
}

static inline const char *crypto_tfm_alg_modname(struct crypto_tfm *tfm)
{
	return module_name(tfm->__crt_alg->cra_module);
}

static inline u32 crypto_tfm_alg_type(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_flags & CRYPTO_ALG_TYPE_MASK;
}

static inline unsigned int crypto_tfm_alg_min_keysize(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->__crt_alg->cra_cipher.cia_min_keysize;
}

static inline unsigned int crypto_tfm_alg_max_keysize(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->__crt_alg->cra_cipher.cia_max_keysize;
}

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

static inline void *crypto_tfm_ctx(struct crypto_tfm *tfm)
{
	return (void *)&tfm[1];
}

/*
 * API wrappers.
 */
static inline void crypto_digest_init(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	tfm->crt_digest.dit_init(tfm);
}

static inline void crypto_digest_update(struct crypto_tfm *tfm,
                                        struct scatterlist *sg,
                                        unsigned int nsg)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	tfm->crt_digest.dit_update(tfm, sg, nsg);
}

static inline void crypto_digest_final(struct crypto_tfm *tfm, u8 *out)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	tfm->crt_digest.dit_final(tfm, out);
}

static inline void crypto_digest_digest(struct crypto_tfm *tfm,
                                        struct scatterlist *sg,
                                        unsigned int nsg, u8 *out)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	tfm->crt_digest.dit_digest(tfm, sg, nsg, out);
}

static inline int crypto_digest_setkey(struct crypto_tfm *tfm,
                                       const u8 *key, unsigned int keylen)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	if (tfm->crt_digest.dit_setkey == NULL)
		return -ENOSYS;
	return tfm->crt_digest.dit_setkey(tfm, key, keylen);
}

static inline int crypto_cipher_setkey(struct crypto_tfm *tfm,
                                       const u8 *key, unsigned int keylen)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_setkey(tfm, key, keylen);
}

static inline int crypto_cipher_encrypt(struct crypto_tfm *tfm,
                                        struct scatterlist *dst,
                                        struct scatterlist *src,
                                        unsigned int nbytes)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_encrypt(tfm, dst, src, nbytes);
}                                        

static inline int crypto_cipher_encrypt_iv(struct crypto_tfm *tfm,
                                           struct scatterlist *dst,
                                           struct scatterlist *src,
                                           unsigned int nbytes, u8 *iv)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	BUG_ON(tfm->crt_cipher.cit_mode == CRYPTO_TFM_MODE_ECB);
	return tfm->crt_cipher.cit_encrypt_iv(tfm, dst, src, nbytes, iv);
}                                        

static inline int crypto_cipher_decrypt(struct crypto_tfm *tfm,
                                        struct scatterlist *dst,
                                        struct scatterlist *src,
                                        unsigned int nbytes)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_decrypt(tfm, dst, src, nbytes);
}

static inline int crypto_cipher_decrypt_iv(struct crypto_tfm *tfm,
                                           struct scatterlist *dst,
                                           struct scatterlist *src,
                                           unsigned int nbytes, u8 *iv)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	BUG_ON(tfm->crt_cipher.cit_mode == CRYPTO_TFM_MODE_ECB);
	return tfm->crt_cipher.cit_decrypt_iv(tfm, dst, src, nbytes, iv);
}

static inline void crypto_cipher_set_iv(struct crypto_tfm *tfm,
                                        const u8 *src, unsigned int len)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	memcpy(tfm->crt_cipher.cit_iv, src, len);
}

static inline void crypto_cipher_get_iv(struct crypto_tfm *tfm,
                                        u8 *dst, unsigned int len)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	memcpy(dst, tfm->crt_cipher.cit_iv, len);
}

static inline int crypto_comp_compress(struct crypto_tfm *tfm,
                                       const u8 *src, unsigned int slen,
                                       u8 *dst, unsigned int *dlen)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_COMPRESS);
	return tfm->crt_compress.cot_compress(tfm, src, slen, dst, dlen);
}

static inline int crypto_comp_decompress(struct crypto_tfm *tfm,
                                         const u8 *src, unsigned int slen,
                                         u8 *dst, unsigned int *dlen)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_COMPRESS);
	return tfm->crt_compress.cot_decompress(tfm, src, slen, dst, dlen);
}

/*
 * HMAC support.
 */
#ifdef CONFIG_CRYPTO_HMAC
void crypto_hmac_init(struct crypto_tfm *tfm, u8 *key, unsigned int *keylen);
void crypto_hmac_update(struct crypto_tfm *tfm,
                        struct scatterlist *sg, unsigned int nsg);
void crypto_hmac_final(struct crypto_tfm *tfm, u8 *key,
                       unsigned int *keylen, u8 *out);
void crypto_hmac(struct crypto_tfm *tfm, u8 *key, unsigned int *keylen,
                 struct scatterlist *sg, unsigned int nsg, u8 *out);
#endif	/* CONFIG_CRYPTO_HMAC */

#endif	/* _LINUX_CRYPTO_H */

