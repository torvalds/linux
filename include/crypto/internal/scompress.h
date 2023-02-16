/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Synchronous Compression operations
 *
 * Copyright 2015 LG Electronics Inc.
 * Copyright (c) 2016, Intel Corporation
 * Author: Giovanni Cabiddu <giovanni.cabiddu@intel.com>
 */
#ifndef _CRYPTO_SCOMP_INT_H
#define _CRYPTO_SCOMP_INT_H

#include <crypto/acompress.h>
#include <crypto/algapi.h>

#define SCOMP_SCRATCH_SIZE	131072

struct acomp_req;

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
 * @stat:	Statistics for compress algorithm
 * @base:	Common crypto API algorithm data structure
 * @calg:	Cmonn algorithm data structure shared with acomp
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

	union {
		struct COMP_ALG_COMMON;
		struct comp_alg_common calg;
	};
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
 */
void crypto_unregister_scomp(struct scomp_alg *alg);

int crypto_register_scomps(struct scomp_alg *algs, int count);
void crypto_unregister_scomps(struct scomp_alg *algs, int count);

#endif
