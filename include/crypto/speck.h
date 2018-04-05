// SPDX-License-Identifier: GPL-2.0
/*
 * Common values for the Speck algorithm
 */

#ifndef _CRYPTO_SPECK_H
#define _CRYPTO_SPECK_H

#include <linux/types.h>

/* Speck128 */

#define SPECK128_BLOCK_SIZE	16

#define SPECK128_128_KEY_SIZE	16
#define SPECK128_128_NROUNDS	32

#define SPECK128_192_KEY_SIZE	24
#define SPECK128_192_NROUNDS	33

#define SPECK128_256_KEY_SIZE	32
#define SPECK128_256_NROUNDS	34

struct speck128_tfm_ctx {
	u64 round_keys[SPECK128_256_NROUNDS];
	int nrounds;
};

void crypto_speck128_encrypt(const struct speck128_tfm_ctx *ctx,
			     u8 *out, const u8 *in);

void crypto_speck128_decrypt(const struct speck128_tfm_ctx *ctx,
			     u8 *out, const u8 *in);

int crypto_speck128_setkey(struct speck128_tfm_ctx *ctx, const u8 *key,
			   unsigned int keysize);

/* Speck64 */

#define SPECK64_BLOCK_SIZE	8

#define SPECK64_96_KEY_SIZE	12
#define SPECK64_96_NROUNDS	26

#define SPECK64_128_KEY_SIZE	16
#define SPECK64_128_NROUNDS	27

struct speck64_tfm_ctx {
	u32 round_keys[SPECK64_128_NROUNDS];
	int nrounds;
};

void crypto_speck64_encrypt(const struct speck64_tfm_ctx *ctx,
			    u8 *out, const u8 *in);

void crypto_speck64_decrypt(const struct speck64_tfm_ctx *ctx,
			    u8 *out, const u8 *in);

int crypto_speck64_setkey(struct speck64_tfm_ctx *ctx, const u8 *key,
			  unsigned int keysize);

#endif /* _CRYPTO_SPECK_H */
