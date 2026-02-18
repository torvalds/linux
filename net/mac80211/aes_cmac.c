// SPDX-License-Identifier: GPL-2.0-only
/*
 * AES-128-CMAC with TLen 16 for IEEE 802.11w BIP
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 * Copyright (C) 2020 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/err.h>
#include <crypto/aes-cbc-macs.h>

#include <net/mac80211.h>
#include "key.h"
#include "aes_cmac.h"

#define AAD_LEN 20

static const u8 zero[IEEE80211_CMAC_256_MIC_LEN];

void ieee80211_aes_cmac(const struct aes_cmac_key *key, const u8 *aad,
			const u8 *data, size_t data_len, u8 *mic,
			unsigned int mic_len)
{
	struct aes_cmac_ctx ctx;
	u8 out[AES_BLOCK_SIZE];
	const __le16 *fc;

	aes_cmac_init(&ctx, key);
	aes_cmac_update(&ctx, aad, AAD_LEN);
	fc = (const __le16 *)aad;
	if (ieee80211_is_beacon(*fc)) {
		/* mask Timestamp field to zero */
		aes_cmac_update(&ctx, zero, 8);
		aes_cmac_update(&ctx, data + 8, data_len - 8 - mic_len);
	} else {
		aes_cmac_update(&ctx, data, data_len - mic_len);
	}
	aes_cmac_update(&ctx, zero, mic_len);
	aes_cmac_final(&ctx, out);
	memcpy(mic, out, mic_len);
}
