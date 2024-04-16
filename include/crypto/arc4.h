/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Common values for ARC4 Cipher Algorithm
 */

#ifndef _CRYPTO_ARC4_H
#define _CRYPTO_ARC4_H

#include <linux/types.h>

#define ARC4_MIN_KEY_SIZE	1
#define ARC4_MAX_KEY_SIZE	256
#define ARC4_BLOCK_SIZE		1

struct arc4_ctx {
	u32 S[256];
	u32 x, y;
};

int arc4_setkey(struct arc4_ctx *ctx, const u8 *in_key, unsigned int key_len);
void arc4_crypt(struct arc4_ctx *ctx, u8 *out, const u8 *in, unsigned int len);

#endif /* _CRYPTO_ARC4_H */
