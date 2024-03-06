/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Public Key Signature Algorithm
 *
 * Copyright (c) 2023 Herbert Xu <herbert@gondor.apana.org.au>
 */
#ifndef _CRYPTO_INTERNAL_SIG_H
#define _CRYPTO_INTERNAL_SIG_H

#include <crypto/algapi.h>
#include <crypto/sig.h>

static inline void *crypto_sig_ctx(struct crypto_sig *tfm)
{
	return crypto_tfm_ctx(&tfm->base);
}
#endif
