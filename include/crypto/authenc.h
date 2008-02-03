/*
 * Authenc: Simple AEAD wrapper for IPsec
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef _CRYPTO_AUTHENC_H
#define _CRYPTO_AUTHENC_H

#include <linux/types.h>

enum {
	CRYPTO_AUTHENC_KEYA_UNSPEC,
	CRYPTO_AUTHENC_KEYA_PARAM,
};

struct crypto_authenc_key_param {
	__be32 enckeylen;
};

#endif	/* _CRYPTO_AUTHENC_H */

