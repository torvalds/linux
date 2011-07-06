/*
 * Copyright 2002-2004, Instant802 Networks, Inc.
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include "ieee80211_i.h"
#include "michael.h"
#include "tkip.h"
#include "aes_ccm.h"
#include "aes_cmac.h"
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
	    !(tx->flags & IEEE80211_TX_FRAGMENTED) &&
	    !(tx->key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_MMIC)) {
		/* hwaccel - with no need for SW-generated MMIC */
		return TX_CONTINUE;
	}

	tail = MICHAEL_MIC_LEN;
	if (!info->control.hw_key)
		tail += TKIP_ICV_LEN;

	if (WARN_ON(skb_tailroom(skb) < tail ||
		    skb_headroom(skb) < TKIP_IV_LEN))
		return TX_DROP;

	key = &tx->key->conf.key[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY];
	mic = skb_put(skb, MICHAEL_MIC_LEN);
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
			goto mic_fail;

		if (!(status->flag & RX_FLAG_IV_STRIPPED))
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
		 * frames in the BSS. (
		 */
		return RX_DROP_UNUSABLE;
	}

	if (status->flag & RX_FLAG_MMIC_ERROR)
		goto mic_fail;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	if (skb->len < hdrlen + MICHAEL_MIC_LEN)
		return RX_DROP_UNUSABLE;

	data = skb->data + hdrlen;
	data_len = skb->len - hdrlen - MICHAEL_MIC_LEN;
	key = &rx->key->conf.key[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY];
	michael_mic(key, hdr, data, data_len, mic);
	if (memcmp(mic, data + data_len, MICHAEL_MIC_LEN) != 0)
		goto mic_fail;

	/* remove Michael MIC from payload */
	skb_trim(skb, skb->len - MICHAEL_MIC_LEN);

update_iv:
	/* update IV in key information to be able to detect replays */
	rx->key->u.tkip.rx[rx->queue].iv32 = rx->tkip_iv32;
	rx->key->u.tkip.rx[rx->queue].iv16 = rx->tkip_iv16;

	return RX_CONTINUE;

mic_fail:
	/*
	 * In some cases the key can be unset - e.g. a multicast packet, in
	 * a driver that supports HW encryption. Send up the key idx only if
	 * the key is set.
	 */
	mac80211_ev_michael_mic_failure(rx->sdata,
					rx->key ? rx->key->conf.keyidx : -1,
					(void *) skb->data, NULL, GFP_ATOMIC);
	return RX_DROP_UNUSABLE;
}


static int tkip_encrypt_skb(struct ieee80211_tx_data *tx, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_key *key = tx->key;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	unsigned long flags;
	unsigned int hdrlen;
	int len, tail;
	u8 *pos;

	if (info->control.hw_key &&
	    !(info->control.hw_key->flags & IEEE80211_KEY_FLAG_GENERATE_IV)) {
		/* hwaccel - with no need for software-generated IV */
		return 0;
	}

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	len = skb->len - hdrlen;

	if (info->control.hw_key)
		tail = 0;
	else
		tail = TKIP_ICV_LEN;

	if (WARN_ON(skb_tailroom(skb) < tail ||
		    skb_headroom(skb) < TKIP_IV_LEN))
		return -1;

	pos = skb_push(skb, TKIP_IV_LEN);
	memmove(pos, pos + TKIP_IV_LEN, hdrlen);
	pos += hdrlen;

	/* Increase IV for the frame */
	spin_lock_irqsave(&key->u.tkip.txlock, flags);
	key->u.tkip.tx.iv16++;
	if (key->u.tkip.tx.iv16 == 0)
		key->u.tkip.tx.iv32++;
	pos = ieee80211_tkip_add_iv(pos, key);
	spin_unlock_irqrestore(&key->u.tkip.txlock, flags);

	/* hwaccel - with software IV */
	if (info->control.hw_key)
		return 0;

	/* Add room for ICV */
	skb_put(skb, TKIP_ICV_LEN);

	return ieee80211_tkip_encrypt_data(tx->local->wep_tx_tfm,
					   key, skb, pos, len);
}


ieee80211_tx_result
ieee80211_crypto_tkip_encrypt(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb = tx->skb;

	ieee80211_tx_set_protected(tx);

	do {
		if (tkip_encrypt_skb(tx, skb) < 0)
			return TX_DROP;
	} while ((skb = skb->next));

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

	/*
	 * Let TKIP code verify IV, but skip decryption.
	 * In the case where hardware checks the IV as well,
	 * we don't even get here, see ieee80211_rx_h_decrypt()
	 */
	if (status->flag & RX_FLAG_DECRYPTED)
		hwaccel = 1;

	res = ieee80211_tkip_decrypt_data(rx->local->wep_rx_tfm,
					  key, skb->data + hdrlen,
					  skb->len - hdrlen, rx->sta->sta.addr,
					  hdr->addr1, hwaccel, rx->queue,
					  &rx->tkip_iv32,
					  &rx->tkip_iv16);
	if (res != TKIP_DECRYPT_OK)
		return RX_DROP_UNUSABLE;

	/* Trim ICV */
	skb_trim(skb, skb->len - TKIP_ICV_LEN);

	/* Remove IV */
	memmove(skb->data + TKIP_IV_LEN, skb->data, hdrlen);
	skb_pull(skb, TKIP_IV_LEN);

	return RX_CONTINUE;
}


static void ccmp_special_blocks(struct sk_buff *skb, u8 *pn, u8 *scratch,
				int encrypted)
{
	__le16 mask_fc;
	int a4_included, mgmt;
	u8 qos_tid;
	u8 *b_0, *aad;
	u16 data_len, len_a;
	unsigned int hdrlen;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	memset(scratch, 0, 6 * AES_BLOCK_LEN);

	b_0 = scratch + 3 * AES_BLOCK_LEN;
	aad = scratch + 4 * AES_BLOCK_LEN;

	/*
	 * Mask FC: zero subtype b4 b5 b6 (if not mgmt)
	 * Retry, PwrMgt, MoreData; set Protected
	 */
	mgmt = ieee80211_is_mgmt(hdr->frame_control);
	mask_fc = hdr->frame_control;
	mask_fc &= ~cpu_to_le16(IEEE80211_FCTL_RETRY |
				IEEE80211_FCTL_PM | IEEE80211_FCTL_MOREDATA);
	if (!mgmt)
		mask_fc &= ~cpu_to_le16(0x0070);
	mask_fc |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	len_a = hdrlen - 2;
	a4_included = ieee80211_has_a4(hdr->frame_control);

	if (ieee80211_is_data_qos(hdr->frame_control))
		qos_tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
	else
		qos_tid = 0;

	data_len = skb->len - hdrlen - CCMP_HDR_LEN;
	if (encrypted)
		data_len -= CCMP_MIC_LEN;

	/* First block, b_0 */
	b_0[0] = 0x59; /* flags: Adata: 1, M: 011, L: 001 */
	/* Nonce: Nonce Flags | A2 | PN
	 * Nonce Flags: Priority (b0..b3) | Management (b4) | Reserved (b5..b7)
	 */
	b_0[1] = qos_tid | (mgmt << 4);
	memcpy(&b_0[2], hdr->addr2, ETH_ALEN);
	memcpy(&b_0[8], pn, CCMP_PN_LEN);
	/* l(m) */
	put_unaligned_be16(data_len, &b_0[14]);

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


static int ccmp_encrypt_skb(struct ieee80211_tx_data *tx, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_key *key = tx->key;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int hdrlen, len, tail;
	u8 *pos;
	u8 pn[6];
	u64 pn64;
	u8 scratch[6 * AES_BLOCK_LEN];

	if (info->control.hw_key &&
	    !(info->control.hw_key->flags & IEEE80211_KEY_FLAG_GENERATE_IV)) {
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
		tail = CCMP_MIC_LEN;

	if (WARN_ON(skb_tailroom(skb) < tail ||
		    skb_headroom(skb) < CCMP_HDR_LEN))
		return -1;

	pos = skb_push(skb, CCMP_HDR_LEN);
	memmove(pos, pos + CCMP_HDR_LEN, hdrlen);
	hdr = (struct ieee80211_hdr *) pos;
	pos += hdrlen;

	pn64 = atomic64_inc_return(&key->u.ccmp.tx_pn);

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

	pos += CCMP_HDR_LEN;
	ccmp_special_blocks(skb, pn, scratch, 0);
	ieee80211_aes_ccm_encrypt(key->u.ccmp.tfm, scratch, pos, len,
				  pos, skb_put(skb, CCMP_MIC_LEN));

	return 0;
}


ieee80211_tx_result
ieee80211_crypto_ccmp_encrypt(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb = tx->skb;

	ieee80211_tx_set_protected(tx);

	do {
		if (ccmp_encrypt_skb(tx, skb) < 0)
			return TX_DROP;
	} while ((skb = skb->next));

	return TX_CONTINUE;
}


ieee80211_rx_result
ieee80211_crypto_ccmp_decrypt(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;
	int hdrlen;
	struct ieee80211_key *key = rx->key;
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	u8 pn[CCMP_PN_LEN];
	int data_len;
	int queue;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	if (!ieee80211_is_data(hdr->frame_control) &&
	    !ieee80211_is_robust_mgmt_frame(hdr))
		return RX_CONTINUE;

	data_len = skb->len - hdrlen - CCMP_HDR_LEN - CCMP_MIC_LEN;
	if (!rx->sta || data_len < 0)
		return RX_DROP_UNUSABLE;

	ccmp_hdr2pn(pn, skb->data + hdrlen);

	queue = ieee80211_is_mgmt(hdr->frame_control) ?
		NUM_RX_DATA_QUEUES : rx->queue;

	if (memcmp(pn, key->u.ccmp.rx_pn[queue], CCMP_PN_LEN) <= 0) {
		key->u.ccmp.replays++;
		return RX_DROP_UNUSABLE;
	}

	if (!(status->flag & RX_FLAG_DECRYPTED)) {
		u8 scratch[6 * AES_BLOCK_LEN];
		/* hardware didn't decrypt/verify MIC */
		ccmp_special_blocks(skb, pn, scratch, 1);

		if (ieee80211_aes_ccm_decrypt(
			    key->u.ccmp.tfm, scratch,
			    skb->data + hdrlen + CCMP_HDR_LEN, data_len,
			    skb->data + skb->len - CCMP_MIC_LEN,
			    skb->data + hdrlen + CCMP_HDR_LEN))
			return RX_DROP_UNUSABLE;
	}

	memcpy(key->u.ccmp.rx_pn[queue], pn, CCMP_PN_LEN);

	/* Remove CCMP header and MIC */
	skb_trim(skb, skb->len - CCMP_MIC_LEN);
	memmove(skb->data + CCMP_HDR_LEN, skb->data, hdrlen);
	skb_pull(skb, CCMP_HDR_LEN);

	return RX_CONTINUE;
}


static void bip_aad(struct sk_buff *skb, u8 *aad)
{
	/* BIP AAD: FC(masked) || A1 || A2 || A3 */

	/* FC type/subtype */
	aad[0] = skb->data[0];
	/* Mask FC Retry, PwrMgt, MoreData flags to zero */
	aad[1] = skb->data[1] & ~(BIT(4) | BIT(5) | BIT(6));
	/* A1 || A2 || A3 */
	memcpy(aad + 2, skb->data + 4, 3 * ETH_ALEN);
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
	struct sk_buff *skb = tx->skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_key *key = tx->key;
	struct ieee80211_mmie *mmie;
	u8 *pn, aad[20];
	int i;

	if (info->control.hw_key)
		return 0;

	if (WARN_ON(skb_tailroom(skb) < sizeof(*mmie)))
		return TX_DROP;

	mmie = (struct ieee80211_mmie *) skb_put(skb, sizeof(*mmie));
	mmie->element_id = WLAN_EID_MMIE;
	mmie->length = sizeof(*mmie) - 2;
	mmie->key_id = cpu_to_le16(key->conf.keyidx);

	/* PN = PN + 1 */
	pn = key->u.aes_cmac.tx_pn;

	for (i = sizeof(key->u.aes_cmac.tx_pn) - 1; i >= 0; i--) {
		pn[i]++;
		if (pn[i])
			break;
	}
	bip_ipn_swap(mmie->sequence_number, pn);

	bip_aad(skb, aad);

	/*
	 * MIC = AES-128-CMAC(IGTK, AAD || Management Frame Body || MMIE, 64)
	 */
	ieee80211_aes_cmac(key->u.aes_cmac.tfm, key->u.aes_cmac.tx_crypto_buf,
			   aad, skb->data + 24, skb->len - 24, mmie->mic);

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
		ieee80211_aes_cmac(key->u.aes_cmac.tfm,
				   key->u.aes_cmac.rx_crypto_buf, aad,
				   skb->data + 24, skb->len - 24, mic);
		if (memcmp(mic, mmie->mic, sizeof(mmie->mic)) != 0) {
			key->u.aes_cmac.icverrors++;
			return RX_DROP_UNUSABLE;
		}
	}

	memcpy(key->u.aes_cmac.rx_pn, ipn, 6);

	/* Remove MMIE */
	skb_trim(skb, skb->len - sizeof(*mmie));

	return RX_CONTINUE;
}
