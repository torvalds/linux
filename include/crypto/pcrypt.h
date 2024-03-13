/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * pcrypt - Parallel crypto engine.
 *
 * Copyright (C) 2009 secunet Security Networks AG
 * Copyright (C) 2009 Steffen Klassert <steffen.klassert@secunet.com>
 */

#ifndef _CRYPTO_PCRYPT_H
#define _CRYPTO_PCRYPT_H

#include <linux/container_of.h>
#include <linux/crypto.h>
#include <linux/padata.h>

struct pcrypt_request {
	struct padata_priv	padata;
	void			*data;
	void			*__ctx[] CRYPTO_MINALIGN_ATTR;
};

static inline void *pcrypt_request_ctx(struct pcrypt_request *req)
{
	return req->__ctx;
}

static inline
struct padata_priv *pcrypt_request_padata(struct pcrypt_request *req)
{
	return &req->padata;
}

static inline
struct pcrypt_request *pcrypt_padata_request(struct padata_priv *padata)
{
	return container_of(padata, struct pcrypt_request, padata);
}

#endif
