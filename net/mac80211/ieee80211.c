/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/rtnetlink.h>
#include <net/iw_handler.h>
#include <linux/compiler.h>
#include <linux/bitmap.h>
#include <net/cfg80211.h>

#include "ieee80211_common.h"
#include "ieee80211_i.h"
#include "ieee80211_rate.h"
#include "wep.h"
#include "wpa.h"
#include "tkip.h"
#include "wme.h"
#include "aes_ccm.h"
#include "ieee80211_led.h"
#include "ieee80211_cfg.h"
#include "debugfs.h"
#include "debugfs_netdev.h"
#include "debugfs_key.h"

/* privid for wiphys to determine whether they belong to us or not */
void *mac80211_wiphy_privid = &mac80211_wiphy_privid;

/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
static const unsigned char rfc1042_header[] =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
static const unsigned char bridge_tunnel_header[] =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };

/* No encapsulation header if EtherType < 0x600 (=length) */
static const unsigned char eapol_header[] =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };


static inline void ieee80211_include_sequence(struct ieee80211_sub_if_data *sdata,
					      struct ieee80211_hdr *hdr)
{
	/* Set the sequence number for this frame. */
	hdr->seq_ctrl = cpu_to_le16(sdata->sequence);

	/* Increase the sequence number. */
	sdata->sequence = (sdata->sequence + 0x10) & IEEE80211_SCTL_SEQ;
}

struct ieee80211_key_conf *
ieee80211_key_data2conf(struct ieee80211_local *local,
			const struct ieee80211_key *data)
{
	struct ieee80211_key_conf *conf;

	conf = kmalloc(sizeof(*conf) + data->keylen, GFP_ATOMIC);
	if (!conf)
		return NULL;

	conf->hw_key_idx = data->hw_key_idx;
	conf->alg = data->alg;
	conf->keylen = data->keylen;
	conf->flags = 0;
	if (data->force_sw_encrypt)
		conf->flags |= IEEE80211_KEY_FORCE_SW_ENCRYPT;
	conf->keyidx = data->keyidx;
	if (data->default_tx_key)
		conf->flags |= IEEE80211_KEY_DEFAULT_TX_KEY;
	if (local->default_wep_only)
		conf->flags |= IEEE80211_KEY_DEFAULT_WEP_ONLY;
	memcpy(conf->key, data->key, data->keylen);

	return conf;
}

struct ieee80211_key *ieee80211_key_alloc(struct ieee80211_sub_if_data *sdata,
					  int idx, size_t key_len, gfp_t flags)
{
	struct ieee80211_key *key;

	key = kzalloc(sizeof(struct ieee80211_key) + key_len, flags);
	if (!key)
		return NULL;
	kref_init(&key->kref);
	return key;
}

static void ieee80211_key_release(struct kref *kref)
{
	struct ieee80211_key *key;

	key = container_of(kref, struct ieee80211_key, kref);
	if (key->alg == ALG_CCMP)
		ieee80211_aes_key_free(key->u.ccmp.tfm);
	ieee80211_debugfs_key_remove(key);
	kfree(key);
}

void ieee80211_key_free(struct ieee80211_key *key)
{
	if (key)
		kref_put(&key->kref, ieee80211_key_release);
}

static int rate_list_match(const int *rate_list, int rate)
{
	int i;

	if (!rate_list)
		return 0;

	for (i = 0; rate_list[i] >= 0; i++)
		if (rate_list[i] == rate)
			return 1;

	return 0;
}


void ieee80211_prepare_rates(struct ieee80211_local *local,
			     struct ieee80211_hw_mode *mode)
{
	int i;

	for (i = 0; i < mode->num_rates; i++) {
		struct ieee80211_rate *rate = &mode->rates[i];

		rate->flags &= ~(IEEE80211_RATE_SUPPORTED |
				 IEEE80211_RATE_BASIC);

		if (local->supp_rates[mode->mode]) {
			if (!rate_list_match(local->supp_rates[mode->mode],
					     rate->rate))
				continue;
		}

		rate->flags |= IEEE80211_RATE_SUPPORTED;

		/* Use configured basic rate set if it is available. If not,
		 * use defaults that are sane for most cases. */
		if (local->basic_rates[mode->mode]) {
			if (rate_list_match(local->basic_rates[mode->mode],
					    rate->rate))
				rate->flags |= IEEE80211_RATE_BASIC;
		} else switch (mode->mode) {
		case MODE_IEEE80211A:
			if (rate->rate == 60 || rate->rate == 120 ||
			    rate->rate == 240)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		case MODE_IEEE80211B:
			if (rate->rate == 10 || rate->rate == 20)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		case MODE_ATHEROS_TURBO:
			if (rate->rate == 120 || rate->rate == 240 ||
			    rate->rate == 480)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		case MODE_IEEE80211G:
			if (rate->rate == 10 || rate->rate == 20 ||
			    rate->rate == 55 || rate->rate == 110)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		}

		/* Set ERP and MANDATORY flags based on phymode */
		switch (mode->mode) {
		case MODE_IEEE80211A:
			if (rate->rate == 60 || rate->rate == 120 ||
			    rate->rate == 240)
				rate->flags |= IEEE80211_RATE_MANDATORY;
			break;
		case MODE_IEEE80211B:
			if (rate->rate == 10)
				rate->flags |= IEEE80211_RATE_MANDATORY;
			break;
		case MODE_ATHEROS_TURBO:
			break;
		case MODE_IEEE80211G:
			if (rate->rate == 10 || rate->rate == 20 ||
			    rate->rate == 55 || rate->rate == 110 ||
			    rate->rate == 60 || rate->rate == 120 ||
			    rate->rate == 240)
				rate->flags |= IEEE80211_RATE_MANDATORY;
			break;
		}
		if (ieee80211_is_erp_rate(mode->mode, rate->rate))
			rate->flags |= IEEE80211_RATE_ERP;
	}
}


static void ieee80211_key_threshold_notify(struct net_device *dev,
					   struct ieee80211_key *key,
					   struct sta_info *sta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_msg_key_notification *msg;

	/* if no one will get it anyway, don't even allocate it.
	 * unlikely because this is only relevant for APs
	 * where the device must be open... */
	if (unlikely(!local->apdev))
		return;

	skb = dev_alloc_skb(sizeof(struct ieee80211_frame_info) +
			    sizeof(struct ieee80211_msg_key_notification));
	if (!skb)
		return;

	skb_reserve(skb, sizeof(struct ieee80211_frame_info));
	msg = (struct ieee80211_msg_key_notification *)
		skb_put(skb, sizeof(struct ieee80211_msg_key_notification));
	msg->tx_rx_count = key->tx_rx_count;
	memcpy(msg->ifname, dev->name, IFNAMSIZ);
	if (sta)
		memcpy(msg->addr, sta->addr, ETH_ALEN);
	else
		memset(msg->addr, 0xff, ETH_ALEN);

	key->tx_rx_count = 0;

	ieee80211_rx_mgmt(local, skb, NULL,
			  ieee80211_msg_key_threshold_notification);
}


static u8 * ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len)
{
	u16 fc;

	if (len < 24)
		return NULL;

	fc = le16_to_cpu(hdr->frame_control);

	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_DATA:
		switch (fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) {
		case IEEE80211_FCTL_TODS:
			return hdr->addr1;
		case (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS):
			return NULL;
		case IEEE80211_FCTL_FROMDS:
			return hdr->addr2;
		case 0:
			return hdr->addr3;
		}
		break;
	case IEEE80211_FTYPE_MGMT:
		return hdr->addr3;
	case IEEE80211_FTYPE_CTL:
		if ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PSPOLL)
			return hdr->addr1;
		else
			return NULL;
	}

	return NULL;
}

int ieee80211_get_hdrlen(u16 fc)
{
	int hdrlen = 24;

	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_DATA:
		if ((fc & IEEE80211_FCTL_FROMDS) && (fc & IEEE80211_FCTL_TODS))
			hdrlen = 30; /* Addr4 */
		/*
		 * The QoS Control field is two bytes and its presence is
		 * indicated by the IEEE80211_STYPE_QOS_DATA bit. Add 2 to
		 * hdrlen if that bit is set.
		 * This works by masking out the bit and shifting it to
		 * bit position 1 so the result has the value 0 or 2.
		 */
		hdrlen += (fc & IEEE80211_STYPE_QOS_DATA)
				>> (ilog2(IEEE80211_STYPE_QOS_DATA)-1);
		break;
	case IEEE80211_FTYPE_CTL:
		/*
		 * ACK and CTS are 10 bytes, all others 16. To see how
		 * to get this condition consider
		 *   subtype mask:   0b0000000011110000 (0x00F0)
		 *   ACK subtype:    0b0000000011010000 (0x00D0)
		 *   CTS subtype:    0b0000000011000000 (0x00C0)
		 *   bits that matter:         ^^^      (0x00E0)
		 *   value of those: 0b0000000011000000 (0x00C0)
		 */
		if ((fc & 0xE0) == 0xC0)
			hdrlen = 10;
		else
			hdrlen = 16;
		break;
	}

	return hdrlen;
}
EXPORT_SYMBOL(ieee80211_get_hdrlen);

int ieee80211_get_hdrlen_from_skb(const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr = (const struct ieee80211_hdr *) skb->data;
	int hdrlen;

	if (unlikely(skb->len < 10))
		return 0;
	hdrlen = ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_control));
	if (unlikely(hdrlen > skb->len))
		return 0;
	return hdrlen;
}
EXPORT_SYMBOL(ieee80211_get_hdrlen_from_skb);

static int ieee80211_get_radiotap_len(struct sk_buff *skb)
{
	struct ieee80211_radiotap_header *hdr =
		(struct ieee80211_radiotap_header *) skb->data;

	return le16_to_cpu(hdr->it_len);
}

#ifdef CONFIG_MAC80211_LOWTX_FRAME_DUMP
static void ieee80211_dump_frame(const char *ifname, const char *title,
				 const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u16 fc;
	int hdrlen;

	printk(KERN_DEBUG "%s: %s (len=%d)", ifname, title, skb->len);
	if (skb->len < 4) {
		printk("\n");
		return;
	}

	fc = le16_to_cpu(hdr->frame_control);
	hdrlen = ieee80211_get_hdrlen(fc);
	if (hdrlen > skb->len)
		hdrlen = skb->len;
	if (hdrlen >= 4)
		printk(" FC=0x%04x DUR=0x%04x",
		       fc, le16_to_cpu(hdr->duration_id));
	if (hdrlen >= 10)
		printk(" A1=" MAC_FMT, MAC_ARG(hdr->addr1));
	if (hdrlen >= 16)
		printk(" A2=" MAC_FMT, MAC_ARG(hdr->addr2));
	if (hdrlen >= 24)
		printk(" A3=" MAC_FMT, MAC_ARG(hdr->addr3));
	if (hdrlen >= 30)
		printk(" A4=" MAC_FMT, MAC_ARG(hdr->addr4));
	printk("\n");
}
#else /* CONFIG_MAC80211_LOWTX_FRAME_DUMP */
static inline void ieee80211_dump_frame(const char *ifname, const char *title,
					struct sk_buff *skb)
{
}
#endif /* CONFIG_MAC80211_LOWTX_FRAME_DUMP */


static int ieee80211_is_eapol(const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr;
	u16 fc;
	int hdrlen;

	if (unlikely(skb->len < 10))
		return 0;

	hdr = (const struct ieee80211_hdr *) skb->data;
	fc = le16_to_cpu(hdr->frame_control);

	if (unlikely(!WLAN_FC_DATA_PRESENT(fc)))
		return 0;

	hdrlen = ieee80211_get_hdrlen(fc);

	if (unlikely(skb->len >= hdrlen + sizeof(eapol_header) &&
		     memcmp(skb->data + hdrlen, eapol_header,
			    sizeof(eapol_header)) == 0))
		return 1;

	return 0;
}


static ieee80211_txrx_result
ieee80211_tx_h_rate_ctrl(struct ieee80211_txrx_data *tx)
{
	struct rate_control_extra extra;

	memset(&extra, 0, sizeof(extra));
	extra.mode = tx->u.tx.mode;
	extra.mgmt_data = tx->sdata &&
		tx->sdata->type == IEEE80211_IF_TYPE_MGMT;
	extra.ethertype = tx->ethertype;

	tx->u.tx.rate = rate_control_get_rate(tx->local, tx->dev, tx->skb,
					      &extra);
	if (unlikely(extra.probe != NULL)) {
		tx->u.tx.control->flags |= IEEE80211_TXCTL_RATE_CTRL_PROBE;
		tx->u.tx.probe_last_frag = 1;
		tx->u.tx.control->alt_retry_rate = tx->u.tx.rate->val;
		tx->u.tx.rate = extra.probe;
	} else {
		tx->u.tx.control->alt_retry_rate = -1;
	}
	if (!tx->u.tx.rate)
		return TXRX_DROP;
	if (tx->u.tx.mode->mode == MODE_IEEE80211G &&
	    tx->local->cts_protect_erp_frames && tx->fragmented &&
	    extra.nonerp) {
		tx->u.tx.last_frag_rate = tx->u.tx.rate;
		tx->u.tx.probe_last_frag = extra.probe ? 1 : 0;

		tx->u.tx.rate = extra.nonerp;
		tx->u.tx.control->rate = extra.nonerp;
		tx->u.tx.control->flags &= ~IEEE80211_TXCTL_RATE_CTRL_PROBE;
	} else {
		tx->u.tx.last_frag_rate = tx->u.tx.rate;
		tx->u.tx.control->rate = tx->u.tx.rate;
	}
	tx->u.tx.control->tx_rate = tx->u.tx.rate->val;
	if ((tx->u.tx.rate->flags & IEEE80211_RATE_PREAMBLE2) &&
	    tx->local->short_preamble &&
	    (!tx->sta || (tx->sta->flags & WLAN_STA_SHORT_PREAMBLE))) {
		tx->u.tx.short_preamble = 1;
		tx->u.tx.control->tx_rate = tx->u.tx.rate->val2;
	}

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_tx_h_select_key(struct ieee80211_txrx_data *tx)
{
	if (tx->sta)
		tx->u.tx.control->key_idx = tx->sta->key_idx_compression;
	else
		tx->u.tx.control->key_idx = HW_KEY_IDX_INVALID;

	if (unlikely(tx->u.tx.control->flags & IEEE80211_TXCTL_DO_NOT_ENCRYPT))
		tx->key = NULL;
	else if (tx->sta && tx->sta->key)
		tx->key = tx->sta->key;
	else if (tx->sdata->default_key)
		tx->key = tx->sdata->default_key;
	else if (tx->sdata->drop_unencrypted &&
		 !(tx->sdata->eapol && ieee80211_is_eapol(tx->skb))) {
		I802_DEBUG_INC(tx->local->tx_handlers_drop_unencrypted);
		return TXRX_DROP;
	} else
		tx->key = NULL;

	if (tx->key) {
		tx->key->tx_rx_count++;
		if (unlikely(tx->local->key_tx_rx_threshold &&
			     tx->key->tx_rx_count >
			     tx->local->key_tx_rx_threshold)) {
			ieee80211_key_threshold_notify(tx->dev, tx->key,
						       tx->sta);
		}
	}

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_tx_h_fragment(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;
	size_t hdrlen, per_fragm, num_fragm, payload_len, left;
	struct sk_buff **frags, *first, *frag;
	int i;
	u16 seq;
	u8 *pos;
	int frag_threshold = tx->local->fragmentation_threshold;

	if (!tx->fragmented)
		return TXRX_CONTINUE;

	first = tx->skb;

	hdrlen = ieee80211_get_hdrlen(tx->fc);
	payload_len = first->len - hdrlen;
	per_fragm = frag_threshold - hdrlen - FCS_LEN;
	num_fragm = (payload_len + per_fragm - 1) / per_fragm;

	frags = kzalloc(num_fragm * sizeof(struct sk_buff *), GFP_ATOMIC);
	if (!frags)
		goto fail;

	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_MOREFRAGS);
	seq = le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ;
	pos = first->data + hdrlen + per_fragm;
	left = payload_len - per_fragm;
	for (i = 0; i < num_fragm - 1; i++) {
		struct ieee80211_hdr *fhdr;
		size_t copylen;

		if (left <= 0)
			goto fail;

		/* reserve enough extra head and tail room for possible
		 * encryption */
		frag = frags[i] =
			dev_alloc_skb(tx->local->hw.extra_tx_headroom +
				      frag_threshold +
				      IEEE80211_ENCRYPT_HEADROOM +
				      IEEE80211_ENCRYPT_TAILROOM);
		if (!frag)
			goto fail;
		/* Make sure that all fragments use the same priority so
		 * that they end up using the same TX queue */
		frag->priority = first->priority;
		skb_reserve(frag, tx->local->hw.extra_tx_headroom +
			IEEE80211_ENCRYPT_HEADROOM);
		fhdr = (struct ieee80211_hdr *) skb_put(frag, hdrlen);
		memcpy(fhdr, first->data, hdrlen);
		if (i == num_fragm - 2)
			fhdr->frame_control &= cpu_to_le16(~IEEE80211_FCTL_MOREFRAGS);
		fhdr->seq_ctrl = cpu_to_le16(seq | ((i + 1) & IEEE80211_SCTL_FRAG));
		copylen = left > per_fragm ? per_fragm : left;
		memcpy(skb_put(frag, copylen), pos, copylen);

		pos += copylen;
		left -= copylen;
	}
	skb_trim(first, hdrlen + per_fragm);

	tx->u.tx.num_extra_frag = num_fragm - 1;
	tx->u.tx.extra_frag = frags;

	return TXRX_CONTINUE;

 fail:
	printk(KERN_DEBUG "%s: failed to fragment frame\n", tx->dev->name);
	if (frags) {
		for (i = 0; i < num_fragm - 1; i++)
			if (frags[i])
				dev_kfree_skb(frags[i]);
		kfree(frags);
	}
	I802_DEBUG_INC(tx->local->tx_handlers_drop_fragment);
	return TXRX_DROP;
}


static int wep_encrypt_skb(struct ieee80211_txrx_data *tx, struct sk_buff *skb)
{
	if (tx->key->force_sw_encrypt) {
		if (ieee80211_wep_encrypt(tx->local, skb, tx->key))
			return -1;
	} else {
		tx->u.tx.control->key_idx = tx->key->hw_key_idx;
		if (tx->local->hw.flags & IEEE80211_HW_WEP_INCLUDE_IV) {
			if (ieee80211_wep_add_iv(tx->local, skb, tx->key) ==
			    NULL)
				return -1;
		}
	}
	return 0;
}


void ieee80211_tx_set_iswep(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;

	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	if (tx->u.tx.extra_frag) {
		struct ieee80211_hdr *fhdr;
		int i;
		for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
			fhdr = (struct ieee80211_hdr *)
				tx->u.tx.extra_frag[i]->data;
			fhdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		}
	}
}


static ieee80211_txrx_result
ieee80211_tx_h_wep_encrypt(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;
	u16 fc;

	fc = le16_to_cpu(hdr->frame_control);

	if (!tx->key || tx->key->alg != ALG_WEP ||
	    ((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA &&
	     ((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_MGMT ||
	      (fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_AUTH)))
		return TXRX_CONTINUE;

	tx->u.tx.control->iv_len = WEP_IV_LEN;
	tx->u.tx.control->icv_len = WEP_ICV_LEN;
	ieee80211_tx_set_iswep(tx);

	if (wep_encrypt_skb(tx, tx->skb) < 0) {
		I802_DEBUG_INC(tx->local->tx_handlers_drop_wep);
		return TXRX_DROP;
	}

	if (tx->u.tx.extra_frag) {
		int i;
		for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
			if (wep_encrypt_skb(tx, tx->u.tx.extra_frag[i]) < 0) {
				I802_DEBUG_INC(tx->local->
					       tx_handlers_drop_wep);
				return TXRX_DROP;
			}
		}
	}

	return TXRX_CONTINUE;
}


static int ieee80211_frame_duration(struct ieee80211_local *local, size_t len,
				    int rate, int erp, int short_preamble)
{
	int dur;

	/* calculate duration (in microseconds, rounded up to next higher
	 * integer if it includes a fractional microsecond) to send frame of
	 * len bytes (does not include FCS) at the given rate. Duration will
	 * also include SIFS.
	 *
	 * rate is in 100 kbps, so divident is multiplied by 10 in the
	 * DIV_ROUND_UP() operations.
	 */

	if (local->hw.conf.phymode == MODE_IEEE80211A || erp ||
	    local->hw.conf.phymode == MODE_ATHEROS_TURBO) {
		/*
		 * OFDM:
		 *
		 * N_DBPS = DATARATE x 4
		 * N_SYM = Ceiling((16+8xLENGTH+6) / N_DBPS)
		 *	(16 = SIGNAL time, 6 = tail bits)
		 * TXTIME = T_PREAMBLE + T_SIGNAL + T_SYM x N_SYM + Signal Ext
		 *
		 * T_SYM = 4 usec
		 * 802.11a - 17.5.2: aSIFSTime = 16 usec
		 * 802.11g - 19.8.4: aSIFSTime = 10 usec +
		 *	signal ext = 6 usec
		 */
		/* FIX: Atheros Turbo may have different (shorter) duration? */
		dur = 16; /* SIFS + signal ext */
		dur += 16; /* 17.3.2.3: T_PREAMBLE = 16 usec */
		dur += 4; /* 17.3.2.3: T_SIGNAL = 4 usec */
		dur += 4 * DIV_ROUND_UP((16 + 8 * (len + 4) + 6) * 10,
					4 * rate); /* T_SYM x N_SYM */
	} else {
		/*
		 * 802.11b or 802.11g with 802.11b compatibility:
		 * 18.3.4: TXTIME = PreambleLength + PLCPHeaderTime +
		 * Ceiling(((LENGTH+PBCC)x8)/DATARATE). PBCC=0.
		 *
		 * 802.11 (DS): 15.3.3, 802.11b: 18.3.4
		 * aSIFSTime = 10 usec
		 * aPreambleLength = 144 usec or 72 usec with short preamble
		 * aPLCPHeaderLength = 48 usec or 24 usec with short preamble
		 */
		dur = 10; /* aSIFSTime = 10 usec */
		dur += short_preamble ? (72 + 24) : (144 + 48);

		dur += DIV_ROUND_UP(8 * (len + 4) * 10, rate);
	}

	return dur;
}


/* Exported duration function for driver use */
__le16 ieee80211_generic_frame_duration(struct ieee80211_hw *hw,
					size_t frame_len, int rate)
{
	struct ieee80211_local *local = hw_to_local(hw);
	u16 dur;
	int erp;

	erp = ieee80211_is_erp_rate(hw->conf.phymode, rate);
	dur = ieee80211_frame_duration(local, frame_len, rate,
				       erp, local->short_preamble);

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_generic_frame_duration);


static u16 ieee80211_duration(struct ieee80211_txrx_data *tx, int group_addr,
			      int next_frag_len)
{
	int rate, mrate, erp, dur, i;
	struct ieee80211_rate *txrate = tx->u.tx.rate;
	struct ieee80211_local *local = tx->local;
	struct ieee80211_hw_mode *mode = tx->u.tx.mode;

	erp = txrate->flags & IEEE80211_RATE_ERP;

	/*
	 * data and mgmt (except PS Poll):
	 * - during CFP: 32768
	 * - during contention period:
	 *   if addr1 is group address: 0
	 *   if more fragments = 0 and addr1 is individual address: time to
	 *      transmit one ACK plus SIFS
	 *   if more fragments = 1 and addr1 is individual address: time to
	 *      transmit next fragment plus 2 x ACK plus 3 x SIFS
	 *
	 * IEEE 802.11, 9.6:
	 * - control response frame (CTS or ACK) shall be transmitted using the
	 *   same rate as the immediately previous frame in the frame exchange
	 *   sequence, if this rate belongs to the PHY mandatory rates, or else
	 *   at the highest possible rate belonging to the PHY rates in the
	 *   BSSBasicRateSet
	 */

	if ((tx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL) {
		/* TODO: These control frames are not currently sent by
		 * 80211.o, but should they be implemented, this function
		 * needs to be updated to support duration field calculation.
		 *
		 * RTS: time needed to transmit pending data/mgmt frame plus
		 *    one CTS frame plus one ACK frame plus 3 x SIFS
		 * CTS: duration of immediately previous RTS minus time
		 *    required to transmit CTS and its SIFS
		 * ACK: 0 if immediately previous directed data/mgmt had
		 *    more=0, with more=1 duration in ACK frame is duration
		 *    from previous frame minus time needed to transmit ACK
		 *    and its SIFS
		 * PS Poll: BIT(15) | BIT(14) | aid
		 */
		return 0;
	}

	/* data/mgmt */
	if (0 /* FIX: data/mgmt during CFP */)
		return 32768;

	if (group_addr) /* Group address as the destination - no ACK */
		return 0;

	/* Individual destination address:
	 * IEEE 802.11, Ch. 9.6 (after IEEE 802.11g changes)
	 * CTS and ACK frames shall be transmitted using the highest rate in
	 * basic rate set that is less than or equal to the rate of the
	 * immediately previous frame and that is using the same modulation
	 * (CCK or OFDM). If no basic rate set matches with these requirements,
	 * the highest mandatory rate of the PHY that is less than or equal to
	 * the rate of the previous frame is used.
	 * Mandatory rates for IEEE 802.11g PHY: 1, 2, 5.5, 11, 6, 12, 24 Mbps
	 */
	rate = -1;
	mrate = 10; /* use 1 Mbps if everything fails */
	for (i = 0; i < mode->num_rates; i++) {
		struct ieee80211_rate *r = &mode->rates[i];
		if (r->rate > txrate->rate)
			break;

		if (IEEE80211_RATE_MODULATION(txrate->flags) !=
		    IEEE80211_RATE_MODULATION(r->flags))
			continue;

		if (r->flags & IEEE80211_RATE_BASIC)
			rate = r->rate;
		else if (r->flags & IEEE80211_RATE_MANDATORY)
			mrate = r->rate;
	}
	if (rate == -1) {
		/* No matching basic rate found; use highest suitable mandatory
		 * PHY rate */
		rate = mrate;
	}

	/* Time needed to transmit ACK
	 * (10 bytes + 4-byte FCS = 112 bits) plus SIFS; rounded up
	 * to closest integer */

	dur = ieee80211_frame_duration(local, 10, rate, erp,
				       local->short_preamble);

	if (next_frag_len) {
		/* Frame is fragmented: duration increases with time needed to
		 * transmit next fragment plus ACK and 2 x SIFS. */
		dur *= 2; /* ACK + SIFS */
		/* next fragment */
		dur += ieee80211_frame_duration(local, next_frag_len,
						txrate->rate, erp,
						local->short_preamble);
	}

	return dur;
}


static ieee80211_txrx_result
ieee80211_tx_h_misc(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;
	u16 dur;
	struct ieee80211_tx_control *control = tx->u.tx.control;
	struct ieee80211_hw_mode *mode = tx->u.tx.mode;

	if (!is_multicast_ether_addr(hdr->addr1)) {
		if (tx->skb->len + FCS_LEN > tx->local->rts_threshold &&
		    tx->local->rts_threshold < IEEE80211_MAX_RTS_THRESHOLD) {
			control->flags |= IEEE80211_TXCTL_USE_RTS_CTS;
			control->retry_limit =
				tx->local->long_retry_limit;
		} else {
			control->retry_limit =
				tx->local->short_retry_limit;
		}
	} else {
		control->retry_limit = 1;
	}

	if (tx->fragmented) {
		/* Do not use multiple retry rates when sending fragmented
		 * frames.
		 * TODO: The last fragment could still use multiple retry
		 * rates. */
		control->alt_retry_rate = -1;
	}

	/* Use CTS protection for unicast frames sent using extended rates if
	 * there are associated non-ERP stations and RTS/CTS is not configured
	 * for the frame. */
	if (mode->mode == MODE_IEEE80211G &&
	    (tx->u.tx.rate->flags & IEEE80211_RATE_ERP) &&
	    tx->u.tx.unicast &&
	    tx->local->cts_protect_erp_frames &&
	    !(control->flags & IEEE80211_TXCTL_USE_RTS_CTS))
		control->flags |= IEEE80211_TXCTL_USE_CTS_PROTECT;

	/* Setup duration field for the first fragment of the frame. Duration
	 * for remaining fragments will be updated when they are being sent
	 * to low-level driver in ieee80211_tx(). */
	dur = ieee80211_duration(tx, is_multicast_ether_addr(hdr->addr1),
				 tx->fragmented ? tx->u.tx.extra_frag[0]->len :
				 0);
	hdr->duration_id = cpu_to_le16(dur);

	if ((control->flags & IEEE80211_TXCTL_USE_RTS_CTS) ||
	    (control->flags & IEEE80211_TXCTL_USE_CTS_PROTECT)) {
		struct ieee80211_rate *rate;

		/* Do not use multiple retry rates when using RTS/CTS */
		control->alt_retry_rate = -1;

		/* Use min(data rate, max base rate) as CTS/RTS rate */
		rate = tx->u.tx.rate;
		while (rate > mode->rates &&
		       !(rate->flags & IEEE80211_RATE_BASIC))
			rate--;

		control->rts_cts_rate = rate->val;
		control->rts_rate = rate;
	}

	if (tx->sta) {
		tx->sta->tx_packets++;
		tx->sta->tx_fragments++;
		tx->sta->tx_bytes += tx->skb->len;
		if (tx->u.tx.extra_frag) {
			int i;
			tx->sta->tx_fragments += tx->u.tx.num_extra_frag;
			for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
				tx->sta->tx_bytes +=
					tx->u.tx.extra_frag[i]->len;
			}
		}
	}

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_tx_h_check_assoc(struct ieee80211_txrx_data *tx)
{
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	struct sk_buff *skb = tx->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
	u32 sta_flags;

	if (unlikely(tx->local->sta_scanning != 0) &&
	    ((tx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_MGMT ||
	     (tx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_PROBE_REQ))
		return TXRX_DROP;

	if (tx->u.tx.ps_buffered)
		return TXRX_CONTINUE;

	sta_flags = tx->sta ? tx->sta->flags : 0;

	if (likely(tx->u.tx.unicast)) {
		if (unlikely(!(sta_flags & WLAN_STA_ASSOC) &&
			     tx->sdata->type != IEEE80211_IF_TYPE_IBSS &&
			     (tx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA)) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
			printk(KERN_DEBUG "%s: dropped data frame to not "
			       "associated station " MAC_FMT "\n",
			       tx->dev->name, MAC_ARG(hdr->addr1));
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
			I802_DEBUG_INC(tx->local->tx_handlers_drop_not_assoc);
			return TXRX_DROP;
		}
	} else {
		if (unlikely((tx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA &&
			     tx->local->num_sta == 0 &&
			     !tx->local->allow_broadcast_always &&
			     tx->sdata->type != IEEE80211_IF_TYPE_IBSS)) {
			/*
			 * No associated STAs - no need to send multicast
			 * frames.
			 */
			return TXRX_DROP;
		}
		return TXRX_CONTINUE;
	}

	if (unlikely(!tx->u.tx.mgmt_interface && tx->sdata->ieee802_1x &&
		     !(sta_flags & WLAN_STA_AUTHORIZED))) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		printk(KERN_DEBUG "%s: dropped frame to " MAC_FMT
		       " (unauthorized port)\n", tx->dev->name,
		       MAC_ARG(hdr->addr1));
#endif
		I802_DEBUG_INC(tx->local->tx_handlers_drop_unauth_port);
		return TXRX_DROP;
	}

	return TXRX_CONTINUE;
}

static ieee80211_txrx_result
ieee80211_tx_h_sequence(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;

	if (ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_control)) >= 24)
		ieee80211_include_sequence(tx->sdata, hdr);

	return TXRX_CONTINUE;
}

/* This function is called whenever the AP is about to exceed the maximum limit
 * of buffered frames for power saving STAs. This situation should not really
 * happen often during normal operation, so dropping the oldest buffered packet
 * from each queue should be OK to make some room for new frames. */
static void purge_old_ps_buffers(struct ieee80211_local *local)
{
	int total = 0, purged = 0;
	struct sk_buff *skb;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;

	read_lock(&local->sub_if_lock);
	list_for_each_entry(sdata, &local->sub_if_list, list) {
		struct ieee80211_if_ap *ap;
		if (sdata->dev == local->mdev ||
		    sdata->type != IEEE80211_IF_TYPE_AP)
			continue;
		ap = &sdata->u.ap;
		skb = skb_dequeue(&ap->ps_bc_buf);
		if (skb) {
			purged++;
			dev_kfree_skb(skb);
		}
		total += skb_queue_len(&ap->ps_bc_buf);
	}
	read_unlock(&local->sub_if_lock);

	spin_lock_bh(&local->sta_lock);
	list_for_each_entry(sta, &local->sta_list, list) {
		skb = skb_dequeue(&sta->ps_tx_buf);
		if (skb) {
			purged++;
			dev_kfree_skb(skb);
		}
		total += skb_queue_len(&sta->ps_tx_buf);
	}
	spin_unlock_bh(&local->sta_lock);

	local->total_ps_buffered = total;
	printk(KERN_DEBUG "%s: PS buffers full - purged %d frames\n",
	       local->mdev->name, purged);
}


static inline ieee80211_txrx_result
ieee80211_tx_h_multicast_ps_buf(struct ieee80211_txrx_data *tx)
{
	/* broadcast/multicast frame */
	/* If any of the associated stations is in power save mode,
	 * the frame is buffered to be sent after DTIM beacon frame */
	if ((tx->local->hw.flags & IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING) &&
	    tx->sdata->type != IEEE80211_IF_TYPE_WDS &&
	    tx->sdata->bss && atomic_read(&tx->sdata->bss->num_sta_ps) &&
	    !(tx->fc & IEEE80211_FCTL_ORDER)) {
		if (tx->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
			purge_old_ps_buffers(tx->local);
		if (skb_queue_len(&tx->sdata->bss->ps_bc_buf) >=
		    AP_MAX_BC_BUFFER) {
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: BC TX buffer full - "
				       "dropping the oldest frame\n",
				       tx->dev->name);
			}
			dev_kfree_skb(skb_dequeue(&tx->sdata->bss->ps_bc_buf));
		} else
			tx->local->total_ps_buffered++;
		skb_queue_tail(&tx->sdata->bss->ps_bc_buf, tx->skb);
		return TXRX_QUEUED;
	}

	return TXRX_CONTINUE;
}


static inline ieee80211_txrx_result
ieee80211_tx_h_unicast_ps_buf(struct ieee80211_txrx_data *tx)
{
	struct sta_info *sta = tx->sta;

	if (unlikely(!sta ||
		     ((tx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT &&
		      (tx->fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PROBE_RESP)))
		return TXRX_CONTINUE;

	if (unlikely((sta->flags & WLAN_STA_PS) && !sta->pspoll)) {
		struct ieee80211_tx_packet_data *pkt_data;
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		printk(KERN_DEBUG "STA " MAC_FMT " aid %d: PS buffer (entries "
		       "before %d)\n",
		       MAC_ARG(sta->addr), sta->aid,
		       skb_queue_len(&sta->ps_tx_buf));
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
		sta->flags |= WLAN_STA_TIM;
		if (tx->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
			purge_old_ps_buffers(tx->local);
		if (skb_queue_len(&sta->ps_tx_buf) >= STA_MAX_TX_BUFFER) {
			struct sk_buff *old = skb_dequeue(&sta->ps_tx_buf);
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: STA " MAC_FMT " TX "
				       "buffer full - dropping oldest frame\n",
				       tx->dev->name, MAC_ARG(sta->addr));
			}
			dev_kfree_skb(old);
		} else
			tx->local->total_ps_buffered++;
		/* Queue frame to be sent after STA sends an PS Poll frame */
		if (skb_queue_empty(&sta->ps_tx_buf)) {
			if (tx->local->ops->set_tim)
				tx->local->ops->set_tim(local_to_hw(tx->local),
						       sta->aid, 1);
			if (tx->sdata->bss)
				bss_tim_set(tx->local, tx->sdata->bss, sta->aid);
		}
		pkt_data = (struct ieee80211_tx_packet_data *)tx->skb->cb;
		pkt_data->jiffies = jiffies;
		skb_queue_tail(&sta->ps_tx_buf, tx->skb);
		return TXRX_QUEUED;
	}
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	else if (unlikely(sta->flags & WLAN_STA_PS)) {
		printk(KERN_DEBUG "%s: STA " MAC_FMT " in PS mode, but pspoll "
		       "set -> send frame\n", tx->dev->name,
		       MAC_ARG(sta->addr));
	}
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
	sta->pspoll = 0;

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_tx_h_ps_buf(struct ieee80211_txrx_data *tx)
{
	if (unlikely(tx->u.tx.ps_buffered))
		return TXRX_CONTINUE;

	if (tx->u.tx.unicast)
		return ieee80211_tx_h_unicast_ps_buf(tx);
	else
		return ieee80211_tx_h_multicast_ps_buf(tx);
}


static void inline
__ieee80211_tx_prepare(struct ieee80211_txrx_data *tx,
		       struct sk_buff *skb,
		       struct net_device *dev,
		       struct ieee80211_tx_control *control)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	int hdrlen;

	memset(tx, 0, sizeof(*tx));
	tx->skb = skb;
	tx->dev = dev; /* use original interface */
	tx->local = local;
	tx->sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	tx->sta = sta_info_get(local, hdr->addr1);
	tx->fc = le16_to_cpu(hdr->frame_control);
	control->power_level = local->hw.conf.power_level;
	tx->u.tx.control = control;
	tx->u.tx.unicast = !is_multicast_ether_addr(hdr->addr1);
	if (is_multicast_ether_addr(hdr->addr1))
		control->flags |= IEEE80211_TXCTL_NO_ACK;
	else
		control->flags &= ~IEEE80211_TXCTL_NO_ACK;
	tx->fragmented = local->fragmentation_threshold <
		IEEE80211_MAX_FRAG_THRESHOLD && tx->u.tx.unicast &&
		skb->len + FCS_LEN > local->fragmentation_threshold &&
		(!local->ops->set_frag_threshold);
	if (!tx->sta)
		control->flags |= IEEE80211_TXCTL_CLEAR_DST_MASK;
	else if (tx->sta->clear_dst_mask) {
		control->flags |= IEEE80211_TXCTL_CLEAR_DST_MASK;
		tx->sta->clear_dst_mask = 0;
	}
	control->antenna_sel_tx = local->hw.conf.antenna_sel_tx;
	if (local->sta_antenna_sel != STA_ANTENNA_SEL_AUTO && tx->sta)
		control->antenna_sel_tx = tx->sta->antenna_sel_tx;
	hdrlen = ieee80211_get_hdrlen(tx->fc);
	if (skb->len > hdrlen + sizeof(rfc1042_header) + 2) {
		u8 *pos = &skb->data[hdrlen + sizeof(rfc1042_header)];
		tx->ethertype = (pos[0] << 8) | pos[1];
	}
	control->flags |= IEEE80211_TXCTL_FIRST_FRAGMENT;

}

static int inline is_ieee80211_device(struct net_device *dev,
				      struct net_device *master)
{
	return (wdev_priv(dev->ieee80211_ptr) ==
		wdev_priv(master->ieee80211_ptr));
}

/* Device in tx->dev has a reference added; use dev_put(tx->dev) when
 * finished with it. */
static int inline ieee80211_tx_prepare(struct ieee80211_txrx_data *tx,
				       struct sk_buff *skb,
				       struct net_device *mdev,
				       struct ieee80211_tx_control *control)
{
	struct ieee80211_tx_packet_data *pkt_data;
	struct net_device *dev;

	pkt_data = (struct ieee80211_tx_packet_data *)skb->cb;
	dev = dev_get_by_index(pkt_data->ifindex);
	if (unlikely(dev && !is_ieee80211_device(dev, mdev))) {
		dev_put(dev);
		dev = NULL;
	}
	if (unlikely(!dev))
		return -ENODEV;
	__ieee80211_tx_prepare(tx, skb, dev, control);
	return 0;
}

static inline int __ieee80211_queue_stopped(const struct ieee80211_local *local,
					    int queue)
{
	return test_bit(IEEE80211_LINK_STATE_XOFF, &local->state[queue]);
}

static inline int __ieee80211_queue_pending(const struct ieee80211_local *local,
					    int queue)
{
	return test_bit(IEEE80211_LINK_STATE_PENDING, &local->state[queue]);
}

#define IEEE80211_TX_OK		0
#define IEEE80211_TX_AGAIN	1
#define IEEE80211_TX_FRAG_AGAIN	2

static int __ieee80211_tx(struct ieee80211_local *local, struct sk_buff *skb,
			  struct ieee80211_txrx_data *tx)
{
	struct ieee80211_tx_control *control = tx->u.tx.control;
	int ret, i;

	if (!ieee80211_qdisc_installed(local->mdev) &&
	    __ieee80211_queue_stopped(local, 0)) {
		netif_stop_queue(local->mdev);
		return IEEE80211_TX_AGAIN;
	}
	if (skb) {
		ieee80211_dump_frame(local->mdev->name, "TX to low-level driver", skb);
		ret = local->ops->tx(local_to_hw(local), skb, control);
		if (ret)
			return IEEE80211_TX_AGAIN;
		local->mdev->trans_start = jiffies;
		ieee80211_led_tx(local, 1);
	}
	if (tx->u.tx.extra_frag) {
		control->flags &= ~(IEEE80211_TXCTL_USE_RTS_CTS |
				    IEEE80211_TXCTL_USE_CTS_PROTECT |
				    IEEE80211_TXCTL_CLEAR_DST_MASK |
				    IEEE80211_TXCTL_FIRST_FRAGMENT);
		for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
			if (!tx->u.tx.extra_frag[i])
				continue;
			if (__ieee80211_queue_stopped(local, control->queue))
				return IEEE80211_TX_FRAG_AGAIN;
			if (i == tx->u.tx.num_extra_frag) {
				control->tx_rate = tx->u.tx.last_frag_hwrate;
				control->rate = tx->u.tx.last_frag_rate;
				if (tx->u.tx.probe_last_frag)
					control->flags |=
						IEEE80211_TXCTL_RATE_CTRL_PROBE;
				else
					control->flags &=
						~IEEE80211_TXCTL_RATE_CTRL_PROBE;
			}

			ieee80211_dump_frame(local->mdev->name,
					     "TX to low-level driver",
					     tx->u.tx.extra_frag[i]);
			ret = local->ops->tx(local_to_hw(local),
					    tx->u.tx.extra_frag[i],
					    control);
			if (ret)
				return IEEE80211_TX_FRAG_AGAIN;
			local->mdev->trans_start = jiffies;
			ieee80211_led_tx(local, 1);
			tx->u.tx.extra_frag[i] = NULL;
		}
		kfree(tx->u.tx.extra_frag);
		tx->u.tx.extra_frag = NULL;
	}
	return IEEE80211_TX_OK;
}

static int ieee80211_tx(struct net_device *dev, struct sk_buff *skb,
			struct ieee80211_tx_control *control, int mgmt)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	ieee80211_tx_handler *handler;
	struct ieee80211_txrx_data tx;
	ieee80211_txrx_result res = TXRX_DROP;
	int ret, i;

	WARN_ON(__ieee80211_queue_pending(local, control->queue));

	if (unlikely(skb->len < 10)) {
		dev_kfree_skb(skb);
		return 0;
	}

	__ieee80211_tx_prepare(&tx, skb, dev, control);
	sta = tx.sta;
	tx.u.tx.mgmt_interface = mgmt;
	tx.u.tx.mode = local->hw.conf.mode;

	for (handler = local->tx_handlers; *handler != NULL; handler++) {
		res = (*handler)(&tx);
		if (res != TXRX_CONTINUE)
			break;
	}

	skb = tx.skb; /* handlers are allowed to change skb */

	if (sta)
		sta_info_put(sta);

	if (unlikely(res == TXRX_DROP)) {
		I802_DEBUG_INC(local->tx_handlers_drop);
		goto drop;
	}

	if (unlikely(res == TXRX_QUEUED)) {
		I802_DEBUG_INC(local->tx_handlers_queued);
		return 0;
	}

	if (tx.u.tx.extra_frag) {
		for (i = 0; i < tx.u.tx.num_extra_frag; i++) {
			int next_len, dur;
			struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *)
				tx.u.tx.extra_frag[i]->data;

			if (i + 1 < tx.u.tx.num_extra_frag) {
				next_len = tx.u.tx.extra_frag[i + 1]->len;
			} else {
				next_len = 0;
				tx.u.tx.rate = tx.u.tx.last_frag_rate;
				tx.u.tx.last_frag_hwrate = tx.u.tx.rate->val;
			}
			dur = ieee80211_duration(&tx, 0, next_len);
			hdr->duration_id = cpu_to_le16(dur);
		}
	}

retry:
	ret = __ieee80211_tx(local, skb, &tx);
	if (ret) {
		struct ieee80211_tx_stored_packet *store =
			&local->pending_packet[control->queue];

		if (ret == IEEE80211_TX_FRAG_AGAIN)
			skb = NULL;
		set_bit(IEEE80211_LINK_STATE_PENDING,
			&local->state[control->queue]);
		smp_mb();
		/* When the driver gets out of buffers during sending of
		 * fragments and calls ieee80211_stop_queue, there is
		 * a small window between IEEE80211_LINK_STATE_XOFF and
		 * IEEE80211_LINK_STATE_PENDING flags are set. If a buffer
		 * gets available in that window (i.e. driver calls
		 * ieee80211_wake_queue), we would end up with ieee80211_tx
		 * called with IEEE80211_LINK_STATE_PENDING. Prevent this by
		 * continuing transmitting here when that situation is
		 * possible to have happened. */
		if (!__ieee80211_queue_stopped(local, control->queue)) {
			clear_bit(IEEE80211_LINK_STATE_PENDING,
				  &local->state[control->queue]);
			goto retry;
		}
		memcpy(&store->control, control,
		       sizeof(struct ieee80211_tx_control));
		store->skb = skb;
		store->extra_frag = tx.u.tx.extra_frag;
		store->num_extra_frag = tx.u.tx.num_extra_frag;
		store->last_frag_hwrate = tx.u.tx.last_frag_hwrate;
		store->last_frag_rate = tx.u.tx.last_frag_rate;
		store->last_frag_rate_ctrl_probe = tx.u.tx.probe_last_frag;
	}
	return 0;

 drop:
	if (skb)
		dev_kfree_skb(skb);
	for (i = 0; i < tx.u.tx.num_extra_frag; i++)
		if (tx.u.tx.extra_frag[i])
			dev_kfree_skb(tx.u.tx.extra_frag[i]);
	kfree(tx.u.tx.extra_frag);
	return 0;
}

static void ieee80211_tx_pending(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *)data;
	struct net_device *dev = local->mdev;
	struct ieee80211_tx_stored_packet *store;
	struct ieee80211_txrx_data tx;
	int i, ret, reschedule = 0;

	netif_tx_lock_bh(dev);
	for (i = 0; i < local->hw.queues; i++) {
		if (__ieee80211_queue_stopped(local, i))
			continue;
		if (!__ieee80211_queue_pending(local, i)) {
			reschedule = 1;
			continue;
		}
		store = &local->pending_packet[i];
		tx.u.tx.control = &store->control;
		tx.u.tx.extra_frag = store->extra_frag;
		tx.u.tx.num_extra_frag = store->num_extra_frag;
		tx.u.tx.last_frag_hwrate = store->last_frag_hwrate;
		tx.u.tx.last_frag_rate = store->last_frag_rate;
		tx.u.tx.probe_last_frag = store->last_frag_rate_ctrl_probe;
		ret = __ieee80211_tx(local, store->skb, &tx);
		if (ret) {
			if (ret == IEEE80211_TX_FRAG_AGAIN)
				store->skb = NULL;
		} else {
			clear_bit(IEEE80211_LINK_STATE_PENDING,
				  &local->state[i]);
			reschedule = 1;
		}
	}
	netif_tx_unlock_bh(dev);
	if (reschedule) {
		if (!ieee80211_qdisc_installed(dev)) {
			if (!__ieee80211_queue_stopped(local, 0))
				netif_wake_queue(dev);
		} else
			netif_schedule(dev);
	}
}

static void ieee80211_clear_tx_pending(struct ieee80211_local *local)
{
	int i, j;
	struct ieee80211_tx_stored_packet *store;

	for (i = 0; i < local->hw.queues; i++) {
		if (!__ieee80211_queue_pending(local, i))
			continue;
		store = &local->pending_packet[i];
		kfree_skb(store->skb);
		for (j = 0; j < store->num_extra_frag; j++)
			kfree_skb(store->extra_frag[j]);
		kfree(store->extra_frag);
		clear_bit(IEEE80211_LINK_STATE_PENDING, &local->state[i]);
	}
}

static int ieee80211_master_start_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct ieee80211_tx_control control;
	struct ieee80211_tx_packet_data *pkt_data;
	struct net_device *odev = NULL;
	struct ieee80211_sub_if_data *osdata;
	int headroom;
	int ret;

	/*
	 * copy control out of the skb so other people can use skb->cb
	 */
	pkt_data = (struct ieee80211_tx_packet_data *)skb->cb;
	memset(&control, 0, sizeof(struct ieee80211_tx_control));

	if (pkt_data->ifindex)
		odev = dev_get_by_index(pkt_data->ifindex);
	if (unlikely(odev && !is_ieee80211_device(odev, dev))) {
		dev_put(odev);
		odev = NULL;
	}
	if (unlikely(!odev)) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		printk(KERN_DEBUG "%s: Discarded packet with nonexistent "
		       "originating device\n", dev->name);
#endif
		dev_kfree_skb(skb);
		return 0;
	}
	osdata = IEEE80211_DEV_TO_SUB_IF(odev);

	headroom = osdata->local->hw.extra_tx_headroom +
		IEEE80211_ENCRYPT_HEADROOM;
	if (skb_headroom(skb) < headroom) {
		if (pskb_expand_head(skb, headroom, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return 0;
		}
	}

	control.ifindex = odev->ifindex;
	control.type = osdata->type;
	if (pkt_data->req_tx_status)
		control.flags |= IEEE80211_TXCTL_REQ_TX_STATUS;
	if (pkt_data->do_not_encrypt)
		control.flags |= IEEE80211_TXCTL_DO_NOT_ENCRYPT;
	if (pkt_data->requeue)
		control.flags |= IEEE80211_TXCTL_REQUEUE;
	control.queue = pkt_data->queue;

	ret = ieee80211_tx(odev, skb, &control,
			   control.type == IEEE80211_IF_TYPE_MGMT);
	dev_put(odev);

	return ret;
}


/**
 * ieee80211_subif_start_xmit - netif start_xmit function for Ethernet-type
 * subinterfaces (wlan#, WDS, and VLAN interfaces)
 * @skb: packet to be sent
 * @dev: incoming interface
 *
 * Returns: 0 on success (and frees skb in this case) or 1 on failure (skb will
 * not be freed, and caller is responsible for either retrying later or freeing
 * skb).
 *
 * This function takes in an Ethernet header and encapsulates it with suitable
 * IEEE 802.11 header based on which interface the packet is coming in. The
 * encapsulated packet will then be passed to master interface, wlan#.11, for
 * transmission (through low-level driver).
 */
static int ieee80211_subif_start_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_packet_data *pkt_data;
	struct ieee80211_sub_if_data *sdata;
	int ret = 1, head_need;
	u16 ethertype, hdrlen, fc;
	struct ieee80211_hdr hdr;
	const u8 *encaps_data;
	int encaps_len, skip_header_bytes;
	int nh_pos, h_pos, no_encrypt = 0;
	struct sta_info *sta;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (unlikely(skb->len < ETH_HLEN)) {
		printk(KERN_DEBUG "%s: short skb (len=%d)\n",
		       dev->name, skb->len);
		ret = 0;
		goto fail;
	}

	nh_pos = skb_network_header(skb) - skb->data;
	h_pos = skb_transport_header(skb) - skb->data;

	/* convert Ethernet header to proper 802.11 header (based on
	 * operation mode) */
	ethertype = (skb->data[12] << 8) | skb->data[13];
	/* TODO: handling for 802.1x authorized/unauthorized port */
	fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA;

	if (likely(sdata->type == IEEE80211_IF_TYPE_AP ||
		   sdata->type == IEEE80211_IF_TYPE_VLAN)) {
		fc |= IEEE80211_FCTL_FROMDS;
		/* DA BSSID SA */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, dev->dev_addr, ETH_ALEN);
		memcpy(hdr.addr3, skb->data + ETH_ALEN, ETH_ALEN);
		hdrlen = 24;
	} else if (sdata->type == IEEE80211_IF_TYPE_WDS) {
		fc |= IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS;
		/* RA TA DA SA */
		memcpy(hdr.addr1, sdata->u.wds.remote_addr, ETH_ALEN);
		memcpy(hdr.addr2, dev->dev_addr, ETH_ALEN);
		memcpy(hdr.addr3, skb->data, ETH_ALEN);
		memcpy(hdr.addr4, skb->data + ETH_ALEN, ETH_ALEN);
		hdrlen = 30;
	} else if (sdata->type == IEEE80211_IF_TYPE_STA) {
		fc |= IEEE80211_FCTL_TODS;
		/* BSSID SA DA */
		memcpy(hdr.addr1, sdata->u.sta.bssid, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, skb->data, ETH_ALEN);
		hdrlen = 24;
	} else if (sdata->type == IEEE80211_IF_TYPE_IBSS) {
		/* DA SA BSSID */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, sdata->u.sta.bssid, ETH_ALEN);
		hdrlen = 24;
	} else {
		ret = 0;
		goto fail;
	}

	/* receiver is QoS enabled, use a QoS type frame */
	sta = sta_info_get(local, hdr.addr1);
	if (sta) {
		if (sta->flags & WLAN_STA_WME) {
			fc |= IEEE80211_STYPE_QOS_DATA;
			hdrlen += 2;
		}
		sta_info_put(sta);
	}

	hdr.frame_control = cpu_to_le16(fc);
	hdr.duration_id = 0;
	hdr.seq_ctrl = 0;

	skip_header_bytes = ETH_HLEN;
	if (ethertype == ETH_P_AARP || ethertype == ETH_P_IPX) {
		encaps_data = bridge_tunnel_header;
		encaps_len = sizeof(bridge_tunnel_header);
		skip_header_bytes -= 2;
	} else if (ethertype >= 0x600) {
		encaps_data = rfc1042_header;
		encaps_len = sizeof(rfc1042_header);
		skip_header_bytes -= 2;
	} else {
		encaps_data = NULL;
		encaps_len = 0;
	}

	skb_pull(skb, skip_header_bytes);
	nh_pos -= skip_header_bytes;
	h_pos -= skip_header_bytes;

	/* TODO: implement support for fragments so that there is no need to
	 * reallocate and copy payload; it might be enough to support one
	 * extra fragment that would be copied in the beginning of the frame
	 * data.. anyway, it would be nice to include this into skb structure
	 * somehow
	 *
	 * There are few options for this:
	 * use skb->cb as an extra space for 802.11 header
	 * allocate new buffer if not enough headroom
	 * make sure that there is enough headroom in every skb by increasing
	 * build in headroom in __dev_alloc_skb() (linux/skbuff.h) and
	 * alloc_skb() (net/core/skbuff.c)
	 */
	head_need = hdrlen + encaps_len + local->hw.extra_tx_headroom;
	head_need -= skb_headroom(skb);

	/* We are going to modify skb data, so make a copy of it if happens to
	 * be cloned. This could happen, e.g., with Linux bridge code passing
	 * us broadcast frames. */

	if (head_need > 0 || skb_cloned(skb)) {
#if 0
		printk(KERN_DEBUG "%s: need to reallocate buffer for %d bytes "
		       "of headroom\n", dev->name, head_need);
#endif

		if (skb_cloned(skb))
			I802_DEBUG_INC(local->tx_expand_skb_head_cloned);
		else
			I802_DEBUG_INC(local->tx_expand_skb_head);
		/* Since we have to reallocate the buffer, make sure that there
		 * is enough room for possible WEP IV/ICV and TKIP (8 bytes
		 * before payload and 12 after). */
		if (pskb_expand_head(skb, (head_need > 0 ? head_need + 8 : 8),
				     12, GFP_ATOMIC)) {
			printk(KERN_DEBUG "%s: failed to reallocate TX buffer"
			       "\n", dev->name);
			goto fail;
		}
	}

	if (encaps_data) {
		memcpy(skb_push(skb, encaps_len), encaps_data, encaps_len);
		nh_pos += encaps_len;
		h_pos += encaps_len;
	}
	memcpy(skb_push(skb, hdrlen), &hdr, hdrlen);
	nh_pos += hdrlen;
	h_pos += hdrlen;

	pkt_data = (struct ieee80211_tx_packet_data *)skb->cb;
	memset(pkt_data, 0, sizeof(struct ieee80211_tx_packet_data));
	pkt_data->ifindex = sdata->dev->ifindex;
	pkt_data->mgmt_iface = (sdata->type == IEEE80211_IF_TYPE_MGMT);
	pkt_data->do_not_encrypt = no_encrypt;

	skb->dev = local->mdev;
	sdata->stats.tx_packets++;
	sdata->stats.tx_bytes += skb->len;

	/* Update skb pointers to various headers since this modified frame
	 * is going to go through Linux networking code that may potentially
	 * need things like pointer to IP header. */
	skb_set_mac_header(skb, 0);
	skb_set_network_header(skb, nh_pos);
	skb_set_transport_header(skb, h_pos);

	dev->trans_start = jiffies;
	dev_queue_xmit(skb);

	return 0;

 fail:
	if (!ret)
		dev_kfree_skb(skb);

	return ret;
}


/*
 * This is the transmit routine for the 802.11 type interfaces
 * called by upper layers of the linux networking
 * stack when it has a frame to transmit
 */
static int
ieee80211_mgmt_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_tx_packet_data *pkt_data;
	struct ieee80211_hdr *hdr;
	u16 fc;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (skb->len < 10) {
		dev_kfree_skb(skb);
		return 0;
	}

	if (skb_headroom(skb) < sdata->local->hw.extra_tx_headroom) {
		if (pskb_expand_head(skb,
		    sdata->local->hw.extra_tx_headroom, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return 0;
		}
	}

	hdr = (struct ieee80211_hdr *) skb->data;
	fc = le16_to_cpu(hdr->frame_control);

	pkt_data = (struct ieee80211_tx_packet_data *) skb->cb;
	memset(pkt_data, 0, sizeof(struct ieee80211_tx_packet_data));
	pkt_data->ifindex = sdata->dev->ifindex;
	pkt_data->mgmt_iface = (sdata->type == IEEE80211_IF_TYPE_MGMT);

	skb->priority = 20; /* use hardcoded priority for mgmt TX queue */
	skb->dev = sdata->local->mdev;

	/*
	 * We're using the protocol field of the the frame control header
	 * to request TX callback for hostapd. BIT(1) is checked.
	 */
	if ((fc & BIT(1)) == BIT(1)) {
		pkt_data->req_tx_status = 1;
		fc &= ~BIT(1);
		hdr->frame_control = cpu_to_le16(fc);
	}

	pkt_data->do_not_encrypt = !(fc & IEEE80211_FCTL_PROTECTED);

	sdata->stats.tx_packets++;
	sdata->stats.tx_bytes += skb->len;

	dev_queue_xmit(skb);

	return 0;
}


static void ieee80211_beacon_add_tim(struct ieee80211_local *local,
				     struct ieee80211_if_ap *bss,
				     struct sk_buff *skb)
{
	u8 *pos, *tim;
	int aid0 = 0;
	int i, have_bits = 0, n1, n2;

	/* Generate bitmap for TIM only if there are any STAs in power save
	 * mode. */
	spin_lock_bh(&local->sta_lock);
	if (atomic_read(&bss->num_sta_ps) > 0)
		/* in the hope that this is faster than
		 * checking byte-for-byte */
		have_bits = !bitmap_empty((unsigned long*)bss->tim,
					  IEEE80211_MAX_AID+1);

	if (bss->dtim_count == 0)
		bss->dtim_count = bss->dtim_period - 1;
	else
		bss->dtim_count--;

	tim = pos = (u8 *) skb_put(skb, 6);
	*pos++ = WLAN_EID_TIM;
	*pos++ = 4;
	*pos++ = bss->dtim_count;
	*pos++ = bss->dtim_period;

	if (bss->dtim_count == 0 && !skb_queue_empty(&bss->ps_bc_buf))
		aid0 = 1;

	if (have_bits) {
		/* Find largest even number N1 so that bits numbered 1 through
		 * (N1 x 8) - 1 in the bitmap are 0 and number N2 so that bits
		 * (N2 + 1) x 8 through 2007 are 0. */
		n1 = 0;
		for (i = 0; i < IEEE80211_MAX_TIM_LEN; i++) {
			if (bss->tim[i]) {
				n1 = i & 0xfe;
				break;
			}
		}
		n2 = n1;
		for (i = IEEE80211_MAX_TIM_LEN - 1; i >= n1; i--) {
			if (bss->tim[i]) {
				n2 = i;
				break;
			}
		}

		/* Bitmap control */
		*pos++ = n1 | aid0;
		/* Part Virt Bitmap */
		memcpy(pos, bss->tim + n1, n2 - n1 + 1);

		tim[1] = n2 - n1 + 4;
		skb_put(skb, n2 - n1);
	} else {
		*pos++ = aid0; /* Bitmap control */
		*pos++ = 0; /* Part Virt Bitmap */
	}
	spin_unlock_bh(&local->sta_lock);
}


struct sk_buff * ieee80211_beacon_get(struct ieee80211_hw *hw, int if_id,
				      struct ieee80211_tx_control *control)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sk_buff *skb;
	struct net_device *bdev;
	struct ieee80211_sub_if_data *sdata = NULL;
	struct ieee80211_if_ap *ap = NULL;
	struct ieee80211_rate *rate;
	struct rate_control_extra extra;
	u8 *b_head, *b_tail;
	int bh_len, bt_len;

	bdev = dev_get_by_index(if_id);
	if (bdev) {
		sdata = IEEE80211_DEV_TO_SUB_IF(bdev);
		ap = &sdata->u.ap;
		dev_put(bdev);
	}

	if (!ap || sdata->type != IEEE80211_IF_TYPE_AP ||
	    !ap->beacon_head) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit())
			printk(KERN_DEBUG "no beacon data avail for idx=%d "
			       "(%s)\n", if_id, bdev ? bdev->name : "N/A");
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
		return NULL;
	}

	/* Assume we are generating the normal beacon locally */
	b_head = ap->beacon_head;
	b_tail = ap->beacon_tail;
	bh_len = ap->beacon_head_len;
	bt_len = ap->beacon_tail_len;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
		bh_len + bt_len + 256 /* maximum TIM len */);
	if (!skb)
		return NULL;

	skb_reserve(skb, local->hw.extra_tx_headroom);
	memcpy(skb_put(skb, bh_len), b_head, bh_len);

	ieee80211_include_sequence(sdata, (struct ieee80211_hdr *)skb->data);

	ieee80211_beacon_add_tim(local, ap, skb);

	if (b_tail) {
		memcpy(skb_put(skb, bt_len), b_tail, bt_len);
	}

	if (control) {
		memset(&extra, 0, sizeof(extra));
		extra.mode = local->oper_hw_mode;

		rate = rate_control_get_rate(local, local->mdev, skb, &extra);
		if (!rate) {
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: ieee80211_beacon_get: no rate "
				       "found\n", local->mdev->name);
			}
			dev_kfree_skb(skb);
			return NULL;
		}

		control->tx_rate = (local->short_preamble &&
				    (rate->flags & IEEE80211_RATE_PREAMBLE2)) ?
			rate->val2 : rate->val;
		control->antenna_sel_tx = local->hw.conf.antenna_sel_tx;
		control->power_level = local->hw.conf.power_level;
		control->flags |= IEEE80211_TXCTL_NO_ACK;
		control->retry_limit = 1;
		control->flags |= IEEE80211_TXCTL_CLEAR_DST_MASK;
	}

	ap->num_beacons++;
	return skb;
}
EXPORT_SYMBOL(ieee80211_beacon_get);

__le16 ieee80211_rts_duration(struct ieee80211_hw *hw,
			      size_t frame_len,
			      const struct ieee80211_tx_control *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	int short_preamble = local->short_preamble;
	int erp;
	u16 dur;

	rate = frame_txctl->rts_rate;
	erp = !!(rate->flags & IEEE80211_RATE_ERP);

	/* CTS duration */
	dur = ieee80211_frame_duration(local, 10, rate->rate,
				       erp, short_preamble);
	/* Data frame duration */
	dur += ieee80211_frame_duration(local, frame_len, rate->rate,
					erp, short_preamble);
	/* ACK duration */
	dur += ieee80211_frame_duration(local, 10, rate->rate,
					erp, short_preamble);

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_rts_duration);


__le16 ieee80211_ctstoself_duration(struct ieee80211_hw *hw,
				    size_t frame_len,
				    const struct ieee80211_tx_control *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	int short_preamble = local->short_preamble;
	int erp;
	u16 dur;

	rate = frame_txctl->rts_rate;
	erp = !!(rate->flags & IEEE80211_RATE_ERP);

	/* Data frame duration */
	dur = ieee80211_frame_duration(local, frame_len, rate->rate,
				       erp, short_preamble);
	if (!(frame_txctl->flags & IEEE80211_TXCTL_NO_ACK)) {
		/* ACK duration */
		dur += ieee80211_frame_duration(local, 10, rate->rate,
						erp, short_preamble);
	}

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_ctstoself_duration);

void ieee80211_rts_get(struct ieee80211_hw *hw,
		       const void *frame, size_t frame_len,
		       const struct ieee80211_tx_control *frame_txctl,
		       struct ieee80211_rts *rts)
{
	const struct ieee80211_hdr *hdr = frame;
	u16 fctl;

	fctl = IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS;
	rts->frame_control = cpu_to_le16(fctl);
	rts->duration = ieee80211_rts_duration(hw, frame_len, frame_txctl);
	memcpy(rts->ra, hdr->addr1, sizeof(rts->ra));
	memcpy(rts->ta, hdr->addr2, sizeof(rts->ta));
}
EXPORT_SYMBOL(ieee80211_rts_get);

void ieee80211_ctstoself_get(struct ieee80211_hw *hw,
			     const void *frame, size_t frame_len,
			     const struct ieee80211_tx_control *frame_txctl,
			     struct ieee80211_cts *cts)
{
	const struct ieee80211_hdr *hdr = frame;
	u16 fctl;

	fctl = IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS;
	cts->frame_control = cpu_to_le16(fctl);
	cts->duration = ieee80211_ctstoself_duration(hw, frame_len, frame_txctl);
	memcpy(cts->ra, hdr->addr1, sizeof(cts->ra));
}
EXPORT_SYMBOL(ieee80211_ctstoself_get);

struct sk_buff *
ieee80211_get_buffered_bc(struct ieee80211_hw *hw, int if_id,
			  struct ieee80211_tx_control *control)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sk_buff *skb;
	struct sta_info *sta;
	ieee80211_tx_handler *handler;
	struct ieee80211_txrx_data tx;
	ieee80211_txrx_result res = TXRX_DROP;
	struct net_device *bdev;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_ap *bss = NULL;

	bdev = dev_get_by_index(if_id);
	if (bdev) {
		sdata = IEEE80211_DEV_TO_SUB_IF(bdev);
		bss = &sdata->u.ap;
		dev_put(bdev);
	}
	if (!bss || sdata->type != IEEE80211_IF_TYPE_AP || !bss->beacon_head)
		return NULL;

	if (bss->dtim_count != 0)
		return NULL; /* send buffered bc/mc only after DTIM beacon */
	memset(control, 0, sizeof(*control));
	while (1) {
		skb = skb_dequeue(&bss->ps_bc_buf);
		if (!skb)
			return NULL;
		local->total_ps_buffered--;

		if (!skb_queue_empty(&bss->ps_bc_buf) && skb->len >= 2) {
			struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *) skb->data;
			/* more buffered multicast/broadcast frames ==> set
			 * MoreData flag in IEEE 802.11 header to inform PS
			 * STAs */
			hdr->frame_control |=
				cpu_to_le16(IEEE80211_FCTL_MOREDATA);
		}

		if (ieee80211_tx_prepare(&tx, skb, local->mdev, control) == 0)
			break;
		dev_kfree_skb_any(skb);
	}
	sta = tx.sta;
	tx.u.tx.ps_buffered = 1;

	for (handler = local->tx_handlers; *handler != NULL; handler++) {
		res = (*handler)(&tx);
		if (res == TXRX_DROP || res == TXRX_QUEUED)
			break;
	}
	dev_put(tx.dev);
	skb = tx.skb; /* handlers are allowed to change skb */

	if (res == TXRX_DROP) {
		I802_DEBUG_INC(local->tx_handlers_drop);
		dev_kfree_skb(skb);
		skb = NULL;
	} else if (res == TXRX_QUEUED) {
		I802_DEBUG_INC(local->tx_handlers_queued);
		skb = NULL;
	}

	if (sta)
		sta_info_put(sta);

	return skb;
}
EXPORT_SYMBOL(ieee80211_get_buffered_bc);

static int __ieee80211_if_config(struct net_device *dev,
				 struct sk_buff *beacon,
				 struct ieee80211_tx_control *control)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_if_conf conf;
	static u8 scan_bssid[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (!local->ops->config_interface || !netif_running(dev))
		return 0;

	memset(&conf, 0, sizeof(conf));
	conf.type = sdata->type;
	if (sdata->type == IEEE80211_IF_TYPE_STA ||
	    sdata->type == IEEE80211_IF_TYPE_IBSS) {
		if (local->sta_scanning &&
		    local->scan_dev == dev)
			conf.bssid = scan_bssid;
		else
			conf.bssid = sdata->u.sta.bssid;
		conf.ssid = sdata->u.sta.ssid;
		conf.ssid_len = sdata->u.sta.ssid_len;
		conf.generic_elem = sdata->u.sta.extra_ie;
		conf.generic_elem_len = sdata->u.sta.extra_ie_len;
	} else if (sdata->type == IEEE80211_IF_TYPE_AP) {
		conf.ssid = sdata->u.ap.ssid;
		conf.ssid_len = sdata->u.ap.ssid_len;
		conf.generic_elem = sdata->u.ap.generic_elem;
		conf.generic_elem_len = sdata->u.ap.generic_elem_len;
		conf.beacon = beacon;
		conf.beacon_control = control;
	}
	return local->ops->config_interface(local_to_hw(local),
					   dev->ifindex, &conf);
}

int ieee80211_if_config(struct net_device *dev)
{
	return __ieee80211_if_config(dev, NULL, NULL);
}

int ieee80211_if_config_beacon(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_control control;
	struct sk_buff *skb;

	if (!(local->hw.flags & IEEE80211_HW_HOST_GEN_BEACON_TEMPLATE))
		return 0;
	skb = ieee80211_beacon_get(local_to_hw(local), dev->ifindex, &control);
	if (!skb)
		return -ENOMEM;
	return __ieee80211_if_config(dev, skb, &control);
}

int ieee80211_hw_config(struct ieee80211_local *local)
{
	struct ieee80211_hw_mode *mode;
	struct ieee80211_channel *chan;
	int ret = 0;

	if (local->sta_scanning) {
		chan = local->scan_channel;
		mode = local->scan_hw_mode;
	} else {
		chan = local->oper_channel;
		mode = local->oper_hw_mode;
	}

	local->hw.conf.channel = chan->chan;
	local->hw.conf.channel_val = chan->val;
	local->hw.conf.power_level = chan->power_level;
	local->hw.conf.freq = chan->freq;
	local->hw.conf.phymode = mode->mode;
	local->hw.conf.antenna_max = chan->antenna_max;
	local->hw.conf.chan = chan;
	local->hw.conf.mode = mode;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "HW CONFIG: channel=%d freq=%d "
	       "phymode=%d\n", local->hw.conf.channel, local->hw.conf.freq,
	       local->hw.conf.phymode);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */

	if (local->ops->config)
		ret = local->ops->config(local_to_hw(local), &local->hw.conf);

	return ret;
}


static int ieee80211_change_mtu(struct net_device *dev, int new_mtu)
{
	/* FIX: what would be proper limits for MTU?
	 * This interface uses 802.3 frames. */
	if (new_mtu < 256 || new_mtu > IEEE80211_MAX_DATA_LEN - 24 - 6) {
		printk(KERN_WARNING "%s: invalid MTU %d\n",
		       dev->name, new_mtu);
		return -EINVAL;
	}

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: setting MTU %d\n", dev->name, new_mtu);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
	dev->mtu = new_mtu;
	return 0;
}


static int ieee80211_change_mtu_apdev(struct net_device *dev, int new_mtu)
{
	/* FIX: what would be proper limits for MTU?
	 * This interface uses 802.11 frames. */
	if (new_mtu < 256 || new_mtu > IEEE80211_MAX_DATA_LEN) {
		printk(KERN_WARNING "%s: invalid MTU %d\n",
		       dev->name, new_mtu);
		return -EINVAL;
	}

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: setting MTU %d\n", dev->name, new_mtu);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
	dev->mtu = new_mtu;
	return 0;
}

enum netif_tx_lock_class {
	TX_LOCK_NORMAL,
	TX_LOCK_MASTER,
};

static inline void netif_tx_lock_nested(struct net_device *dev, int subclass)
{
	spin_lock_nested(&dev->_xmit_lock, subclass);
	dev->xmit_lock_owner = smp_processor_id();
}

static void ieee80211_set_multicast_list(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	unsigned short flags;

	netif_tx_lock_nested(local->mdev, TX_LOCK_MASTER);
	if (((dev->flags & IFF_ALLMULTI) != 0) ^ (sdata->allmulti != 0)) {
		if (sdata->allmulti) {
			sdata->allmulti = 0;
			local->iff_allmultis--;
		} else {
			sdata->allmulti = 1;
			local->iff_allmultis++;
		}
	}
	if (((dev->flags & IFF_PROMISC) != 0) ^ (sdata->promisc != 0)) {
		if (sdata->promisc) {
			sdata->promisc = 0;
			local->iff_promiscs--;
		} else {
			sdata->promisc = 1;
			local->iff_promiscs++;
		}
	}
	if (dev->mc_count != sdata->mc_count) {
		local->mc_count = local->mc_count - sdata->mc_count +
				  dev->mc_count;
		sdata->mc_count = dev->mc_count;
	}
	if (local->ops->set_multicast_list) {
		flags = local->mdev->flags;
		if (local->iff_allmultis)
			flags |= IFF_ALLMULTI;
		if (local->iff_promiscs)
			flags |= IFF_PROMISC;
		read_lock(&local->sub_if_lock);
		local->ops->set_multicast_list(local_to_hw(local), flags,
					      local->mc_count);
		read_unlock(&local->sub_if_lock);
	}
	netif_tx_unlock(local->mdev);
}

struct dev_mc_list *ieee80211_get_mc_list_item(struct ieee80211_hw *hw,
					       struct dev_mc_list *prev,
					       void **ptr)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata = *ptr;
	struct dev_mc_list *mc;

	if (!prev) {
		WARN_ON(sdata);
		sdata = NULL;
	}
	if (!prev || !prev->next) {
		if (sdata)
			sdata = list_entry(sdata->list.next,
					   struct ieee80211_sub_if_data, list);
		else
			sdata = list_entry(local->sub_if_list.next,
					   struct ieee80211_sub_if_data, list);
		if (&sdata->list != &local->sub_if_list)
			mc = sdata->dev->mc_list;
		else
			mc = NULL;
	} else
		mc = prev->next;

	*ptr = sdata;
	return mc;
}
EXPORT_SYMBOL(ieee80211_get_mc_list_item);

static struct net_device_stats *ieee80211_get_stats(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	return &(sdata->stats);
}

static void ieee80211_if_shutdown(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ASSERT_RTNL();
	switch (sdata->type) {
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		sdata->u.sta.state = IEEE80211_DISABLED;
		del_timer_sync(&sdata->u.sta.timer);
		skb_queue_purge(&sdata->u.sta.skb_queue);
		if (!local->ops->hw_scan &&
		    local->scan_dev == sdata->dev) {
			local->sta_scanning = 0;
			cancel_delayed_work(&local->scan_work);
		}
		flush_workqueue(local->hw.workqueue);
		break;
	}
}

static inline int identical_mac_addr_allowed(int type1, int type2)
{
	return (type1 == IEEE80211_IF_TYPE_MNTR ||
		type2 == IEEE80211_IF_TYPE_MNTR ||
		(type1 == IEEE80211_IF_TYPE_AP &&
		 type2 == IEEE80211_IF_TYPE_WDS) ||
		(type1 == IEEE80211_IF_TYPE_WDS &&
		 (type2 == IEEE80211_IF_TYPE_WDS ||
		  type2 == IEEE80211_IF_TYPE_AP)) ||
		(type1 == IEEE80211_IF_TYPE_AP &&
		 type2 == IEEE80211_IF_TYPE_VLAN) ||
		(type1 == IEEE80211_IF_TYPE_VLAN &&
		 (type2 == IEEE80211_IF_TYPE_AP ||
		  type2 == IEEE80211_IF_TYPE_VLAN)));
}

static int ieee80211_master_open(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;
	int res = -EOPNOTSUPP;

	read_lock(&local->sub_if_lock);
	list_for_each_entry(sdata, &local->sub_if_list, list) {
		if (sdata->dev != dev && netif_running(sdata->dev)) {
			res = 0;
			break;
		}
	}
	read_unlock(&local->sub_if_lock);
	return res;
}

static int ieee80211_master_stop(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;

	read_lock(&local->sub_if_lock);
	list_for_each_entry(sdata, &local->sub_if_list, list)
		if (sdata->dev != dev && netif_running(sdata->dev))
			dev_close(sdata->dev);
	read_unlock(&local->sub_if_lock);

	return 0;
}

static int ieee80211_mgmt_open(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (!netif_running(local->mdev))
		return -EOPNOTSUPP;
	return 0;
}

static int ieee80211_mgmt_stop(struct net_device *dev)
{
	return 0;
}

/* Check if running monitor interfaces should go to a "soft monitor" mode
 * and switch them if necessary. */
static inline void ieee80211_start_soft_monitor(struct ieee80211_local *local)
{
	struct ieee80211_if_init_conf conf;

	if (local->open_count && local->open_count == local->monitors &&
	    !(local->hw.flags & IEEE80211_HW_MONITOR_DURING_OPER) &&
	    local->ops->remove_interface) {
		conf.if_id = -1;
		conf.type = IEEE80211_IF_TYPE_MNTR;
		conf.mac_addr = NULL;
		local->ops->remove_interface(local_to_hw(local), &conf);
	}
}

/* Check if running monitor interfaces should go to a "hard monitor" mode
 * and switch them if necessary. */
static void ieee80211_start_hard_monitor(struct ieee80211_local *local)
{
	struct ieee80211_if_init_conf conf;

	if (local->open_count && local->open_count == local->monitors &&
	    !(local->hw.flags & IEEE80211_HW_MONITOR_DURING_OPER) &&
	    local->ops->add_interface) {
		conf.if_id = -1;
		conf.type = IEEE80211_IF_TYPE_MNTR;
		conf.mac_addr = NULL;
		local->ops->add_interface(local_to_hw(local), &conf);
	}
}

static int ieee80211_open(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata, *nsdata;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_if_init_conf conf;
	int res;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	read_lock(&local->sub_if_lock);
	list_for_each_entry(nsdata, &local->sub_if_list, list) {
		struct net_device *ndev = nsdata->dev;

		if (ndev != dev && ndev != local->mdev && netif_running(ndev) &&
		    compare_ether_addr(dev->dev_addr, ndev->dev_addr) == 0 &&
		    !identical_mac_addr_allowed(sdata->type, nsdata->type)) {
			read_unlock(&local->sub_if_lock);
			return -ENOTUNIQ;
		}
	}
	read_unlock(&local->sub_if_lock);

	if (sdata->type == IEEE80211_IF_TYPE_WDS &&
	    is_zero_ether_addr(sdata->u.wds.remote_addr))
		return -ENOLINK;

	if (sdata->type == IEEE80211_IF_TYPE_MNTR && local->open_count &&
	    !(local->hw.flags & IEEE80211_HW_MONITOR_DURING_OPER)) {
		/* run the interface in a "soft monitor" mode */
		local->monitors++;
		local->open_count++;
		local->hw.conf.flags |= IEEE80211_CONF_RADIOTAP;
		return 0;
	}
	ieee80211_start_soft_monitor(local);

	if (local->ops->add_interface) {
		conf.if_id = dev->ifindex;
		conf.type = sdata->type;
		conf.mac_addr = dev->dev_addr;
		res = local->ops->add_interface(local_to_hw(local), &conf);
		if (res) {
			if (sdata->type == IEEE80211_IF_TYPE_MNTR)
				ieee80211_start_hard_monitor(local);
			return res;
		}
	} else {
		if (sdata->type != IEEE80211_IF_TYPE_STA)
			return -EOPNOTSUPP;
		if (local->open_count > 0)
			return -ENOBUFS;
	}

	if (local->open_count == 0) {
		res = 0;
		tasklet_enable(&local->tx_pending_tasklet);
		tasklet_enable(&local->tasklet);
		if (local->ops->open)
			res = local->ops->open(local_to_hw(local));
		if (res == 0) {
			res = dev_open(local->mdev);
			if (res) {
				if (local->ops->stop)
					local->ops->stop(local_to_hw(local));
			} else {
				res = ieee80211_hw_config(local);
				if (res && local->ops->stop)
					local->ops->stop(local_to_hw(local));
				else if (!res && local->apdev)
					dev_open(local->apdev);
			}
		}
		if (res) {
			if (local->ops->remove_interface)
				local->ops->remove_interface(local_to_hw(local),
							    &conf);
			return res;
		}
	}
	local->open_count++;

	if (sdata->type == IEEE80211_IF_TYPE_MNTR) {
		local->monitors++;
		local->hw.conf.flags |= IEEE80211_CONF_RADIOTAP;
	} else
		ieee80211_if_config(dev);

	if (sdata->type == IEEE80211_IF_TYPE_STA &&
	    !local->user_space_mlme)
		netif_carrier_off(dev);

	netif_start_queue(dev);
	return 0;
}


static int ieee80211_stop(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->type == IEEE80211_IF_TYPE_MNTR &&
	    local->open_count > 1 &&
	    !(local->hw.flags & IEEE80211_HW_MONITOR_DURING_OPER)) {
		/* remove "soft monitor" interface */
		local->open_count--;
		local->monitors--;
		if (!local->monitors)
			local->hw.conf.flags &= ~IEEE80211_CONF_RADIOTAP;
		return 0;
	}

	netif_stop_queue(dev);
	ieee80211_if_shutdown(dev);

	if (sdata->type == IEEE80211_IF_TYPE_MNTR) {
		local->monitors--;
		if (!local->monitors)
			local->hw.conf.flags &= ~IEEE80211_CONF_RADIOTAP;
	}

	local->open_count--;
	if (local->open_count == 0) {
		if (netif_running(local->mdev))
			dev_close(local->mdev);
		if (local->apdev)
			dev_close(local->apdev);
		if (local->ops->stop)
			local->ops->stop(local_to_hw(local));
		tasklet_disable(&local->tx_pending_tasklet);
		tasklet_disable(&local->tasklet);
	}
	if (local->ops->remove_interface) {
		struct ieee80211_if_init_conf conf;

		conf.if_id = dev->ifindex;
		conf.type = sdata->type;
		conf.mac_addr = dev->dev_addr;
		local->ops->remove_interface(local_to_hw(local), &conf);
	}

	ieee80211_start_hard_monitor(local);

	return 0;
}


static int header_parse_80211(struct sk_buff *skb, unsigned char *haddr)
{
	memcpy(haddr, skb_mac_header(skb) + 10, ETH_ALEN); /* addr2 */
	return ETH_ALEN;
}

static inline int ieee80211_bssid_match(const u8 *raddr, const u8 *addr)
{
	return compare_ether_addr(raddr, addr) == 0 ||
	       is_broadcast_ether_addr(raddr);
}


static ieee80211_txrx_result
ieee80211_rx_h_data(struct ieee80211_txrx_data *rx)
{
	struct net_device *dev = rx->dev;
	struct ieee80211_local *local = rx->local;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;
	u16 fc, hdrlen, ethertype;
	u8 *payload;
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	struct sk_buff *skb = rx->skb, *skb2;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	fc = rx->fc;
	if (unlikely((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA))
		return TXRX_CONTINUE;

	if (unlikely(!WLAN_FC_DATA_PRESENT(fc)))
		return TXRX_DROP;

	hdrlen = ieee80211_get_hdrlen(fc);

	/* convert IEEE 802.11 header + possible LLC headers into Ethernet
	 * header
	 * IEEE 802.11 address fields:
	 * ToDS FromDS Addr1 Addr2 Addr3 Addr4
	 *   0     0   DA    SA    BSSID n/a
	 *   0     1   DA    BSSID SA    n/a
	 *   1     0   BSSID SA    DA    n/a
	 *   1     1   RA    TA    DA    SA
	 */

	switch (fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) {
	case IEEE80211_FCTL_TODS:
		/* BSSID SA DA */
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);

		if (unlikely(sdata->type != IEEE80211_IF_TYPE_AP &&
			     sdata->type != IEEE80211_IF_TYPE_VLAN)) {
			printk(KERN_DEBUG "%s: dropped ToDS frame (BSSID="
			       MAC_FMT " SA=" MAC_FMT " DA=" MAC_FMT ")\n",
			       dev->name, MAC_ARG(hdr->addr1),
			       MAC_ARG(hdr->addr2), MAC_ARG(hdr->addr3));
			return TXRX_DROP;
		}
		break;
	case (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS):
		/* RA TA DA SA */
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr4, ETH_ALEN);

		if (unlikely(sdata->type != IEEE80211_IF_TYPE_WDS)) {
			printk(KERN_DEBUG "%s: dropped FromDS&ToDS frame (RA="
			       MAC_FMT " TA=" MAC_FMT " DA=" MAC_FMT " SA="
			       MAC_FMT ")\n",
			       rx->dev->name, MAC_ARG(hdr->addr1),
			       MAC_ARG(hdr->addr2), MAC_ARG(hdr->addr3),
			       MAC_ARG(hdr->addr4));
			return TXRX_DROP;
		}
		break;
	case IEEE80211_FCTL_FROMDS:
		/* DA BSSID SA */
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr3, ETH_ALEN);

		if (sdata->type != IEEE80211_IF_TYPE_STA) {
			return TXRX_DROP;
		}
		break;
	case 0:
		/* DA SA BSSID */
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);

		if (sdata->type != IEEE80211_IF_TYPE_IBSS) {
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: dropped IBSS frame (DA="
				       MAC_FMT " SA=" MAC_FMT " BSSID=" MAC_FMT
				       ")\n",
				       dev->name, MAC_ARG(hdr->addr1),
				       MAC_ARG(hdr->addr2),
				       MAC_ARG(hdr->addr3));
			}
			return TXRX_DROP;
		}
		break;
	}

	payload = skb->data + hdrlen;

	if (unlikely(skb->len - hdrlen < 8)) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: RX too short data frame "
			       "payload\n", dev->name);
		}
		return TXRX_DROP;
	}

	ethertype = (payload[6] << 8) | payload[7];

	if (likely((compare_ether_addr(payload, rfc1042_header) == 0 &&
		    ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
		   compare_ether_addr(payload, bridge_tunnel_header) == 0)) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and
		 * replace EtherType */
		skb_pull(skb, hdrlen + 6);
		memcpy(skb_push(skb, ETH_ALEN), src, ETH_ALEN);
		memcpy(skb_push(skb, ETH_ALEN), dst, ETH_ALEN);
	} else {
		struct ethhdr *ehdr;
		__be16 len;
		skb_pull(skb, hdrlen);
		len = htons(skb->len);
		ehdr = (struct ethhdr *) skb_push(skb, sizeof(struct ethhdr));
		memcpy(ehdr->h_dest, dst, ETH_ALEN);
		memcpy(ehdr->h_source, src, ETH_ALEN);
		ehdr->h_proto = len;
	}
	skb->dev = dev;

	skb2 = NULL;

	sdata->stats.rx_packets++;
	sdata->stats.rx_bytes += skb->len;

	if (local->bridge_packets && (sdata->type == IEEE80211_IF_TYPE_AP
	    || sdata->type == IEEE80211_IF_TYPE_VLAN) && rx->u.rx.ra_match) {
		if (is_multicast_ether_addr(skb->data)) {
			/* send multicast frames both to higher layers in
			 * local net stack and back to the wireless media */
			skb2 = skb_copy(skb, GFP_ATOMIC);
			if (!skb2)
				printk(KERN_DEBUG "%s: failed to clone "
				       "multicast frame\n", dev->name);
		} else {
			struct sta_info *dsta;
			dsta = sta_info_get(local, skb->data);
			if (dsta && !dsta->dev) {
				printk(KERN_DEBUG "Station with null dev "
				       "structure!\n");
			} else if (dsta && dsta->dev == dev) {
				/* Destination station is associated to this
				 * AP, so send the frame directly to it and
				 * do not pass the frame to local net stack.
				 */
				skb2 = skb;
				skb = NULL;
			}
			if (dsta)
				sta_info_put(dsta);
		}
	}

	if (skb) {
		/* deliver to local stack */
		skb->protocol = eth_type_trans(skb, dev);
		memset(skb->cb, 0, sizeof(skb->cb));
		netif_rx(skb);
	}

	if (skb2) {
		/* send to wireless media */
		skb2->protocol = __constant_htons(ETH_P_802_3);
		skb_set_network_header(skb2, 0);
		skb_set_mac_header(skb2, 0);
		dev_queue_xmit(skb2);
	}

	return TXRX_QUEUED;
}


static struct ieee80211_rate *
ieee80211_get_rate(struct ieee80211_local *local, int phymode, int hw_rate)
{
	struct ieee80211_hw_mode *mode;
	int r;

	list_for_each_entry(mode, &local->modes_list, list) {
		if (mode->mode != phymode)
			continue;
		for (r = 0; r < mode->num_rates; r++) {
			struct ieee80211_rate *rate = &mode->rates[r];
			if (rate->val == hw_rate ||
			    (rate->flags & IEEE80211_RATE_PREAMBLE2 &&
			     rate->val2 == hw_rate))
				return rate;
		}
	}

	return NULL;
}

static void
ieee80211_fill_frame_info(struct ieee80211_local *local,
			  struct ieee80211_frame_info *fi,
			  struct ieee80211_rx_status *status)
{
	if (status) {
		struct timespec ts;
		struct ieee80211_rate *rate;

		jiffies_to_timespec(jiffies, &ts);
		fi->hosttime = cpu_to_be64((u64) ts.tv_sec * 1000000 +
					   ts.tv_nsec / 1000);
		fi->mactime = cpu_to_be64(status->mactime);
		switch (status->phymode) {
		case MODE_IEEE80211A:
			fi->phytype = htonl(ieee80211_phytype_ofdm_dot11_a);
			break;
		case MODE_IEEE80211B:
			fi->phytype = htonl(ieee80211_phytype_dsss_dot11_b);
			break;
		case MODE_IEEE80211G:
			fi->phytype = htonl(ieee80211_phytype_pbcc_dot11_g);
			break;
		case MODE_ATHEROS_TURBO:
			fi->phytype =
				htonl(ieee80211_phytype_dsss_dot11_turbo);
			break;
		default:
			fi->phytype = htonl(0xAAAAAAAA);
			break;
		}
		fi->channel = htonl(status->channel);
		rate = ieee80211_get_rate(local, status->phymode,
					  status->rate);
		if (rate) {
			fi->datarate = htonl(rate->rate);
			if (rate->flags & IEEE80211_RATE_PREAMBLE2) {
				if (status->rate == rate->val)
					fi->preamble = htonl(2); /* long */
				else if (status->rate == rate->val2)
					fi->preamble = htonl(1); /* short */
			} else
				fi->preamble = htonl(0);
		} else {
			fi->datarate = htonl(0);
			fi->preamble = htonl(0);
		}

		fi->antenna = htonl(status->antenna);
		fi->priority = htonl(0xffffffff); /* no clue */
		fi->ssi_type = htonl(ieee80211_ssi_raw);
		fi->ssi_signal = htonl(status->ssi);
		fi->ssi_noise = 0x00000000;
		fi->encoding = 0;
	} else {
		/* clear everything because we really don't know.
		 * the msg_type field isn't present on monitor frames
		 * so we don't know whether it will be present or not,
		 * but it's ok to not clear it since it'll be assigned
		 * anyway */
		memset(fi, 0, sizeof(*fi) - sizeof(fi->msg_type));

		fi->ssi_type = htonl(ieee80211_ssi_none);
	}
	fi->version = htonl(IEEE80211_FI_VERSION);
	fi->length = cpu_to_be32(sizeof(*fi) - sizeof(fi->msg_type));
}

/* this routine is actually not just for this, but also
 * for pushing fake 'management' frames into userspace.
 * it shall be replaced by a netlink-based system. */
void
ieee80211_rx_mgmt(struct ieee80211_local *local, struct sk_buff *skb,
		  struct ieee80211_rx_status *status, u32 msg_type)
{
	struct ieee80211_frame_info *fi;
	const size_t hlen = sizeof(struct ieee80211_frame_info);
	struct ieee80211_sub_if_data *sdata;

	skb->dev = local->apdev;

	sdata = IEEE80211_DEV_TO_SUB_IF(local->apdev);

	if (skb_headroom(skb) < hlen) {
		I802_DEBUG_INC(local->rx_expand_skb_head);
		if (pskb_expand_head(skb, hlen, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return;
		}
	}

	fi = (struct ieee80211_frame_info *) skb_push(skb, hlen);

	ieee80211_fill_frame_info(local, fi, status);
	fi->msg_type = htonl(msg_type);

	sdata->stats.rx_packets++;
	sdata->stats.rx_bytes += skb->len;

	skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}

static void
ieee80211_rx_monitor(struct net_device *dev, struct sk_buff *skb,
		     struct ieee80211_rx_status *status)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_rate *rate;
	struct ieee80211_rtap_hdr {
		struct ieee80211_radiotap_header hdr;
		u8 flags;
		u8 rate;
		__le16 chan_freq;
		__le16 chan_flags;
		u8 antsignal;
	} __attribute__ ((packed)) *rthdr;

	skb->dev = dev;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (status->flag & RX_FLAG_RADIOTAP)
		goto out;

	if (skb_headroom(skb) < sizeof(*rthdr)) {
		I802_DEBUG_INC(local->rx_expand_skb_head);
		if (pskb_expand_head(skb, sizeof(*rthdr), 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return;
		}
	}

	rthdr = (struct ieee80211_rtap_hdr *) skb_push(skb, sizeof(*rthdr));
	memset(rthdr, 0, sizeof(*rthdr));
	rthdr->hdr.it_len = cpu_to_le16(sizeof(*rthdr));
	rthdr->hdr.it_present =
		cpu_to_le32((1 << IEEE80211_RADIOTAP_FLAGS) |
			    (1 << IEEE80211_RADIOTAP_RATE) |
			    (1 << IEEE80211_RADIOTAP_CHANNEL) |
			    (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL));
	rthdr->flags = local->hw.flags & IEEE80211_HW_RX_INCLUDES_FCS ?
		       IEEE80211_RADIOTAP_F_FCS : 0;
	rate = ieee80211_get_rate(local, status->phymode, status->rate);
	if (rate)
		rthdr->rate = rate->rate / 5;
	rthdr->chan_freq = cpu_to_le16(status->freq);
	rthdr->chan_flags =
		status->phymode == MODE_IEEE80211A ?
		cpu_to_le16(IEEE80211_CHAN_OFDM | IEEE80211_CHAN_5GHZ) :
		cpu_to_le16(IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ);
	rthdr->antsignal = status->ssi;

 out:
	sdata->stats.rx_packets++;
	sdata->stats.rx_bytes += skb->len;

	skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}

int ieee80211_radar_status(struct ieee80211_hw *hw, int channel,
			   int radar, int radar_type)
{
	struct sk_buff *skb;
	struct ieee80211_radar_info *msg;
	struct ieee80211_local *local = hw_to_local(hw);

	if (!local->apdev)
		return 0;

	skb = dev_alloc_skb(sizeof(struct ieee80211_frame_info) +
			    sizeof(struct ieee80211_radar_info));

	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, sizeof(struct ieee80211_frame_info));

	msg = (struct ieee80211_radar_info *)
		skb_put(skb, sizeof(struct ieee80211_radar_info));
	msg->channel = channel;
	msg->radar = radar;
	msg->radar_type = radar_type;

	ieee80211_rx_mgmt(local, skb, NULL, ieee80211_msg_radar);
	return 0;
}
EXPORT_SYMBOL(ieee80211_radar_status);

int ieee80211_set_aid_for_sta(struct ieee80211_hw *hw, u8 *peer_address,
			      u16 aid)
{
	struct sk_buff *skb;
	struct ieee80211_msg_set_aid_for_sta *msg;
	struct ieee80211_local *local = hw_to_local(hw);

	/* unlikely because if this event only happens for APs,
	 * which require an open ap device. */
	if (unlikely(!local->apdev))
		return 0;

	skb = dev_alloc_skb(sizeof(struct ieee80211_frame_info) +
			    sizeof(struct ieee80211_msg_set_aid_for_sta));

	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, sizeof(struct ieee80211_frame_info));

	msg = (struct ieee80211_msg_set_aid_for_sta *)
		skb_put(skb, sizeof(struct ieee80211_msg_set_aid_for_sta));
	memcpy(msg->sta_address, peer_address, ETH_ALEN);
	msg->aid = aid;

	ieee80211_rx_mgmt(local, skb, NULL, ieee80211_msg_set_aid_for_sta);
	return 0;
}
EXPORT_SYMBOL(ieee80211_set_aid_for_sta);

static void ap_sta_ps_start(struct net_device *dev, struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata;
	sdata = IEEE80211_DEV_TO_SUB_IF(sta->dev);

	if (sdata->bss)
		atomic_inc(&sdata->bss->num_sta_ps);
	sta->flags |= WLAN_STA_PS;
	sta->pspoll = 0;
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	printk(KERN_DEBUG "%s: STA " MAC_FMT " aid %d enters power "
	       "save mode\n", dev->name, MAC_ARG(sta->addr), sta->aid);
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
}


static int ap_sta_ps_end(struct net_device *dev, struct sta_info *sta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	int sent = 0;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_tx_packet_data *pkt_data;

	sdata = IEEE80211_DEV_TO_SUB_IF(sta->dev);
	if (sdata->bss)
		atomic_dec(&sdata->bss->num_sta_ps);
	sta->flags &= ~(WLAN_STA_PS | WLAN_STA_TIM);
	sta->pspoll = 0;
	if (!skb_queue_empty(&sta->ps_tx_buf)) {
		if (local->ops->set_tim)
			local->ops->set_tim(local_to_hw(local), sta->aid, 0);
		if (sdata->bss)
			bss_tim_clear(local, sdata->bss, sta->aid);
	}
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	printk(KERN_DEBUG "%s: STA " MAC_FMT " aid %d exits power "
	       "save mode\n", dev->name, MAC_ARG(sta->addr), sta->aid);
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
	/* Send all buffered frames to the station */
	while ((skb = skb_dequeue(&sta->tx_filtered)) != NULL) {
		pkt_data = (struct ieee80211_tx_packet_data *) skb->cb;
		sent++;
		pkt_data->requeue = 1;
		dev_queue_xmit(skb);
	}
	while ((skb = skb_dequeue(&sta->ps_tx_buf)) != NULL) {
		pkt_data = (struct ieee80211_tx_packet_data *) skb->cb;
		local->total_ps_buffered--;
		sent++;
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		printk(KERN_DEBUG "%s: STA " MAC_FMT " aid %d send PS frame "
		       "since STA not sleeping anymore\n", dev->name,
		       MAC_ARG(sta->addr), sta->aid);
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
		pkt_data->requeue = 1;
		dev_queue_xmit(skb);
	}

	return sent;
}


static ieee80211_txrx_result
ieee80211_rx_h_ps_poll(struct ieee80211_txrx_data *rx)
{
	struct sk_buff *skb;
	int no_pending_pkts;

	if (likely(!rx->sta ||
		   (rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_CTL ||
		   (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_PSPOLL ||
		   !rx->u.rx.ra_match))
		return TXRX_CONTINUE;

	skb = skb_dequeue(&rx->sta->tx_filtered);
	if (!skb) {
		skb = skb_dequeue(&rx->sta->ps_tx_buf);
		if (skb)
			rx->local->total_ps_buffered--;
	}
	no_pending_pkts = skb_queue_empty(&rx->sta->tx_filtered) &&
		skb_queue_empty(&rx->sta->ps_tx_buf);

	if (skb) {
		struct ieee80211_hdr *hdr =
			(struct ieee80211_hdr *) skb->data;

		/* tell TX path to send one frame even though the STA may
		 * still remain is PS mode after this frame exchange */
		rx->sta->pspoll = 1;

#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		printk(KERN_DEBUG "STA " MAC_FMT " aid %d: PS Poll (entries "
		       "after %d)\n",
		       MAC_ARG(rx->sta->addr), rx->sta->aid,
		       skb_queue_len(&rx->sta->ps_tx_buf));
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */

		/* Use MoreData flag to indicate whether there are more
		 * buffered frames for this STA */
		if (no_pending_pkts) {
			hdr->frame_control &= cpu_to_le16(~IEEE80211_FCTL_MOREDATA);
			rx->sta->flags &= ~WLAN_STA_TIM;
		} else
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_MOREDATA);

		dev_queue_xmit(skb);

		if (no_pending_pkts) {
			if (rx->local->ops->set_tim)
				rx->local->ops->set_tim(local_to_hw(rx->local),
						       rx->sta->aid, 0);
			if (rx->sdata->bss)
				bss_tim_clear(rx->local, rx->sdata->bss, rx->sta->aid);
		}
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	} else if (!rx->u.rx.sent_ps_buffered) {
		printk(KERN_DEBUG "%s: STA " MAC_FMT " sent PS Poll even "
		       "though there is no buffered frames for it\n",
		       rx->dev->name, MAC_ARG(rx->sta->addr));
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */

	}

	/* Free PS Poll skb here instead of returning TXRX_DROP that would
	 * count as an dropped frame. */
	dev_kfree_skb(rx->skb);

	return TXRX_QUEUED;
}


static inline struct ieee80211_fragment_entry *
ieee80211_reassemble_add(struct ieee80211_sub_if_data *sdata,
			 unsigned int frag, unsigned int seq, int rx_queue,
			 struct sk_buff **skb)
{
	struct ieee80211_fragment_entry *entry;
	int idx;

	idx = sdata->fragment_next;
	entry = &sdata->fragments[sdata->fragment_next++];
	if (sdata->fragment_next >= IEEE80211_FRAGMENT_MAX)
		sdata->fragment_next = 0;

	if (!skb_queue_empty(&entry->skb_list)) {
#ifdef CONFIG_MAC80211_DEBUG
		struct ieee80211_hdr *hdr =
			(struct ieee80211_hdr *) entry->skb_list.next->data;
		printk(KERN_DEBUG "%s: RX reassembly removed oldest "
		       "fragment entry (idx=%d age=%lu seq=%d last_frag=%d "
		       "addr1=" MAC_FMT " addr2=" MAC_FMT "\n",
		       sdata->dev->name, idx,
		       jiffies - entry->first_frag_time, entry->seq,
		       entry->last_frag, MAC_ARG(hdr->addr1),
		       MAC_ARG(hdr->addr2));
#endif /* CONFIG_MAC80211_DEBUG */
		__skb_queue_purge(&entry->skb_list);
	}

	__skb_queue_tail(&entry->skb_list, *skb); /* no need for locking */
	*skb = NULL;
	entry->first_frag_time = jiffies;
	entry->seq = seq;
	entry->rx_queue = rx_queue;
	entry->last_frag = frag;
	entry->ccmp = 0;
	entry->extra_len = 0;

	return entry;
}


static inline struct ieee80211_fragment_entry *
ieee80211_reassemble_find(struct ieee80211_sub_if_data *sdata,
			  u16 fc, unsigned int frag, unsigned int seq,
			  int rx_queue, struct ieee80211_hdr *hdr)
{
	struct ieee80211_fragment_entry *entry;
	int i, idx;

	idx = sdata->fragment_next;
	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++) {
		struct ieee80211_hdr *f_hdr;
		u16 f_fc;

		idx--;
		if (idx < 0)
			idx = IEEE80211_FRAGMENT_MAX - 1;

		entry = &sdata->fragments[idx];
		if (skb_queue_empty(&entry->skb_list) || entry->seq != seq ||
		    entry->rx_queue != rx_queue ||
		    entry->last_frag + 1 != frag)
			continue;

		f_hdr = (struct ieee80211_hdr *) entry->skb_list.next->data;
		f_fc = le16_to_cpu(f_hdr->frame_control);

		if ((fc & IEEE80211_FCTL_FTYPE) != (f_fc & IEEE80211_FCTL_FTYPE) ||
		    compare_ether_addr(hdr->addr1, f_hdr->addr1) != 0 ||
		    compare_ether_addr(hdr->addr2, f_hdr->addr2) != 0)
			continue;

		if (entry->first_frag_time + 2 * HZ < jiffies) {
			__skb_queue_purge(&entry->skb_list);
			continue;
		}
		return entry;
	}

	return NULL;
}


static ieee80211_txrx_result
ieee80211_rx_h_defragment(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_hdr *hdr;
	u16 sc;
	unsigned int frag, seq;
	struct ieee80211_fragment_entry *entry;
	struct sk_buff *skb;

	hdr = (struct ieee80211_hdr *) rx->skb->data;
	sc = le16_to_cpu(hdr->seq_ctrl);
	frag = sc & IEEE80211_SCTL_FRAG;

	if (likely((!(rx->fc & IEEE80211_FCTL_MOREFRAGS) && frag == 0) ||
		   (rx->skb)->len < 24 ||
		   is_multicast_ether_addr(hdr->addr1))) {
		/* not fragmented */
		goto out;
	}
	I802_DEBUG_INC(rx->local->rx_handlers_fragments);

	seq = (sc & IEEE80211_SCTL_SEQ) >> 4;

	if (frag == 0) {
		/* This is the first fragment of a new frame. */
		entry = ieee80211_reassemble_add(rx->sdata, frag, seq,
						 rx->u.rx.queue, &(rx->skb));
		if (rx->key && rx->key->alg == ALG_CCMP &&
		    (rx->fc & IEEE80211_FCTL_PROTECTED)) {
			/* Store CCMP PN so that we can verify that the next
			 * fragment has a sequential PN value. */
			entry->ccmp = 1;
			memcpy(entry->last_pn,
			       rx->key->u.ccmp.rx_pn[rx->u.rx.queue],
			       CCMP_PN_LEN);
		}
		return TXRX_QUEUED;
	}

	/* This is a fragment for a frame that should already be pending in
	 * fragment cache. Add this fragment to the end of the pending entry.
	 */
	entry = ieee80211_reassemble_find(rx->sdata, rx->fc, frag, seq,
					  rx->u.rx.queue, hdr);
	if (!entry) {
		I802_DEBUG_INC(rx->local->rx_handlers_drop_defrag);
		return TXRX_DROP;
	}

	/* Verify that MPDUs within one MSDU have sequential PN values.
	 * (IEEE 802.11i, 8.3.3.4.5) */
	if (entry->ccmp) {
		int i;
		u8 pn[CCMP_PN_LEN], *rpn;
		if (!rx->key || rx->key->alg != ALG_CCMP)
			return TXRX_DROP;
		memcpy(pn, entry->last_pn, CCMP_PN_LEN);
		for (i = CCMP_PN_LEN - 1; i >= 0; i--) {
			pn[i]++;
			if (pn[i])
				break;
		}
		rpn = rx->key->u.ccmp.rx_pn[rx->u.rx.queue];
		if (memcmp(pn, rpn, CCMP_PN_LEN) != 0) {
			printk(KERN_DEBUG "%s: defrag: CCMP PN not sequential"
			       " A2=" MAC_FMT " PN=%02x%02x%02x%02x%02x%02x "
			       "(expected %02x%02x%02x%02x%02x%02x)\n",
			       rx->dev->name, MAC_ARG(hdr->addr2),
			       rpn[0], rpn[1], rpn[2], rpn[3], rpn[4], rpn[5],
			       pn[0], pn[1], pn[2], pn[3], pn[4], pn[5]);
			return TXRX_DROP;
		}
		memcpy(entry->last_pn, pn, CCMP_PN_LEN);
	}

	skb_pull(rx->skb, ieee80211_get_hdrlen(rx->fc));
	__skb_queue_tail(&entry->skb_list, rx->skb);
	entry->last_frag = frag;
	entry->extra_len += rx->skb->len;
	if (rx->fc & IEEE80211_FCTL_MOREFRAGS) {
		rx->skb = NULL;
		return TXRX_QUEUED;
	}

	rx->skb = __skb_dequeue(&entry->skb_list);
	if (skb_tailroom(rx->skb) < entry->extra_len) {
		I802_DEBUG_INC(rx->local->rx_expand_skb_head2);
		if (unlikely(pskb_expand_head(rx->skb, 0, entry->extra_len,
					      GFP_ATOMIC))) {
			I802_DEBUG_INC(rx->local->rx_handlers_drop_defrag);
			__skb_queue_purge(&entry->skb_list);
			return TXRX_DROP;
		}
	}
	while ((skb = __skb_dequeue(&entry->skb_list)))
		memcpy(skb_put(rx->skb, skb->len), skb->data, skb->len);

	/* Complete frame has been reassembled - process it now */
	rx->fragmented = 1;

 out:
	if (rx->sta)
		rx->sta->rx_packets++;
	if (is_multicast_ether_addr(hdr->addr1))
		rx->local->dot11MulticastReceivedFrameCount++;
	else
		ieee80211_led_rx(rx->local);
	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_rx_h_monitor(struct ieee80211_txrx_data *rx)
{
	if (rx->sdata->type == IEEE80211_IF_TYPE_MNTR) {
		ieee80211_rx_monitor(rx->dev, rx->skb, rx->u.rx.status);
		return TXRX_QUEUED;
	}

	if (rx->u.rx.status->flag & RX_FLAG_RADIOTAP)
		skb_pull(rx->skb, ieee80211_get_radiotap_len(rx->skb));

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_rx_h_check(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_hdr *hdr;
	int always_sta_key;
	hdr = (struct ieee80211_hdr *) rx->skb->data;

	/* Drop duplicate 802.11 retransmissions (IEEE 802.11 Chap. 9.2.9) */
	if (rx->sta && !is_multicast_ether_addr(hdr->addr1)) {
		if (unlikely(rx->fc & IEEE80211_FCTL_RETRY &&
			     rx->sta->last_seq_ctrl[rx->u.rx.queue] ==
			     hdr->seq_ctrl)) {
			if (rx->u.rx.ra_match) {
				rx->local->dot11FrameDuplicateCount++;
				rx->sta->num_duplicates++;
			}
			return TXRX_DROP;
		} else
			rx->sta->last_seq_ctrl[rx->u.rx.queue] = hdr->seq_ctrl;
	}

	if ((rx->local->hw.flags & IEEE80211_HW_RX_INCLUDES_FCS) &&
	    rx->skb->len > FCS_LEN)
		skb_trim(rx->skb, rx->skb->len - FCS_LEN);

	if (unlikely(rx->skb->len < 16)) {
		I802_DEBUG_INC(rx->local->rx_handlers_drop_short);
		return TXRX_DROP;
	}

	if (!rx->u.rx.ra_match)
		rx->skb->pkt_type = PACKET_OTHERHOST;
	else if (compare_ether_addr(rx->dev->dev_addr, hdr->addr1) == 0)
		rx->skb->pkt_type = PACKET_HOST;
	else if (is_multicast_ether_addr(hdr->addr1)) {
		if (is_broadcast_ether_addr(hdr->addr1))
			rx->skb->pkt_type = PACKET_BROADCAST;
		else
			rx->skb->pkt_type = PACKET_MULTICAST;
	} else
		rx->skb->pkt_type = PACKET_OTHERHOST;

	/* Drop disallowed frame classes based on STA auth/assoc state;
	 * IEEE 802.11, Chap 5.5.
	 *
	 * 80211.o does filtering only based on association state, i.e., it
	 * drops Class 3 frames from not associated stations. hostapd sends
	 * deauth/disassoc frames when needed. In addition, hostapd is
	 * responsible for filtering on both auth and assoc states.
	 */
	if (unlikely(((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA ||
		      ((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL &&
		       (rx->fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PSPOLL)) &&
		     rx->sdata->type != IEEE80211_IF_TYPE_IBSS &&
		     (!rx->sta || !(rx->sta->flags & WLAN_STA_ASSOC)))) {
		if ((!(rx->fc & IEEE80211_FCTL_FROMDS) &&
		     !(rx->fc & IEEE80211_FCTL_TODS) &&
		     (rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA)
		    || !rx->u.rx.ra_match) {
			/* Drop IBSS frames and frames for other hosts
			 * silently. */
			return TXRX_DROP;
		}

		if (!rx->local->apdev)
			return TXRX_DROP;

		ieee80211_rx_mgmt(rx->local, rx->skb, rx->u.rx.status,
				  ieee80211_msg_sta_not_assoc);
		return TXRX_QUEUED;
	}

	if (rx->sdata->type == IEEE80211_IF_TYPE_STA)
		always_sta_key = 0;
	else
		always_sta_key = 1;

	if (rx->sta && rx->sta->key && always_sta_key) {
		rx->key = rx->sta->key;
	} else {
		if (rx->sta && rx->sta->key)
			rx->key = rx->sta->key;
		else
			rx->key = rx->sdata->default_key;

		if ((rx->local->hw.flags & IEEE80211_HW_WEP_INCLUDE_IV) &&
		    rx->fc & IEEE80211_FCTL_PROTECTED) {
			int keyidx = ieee80211_wep_get_keyidx(rx->skb);

			if (keyidx >= 0 && keyidx < NUM_DEFAULT_KEYS &&
			    (!rx->sta || !rx->sta->key || keyidx > 0))
				rx->key = rx->sdata->keys[keyidx];

			if (!rx->key) {
				if (!rx->u.rx.ra_match)
					return TXRX_DROP;
				printk(KERN_DEBUG "%s: RX WEP frame with "
				       "unknown keyidx %d (A1=" MAC_FMT " A2="
				       MAC_FMT " A3=" MAC_FMT ")\n",
				       rx->dev->name, keyidx,
				       MAC_ARG(hdr->addr1),
				       MAC_ARG(hdr->addr2),
				       MAC_ARG(hdr->addr3));
				if (!rx->local->apdev)
					return TXRX_DROP;
				ieee80211_rx_mgmt(
					rx->local, rx->skb, rx->u.rx.status,
					ieee80211_msg_wep_frame_unknown_key);
				return TXRX_QUEUED;
			}
		}
	}

	if (rx->fc & IEEE80211_FCTL_PROTECTED && rx->key && rx->u.rx.ra_match) {
		rx->key->tx_rx_count++;
		if (unlikely(rx->local->key_tx_rx_threshold &&
			     rx->key->tx_rx_count >
			     rx->local->key_tx_rx_threshold)) {
			ieee80211_key_threshold_notify(rx->dev, rx->key,
						       rx->sta);
		}
	}

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_rx_h_sta_process(struct ieee80211_txrx_data *rx)
{
	struct sta_info *sta = rx->sta;
	struct net_device *dev = rx->dev;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;

	if (!sta)
		return TXRX_CONTINUE;

	/* Update last_rx only for IBSS packets which are for the current
	 * BSSID to avoid keeping the current IBSS network alive in cases where
	 * other STAs are using different BSSID. */
	if (rx->sdata->type == IEEE80211_IF_TYPE_IBSS) {
		u8 *bssid = ieee80211_get_bssid(hdr, rx->skb->len);
		if (compare_ether_addr(bssid, rx->sdata->u.sta.bssid) == 0)
			sta->last_rx = jiffies;
	} else
	if (!is_multicast_ether_addr(hdr->addr1) ||
	    rx->sdata->type == IEEE80211_IF_TYPE_STA) {
		/* Update last_rx only for unicast frames in order to prevent
		 * the Probe Request frames (the only broadcast frames from a
		 * STA in infrastructure mode) from keeping a connection alive.
		 */
		sta->last_rx = jiffies;
	}

	if (!rx->u.rx.ra_match)
		return TXRX_CONTINUE;

	sta->rx_fragments++;
	sta->rx_bytes += rx->skb->len;
	sta->last_rssi = (sta->last_rssi * 15 +
			  rx->u.rx.status->ssi) / 16;
	sta->last_signal = (sta->last_signal * 15 +
			    rx->u.rx.status->signal) / 16;
	sta->last_noise = (sta->last_noise * 15 +
			   rx->u.rx.status->noise) / 16;

	if (!(rx->fc & IEEE80211_FCTL_MOREFRAGS)) {
		/* Change STA power saving mode only in the end of a frame
		 * exchange sequence */
		if ((sta->flags & WLAN_STA_PS) && !(rx->fc & IEEE80211_FCTL_PM))
			rx->u.rx.sent_ps_buffered += ap_sta_ps_end(dev, sta);
		else if (!(sta->flags & WLAN_STA_PS) &&
			 (rx->fc & IEEE80211_FCTL_PM))
			ap_sta_ps_start(dev, sta);
	}

	/* Drop data::nullfunc frames silently, since they are used only to
	 * control station power saving mode. */
	if ((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA &&
	    (rx->fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_NULLFUNC) {
		I802_DEBUG_INC(rx->local->rx_handlers_drop_nullfunc);
		/* Update counter and free packet here to avoid counting this
		 * as a dropped packed. */
		sta->rx_packets++;
		dev_kfree_skb(rx->skb);
		return TXRX_QUEUED;
	}

	return TXRX_CONTINUE;
} /* ieee80211_rx_h_sta_process */


static ieee80211_txrx_result
ieee80211_rx_h_wep_weak_iv_detection(struct ieee80211_txrx_data *rx)
{
	if (!rx->sta || !(rx->fc & IEEE80211_FCTL_PROTECTED) ||
	    (rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA ||
	    !rx->key || rx->key->alg != ALG_WEP || !rx->u.rx.ra_match)
		return TXRX_CONTINUE;

	/* Check for weak IVs, if hwaccel did not remove IV from the frame */
	if ((rx->local->hw.flags & IEEE80211_HW_WEP_INCLUDE_IV) ||
	    rx->key->force_sw_encrypt) {
		u8 *iv = ieee80211_wep_is_weak_iv(rx->skb, rx->key);
		if (iv) {
			rx->sta->wep_weak_iv_count++;
		}
	}

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_rx_h_wep_decrypt(struct ieee80211_txrx_data *rx)
{
	/* If the device handles decryption totally, skip this test */
	if (rx->local->hw.flags & IEEE80211_HW_DEVICE_HIDES_WEP)
		return TXRX_CONTINUE;

	if ((rx->key && rx->key->alg != ALG_WEP) ||
	    !(rx->fc & IEEE80211_FCTL_PROTECTED) ||
	    ((rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA &&
	     ((rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_MGMT ||
	      (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_AUTH)))
		return TXRX_CONTINUE;

	if (!rx->key) {
		printk(KERN_DEBUG "%s: RX WEP frame, but no key set\n",
		       rx->dev->name);
		return TXRX_DROP;
	}

	if (!(rx->u.rx.status->flag & RX_FLAG_DECRYPTED) ||
	    rx->key->force_sw_encrypt) {
		if (ieee80211_wep_decrypt(rx->local, rx->skb, rx->key)) {
			printk(KERN_DEBUG "%s: RX WEP frame, decrypt "
			       "failed\n", rx->dev->name);
			return TXRX_DROP;
		}
	} else if (rx->local->hw.flags & IEEE80211_HW_WEP_INCLUDE_IV) {
		ieee80211_wep_remove_iv(rx->local, rx->skb, rx->key);
		/* remove ICV */
		skb_trim(rx->skb, rx->skb->len - 4);
	}

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_rx_h_802_1x_pae(struct ieee80211_txrx_data *rx)
{
	if (rx->sdata->eapol && ieee80211_is_eapol(rx->skb) &&
	    rx->sdata->type != IEEE80211_IF_TYPE_STA && rx->u.rx.ra_match) {
		/* Pass both encrypted and unencrypted EAPOL frames to user
		 * space for processing. */
		if (!rx->local->apdev)
			return TXRX_DROP;
		ieee80211_rx_mgmt(rx->local, rx->skb, rx->u.rx.status,
				  ieee80211_msg_normal);
		return TXRX_QUEUED;
	}

	if (unlikely(rx->sdata->ieee802_1x &&
		     (rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA &&
		     (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_NULLFUNC &&
		     (!rx->sta || !(rx->sta->flags & WLAN_STA_AUTHORIZED)) &&
		     !ieee80211_is_eapol(rx->skb))) {
#ifdef CONFIG_MAC80211_DEBUG
		struct ieee80211_hdr *hdr =
			(struct ieee80211_hdr *) rx->skb->data;
		printk(KERN_DEBUG "%s: dropped frame from " MAC_FMT
		       " (unauthorized port)\n", rx->dev->name,
		       MAC_ARG(hdr->addr2));
#endif /* CONFIG_MAC80211_DEBUG */
		return TXRX_DROP;
	}

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_rx_h_drop_unencrypted(struct ieee80211_txrx_data *rx)
{
	/*  If the device handles decryption totally, skip this test */
	if (rx->local->hw.flags & IEEE80211_HW_DEVICE_HIDES_WEP)
		return TXRX_CONTINUE;

	/* Drop unencrypted frames if key is set. */
	if (unlikely(!(rx->fc & IEEE80211_FCTL_PROTECTED) &&
		     (rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA &&
		     (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_NULLFUNC &&
		     (rx->key || rx->sdata->drop_unencrypted) &&
		     (rx->sdata->eapol == 0 ||
		      !ieee80211_is_eapol(rx->skb)))) {
		printk(KERN_DEBUG "%s: RX non-WEP frame, but expected "
		       "encryption\n", rx->dev->name);
		return TXRX_DROP;
	}
	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_rx_h_mgmt(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_sub_if_data *sdata;

	if (!rx->u.rx.ra_match)
		return TXRX_DROP;

	sdata = IEEE80211_DEV_TO_SUB_IF(rx->dev);
	if ((sdata->type == IEEE80211_IF_TYPE_STA ||
	     sdata->type == IEEE80211_IF_TYPE_IBSS) &&
	    !rx->local->user_space_mlme) {
		ieee80211_sta_rx_mgmt(rx->dev, rx->skb, rx->u.rx.status);
	} else {
		/* Management frames are sent to hostapd for processing */
		if (!rx->local->apdev)
			return TXRX_DROP;
		ieee80211_rx_mgmt(rx->local, rx->skb, rx->u.rx.status,
				  ieee80211_msg_normal);
	}
	return TXRX_QUEUED;
}


static ieee80211_txrx_result
ieee80211_rx_h_passive_scan(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_local *local = rx->local;
	struct sk_buff *skb = rx->skb;

	if (unlikely(local->sta_scanning != 0)) {
		ieee80211_sta_rx_scan(rx->dev, skb, rx->u.rx.status);
		return TXRX_QUEUED;
	}

	if (unlikely(rx->u.rx.in_scan)) {
		/* scanning finished during invoking of handlers */
		I802_DEBUG_INC(local->rx_handlers_drop_passive_scan);
		return TXRX_DROP;
	}

	return TXRX_CONTINUE;
}


static void ieee80211_rx_michael_mic_report(struct net_device *dev,
					    struct ieee80211_hdr *hdr,
					    struct sta_info *sta,
					    struct ieee80211_txrx_data *rx)
{
	int keyidx, hdrlen;

	hdrlen = ieee80211_get_hdrlen_from_skb(rx->skb);
	if (rx->skb->len >= hdrlen + 4)
		keyidx = rx->skb->data[hdrlen + 3] >> 6;
	else
		keyidx = -1;

	/* TODO: verify that this is not triggered by fragmented
	 * frames (hw does not verify MIC for them). */
	printk(KERN_DEBUG "%s: TKIP hwaccel reported Michael MIC "
	       "failure from " MAC_FMT " to " MAC_FMT " keyidx=%d\n",
	       dev->name, MAC_ARG(hdr->addr2), MAC_ARG(hdr->addr1), keyidx);

	if (!sta) {
		/* Some hardware versions seem to generate incorrect
		 * Michael MIC reports; ignore them to avoid triggering
		 * countermeasures. */
		printk(KERN_DEBUG "%s: ignored spurious Michael MIC "
		       "error for unknown address " MAC_FMT "\n",
		       dev->name, MAC_ARG(hdr->addr2));
		goto ignore;
	}

	if (!(rx->fc & IEEE80211_FCTL_PROTECTED)) {
		printk(KERN_DEBUG "%s: ignored spurious Michael MIC "
		       "error for a frame with no ISWEP flag (src "
		       MAC_FMT ")\n", dev->name, MAC_ARG(hdr->addr2));
		goto ignore;
	}

	if ((rx->local->hw.flags & IEEE80211_HW_WEP_INCLUDE_IV) &&
	    rx->sdata->type == IEEE80211_IF_TYPE_AP) {
		keyidx = ieee80211_wep_get_keyidx(rx->skb);
		/* AP with Pairwise keys support should never receive Michael
		 * MIC errors for non-zero keyidx because these are reserved
		 * for group keys and only the AP is sending real multicast
		 * frames in BSS. */
		if (keyidx) {
			printk(KERN_DEBUG "%s: ignored Michael MIC error for "
			       "a frame with non-zero keyidx (%d) (src " MAC_FMT
			       ")\n", dev->name, keyidx, MAC_ARG(hdr->addr2));
			goto ignore;
		}
	}

	if ((rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA &&
	    ((rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_MGMT ||
	     (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_AUTH)) {
		printk(KERN_DEBUG "%s: ignored spurious Michael MIC "
		       "error for a frame that cannot be encrypted "
		       "(fc=0x%04x) (src " MAC_FMT ")\n",
		       dev->name, rx->fc, MAC_ARG(hdr->addr2));
		goto ignore;
	}

	do {
		union iwreq_data wrqu;
		char *buf = kmalloc(128, GFP_ATOMIC);
		if (!buf)
			break;

		/* TODO: needed parameters: count, key type, TSC */
		sprintf(buf, "MLME-MICHAELMICFAILURE.indication("
			"keyid=%d %scast addr=" MAC_FMT ")",
			keyidx, hdr->addr1[0] & 0x01 ? "broad" : "uni",
			MAC_ARG(hdr->addr2));
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.data.length = strlen(buf);
		wireless_send_event(rx->dev, IWEVCUSTOM, &wrqu, buf);
		kfree(buf);
	} while (0);

	/* TODO: consider verifying the MIC error report with software
	 * implementation if we get too many spurious reports from the
	 * hardware. */
	if (!rx->local->apdev)
		goto ignore;
	ieee80211_rx_mgmt(rx->local, rx->skb, rx->u.rx.status,
			  ieee80211_msg_michael_mic_failure);
	return;

 ignore:
	dev_kfree_skb(rx->skb);
	rx->skb = NULL;
}

static inline ieee80211_txrx_result __ieee80211_invoke_rx_handlers(
				struct ieee80211_local *local,
				ieee80211_rx_handler *handlers,
				struct ieee80211_txrx_data *rx,
				struct sta_info *sta)
{
	ieee80211_rx_handler *handler;
	ieee80211_txrx_result res = TXRX_DROP;

	for (handler = handlers; *handler != NULL; handler++) {
		res = (*handler)(rx);
		if (res != TXRX_CONTINUE) {
			if (res == TXRX_DROP) {
				I802_DEBUG_INC(local->rx_handlers_drop);
				if (sta)
					sta->rx_dropped++;
			}
			if (res == TXRX_QUEUED)
				I802_DEBUG_INC(local->rx_handlers_queued);
			break;
		}
	}

	if (res == TXRX_DROP) {
		dev_kfree_skb(rx->skb);
	}
	return res;
}

static inline void ieee80211_invoke_rx_handlers(struct ieee80211_local *local,
						ieee80211_rx_handler *handlers,
						struct ieee80211_txrx_data *rx,
						struct sta_info *sta)
{
	if (__ieee80211_invoke_rx_handlers(local, handlers, rx, sta) ==
	    TXRX_CONTINUE)
		dev_kfree_skb(rx->skb);
}

/*
 * This is the receive path handler. It is called by a low level driver when an
 * 802.11 MPDU is received from the hardware.
 */
void __ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb,
		    struct ieee80211_rx_status *status)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	struct ieee80211_hdr *hdr;
	struct ieee80211_txrx_data rx;
	u16 type;
	int multicast;
	int radiotap_len = 0;

	if (status->flag & RX_FLAG_RADIOTAP) {
		radiotap_len = ieee80211_get_radiotap_len(skb);
		skb_pull(skb, radiotap_len);
	}

	hdr = (struct ieee80211_hdr *) skb->data;
	memset(&rx, 0, sizeof(rx));
	rx.skb = skb;
	rx.local = local;

	rx.u.rx.status = status;
	rx.fc = skb->len >= 2 ? le16_to_cpu(hdr->frame_control) : 0;
	type = rx.fc & IEEE80211_FCTL_FTYPE;
	if (type == IEEE80211_FTYPE_DATA || type == IEEE80211_FTYPE_MGMT)
		local->dot11ReceivedFragmentCount++;
	multicast = is_multicast_ether_addr(hdr->addr1);

	if (skb->len >= 16)
		sta = rx.sta = sta_info_get(local, hdr->addr2);
	else
		sta = rx.sta = NULL;

	if (sta) {
		rx.dev = sta->dev;
		rx.sdata = IEEE80211_DEV_TO_SUB_IF(rx.dev);
	}

	if ((status->flag & RX_FLAG_MMIC_ERROR)) {
		ieee80211_rx_michael_mic_report(local->mdev, hdr, sta, &rx);
		goto end;
	}

	if (unlikely(local->sta_scanning))
		rx.u.rx.in_scan = 1;

	if (__ieee80211_invoke_rx_handlers(local, local->rx_pre_handlers, &rx,
					   sta) != TXRX_CONTINUE)
		goto end;
	skb = rx.skb;

	skb_push(skb, radiotap_len);
	if (sta && !sta->assoc_ap && !(sta->flags & WLAN_STA_WDS) &&
	    !local->iff_promiscs && !multicast) {
		rx.u.rx.ra_match = 1;
		ieee80211_invoke_rx_handlers(local, local->rx_handlers, &rx,
					     sta);
	} else {
		struct ieee80211_sub_if_data *prev = NULL;
		struct sk_buff *skb_new;
		u8 *bssid = ieee80211_get_bssid(hdr, skb->len - radiotap_len);

		read_lock(&local->sub_if_lock);
		list_for_each_entry(sdata, &local->sub_if_list, list) {
			rx.u.rx.ra_match = 1;
			switch (sdata->type) {
			case IEEE80211_IF_TYPE_STA:
				if (!bssid)
					continue;
				if (!ieee80211_bssid_match(bssid,
							sdata->u.sta.bssid)) {
					if (!rx.u.rx.in_scan)
						continue;
					rx.u.rx.ra_match = 0;
				} else if (!multicast &&
					   compare_ether_addr(sdata->dev->dev_addr,
							      hdr->addr1) != 0) {
					if (!sdata->promisc)
						continue;
					rx.u.rx.ra_match = 0;
				}
				break;
			case IEEE80211_IF_TYPE_IBSS:
				if (!bssid)
					continue;
				if (!ieee80211_bssid_match(bssid,
							sdata->u.sta.bssid)) {
					if (!rx.u.rx.in_scan)
						continue;
					rx.u.rx.ra_match = 0;
				} else if (!multicast &&
					   compare_ether_addr(sdata->dev->dev_addr,
							      hdr->addr1) != 0) {
					if (!sdata->promisc)
						continue;
					rx.u.rx.ra_match = 0;
				} else if (!sta)
					sta = rx.sta =
						ieee80211_ibss_add_sta(sdata->dev,
								       skb, bssid,
								       hdr->addr2);
				break;
			case IEEE80211_IF_TYPE_AP:
				if (!bssid) {
					if (compare_ether_addr(sdata->dev->dev_addr,
							       hdr->addr1) != 0)
						continue;
				} else if (!ieee80211_bssid_match(bssid,
							sdata->dev->dev_addr)) {
					if (!rx.u.rx.in_scan)
						continue;
					rx.u.rx.ra_match = 0;
				}
				if (sdata->dev == local->mdev &&
				    !rx.u.rx.in_scan)
					/* do not receive anything via
					 * master device when not scanning */
					continue;
				break;
			case IEEE80211_IF_TYPE_WDS:
				if (bssid ||
				    (rx.fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA)
					continue;
				if (compare_ether_addr(sdata->u.wds.remote_addr,
						       hdr->addr2) != 0)
					continue;
				break;
			}

			if (prev) {
				skb_new = skb_copy(skb, GFP_ATOMIC);
				if (!skb_new) {
					if (net_ratelimit())
						printk(KERN_DEBUG "%s: failed to copy "
						       "multicast frame for %s",
						       local->mdev->name, prev->dev->name);
					continue;
				}
				rx.skb = skb_new;
				rx.dev = prev->dev;
				rx.sdata = prev;
				ieee80211_invoke_rx_handlers(local,
							     local->rx_handlers,
							     &rx, sta);
			}
			prev = sdata;
		}
		if (prev) {
			rx.skb = skb;
			rx.dev = prev->dev;
			rx.sdata = prev;
			ieee80211_invoke_rx_handlers(local, local->rx_handlers,
						     &rx, sta);
		} else
			dev_kfree_skb(skb);
		read_unlock(&local->sub_if_lock);
	}

  end:
	if (sta)
		sta_info_put(sta);
}
EXPORT_SYMBOL(__ieee80211_rx);

static ieee80211_txrx_result
ieee80211_tx_h_load_stats(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_local *local = tx->local;
	struct ieee80211_hw_mode *mode = tx->u.tx.mode;
	struct sk_buff *skb = tx->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u32 load = 0, hdrtime;

	/* TODO: this could be part of tx_status handling, so that the number
	 * of retries would be known; TX rate should in that case be stored
	 * somewhere with the packet */

	/* Estimate total channel use caused by this frame */

	/* 1 bit at 1 Mbit/s takes 1 usec; in channel_use values,
	 * 1 usec = 1/8 * (1080 / 10) = 13.5 */

	if (mode->mode == MODE_IEEE80211A ||
	    mode->mode == MODE_ATHEROS_TURBO ||
	    mode->mode == MODE_ATHEROS_TURBOG ||
	    (mode->mode == MODE_IEEE80211G &&
	     tx->u.tx.rate->flags & IEEE80211_RATE_ERP))
		hdrtime = CHAN_UTIL_HDR_SHORT;
	else
		hdrtime = CHAN_UTIL_HDR_LONG;

	load = hdrtime;
	if (!is_multicast_ether_addr(hdr->addr1))
		load += hdrtime;

	if (tx->u.tx.control->flags & IEEE80211_TXCTL_USE_RTS_CTS)
		load += 2 * hdrtime;
	else if (tx->u.tx.control->flags & IEEE80211_TXCTL_USE_CTS_PROTECT)
		load += hdrtime;

	load += skb->len * tx->u.tx.rate->rate_inv;

	if (tx->u.tx.extra_frag) {
		int i;
		for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
			load += 2 * hdrtime;
			load += tx->u.tx.extra_frag[i]->len *
				tx->u.tx.rate->rate;
		}
	}

	/* Divide channel_use by 8 to avoid wrapping around the counter */
	load >>= CHAN_UTIL_SHIFT;
	local->channel_use_raw += load;
	if (tx->sta)
		tx->sta->channel_use_raw += load;
	tx->sdata->channel_use_raw += load;

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_rx_h_load_stats(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_local *local = rx->local;
	struct sk_buff *skb = rx->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u32 load = 0, hdrtime;
	struct ieee80211_rate *rate;
	struct ieee80211_hw_mode *mode = local->hw.conf.mode;
	int i;

	/* Estimate total channel use caused by this frame */

	if (unlikely(mode->num_rates < 0))
		return TXRX_CONTINUE;

	rate = &mode->rates[0];
	for (i = 0; i < mode->num_rates; i++) {
		if (mode->rates[i].val == rx->u.rx.status->rate) {
			rate = &mode->rates[i];
			break;
		}
	}

	/* 1 bit at 1 Mbit/s takes 1 usec; in channel_use values,
	 * 1 usec = 1/8 * (1080 / 10) = 13.5 */

	if (mode->mode == MODE_IEEE80211A ||
	    mode->mode == MODE_ATHEROS_TURBO ||
	    mode->mode == MODE_ATHEROS_TURBOG ||
	    (mode->mode == MODE_IEEE80211G &&
	     rate->flags & IEEE80211_RATE_ERP))
		hdrtime = CHAN_UTIL_HDR_SHORT;
	else
		hdrtime = CHAN_UTIL_HDR_LONG;

	load = hdrtime;
	if (!is_multicast_ether_addr(hdr->addr1))
		load += hdrtime;

	load += skb->len * rate->rate_inv;

	/* Divide channel_use by 8 to avoid wrapping around the counter */
	load >>= CHAN_UTIL_SHIFT;
	local->channel_use_raw += load;
	if (rx->sta)
		rx->sta->channel_use_raw += load;
	rx->u.rx.load = load;

	return TXRX_CONTINUE;
}

static ieee80211_txrx_result
ieee80211_rx_h_if_stats(struct ieee80211_txrx_data *rx)
{
	rx->sdata->channel_use_raw += rx->u.rx.load;
	return TXRX_CONTINUE;
}

static void ieee80211_stat_refresh(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata;

	if (!local->stat_time)
		return;

	/* go through all stations */
	spin_lock_bh(&local->sta_lock);
	list_for_each_entry(sta, &local->sta_list, list) {
		sta->channel_use = (sta->channel_use_raw / local->stat_time) /
			CHAN_UTIL_PER_10MS;
		sta->channel_use_raw = 0;
	}
	spin_unlock_bh(&local->sta_lock);

	/* go through all subinterfaces */
	read_lock(&local->sub_if_lock);
	list_for_each_entry(sdata, &local->sub_if_list, list) {
		sdata->channel_use = (sdata->channel_use_raw /
				      local->stat_time) / CHAN_UTIL_PER_10MS;
		sdata->channel_use_raw = 0;
	}
	read_unlock(&local->sub_if_lock);

	/* hardware interface */
	local->channel_use = (local->channel_use_raw /
			      local->stat_time) / CHAN_UTIL_PER_10MS;
	local->channel_use_raw = 0;

	local->stat_timer.expires = jiffies + HZ * local->stat_time / 100;
	add_timer(&local->stat_timer);
}


/* This is a version of the rx handler that can be called from hard irq
 * context. Post the skb on the queue and schedule the tasklet */
void ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb,
			  struct ieee80211_rx_status *status)
{
	struct ieee80211_local *local = hw_to_local(hw);

	BUILD_BUG_ON(sizeof(struct ieee80211_rx_status) > sizeof(skb->cb));

	skb->dev = local->mdev;
	/* copy status into skb->cb for use by tasklet */
	memcpy(skb->cb, status, sizeof(*status));
	skb->pkt_type = IEEE80211_RX_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_rx_irqsafe);

void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw,
				 struct sk_buff *skb,
				 struct ieee80211_tx_status *status)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_status *saved;
	int tmp;

	skb->dev = local->mdev;
	saved = kmalloc(sizeof(struct ieee80211_tx_status), GFP_ATOMIC);
	if (unlikely(!saved)) {
		if (net_ratelimit())
			printk(KERN_WARNING "%s: Not enough memory, "
			       "dropping tx status", skb->dev->name);
		/* should be dev_kfree_skb_irq, but due to this function being
		 * named _irqsafe instead of just _irq we can't be sure that
		 * people won't call it from non-irq contexts */
		dev_kfree_skb_any(skb);
		return;
	}
	memcpy(saved, status, sizeof(struct ieee80211_tx_status));
	/* copy pointer to saved status into skb->cb for use by tasklet */
	memcpy(skb->cb, &saved, sizeof(saved));

	skb->pkt_type = IEEE80211_TX_STATUS_MSG;
	skb_queue_tail(status->control.flags & IEEE80211_TXCTL_REQ_TX_STATUS ?
		       &local->skb_queue : &local->skb_queue_unreliable, skb);
	tmp = skb_queue_len(&local->skb_queue) +
		skb_queue_len(&local->skb_queue_unreliable);
	while (tmp > IEEE80211_IRQSAFE_QUEUE_LIMIT &&
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		memcpy(&saved, skb->cb, sizeof(saved));
		kfree(saved);
		dev_kfree_skb_irq(skb);
		tmp--;
		I802_DEBUG_INC(local->tx_status_drop);
	}
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_tx_status_irqsafe);

static void ieee80211_tasklet_handler(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
	struct sk_buff *skb;
	struct ieee80211_rx_status rx_status;
	struct ieee80211_tx_status *tx_status;

	while ((skb = skb_dequeue(&local->skb_queue)) ||
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		switch (skb->pkt_type) {
		case IEEE80211_RX_MSG:
			/* status is in skb->cb */
			memcpy(&rx_status, skb->cb, sizeof(rx_status));
			/* Clear skb->type in order to not confuse kernel
			 * netstack. */
			skb->pkt_type = 0;
			__ieee80211_rx(local_to_hw(local), skb, &rx_status);
			break;
		case IEEE80211_TX_STATUS_MSG:
			/* get pointer to saved status out of skb->cb */
			memcpy(&tx_status, skb->cb, sizeof(tx_status));
			skb->pkt_type = 0;
			ieee80211_tx_status(local_to_hw(local),
					    skb, tx_status);
			kfree(tx_status);
			break;
		default: /* should never get here! */
			printk(KERN_ERR "%s: Unknown message type (%d)\n",
			       local->mdev->name, skb->pkt_type);
			dev_kfree_skb(skb);
			break;
		}
	}
}


/* Remove added headers (e.g., QoS control), encryption header/MIC, etc. to
 * make a prepared TX frame (one that has been given to hw) to look like brand
 * new IEEE 802.11 frame that is ready to go through TX processing again.
 * Also, tx_packet_data in cb is restored from tx_control. */
static void ieee80211_remove_tx_extra(struct ieee80211_local *local,
				      struct ieee80211_key *key,
				      struct sk_buff *skb,
				      struct ieee80211_tx_control *control)
{
	int hdrlen, iv_len, mic_len;
	struct ieee80211_tx_packet_data *pkt_data;

	pkt_data = (struct ieee80211_tx_packet_data *)skb->cb;
	pkt_data->ifindex = control->ifindex;
	pkt_data->mgmt_iface = (control->type == IEEE80211_IF_TYPE_MGMT);
	pkt_data->req_tx_status = !!(control->flags & IEEE80211_TXCTL_REQ_TX_STATUS);
	pkt_data->do_not_encrypt = !!(control->flags & IEEE80211_TXCTL_DO_NOT_ENCRYPT);
	pkt_data->requeue = !!(control->flags & IEEE80211_TXCTL_REQUEUE);
	pkt_data->queue = control->queue;

	hdrlen = ieee80211_get_hdrlen_from_skb(skb);

	if (!key)
		goto no_key;

	switch (key->alg) {
	case ALG_WEP:
		iv_len = WEP_IV_LEN;
		mic_len = WEP_ICV_LEN;
		break;
	case ALG_TKIP:
		iv_len = TKIP_IV_LEN;
		mic_len = TKIP_ICV_LEN;
		break;
	case ALG_CCMP:
		iv_len = CCMP_HDR_LEN;
		mic_len = CCMP_MIC_LEN;
		break;
	default:
		goto no_key;
	}

	if (skb->len >= mic_len && key->force_sw_encrypt)
		skb_trim(skb, skb->len - mic_len);
	if (skb->len >= iv_len && skb->len > hdrlen) {
		memmove(skb->data + iv_len, skb->data, hdrlen);
		skb_pull(skb, iv_len);
	}

no_key:
	{
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		u16 fc = le16_to_cpu(hdr->frame_control);
		if ((fc & 0x8C) == 0x88) /* QoS Control Field */ {
			fc &= ~IEEE80211_STYPE_QOS_DATA;
			hdr->frame_control = cpu_to_le16(fc);
			memmove(skb->data + 2, skb->data, hdrlen - 2);
			skb_pull(skb, 2);
		}
	}
}


void ieee80211_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb,
			 struct ieee80211_tx_status *status)
{
	struct sk_buff *skb2;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_local *local = hw_to_local(hw);
	u16 frag, type;
	u32 msg_type;

	if (!status) {
		printk(KERN_ERR
		       "%s: ieee80211_tx_status called with NULL status\n",
		       local->mdev->name);
		dev_kfree_skb(skb);
		return;
	}

	if (status->excessive_retries) {
		struct sta_info *sta;
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			if (sta->flags & WLAN_STA_PS) {
				/* The STA is in power save mode, so assume
				 * that this TX packet failed because of that.
				 */
				status->excessive_retries = 0;
				status->flags |= IEEE80211_TX_STATUS_TX_FILTERED;
			}
			sta_info_put(sta);
		}
	}

	if (status->flags & IEEE80211_TX_STATUS_TX_FILTERED) {
		struct sta_info *sta;
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			sta->tx_filtered_count++;

			/* Clear the TX filter mask for this STA when sending
			 * the next packet. If the STA went to power save mode,
			 * this will happen when it is waking up for the next
			 * time. */
			sta->clear_dst_mask = 1;

			/* TODO: Is the WLAN_STA_PS flag always set here or is
			 * the race between RX and TX status causing some
			 * packets to be filtered out before 80211.o gets an
			 * update for PS status? This seems to be the case, so
			 * no changes are likely to be needed. */
			if (sta->flags & WLAN_STA_PS &&
			    skb_queue_len(&sta->tx_filtered) <
			    STA_MAX_TX_BUFFER) {
				ieee80211_remove_tx_extra(local, sta->key,
							  skb,
							  &status->control);
				skb_queue_tail(&sta->tx_filtered, skb);
			} else if (!(sta->flags & WLAN_STA_PS) &&
				   !(status->control.flags & IEEE80211_TXCTL_REQUEUE)) {
				/* Software retry the packet once */
				status->control.flags |= IEEE80211_TXCTL_REQUEUE;
				ieee80211_remove_tx_extra(local, sta->key,
							  skb,
							  &status->control);
				dev_queue_xmit(skb);
			} else {
				if (net_ratelimit()) {
					printk(KERN_DEBUG "%s: dropped TX "
					       "filtered frame queue_len=%d "
					       "PS=%d @%lu\n",
					       local->mdev->name,
					       skb_queue_len(
						       &sta->tx_filtered),
					       !!(sta->flags & WLAN_STA_PS),
					       jiffies);
				}
				dev_kfree_skb(skb);
			}
			sta_info_put(sta);
			return;
		}
	} else {
		/* FIXME: STUPID to call this with both local and local->mdev */
		rate_control_tx_status(local, local->mdev, skb, status);
	}

	ieee80211_led_tx(local, 0);

	/* SNMP counters
	 * Fragments are passed to low-level drivers as separate skbs, so these
	 * are actually fragments, not frames. Update frame counters only for
	 * the first fragment of the frame. */

	frag = le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG;
	type = le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_FTYPE;

	if (status->flags & IEEE80211_TX_STATUS_ACK) {
		if (frag == 0) {
			local->dot11TransmittedFrameCount++;
			if (is_multicast_ether_addr(hdr->addr1))
				local->dot11MulticastTransmittedFrameCount++;
			if (status->retry_count > 0)
				local->dot11RetryCount++;
			if (status->retry_count > 1)
				local->dot11MultipleRetryCount++;
		}

		/* This counter shall be incremented for an acknowledged MPDU
		 * with an individual address in the address 1 field or an MPDU
		 * with a multicast address in the address 1 field of type Data
		 * or Management. */
		if (!is_multicast_ether_addr(hdr->addr1) ||
		    type == IEEE80211_FTYPE_DATA ||
		    type == IEEE80211_FTYPE_MGMT)
			local->dot11TransmittedFragmentCount++;
	} else {
		if (frag == 0)
			local->dot11FailedCount++;
	}

	if (!(status->control.flags & IEEE80211_TXCTL_REQ_TX_STATUS)
	    || unlikely(!local->apdev)) {
		dev_kfree_skb(skb);
		return;
	}

	msg_type = (status->flags & IEEE80211_TX_STATUS_ACK) ?
		ieee80211_msg_tx_callback_ack : ieee80211_msg_tx_callback_fail;

	/* skb was the original skb used for TX. Clone it and give the clone
	 * to netif_rx(). Free original skb. */
	skb2 = skb_copy(skb, GFP_ATOMIC);
	if (!skb2) {
		dev_kfree_skb(skb);
		return;
	}
	dev_kfree_skb(skb);
	skb = skb2;

	/* Send frame to hostapd */
	ieee80211_rx_mgmt(local, skb, NULL, msg_type);
}
EXPORT_SYMBOL(ieee80211_tx_status);

/* TODO: implement register/unregister functions for adding TX/RX handlers
 * into ordered list */

/* rx_pre handlers don't have dev and sdata fields available in
 * ieee80211_txrx_data */
static ieee80211_rx_handler ieee80211_rx_pre_handlers[] =
{
	ieee80211_rx_h_parse_qos,
	ieee80211_rx_h_load_stats,
	NULL
};

static ieee80211_rx_handler ieee80211_rx_handlers[] =
{
	ieee80211_rx_h_if_stats,
	ieee80211_rx_h_monitor,
	ieee80211_rx_h_passive_scan,
	ieee80211_rx_h_check,
	ieee80211_rx_h_sta_process,
	ieee80211_rx_h_ccmp_decrypt,
	ieee80211_rx_h_tkip_decrypt,
	ieee80211_rx_h_wep_weak_iv_detection,
	ieee80211_rx_h_wep_decrypt,
	ieee80211_rx_h_defragment,
	ieee80211_rx_h_ps_poll,
	ieee80211_rx_h_michael_mic_verify,
	/* this must be after decryption - so header is counted in MPDU mic
	 * must be before pae and data, so QOS_DATA format frames
	 * are not passed to user space by these functions
	 */
	ieee80211_rx_h_remove_qos_control,
	ieee80211_rx_h_802_1x_pae,
	ieee80211_rx_h_drop_unencrypted,
	ieee80211_rx_h_data,
	ieee80211_rx_h_mgmt,
	NULL
};

static ieee80211_tx_handler ieee80211_tx_handlers[] =
{
	ieee80211_tx_h_check_assoc,
	ieee80211_tx_h_sequence,
	ieee80211_tx_h_ps_buf,
	ieee80211_tx_h_select_key,
	ieee80211_tx_h_michael_mic_add,
	ieee80211_tx_h_fragment,
	ieee80211_tx_h_tkip_encrypt,
	ieee80211_tx_h_ccmp_encrypt,
	ieee80211_tx_h_wep_encrypt,
	ieee80211_tx_h_rate_ctrl,
	ieee80211_tx_h_misc,
	ieee80211_tx_h_load_stats,
	NULL
};


int ieee80211_if_update_wds(struct net_device *dev, u8 *remote_addr)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta;

	if (compare_ether_addr(remote_addr, sdata->u.wds.remote_addr) == 0)
		return 0;

	/* Create STA entry for the new peer */
	sta = sta_info_add(local, dev, remote_addr, GFP_KERNEL);
	if (!sta)
		return -ENOMEM;
	sta_info_put(sta);

	/* Remove STA entry for the old peer */
	sta = sta_info_get(local, sdata->u.wds.remote_addr);
	if (sta) {
		sta_info_put(sta);
		sta_info_free(sta, 0);
	} else {
		printk(KERN_DEBUG "%s: could not find STA entry for WDS link "
		       "peer " MAC_FMT "\n",
		       dev->name, MAC_ARG(sdata->u.wds.remote_addr));
	}

	/* Update WDS link data */
	memcpy(&sdata->u.wds.remote_addr, remote_addr, ETH_ALEN);

	return 0;
}

/* Must not be called for mdev and apdev */
void ieee80211_if_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->hard_start_xmit = ieee80211_subif_start_xmit;
	dev->wireless_handlers = &ieee80211_iw_handler_def;
	dev->set_multicast_list = ieee80211_set_multicast_list;
	dev->change_mtu = ieee80211_change_mtu;
	dev->get_stats = ieee80211_get_stats;
	dev->open = ieee80211_open;
	dev->stop = ieee80211_stop;
	dev->uninit = ieee80211_if_reinit;
	dev->destructor = ieee80211_if_free;
}

void ieee80211_if_mgmt_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->hard_start_xmit = ieee80211_mgmt_start_xmit;
	dev->change_mtu = ieee80211_change_mtu_apdev;
	dev->get_stats = ieee80211_get_stats;
	dev->open = ieee80211_mgmt_open;
	dev->stop = ieee80211_mgmt_stop;
	dev->type = ARPHRD_IEEE80211_PRISM;
	dev->hard_header_parse = header_parse_80211;
	dev->uninit = ieee80211_if_reinit;
	dev->destructor = ieee80211_if_free;
}

int ieee80211_init_rate_ctrl_alg(struct ieee80211_local *local,
				 const char *name)
{
	struct rate_control_ref *ref, *old;

	ASSERT_RTNL();
	if (local->open_count || netif_running(local->mdev) ||
	    (local->apdev && netif_running(local->apdev)))
		return -EBUSY;

	ref = rate_control_alloc(name, local);
	if (!ref) {
		printk(KERN_WARNING "%s: Failed to select rate control "
		       "algorithm\n", local->mdev->name);
		return -ENOENT;
	}

	old = local->rate_ctrl;
	local->rate_ctrl = ref;
	if (old) {
		rate_control_put(old);
		sta_info_flush(local, NULL);
	}

	printk(KERN_DEBUG "%s: Selected rate control "
	       "algorithm '%s'\n", local->mdev->name,
	       ref->ops->name);


	return 0;
}

static void rate_control_deinitialize(struct ieee80211_local *local)
{
	struct rate_control_ref *ref;

	ref = local->rate_ctrl;
	local->rate_ctrl = NULL;
	rate_control_put(ref);
}

struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
					const struct ieee80211_ops *ops)
{
	struct net_device *mdev;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	int priv_size;
	struct wiphy *wiphy;

	/* Ensure 32-byte alignment of our private data and hw private data.
	 * We use the wiphy priv data for both our ieee80211_local and for
	 * the driver's private data
	 *
	 * In memory it'll be like this:
	 *
	 * +-------------------------+
	 * | struct wiphy	    |
	 * +-------------------------+
	 * | struct ieee80211_local  |
	 * +-------------------------+
	 * | driver's private data   |
	 * +-------------------------+
	 *
	 */
	priv_size = ((sizeof(struct ieee80211_local) +
		      NETDEV_ALIGN_CONST) & ~NETDEV_ALIGN_CONST) +
		    priv_data_len;

	wiphy = wiphy_new(&mac80211_config_ops, priv_size);

	if (!wiphy)
		return NULL;

	wiphy->privid = mac80211_wiphy_privid;

	local = wiphy_priv(wiphy);
	local->hw.wiphy = wiphy;

	local->hw.priv = (char *)local +
			 ((sizeof(struct ieee80211_local) +
			   NETDEV_ALIGN_CONST) & ~NETDEV_ALIGN_CONST);

	local->ops = ops;

	/* for now, mdev needs sub_if_data :/ */
	mdev = alloc_netdev(sizeof(struct ieee80211_sub_if_data),
			    "wmaster%d", ether_setup);
	if (!mdev) {
		wiphy_free(wiphy);
		return NULL;
	}

	sdata = IEEE80211_DEV_TO_SUB_IF(mdev);
	mdev->ieee80211_ptr = &sdata->wdev;
	sdata->wdev.wiphy = wiphy;

	local->hw.queues = 1; /* default */

	local->mdev = mdev;
	local->rx_pre_handlers = ieee80211_rx_pre_handlers;
	local->rx_handlers = ieee80211_rx_handlers;
	local->tx_handlers = ieee80211_tx_handlers;

	local->bridge_packets = 1;

	local->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;
	local->fragmentation_threshold = IEEE80211_MAX_FRAG_THRESHOLD;
	local->short_retry_limit = 7;
	local->long_retry_limit = 4;
	local->hw.conf.radio_enabled = 1;
	local->rate_ctrl_num_up = RATE_CONTROL_NUM_UP;
	local->rate_ctrl_num_down = RATE_CONTROL_NUM_DOWN;

	local->enabled_modes = (unsigned int) -1;

	INIT_LIST_HEAD(&local->modes_list);

	rwlock_init(&local->sub_if_lock);
	INIT_LIST_HEAD(&local->sub_if_list);

	INIT_DELAYED_WORK(&local->scan_work, ieee80211_sta_scan_work);
	init_timer(&local->stat_timer);
	local->stat_timer.function = ieee80211_stat_refresh;
	local->stat_timer.data = (unsigned long) local;
	ieee80211_rx_bss_list_init(mdev);

	sta_info_init(local);

	mdev->hard_start_xmit = ieee80211_master_start_xmit;
	mdev->open = ieee80211_master_open;
	mdev->stop = ieee80211_master_stop;
	mdev->type = ARPHRD_IEEE80211;
	mdev->hard_header_parse = header_parse_80211;

	sdata->type = IEEE80211_IF_TYPE_AP;
	sdata->dev = mdev;
	sdata->local = local;
	sdata->u.ap.force_unicast_rateidx = -1;
	sdata->u.ap.max_ratectrl_rateidx = -1;
	ieee80211_if_sdata_init(sdata);
	list_add_tail(&sdata->list, &local->sub_if_list);

	tasklet_init(&local->tx_pending_tasklet, ieee80211_tx_pending,
		     (unsigned long)local);
	tasklet_disable(&local->tx_pending_tasklet);

	tasklet_init(&local->tasklet,
		     ieee80211_tasklet_handler,
		     (unsigned long) local);
	tasklet_disable(&local->tasklet);

	skb_queue_head_init(&local->skb_queue);
	skb_queue_head_init(&local->skb_queue_unreliable);

	return local_to_hw(local);
}
EXPORT_SYMBOL(ieee80211_alloc_hw);

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	const char *name;
	int result;

	result = wiphy_register(local->hw.wiphy);
	if (result < 0)
		return result;

	name = wiphy_dev(local->hw.wiphy)->driver->name;
	local->hw.workqueue = create_singlethread_workqueue(name);
	if (!local->hw.workqueue) {
		result = -ENOMEM;
		goto fail_workqueue;
	}

	debugfs_hw_add(local);

	local->hw.conf.beacon_int = 1000;

	local->wstats_flags |= local->hw.max_rssi ?
			       IW_QUAL_LEVEL_UPDATED : IW_QUAL_LEVEL_INVALID;
	local->wstats_flags |= local->hw.max_signal ?
			       IW_QUAL_QUAL_UPDATED : IW_QUAL_QUAL_INVALID;
	local->wstats_flags |= local->hw.max_noise ?
			       IW_QUAL_NOISE_UPDATED : IW_QUAL_NOISE_INVALID;
	if (local->hw.max_rssi < 0 || local->hw.max_noise < 0)
		local->wstats_flags |= IW_QUAL_DBM;

	result = sta_info_start(local);
	if (result < 0)
		goto fail_sta_info;

	rtnl_lock();
	result = dev_alloc_name(local->mdev, local->mdev->name);
	if (result < 0)
		goto fail_dev;

	memcpy(local->mdev->dev_addr, local->hw.wiphy->perm_addr, ETH_ALEN);
	SET_NETDEV_DEV(local->mdev, wiphy_dev(local->hw.wiphy));

	result = register_netdevice(local->mdev);
	if (result < 0)
		goto fail_dev;

	ieee80211_debugfs_add_netdev(IEEE80211_DEV_TO_SUB_IF(local->mdev));

	result = ieee80211_init_rate_ctrl_alg(local, NULL);
	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize rate control "
		       "algorithm\n", local->mdev->name);
		goto fail_rate;
	}

	result = ieee80211_wep_init(local);

	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize wep\n",
		       local->mdev->name);
		goto fail_wep;
	}

	ieee80211_install_qdisc(local->mdev);

	/* add one default STA interface */
	result = ieee80211_if_add(local->mdev, "wlan%d", NULL,
				  IEEE80211_IF_TYPE_STA);
	if (result)
		printk(KERN_WARNING "%s: Failed to add default virtual iface\n",
		       local->mdev->name);

	local->reg_state = IEEE80211_DEV_REGISTERED;
	rtnl_unlock();

	ieee80211_led_init(local);

	return 0;

fail_wep:
	rate_control_deinitialize(local);
fail_rate:
	ieee80211_debugfs_remove_netdev(IEEE80211_DEV_TO_SUB_IF(local->mdev));
	unregister_netdevice(local->mdev);
fail_dev:
	rtnl_unlock();
	sta_info_stop(local);
fail_sta_info:
	debugfs_hw_del(local);
	destroy_workqueue(local->hw.workqueue);
fail_workqueue:
	wiphy_unregister(local->hw.wiphy);
	return result;
}
EXPORT_SYMBOL(ieee80211_register_hw);

int ieee80211_register_hwmode(struct ieee80211_hw *hw,
			      struct ieee80211_hw_mode *mode)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	int i;

	INIT_LIST_HEAD(&mode->list);
	list_add_tail(&mode->list, &local->modes_list);

	local->hw_modes |= (1 << mode->mode);
	for (i = 0; i < mode->num_rates; i++) {
		rate = &(mode->rates[i]);
		rate->rate_inv = CHAN_UTIL_RATE_LCM / rate->rate;
	}
	ieee80211_prepare_rates(local, mode);

	if (!local->oper_hw_mode) {
		/* Default to this mode */
		local->hw.conf.phymode = mode->mode;
		local->oper_hw_mode = local->scan_hw_mode = mode;
		local->oper_channel = local->scan_channel = &mode->channels[0];
		local->hw.conf.mode = local->oper_hw_mode;
		local->hw.conf.chan = local->oper_channel;
	}

	if (!(hw->flags & IEEE80211_HW_DEFAULT_REG_DOMAIN_CONFIGURED))
		ieee80211_init_client(local->mdev);

	return 0;
}
EXPORT_SYMBOL(ieee80211_register_hwmode);

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata, *tmp;
	struct list_head tmp_list;
	int i;

	tasklet_kill(&local->tx_pending_tasklet);
	tasklet_kill(&local->tasklet);

	rtnl_lock();

	BUG_ON(local->reg_state != IEEE80211_DEV_REGISTERED);

	local->reg_state = IEEE80211_DEV_UNREGISTERED;
	if (local->apdev)
		ieee80211_if_del_mgmt(local);

	write_lock_bh(&local->sub_if_lock);
	list_replace_init(&local->sub_if_list, &tmp_list);
	write_unlock_bh(&local->sub_if_lock);

	list_for_each_entry_safe(sdata, tmp, &tmp_list, list)
		__ieee80211_if_del(local, sdata);

	rtnl_unlock();

	if (local->stat_time)
		del_timer_sync(&local->stat_timer);

	ieee80211_rx_bss_list_deinit(local->mdev);
	ieee80211_clear_tx_pending(local);
	sta_info_stop(local);
	rate_control_deinitialize(local);
	debugfs_hw_del(local);

	for (i = 0; i < NUM_IEEE80211_MODES; i++) {
		kfree(local->supp_rates[i]);
		kfree(local->basic_rates[i]);
	}

	if (skb_queue_len(&local->skb_queue)
			|| skb_queue_len(&local->skb_queue_unreliable))
		printk(KERN_WARNING "%s: skb_queue not empty\n",
		       local->mdev->name);
	skb_queue_purge(&local->skb_queue);
	skb_queue_purge(&local->skb_queue_unreliable);

	destroy_workqueue(local->hw.workqueue);
	wiphy_unregister(local->hw.wiphy);
	ieee80211_wep_free(local);
	ieee80211_led_exit(local);
}
EXPORT_SYMBOL(ieee80211_unregister_hw);

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	ieee80211_if_free(local->mdev);
	wiphy_free(local->hw.wiphy);
}
EXPORT_SYMBOL(ieee80211_free_hw);

void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (test_and_clear_bit(IEEE80211_LINK_STATE_XOFF,
			       &local->state[queue])) {
		if (test_bit(IEEE80211_LINK_STATE_PENDING,
			     &local->state[queue]))
			tasklet_schedule(&local->tx_pending_tasklet);
		else
			if (!ieee80211_qdisc_installed(local->mdev)) {
				if (queue == 0)
					netif_wake_queue(local->mdev);
			} else
				__netif_schedule(local->mdev);
	}
}
EXPORT_SYMBOL(ieee80211_wake_queue);

void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (!ieee80211_qdisc_installed(local->mdev) && queue == 0)
		netif_stop_queue(local->mdev);
	set_bit(IEEE80211_LINK_STATE_XOFF, &local->state[queue]);
}
EXPORT_SYMBOL(ieee80211_stop_queue);

void ieee80211_start_queues(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	int i;

	for (i = 0; i < local->hw.queues; i++)
		clear_bit(IEEE80211_LINK_STATE_XOFF, &local->state[i]);
	if (!ieee80211_qdisc_installed(local->mdev))
		netif_start_queue(local->mdev);
}
EXPORT_SYMBOL(ieee80211_start_queues);

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	int i;

	for (i = 0; i < hw->queues; i++)
		ieee80211_stop_queue(hw, i);
}
EXPORT_SYMBOL(ieee80211_stop_queues);

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	int i;

	for (i = 0; i < hw->queues; i++)
		ieee80211_wake_queue(hw, i);
}
EXPORT_SYMBOL(ieee80211_wake_queues);

struct net_device_stats *ieee80211_dev_stats(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	return &sdata->stats;
}

static int __init ieee80211_init(void)
{
	struct sk_buff *skb;
	int ret;

	BUILD_BUG_ON(sizeof(struct ieee80211_tx_packet_data) > sizeof(skb->cb));

	ret = ieee80211_wme_register();
	if (ret) {
		printk(KERN_DEBUG "ieee80211_init: failed to "
		       "initialize WME (err=%d)\n", ret);
		return ret;
	}

	ieee80211_debugfs_netdev_init();

	return 0;
}


static void __exit ieee80211_exit(void)
{
	ieee80211_wme_unregister();
	ieee80211_debugfs_netdev_exit();
}


module_init(ieee80211_init);
module_exit(ieee80211_exit);

MODULE_DESCRIPTION("IEEE 802.11 subsystem");
MODULE_LICENSE("GPL");
