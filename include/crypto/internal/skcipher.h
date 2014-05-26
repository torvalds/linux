/*
 * Symmetric key ciphers.
 * 
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#ifndef _CRYPTO_INTERNAL_SKCIPHER_H
#define _CRYPTO_INTERNAL_SKCIPHER_H

#include <crypto/algapi.h>
#include <crypto/skcipher.h>
#include <linux/types.h>

struct rtattr;

struct crypto_skcipher_spawn {
	struct crypto_spawn base;
};

extern const struct crypto_type crypto_givcipher_type;

static inline void crypto_set_skcipher_spawn(
	struct crypto_skcipher_spawn *spawn, struct crypto_instance *inst)
{
	crypto_set_spawn(&spawn->base, inst);
}

int crypto_grab_skcipher(struct crypto_skcipher_spawn *spawn, const char *name,
			 u32 type, u32 mask);

struct crypto_alg *crypto_lookup_skcipher(const char *name, u32 type, u32 mask);

static inline void crypto_drop_skcipher(struct crypto_skcipher_spawn *spawn)
{
	crypto_drop_spawn(&spawn->base);
}

static inline struct crypto_alg *crypto_skcipher_spawn_alg(
	struct crypto_skcipher_spawn *spawn)
{
	return spawn->base.alg;
}

static inline struct crypto_ablkcipher *crypto_spawn_skcipher(
	struct crypto_skcipher_spawn *spawn)
{
	return __crypto_ablkcipher_cast(
		crypto_spawn_tfm(&spawn->base, crypto_skcipher_type(0),
				 crypto_skcipher_mask(0)));
}

int skcipher_null_givencrypt(struct skcipher_givcrypt_request *req);
int skcipher_null_givdecrypt(struct skcipher_givcrypt_request *req);
const char *crypto_default_geniv(const struct crypto_alg *alg);

struct crypto_instance *skcipher_geniv_alloc(struct crypto_template *tmpl,
					     struct rtattr **tb, u32 type,
					     u32 mask);
void skcipher_geniv_free(struct crypto_instance *inst);
int skcipher_geniv_init(struct crypto_tfm *tfm);
void skcipher_geniv_exit(struct crypto_tfm *tfm);

static inline struct crypto_ablkcipher *skcipher_geniv_cipher(
	struct crypto_ablkcipher *geniv)
{
	return crypto_ablkcipher_crt(geniv)->base;
}

static inline int skcipher_enqueue_givcrypt(
	struct crypto_queue *queue, struct skcipher_givcrypt_request *request)
{
	return ablkcipher_enqueue_request(queue, &request->creq);
}

static inline struct skcipher_givcrypt_request *skcipher_dequeue_givcrypt(
	struct crypto_queue *queue)
{
	return skcipher_givcrypt_cast(crypto_dequeue_request(queue));
}

static inline void *skcipher_givcrypt_reqctx(
	struct skcipher_givcrypt_request *req)
{
	return ablkcipher_request_ctx(&req->creq);
}

static inline void ablkcipher_request_complete(struct ablkcipher_request *req,
					       int err)
{
	req->base.complete(&req->base, err);
}

static inline void skcipher_givcrypt_complete(
	struct skcipher_givcrypt_request *req, int err)
{
	ablkcipher_request_complete(&req->creq, err);
}

static inline u32 ablkcipher_request_flags(struct ablkcipher_request *req)
{
	return req->base.flags;
}

#endif	/* _CRYPTO_INTERNAL_SKCIPHER_H */

