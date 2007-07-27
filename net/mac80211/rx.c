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
#include <net/iw_handler.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>

#include "ieee80211_i.h"
#include "ieee80211_led.h"
#include "ieee80211_common.h"
#include "wep.h"
#include "wpa.h"
#include "tkip.h"
#include "wme.h"

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

static ieee80211_txrx_result
ieee80211_rx_h_monitor(struct ieee80211_txrx_data *rx)
{
	if (rx->sdata->type == IEEE80211_IF_TYPE_MNTR) {
		ieee80211_rx_monitor(rx->dev, rx->skb, rx->u.rx.status);
		return TXRX_QUEUED;
	}

	if (rx->u.rx.status->flag & RX_FLAG_RADIOTAP)
		skb_pull(rx->skb, ieee80211_get_radiotap_len(rx->skb->data));

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

	if (unlikely(rx->u.rx.in_scan)) {
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

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_rx_h_load_key(struct ieee80211_txrx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;
	int always_sta_key;

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
	while ((skb = __skb_dequeue(&entry->skb_list))) {
		memcpy(skb_put(rx->skb, skb->len), skb->data, skb->len);
		dev_kfree_skb(skb);
	}

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

ieee80211_rx_handler ieee80211_rx_handlers[] =
{
	ieee80211_rx_h_if_stats,
	ieee80211_rx_h_monitor,
	ieee80211_rx_h_passive_scan,
	ieee80211_rx_h_check,
	ieee80211_rx_h_load_key,
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
			if (!rx->u.rx.in_scan)
				return 0;
			rx->u.rx.ra_match = 0;
		} else if (!multicast &&
			   compare_ether_addr(sdata->dev->dev_addr,
					      hdr->addr1) != 0) {
			if (!sdata->promisc)
				return 0;
			rx->u.rx.ra_match = 0;
		}
		break;
	case IEEE80211_IF_TYPE_IBSS:
		if (!bssid)
			return 0;
		if (!ieee80211_bssid_match(bssid, sdata->u.sta.bssid)) {
			if (!rx->u.rx.in_scan)
				return 0;
			rx->u.rx.ra_match = 0;
		} else if (!multicast &&
			   compare_ether_addr(sdata->dev->dev_addr,
					      hdr->addr1) != 0) {
			if (!sdata->promisc)
				return 0;
			rx->u.rx.ra_match = 0;
		} else if (!rx->sta)
			rx->sta = ieee80211_ibss_add_sta(sdata->dev, rx->skb,
							 bssid, hdr->addr2);
		break;
	case IEEE80211_IF_TYPE_AP:
		if (!bssid) {
			if (compare_ether_addr(sdata->dev->dev_addr,
					       hdr->addr1))
				return 0;
		} else if (!ieee80211_bssid_match(bssid,
					sdata->dev->dev_addr)) {
			if (!rx->u.rx.in_scan)
				return 0;
			rx->u.rx.ra_match = 0;
		}
		if (sdata->dev == sdata->local->mdev && !rx->u.rx.in_scan)
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
	int radiotap_len = 0, prepres;
	struct ieee80211_sub_if_data *prev = NULL;
	struct sk_buff *skb_new;
	u8 *bssid;

	if (status->flag & RX_FLAG_RADIOTAP) {
		radiotap_len = ieee80211_get_radiotap_len(skb->data);
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

	if (skb->len >= 16) {
		sta = rx.sta = sta_info_get(local, hdr->addr2);
		if (sta) {
			rx.dev = rx.sta->dev;
			rx.sdata = IEEE80211_DEV_TO_SUB_IF(rx.dev);
		}
	} else
		sta = rx.sta = NULL;

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
	    !local->iff_promiscs && !is_multicast_ether_addr(hdr->addr1)) {
		rx.u.rx.ra_match = 1;
		ieee80211_invoke_rx_handlers(local, local->rx_handlers, &rx,
					     rx.sta);
		sta_info_put(sta);
		return;
	}

	bssid = ieee80211_get_bssid(hdr, skb->len - radiotap_len);

	read_lock(&local->sub_if_lock);
	list_for_each_entry(sdata, &local->sub_if_list, list) {
		rx.u.rx.ra_match = 1;

		prepres = prepare_for_handlers(sdata, bssid, &rx, hdr);
		/* prepare_for_handlers can change sta */
		sta = rx.sta;

		if (!prepres)
			continue;

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
			ieee80211_invoke_rx_handlers(local, local->rx_handlers,
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

 end:
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
