/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Crypto engine API
 *
 * Copyright (c) 2016 Baolin Wang <baolin.wang@linaro.org>
 */
#ifndef _CRYPTO_ENGINE_H
#define _CRYPTO_ENGINE_H

#include <linux/types.h>

struct aead_request;
struct ahash_request;
struct akcipher_request;
struct crypto_engine;
struct device;
struct kpp_request;
struct skcipher_request;

/*
 * struct crypto_engine_op - crypto hardware engine operations
 * @do_one_request: do encryption for current request
 */
struct crypto_engine_op {
	int (*do_one_request)(struct crypto_engine *engine,
			      void *areq);
};

struct crypto_engine_ctx {
	struct crypto_engine_op op;
};

int crypto_transfer_aead_request_to_engine(struct crypto_engine *engine,
					   struct aead_request *req);
int crypto_transfer_akcipher_request_to_engine(struct crypto_engine *engine,
					       struct akcipher_request *req);
int crypto_transfer_hash_request_to_engine(struct crypto_engine *engine,
					       struct ahash_request *req);
int crypto_transfer_kpp_request_to_engine(struct crypto_engine *engine,
					  struct kpp_request *req);
int crypto_transfer_skcipher_request_to_engine(struct crypto_engine *engine,
					       struct skcipher_request *req);
void crypto_finalize_aead_request(struct crypto_engine *engine,
				  struct aead_request *req, int err);
void crypto_finalize_akcipher_request(struct crypto_engine *engine,
				      struct akcipher_request *req, int err);
void crypto_finalize_hash_request(struct crypto_engine *engine,
				  struct ahash_request *req, int err);
void crypto_finalize_kpp_request(struct crypto_engine *engine,
				 struct kpp_request *req, int err);
void crypto_finalize_skcipher_request(struct crypto_engine *engine,
				      struct skcipher_request *req, int err);
int crypto_engine_start(struct crypto_engine *engine);
int crypto_engine_stop(struct crypto_engine *engine);
struct crypto_engine *crypto_engine_alloc_init(struct device *dev, bool rt);
struct crypto_engine *crypto_engine_alloc_init_and_set(struct device *dev,
						       bool retry_support,
						       int (*cbk_do_batch)(struct crypto_engine *engine),
						       bool rt, int qlen);
int crypto_engine_exit(struct crypto_engine *engine);

#endif /* _CRYPTO_ENGINE_H */
