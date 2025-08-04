/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for the GHASH hash function
 */

#ifndef __CRYPTO_GHASH_H__
#define __CRYPTO_GHASH_H__

#include <linux/types.h>

#define GHASH_BLOCK_SIZE	16
#define GHASH_DIGEST_SIZE	16

struct gf128mul_4k;

struct ghash_ctx {
	struct gf128mul_4k *gf128;
};

struct ghash_desc_ctx {
	u8 buffer[GHASH_BLOCK_SIZE];
};

#endif
