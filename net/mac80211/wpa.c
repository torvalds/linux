// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2002-2004, Instant802 Networks, Inc.
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 * Copyright (C) 2020-2021 Intel Corporation
 */

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/compiler.h>
#include <linux/ieee80211.h>
#include <linux/gfp.h>
#include <asm/unaligned.h>
#include <net/mac80211.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>

#include "ieee80211_i.h"
#include "michael.h"
#include "tkip.h"
#include "aes_ccm.h"
#include "aes_cmac.h"
#include "aes_gmac.h"
#include "aes_gcm.h"
#include "wpa.h"

ieee80211_tx_result
ieee80211_tx_h_michael_mic_add(struct ieee80211_tx_data *tx)
{
	u8 *data, *key, *mic;
	size_t data_len;
	unsigned int hdrlen;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb = tx->skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int tail;

	hdr = (struct ieee80211_hdr *)skb->data;
	if (!tx->key || tx->key->conf.cipher != WLAN_CIPHER_SUITE_TKIP ||
	    skb->len < 24 || !ieee80211_is_data_present(hdr->frame_control))
		return TX_CONTINUE;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	if (skb->len < hdrlen)
		return TX_DROP;

	data = skb->data + hdrlen;
	data_len = skb->len - hdrlen;

	if (unlikely(info->flags & IEEE80211_TX_INTFL_TKIP_MIC_FAILURE)) {
		/* Need to use software crypto for the test */
		info->control.hw_key = NULL;
	}

	if (info->control.hw_key &&
	    (info->flags & IEEE80211_TX_CTL_DONTFRAG ||
	     ieee80211_hw_check(&tx->local->hw, SUPPORTS_TX_FRAG)) &&
	    !(tx->key->conf.flags & (IEEE80211_KEY_FLAG_GENERATE_MMIC |
				     IEEE80211_KEY_FLAG_PUT_MIC_SPACE))) {
		/* hwaccel - with no need for SW-generated MMIC or MIC space */
		return TX_CONTINUE;
	}

	tail = MICHAEL_MIC_LEN;
	if (!info->control.hw_key)
		tail += IEEE80211_TKIP_ICV_LEN;

	if (WARN(skb_tailroom(skb) < tail ||
		 skb_headroom(skb) < IEEE80211_TKIP_IV_LEN,
		 "mmic: not enough head/tail (%d/%d,%d/%d)\n",
		 skb_headroom(skb), IEEE80211_TKIP_IV_LEN,
		 skb_tailroom(skb), tail))
		return TX_DROP;

	mic = skb_put(skb, MICHAEL_MIC_LEN);

	if (tx->key->conf.flags & IEEE80211_KEY_FLAG_PUT_MIC_SPACE) {
		/* Zeroed MIC can help with debug */
		memset(mic, 0, MICHAEL_MIC_LEN);
		return TX_CONTINUE;
	}

	key = &tx->key->conf.key[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY];
	michael_mic(key, hdr, data, data_len, mic);
	if (unlikely(info->flags & IEEE80211_TX_INTFL_TKIP_MIC_FAILURE))
		mic[0]++;

	return TX_CONTINUE;
}


ieee80211_rx_result
ieee80211_rx_h_michael_mic_verify(struct ieee80211_rx_data *rx)
{
	u8 *data, *key = NULL;
	size_t data_len;
	unsigned int hdrlen;
	u8 mic[MICHAEL_MIC_LEN];
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	/*
	 * it makes no sense to check for MIC errors on anything other
	 * than data frames.
	 */
	if (!ieee80211_is_data_present(hdr->frame_control))
		return RX_CONTINUE;

	/*
	 * No way to verify the MIC if the hardware stripped it or
	 * the IV with the key index. In this case we have solely rely
	 * on the driver to set RX_FLAG_MMIC_ERROR in the event of a
	 * MIC failure report.
	 */
	if (status->flag & (RX_FLAG_MMIC_STRIPPED | RX_FLAG_IV_STRIPPED)) {
		if (status->flag & RX_FLAG_MMIC_ERROR)
			goto mic_fail_no_key;

		if (!(status->flag & RX_FLAG_IV_STRIPPED) && rx->key &&
		    rx->key->conf.cipher == WLAN_CIPHER_SUITE_TKIP)
			goto update_iv;

		return RX_CONTINUE;
	}

	/*
	 * Some hardware seems to generate Michael MIC failure reports; even
	 * though, the frame was not encrypted with TKIP and therefore has no
	 * MIC. Ignore the flag them to avoid triggering countermeasures.
	 */
	if (!rx->key || rx->key->conf.cipher != WLAN_CIPHER_SUITE_TKIP ||
	    !(status->flag & RX_FLAG_DECRYPTED))
		return RX_CONTINUE;

	if (rx->sdata->vif.type == NL80211_IFTYPE_AP && rx->key->conf.keyidx) {
		/*
		 * APs with pairwise keys should never receive Michael MIC
		 * errors for non-zero keyidx because these are reserved for
		 * group keys and only the AP is sending real multicast
		 * frames in the BSS.
		 */
		return RX_DROP_UNUSABLE;
	}

	if (status->flag & RX_FLAG_MMIC_ERROR)
		goto mic_fail;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	if (skb->len < hdrlen + MICHAEL_MIC_LEN)
		return RX_DROP_UNUSABLE;

	if (skb_linearize(rx->skb))
		return RX_DROP_UNUSABLE;
	hdr = (void *)skb->data;

	data = skb->data + hdrlen;
	data_len = skb->len - hdrlen - MICHAEL_MIC_LEN;
	key = &rx->key->conf.key[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY];
	michael_mic(key, hdr, data, data_len, mic);
	if (crypto_memneq(mic, data + data_len, MICHAEL_MIC_LEN))
		goto mic_fail;

	/* remove Michael MIC from payload */
	skb_trim(skb, skb->len - MICHAEL_MIC_LEN);

update_iv:
	/* update IV in key information to be able to detect replays */
	rx->key->u.tkip.rx[rx->security_idx].iv32 = rx->tkip.iv32;
	rx->key->u.tkip.rx[rx->security_idx].iv16 = rx->tkip.iv16;

	return RX_CONTINUE;

mic_fail:
	rx->key->u.tkip.mic_failures++;

mic_fail_no_key:
	/*
	 * In some cases the key can be unset - e.g. a multicast packet, in
	 * a driver that supports HW encryption. Send up the key idx only if
	 * the key is set.
	 */
	cfg80211_michael_mic_failure(rx->sdata->dev, hdr->addr2,
				     is_multicast_ether_addr(hdr->addr1) ?
				     NL80211_KEYTYPE_GROUP :
				     NL80211_KEYTYPE_PAIRWISE,
				     rx->key ? rx->key->conf.keyidx : -1,
				     NULL, GFP_ATOMIC);
	return RX_DROP_UNUSABLE;
}

static int tkip_encrypt_skb(struct ieee80211_tx_data *tx, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_key *key = tx->key;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	unsigned int hdrlen;
	int len, tail;
	u64 pn;
	u8 *pos;

	if (info->control.hw_key &&
	    !(info->control.hw_key->flags & IEEE80211_KEY_FLAG_GENERATE_IV) &&
	    !(info->control.hw_key->flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE)) {
		/* hwaccel - with no need for software-generated IV */
		return 0;
	}

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	len = skb->len - hdrlen;

	if (info->control.hw_key)
		tail = 0;
	else
		tail = IEEE80211_TKIP_ICV_LEN;

	if (WARN_ON(skb_tailroom(skb) < tail ||
		    skb_headroom(skb) < IEEE80211_TKIP_IV_LEN))
		return -1;

	pos = skb_push(skb, IEEE80211_TKIP_IV_LEN);
	memmove(pos, pos + IEEE80211_TKIP_IV_LEN, hdrlen);
	pos += hdrlen;

	/* the HW only needs room for the IV, but not the actual IV */
	if (info->control.hw_key &&
	    (info->control.hw_key->flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE))
		return 0;

	/* Increase IV for the frame */
	pn = atomic64_inc_return(&key->conf.tx_pn);
	pos = ieee80211_tkip_add_iv(pos, &key->conf, pn);

	/* hwaccel - with software IV */
	if (info->control.hw_key)
		return 0;

	/* Add room for ICV */
	skb_put(skb, IEEE80211_TKIP_ICV_LEN);

	return ieee80211_tkip_encrypt_data(&tx->local->wep_tx_ctx,
					   key, skb, pos, len);
}


ieee80211_tx_result
ieee80211_crypto_tkip_encrypt(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;

	ieee80211_tx_set_protected(tx);

	skb_queue_walk(&tx->skbs, skb) {
		if (tkip_encrypt_skb(tx, skb) < 0)
			return TX_DROP;
	}

	return TX_CONTINUE;
}


ieee80211_rx_result
ieee80211_crypto_tkip_decrypt(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;
	int hdrlen, res, hwaccel = 0;
	struct ieee80211_key *key = rx->key;
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);

	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	if (!ieee80211_is_data(hdr->frame_control))
		return RX_CONTINUE;

	if (!rx->sta || skb->len - hdrlen < 12)
		return RX_DROP_UNUSABLE;

	/* it may be possible to optimize this a bit more */
	if (skb_linearize(rx->skb))
		return RX_DROP_UNUSABLE;
	hdr = (void *)skb->data;

	/*
	 * Let TKIP code verify IV, but skip decryption.
	 * In the case where hardware checks the IV as well,
	 * we don't even get here, see ieee80211_rx_h_decrypt()
	 */
	if (status->flag & RX_FLAG_DECRYPTED)
		hwaccel = 1;

	res = ieee80211_tkip_decrypt_data(&rx->local->wep_rx_ctx,
					  key, skb->data + hdrlen,
					  skb->len - hdrlen, rx->sta->sta.addr,
					  hdr->addr1, hwaccel, rx->security_idx,
					  &rx->tkip.iv32,
					  &rx->tkip.iv16);
	if (res != TKIP_DECRYPT_OK)
		return RX_DROP_UNUSABLE;

	/* Trim ICV */
	if (!(status->flag & RX_FLAG_ICV_STRIPPED))
		skb_trim(skb, skb->len - IEEE80211_TKIP_ICV_LEN);

	/* Remove IV */
	memmove(skb->data + IEEE80211_TKIP_IV_LEN, skb->data, hdrlen);
	skb_pull(skb, IEEE80211_TKIP_IV_LEN);

	return RX_CONTINUE;
}

/*
 * Calculate AAD for CCMP/GCMP, returning qos_tid since we
 * need that in CCMP also for b_0.
 */
static u8 ccmp_gcmp_aad(struct sk_buff *skb, u8 *aad)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 mask_fc;
	int a4_included, mgmt;
	u8 qos_tid;
	u16 len_a = 22;

	/*
	 * Mask FC: zero subtype b4 b5 b6 (if not mgmt)
	 * Retry, PwrMgt, MoreData, Order (if Qos Data); set Protected
	 */
	mgmt = ieee80211_is_mgmt(hdr->frame_control);
	mask_fc = hdr->frame_control;
	mask_fc &= ~cpu_to_le16(IEEE80211_FCTL_RETRY |
				IEEE80211_FCTL_PM | IEEE80211_FCTL_MOREDATA);
	if (!mgmt)
		mask_fc &= ~cpu_to_le16(0x0070);
	mask_fc |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);

	a4_included = ieee80211_has_a4(hdr->frame_control);
	if (a4_included)
		len_a += 6;

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		qos_tid = ieee80211_get_tid(hdr);
		mask_fc &= ~cpu_to_le16(IEEE80211_FCTL_ORDER);
		len_a += 2;
	} else {
		qos_tid = 0;
	}

	/* AAD (extra authenticate-only data) / masked 802.11 header
	 * FC | A1 | A2 | A3 | SC | [A4] | [QC] */
	put_unaligned_be16(len_a, &aad[0]);
	put_unaligned(mask_fc, (__le16 *)&aad[2]);
	memcpy(&aad[4], &hdr->addr1, 3 * ETH_ALEN);

	/* Mask Seq#, leave Frag# */
	aad[22] = *((u8 *) &hdr->seq_ctrl) & 0x0f;
	aad[23] = 0;

	if (a4_included) {
		memcpy(&aad[24], hdr->addr4, ETH_ALEN);
		aad[30] = qos_tid;
		aad[31] = 0;
	} else {
		memset(&aad[24], 0, ETH_ALEN + IEEE80211_QOS_CTL_LEN);
		aad[24] = qos_tid;
	}

	return qos_tid;
}

static void ccmp_special_blocks(struct sk_buff *skb, u8 *pn, u8 *b_0, u8 *aad)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	u8 qos_tid = ccmp_gcmp_aad(skb, aad);

	/* In CCM, the initial vectors (IV) used for CTR mode encryption and CBC
	 * mode authentication are not allowed to collide, yet both are derived
	 * from this vector b_0. We only set L := 1 here to indicate that the
	 * data size can be represented in (L+1) bytes. The CCM layer will take
	 * care of storing the data length in the top (L+1) bytes and setting
	 * and clearing the other bits as is required to derive the two IVs.
	 */
	b_0[0] = 0x1;

	/* Nonce: Nonce Flags | A2 | PN
	 * Nonce Flags: Priority (b0..b3) | Management (b4) | Reserved (b5..b7)
	 */
	b_0[1] = qos_tid | (ieee80211_is_mgmt(hdr->frame_control) << 4);
	memcpy(&b_0[2], hdr->addr2, ETH_ALEN);
	memcpy(&b_0[8], pn, IEEE80211_CCMP_PN_LEN);
}

static inline void ccmp_pn2hdr(u8 *hdr, u8 *pn, int key_id)
{
	hdr[0] = pn[5];
	hdr[1] = pn[4];
	hdr[2] = 0;
	hdr[3] = 0x20 | (key_id << 6);
	hdr[4] = pn[3];
	hdr[5] = pn[2];
	hdr[6] = pn[1];
	hdr[7] = pn[0];
}


static inline void ccmp_hdr2pn(u8 *pn, u8 *hdr)
{
	pn[0] = hdr[7];
	pn[1] = hdr[6];
	pn[2] = hdr[5];
	pn[3] = hdr[4];
	pn[4] = hdr[1];
	pn[5] = hdr[0];
}


static int ccmp_encrypt_skb(struct ieee80211_tx_data *tx, struct sk_buff *skb,
			    unsigned int mic_len)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_key *key = tx->key;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int hdrlen, len, tail;
	u8 *pos;
	u8 pn[6];
	u64 pn64;
	u8 aad[CCM_AAD_LEN];
	u8 b_0[AES_BLOCK_SIZE];

	if (info->control.hw_key &&
	    !(info->control.hw_key->flags & IEEE80211_KEY_FLAG_GENERATE_IV) &&
	    !(info->control.hw_key->flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE) &&
	    !((info->control.hw_key->flags &
	       IEEE80211_KEY_FLAG_GENERATE_IV_MGMT) &&
	      ieee80211_is_mgmt(hdr->frame_control))) {
		/*
		 * hwaccel has no need for preallocated room for CCMP
		 * header or MIC fields
		 */
		return 0;
	}

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	len = skb->len - hdrlen;

	if (info->control.hw_key)
		tail = 0;
	else
		tail = mic_len;

	if (WARN_ON(skb_tailroom(skb) < tail ||
		    skb_headroom(skb) < IEEE80211_CCMP_HDR_LEN))
		return -1;

	pos = skb_push(skb, IEEE80211_CCMP_HDR_LEN);
	memmove(pos, pos + IEEE80211_CCMP_HDR_LEN, hdrlen);

	/* the HW only needs room for the IV, but not the actual IV */
	if (info->control.hw_key &&
	    (info->control.hw_key->flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE))
		return 0;

	pos += hdrlen;

	pn64 = atomic64_inc_return(&key->conf.tx_pn);

	pn[5] = pn64;
	pn[4] = pn64 >> 8;
	pn[3] = pn64 >> 16;
	pn[2] = pn64 >> 24;
	pn[1] = pn64 >> 32;
	pn[0] = pn64 >> 40;

	ccmp_pn2hdr(pos, pn, key->conf.keyidx);

	/* hwaccel - with software CCMP header */
	if (info->control.hw_key)
		return 0;

	pos += IEEE80211_CCMP_HDR_LEN;
	ccmp_special_blocks(skb, pn, b_0, aad);
	return ieee80211_aes_ccm_encrypt(key->u.ccmp.tfm, b_0, aad, pos, len,
					 skb_put(skb, mic_len));
}


ieee80211_tx_result
ieee80211_crypto_ccmp_encrypt(struct ieee80211_tx_data *tx,
			      unsigned int mic_len)
{
	struct sk_buff *skb;

	ieee80211_tx_set_protected(tx);

	skb_queue_walk(&tx->skbs, skb) {
		if (ccmp_encrypt_skb(tx, skb, mic_len) < 0)
			return TX_DROP;
	}

	return TX_CONTINUE;
}


ieee80211_rx_result
ieee80211_crypto_ccmp_decrypt(struct ieee80211_rx_data *rx,
			      unsigned int mic_len)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;
	int hdrlen;
	struct ieee80211_key *key = rx->key;
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	u8 pn[IEEE80211_CCMP_PN_LEN];
	int data_len;
	int queue;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	if (!ieee80211_is_data(hdr->frame_control) &&
	    !ieee80211_is_robust_mgmt_frame(skb))
		return RX_CONTINUE;

	if (status->flag & RX_FLAG_DECRYPTED) {
		if (!pskb_may_pull(rx->skb, hdrlen + IEEE80211_CCMP_HDR_LEN))
			return RX_DROP_UNUSABLE;
		if (status->flag & RX_FLAG_MIC_STRIPPED)
			mic_len = 0;
	} else {
		if (skb_linearize(rx->skb))
			return RX_DROP_UNUSABLE;
	}

	/* reload hdr - skb might have been reallocated */
	hdr = (void *)rx->skb->data;

	data_len = skb->len - hdrlen - IEEE80211_CCMP_HDR_LEN - mic_len;
	if (!rx->sta || data_len < 0)
		return RX_DROP_UNUSABLE;

	if (!(status->flag & RX_FLAG_PN_VALIDATED)) {
		int res;

		ccmp_hdr2pn(pn, skb->data + hdrlen);

		queue = rx->security_idx;

		res = memcmp(pn, key->u.ccmp.rx_pn[queue],
			     IEEE80211_CCMP_PN_LEN);
		if (res < 0 ||
		    (!res && !(status->flag & RX_FLAG_ALLOW_SAME_PN))) {
			key->u.ccmp.replays++;
			return RX_DROP_UNUSABLE;
		}

		if (!(status->flag & RX_FLAG_DECRYPTED)) {
			u8 aad[2 * AES_BLOCK_SIZE];
			u8 b_0[AES_BLOCK_SIZE];
			/* hardware didn't decrypt/verify MIC */
			ccmp_special_blocks(skb, pn, b_0, aad);

			if (ieee80211_aes_ccm_decrypt(
				    key->u.ccmp.tfm, b_0, aad,
				    skb->data + hdrlen + IEEE80211_CCMP_HDR_LEN,
				    data_len,
				    skb->data + skb->len - mic_len))
				return RX_DROP_UNUSABLE;
		}

		memcpy(key->u.ccmp.rx_pn[queue], pn, IEEE80211_CCMP_PN_LEN);
		if (unlikely(ieee80211_is_frag(hdr)))
			memcpy(rx->ccm_gcm.pn, pn, IEEE80211_CCMP_PN_LEN);
	}

	/* Remove CCMP header and MIC */
	if (pskb_trim(skb, skb->len - mic_len))
		return RX_DROP_UNUSABLE;
	memmove(skb->data + IEEE80211_CCMP_HDR_LEN, skb->data, hdrlen);
	skb_pull(skb, IEEE80211_CCMP_HDR_LEN);

	return RX_CONTINUE;
}

static void gcmp_special_blocks(struct sk_buff *skb, u8 *pn, u8 *j_0, u8 *aad)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;

	memcpy(j_0, hdr->addr2, ETH_ALEN);
	memcpy(&j_0[ETH_ALEN], pn, IEEE80211_GCMP_PN_LEN);
	j_0[13] = 0;
	j_0[14] = 0;
	j_0[AES_BLOCK_SIZE - 1] = 0x01;

	ccmp_gcmp_aad(skb, aad);
}

static inline void gcmp_pn2hdr(u8 *hdr, const u8 *pn, int key_id)
{
	hdr[0] = pn[5];
	hdr[1] = pn[4];
	hdr[2] = 0;
	hdr[3] = 0x20 | (key_id << 6);
	hdr[4] = pn[3];
	hdr[5] = pn[2];
	hdr[6] = pn[1];
	hdr[7] = pn[0];
}

static inline void gcmp_hdr2pn(u8 *pn, const u8 *hdr)
{
	pn[0] = hdr[7];
	pn[1] = hdr[6];
	pn[2] = hdr[5];
	pn[3] = hdr[4];
	pn[4] = hdr[1];
	pn[5] = hdr[0];
}

static int gcmp_encrypt_skb(struct ieee80211_tx_data *tx, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_key *key = tx->key;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int hdrlen, len, tail;
	u8 *pos;
	u8 pn[6];
	u64 pn64;
	u8 aad[GCM_AAD_LEN];
	u8 j_0[AES_BLOCK_SIZE];

	if (info->control.hw_key &&
	    !(info->control.hw_key->flags & IEEE80211_KEY_FLAG_GENERATE_IV) &&
	    !(info->control.hw_key->flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE) &&
	    !((info->control.hw_key->flags &
	       IEEE80211_KEY_FLAG_GENERATE_IV_MGMT) &&
	      ieee80211_is_mgmt(hdr->frame_control))) {
		/* hwaccel has no need for preallocated room for GCMP
		 * header or MIC fields
		 */
		return 0;
	}

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	len = skb->len - hdrlen;

	if (info->control.hw_key)
		tail = 0;
	else
		tail = IEEE80211_GCMP_MIC_LEN;

	if (WARN_ON(skb_tailroom(skb) < tail ||
		    skb_headroom(skb) < IEEE80211_GCMP_HDR_LEN))
		return -1;

	pos = skb_push(skb, IEEE80211_GCMP_HDR_LEN);
	memmove(pos, pos + IEEE80211_GCMP_HDR_LEN, hdrlen);
	skb_set_network_header(skb, skb_network_offset(skb) +
				    IEEE80211_GCMP_HDR_LEN);

	/* the HW only needs room for the IV, but not the actual IV */
	if (info->control.hw_key &&
	    (info->control.hw_key->flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE))
		return 0;

	pos += hdrlen;

	pn64 = atomic64_inc_return(&key->conf.tx_pn);

	pn[5] = pn64;
	pn[4] = pn64 >> 8;
	pn[3] = pn64 >> 16;
	pn[2] = pn64 >> 24;
	pn[1] = pn64 >> 32;
	pn[0] = pn64 >> 40;

	gcmp_pn2hdr(pos, pn, key->conf.keyidx);

	/* hwaccel - with software GCMP header */
	if (info->control.hw_key)
		return 0;

	pos += IEEE80211_GCMP_HDR_LEN;
	gcmp_special_blocks(skb, pn, j_0, aad);
	return ieee80211_aes_gcm_encrypt(key->u.gcmp.tfm, j_0, aad, pos, len,
					 skb_put(skb, IEEE80211_GCMP_MIC_LEN));
}

ieee80211_tx_result
ieee80211_crypto_gcmp_encrypt(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;

	ieee80211_tx_set_protected(tx);

	skb_queue_walk(&tx->skbs, skb) {
		if (gcmp_encrypt_skb(tx, skb) < 0)
			return TX_DROP;
	}

	return TX_CONTINUE;
}

ieee80211_rx_result
ieee80211_crypto_gcmp_decrypt(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;
	int hdrlen;
	struct ieee80211_key *key = rx->key;
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	u8 pn[IEEE80211_GCMP_PN_LEN];
	int data_len, queue, mic_len = IEEE80211_GCMP_MIC_LEN;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	if (!ieee80211_is_data(hdr->frame_control) &&
	    !ieee80211_is_robust_mgmt_frame(skb))
		return RX_CONTINUE;

	if (status->flag & RX_FLAG_DECRYPTED) {
		if (!pskb_may_pull(rx->skb, hdrlen + IEEE80211_GCMP_HDR_LEN))
			return RX_DROP_UNUSABLE;
		if (status->flag & RX_FLAG_MIC_STRIPPED)
			mic_len = 0;
	} else {
		if (skb_linearize(rx->skb))
			return RX_DROP_UNUSABLE;
	}

	/* reload hdr - skb might have been reallocated */
	hdr = (void *)rx->skb->data;

	data_len = skb->len - hdrlen - IEEE80211_GCMP_HDR_LEN - mic_len;
	if (!rx->sta || data_len < 0)
		return RX_DROP_UNUSABLE;

	if (!(status->flag & RX_FLAG_PN_VALIDATED)) {
		int res;

		gcmp_hdr2pn(pn, skb->data + hdrlen);

		queue = rx->security_idx;

		res = memcmp(pn, key->u.gcmp.rx_pn[queue],
			     IEEE80211_GCMP_PN_LEN);
		if (res < 0 ||
		    (!res && !(status->flag & RX_FLAG_ALLOW_SAME_PN))) {
			key->u.gcmp.replays++;
			return RX_DROP_UNUSABLE;
		}

		if (!(status->flag & RX_FLAG_DECRYPTED)) {
			u8 aad[2 * AES_BLOCK_SIZE];
			u8 j_0[AES_BLOCK_SIZE];
			/* hardware didn't decrypt/verify MIC */
			gcmp_special_blocks(skb, pn, j_0, aad);

			if (ieee80211_aes_gcm_decrypt(
				    key->u.gcmp.tfm, j_0, aad,
				    skb->data + hdrlen + IEEE80211_GCMP_HDR_LEN,
				    data_len,
				    skb->data + skb->len -
				    IEEE80211_GCMP_MIC_LEN))
				return RX_DROP_UNUSABLE;
		}

		memcpy(key->u.gcmp.rx_pn[queue], pn, IEEE80211_GCMP_PN_LEN);
		if (unlikely(ieee80211_is_frag(hdr)))
			memcpy(rx->ccm_gcm.pn, pn, IEEE80211_CCMP_PN_LEN);
	}

	/* Remove GCMP header and MIC */
	if (pskb_trim(skb, skb->len - mic_len))
		return RX_DROP_UNUSABLE;
	memmove(skb->data + IEEE80211_GCMP_HDR_LEN, skb->data, hdrlen);
	skb_pull(skb, IEEE80211_GCMP_HDR_LEN);

	return RX_CONTINUE;
}

static ieee80211_tx_result
ieee80211_crypto_cs_encrypt(struct ieee80211_tx_data *tx,
			    struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_key *key = tx->key;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int hdrlen;
	u8 *pos, iv_len = key->conf.iv_len;

	if (info->control.hw_key &&
	    !(info->control.hw_key->flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE)) {
		/* hwaccel has no need for preallocated head room */
		return TX_CONTINUE;
	}

	if (unlikely(skb_headroom(skb) < iv_len &&
		     pskb_expand_head(skb, iv_len, 0, GFP_ATOMIC)))
		return TX_DROP;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	pos = skb_push(skb, iv_len);
	memmove(pos, pos + iv_len, hdrlen);

	return TX_CONTINUE;
}

static inline int ieee80211_crypto_cs_pn_compare(u8 *pn1, u8 *pn2, int len)
{
	int i;

	/* pn is little endian */
	for (i = len - 1; i >= 0; i--) {
		if (pn1[i] < pn2[i])
			return -1;
		else if (pn1[i] > pn2[i])
			return 1;
	}

	return 0;
}

static ieee80211_rx_result
ieee80211_crypto_cs_decrypt(struct ieee80211_rx_data *rx)
{
	struct ieee80211_key *key = rx->key;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;
	const struct ieee80211_cipher_scheme *cs = NULL;
	int hdrlen = ieee80211_hdrlen(hdr->frame_control);
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(rx->skb);
	int data_len;
	u8 *rx_pn;
	u8 *skb_pn;
	u8 qos_tid;

	if (!rx->sta || !rx->sta->cipher_scheme ||
	    !(status->flag & RX_FLAG_DECRYPTED))
		return RX_DROP_UNUSABLE;

	if (!ieee80211_is_data(hdr->frame_control))
		return RX_CONTINUE;

	cs = rx->sta->cipher_scheme;

	data_len = rx->skb->len - hdrlen - cs->hdr_len;

	if (data_len < 0)
		return RX_DROP_UNUSABLE;

	if (ieee80211_is_data_qos(hdr->frame_control))
		qos_tid = ieee80211_get_tid(hdr);
	else
		qos_tid = 0;

	if (skb_linearize(rx->skb))
		return RX_DROP_UNUSABLE;

	rx_pn = key->u.gen.rx_pn[qos_tid];
	skb_pn = rx->skb->data + hdrlen + cs->pn_off;

	if (ieee80211_crypto_cs_pn_compare(skb_pn, rx_pn, cs->pn_len) <= 0)
		return RX_DROP_UNUSABLE;

	memcpy(rx_pn, skb_pn, cs->pn_len);

	/* remove security header and MIC */
	if (pskb_trim(rx->skb, rx->skb->len - cs->mic_len))
		return RX_DROP_UNUSABLE;

	memmove(rx->skb->data + cs->hdr_len, rx->skb->data, hdrlen);
	skb_pull(rx->skb, cs->hdr_len);

	return RX_CONTINUE;
}

static void bip_aad(struct sk_buff *skb, u8 *aad)
{
	__le16 mask_fc;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	/* BIP AAD: FC(masked) || A1 || A2 || A3 */

	/* FC type/subtype */
	/* Mask FC Retry, PwrMgt, MoreData flags to zero */
	mask_fc = hdr->frame_control;
	mask_fc &= ~cpu_to_le16(IEEE80211_FCTL_RETRY | IEEE80211_FCTL_PM |
				IEEE80211_FCTL_MOREDATA);
	put_unaligned(mask_fc, (__le16 *) &aad[0]);
	/* A1 || A2 || A3 */
	memcpy(aad + 2, &hdr->addr1, 3 * ETH_ALEN);
}


static inline void bip_ipn_set64(u8 *d, u64 pn)
{
	*d++ = pn;
	*d++ = pn >> 8;
	*d++ = pn >> 16;
	*d++ = pn >> 24;
	*d++ = pn >> 32;
	*d = pn >> 40;
}

static inline void bip_ipn_swap(u8 *d, const u8 *s)
{
	*d++ = s[5];
	*d++ = s[4];
	*d++ = s[3];
	*d++ = s[2];
	*d++ = s[1];
	*d = s[0];
}


ieee80211_tx_result
ieee80211_crypto_aes_cmac_encrypt(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	struct ieee80211_key *key = tx->key;
	struct ieee80211_mmie *mmie;
	u8 aad[20];
	u64 pn64;

	if (WARN_ON(skb_queue_len(&tx->skbs) != 1))
		return TX_DROP;

	skb = skb_peek(&tx->skbs);

	info = IEEE80211_SKB_CB(skb);

	if (info->control.hw_key &&
	    !(key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_MMIE))
		return TX_CONTINUE;

	if (WARN_ON(skb_tailroom(skb) < sizeof(*mmie)))
		return TX_DROP;

	mmie = skb_put(skb, sizeof(*mmie));
	mmie->element_id = WLAN_EID_MMIE;
	mmie->length = sizeof(*mmie) - 2;
	mmie->key_id = cpu_to_le16(key->conf.keyidx);

	/* PN = PN + 1 */
	pn64 = atomic64_inc_return(&key->conf.tx_pn);

	bip_ipn_set64(mmie->sequence_number, pn64);

	if (info->control.hw_key)
		return TX_CONTINUE;

	bip_aad(skb, aad);

	/*
	 * MIC = AES-128-CMAC(IGTK, AAD || Management Frame Body || MMIE, 64)
	 */
	ieee80211_aes_cmac(key->u.aes_cmac.tfm, aad,
			   skb->data + 24, skb->len - 24, mmie->mic);

	return TX_CONTINUE;
}

ieee80211_tx_result
ieee80211_crypto_aes_cmac_256_encrypt(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	struct ieee80211_key *key = tx->key;
	struct ieee80211_mmie_16 *mmie;
	u8 aad[20];
	u64 pn64;

	if (WARN_ON(skb_queue_len(&tx->skbs) != 1))
		return TX_DROP;

	skb = skb_peek(&tx->skbs);

	info = IEEE80211_SKB_CB(skb);

	if (info->control.hw_key)
		return TX_CONTINUE;

	if (WARN_ON(skb_tailroom(skb) < sizeof(*mmie)))
		return TX_DROP;

	mmie = skb_put(skb, sizeof(*mmie));
	mmie->element_id = WLAN_EID_MMIE;
	mmie->length = sizeof(*mmie) - 2;
	mmie->key_id = cpu_to_le16(key->conf.keyidx);

	/* PN = PN + 1 */
	pn64 = atomic64_inc_return(&key->conf.tx_pn);

	bip_ipn_set64(mmie->sequence_number, pn64);

	bip_aad(skb, aad);

	/* MIC = AES-256-CMAC(IGTK, AAD || Management Frame Body || MMIE, 128)
	 */
	ieee80211_aes_cmac_256(key->u.aes_cmac.tfm, aad,
			       skb->data + 24, skb->len - 24, mmie->mic);

	return TX_CONTINUE;
}

ieee80211_rx_result
ieee80211_crypto_aes_cmac_decrypt(struct ieee80211_rx_data *rx)
{
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_key *key = rx->key;
	struct ieee80211_mmie *mmie;
	u8 aad[20], mic[8], ipn[6];
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (!ieee80211_is_mgmt(hdr->frame_control))
		return RX_CONTINUE;

	/* management frames are already linear */

	if (skb->len < 24 + sizeof(*mmie))
		return RX_DROP_UNUSABLE;

	mmie = (struct ieee80211_mmie *)
		(skb->data + skb->len - sizeof(*mmie));
	if (mmie->element_id != WLAN_EID_MMIE ||
	    mmie->length != sizeof(*mmie) - 2)
		return RX_DROP_UNUSABLE; /* Invalid MMIE */

	bip_ipn_swap(ipn, mmie->sequence_number);

	if (memcmp(ipn, key->u.aes_cmac.rx_pn, 6) <= 0) {
		key->u.aes_cmac.replays++;
		return RX_DROP_UNUSABLE;
	}

	if (!(status->flag & RX_FLAG_DECRYPTED)) {
		/* hardware didn't decrypt/verify MIC */
		bip_aad(skb, aad);
		ieee80211_aes_cmac(key->u.aes_cmac.tfm, aad,
				   skb->data + 24, skb->len - 24, mic);
		if (crypto_memneq(mic, mmie->mic, sizeof(mmie->mic))) {
			key->u.aes_cmac.icverrors++;
			return RX_DROP_UNUSABLE;
		}
	}

	memcpy(key->u.aes_cmac.rx_pn, ipn, 6);

	/* Remove MMIE */
	skb_trim(skb, skb->len - sizeof(*mmie));

	return RX_CONTINUE;
}

ieee80211_rx_result
ieee80211_crypto_aes_cmac_256_decrypt(struct ieee80211_rx_data *rx)
{
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_key *key = rx->key;
	struct ieee80211_mmie_16 *mmie;
	u8 aad[20], mic[16], ipn[6];
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_is_mgmt(hdr->frame_control))
		return RX_CONTINUE;

	/* management frames are already linear */

	if (skb->len < 24 + sizeof(*mmie))
		return RX_DROP_UNUSABLE;

	mmie = (struct ieee80211_mmie_16 *)
		(skb->data + skb->len - sizeof(*mmie));
	if (mmie->element_id != WLAN_EID_MMIE ||
	    mmie->length != sizeof(*mmie) - 2)
		return RX_DROP_UNUSABLE; /* Invalid MMIE */

	bip_ipn_swap(ipn, mmie->sequence_number);

	if (memcmp(ipn, key->u.aes_cmac.rx_pn, 6) <= 0) {
		key->u.aes_cmac.replays++;
		return RX_DROP_UNUSABLE;
	}

	if (!(status->flag & RX_FLAG_DECRYPTED)) {
		/* hardware didn't decrypt/verify MIC */
		bip_aad(skb, aad);
		ieee80211_aes_cmac_256(key->u.aes_cmac.tfm, aad,
				       skb->data + 24, skb->len - 24, mic);
		if (crypto_memneq(mic, mmie->mic, sizeof(mmie->mic))) {
			key->u.aes_cmac.icverrors++;
			return RX_DROP_UNUSABLE;
		}
	}

	memcpy(key->u.aes_cmac.rx_pn, ipn, 6);

	/* Remove MMIE */
	skb_trim(skb, skb->len - sizeof(*mmie));

	return RX_CONTINUE;
}

ieee80211_tx_result
ieee80211_crypto_aes_gmac_encrypt(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	struct ieee80211_key *key = tx->key;
	struct ieee80211_mmie_16 *mmie;
	struct ieee80211_hdr *hdr;
	u8 aad[GMAC_AAD_LEN];
	u64 pn64;
	u8 nonce[GMAC_NONCE_LEN];

	if (WARN_ON(skb_queue_len(&tx->skbs) != 1))
		return TX_DROP;

	skb = skb_peek(&tx->skbs);

	info = IEEE80211_SKB_CB(skb);

	if (info->control.hw_key)
		return TX_CONTINUE;

	if (WARN_ON(skb_tailroom(skb) < sizeof(*mmie)))
		return TX_DROP;

	mmie = skb_put(skb, sizeof(*mmie));
	mmie->element_id = WLAN_EID_MMIE;
	mmie->length = sizeof(*mmie) - 2;
	mmie->key_id = cpu_to_le16(key->conf.keyidx);

	/* PN = PN + 1 */
	pn64 = atomic64_inc_return(&key->conf.tx_pn);

	bip_ipn_set64(mmie->sequence_number, pn64);

	bip_aad(skb, aad);

	hdr = (struct ieee80211_hdr *)skb->data;
	memcpy(nonce, hdr->addr2, ETH_ALEN);
	bip_ipn_swap(nonce + ETH_ALEN, mmie->sequence_number);

	/* MIC = AES-GMAC(IGTK, AAD || Management Frame Body || MMIE, 128) */
	if (ieee80211_aes_gmac(key->u.aes_gmac.tfm, aad, nonce,
			       skb->data + 24, skb->len - 24, mmie->mic) < 0)
		return TX_DROP;

	return TX_CONTINUE;
}

ieee80211_rx_result
ieee80211_crypto_aes_gmac_decrypt(struct ieee80211_rx_data *rx)
{
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_key *key = rx->key;
	struct ieee80211_mmie_16 *mmie;
	u8 aad[GMAC_AAD_LEN], *mic, ipn[6], nonce[GMAC_NONCE_LEN];
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_is_mgmt(hdr->frame_control))
		return RX_CONTINUE;

	/* management frames are already linear */

	if (skb->len < 24 + sizeof(*mmie))
		return RX_DROP_UNUSABLE;

	mmie = (struct ieee80211_mmie_16 *)
		(skb->data + skb->len - sizeof(*mmie));
	if (mmie->element_id != WLAN_EID_MMIE ||
	    mmie->length != sizeof(*mmie) - 2)
		return RX_DROP_UNUSABLE; /* Invalid MMIE */

	bip_ipn_swap(ipn, mmie->sequence_number);

	if (memcmp(ipn, key->u.aes_gmac.rx_pn, 6) <= 0) {
		key->u.aes_gmac.replays++;
		return RX_DROP_UNUSABLE;
	}

	if (!(status->flag & RX_FLAG_DECRYPTED)) {
		/* hardware didn't decrypt/verify MIC */
		bip_aad(skb, aad);

		memcpy(nonce, hdr->addr2, ETH_ALEN);
		memcpy(nonce + ETH_ALEN, ipn, 6);

		mic = kmalloc(GMAC_MIC_LEN, GFP_ATOMIC);
		if (!mic)
			return RX_DROP_UNUSABLE;
		if (ieee80211_aes_gmac(key->u.aes_gmac.tfm, aad, nonce,
				       skb->data + 24, skb->len - 24,
				       mic) < 0 ||
		    crypto_memneq(mic, mmie->mic, sizeof(mmie->mic))) {
			key->u.aes_gmac.icverrors++;
			kfree(mic);
			return RX_DROP_UNUSABLE;
		}
		kfree(mic);
	}

	memcpy(key->u.aes_gmac.rx_pn, ipn, 6);

	/* Remove MMIE */
	skb_trim(skb, skb->len - sizeof(*mmie));

	return RX_CONTINUE;
}

ieee80211_tx_result
ieee80211_crypto_hw_encrypt(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;
	struct ieee80211_tx_info *info = NULL;
	ieee80211_tx_result res;

	skb_queue_walk(&tx->skbs, skb) {
		info  = IEEE80211_SKB_CB(skb);

		/* handle hw-only algorithm */
		if (!info->control.hw_key)
			return TX_DROP;

		if (tx->key->flags & KEY_FLAG_CIPHER_SCHEME) {
			res = ieee80211_crypto_cs_encrypt(tx, skb);
			if (res != TX_CONTINUE)
				return res;
		}
	}

	ieee80211_tx_set_protected(tx);

	return TX_CONTINUE;
}

ieee80211_rx_result
ieee80211_crypto_hw_decrypt(struct ieee80211_rx_data *rx)
{
	if (rx->sta && rx->sta->cipher_scheme)
		return ieee80211_crypto_cs_decrypt(rx);

	return RX_DROP_UNUSABLE;
}
