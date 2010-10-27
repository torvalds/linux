/*
 * pcrypt - Parallel crypto engine.
 *
 * Copyright (C) 2009 secunet Security Networks AG
 * Copyright (C) 2009 Steffen Klassert <steffen.klassert@secunet.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _CRYPTO_PCRYPT_H
#define _CRYPTO_PCRYPT_H

#include <linux/crypto.h>
#include <linux/kernel.h>
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
