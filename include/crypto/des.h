/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * DES & Triple DES EDE Cipher Algorithms.
 */

#ifndef __CRYPTO_DES_H
#define __CRYPTO_DES_H

#include <linux/types.h>

#define DES_KEY_SIZE		8
#define DES_EXPKEY_WORDS	32
#define DES_BLOCK_SIZE		8

#define DES3_EDE_KEY_SIZE	(3 * DES_KEY_SIZE)
#define DES3_EDE_EXPKEY_WORDS	(3 * DES_EXPKEY_WORDS)
#define DES3_EDE_BLOCK_SIZE	DES_BLOCK_SIZE

struct des_ctx {
	u32 expkey[DES_EXPKEY_WORDS];
};

struct des3_ede_ctx {
	u32 expkey[DES3_EDE_EXPKEY_WORDS];
};

void des_encrypt(const struct des_ctx *ctx, u8 *dst, const u8 *src);
void des_decrypt(const struct des_ctx *ctx, u8 *dst, const u8 *src);

void des3_ede_encrypt(const struct des3_ede_ctx *dctx, u8 *dst, const u8 *src);
void des3_ede_decrypt(const struct des3_ede_ctx *dctx, u8 *dst, const u8 *src);

/**
 * des_expand_key - Expand a DES input key into a key schedule
 * @ctx: the key schedule
 * @key: buffer containing the input key
 * @len: size of the buffer contents
 *
 * Returns 0 on success, -EINVAL if the input key is rejected and -ENOKEY if
 * the key is accepted but has been found to be weak.
 */
int des_expand_key(struct des_ctx *ctx, const u8 *key, unsigned int keylen);

/**
 * des3_ede_expand_key - Expand a triple DES input key into a key schedule
 * @ctx: the key schedule
 * @key: buffer containing the input key
 * @len: size of the buffer contents
 *
 * Returns 0 on success, -EINVAL if the input key is rejected and -ENOKEY if
 * the key is accepted but has been found to be weak. Note that weak keys will
 * be rejected (and -EINVAL will be returned) when running in FIPS mode.
 */
int des3_ede_expand_key(struct des3_ede_ctx *ctx, const u8 *key,
			unsigned int keylen);

#endif /* __CRYPTO_DES_H */
