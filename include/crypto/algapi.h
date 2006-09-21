/*
 * Cryptographic API for algorithms (i.e., low-level API).
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#ifndef _CRYPTO_ALGAPI_H
#define _CRYPTO_ALGAPI_H

#include <linux/crypto.h>

struct module;

struct crypto_instance {
	struct crypto_alg alg;

	struct crypto_template *tmpl;
	struct hlist_node list;

	void *__ctx[] CRYPTO_MINALIGN_ATTR;
};

struct crypto_template {
	struct list_head list;
	struct hlist_head instances;
	struct module *module;

	struct crypto_instance *(*alloc)(void *param, unsigned int len);
	void (*free)(struct crypto_instance *inst);

	char name[CRYPTO_MAX_ALG_NAME];
};

struct crypto_spawn {
	struct list_head list;
	struct crypto_alg *alg;
	struct crypto_instance *inst;
};

int crypto_register_template(struct crypto_template *tmpl);
void crypto_unregister_template(struct crypto_template *tmpl);
struct crypto_template *crypto_lookup_template(const char *name);

int crypto_init_spawn(struct crypto_spawn *spawn, struct crypto_alg *alg,
		      struct crypto_instance *inst);
void crypto_drop_spawn(struct crypto_spawn *spawn);
struct crypto_tfm *crypto_spawn_tfm(struct crypto_spawn *spawn);

static inline void *crypto_instance_ctx(struct crypto_instance *inst)
{
	return inst->__ctx;
}

#endif	/* _CRYPTO_ALGAPI_H */

