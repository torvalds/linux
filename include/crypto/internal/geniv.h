/*
 * geniv: IV generation
 *
 * Copyright (c) 2015 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef _CRYPTO_INTERNAL_GENIV_H
#define _CRYPTO_INTERNAL_GENIV_H

#include <crypto/internal/aead.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct aead_geniv_ctx {
	spinlock_t lock;
	struct crypto_aead *child;
	struct crypto_sync_skcipher *sknull;
	u8 salt[] __attribute__ ((aligned(__alignof__(u32))));
};

struct aead_instance *aead_geniv_alloc(struct crypto_template *tmpl,
				       struct rtattr **tb, u32 type, u32 mask);
void aead_geniv_free(struct aead_instance *inst);
int aead_init_geniv(struct crypto_aead *tfm);
void aead_exit_geniv(struct crypto_aead *tfm);

#endif	/* _CRYPTO_INTERNAL_GENIV_H */
