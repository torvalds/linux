/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for GHASH algorithms
 */

#ifndef __CRYPTO_GHASH_H__
#define __CRYPTO_GHASH_H__

#include <linux/types.h>
#include <crypto/gf128mul.h>

#define GHASH_BLOCK_SIZE	16
#define GHASH_DIGEST_SIZE	16

struct ghash_ctx {
	struct gf128mul_4k *gf128;
};

struct ghash_desc_ctx {
	u8 buffer[GHASH_BLOCK_SIZE];
	u32 bytes;
};

#endif
