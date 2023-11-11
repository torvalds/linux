/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2003-2004, Instant802 Networks, Inc.
 * Copyright 2006, Devicescape Software, Inc.
 */

#ifndef AES_CCM_H
#define AES_CCM_H

#include "aead_api.h"

#define CCM_AAD_LEN	32

static inline struct crypto_aead *
ieee80211_aes_key_setup_encrypt(const u8 key[], size_t key_len, size_t mic_len)
{
	return aead_key_setup_encrypt("ccm(aes)", key, key_len, mic_len);
}

static inline int
ieee80211_aes_ccm_encrypt(struct crypto_aead *tfm,
			  u8 *b_0, u8 *aad, u8 *data,
			  size_t data_len, u8 *mic)
{
	return aead_encrypt(tfm, b_0, aad + 2,
			    be16_to_cpup((__be16 *)aad),
			    data, data_len, mic);
}

static inline int
ieee80211_aes_ccm_decrypt(struct crypto_aead *tfm,
			  u8 *b_0, u8 *aad, u8 *data,
			  size_t data_len, u8 *mic)
{
	return aead_decrypt(tfm, b_0, aad + 2,
			    be16_to_cpup((__be16 *)aad),
			    data, data_len, mic);
}

static inline void ieee80211_aes_key_free(struct crypto_aead *tfm)
{
	return aead_key_free(tfm);
}

#endif /* AES_CCM_H */
