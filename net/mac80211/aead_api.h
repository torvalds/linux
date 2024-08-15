/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _AEAD_API_H
#define _AEAD_API_H

#include <crypto/aead.h>
#include <linux/crypto.h>

struct crypto_aead *
aead_key_setup_encrypt(const char *alg, const u8 key[],
		       size_t key_len, size_t mic_len);

int aead_encrypt(struct crypto_aead *tfm, u8 *b_0, u8 *aad,
		 size_t aad_len, u8 *data,
		 size_t data_len, u8 *mic);

int aead_decrypt(struct crypto_aead *tfm, u8 *b_0, u8 *aad,
		 size_t aad_len, u8 *data,
		 size_t data_len, u8 *mic);

void aead_key_free(struct crypto_aead *tfm);

#endif /* _AEAD_API_H */
