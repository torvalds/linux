/*
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef AES_CMAC_H
#define AES_CMAC_H

#include <linux/crypto.h>

void gf_mulx(u8 *pad);
void aes_cmac_vector(struct crypto_cipher *tfm, size_t num_elem,
		     const u8 *addr[], const size_t *len, u8 *mac,
		     size_t mac_len);
struct crypto_cipher *ieee80211_aes_cmac_key_setup(const u8 key[],
						   size_t key_len);
void ieee80211_aes_cmac(struct crypto_cipher *tfm, const u8 *aad,
			const u8 *data, size_t data_len, u8 *mic);
void ieee80211_aes_cmac_256(struct crypto_cipher *tfm, const u8 *aad,
			    const u8 *data, size_t data_len, u8 *mic);
void ieee80211_aes_cmac_key_free(struct crypto_cipher *tfm);

#endif /* AES_CMAC_H */
