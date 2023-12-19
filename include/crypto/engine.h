/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Crypto engine API
 *
 * Copyright (c) 2016 Baolin Wang <baolin.wang@linaro.org>
 */
#ifndef _CRYPTO_ENGINE_H
#define _CRYPTO_ENGINE_H

#include <crypto/aead.h>
#include <crypto/akcipher.h>
#include <crypto/hash.h>
#include <crypto/kpp.h>
#include <crypto/skcipher.h>
#include <linux/types.h>

struct crypto_engine;
struct device;

/*
 * struct crypto_engine_op - crypto hardware engine operations
 * @do_one_request: do encryption for current request
 */
struct crypto_engine_op {
	int (*do_one_request)(struct crypto_engine *engine,
			      void *areq);
};

struct aead_engine_alg {
	struct aead_alg base;
	struct crypto_engine_op op;
};

struct ahash_engine_alg {
	struct ahash_alg base;
	struct crypto_engine_op op;
};

struct akcipher_engine_alg {
	struct akcipher_alg base;
	struct crypto_engine_op op;
};

struct kpp_engine_alg {
	struct kpp_alg base;
	struct crypto_engine_op op;
};

struct skcipher_engine_alg {
	struct skcipher_alg base;
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
void crypto_engine_exit(struct crypto_engine *engine);

int crypto_engine_register_aead(struct aead_engine_alg *alg);
void crypto_engine_unregister_aead(struct aead_engine_alg *alg);
int crypto_engine_register_aeads(struct aead_engine_alg *algs, int count);
void crypto_engine_unregister_aeads(struct aead_engine_alg *algs, int count);

int crypto_engine_register_ahash(struct ahash_engine_alg *alg);
void crypto_engine_unregister_ahash(struct ahash_engine_alg *alg);
int crypto_engine_register_ahashes(struct ahash_engine_alg *algs, int count);
void crypto_engine_unregister_ahashes(struct ahash_engine_alg *algs,
				      int count);

int crypto_engine_register_akcipher(struct akcipher_engine_alg *alg);
void crypto_engine_unregister_akcipher(struct akcipher_engine_alg *alg);

int crypto_engine_register_kpp(struct kpp_engine_alg *alg);
void crypto_engine_unregister_kpp(struct kpp_engine_alg *alg);

int crypto_engine_register_skcipher(struct skcipher_engine_alg *alg);
void crypto_engine_unregister_skcipher(struct skcipher_engine_alg *alg);
int crypto_engine_register_skciphers(struct skcipher_engine_alg *algs,
				     int count);
void crypto_engine_unregister_skciphers(struct skcipher_engine_alg *algs,
					int count);

#endif /* _CRYPTO_ENGINE_H */
