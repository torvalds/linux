/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _CRYPTO_INTERNAL_CHACHA_H
#define _CRYPTO_INTERNAL_CHACHA_H

#include <crypto/chacha.h>
#include <crypto/internal/skcipher.h>
#include <linux/crypto.h>

struct chacha_ctx {
	u32 key[8];
	int nrounds;
};

static inline int chacha_setkey(struct crypto_skcipher *tfm, const u8 *key,
				unsigned int keysize, int nrounds)
{
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	int i;

	if (keysize != CHACHA_KEY_SIZE)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ctx->key); i++)
		ctx->key[i] = get_unaligned_le32(key + i * sizeof(u32));

	ctx->nrounds = nrounds;
	return 0;
}

static inline int chacha20_setkey(struct crypto_skcipher *tfm, const u8 *key,
				  unsigned int keysize)
{
	return chacha_setkey(tfm, key, keysize, 20);
}

static int inline chacha12_setkey(struct crypto_skcipher *tfm, const u8 *key,
				  unsigned int keysize)
{
	return chacha_setkey(tfm, key, keysize, 12);
}

#endif /* _CRYPTO_CHACHA_H */
