/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 */

#ifndef AES_CMAC_H
#define AES_CMAC_H

#include <linux/crypto.h>
#include <crypto/hash.h>

struct crypto_shash *ieee80211_aes_cmac_key_setup(const u8 key[],
						  size_t key_len);
void ieee80211_aes_cmac(struct crypto_shash *tfm, const u8 *aad,
			const u8 *data, size_t data_len, u8 *mic);
void ieee80211_aes_cmac_256(struct crypto_shash *tfm, const u8 *aad,
			    const u8 *data, size_t data_len, u8 *mic);
void ieee80211_aes_cmac_key_free(struct crypto_shash *tfm);

#endif /* AES_CMAC_H */
