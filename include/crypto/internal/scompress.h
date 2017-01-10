/*
 * Synchronous Compression operations
 *
 * Copyright 2015 LG Electronics Inc.
 * Copyright (c) 2016, Intel Corporation
 * Author: Giovanni Cabiddu <giovanni.cabiddu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef _CRYPTO_SCOMP_INT_H
#define _CRYPTO_SCOMP_INT_H
#include <linux/crypto.h>

#define SCOMP_SCRATCH_SIZE	131072

struct crypto_scomp {
	struct crypto_tfm base;
};

/**
 * struct scomp_alg - synchronous compression algorithm
 *
 * @alloc_ctx:	Function allocates algorithm specific context
 * @free_ctx:	Function frees context allocated with alloc_ctx
 * @compress:	Function performs a compress operation
 * @decompress:	Function performs a de-compress operation
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
 * @base:	Common crypto API algorithm data structure
 */
struct scomp_alg {
	void *(*alloc_ctx)(struct crypto_scomp *tfm);
	void (*free_ctx)(struct crypto_scomp *tfm, void *ctx);
	int (*compress)(struct crypto_scomp *tfm, const u8 *src,
			unsigned int slen, u8 *dst, unsigned int *dlen,
			void *ctx);
	int (*decompress)(struct crypto_scomp *tfm, const u8 *src,
			  unsigned int slen, u8 *dst, unsigned int *dlen,
			  void *ctx);
	struct crypto_alg base;
};

static inline struct scomp_alg *__crypto_scomp_alg(struct crypto_alg *alg)
{
	return container_of(alg, struct scomp_alg, base);
}

static inline struct crypto_scomp *__crypto_scomp_tfm(struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_scomp, base);
}

static inline struct crypto_tfm *crypto_scomp_tfm(struct crypto_scomp *tfm)
{
	return &tfm->base;
}

static inline void crypto_free_scomp(struct crypto_scomp *tfm)
{
	crypto_destroy_tfm(tfm, crypto_scomp_tfm(tfm));
}

static inline struct scomp_alg *crypto_scomp_alg(struct crypto_scomp *tfm)
{
	return __crypto_scomp_alg(crypto_scomp_tfm(tfm)->__crt_alg);
}

static inline void *crypto_scomp_alloc_ctx(struct crypto_scomp *tfm)
{
	return crypto_scomp_alg(tfm)->alloc_ctx(tfm);
}

static inline void crypto_scomp_free_ctx(struct crypto_scomp *tfm,
					 void *ctx)
{
	return crypto_scomp_alg(tfm)->free_ctx(tfm, ctx);
}

static inline int crypto_scomp_compress(struct crypto_scomp *tfm,
					const u8 *src, unsigned int slen,
					u8 *dst, unsigned int *dlen, void *ctx)
{
	return crypto_scomp_alg(tfm)->compress(tfm, src, slen, dst, dlen, ctx);
}

static inline int crypto_scomp_decompress(struct crypto_scomp *tfm,
					  const u8 *src, unsigned int slen,
					  u8 *dst, unsigned int *dlen,
					  void *ctx)
{
	return crypto_scomp_alg(tfm)->decompress(tfm, src, slen, dst, dlen,
						 ctx);
}

int crypto_init_scomp_ops_async(struct crypto_tfm *tfm);
struct acomp_req *crypto_acomp_scomp_alloc_ctx(struct acomp_req *req);
void crypto_acomp_scomp_free_ctx(struct acomp_req *req);

/**
 * crypto_register_scomp() -- Register synchronous compression algorithm
 *
 * Function registers an implementation of a synchronous
 * compression algorithm
 *
 * @alg:	algorithm definition
 *
 * Return: zero on success; error code in case of error
 */
int crypto_register_scomp(struct scomp_alg *alg);

/**
 * crypto_unregister_scomp() -- Unregister synchronous compression algorithm
 *
 * Function unregisters an implementation of a synchronous
 * compression algorithm
 *
 * @alg:	algorithm definition
 *
 * Return: zero on success; error code in case of error
 */
int crypto_unregister_scomp(struct scomp_alg *alg);

#endif
