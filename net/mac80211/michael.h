/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Michael MIC implementation - optimized for TKIP MIC operations
 * Copyright 2002-2003, Instant802 Networks, Inc.
 */

#ifndef MICHAEL_H
#define MICHAEL_H

#include <linux/types.h>
#include <linux/ieee80211.h>

#define MICHAEL_MIC_LEN 8

struct michael_mic_ctx {
	u32 l, r;
};

void michael_mic(const u8 *key, struct ieee80211_hdr *hdr,
		 const u8 *data, size_t data_len, u8 *mic);

#endif /* MICHAEL_H */
