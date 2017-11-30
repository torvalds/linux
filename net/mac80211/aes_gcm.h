/*
 * Copyright 2014-2015, Qualcomm Atheros, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef AES_GCM_H
#define AES_GCM_H

#include "aead_api.h"

#define GCM_AAD_LEN	32

static inline int ieee80211_aes_gcm_encrypt(struct crypto_aead *tfm,
					    u8 *j_0, u8 *aad,  u8 *data,
					    size_t data_len, u8 *mic)
{
	return aead_encrypt(tfm, j_0, aad + 2,
			    be16_to_cpup((__be16 *)aad),
			    data, data_len, mic);
}

static inline int ieee80211_aes_gcm_decrypt(struct crypto_aead *tfm,
					    u8 *j_0, u8 *aad, u8 *data,
					    size_t data_len, u8 *mic)
{
	return aead_decrypt(tfm, j_0, aad + 2,
			    be16_to_cpup((__be16 *)aad),
			    data, data_len, mic);
}

static inline struct crypto_aead *
ieee80211_aes_gcm_key_setup_encrypt(const u8 key[], size_t key_len)
{
	return aead_key_setup_encrypt("gcm(aes)", key,
				      key_len, IEEE80211_GCMP_MIC_LEN);
}

static inline void ieee80211_aes_gcm_key_free(struct crypto_aead *tfm)
{
	return aead_key_free(tfm);
}

#endif /* AES_GCM_H */
