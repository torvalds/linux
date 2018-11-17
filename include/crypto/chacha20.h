/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values and helper functions for the ChaCha20 and XChaCha20 algorithms.
 *
 * XChaCha20 extends ChaCha20's nonce to 192 bits, while provably retaining
 * ChaCha20's security.  Here they share the same key size, tfm context, and
 * setkey function; only their IV size and encrypt/decrypt function differ.
 */

#ifndef _CRYPTO_CHACHA20_H
#define _CRYPTO_CHACHA20_H

#include <crypto/skcipher.h>
#include <linux/types.h>
#include <linux/crypto.h>

/* 32-bit stream position, then 96-bit nonce (RFC7539 convention) */
#define CHACHA20_IV_SIZE	16

#define CHACHA20_KEY_SIZE	32
#define CHACHA20_BLOCK_SIZE	64
#define CHACHAPOLY_IV_SIZE	12

/* 192-bit nonce, then 64-bit stream position */
#define XCHACHA20_IV_SIZE	32

struct chacha20_ctx {
	u32 key[8];
};

void chacha20_block(u32 *state, u8 *stream);
void hchacha20_block(const u32 *in, u32 *out);

void crypto_chacha20_init(u32 *state, struct chacha20_ctx *ctx, u8 *iv);

int crypto_chacha20_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keysize);

int crypto_chacha20_crypt(struct skcipher_request *req);
int crypto_xchacha20_crypt(struct skcipher_request *req);

#endif
