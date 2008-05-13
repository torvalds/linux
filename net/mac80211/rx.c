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

#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rcupdate.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>

#include "ieee80211_i.h"
#include "led.h"
#include "mesh.h"
#include "wep.h"
#include "wpa.h"
#include "tkip.h"
#include "wme.h"

u8 ieee80211_sta_manage_reorder_buf(struct ieee80211_hw *hw,
				struct tid_ampdu_rx *tid_agg_rx,
				struct sk_buff *skb, u16 mpdu_seq_num,
				int bar_req);
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
	if (((hdr->frame_control & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
			cpu_to_le16(IEEE80211_FTYPE_CTL)) &&
	    ((hdr->frame_control & cpu_to_le16(IEEE80211_FCTL_STYPE)) !=
			cpu_to_le16(IEEE80211_STYPE_PSPOLL)) &&
	    ((hdr->frame_control & cpu_to_le16(IEEE80211_FCTL_STYPE)) !=
			cpu_to_le16(IEEE80211_STYPE_BACK_REQ)))
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
		     struct ieee80211_rx_status *status,
		     struct ieee80211_rate *rate)
{
	struct ieee80211_sub_if_data *sdata;
	int needed_headroom = 0;
	struct ieee80211_radiotap_header *rthdr;
	__le64 *rttsft = NULL;
	struct ieee80211_rtap_fixed_data {
		u8 flags;
		u8 rate;
		__le16 chan_freq;
		__le16 chan_flags;
		u8 antsignal;
		u8 padding_for_rxflags;
		__le16 rx_flags;
	} __attribute__ ((packed)) *rtfixed;
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
		/* room for radiotap header, always present fields and TSFT */
		needed_headroom = sizeof(*rthdr) + sizeof(*rtfixed) + 8;

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
		    pskb_expand_head(skb, needed_headroom, 0, GFP_ATOMIC)) {
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
		rtfixed = (void *) skb_push(skb, sizeof(*rtfixed));
		rtap_len = sizeof(*rthdr) + sizeof(*rtfixed);
		if (status->flag & RX_FLAG_TSFT) {
			rttsft = (void *) skb_push(skb, sizeof(*rttsft));
			rtap_len += 8;
		}
		rthdr = (void *) skb_push(skb, sizeof(*rthdr));
		memset(rthdr, 0, sizeof(*rthdr));
		memset(rtfixed, 0, sizeof(*rtfixed));
		rthdr->it_present =
			cpu_to_le32((1 << IEEE80211_RADIOTAP_FLAGS) |
				    (1 << IEEE80211_RADIOTAP_RATE) |
				    (1 << IEEE80211_RADIOTAP_CHANNEL) |
				    (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL) |
				    (1 << IEEE80211_RADIOTAP_RX_FLAGS));
		rtfixed->flags = 0;
		if (local->hw.flags & IEEE80211_HW_RX_INCLUDES_FCS)
			rtfixed->flags |= IEEE80211_RADIOTAP_F_FCS;

		if (rttsft) {
			*rttsft = cpu_to_le64(status->mactime);
			rthdr->it_present |=
				cpu_to_le32(1 << IEEE80211_RADIOTAP_TSFT);
		}

		/* FIXME: when radiotap gets a 'bad PLCP' flag use it here */
		rtfixed->rx_flags = 0;
		if (status->flag &
		    (RX_FLAG_FAILED_FCS_CRC | RX_FLAG_FAILED_PLCP_CRC))
			rtfixed->rx_flags |=
				cpu_to_le16(IEEE80211_RADIOTAP_F_RX_BADFCS);

		rtfixed->rate = rate->bitrate / 5;

		rtfixed->chan_freq = cpu_to_le16(status->freq);

		if (status->band == IEEE80211_BAND_5GHZ)
			rtfixed->chan_flags =
				cpu_to_le16(IEEE80211_CHAN_OFDM |
					    IEEE80211_CHAN_5GHZ);
		else
			rtfixed->chan_flags =
				cpu_to_le16(IEEE80211_CHAN_DYN |
					    IEEE80211_CHAN_2GHZ);

		rtfixed->antsignal = status->ssi;
		rthdr->it_len = cpu_to_le16(rtap_len);
	}

	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;

		if (sdata->vif.type != IEEE80211_IF_TYPE_MNTR)
			continue;

		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES)
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


static void ieee80211_parse_qos(struct ieee80211_rx_data *rx)
{
	u8 *data = rx->skb->data;
	int tid;

	/* does the frame have a qos control field? */
	if (WLAN_FC_IS_QOS_DATA(rx->fc)) {
		u8 *qc = data + ieee80211_get_hdrlen(rx->fc) - QOS_CONTROL_LEN;
		/* frame has qos control */
		tid = qc[0] & QOS_CONTROL_TID_MASK;
		if (qc[0] & IEEE80211_QOS_CONTROL_A_MSDU_PRESENT)
			rx->flags |= IEEE80211_RX_AMSDU;
		else
			rx->flags &= ~IEEE80211_RX_AMSDU;
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

	rx->queue = tid;
	/* Set skb->priority to 1d tag if highest order bit of TID is not set.
	 * For now, set skb->priority to 0 for other cases. */
	rx->skb->priority = (tid > 7) ? 0 : tid;
}

static void ieee80211_verify_ip_alignment(struct ieee80211_rx_data *rx)
{
#ifdef CONFIG_MAC80211_DEBUG_PACKET_ALIGNMENT
	int hdrlen;

	if (!WLAN_FC_DATA_PRESENT(rx->fc))
		return;

	/*
	 * Drivers are required to align the payload data in a way that
	 * guarantees that the contained IP header is aligned to a four-
	 * byte boundary. In the case of regular frames, this simply means
	 * aligning the payload to a four-byte boundary (because either
	 * the IP header is directly contained, or IV/RFC1042 headers that
	 * have a length divisible by four are in front of it.
	 *
	 * With A-MSDU frames, however, the payload data address must
	 * yield two modulo four because there are 14-byte 802.3 headers
	 * within the A-MSDU frames that push the IP header further back
	 * to a multiple of four again. Thankfully, the specs were sane
	 * enough this time around to require padding each A-MSDU subframe
	 * to a length that is a multiple of four.
	 *
	 * Padding like atheros hardware adds which is inbetween the 802.11
	 * header and the payload is not supported, the driver is required
	 * to move the 802.11 header further back in that case.
	 */
	hdrlen = ieee80211_get_hdrlen(rx->fc);
	if (rx->flags & IEEE80211_RX_AMSDU)
		hdrlen += ETH_HLEN;
	WARN_ON_ONCE(((unsigned long)(rx->skb->data + hdrlen)) & 3);
#endif
}


static u32 ieee80211_rx_load_stats(struct ieee80211_local *local,
				   struct sk_buff *skb,
				   struct ieee80211_rx_status *status,
				   struct ieee80211_rate *rate)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u32 load = 0, hdrtime;

	/* Estimate total channel use caused by this frame */

	/* 1 bit at 1 Mbit/s takes 1 usec; in channel_use values,
	 * 1 usec = 1/8 * (1080 / 10) = 13.5 */

	if (status->band == IEEE80211_BAND_5GHZ ||
	    (status->band == IEEE80211_BAND_5GHZ &&
	     rate->flags & IEEE80211_RATE_ERP_G))
		hdrtime = CHAN_UTIL_HDR_SHORT;
	else
		hdrtime = CHAN_UTIL_HDR_LONG;

	load = hdrtime;
	if (!is_multicast_ether_addr(hdr->addr1))
		load += hdrtime;

	/* TODO: optimise again */
	load += skb->len * CHAN_UTIL_RATE_LCM / rate->bitrate;

	/* Divide channel_use by 8 to avoid wrapping around the counter */
	load >>= CHAN_UTIL_SHIFT;

	return load;
}

/* rx handlers */

static ieee80211_rx_result
ieee80211_rx_h_if_stats(struct ieee80211_rx_data *rx)
{
	if (rx->sta)
		rx->sta->channel_use_raw += rx->load;
	rx->sdata->channel_use_raw += rx->load;
	return RX_CONTINUE;
}

static ieee80211_rx_result
ieee80211_rx_h_passive_scan(struct ieee80211_rx_data *rx)
{
	struct ieee80211_local *local = rx->local;
	struct sk_buff *skb = rx->skb;

	if (unlikely(local->sta_hw_scanning))
		return ieee80211_sta_rx_scan(rx->dev, skb, rx->status);

	if (unlikely(local->sta_sw_scanning)) {
		/* drop all the other packets during a software scan anyway */
		if (ieee80211_sta_rx_scan(rx->dev, skb, rx->status)
		    != RX_QUEUED)
			dev_kfree_skb(skb);
		return RX_QUEUED;
	}

	if (unlikely(rx->flags & IEEE80211_RX_IN_SCAN)) {
		/* scanning finished during invoking of handlers */
		I802_DEBUG_INC(local->rx_handlers_drop_passive_scan);
		return RX_DROP_UNUSABLE;
	}

	return RX_CONTINUE;
}

static ieee80211_rx_result
ieee80211_rx_mesh_check(struct ieee80211_rx_data *rx)
{
	int hdrlen = ieee80211_get_hdrlen(rx->fc);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;

#define msh_h_get(h, l) ((struct ieee80211s_hdr *) ((u8 *)h + l))

	if ((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) {
		if (!((rx->fc & IEEE80211_FCTL_FROMDS) &&
		      (rx->fc & IEEE80211_FCTL_TODS)))
			return RX_DROP_MONITOR;
		if (memcmp(hdr->addr4, rx->dev->dev_addr, ETH_ALEN) == 0)
			return RX_DROP_MONITOR;
	}

	/* If there is not an established peer link and this is not a peer link
	 * establisment frame, beacon or probe, drop the frame.
	 */

	if (!rx->sta || sta_plink_state(rx->sta) != PLINK_ESTAB) {
		struct ieee80211_mgmt *mgmt;

		if ((rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_MGMT)
			return RX_DROP_MONITOR;

		switch (rx->fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_ACTION:
			mgmt = (struct ieee80211_mgmt *)hdr;
			if (mgmt->u.action.category != PLINK_CATEGORY)
				return RX_DROP_MONITOR;
			/* fall through on else */
		case IEEE80211_STYPE_PROBE_REQ:
		case IEEE80211_STYPE_PROBE_RESP:
		case IEEE80211_STYPE_BEACON:
			return RX_CONTINUE;
			break;
		default:
			return RX_DROP_MONITOR;
		}

	 } else if ((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA &&
		    is_multicast_ether_addr(hdr->addr1) &&
		    mesh_rmc_check(hdr->addr4, msh_h_get(hdr, hdrlen), rx->dev))
		return RX_DROP_MONITOR;
#undef msh_h_get

	return RX_CONTINUE;
}


static ieee80211_rx_result
ieee80211_rx_h_check(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *) rx->skb->data;

	/* Drop duplicate 802.11 retransmissions (IEEE 802.11 Chap. 9.2.9) */
	if (rx->sta && !is_multicast_ether_addr(hdr->addr1)) {
		if (unlikely(rx->fc & IEEE80211_FCTL_RETRY &&
			     rx->sta->last_seq_ctrl[rx->queue] ==
			     hdr->seq_ctrl)) {
			if (rx->flags & IEEE80211_RX_RA_MATCH) {
				rx->local->dot11FrameDuplicateCount++;
				rx->sta->num_duplicates++;
			}
			return RX_DROP_MONITOR;
		} else
			rx->sta->last_seq_ctrl[rx->queue] = hdr->seq_ctrl;
	}

	if (unlikely(rx->skb->len < 16)) {
		I802_DEBUG_INC(rx->local->rx_handlers_drop_short);
		return RX_DROP_MONITOR;
	}

	/* Drop disallowed frame classes based on STA auth/assoc state;
	 * IEEE 802.11, Chap 5.5.
	 *
	 * 80211.o does filtering only based on association state, i.e., it
	 * drops Class 3 frames from not associated stations. hostapd sends
	 * deauth/disassoc frames when needed. In addition, hostapd is
	 * responsible for filtering on both auth and assoc states.
	 */

	if (ieee80211_vif_is_mesh(&rx->sdata->vif))
		return ieee80211_rx_mesh_check(rx);

	if (unlikely(((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA ||
		      ((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL &&
		       (rx->fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PSPOLL)) &&
		     rx->sdata->vif.type != IEEE80211_IF_TYPE_IBSS &&
		     (!rx->sta || !(rx->sta->flags & WLAN_STA_ASSOC)))) {
		if ((!(rx->fc & IEEE80211_FCTL_FROMDS) &&
		     !(rx->fc & IEEE80211_FCTL_TODS) &&
		     (rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA)
		    || !(rx->flags & IEEE80211_RX_RA_MATCH)) {
			/* Drop IBSS frames and frames for other hosts
			 * silently. */
			return RX_DROP_MONITOR;
		}

		return RX_DROP_MONITOR;
	}

	return RX_CONTINUE;
}


static ieee80211_rx_result
ieee80211_rx_h_decrypt(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;
	int keyidx;
	int hdrlen;
	ieee80211_rx_result result = RX_DROP_UNUSABLE;
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
		return RX_CONTINUE;

	/*
	 * No point in finding a key and decrypting if the frame is neither
	 * addressed to us nor a multicast frame.
	 */
	if (!(rx->flags & IEEE80211_RX_RA_MATCH))
		return RX_CONTINUE;

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
		if ((rx->status->flag & RX_FLAG_DECRYPTED) &&
		    (rx->status->flag & RX_FLAG_IV_STRIPPED))
			return RX_CONTINUE;

		hdrlen = ieee80211_get_hdrlen(rx->fc);

		if (rx->skb->len < 8 + hdrlen)
			return RX_DROP_UNUSABLE; /* TODO: count this? */

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
		return RX_DROP_MONITOR;
	}

	/* Check for weak IVs if possible */
	if (rx->sta && rx->key->conf.alg == ALG_WEP &&
	    ((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) &&
	    (!(rx->status->flag & RX_FLAG_IV_STRIPPED) ||
	     !(rx->status->flag & RX_FLAG_DECRYPTED)) &&
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
	rx->status->flag |= RX_FLAG_DECRYPTED;

	return result;
}

static void ap_sta_ps_start(struct net_device *dev, struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata;
	DECLARE_MAC_BUF(mac);

	sdata = sta->sdata;

	if (sdata->bss)
		atomic_inc(&sdata->bss->num_sta_ps);
	sta->flags |= WLAN_STA_PS;
	sta->flags &= ~WLAN_STA_PSPOLL;
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

	sdata = sta->sdata;

	if (sdata->bss)
		atomic_dec(&sdata->bss->num_sta_ps);

	sta->flags &= ~(WLAN_STA_PS | WLAN_STA_PSPOLL);

	if (!skb_queue_empty(&sta->ps_tx_buf))
		sta_info_clear_tim_bit(sta);

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

static ieee80211_rx_result
ieee80211_rx_h_sta_process(struct ieee80211_rx_data *rx)
{
	struct sta_info *sta = rx->sta;
	struct net_device *dev = rx->dev;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;

	if (!sta)
		return RX_CONTINUE;

	/* Update last_rx only for IBSS packets which are for the current
	 * BSSID to avoid keeping the current IBSS network alive in cases where
	 * other STAs are using different BSSID. */
	if (rx->sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		u8 *bssid = ieee80211_get_bssid(hdr, rx->skb->len,
						IEEE80211_IF_TYPE_IBSS);
		if (compare_ether_addr(bssid, rx->sdata->u.sta.bssid) == 0)
			sta->last_rx = jiffies;
	} else
	if (!is_multicast_ether_addr(hdr->addr1) ||
	    rx->sdata->vif.type == IEEE80211_IF_TYPE_STA) {
		/* Update last_rx only for unicast frames in order to prevent
		 * the Probe Request frames (the only broadcast frames from a
		 * STA in infrastructure mode) from keeping a connection alive.
		 * Mesh beacons will update last_rx when if they are found to
		 * match the current local configuration when processed.
		 */
		sta->last_rx = jiffies;
	}

	if (!(rx->flags & IEEE80211_RX_RA_MATCH))
		return RX_CONTINUE;

	sta->rx_fragments++;
	sta->rx_bytes += rx->skb->len;
	sta->last_rssi = rx->status->ssi;
	sta->last_signal = rx->status->signal;
	sta->last_noise = rx->status->noise;

	if (!(rx->fc & IEEE80211_FCTL_MOREFRAGS)) {
		/* Change STA power saving mode only in the end of a frame
		 * exchange sequence */
		if ((sta->flags & WLAN_STA_PS) && !(rx->fc & IEEE80211_FCTL_PM))
			rx->sent_ps_buffered += ap_sta_ps_end(dev, sta);
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
		return RX_QUEUED;
	}

	return RX_CONTINUE;
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

		if (time_after(jiffies, entry->first_frag_time + 2 * HZ)) {
			__skb_queue_purge(&entry->skb_list);
			continue;
		}
		return entry;
	}

	return NULL;
}

static ieee80211_rx_result
ieee80211_rx_h_defragment(struct ieee80211_rx_data *rx)
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
						 rx->queue, &(rx->skb));
		if (rx->key && rx->key->conf.alg == ALG_CCMP &&
		    (rx->fc & IEEE80211_FCTL_PROTECTED)) {
			/* Store CCMP PN so that we can verify that the next
			 * fragment has a sequential PN value. */
			entry->ccmp = 1;
			memcpy(entry->last_pn,
			       rx->key->u.ccmp.rx_pn[rx->queue],
			       CCMP_PN_LEN);
		}
		return RX_QUEUED;
	}

	/* This is a fragment for a frame that should already be pending in
	 * fragment cache. Add this fragment to the end of the pending entry.
	 */
	entry = ieee80211_reassemble_find(rx->sdata, rx->fc, frag, seq,
					  rx->queue, hdr);
	if (!entry) {
		I802_DEBUG_INC(rx->local->rx_handlers_drop_defrag);
		return RX_DROP_MONITOR;
	}

	/* Verify that MPDUs within one MSDU have sequential PN values.
	 * (IEEE 802.11i, 8.3.3.4.5) */
	if (entry->ccmp) {
		int i;
		u8 pn[CCMP_PN_LEN], *rpn;
		if (!rx->key || rx->key->conf.alg != ALG_CCMP)
			return RX_DROP_UNUSABLE;
		memcpy(pn, entry->last_pn, CCMP_PN_LEN);
		for (i = CCMP_PN_LEN - 1; i >= 0; i--) {
			pn[i]++;
			if (pn[i])
				break;
		}
		rpn = rx->key->u.ccmp.rx_pn[rx->queue];
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
			return RX_DROP_UNUSABLE;
		}
		memcpy(entry->last_pn, pn, CCMP_PN_LEN);
	}

	skb_pull(rx->skb, ieee80211_get_hdrlen(rx->fc));
	__skb_queue_tail(&entry->skb_list, rx->skb);
	entry->last_frag = frag;
	entry->extra_len += rx->skb->len;
	if (rx->fc & IEEE80211_FCTL_MOREFRAGS) {
		rx->skb = NULL;
		return RX_QUEUED;
	}

	rx->skb = __skb_dequeue(&entry->skb_list);
	if (skb_tailroom(rx->skb) < entry->extra_len) {
		I802_DEBUG_INC(rx->local->rx_expand_skb_head2);
		if (unlikely(pskb_expand_head(rx->skb, 0, entry->extra_len,
					      GFP_ATOMIC))) {
			I802_DEBUG_INC(rx->local->rx_handlers_drop_defrag);
			__skb_queue_purge(&entry->skb_list);
			return RX_DROP_UNUSABLE;
		}
	}
	while ((skb = __skb_dequeue(&entry->skb_list))) {
		memcpy(skb_put(rx->skb, skb->len), skb->data, skb->len);
		dev_kfree_skb(skb);
	}

	/* Complete frame has been reassembled - process it now */
	rx->flags |= IEEE80211_RX_FRAGMENTED;

 out:
	if (rx->sta)
		rx->sta->rx_packets++;
	if (is_multicast_ether_addr(hdr->addr1))
		rx->local->dot11MulticastReceivedFrameCount++;
	else
		ieee80211_led_rx(rx->local);
	return RX_CONTINUE;
}

static ieee80211_rx_result
ieee80211_rx_h_ps_poll(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(rx->dev);
	struct sk_buff *skb;
	int no_pending_pkts;
	DECLARE_MAC_BUF(mac);

	if (likely(!rx->sta ||
		   (rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_CTL ||
		   (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_PSPOLL ||
		   !(rx->flags & IEEE80211_RX_RA_MATCH)))
		return RX_CONTINUE;

	if ((sdata->vif.type != IEEE80211_IF_TYPE_AP) &&
	    (sdata->vif.type != IEEE80211_IF_TYPE_VLAN))
		return RX_DROP_UNUSABLE;

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

		/*
		 * Tell TX path to send one frame even though the STA may
		 * still remain is PS mode after this frame exchange.
		 */
		rx->sta->flags |= WLAN_STA_PSPOLL;

#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		printk(KERN_DEBUG "STA %s aid %d: PS Poll (entries after %d)\n",
		       print_mac(mac, rx->sta->addr), rx->sta->aid,
		       skb_queue_len(&rx->sta->ps_tx_buf));
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */

		/* Use MoreData flag to indicate whether there are more
		 * buffered frames for this STA */
		if (no_pending_pkts)
			hdr->frame_control &= cpu_to_le16(~IEEE80211_FCTL_MOREDATA);
		else
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_MOREDATA);

		dev_queue_xmit(skb);

		if (no_pending_pkts)
			sta_info_clear_tim_bit(rx->sta);
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	} else if (!rx->sent_ps_buffered) {
		/*
		 * FIXME: This can be the result of a race condition between
		 *	  us expiring a frame and the station polling for it.
		 *	  Should we send it a null-func frame indicating we
		 *	  have nothing buffered for it?
		 */
		printk(KERN_DEBUG "%s: STA %s sent PS Poll even "
		       "though there is no buffered frames for it\n",
		       rx->dev->name, print_mac(mac, rx->sta->addr));
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
	}

	/* Free PS Poll skb here instead of returning RX_DROP that would
	 * count as an dropped frame. */
	dev_kfree_skb(rx->skb);

	return RX_QUEUED;
}

static ieee80211_rx_result
ieee80211_rx_h_remove_qos_control(struct ieee80211_rx_data *rx)
{
	u16 fc = rx->fc;
	u8 *data = rx->skb->data;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) data;

	if (!WLAN_FC_IS_QOS_DATA(fc))
		return RX_CONTINUE;

	/* remove the qos control field, update frame type and meta-data */
	memmove(data + 2, data, ieee80211_get_hdrlen(fc) - 2);
	hdr = (struct ieee80211_hdr *) skb_pull(rx->skb, 2);
	/* change frame type to non QOS */
	rx->fc = fc &= ~IEEE80211_STYPE_QOS_DATA;
	hdr->frame_control = cpu_to_le16(fc);

	return RX_CONTINUE;
}

static int
ieee80211_802_1x_port_control(struct ieee80211_rx_data *rx)
{
	if (unlikely(!rx->sta || !(rx->sta->flags & WLAN_STA_AUTHORIZED))) {
#ifdef CONFIG_MAC80211_DEBUG
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: dropped frame "
			       "(unauthorized port)\n", rx->dev->name);
#endif /* CONFIG_MAC80211_DEBUG */
		return -EACCES;
	}

	return 0;
}

static int
ieee80211_drop_unencrypted(struct ieee80211_rx_data *rx)
{
	/*
	 * Pass through unencrypted frames if the hardware has
	 * decrypted them already.
	 */
	if (rx->status->flag & RX_FLAG_DECRYPTED)
		return 0;

	/* Drop unencrypted frames if key is set. */
	if (unlikely(!(rx->fc & IEEE80211_FCTL_PROTECTED) &&
		     (rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA &&
		     (rx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_NULLFUNC &&
		     (rx->key || rx->sdata->drop_unencrypted)))
		return -EACCES;

	return 0;
}

static int
ieee80211_data_to_8023(struct ieee80211_rx_data *rx)
{
	struct net_device *dev = rx->dev;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) rx->skb->data;
	u16 fc, hdrlen, ethertype;
	u8 *payload;
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	struct sk_buff *skb = rx->skb;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	DECLARE_MAC_BUF(mac);
	DECLARE_MAC_BUF(mac2);
	DECLARE_MAC_BUF(mac3);
	DECLARE_MAC_BUF(mac4);

	fc = rx->fc;

	if (unlikely(!WLAN_FC_DATA_PRESENT(fc)))
		return -1;

	hdrlen = ieee80211_get_hdrlen(fc);

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		int meshhdrlen = ieee80211_get_mesh_hdrlen(
				(struct ieee80211s_hdr *) (skb->data + hdrlen));
		/* Copy on cb:
		 *  - mesh header: to be used for mesh forwarding
		 * decision. It will also be used as mesh header template at
		 * tx.c:ieee80211_subif_start_xmit() if interface
		 * type is mesh and skb->pkt_type == PACKET_OTHERHOST
		 *  - ta: to be used if a RERR needs to be sent.
		 */
		memcpy(skb->cb, skb->data + hdrlen, meshhdrlen);
		memcpy(MESH_PREQ(skb), hdr->addr2, ETH_ALEN);
		hdrlen += meshhdrlen;
	}

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

		if (unlikely(sdata->vif.type != IEEE80211_IF_TYPE_AP &&
			     sdata->vif.type != IEEE80211_IF_TYPE_VLAN)) {
			if (net_ratelimit())
				printk(KERN_DEBUG "%s: dropped ToDS frame "
				       "(BSSID=%s SA=%s DA=%s)\n",
				       dev->name,
				       print_mac(mac, hdr->addr1),
				       print_mac(mac2, hdr->addr2),
				       print_mac(mac3, hdr->addr3));
			return -1;
		}
		break;
	case (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS):
		/* RA TA DA SA */
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr4, ETH_ALEN);

		 if (unlikely(sdata->vif.type != IEEE80211_IF_TYPE_WDS &&
			     sdata->vif.type != IEEE80211_IF_TYPE_MESH_POINT)) {
			 if (net_ratelimit())
				 printk(KERN_DEBUG "%s: dropped FromDS&ToDS "
				       "frame (RA=%s TA=%s DA=%s SA=%s)\n",
				       rx->dev->name,
				       print_mac(mac, hdr->addr1),
				       print_mac(mac2, hdr->addr2),
				       print_mac(mac3, hdr->addr3),
				       print_mac(mac4, hdr->addr4));
			return -1;
		}
		break;
	case IEEE80211_FCTL_FROMDS:
		/* DA BSSID SA */
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr3, ETH_ALEN);

		if (sdata->vif.type != IEEE80211_IF_TYPE_STA ||
		    (is_multicast_ether_addr(dst) &&
		     !compare_ether_addr(src, dev->dev_addr)))
			return -1;
		break;
	case 0:
		/* DA SA BSSID */
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);

		if (sdata->vif.type != IEEE80211_IF_TYPE_IBSS) {
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: dropped IBSS frame "
				       "(DA=%s SA=%s BSSID=%s)\n",
				       dev->name,
				       print_mac(mac, hdr->addr1),
				       print_mac(mac2, hdr->addr2),
				       print_mac(mac3, hdr->addr3));
			}
			return -1;
		}
		break;
	}

	if (unlikely(skb->len - hdrlen < 8)) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: RX too short data frame "
			       "payload\n", dev->name);
		}
		return -1;
	}

	payload = skb->data + hdrlen;
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
	return 0;
}

/*
 * requires that rx->skb is a frame with ethernet header
 */
static bool ieee80211_frame_allowed(struct ieee80211_rx_data *rx)
{
	static const u8 pae_group_addr[ETH_ALEN]
		= { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x03 };
	struct ethhdr *ehdr = (struct ethhdr *) rx->skb->data;

	/*
	 * Allow EAPOL frames to us/the PAE group address regardless
	 * of whether the frame was encrypted or not.
	 */
	if (ehdr->h_proto == htons(ETH_P_PAE) &&
	    (compare_ether_addr(ehdr->h_dest, rx->dev->dev_addr) == 0 ||
	     compare_ether_addr(ehdr->h_dest, pae_group_addr) == 0))
		return true;

	if (ieee80211_802_1x_port_control(rx) ||
	    ieee80211_drop_unencrypted(rx))
		return false;

	return true;
}

/*
 * requires that rx->skb is a frame with ethernet header
 */
static void
ieee80211_deliver_skb(struct ieee80211_rx_data *rx)
{
	struct net_device *dev = rx->dev;
	struct ieee80211_local *local = rx->local;
	struct sk_buff *skb, *xmit_skb;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ethhdr *ehdr = (struct ethhdr *) rx->skb->data;
	struct sta_info *dsta;

	skb = rx->skb;
	xmit_skb = NULL;

	if (local->bridge_packets && (sdata->vif.type == IEEE80211_IF_TYPE_AP ||
				      sdata->vif.type == IEEE80211_IF_TYPE_VLAN) &&
	    (rx->flags & IEEE80211_RX_RA_MATCH)) {
		if (is_multicast_ether_addr(ehdr->h_dest)) {
			/*
			 * send multicast frames both to higher layers in
			 * local net stack and back to the wireless medium
			 */
			xmit_skb = skb_copy(skb, GFP_ATOMIC);
			if (!xmit_skb && net_ratelimit())
				printk(KERN_DEBUG "%s: failed to clone "
				       "multicast frame\n", dev->name);
		} else {
			dsta = sta_info_get(local, skb->data);
			if (dsta && dsta->sdata->dev == dev) {
				/*
				 * The destination station is associated to
				 * this AP (in this VLAN), so send the frame
				 * directly to it and do not pass it to local
				 * net stack.
				 */
				xmit_skb = skb;
				skb = NULL;
			}
		}
	}

	/* Mesh forwarding */
	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		u8 *mesh_ttl = &((struct ieee80211s_hdr *)skb->cb)->ttl;
		(*mesh_ttl)--;

		if (is_multicast_ether_addr(skb->data)) {
			if (*mesh_ttl > 0) {
				xmit_skb = skb_copy(skb, GFP_ATOMIC);
				if (xmit_skb)
					xmit_skb->pkt_type = PACKET_OTHERHOST;
				else if (net_ratelimit())
					printk(KERN_DEBUG "%s: failed to clone "
					       "multicast frame\n", dev->name);
			} else
				IEEE80211_IFSTA_MESH_CTR_INC(&sdata->u.sta,
							     dropped_frames_ttl);
		} else if (skb->pkt_type != PACKET_OTHERHOST &&
			compare_ether_addr(dev->dev_addr, skb->data) != 0) {
			if (*mesh_ttl == 0) {
				IEEE80211_IFSTA_MESH_CTR_INC(&sdata->u.sta,
							     dropped_frames_ttl);
				dev_kfree_skb(skb);
				skb = NULL;
			} else {
				xmit_skb = skb;
				xmit_skb->pkt_type = PACKET_OTHERHOST;
				if (!(dev->flags & IFF_PROMISC))
					skb  = NULL;
			}
		}
	}

	if (skb) {
		/* deliver to local stack */
		skb->protocol = eth_type_trans(skb, dev);
		memset(skb->cb, 0, sizeof(skb->cb));
		netif_rx(skb);
	}

	if (xmit_skb) {
		/* send to wireless media */
		xmit_skb->protocol = htons(ETH_P_802_3);
		skb_reset_network_header(xmit_skb);
		skb_reset_mac_header(xmit_skb);
		dev_queue_xmit(xmit_skb);
	}
}

static ieee80211_rx_result
ieee80211_rx_h_amsdu(struct ieee80211_rx_data *rx)
{
	struct net_device *dev = rx->dev;
	struct ieee80211_local *local = rx->local;
	u16 fc, ethertype;
	u8 *payload;
	struct sk_buff *skb = rx->skb, *frame = NULL;
	const struct ethhdr *eth;
	int remaining, err;
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	DECLARE_MAC_BUF(mac);

	fc = rx->fc;
	if (unlikely((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA))
		return RX_CONTINUE;

	if (unlikely(!WLAN_FC_DATA_PRESENT(fc)))
		return RX_DROP_MONITOR;

	if (!(rx->flags & IEEE80211_RX_AMSDU))
		return RX_CONTINUE;

	err = ieee80211_data_to_8023(rx);
	if (unlikely(err))
		return RX_DROP_UNUSABLE;

	skb->dev = dev;

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb->len;

	/* skip the wrapping header */
	eth = (struct ethhdr *) skb_pull(skb, sizeof(struct ethhdr));
	if (!eth)
		return RX_DROP_UNUSABLE;

	while (skb != frame) {
		u8 padding;
		__be16 len = eth->h_proto;
		unsigned int subframe_len = sizeof(struct ethhdr) + ntohs(len);

		remaining = skb->len;
		memcpy(dst, eth->h_dest, ETH_ALEN);
		memcpy(src, eth->h_source, ETH_ALEN);

		padding = ((4 - subframe_len) & 0x3);
		/* the last MSDU has no padding */
		if (subframe_len > remaining) {
			printk(KERN_DEBUG "%s: wrong buffer size\n", dev->name);
			return RX_DROP_UNUSABLE;
		}

		skb_pull(skb, sizeof(struct ethhdr));
		/* if last subframe reuse skb */
		if (remaining <= subframe_len + padding)
			frame = skb;
		else {
			frame = dev_alloc_skb(local->hw.extra_tx_headroom +
					      subframe_len);

			if (frame == NULL)
				return RX_DROP_UNUSABLE;

			skb_reserve(frame, local->hw.extra_tx_headroom +
				    sizeof(struct ethhdr));
			memcpy(skb_put(frame, ntohs(len)), skb->data,
				ntohs(len));

			eth = (struct ethhdr *) skb_pull(skb, ntohs(len) +
							padding);
			if (!eth) {
				printk(KERN_DEBUG "%s: wrong buffer size\n",
				       dev->name);
				dev_kfree_skb(frame);
				return RX_DROP_UNUSABLE;
			}
		}

		skb_reset_network_header(frame);
		frame->dev = dev;
		frame->priority = skb->priority;
		rx->skb = frame;

		payload = frame->data;
		ethertype = (payload[6] << 8) | payload[7];

		if (likely((compare_ether_addr(payload, rfc1042_header) == 0 &&
			    ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
			   compare_ether_addr(payload,
					      bridge_tunnel_header) == 0)) {
			/* remove RFC1042 or Bridge-Tunnel
			 * encapsulation and replace EtherType */
			skb_pull(frame, 6);
			memcpy(skb_push(frame, ETH_ALEN), src, ETH_ALEN);
			memcpy(skb_push(frame, ETH_ALEN), dst, ETH_ALEN);
		} else {
			memcpy(skb_push(frame, sizeof(__be16)),
			       &len, sizeof(__be16));
			memcpy(skb_push(frame, ETH_ALEN), src, ETH_ALEN);
			memcpy(skb_push(frame, ETH_ALEN), dst, ETH_ALEN);
		}

		if (!ieee80211_frame_allowed(rx)) {
			if (skb == frame) /* last frame */
				return RX_DROP_UNUSABLE;
			dev_kfree_skb(frame);
			continue;
		}

		ieee80211_deliver_skb(rx);
	}

	return RX_QUEUED;
}

static ieee80211_rx_result
ieee80211_rx_h_data(struct ieee80211_rx_data *rx)
{
	struct net_device *dev = rx->dev;
	u16 fc;
	int err;

	fc = rx->fc;
	if (unlikely((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA))
		return RX_CONTINUE;

	if (unlikely(!WLAN_FC_DATA_PRESENT(fc)))
		return RX_DROP_MONITOR;

	err = ieee80211_data_to_8023(rx);
	if (unlikely(err))
		return RX_DROP_UNUSABLE;

	if (!ieee80211_frame_allowed(rx))
		return RX_DROP_MONITOR;

	rx->skb->dev = dev;

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += rx->skb->len;

	ieee80211_deliver_skb(rx);

	return RX_QUEUED;
}

static ieee80211_rx_result
ieee80211_rx_h_ctrl(struct ieee80211_rx_data *rx)
{
	struct ieee80211_local *local = rx->local;
	struct ieee80211_hw *hw = &local->hw;
	struct sk_buff *skb = rx->skb;
	struct ieee80211_bar *bar = (struct ieee80211_bar *) skb->data;
	struct tid_ampdu_rx *tid_agg_rx;
	u16 start_seq_num;
	u16 tid;

	if (likely((rx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_CTL))
		return RX_CONTINUE;

	if ((rx->fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_BACK_REQ) {
		if (!rx->sta)
			return RX_CONTINUE;
		tid = le16_to_cpu(bar->control) >> 12;
		if (rx->sta->ampdu_mlme.tid_state_rx[tid]
					!= HT_AGG_STATE_OPERATIONAL)
			return RX_CONTINUE;
		tid_agg_rx = rx->sta->ampdu_mlme.tid_rx[tid];

		start_seq_num = le16_to_cpu(bar->start_seq_num) >> 4;

		/* reset session timer */
		if (tid_agg_rx->timeout) {
			unsigned long expires =
				jiffies + (tid_agg_rx->timeout / 1000) * HZ;
			mod_timer(&tid_agg_rx->session_timer, expires);
		}

		/* manage reordering buffer according to requested */
		/* sequence number */
		rcu_read_lock();
		ieee80211_sta_manage_reorder_buf(hw, tid_agg_rx, NULL,
						 start_seq_num, 1);
		rcu_read_unlock();
		return RX_DROP_UNUSABLE;
	}

	return RX_CONTINUE;
}

static ieee80211_rx_result
ieee80211_rx_h_mgmt(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata;

	if (!(rx->flags & IEEE80211_RX_RA_MATCH))
		return RX_DROP_MONITOR;

	sdata = IEEE80211_DEV_TO_SUB_IF(rx->dev);
	if ((sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	     sdata->vif.type == IEEE80211_IF_TYPE_IBSS ||
	     sdata->vif.type == IEEE80211_IF_TYPE_MESH_POINT) &&
	    !(sdata->flags & IEEE80211_SDATA_USERSPACE_MLME))
		ieee80211_sta_rx_mgmt(rx->dev, rx->skb, rx->status);
	else
		return RX_DROP_MONITOR;

	return RX_QUEUED;
}

static void ieee80211_rx_michael_mic_report(struct net_device *dev,
					    struct ieee80211_hdr *hdr,
					    struct ieee80211_rx_data *rx)
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

	if (!rx->sta) {
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

	if (rx->sdata->vif.type == IEEE80211_IF_TYPE_AP && keyidx) {
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

/* TODO: use IEEE80211_RX_FRAGMENTED */
static void ieee80211_rx_cooked_monitor(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local = rx->local;
	struct ieee80211_rtap_hdr {
		struct ieee80211_radiotap_header hdr;
		u8 flags;
		u8 rate;
		__le16 chan_freq;
		__le16 chan_flags;
	} __attribute__ ((packed)) *rthdr;
	struct sk_buff *skb = rx->skb, *skb2;
	struct net_device *prev_dev = NULL;
	struct ieee80211_rx_status *status = rx->status;

	if (rx->flags & IEEE80211_RX_CMNTR_REPORTED)
		goto out_free_skb;

	if (skb_headroom(skb) < sizeof(*rthdr) &&
	    pskb_expand_head(skb, sizeof(*rthdr), 0, GFP_ATOMIC))
		goto out_free_skb;

	rthdr = (void *)skb_push(skb, sizeof(*rthdr));
	memset(rthdr, 0, sizeof(*rthdr));
	rthdr->hdr.it_len = cpu_to_le16(sizeof(*rthdr));
	rthdr->hdr.it_present =
		cpu_to_le32((1 << IEEE80211_RADIOTAP_FLAGS) |
			    (1 << IEEE80211_RADIOTAP_RATE) |
			    (1 << IEEE80211_RADIOTAP_CHANNEL));

	rthdr->rate = rx->rate->bitrate / 5;
	rthdr->chan_freq = cpu_to_le16(status->freq);

	if (status->band == IEEE80211_BAND_5GHZ)
		rthdr->chan_flags = cpu_to_le16(IEEE80211_CHAN_OFDM |
						IEEE80211_CHAN_5GHZ);
	else
		rthdr->chan_flags = cpu_to_le16(IEEE80211_CHAN_DYN |
						IEEE80211_CHAN_2GHZ);

	skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;

		if (sdata->vif.type != IEEE80211_IF_TYPE_MNTR ||
		    !(sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES))
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
		skb = NULL;
	} else
		goto out_free_skb;

	rx->flags |= IEEE80211_RX_CMNTR_REPORTED;
	return;

 out_free_skb:
	dev_kfree_skb(skb);
}

typedef ieee80211_rx_result (*ieee80211_rx_handler)(struct ieee80211_rx_data *);
static ieee80211_rx_handler ieee80211_rx_handlers[] =
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
	ieee80211_rx_h_amsdu,
	ieee80211_rx_h_data,
	ieee80211_rx_h_ctrl,
	ieee80211_rx_h_mgmt,
	NULL
};

static void ieee80211_invoke_rx_handlers(struct ieee80211_sub_if_data *sdata,
					 struct ieee80211_rx_data *rx,
					 struct sk_buff *skb)
{
	ieee80211_rx_handler *handler;
	ieee80211_rx_result res = RX_DROP_MONITOR;

	rx->skb = skb;
	rx->sdata = sdata;
	rx->dev = sdata->dev;

	for (handler = ieee80211_rx_handlers; *handler != NULL; handler++) {
		res = (*handler)(rx);

		switch (res) {
		case RX_CONTINUE:
			continue;
		case RX_DROP_UNUSABLE:
		case RX_DROP_MONITOR:
			I802_DEBUG_INC(sdata->local->rx_handlers_drop);
			if (rx->sta)
				rx->sta->rx_dropped++;
			break;
		case RX_QUEUED:
			I802_DEBUG_INC(sdata->local->rx_handlers_queued);
			break;
		}
		break;
	}

	switch (res) {
	case RX_CONTINUE:
	case RX_DROP_MONITOR:
		ieee80211_rx_cooked_monitor(rx);
		break;
	case RX_DROP_UNUSABLE:
		dev_kfree_skb(rx->skb);
		break;
	}
}

/* main receive path */

static int prepare_for_handlers(struct ieee80211_sub_if_data *sdata,
				u8 *bssid, struct ieee80211_rx_data *rx,
				struct ieee80211_hdr *hdr)
{
	int multicast = is_multicast_ether_addr(hdr->addr1);

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_STA:
		if (!bssid)
			return 0;
		if (!ieee80211_bssid_match(bssid, sdata->u.sta.bssid)) {
			if (!(rx->flags & IEEE80211_RX_IN_SCAN))
				return 0;
			rx->flags &= ~IEEE80211_RX_RA_MATCH;
		} else if (!multicast &&
			   compare_ether_addr(sdata->dev->dev_addr,
					      hdr->addr1) != 0) {
			if (!(sdata->dev->flags & IFF_PROMISC))
				return 0;
			rx->flags &= ~IEEE80211_RX_RA_MATCH;
		}
		break;
	case IEEE80211_IF_TYPE_IBSS:
		if (!bssid)
			return 0;
		if ((rx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT &&
		    (rx->fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_BEACON)
			return 1;
		else if (!ieee80211_bssid_match(bssid, sdata->u.sta.bssid)) {
			if (!(rx->flags & IEEE80211_RX_IN_SCAN))
				return 0;
			rx->flags &= ~IEEE80211_RX_RA_MATCH;
		} else if (!multicast &&
			   compare_ether_addr(sdata->dev->dev_addr,
					      hdr->addr1) != 0) {
			if (!(sdata->dev->flags & IFF_PROMISC))
				return 0;
			rx->flags &= ~IEEE80211_RX_RA_MATCH;
		} else if (!rx->sta)
			rx->sta = ieee80211_ibss_add_sta(sdata->dev, rx->skb,
							 bssid, hdr->addr2);
		break;
	case IEEE80211_IF_TYPE_MESH_POINT:
		if (!multicast &&
		    compare_ether_addr(sdata->dev->dev_addr,
				       hdr->addr1) != 0) {
			if (!(sdata->dev->flags & IFF_PROMISC))
				return 0;

			rx->flags &= ~IEEE80211_RX_RA_MATCH;
		}
		break;
	case IEEE80211_IF_TYPE_VLAN:
	case IEEE80211_IF_TYPE_AP:
		if (!bssid) {
			if (compare_ether_addr(sdata->dev->dev_addr,
					       hdr->addr1))
				return 0;
		} else if (!ieee80211_bssid_match(bssid,
					sdata->dev->dev_addr)) {
			if (!(rx->flags & IEEE80211_RX_IN_SCAN))
				return 0;
			rx->flags &= ~IEEE80211_RX_RA_MATCH;
		}
		if (sdata->dev == sdata->local->mdev &&
		    !(rx->flags & IEEE80211_RX_IN_SCAN))
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
 * This is the actual Rx frames handler. as it blongs to Rx path it must
 * be called with rcu_read_lock protection.
 */
static void __ieee80211_rx_handle_packet(struct ieee80211_hw *hw,
					 struct sk_buff *skb,
					 struct ieee80211_rx_status *status,
					 u32 load,
					 struct ieee80211_rate *rate)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_hdr *hdr;
	struct ieee80211_rx_data rx;
	u16 type;
	int prepares;
	struct ieee80211_sub_if_data *prev = NULL;
	struct sk_buff *skb_new;
	u8 *bssid;

	hdr = (struct ieee80211_hdr *) skb->data;
	memset(&rx, 0, sizeof(rx));
	rx.skb = skb;
	rx.local = local;

	rx.status = status;
	rx.load = load;
	rx.rate = rate;
	rx.fc = le16_to_cpu(hdr->frame_control);
	type = rx.fc & IEEE80211_FCTL_FTYPE;

	if (type == IEEE80211_FTYPE_DATA || type == IEEE80211_FTYPE_MGMT)
		local->dot11ReceivedFragmentCount++;

	rx.sta = sta_info_get(local, hdr->addr2);
	if (rx.sta) {
		rx.sdata = rx.sta->sdata;
		rx.dev = rx.sta->sdata->dev;
	}

	if ((status->flag & RX_FLAG_MMIC_ERROR)) {
		ieee80211_rx_michael_mic_report(local->mdev, hdr, &rx);
		return;
	}

	if (unlikely(local->sta_sw_scanning || local->sta_hw_scanning))
		rx.flags |= IEEE80211_RX_IN_SCAN;

	ieee80211_parse_qos(&rx);
	ieee80211_verify_ip_alignment(&rx);

	skb = rx.skb;

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;

		if (sdata->vif.type == IEEE80211_IF_TYPE_MNTR)
			continue;

		bssid = ieee80211_get_bssid(hdr, skb->len, sdata->vif.type);
		rx.flags |= IEEE80211_RX_RA_MATCH;
		prepares = prepare_for_handlers(sdata, bssid, &rx, hdr);

		if (!prepares)
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
				       "multicast frame for %s\n",
				       wiphy_name(local->hw.wiphy),
				       prev->dev->name);
			continue;
		}
		rx.fc = le16_to_cpu(hdr->frame_control);
		ieee80211_invoke_rx_handlers(prev, &rx, skb_new);
		prev = sdata;
	}
	if (prev) {
		rx.fc = le16_to_cpu(hdr->frame_control);
		ieee80211_invoke_rx_handlers(prev, &rx, skb);
	} else
		dev_kfree_skb(skb);
}

#define SEQ_MODULO 0x1000
#define SEQ_MASK   0xfff

static inline int seq_less(u16 sq1, u16 sq2)
{
	return (((sq1 - sq2) & SEQ_MASK) > (SEQ_MODULO >> 1));
}

static inline u16 seq_inc(u16 sq)
{
	return ((sq + 1) & SEQ_MASK);
}

static inline u16 seq_sub(u16 sq1, u16 sq2)
{
	return ((sq1 - sq2) & SEQ_MASK);
}


/*
 * As it function blongs to Rx path it must be called with
 * the proper rcu_read_lock protection for its flow.
 */
u8 ieee80211_sta_manage_reorder_buf(struct ieee80211_hw *hw,
				struct tid_ampdu_rx *tid_agg_rx,
				struct sk_buff *skb, u16 mpdu_seq_num,
				int bar_req)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rx_status status;
	u16 head_seq_num, buf_size;
	int index;
	u32 pkt_load;
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *rate;

	buf_size = tid_agg_rx->buf_size;
	head_seq_num = tid_agg_rx->head_seq_num;

	/* frame with out of date sequence number */
	if (seq_less(mpdu_seq_num, head_seq_num)) {
		dev_kfree_skb(skb);
		return 1;
	}

	/* if frame sequence number exceeds our buffering window size or
	 * block Ack Request arrived - release stored frames */
	if ((!seq_less(mpdu_seq_num, head_seq_num + buf_size)) || (bar_req)) {
		/* new head to the ordering buffer */
		if (bar_req)
			head_seq_num = mpdu_seq_num;
		else
			head_seq_num =
				seq_inc(seq_sub(mpdu_seq_num, buf_size));
		/* release stored frames up to new head to stack */
		while (seq_less(tid_agg_rx->head_seq_num, head_seq_num)) {
			index = seq_sub(tid_agg_rx->head_seq_num,
				tid_agg_rx->ssn)
				% tid_agg_rx->buf_size;

			if (tid_agg_rx->reorder_buf[index]) {
				/* release the reordered frames to stack */
				memcpy(&status,
					tid_agg_rx->reorder_buf[index]->cb,
					sizeof(status));
				sband = local->hw.wiphy->bands[status.band];
				rate = &sband->bitrates[status.rate_idx];
				pkt_load = ieee80211_rx_load_stats(local,
						tid_agg_rx->reorder_buf[index],
						&status, rate);
				__ieee80211_rx_handle_packet(hw,
					tid_agg_rx->reorder_buf[index],
					&status, pkt_load, rate);
				tid_agg_rx->stored_mpdu_num--;
				tid_agg_rx->reorder_buf[index] = NULL;
			}
			tid_agg_rx->head_seq_num =
				seq_inc(tid_agg_rx->head_seq_num);
		}
		if (bar_req)
			return 1;
	}

	/* now the new frame is always in the range of the reordering */
	/* buffer window */
	index = seq_sub(mpdu_seq_num, tid_agg_rx->ssn)
				% tid_agg_rx->buf_size;
	/* check if we already stored this frame */
	if (tid_agg_rx->reorder_buf[index]) {
		dev_kfree_skb(skb);
		return 1;
	}

	/* if arrived mpdu is in the right order and nothing else stored */
	/* release it immediately */
	if (mpdu_seq_num == tid_agg_rx->head_seq_num &&
			tid_agg_rx->stored_mpdu_num == 0) {
		tid_agg_rx->head_seq_num =
			seq_inc(tid_agg_rx->head_seq_num);
		return 0;
	}

	/* put the frame in the reordering buffer */
	tid_agg_rx->reorder_buf[index] = skb;
	tid_agg_rx->stored_mpdu_num++;
	/* release the buffer until next missing frame */
	index = seq_sub(tid_agg_rx->head_seq_num, tid_agg_rx->ssn)
						% tid_agg_rx->buf_size;
	while (tid_agg_rx->reorder_buf[index]) {
		/* release the reordered frame back to stack */
		memcpy(&status, tid_agg_rx->reorder_buf[index]->cb,
			sizeof(status));
		sband = local->hw.wiphy->bands[status.band];
		rate = &sband->bitrates[status.rate_idx];
		pkt_load = ieee80211_rx_load_stats(local,
					tid_agg_rx->reorder_buf[index],
					&status, rate);
		__ieee80211_rx_handle_packet(hw, tid_agg_rx->reorder_buf[index],
					     &status, pkt_load, rate);
		tid_agg_rx->stored_mpdu_num--;
		tid_agg_rx->reorder_buf[index] = NULL;
		tid_agg_rx->head_seq_num = seq_inc(tid_agg_rx->head_seq_num);
		index =	seq_sub(tid_agg_rx->head_seq_num,
			tid_agg_rx->ssn) % tid_agg_rx->buf_size;
	}
	return 1;
}

static u8 ieee80211_rx_reorder_ampdu(struct ieee80211_local *local,
				     struct sk_buff *skb)
{
	struct ieee80211_hw *hw = &local->hw;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct sta_info *sta;
	struct tid_ampdu_rx *tid_agg_rx;
	u16 fc, sc;
	u16 mpdu_seq_num;
	u8 ret = 0, *qc;
	int tid;

	sta = sta_info_get(local, hdr->addr2);
	if (!sta)
		return ret;

	fc = le16_to_cpu(hdr->frame_control);

	/* filter the QoS data rx stream according to
	 * STA/TID and check if this STA/TID is on aggregation */
	if (!WLAN_FC_IS_QOS_DATA(fc))
		goto end_reorder;

	qc = skb->data + ieee80211_get_hdrlen(fc) - QOS_CONTROL_LEN;
	tid = qc[0] & QOS_CONTROL_TID_MASK;

	if (sta->ampdu_mlme.tid_state_rx[tid] != HT_AGG_STATE_OPERATIONAL)
		goto end_reorder;

	tid_agg_rx = sta->ampdu_mlme.tid_rx[tid];

	/* null data frames are excluded */
	if (unlikely(fc & IEEE80211_STYPE_NULLFUNC))
		goto end_reorder;

	/* new un-ordered ampdu frame - process it */

	/* reset session timer */
	if (tid_agg_rx->timeout) {
		unsigned long expires =
			jiffies + (tid_agg_rx->timeout / 1000) * HZ;
		mod_timer(&tid_agg_rx->session_timer, expires);
	}

	/* if this mpdu is fragmented - terminate rx aggregation session */
	sc = le16_to_cpu(hdr->seq_ctrl);
	if (sc & IEEE80211_SCTL_FRAG) {
		ieee80211_sta_stop_rx_ba_session(sta->sdata->dev, sta->addr,
			tid, 0, WLAN_REASON_QSTA_REQUIRE_SETUP);
		ret = 1;
		goto end_reorder;
	}

	/* according to mpdu sequence number deal with reordering buffer */
	mpdu_seq_num = (sc & IEEE80211_SCTL_SEQ) >> 4;
	ret = ieee80211_sta_manage_reorder_buf(hw, tid_agg_rx, skb,
						mpdu_seq_num, 0);
 end_reorder:
	return ret;
}

/*
 * This is the receive path handler. It is called by a low level driver when an
 * 802.11 MPDU is received from the hardware.
 */
void __ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb,
		    struct ieee80211_rx_status *status)
{
	struct ieee80211_local *local = hw_to_local(hw);
	u32 pkt_load;
	struct ieee80211_rate *rate = NULL;
	struct ieee80211_supported_band *sband;

	if (status->band < 0 ||
	    status->band >= IEEE80211_NUM_BANDS) {
		WARN_ON(1);
		return;
	}

	sband = local->hw.wiphy->bands[status->band];

	if (!sband ||
	    status->rate_idx < 0 ||
	    status->rate_idx >= sband->n_bitrates) {
		WARN_ON(1);
		return;
	}

	rate = &sband->bitrates[status->rate_idx];

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
	skb = ieee80211_rx_monitor(local, skb, status, rate);
	if (!skb) {
		rcu_read_unlock();
		return;
	}

	pkt_load = ieee80211_rx_load_stats(local, skb, status, rate);
	local->channel_use_raw += pkt_load;

	if (!ieee80211_rx_reorder_ampdu(local, skb))
		__ieee80211_rx_handle_packet(hw, skb, status, pkt_load, rate);

	rcu_read_unlock();
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
