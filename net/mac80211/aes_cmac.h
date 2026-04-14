/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 */

#ifndef AES_CMAC_H
#define AES_CMAC_H

#include <crypto/aes-cbc-macs.h>

void ieee80211_aes_cmac(const struct aes_cmac_key *key, const u8 *aad,
			const u8 *data, size_t data_len, u8 *mic,
			unsigned int mic_len);

#endif /* AES_CMAC_H */
