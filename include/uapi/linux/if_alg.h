/*
 * if_alg: User-space algorithm interface
 *
 * Copyright (c) 2010 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef _LINUX_IF_ALG_H
#define _LINUX_IF_ALG_H

#include <linux/types.h>

struct sockaddr_alg {
	__u16	salg_family;
	__u8	salg_type[14];
	__u32	salg_feat;
	__u32	salg_mask;
	__u8	salg_name[64];
};

struct af_alg_iv {
	__u32	ivlen;
	__u8	iv[0];
};

/* Socket options */
#define ALG_SET_KEY			1
#define ALG_SET_IV			2
#define ALG_SET_OP			3
#define ALG_SET_AEAD_ASSOCLEN		4
#define ALG_SET_AEAD_AUTHSIZE		5

/* Operations */
#define ALG_OP_DECRYPT			0
#define ALG_OP_ENCRYPT			1

#endif	/* _LINUX_IF_ALG_H */
