/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rcupdate.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>

#include "ieee80211_i.h"
#include "ieee80211_led.h"
#include "wep.h"
#include "wpa.h"
#include "tkip.h"
#include "wme.h"

/*
 * monitor mode reception
 *
 * This function cleans up the SKB, i.e. it removes all the stuff
 * only useful for monitoring.
 */
static struct sk_buff *remove_monitor_info(struct ieee80211_local *local,
					   struct sk_buff *skb,
					   int rtap_len)
{
	skb_pull(skb, rtap_len);

	if (local->hw.flags & IEEE80211_HW_RX_INCLUDES_FCS) {
		if (likely(skb->len > FCS_LEN))
			skb_trim(skb, skb->len - FCS_LEN);
		else {
			/* driver bug */
			WARN_ON(1);
			dev_kfree_skb(skb);
			skb = NULL;
		}
	}

	return skb;
}

static inline int should_drop_frame(struct ieee80211_rx_status *status,
				    struct sk_buff *skb,
				    int present_fcs_len,
				    int radiotap_len)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (status->flag & (RX_FLAG_FAILED_FCS_CRC | RX_FLAG_FAILED_PLCP_CRC))
		return 1;
	if (unlikely(skb->len < 16 + present_fcs_len + radiotap_len))
		return 1;
	if ((hdr->frame_control & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
			cpu_to_le16(IEEE80211_FTYPE_CTL))
		return 1;
	return 0;
}

/*
 * This function copies a received frame to all monitor interfaces and
 * returns a cleaned-up SKB that no longer includes the FCS nor the
 * radiotap header the driver might have added.
 */
static struct sk_buff *
ieee80211_rx_monitor(struct ieee80211_local *local, struct sk_buff *origskb,
		     struct ieee80211_rx_status *status)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_rate *rate;
	int needed_headroom = 0;
	struct ieee80211_rtap_hdr {
		struct ieee80211_radiotap_header hdr;
		u8 flags;
		u8 rate;
		__le16 chan_freq;
		__le16 chan_flags;
		u8 antsignal;
		u8 padding_for_rxflags;
		__le16 rx_flags;
	} __attribute__ ((packed)) *rthdr;
	struct sk_buff *skb, *skb2;
	struct net_device *prev_dev = NULL;
	int present_fcs_len = 0;
	int rtap_len = 0;

	/*
	 * First, we may need to make a copy of the skb because
	 *  (1) we need to modify it for radiotap (if not present), and
	 *  (2) the other RX handlers will modify the skb we got.
	 *
	 * We don't need to, of course, if we aren't going to return
	 * the SKB because it has a bad FCS/PLCP checksum.
	 */
	if (status->flag & RX_FLAG_RADIOTAP)
		rtap_len = ieee80211_get_radiotap_len(origskb->data);
	else
		needed_headroom = sizeof(*rthdr);

	if (local->hw.flags & IEEE80211_HW_RX_INCLUDES_FCS)
		present_fcs_len = FCS_LEN;

	if (!local->monitors) {
		if (should_drop_frame(status, origskb, present_fcs_len,
				      rtap_len)) {
			dev_kfree_skb(origskb);
			return NULL;
		}

		return remove_monitor_info(local, origskb, rtap_len);
	}

	if (should_drop_frame(status, origskb, present_fcs_len, rtap_len)) {
		/* only need to expand headroom if necessary */
		skb = origskb;
		origskb = NULL;

		/*
		 * This shouldn't trigger often because most devices have an
		 * RX header they pull before we get here, and that should
		 * be big enough for our radiotap information. We should
		 * probably export the length to drivers so that we can have
		 * them allocate enough headroom to start with.
		 */
		if (skb_headroom(skb) < needed_headroom &&
		    pskb_expand_head(skb, sizeof(*rthdr), 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return NULL;
		}
	} else {
		/*
		 * Need to make a copy and possibly remove radiotap header
		 * and FCS from the original.
		 */
		skb = skb_copy_expand(origskb, needed_headroom, 0, GFP_ATOMIC);

		origskb = remove_monitor_info(local, origskb, rtap_len);

		if (!skb)
			return origskb;
	}

	/* if necessary, prepend radiotap information */
	if (!(status->flag & RX_FLAG_RADIOTAP)) {
		rthdr = (void *) skb_push(skb, sizeof(*rthdr));
		memset(rthdr, 0, sizeof(*rthdr));
		rthdr->hdr.it_len = cpu_to_le16(sizeof(*rthdr));
		rthdr->hdr.it_present =
			cpu_to_le32((1 << IEEE80211_RADIOTAP_FLAGS) |
				    (1 << IEEE80211_RADIOTAP_RATE) |
				    (1 << IEEE80211_RADIOTAP_CHANNEL) |
				    (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL) |
				    (1 << IEEE80211_RADIOTAP_RX_FLAGS));
		rthdr->flags = local->hw.flags & IEEE80211_HW_RX_INCLUDES_FCS ?
			       IEEE80211_RADIOTAP_F_FCS : 0;

		/* FIXME: when radiotap gets a 'bad PLCP' flag use it here */
		rthdr->rx_flags = 0;
		if (status->flag &
		    (RX_FLAG_FAILED_FCS_CRC | RX_FLAG_FAILED_PLCP_CRC))
			rthdr->rx_flags |=
				cpu_to_le16(IEEE80211_RADIOTAP_F_RX_BADFCS);

		rate = ieee80211_get_rate(local, status->phymode,
					  status->rate);
		if (rate)
			rthdr->rate = rate->rate / 5;

		rthdr->chan_freq = cpu_to_le16(status->freq);

		if (status->phymode == MODE_IEEE80211A)
			rthdr->chan_flags =
				cpu_to_le16(IEEE80211_CHAN_OFDM |
					    IEEE80211_CHAN_5GHZ);
		else
			rthdr->chan_flags =
				cpu_to_le16(IEEE80211_CHAN_DYN |
					    IEEE80211_CHAN_2GHZ);

		rthdr->antsignal = status->ssi;
	}

	skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;

		if (sdata->type != IEEE80211_IF_TYPE_MNTR)
			continue;

		if (prev_dev) {
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (skb2) {
				skb2->dev = prev_dev;
				netif_rx(skb2);
			}
		}

		prev_dev = sdata->dev;
		sdata->dev->stats.rx_packets++;
		sdata->dev->stats.rx_bytes += skb->len;
	}

	if (prev_dev) {
		skb->dev = prev_dev;
		netif_rx(skb);
	} else
		dev_kfree_skb(skb);

	return origskb;
}


/* pre-rx handlers
 *
 * these don't have dev/sdata fields in the rx data
 * The sta value should also not be used because it may
 * be NULL even though a STA (in IBSS mode) will be added.
 */

static ieee80211_txrx_result
ieee80211_rx_h_parse_qos(struct ieee80211_txrx_data *rx)
{
	u8 *data = rx->skb->data;
	int tid;

	/* does the frame have a qos control field? */
	if (WLAN_FC_IS_QOS_DATA(rx->fc)) {
		u8 *qc = data + ieee80211_get_hdrlen(rx->fc) - QOS_CONTROL_LEN;
		/* frame has qos control */
		tid = qc[0] & QOS_CONTROL_TID_MASK;
	} else {
		if (unlikely((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT)) {
			/* Separate TID for management frames */
			tid = NUM_RX_DATA_QUEUES - 1;
		} else {
			/* no qos control present */
			tid = 0; /* 802.1d - Best Effort */
		}
	}

	I802_DEBUG_INC(rx->local->wme_rx_queue[tid]);
	/* only a debug counter, sta might not be assigned properly yet */
	if (rx->sta)
		I802_DEBUG_INC(rx->sta->wme_rx_queue[tid]);

	rx->u.rx.queue = tid;
	/* Set skb->priority to 1d tag if highest order bit of TID is not set.
	 * For now, set skb->priority to 0 for other cases. */
	rx->skb->priority = (tid > 7) ? 0 : tid;

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
	rx->u.rx.load = load;

	return TXRX_CONTINUE;
}

ieee80211_rx_handler ieee80211_rx_pre_handlers[] =
{
	ieee80211_rx_h_parse_qos,
	ieee80211_rx_h_load_stats,
	NULL
};

/* rx handlers */

static ieee80211_txrx_result
ieee80211_rx_h_if_stats(struct ieee80211_txrx_data *rx)
{
	if (rx->sta)
		rx->sta->channel_use_raw += rx->u.rx.load;
	rx->sdata->channel_use_raw += rx->u.rx.load;
	return TXRX_CONTINUE;
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

	if (unlikely(rx->flags & IEEE80211_TXRXD_RXIN_SCAN)) {
		/* scanning finished during invoking of handlers */
		I802_DEBUG_INC(local->rx_handlers_drop_passive_scan);
		return TXRX_DROP;
	}

	return TXRX_CONTINUE;
}

static ieee80211_txrx_result
ieee80211_rx_h_check(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_hdr *hdr;
	hdr = (struct ieee80211_hdr *) rx->skb->data;

	/* Drop duplicate 802.11 retransmissions (IEEE 802.11 Chap. 9.2.9) */
	if (rx->sta && !is_multicast_ether_addr(hdr->addr1)) {
		if (unlikely(rx->fc & IEEE80211_FCTL_RETRY &&
			     rx->sta->last_seq_ctrl[rx->u.rx.queue] ==
			     hdr->seq_ctrl)) {
			if (rx->flags & IEEE80211_TXRXD_RXRA_MATCH) {
				rx->local->dot11FrameDuplicateCount++;
				rx->sta->num_duplicates++;
			}
			return TXRX_DROP;
		} else
			rx->sta->last_seq_ctrl[rx->u.rx.queue] = hdr->seq_ctrl;
	}

	if (unlikely(rx->skb->len < 16)) {
		I802_DEBUG_INC(rx->local->rx_handlers_drop_short);
		return TXRX_DROP;
	}

	if (!(rx->flags & IEEE80211_TXRXD_RXRA_MATCH))
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
		    || !(rx->flags & IEEE80211_TXRXD_RXRA_MATCH)) {
			/* Drop IBSS frames and frames for other hosts
			 * silently. */
			return TXRX_DROP;
		}

		return TXRX_DROP;
	}

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_rx_h_decrypt(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;
	int keyidx;
	int hdrlen;
	ieee80211_txrx_result result = TXRX_DROP;
	struct ieee80211_key *stakey = NULL;

	/*
	 * Key selection 101
	 *
	 * There are three types of keys:
	 *  - GTK (group keys)
	 *  - PTK (pairwise keys)
	 *  - STK (station-to-station pairwise keys)
	 *
	 * When selecting a key, we have to distinguish between multicast
	 * (including broadcast) and unicast frames, the latter can only
	 * use PTKs and STKs while the former always use GTKs. Unless, of
	 * course, actual WEP keys ("pre-RSNA") are used, then unicast
	 * frames can also use key indizes like GTKs. Hence, if we don't
	 * have a PTK/STK we check the key index for a WEP key.
	 *
	 * Note that in a regular BSS, multicast frames are sent by the
	 * AP only, associated stations unicast the frame to the AP first
	 * which then multicasts it on their behalf.
	 *
	 * There is also a slight problem in IBSS mode: GTKs are negotiated
	 * with each station, that is something we don't currently handle.
	 * The spec seems to expect that one negotiates the same key with
	 * every station but there's no such requirement; VLANs could be
	 * possible.
	 */

	if (!(rx->fc & IEEE80211_FCTL_PROTECTED))
		return TXRX_CONTINUE;

	/*
	 * No point in finding a key and decrypting if the frame is neither
	 * addressed to us nor a multicast frame.
	 */
	if (!(rx->flags & IEEE80211_TXRXD_RXRA_MATCH))
		return TXRX_CONTINUE;

	if (rx->sta)
		stakey = rcu_dereference(rx->sta->key);

	if (!is_multicast_ether_addr(hdr->addr1) && stakey) {
		rx->key = stakey;
	} else {
		/*
		 * The device doesn't give us the IV so we won't be
		 * able to look up the key. That's ok though, we
		 * don't need to decrypt the frame, we just won't
		 * be able to keep statistics accurate.
		 * Except for key threshold notifications, should
		 * we somehow allow the driver to tell us which key
		 * the hardware used if this flag is set?
		 */
		if ((rx->u.rx.status->flag & RX_FLAG_DECRYPTED) &&
		    (rx->u.rx.status->flag & RX_FLAG_IV_STRIPPED))
			return TXRX_CONTINUE;

		hdrlen = ieee80211_get_hdrlen(rx->fc);

		if (rx->skb->len < 8 + hdrlen)
			return TXRX_DROP; /* TODO: count this? */

		/*
		 * no need to call ieee80211_wep_get_keyidx,
		 * it verifies a bunch of things we've done already
		 */
		keyidx = rx->skb->data[hdrlen + 3] >> 6;

		rx->key = rcu_dereference(rx->sdata->keys[keyidx]);

		/*
		 * RSNA-protected unicast frames should always be sent with
		 * pairwise or station-to-station keys, but for WEP we allow
		 * using a key index as well.
		 */
		if (rx->key && rx->key->conf.alg != ALG_WEP &&
		    !is_multicast_ether_addr(hdr->addr1))
			rx->key = NULL;
	}

	if (rx->key) {
		rx->key->tx_rx_count++;
		/* TODO: add threshold stuff again */
	} else {
#ifdef CONFIG_MAC80211_DEBUG
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: RX protected frame,"
			       " but have no key\n", rx->dev->name);
#endif /* CONFIG_MAC80211_DEBUG */
		return TXRX_DROP;
	}

	/* Check for weak IVs if possible */
	if (rx->sta && rx->key->conf.alg == ALG_WEP &&
	    ((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) &&
	    (!(rx->u.rx.status->flag & RX_FLAG_IV_STRIPPED) ||
	     !(rx->u.rx.status->flag & RX_FLAG_DECRYPTED)) &&
	    ieee80211_wep_is_weak_iv(rx->skb, rx->key))
		rx->sta->wep_weak_iv_count++;

	switch (rx->key->conf.alg) {
	case ALG_WEP:
		result = ieee80211_crypto_wep_decrypt(rx);
		break;
	case ALG_TKIP:
		result = ieee80211_crypto_tkip_decrypt(rx);
		break;
	case ALG_CCMP:
		result = ieee80211_crypto_ccmp_decrypt(rx);
		break;
	}

	/* either the frame has been decrypted or will be dropped */
	rx->u.rx.status->flag |= RX_FLAG_DECRYPTED;

	return result;
}

static void ap_sta_ps_start(struct net_device *dev, struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata;
	DECLARE_MAC_BUF(mac);

	sdata = IEEE80211_DEV_TO_SUB_IF(sta->dev);

	if (sdata->bss)
		atomic_inc(&sdata->bss->num_sta_ps);
	sta->flags |= WLAN_STA_PS;
	sta->pspoll = 0;
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	printk(KERN_DEBUG "%s: STA %s aid %d enters power save mode\n",
	       dev->name, print_mac(mac, sta->addr), sta->aid);
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
}

static int ap_sta_ps_end(struct net_device *dev, struct sta_info *sta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	int sent = 0;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_tx_packet_data *pkt_data;
	DECLARE_MAC_BUF(mac);

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
	printk(KERN_DEBUG "%s: STA %s aid %d exits power save mode\n",
	       dev->name, print_mac(mac, sta->addr), sta->aid);
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
	/* Send all buffered frames to the station */
	while ((skb = skb_dequeue(&sta->tx_filtered)) != NULL) {
		pkt_data = (struct ieee80211_tx_packet_data *) skb->cb;
		sent++;
		pkt_data->flags |= IEEE80211_TXPD_REQUEUE;
		dev_queue_xmit(skb);
	}
	while ((skb = skb_dequeue(&sta->ps_tx_buf)) != NULL) {
		pkt_data = (struct ieee80211_tx_packet_data *) skb->cb;
		local->total_ps_buffered--;
		sent++;
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		printk(KERN_DEBUG "%s: STA %s aid %d send PS frame "
		       "since STA not sleeping anymore\n", dev->name,
		       print_mac(mac, sta->addr), sta->aid);
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
		pkt_data->flags |= IEEE80211_TXPD_REQUEUE;
		dev_queue_xmit(skb);
	}

	return sent;
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

	if (!(rx->flags & IEEE80211_TXRXD_RXRA_MATCH))
		return TXRX_CONTINUE;

	sta->rx_fragments++;
	sta->rx_bytes += rx->skb->len;
	sta->last_rssi = rx->u.rx.status->ssi;
	sta->last_signal = rx->u.rx.status->signal;
	sta->last_noise = rx->u.rx.status->noise;

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
		DECLARE_MAC_BUF(mac);
		DECLARE_MAC_BUF(mac2);
		printk(KERN_DEBUG "%s: RX reassembly removed oldest "
		       "fragment entry (idx=%d age=%lu seq=%d last_frag=%d "
		       "addr1=%s addr2=%s\n",
		       sdata->dev->name, idx,
		       jiffies - entry->first_frag_time, entry->seq,
		       entry->last_frag, print_mac(mac, hdr->addr1),
		       print_mac(mac2, hdr->addr2));
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
	DECLARE_MAC_BUF(mac);

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
		if (rx->key && rx->key->conf.alg == ALG_CCMP &&
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
		if (!rx->key || rx->key->conf.alg != ALG_CCMP)
			return TXRX_DROP;
		memcpy(pn, entry->last_pn, CCMP_PN_LEN);
		for (i = CCMP_PN_LEN - 1; i >= 0; i--) {
			pn[i]++;
			if (pn[i])
				break;
		}
		rpn = rx->key->u.ccmp.rx_pn[rx->u.rx.queue];
		if (memcmp(pn, rpn, CCMP_PN_LEN) != 0) {
			if (net_ratelimit())
				printk(KERN_DEBUG "%s: defrag: CCMP PN not "
				       "sequential A2=%s"
				       " PN=%02x%02x%02x%02x%02x%02x "
				       "(expected %02x%02x%02x%02x%02x%02x)\n",
				       rx->dev->name, print_mac(mac, hdr->addr2),
				       rpn[0], rpn[1], rpn[2], rpn[3], rpn[4],
				       rpn[5], pn[0], pn[1], pn[2], pn[3],
				       pn[4], pn[5]);
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
	while ((skb = __skb_dequeue(&entry->skb_list))) {
		memcpy(skb_put(rx->skb, skb->len), skb->data, skb->len);
		dev_kfree_skb(skb);
	}

	/* Complete frame has been reassembled - process it now */
	rx->flags |= IEEE80211_TXRXD_FRAGMENTED;

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
ieee80211_rx_h_ps_poll(struct ieee80211_txrx_data *rx)
{
	struct sk_buff *skb;
	int no_pending_pkts;
	DECLARE_MAC_BUF(mac);

	if (likely(!rx->sta ||
		   (rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_CTL ||
		   (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_PSPOLL ||
		   !(rx->flags & IEEE80211_TXRXD_RXRA_MATCH)))
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
		printk(KERN_DEBUG "STA %s aid %d: PS Poll (entries after %d)\n",
		       print_mac(mac, rx->sta->addr), rx->sta->aid,
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
		printk(KERN_DEBUG "%s: STA %s sent PS Poll even "
		       "though there is no buffered frames for it\n",
		       rx->dev->name, print_mac(mac, rx->sta->addr));
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */

	}

	/* Free PS Poll skb here instead of returning TXRX_DROP that would
	 * count as an dropped frame. */
	dev_kfree_skb(rx->skb);

	return TXRX_QUEUED;
}

static ieee80211_txrx_result
ieee80211_rx_h_remove_qos_control(struct ieee80211_txrx_data *rx)
{
	u16 fc = rx->fc;
	u8 *data = rx->skb->data;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) data;

	if (!WLAN_FC_IS_QOS_DATA(fc))
		return TXRX_CONTINUE;

	/* remove the qos control field, update frame type and meta-data */
	memmove(data + 2, data, ieee80211_get_hdrlen(fc) - 2);
	hdr = (struct ieee80211_hdr *) skb_pull(rx->skb, 2);
	/* change frame type to non QOS */
	rx->fc = fc &= ~IEEE80211_STYPE_QOS_DATA;
	hdr->frame_control = cpu_to_le16(fc);

	return TXRX_CONTINUE;
}

static ieee80211_txrx_result
ieee80211_rx_h_802_1x_pae(struct ieee80211_txrx_data *rx)
{
	if (rx->sdata->eapol && ieee80211_is_eapol(rx->skb) &&
	    rx->sdata->type != IEEE80211_IF_TYPE_STA &&
	    (rx->flags & IEEE80211_TXRXD_RXRA_MATCH))
		return TXRX_CONTINUE;

	if (unlikely(rx->sdata->ieee802_1x &&
		     (rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA &&
		     (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_NULLFUNC &&
		     (!rx->sta || !(rx->sta->flags & WLAN_STA_AUTHORIZED)) &&
		     !ieee80211_is_eapol(rx->skb))) {
#ifdef CONFIG_MAC80211_DEBUG
		struct ieee80211_hdr *hdr =
			(struct ieee80211_hdr *) rx->skb->data;
		DECLARE_MAC_BUF(mac);
		printk(KERN_DEBUG "%s: dropped frame from %s"
		       " (unauthorized port)\n", rx->dev->name,
		       print_mac(mac, hdr->addr2));
#endif /* CONFIG_MAC80211_DEBUG */
		return TXRX_DROP;
	}

	return TXRX_CONTINUE;
}

static ieee80211_txrx_result
ieee80211_rx_h_drop_unencrypted(struct ieee80211_txrx_data *rx)
{
	/*
	 * Pass through unencrypted frames if the hardware has
	 * decrypted them already.
	 */
	if (rx->u.rx.status->flag & RX_FLAG_DECRYPTED)
		return TXRX_CONTINUE;

	/* Drop unencrypted frames if key is set. */
	if (unlikely(!(rx->fc & IEEE80211_FCTL_PROTECTED) &&
		     (rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA &&
		     (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_NULLFUNC &&
		     (rx->key || rx->sdata->drop_unencrypted) &&
		     (rx->sdata->eapol == 0 || !ieee80211_is_eapol(rx->skb)))) {
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: RX non-WEP frame, but expected "
			       "encryption\n", rx->dev->name);
		return TXRX_DROP;
	}
	return TXRX_CONTINUE;
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
	DECLARE_MAC_BUF(mac);
	DECLARE_MAC_BUF(mac2);
	DECLARE_MAC_BUF(mac3);
	DECLARE_MAC_BUF(mac4);

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
			if (net_ratelimit())
				printk(KERN_DEBUG "%s: dropped ToDS frame "
				       "(BSSID=%s SA=%s DA=%s)\n",
				       dev->name,
				       print_mac(mac, hdr->addr1),
				       print_mac(mac2, hdr->addr2),
				       print_mac(mac3, hdr->addr3));
			return TXRX_DROP;
		}
		break;
	case (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS):
		/* RA TA DA SA */
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr4, ETH_ALEN);

		if (unlikely(sdata->type != IEEE80211_IF_TYPE_WDS)) {
			if (net_ratelimit())
				printk(KERN_DEBUG "%s: dropped FromDS&ToDS "
				       "frame (RA=%s TA=%s DA=%s SA=%s)\n",
				       rx->dev->name,
				       print_mac(mac, hdr->addr1),
				       print_mac(mac2, hdr->addr2),
				       print_mac(mac3, hdr->addr3),
				       print_mac(mac4, hdr->addr4));
			return TXRX_DROP;
		}
		break;
	case IEEE80211_FCTL_FROMDS:
		/* DA BSSID SA */
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr3, ETH_ALEN);

		if (sdata->type != IEEE80211_IF_TYPE_STA ||
		    (is_multicast_ether_addr(dst) &&
		     !compare_ether_addr(src, dev->dev_addr)))
			return TXRX_DROP;
		break;
	case 0:
		/* DA SA BSSID */
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);

		if (sdata->type != IEEE80211_IF_TYPE_IBSS) {
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: dropped IBSS frame "
				       "(DA=%s SA=%s BSSID=%s)\n",
				       dev->name,
				       print_mac(mac, hdr->addr1),
				       print_mac(mac2, hdr->addr2),
				       print_mac(mac3, hdr->addr3));
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

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb->len;

	if (local->bridge_packets && (sdata->type == IEEE80211_IF_TYPE_AP
	    || sdata->type == IEEE80211_IF_TYPE_VLAN) &&
	    (rx->flags & IEEE80211_TXRXD_RXRA_MATCH)) {
		if (is_multicast_ether_addr(skb->data)) {
			/* send multicast frames both to higher layers in
			 * local net stack and back to the wireless media */
			skb2 = skb_copy(skb, GFP_ATOMIC);
			if (!skb2 && net_ratelimit())
				printk(KERN_DEBUG "%s: failed to clone "
				       "multicast frame\n", dev->name);
		} else {
			struct sta_info *dsta;
			dsta = sta_info_get(local, skb->data);
			if (dsta && !dsta->dev) {
				if (net_ratelimit())
					printk(KERN_DEBUG "Station with null "
					       "dev structure!\n");
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

static ieee80211_txrx_result
ieee80211_rx_h_mgmt(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_sub_if_data *sdata;

	if (!(rx->flags & IEEE80211_TXRXD_RXRA_MATCH))
		return TXRX_DROP;

	sdata = IEEE80211_DEV_TO_SUB_IF(rx->dev);
	if ((sdata->type == IEEE80211_IF_TYPE_STA ||
	     sdata->type == IEEE80211_IF_TYPE_IBSS) &&
	    !(sdata->flags & IEEE80211_SDATA_USERSPACE_MLME))
		ieee80211_sta_rx_mgmt(rx->dev, rx->skb, rx->u.rx.status);
	else
		return TXRX_DROP;

	return TXRX_QUEUED;
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

		switch (res) {
		case TXRX_CONTINUE:
			continue;
		case TXRX_DROP:
			I802_DEBUG_INC(local->rx_handlers_drop);
			if (sta)
				sta->rx_dropped++;
			break;
		case TXRX_QUEUED:
			I802_DEBUG_INC(local->rx_handlers_queued);
			break;
		}
		break;
	}

	if (res == TXRX_DROP)
		dev_kfree_skb(rx->skb);
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

static void ieee80211_rx_michael_mic_report(struct net_device *dev,
					    struct ieee80211_hdr *hdr,
					    struct sta_info *sta,
					    struct ieee80211_txrx_data *rx)
{
	int keyidx, hdrlen;
	DECLARE_MAC_BUF(mac);
	DECLARE_MAC_BUF(mac2);

	hdrlen = ieee80211_get_hdrlen_from_skb(rx->skb);
	if (rx->skb->len >= hdrlen + 4)
		keyidx = rx->skb->data[hdrlen + 3] >> 6;
	else
		keyidx = -1;

	if (net_ratelimit())
		printk(KERN_DEBUG "%s: TKIP hwaccel reported Michael MIC "
		       "failure from %s to %s keyidx=%d\n",
		       dev->name, print_mac(mac, hdr->addr2),
		       print_mac(mac2, hdr->addr1), keyidx);

	if (!sta) {
		/*
		 * Some hardware seem to generate incorrect Michael MIC
		 * reports; ignore them to avoid triggering countermeasures.
		 */
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: ignored spurious Michael MIC "
			       "error for unknown address %s\n",
			       dev->name, print_mac(mac, hdr->addr2));
		goto ignore;
	}

	if (!(rx->fc & IEEE80211_FCTL_PROTECTED)) {
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: ignored spurious Michael MIC "
			       "error for a frame with no PROTECTED flag (src "
			       "%s)\n", dev->name, print_mac(mac, hdr->addr2));
		goto ignore;
	}

	if (rx->sdata->type == IEEE80211_IF_TYPE_AP && keyidx) {
		/*
		 * APs with pairwise keys should never receive Michael MIC
		 * errors for non-zero keyidx because these are reserved for
		 * group keys and only the AP is sending real multicast
		 * frames in the BSS.
		 */
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: ignored Michael MIC error for "
			       "a frame with non-zero keyidx (%d)"
			       " (src %s)\n", dev->name, keyidx,
			       print_mac(mac, hdr->addr2));
		goto ignore;
	}

	if ((rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA &&
	    ((rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_MGMT ||
	     (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_AUTH)) {
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: ignored spurious Michael MIC "
			       "error for a frame that cannot be encrypted "
			       "(fc=0x%04x) (src %s)\n",
			       dev->name, rx->fc, print_mac(mac, hdr->addr2));
		goto ignore;
	}

	mac80211_ev_michael_mic_failure(rx->dev, keyidx, hdr);
 ignore:
	dev_kfree_skb(rx->skb);
	rx->skb = NULL;
}

ieee80211_rx_handler ieee80211_rx_handlers[] =
{
	ieee80211_rx_h_if_stats,
	ieee80211_rx_h_passive_scan,
	ieee80211_rx_h_check,
	ieee80211_rx_h_decrypt,
	ieee80211_rx_h_sta_process,
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

/* main receive path */

static int prepare_for_handlers(struct ieee80211_sub_if_data *sdata,
				u8 *bssid, struct ieee80211_txrx_data *rx,
				struct ieee80211_hdr *hdr)
{
	int multicast = is_multicast_ether_addr(hdr->addr1);

	switch (sdata->type) {
	case IEEE80211_IF_TYPE_STA:
		if (!bssid)
			return 0;
		if (!ieee80211_bssid_match(bssid, sdata->u.sta.bssid)) {
			if (!(rx->flags & IEEE80211_TXRXD_RXIN_SCAN))
				return 0;
			rx->flags &= ~IEEE80211_TXRXD_RXRA_MATCH;
		} else if (!multicast &&
			   compare_ether_addr(sdata->dev->dev_addr,
					      hdr->addr1) != 0) {
			if (!(sdata->dev->flags & IFF_PROMISC))
				return 0;
			rx->flags &= ~IEEE80211_TXRXD_RXRA_MATCH;
		}
		break;
	case IEEE80211_IF_TYPE_IBSS:
		if (!bssid)
			return 0;
		if (!ieee80211_bssid_match(bssid, sdata->u.sta.bssid)) {
			if (!(rx->flags & IEEE80211_TXRXD_RXIN_SCAN))
				return 0;
			rx->flags &= ~IEEE80211_TXRXD_RXRA_MATCH;
		} else if (!multicast &&
			   compare_ether_addr(sdata->dev->dev_addr,
					      hdr->addr1) != 0) {
			if (!(sdata->dev->flags & IFF_PROMISC))
				return 0;
			rx->flags &= ~IEEE80211_TXRXD_RXRA_MATCH;
		} else if (!rx->sta)
			rx->sta = ieee80211_ibss_add_sta(sdata->dev, rx->skb,
							 bssid, hdr->addr2);
		break;
	case IEEE80211_IF_TYPE_VLAN:
	case IEEE80211_IF_TYPE_AP:
		if (!bssid) {
			if (compare_ether_addr(sdata->dev->dev_addr,
					       hdr->addr1))
				return 0;
		} else if (!ieee80211_bssid_match(bssid,
					sdata->dev->dev_addr)) {
			if (!(rx->flags & IEEE80211_TXRXD_RXIN_SCAN))
				return 0;
			rx->flags &= ~IEEE80211_TXRXD_RXRA_MATCH;
		}
		if (sdata->dev == sdata->local->mdev &&
		    !(rx->flags & IEEE80211_TXRXD_RXIN_SCAN))
			/* do not receive anything via
			 * master device when not scanning */
			return 0;
		break;
	case IEEE80211_IF_TYPE_WDS:
		if (bssid ||
		    (rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA)
			return 0;
		if (compare_ether_addr(sdata->u.wds.remote_addr, hdr->addr2))
			return 0;
		break;
	case IEEE80211_IF_TYPE_MNTR:
		/* take everything */
		break;
	case IEEE80211_IF_TYPE_INVALID:
		/* should never get here */
		WARN_ON(1);
		break;
	}

	return 1;
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
	int prepres;
	struct ieee80211_sub_if_data *prev = NULL;
	struct sk_buff *skb_new;
	u8 *bssid;
	int hdrlen;

	/*
	 * key references and virtual interfaces are protected using RCU
	 * and this requires that we are in a read-side RCU section during
	 * receive processing
	 */
	rcu_read_lock();

	/*
	 * Frames with failed FCS/PLCP checksum are not returned,
	 * all other frames are returned without radiotap header
	 * if it was previously present.
	 * Also, frames with less than 16 bytes are dropped.
	 */
	skb = ieee80211_rx_monitor(local, skb, status);
	if (!skb) {
		rcu_read_unlock();
		return;
	}

	hdr = (struct ieee80211_hdr *) skb->data;
	memset(&rx, 0, sizeof(rx));
	rx.skb = skb;
	rx.local = local;

	rx.u.rx.status = status;
	rx.fc = le16_to_cpu(hdr->frame_control);
	type = rx.fc & IEEE80211_FCTL_FTYPE;

	/*
	 * Drivers are required to align the payload data to a four-byte
	 * boundary, so the last two bits of the address where it starts
	 * may not be set. The header is required to be directly before
	 * the payload data, padding like atheros hardware adds which is
	 * inbetween the 802.11 header and the payload is not supported,
	 * the driver is required to move the 802.11 header further back
	 * in that case.
	 */
	hdrlen = ieee80211_get_hdrlen(rx.fc);
	WARN_ON_ONCE(((unsigned long)(skb->data + hdrlen)) & 3);

	if (type == IEEE80211_FTYPE_DATA || type == IEEE80211_FTYPE_MGMT)
		local->dot11ReceivedFragmentCount++;

	sta = rx.sta = sta_info_get(local, hdr->addr2);
	if (sta) {
		rx.dev = rx.sta->dev;
		rx.sdata = IEEE80211_DEV_TO_SUB_IF(rx.dev);
	}

	if ((status->flag & RX_FLAG_MMIC_ERROR)) {
		ieee80211_rx_michael_mic_report(local->mdev, hdr, sta, &rx);
		goto end;
	}

	if (unlikely(local->sta_scanning))
		rx.flags |= IEEE80211_TXRXD_RXIN_SCAN;

	if (__ieee80211_invoke_rx_handlers(local, local->rx_pre_handlers, &rx,
					   sta) != TXRX_CONTINUE)
		goto end;
	skb = rx.skb;

	if (sta && !(sta->flags & (WLAN_STA_WDS | WLAN_STA_ASSOC_AP)) &&
	    !atomic_read(&local->iff_promiscs) &&
	    !is_multicast_ether_addr(hdr->addr1)) {
		rx.flags |= IEEE80211_TXRXD_RXRA_MATCH;
		ieee80211_invoke_rx_handlers(local, local->rx_handlers, &rx,
					     rx.sta);
		sta_info_put(sta);
		rcu_read_unlock();
		return;
	}

	bssid = ieee80211_get_bssid(hdr, skb->len);

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;

		if (sdata->type == IEEE80211_IF_TYPE_MNTR)
			continue;

		rx.flags |= IEEE80211_TXRXD_RXRA_MATCH;
		prepres = prepare_for_handlers(sdata, bssid, &rx, hdr);
		/* prepare_for_handlers can change sta */
		sta = rx.sta;

		if (!prepres)
			continue;

		/*
		 * frame is destined for this interface, but if it's not
		 * also for the previous one we handle that after the
		 * loop to avoid copying the SKB once too much
		 */

		if (!prev) {
			prev = sdata;
			continue;
		}

		/*
		 * frame was destined for the previous interface
		 * so invoke RX handlers for it
		 */

		skb_new = skb_copy(skb, GFP_ATOMIC);
		if (!skb_new) {
			if (net_ratelimit())
				printk(KERN_DEBUG "%s: failed to copy "
				       "multicast frame for %s",
				       wiphy_name(local->hw.wiphy),
				       prev->dev->name);
			continue;
		}
		rx.skb = skb_new;
		rx.dev = prev->dev;
		rx.sdata = prev;
		ieee80211_invoke_rx_handlers(local, local->rx_handlers,
					     &rx, sta);
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

 end:
	rcu_read_unlock();

	if (sta)
		sta_info_put(sta);
}
EXPORT_SYMBOL(__ieee80211_rx);

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
