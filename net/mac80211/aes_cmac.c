// SPDX-License-Identifier: GPL-2.0-only
/*
 * AES-128-CMAC with TLen 16 for IEEE 802.11w BIP
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 * Copyright (C) 2020 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/export.h>
#include <linux/err.h>
#include <crypto/aes.h>

#include <net/mac80211.h>
#include "key.h"
#include "aes_cmac.h"

#define CMAC_TLEN 8 /* CMAC TLen = 64 bits (8 octets) */
#define CMAC_TLEN_256 16 /* CMAC TLen = 128 bits (16 octets) */
#define AAD_LEN 20

static const u8 zero[CMAC_TLEN_256];

void ieee80211_aes_cmac(struct crypto_shash *tfm, const u8 *aad,
			const u8 *data, size_t data_len, u8 *mic)
{
	SHASH_DESC_ON_STACK(desc, tfm);
	u8 out[AES_BLOCK_SIZE];
	const __le16 *fc;

	desc->tfm = tfm;

	crypto_shash_init(desc);
	crypto_shash_update(desc, aad, AAD_LEN);
	fc = (const __le16 *)aad;
	if (ieee80211_is_beacon(*fc)) {
		/* mask Timestamp field to zero */
		crypto_shash_update(desc, zero, 8);
		crypto_shash_update(desc, data + 8, data_len - 8 - CMAC_TLEN);
	} else {
		crypto_shash_update(desc, data, data_len - CMAC_TLEN);
	}
	crypto_shash_finup(desc, zero, CMAC_TLEN, out);

	memcpy(mic, out, CMAC_TLEN);
}

void ieee80211_aes_cmac_256(struct crypto_shash *tfm, const u8 *aad,
			    const u8 *data, size_t data_len, u8 *mic)
{
	SHASH_DESC_ON_STACK(desc, tfm);
	const __le16 *fc;

	desc->tfm = tfm;

	crypto_shash_init(desc);
	crypto_shash_update(desc, aad, AAD_LEN);
	fc = (const __le16 *)aad;
	if (ieee80211_is_beacon(*fc)) {
		/* mask Timestamp field to zero */
		crypto_shash_update(desc, zero, 8);
		crypto_shash_update(desc, data + 8,
				    data_len - 8 - CMAC_TLEN_256);
	} else {
		crypto_shash_update(desc, data, data_len - CMAC_TLEN_256);
	}
	crypto_shash_finup(desc, zero, CMAC_TLEN_256, mic);
}

struct crypto_shash *ieee80211_aes_cmac_key_setup(const u8 key[],
						  size_t key_len)
{
	struct crypto_shash *tfm;

	tfm = crypto_alloc_shash("cmac(aes)", 0, 0);
	if (!IS_ERR(tfm)) {
		int err = crypto_shash_setkey(tfm, key, key_len);

		if (err) {
			crypto_free_shash(tfm);
			return ERR_PTR(err);
		}
	}

	return tfm;
}

void ieee80211_aes_cmac_key_free(struct crypto_shash *tfm)
{
	crypto_free_shash(tfm);
}
