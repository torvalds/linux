#ifndef _CRYPTO_CAST5_H
#define _CRYPTO_CAST5_H

#include <linux/types.h>
#include <linux/crypto.h>
#include <crypto/cast_common.h>

#define CAST5_BLOCK_SIZE 8
#define CAST5_MIN_KEY_SIZE 5
#define CAST5_MAX_KEY_SIZE 16

struct cast5_ctx {
	u32 Km[16];
	u8 Kr[16];
	int rr;	/* rr ? rounds = 12 : rounds = 16; (rfc 2144) */
};

int cast5_setkey(struct crypto_tfm *tfm, const u8 *key, unsigned int keylen);

void __cast5_encrypt(struct cast5_ctx *ctx, u8 *dst, const u8 *src);
void __cast5_decrypt(struct cast5_ctx *ctx, u8 *dst, const u8 *src);

#endif
