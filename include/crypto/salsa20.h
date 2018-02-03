/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for the Salsa20 algorithm
 */

#ifndef _CRYPTO_SALSA20_H
#define _CRYPTO_SALSA20_H

#include <linux/types.h>

#define SALSA20_IV_SIZE		8
#define SALSA20_MIN_KEY_SIZE	16
#define SALSA20_MAX_KEY_SIZE	32
#define SALSA20_BLOCK_SIZE	64

struct crypto_skcipher;

struct salsa20_ctx {
	u32 initial_state[16];
};

void crypto_salsa20_init(u32 *state, const struct salsa20_ctx *ctx,
			 const u8 *iv);
int crypto_salsa20_setkey(struct crypto_skcipher *tfm, const u8 *key,
			  unsigned int keysize);

#endif /* _CRYPTO_SALSA20_H */
