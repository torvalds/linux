/*
 * Copyright 2002-2004, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/compiler.h>
#include <net/mac80211.h>

#include "ieee80211_i.h"
#include "michael.h"
#include "tkip.h"
#include "aes_ccm.h"
#include "wpa.h"

static int ieee80211_get_hdr_info(const struct sk_buff *skb, u8 **sa, u8 **da,
				  u8 *qos_tid, u8 **data, size_t *data_len)
{
	struct ieee80211_hdr *hdr;
	size_t hdrlen;
	u16 fc;
	int a4_included;
	u8 *pos;

	hdr = (struct ieee80211_hdr *) skb->data;
	fc = le16_to_cpu(hdr->frame_control);

	hdrlen = 24;
	if ((fc & (IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS)) ==
	    (IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS)) {
		hdrlen += ETH_ALEN;
		*sa = hdr->addr4;
		*da = hdr->addr3;
	} else if (fc & IEEE80211_FCTL_FROMDS) {
		*sa = hdr->addr3;
		*da = hdr->addr1;
	} else if (fc & IEEE80211_FCTL_TODS) {
		*sa = hdr->addr2;
		*da = hdr->addr3;
	} else {
		*sa = hdr->addr2;
		*da = hdr->addr1;
	}

	if (fc & 0x80)
		hdrlen += 2;

	*data = skb->data + hdrlen;
	*data_len = skb->len - hdrlen;

	a4_included = (fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
		(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS);
	if ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA &&
	    fc & IEEE80211_STYPE_QOS_DATA) {
		pos = (u8 *) &hdr->addr4;
		if (a4_included)
			pos += 6;
		*qos_tid = pos[0] & 0x0f;
		*qos_tid |= 0x80; /* qos_included flag */
	} else
		*qos_tid = 0;

	return skb->len < hdrlen ? -1 : 0;
}


ieee80211_txrx_result
ieee80211_tx_h_michael_mic_add(struct ieee80211_txrx_data *tx)
{
	u8 *data, *sa, *da, *key, *mic, qos_tid;
	size_t data_len;
	u16 fc;
	struct sk_buff *skb = tx->skb;
	int authenticator;
	int wpa_test = 0;

	fc = tx->fc;

	if (!tx->key || tx->key->conf.alg != ALG_TKIP || skb->len < 24 ||
	    !WLAN_FC_DATA_PRESENT(fc))
		return TXRX_CONTINUE;

	if (ieee80211_get_hdr_info(skb, &sa, &da, &qos_tid, &data, &data_len))
		return TXRX_DROP;

	if ((tx->key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE) &&
	    !(tx->flags & IEEE80211_TXRXD_FRAGMENTED) &&
	    !(tx->key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_MMIC) &&
	    !wpa_test) {
		/* hwaccel - with no need for preallocated room for Michael MIC
		 */
		return TXRX_CONTINUE;
	}

	if (skb_tailroom(skb) < MICHAEL_MIC_LEN) {
		I802_DEBUG_INC(tx->local->tx_expand_skb_head);
		if (unlikely(pskb_expand_head(skb, TKIP_IV_LEN,
					      MICHAEL_MIC_LEN + TKIP_ICV_LEN,
					      GFP_ATOMIC))) {
			printk(KERN_DEBUG "%s: failed to allocate more memory "
			       "for Michael MIC\n", tx->dev->name);
			return TXRX_DROP;
		}
	}

#if 0
	authenticator = fc & IEEE80211_FCTL_FROMDS; /* FIX */
#else
	authenticator = 1;
#endif
	key = &tx->key->conf.key[authenticator ? ALG_TKIP_TEMP_AUTH_TX_MIC_KEY :
				 ALG_TKIP_TEMP_AUTH_RX_MIC_KEY];
	mic = skb_put(skb, MICHAEL_MIC_LEN);
	michael_mic(key, da, sa, qos_tid & 0x0f, data, data_len, mic);

	return TXRX_CONTINUE;
}


ieee80211_txrx_result
ieee80211_rx_h_michael_mic_verify(struct ieee80211_txrx_data *rx)
{
	u8 *data, *sa, *da, *key = NULL, qos_tid;
	size_t data_len;
	u16 fc;
	u8 mic[MICHAEL_MIC_LEN];
	struct sk_buff *skb = rx->skb;
	int authenticator = 1, wpa_test = 0;
	DECLARE_MAC_BUF(mac);

	fc = rx->fc;

	/*
	 * No way to verify the MIC if the hardware stripped it
	 */
	if (rx->u.rx.status->flag & RX_FLAG_MMIC_STRIPPED)
		return TXRX_CONTINUE;

	if (!rx->key || rx->key->conf.alg != ALG_TKIP ||
	    !(rx->fc & IEEE80211_FCTL_PROTECTED) || !WLAN_FC_DATA_PRESENT(fc))
		return TXRX_CONTINUE;

	if (ieee80211_get_hdr_info(skb, &sa, &da, &qos_tid, &data, &data_len)
	    || data_len < MICHAEL_MIC_LEN)
		return TXRX_DROP;

	data_len -= MICHAEL_MIC_LEN;

#if 0
	authenticator = fc & IEEE80211_FCTL_TODS; /* FIX */
#else
	authenticator = 1;
#endif
	key = &rx->key->conf.key[authenticator ? ALG_TKIP_TEMP_AUTH_RX_MIC_KEY :
				 ALG_TKIP_TEMP_AUTH_TX_MIC_KEY];
	michael_mic(key, da, sa, qos_tid & 0x0f, data, data_len, mic);
	if (memcmp(mic, data + data_len, MICHAEL_MIC_LEN) != 0 || wpa_test) {
		if (!(rx->flags & IEEE80211_TXRXD_RXRA_MATCH))
			return TXRX_DROP;

		printk(KERN_DEBUG "%s: invalid Michael MIC in data frame from "
		       "%s\n", rx->dev->name, print_mac(mac, sa));

		mac80211_ev_michael_mic_failure(rx->dev, rx->key->conf.keyidx,
						(void *) skb->data);
		return TXRX_DROP;
	}

	/* remove Michael MIC from payload */
	skb_trim(skb, skb->len - MICHAEL_MIC_LEN);

	/* update IV in key information to be able to detect replays */
	rx->key->u.tkip.iv32_rx[rx->u.rx.queue] = rx->u.rx.tkip_iv32;
	rx->key->u.tkip.iv16_rx[rx->u.rx.queue] = rx->u.rx.tkip_iv16;

	return TXRX_CONTINUE;
}


static int tkip_encrypt_skb(struct ieee80211_txrx_data *tx,
			    struct sk_buff *skb, int test)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_key *key = tx->key;
	int hdrlen, len, tailneed;
	u16 fc;
	u8 *pos;

	fc = le16_to_cpu(hdr->frame_control);
	hdrlen = ieee80211_get_hdrlen(fc);
	len = skb->len - hdrlen;

	if (tx->key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE)
		tailneed = 0;
	else
		tailneed = TKIP_ICV_LEN;

	if ((skb_headroom(skb) < TKIP_IV_LEN ||
	     skb_tailroom(skb) < tailneed)) {
		I802_DEBUG_INC(tx->local->tx_expand_skb_head);
		if (unlikely(pskb_expand_head(skb, TKIP_IV_LEN, tailneed,
					      GFP_ATOMIC)))
			return -1;
	}

	pos = skb_push(skb, TKIP_IV_LEN);
	memmove(pos, pos + TKIP_IV_LEN, hdrlen);
	pos += hdrlen;

	/* Increase IV for the frame */
	key->u.tkip.iv16++;
	if (key->u.tkip.iv16 == 0)
		key->u.tkip.iv32++;

	if (tx->key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE) {
		hdr = (struct ieee80211_hdr *)skb->data;

		/* hwaccel - with preallocated room for IV */
		ieee80211_tkip_add_iv(pos, key,
				      (u8) (key->u.tkip.iv16 >> 8),
				      (u8) (((key->u.tkip.iv16 >> 8) | 0x20) &
					    0x7f),
				      (u8) key->u.tkip.iv16);

		tx->u.tx.control->key_idx = tx->key->conf.hw_key_idx;
		return 0;
	}

	/* Add room for ICV */
	skb_put(skb, TKIP_ICV_LEN);

	hdr = (struct ieee80211_hdr *) skb->data;
	ieee80211_tkip_encrypt_data(tx->local->wep_tx_tfm,
				    key, pos, len, hdr->addr2);
	return 0;
}


ieee80211_txrx_result
ieee80211_crypto_tkip_encrypt(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;
	u16 fc;
	struct sk_buff *skb = tx->skb;
	int wpa_test = 0, test = 0;

	fc = le16_to_cpu(hdr->frame_control);

	if (!WLAN_FC_DATA_PRESENT(fc))
		return TXRX_CONTINUE;

	tx->u.tx.control->icv_len = TKIP_ICV_LEN;
	tx->u.tx.control->iv_len = TKIP_IV_LEN;
	ieee80211_tx_set_iswep(tx);

	if ((tx->key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE) &&
	    !(tx->key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV) &&
	    !wpa_test) {
		/* hwaccel - with no need for preallocated room for IV/ICV */
		tx->u.tx.control->key_idx = tx->key->conf.hw_key_idx;
		return TXRX_CONTINUE;
	}

	if (tkip_encrypt_skb(tx, skb, test) < 0)
		return TXRX_DROP;

	if (tx->u.tx.extra_frag) {
		int i;
		for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
			if (tkip_encrypt_skb(tx, tx->u.tx.extra_frag[i], test)
			    < 0)
				return TXRX_DROP;
		}
	}

	return TXRX_CONTINUE;
}


ieee80211_txrx_result
ieee80211_crypto_tkip_decrypt(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;
	u16 fc;
	int hdrlen, res, hwaccel = 0, wpa_test = 0;
	struct ieee80211_key *key = rx->key;
	struct sk_buff *skb = rx->skb;
	DECLARE_MAC_BUF(mac);

	fc = le16_to_cpu(hdr->frame_control);
	hdrlen = ieee80211_get_hdrlen(fc);

	if ((rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA)
		return TXRX_CONTINUE;

	if (!rx->sta || skb->len - hdrlen < 12)
		return TXRX_DROP;

	if (rx->u.rx.status->flag & RX_FLAG_DECRYPTED) {
		if (rx->u.rx.status->flag & RX_FLAG_IV_STRIPPED) {
			/*
			 * Hardware took care of all processing, including
			 * replay protection, and stripped the ICV/IV so
			 * we cannot do any checks here.
			 */
			return TXRX_CONTINUE;
		}

		/* let TKIP code verify IV, but skip decryption */
		hwaccel = 1;
	}

	res = ieee80211_tkip_decrypt_data(rx->local->wep_rx_tfm,
					  key, skb->data + hdrlen,
					  skb->len - hdrlen, rx->sta->addr,
					  hwaccel, rx->u.rx.queue,
					  &rx->u.rx.tkip_iv32,
					  &rx->u.rx.tkip_iv16);
	if (res != TKIP_DECRYPT_OK || wpa_test) {
#ifdef CONFIG_MAC80211_DEBUG
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: TKIP decrypt failed for RX "
			       "frame from %s (res=%d)\n", rx->dev->name,
			       print_mac(mac, rx->sta->addr), res);
#endif /* CONFIG_MAC80211_DEBUG */
		return TXRX_DROP;
	}

	/* Trim ICV */
	skb_trim(skb, skb->len - TKIP_ICV_LEN);

	/* Remove IV */
	memmove(skb->data + TKIP_IV_LEN, skb->data, hdrlen);
	skb_pull(skb, TKIP_IV_LEN);

	return TXRX_CONTINUE;
}


static void ccmp_special_blocks(struct sk_buff *skb, u8 *pn, u8 *b_0, u8 *aad,
				int encrypted)
{
	u16 fc;
	int a4_included, qos_included;
	u8 qos_tid, *fc_pos, *data, *sa, *da;
	int len_a;
	size_t data_len;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	fc_pos = (u8 *) &hdr->frame_control;
	fc = fc_pos[0] ^ (fc_pos[1] << 8);
	a4_included = (fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
		(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS);

	ieee80211_get_hdr_info(skb, &sa, &da, &qos_tid, &data, &data_len);
	data_len -= CCMP_HDR_LEN + (encrypted ? CCMP_MIC_LEN : 0);
	if (qos_tid & 0x80) {
		qos_included = 1;
		qos_tid &= 0x0f;
	} else
		qos_included = 0;
	/* First block, b_0 */

	b_0[0] = 0x59; /* flags: Adata: 1, M: 011, L: 001 */
	/* Nonce: QoS Priority | A2 | PN */
	b_0[1] = qos_tid;
	memcpy(&b_0[2], hdr->addr2, 6);
	memcpy(&b_0[8], pn, CCMP_PN_LEN);
	/* l(m) */
	b_0[14] = (data_len >> 8) & 0xff;
	b_0[15] = data_len & 0xff;


	/* AAD (extra authenticate-only data) / masked 802.11 header
	 * FC | A1 | A2 | A3 | SC | [A4] | [QC] */

	len_a = a4_included ? 28 : 22;
	if (qos_included)
		len_a += 2;

	aad[0] = 0; /* (len_a >> 8) & 0xff; */
	aad[1] = len_a & 0xff;
	/* Mask FC: zero subtype b4 b5 b6 */
	aad[2] = fc_pos[0] & ~(BIT(4) | BIT(5) | BIT(6));
	/* Retry, PwrMgt, MoreData; set Protected */
	aad[3] = (fc_pos[1] & ~(BIT(3) | BIT(4) | BIT(5))) | BIT(6);
	memcpy(&aad[4], &hdr->addr1, 18);

	/* Mask Seq#, leave Frag# */
	aad[22] = *((u8 *) &hdr->seq_ctrl) & 0x0f;
	aad[23] = 0;
	if (a4_included) {
		memcpy(&aad[24], hdr->addr4, 6);
		aad[30] = 0;
		aad[31] = 0;
	} else
		memset(&aad[24], 0, 8);
	if (qos_included) {
		u8 *dpos = &aad[a4_included ? 30 : 24];

		/* Mask QoS Control field */
		dpos[0] = qos_tid;
		dpos[1] = 0;
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


static inline int ccmp_hdr2pn(u8 *pn, u8 *hdr)
{
	pn[0] = hdr[7];
	pn[1] = hdr[6];
	pn[2] = hdr[5];
	pn[3] = hdr[4];
	pn[4] = hdr[1];
	pn[5] = hdr[0];
	return (hdr[3] >> 6) & 0x03;
}


static int ccmp_encrypt_skb(struct ieee80211_txrx_data *tx,
			    struct sk_buff *skb, int test)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_key *key = tx->key;
	int hdrlen, len, tailneed;
	u16 fc;
	u8 *pos, *pn, *b_0, *aad, *scratch;
	int i;

	scratch = key->u.ccmp.tx_crypto_buf;
	b_0 = scratch + 3 * AES_BLOCK_LEN;
	aad = scratch + 4 * AES_BLOCK_LEN;

	fc = le16_to_cpu(hdr->frame_control);
	hdrlen = ieee80211_get_hdrlen(fc);
	len = skb->len - hdrlen;

	if (key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE)
		tailneed = 0;
	else
		tailneed = CCMP_MIC_LEN;

	if ((skb_headroom(skb) < CCMP_HDR_LEN ||
	     skb_tailroom(skb) < tailneed)) {
		I802_DEBUG_INC(tx->local->tx_expand_skb_head);
		if (unlikely(pskb_expand_head(skb, CCMP_HDR_LEN, tailneed,
					      GFP_ATOMIC)))
			return -1;
	}

	pos = skb_push(skb, CCMP_HDR_LEN);
	memmove(pos, pos + CCMP_HDR_LEN, hdrlen);
	hdr = (struct ieee80211_hdr *) pos;
	pos += hdrlen;

	/* PN = PN + 1 */
	pn = key->u.ccmp.tx_pn;

	for (i = CCMP_PN_LEN - 1; i >= 0; i--) {
		pn[i]++;
		if (pn[i])
			break;
	}

	ccmp_pn2hdr(pos, pn, key->conf.keyidx);

	if (key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE) {
		/* hwaccel - with preallocated room for CCMP header */
		tx->u.tx.control->key_idx = key->conf.hw_key_idx;
		return 0;
	}

	pos += CCMP_HDR_LEN;
	ccmp_special_blocks(skb, pn, b_0, aad, 0);
	ieee80211_aes_ccm_encrypt(key->u.ccmp.tfm, scratch, b_0, aad, pos, len,
				  pos, skb_put(skb, CCMP_MIC_LEN));

	return 0;
}


ieee80211_txrx_result
ieee80211_crypto_ccmp_encrypt(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;
	u16 fc;
	struct sk_buff *skb = tx->skb;
	int test = 0;

	fc = le16_to_cpu(hdr->frame_control);

	if (!WLAN_FC_DATA_PRESENT(fc))
		return TXRX_CONTINUE;

	tx->u.tx.control->icv_len = CCMP_MIC_LEN;
	tx->u.tx.control->iv_len = CCMP_HDR_LEN;
	ieee80211_tx_set_iswep(tx);

	if ((tx->key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE) &&
	    !(tx->key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV)) {
		/* hwaccel - with no need for preallocated room for CCMP "
		 * header or MIC fields */
		tx->u.tx.control->key_idx = tx->key->conf.hw_key_idx;
		return TXRX_CONTINUE;
	}

	if (ccmp_encrypt_skb(tx, skb, test) < 0)
		return TXRX_DROP;

	if (tx->u.tx.extra_frag) {
		int i;
		for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
			if (ccmp_encrypt_skb(tx, tx->u.tx.extra_frag[i], test)
			    < 0)
				return TXRX_DROP;
		}
	}

	return TXRX_CONTINUE;
}


ieee80211_txrx_result
ieee80211_crypto_ccmp_decrypt(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;
	u16 fc;
	int hdrlen;
	struct ieee80211_key *key = rx->key;
	struct sk_buff *skb = rx->skb;
	u8 pn[CCMP_PN_LEN];
	int data_len;
	DECLARE_MAC_BUF(mac);

	fc = le16_to_cpu(hdr->frame_control);
	hdrlen = ieee80211_get_hdrlen(fc);

	if ((rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA)
		return TXRX_CONTINUE;

	data_len = skb->len - hdrlen - CCMP_HDR_LEN - CCMP_MIC_LEN;
	if (!rx->sta || data_len < 0)
		return TXRX_DROP;

	if ((rx->u.rx.status->flag & RX_FLAG_DECRYPTED) &&
	    (rx->u.rx.status->flag & RX_FLAG_IV_STRIPPED))
		return TXRX_CONTINUE;

	(void) ccmp_hdr2pn(pn, skb->data + hdrlen);

	if (memcmp(pn, key->u.ccmp.rx_pn[rx->u.rx.queue], CCMP_PN_LEN) <= 0) {
#ifdef CONFIG_MAC80211_DEBUG
		u8 *ppn = key->u.ccmp.rx_pn[rx->u.rx.queue];

		printk(KERN_DEBUG "%s: CCMP replay detected for RX frame from "
		       "%s (RX PN %02x%02x%02x%02x%02x%02x <= prev. PN "
		       "%02x%02x%02x%02x%02x%02x)\n", rx->dev->name,
		       print_mac(mac, rx->sta->addr),
		       pn[0], pn[1], pn[2], pn[3], pn[4], pn[5],
		       ppn[0], ppn[1], ppn[2], ppn[3], ppn[4], ppn[5]);
#endif /* CONFIG_MAC80211_DEBUG */
		key->u.ccmp.replays++;
		return TXRX_DROP;
	}

	if (!(rx->u.rx.status->flag & RX_FLAG_DECRYPTED)) {
		/* hardware didn't decrypt/verify MIC */
		u8 *scratch, *b_0, *aad;

		scratch = key->u.ccmp.rx_crypto_buf;
		b_0 = scratch + 3 * AES_BLOCK_LEN;
		aad = scratch + 4 * AES_BLOCK_LEN;

		ccmp_special_blocks(skb, pn, b_0, aad, 1);

		if (ieee80211_aes_ccm_decrypt(
			    key->u.ccmp.tfm, scratch, b_0, aad,
			    skb->data + hdrlen + CCMP_HDR_LEN, data_len,
			    skb->data + skb->len - CCMP_MIC_LEN,
			    skb->data + hdrlen + CCMP_HDR_LEN)) {
#ifdef CONFIG_MAC80211_DEBUG
			if (net_ratelimit())
				printk(KERN_DEBUG "%s: CCMP decrypt failed "
				       "for RX frame from %s\n", rx->dev->name,
				       print_mac(mac, rx->sta->addr));
#endif /* CONFIG_MAC80211_DEBUG */
			return TXRX_DROP;
		}
	}

	memcpy(key->u.ccmp.rx_pn[rx->u.rx.queue], pn, CCMP_PN_LEN);

	/* Remove CCMP header and MIC */
	skb_trim(skb, skb->len - CCMP_MIC_LEN);
	memmove(skb->data + CCMP_HDR_LEN, skb->data, hdrlen);
	skb_pull(skb, CCMP_HDR_LEN);

	return TXRX_CONTINUE;
}
