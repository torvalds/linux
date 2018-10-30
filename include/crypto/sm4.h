/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Common values for the SM4 algorithm
 * Copyright (C) 2018 ARM Limited or its affiliates.
 */

#ifndef _CRYPTO_SM4_H
#define _CRYPTO_SM4_H

#include <linux/types.h>
#include <linux/crypto.h>

#define SM4_KEY_SIZE	16
#define SM4_BLOCK_SIZE	16
#define SM4_RKEY_WORDS	32

struct crypto_sm4_ctx {
	u32 rkey_enc[SM4_RKEY_WORDS];
	u32 rkey_dec[SM4_RKEY_WORDS];
};

int crypto_sm4_set_key(struct crypto_tfm *tfm, const u8 *in_key,
		       unsigned int key_len);
int crypto_sm4_expand_key(struct crypto_sm4_ctx *ctx, const u8 *in_key,
			  unsigned int key_len);

void crypto_sm4_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in);
void crypto_sm4_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in);

#endif
