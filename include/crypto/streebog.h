/* SPDX-License-Identifier: GPL-2.0+ OR BSD-2-Clause */
/*
 * Copyright (c) 2013 Alexey Degtyarev <alexey@renatasystems.org>
 * Copyright (c) 2018 Vitaly Chikunov <vt@altlinux.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef _CRYPTO_STREEBOG_H_
#define _CRYPTO_STREEBOG_H_

#include <linux/types.h>

#define STREEBOG256_DIGEST_SIZE	32
#define STREEBOG512_DIGEST_SIZE	64
#define STREEBOG_BLOCK_SIZE	64

struct streebog_uint512 {
	__le64 qword[8];
};

struct streebog_state {
	union {
		u8 buffer[STREEBOG_BLOCK_SIZE];
		struct streebog_uint512 m;
	};
	struct streebog_uint512 hash;
	struct streebog_uint512 h;
	struct streebog_uint512 N;
	struct streebog_uint512 Sigma;
	size_t fillsize;
};

#endif /* !_CRYPTO_STREEBOG_H_ */
