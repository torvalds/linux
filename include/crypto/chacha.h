/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values and helper functions for the ChaCha and XChaCha stream ciphers.
 *
 * XChaCha extends ChaCha's nonce to 192 bits, while provably retaining ChaCha's
 * security.  Here they share the same key size, tfm context, and setkey
 * function; only their IV size and encrypt/decrypt function differ.
 *
 * The ChaCha paper specifies 20, 12, and 8-round variants.  In general, it is
 * recommended to use the 20-round variant ChaCha20.  However, the other
 * variants can be needed in some performance-sensitive scenarios.  The generic
 * ChaCha code currently allows only the 20 and 12-round variants.
 */

#ifndef _CRYPTO_CHACHA_H
#define _CRYPTO_CHACHA_H

#include <crypto/skcipher.h>
#include <linux/types.h>
#include <linux/crypto.h>

/* 32-bit stream position, then 96-bit nonce (RFC7539 convention) */
#define CHACHA_IV_SIZE		16

#define CHACHA_KEY_SIZE		32
#define CHACHA_BLOCK_SIZE	64
#define CHACHAPOLY_IV_SIZE	12

/* 192-bit nonce, then 64-bit stream position */
#define XCHACHA_IV_SIZE		32

struct chacha_ctx {
	u32 key[8];
	int nrounds;
};

void chacha_block(u32 *state, u8 *stream, int nrounds);
static inline void chacha20_block(u32 *state, u8 *stream)
{
	chacha_block(state, stream, 20);
}
void hchacha_block(const u32 *in, u32 *out, int nrounds);

void crypto_chacha_init(u32 *state, const struct chacha_ctx *ctx, const u8 *iv);

int crypto_chacha20_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keysize);
int crypto_chacha12_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keysize);

int crypto_chacha_crypt(struct skcipher_request *req);
int crypto_xchacha_crypt(struct skcipher_request *req);

#endif /* _CRYPTO_CHACHA_H */
