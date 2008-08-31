/*
 * Hash algorithms.
 * 
 * Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#ifndef _CRYPTO_INTERNAL_HASH_H
#define _CRYPTO_INTERNAL_HASH_H

#include <crypto/algapi.h>
#include <crypto/hash.h>

struct ahash_request;
struct scatterlist;

struct crypto_hash_walk {
	char *data;

	unsigned int offset;
	unsigned int alignmask;

	struct page *pg;
	unsigned int entrylen;

	unsigned int total;
	struct scatterlist *sg;

	unsigned int flags;
};

extern const struct crypto_type crypto_ahash_type;

int crypto_hash_walk_done(struct crypto_hash_walk *walk, int err);
int crypto_hash_walk_first(struct ahash_request *req,
			   struct crypto_hash_walk *walk);
int crypto_hash_walk_first_compat(struct hash_desc *hdesc,
				  struct crypto_hash_walk *walk,
				  struct scatterlist *sg, unsigned int len);

int crypto_register_shash(struct shash_alg *alg);
int crypto_unregister_shash(struct shash_alg *alg);

static inline void *crypto_ahash_ctx(struct crypto_ahash *tfm)
{
	return crypto_tfm_ctx(&tfm->base);
}

static inline struct ahash_alg *crypto_ahash_alg(
	struct crypto_ahash *tfm)
{
	return &crypto_ahash_tfm(tfm)->__crt_alg->cra_ahash;
}

static inline int ahash_enqueue_request(struct crypto_queue *queue,
					     struct ahash_request *request)
{
	return crypto_enqueue_request(queue, &request->base);
}

static inline struct ahash_request *ahash_dequeue_request(
	struct crypto_queue *queue)
{
	return ahash_request_cast(crypto_dequeue_request(queue));
}

static inline int ahash_tfm_in_queue(struct crypto_queue *queue,
					  struct crypto_ahash *tfm)
{
	return crypto_tfm_in_queue(queue, crypto_ahash_tfm(tfm));
}

static inline void *crypto_shash_ctx(struct crypto_shash *tfm)
{
	return crypto_tfm_ctx(&tfm->base);
}

#endif	/* _CRYPTO_INTERNAL_HASH_H */

