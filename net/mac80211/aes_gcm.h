/*
 * Copyright 2014-2015, Qualcomm Atheros, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef AES_GCM_H
#define AES_GCM_H

#include <linux/crypto.h>

#define GCM_AAD_LEN	32

int ieee80211_aes_gcm_encrypt(struct crypto_aead *tfm, u8 *j_0, u8 *aad,
			      u8 *data, size_t data_len, u8 *mic);
int ieee80211_aes_gcm_decrypt(struct crypto_aead *tfm, u8 *j_0, u8 *aad,
			      u8 *data, size_t data_len, u8 *mic);
struct crypto_aead *ieee80211_aes_gcm_key_setup_encrypt(const u8 key[],
							size_t key_len);
void ieee80211_aes_gcm_key_free(struct crypto_aead *tfm);

#endif /* AES_GCM_H */
