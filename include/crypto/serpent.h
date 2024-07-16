/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for serpent algorithms
 */

#ifndef _CRYPTO_SERPENT_H
#define _CRYPTO_SERPENT_H

#include <linux/types.h>
#include <linux/crypto.h>

#define SERPENT_MIN_KEY_SIZE		  0
#define SERPENT_MAX_KEY_SIZE		 32
#define SERPENT_EXPKEY_WORDS		132
#define SERPENT_BLOCK_SIZE		 16

struct serpent_ctx {
	u32 expkey[SERPENT_EXPKEY_WORDS];
};

int __serpent_setkey(struct serpent_ctx *ctx, const u8 *key,
		     unsigned int keylen);
int serpent_setkey(struct crypto_tfm *tfm, const u8 *key, unsigned int keylen);

void __serpent_encrypt(const void *ctx, u8 *dst, const u8 *src);
void __serpent_decrypt(const void *ctx, u8 *dst, const u8 *src);

#endif
