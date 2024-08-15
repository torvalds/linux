/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * FILS AEAD for (Re)Association Request/Response frames
 * Copyright 2016, Qualcomm Atheros, Inc.
 */

#ifndef FILS_AEAD_H
#define FILS_AEAD_H

int fils_encrypt_assoc_req(struct sk_buff *skb,
			   struct ieee80211_mgd_assoc_data *assoc_data);
int fils_decrypt_assoc_resp(struct ieee80211_sub_if_data *sdata,
			    u8 *frame, size_t *frame_len,
			    struct ieee80211_mgd_assoc_data *assoc_data);

#endif /* FILS_AEAD_H */
