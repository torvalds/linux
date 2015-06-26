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

struct aead_geniv_ctx {
	spinlock_t lock;
	struct crypto_aead *child;
};

#endif	/* _CRYPTO_INTERNAL_GENIV_H */
