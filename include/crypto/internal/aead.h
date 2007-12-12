/*
 * AEAD: Authenticated Encryption with Associated Data
 * 
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#ifndef _CRYPTO_INTERNAL_AEAD_H
#define _CRYPTO_INTERNAL_AEAD_H

#include <crypto/aead.h>
#include <crypto/algapi.h>
#include <linux/types.h>

struct rtattr;

struct crypto_aead_spawn {
	struct crypto_spawn base;
};

extern const struct crypto_type crypto_nivaead_type;

static inline void crypto_set_aead_spawn(
	struct crypto_aead_spawn *spawn, struct crypto_instance *inst)
{
	crypto_set_spawn(&spawn->base, inst);
}

int crypto_grab_aead(struct crypto_aead_spawn *spawn, const char *name,
		     u32 type, u32 mask);

static inline void crypto_drop_aead(struct crypto_aead_spawn *spawn)
{
	crypto_drop_spawn(&spawn->base);
}

static inline struct crypto_alg *crypto_aead_spawn_alg(
	struct crypto_aead_spawn *spawn)
{
	return spawn->base.alg;
}

static inline struct crypto_aead *crypto_spawn_aead(
	struct crypto_aead_spawn *spawn)
{
	return __crypto_aead_cast(
		crypto_spawn_tfm(&spawn->base, CRYPTO_ALG_TYPE_AEAD,
				 CRYPTO_ALG_TYPE_MASK));
}

struct crypto_instance *aead_geniv_alloc(struct crypto_template *tmpl,
					 struct rtattr **tb, u32 type,
					 u32 mask);
void aead_geniv_free(struct crypto_instance *inst);
int aead_geniv_init(struct crypto_tfm *tfm);
void aead_geniv_exit(struct crypto_tfm *tfm);

static inline struct crypto_aead *aead_geniv_base(struct crypto_aead *geniv)
{
	return crypto_aead_crt(geniv)->base;
}

static inline void *aead_givcrypt_reqctx(struct aead_givcrypt_request *req)
{
	return aead_request_ctx(&req->areq);
}

static inline void aead_givcrypt_complete(struct aead_givcrypt_request *req,
					  int err)
{
	aead_request_complete(&req->areq, err);
}

#endif	/* _CRYPTO_INTERNAL_AEAD_H */

