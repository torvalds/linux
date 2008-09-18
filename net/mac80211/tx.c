/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Transmit and frame generation functions.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/bitmap.h>
#include <linux/rcupdate.h>
#include <net/net_namespace.h>
#include <net/ieee80211_radiotap.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>

#include "ieee80211_i.h"
#include "led.h"
#include "mesh.h"
#include "wep.h"
#include "wpa.h"
#include "wme.h"
#include "rate.h"

#define IEEE80211_TX_OK		0
#define IEEE80211_TX_AGAIN	1
#define IEEE80211_TX_FRAG_AGAIN	2

/* misc utils */

static __le16 ieee80211_duration(struct ieee80211_tx_data *tx, int group_addr,
				 int next_frag_len)
{
	int rate, mrate, erp, dur, i;
	struct ieee80211_rate *txrate;
	struct ieee80211_local *local = tx->local;
	struct ieee80211_supported_band *sband;
	struct ieee80211_hdr *hdr;

	sband = local->hw.wiphy->bands[tx->channel->band];
	txrate = &sband->bitrates[tx->rate_idx];

	erp = 0;
	if (tx->sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
		erp = txrate->flags & IEEE80211_RATE_ERP_G;

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
	hdr = (struct ieee80211_hdr *)tx->skb->data;
	if (ieee80211_is_ctl(hdr->frame_control)) {
		/* TODO: These control frames are not currently sent by
		 * mac80211, but should they be implemented, this function
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
		return cpu_to_le16(32768);

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
	/* use lowest available if everything fails */
	mrate = sband->bitrates[0].bitrate;
	for (i = 0; i < sband->n_bitrates; i++) {
		struct ieee80211_rate *r = &sband->bitrates[i];

		if (r->bitrate > txrate->bitrate)
			break;

		if (tx->sdata->bss_conf.basic_rates & BIT(i))
			rate = r->bitrate;

		switch (sband->band) {
		case IEEE80211_BAND_2GHZ: {
			u32 flag;
			if (tx->sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
				flag = IEEE80211_RATE_MANDATORY_G;
			else
				flag = IEEE80211_RATE_MANDATORY_B;
			if (r->flags & flag)
				mrate = r->bitrate;
			break;
		}
		case IEEE80211_BAND_5GHZ:
			if (r->flags & IEEE80211_RATE_MANDATORY_A)
				mrate = r->bitrate;
			break;
		case IEEE80211_NUM_BANDS:
			WARN_ON(1);
			break;
		}
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
				tx->sdata->bss_conf.use_short_preamble);

	if (next_frag_len) {
		/* Frame is fragmented: duration increases with time needed to
		 * transmit next fragment plus ACK and 2 x SIFS. */
		dur *= 2; /* ACK + SIFS */
		/* next fragment */
		dur += ieee80211_frame_duration(local, next_frag_len,
				txrate->bitrate, erp,
				tx->sdata->bss_conf.use_short_preamble);
	}

	return cpu_to_le16(dur);
}

static int inline is_ieee80211_device(struct ieee80211_local *local,
				      struct net_device *dev)
{
	return local == wdev_priv(dev->ieee80211_ptr);
}

/* tx handlers */

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_check_assoc(struct ieee80211_tx_data *tx)
{

	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	u32 sta_flags;

	if (unlikely(info->flags & IEEE80211_TX_CTL_INJECTED))
		return TX_CONTINUE;

	if (unlikely(tx->local->sw_scanning) &&
	    !ieee80211_is_probe_req(hdr->frame_control))
		return TX_DROP;

	if (tx->sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
		return TX_CONTINUE;

	if (tx->flags & IEEE80211_TX_PS_BUFFERED)
		return TX_CONTINUE;

	sta_flags = tx->sta ? get_sta_flags(tx->sta) : 0;

	if (likely(tx->flags & IEEE80211_TX_UNICAST)) {
		if (unlikely(!(sta_flags & WLAN_STA_ASSOC) &&
			     tx->sdata->vif.type != NL80211_IFTYPE_ADHOC &&
			     ieee80211_is_data(hdr->frame_control))) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
			DECLARE_MAC_BUF(mac);
			printk(KERN_DEBUG "%s: dropped data frame to not "
			       "associated station %s\n",
			       tx->dev->name, print_mac(mac, hdr->addr1));
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
			I802_DEBUG_INC(tx->local->tx_handlers_drop_not_assoc);
			return TX_DROP;
		}
	} else {
		if (unlikely(ieee80211_is_data(hdr->frame_control) &&
			     tx->local->num_sta == 0 &&
			     tx->sdata->vif.type != NL80211_IFTYPE_ADHOC)) {
			/*
			 * No associated STAs - no need to send multicast
			 * frames.
			 */
			return TX_DROP;
		}
		return TX_CONTINUE;
	}

	return TX_CONTINUE;
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

	/*
	 * virtual interfaces are protected by RCU
	 */
	rcu_read_lock();

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		struct ieee80211_if_ap *ap;
		if (sdata->vif.type != NL80211_IFTYPE_AP)
			continue;
		ap = &sdata->u.ap;
		skb = skb_dequeue(&ap->ps_bc_buf);
		if (skb) {
			purged++;
			dev_kfree_skb(skb);
		}
		total += skb_queue_len(&ap->ps_bc_buf);
	}

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		skb = skb_dequeue(&sta->ps_tx_buf);
		if (skb) {
			purged++;
			dev_kfree_skb(skb);
		}
		total += skb_queue_len(&sta->ps_tx_buf);
	}

	rcu_read_unlock();

	local->total_ps_buffered = total;
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	printk(KERN_DEBUG "%s: PS buffers full - purged %d frames\n",
	       wiphy_name(local->hw.wiphy), purged);
#endif
}

static ieee80211_tx_result
ieee80211_tx_h_multicast_ps_buf(struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;

	/*
	 * broadcast/multicast frame
	 *
	 * If any of the associated stations is in power save mode,
	 * the frame is buffered to be sent after DTIM beacon frame.
	 * This is done either by the hardware or us.
	 */

	/* powersaving STAs only in AP/VLAN mode */
	if (!tx->sdata->bss)
		return TX_CONTINUE;

	/* no buffering for ordered frames */
	if (ieee80211_has_order(hdr->frame_control))
		return TX_CONTINUE;

	/* no stations in PS mode */
	if (!atomic_read(&tx->sdata->bss->num_sta_ps))
		return TX_CONTINUE;

	/* buffered in mac80211 */
	if (tx->local->hw.flags & IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING) {
		if (tx->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
			purge_old_ps_buffers(tx->local);
		if (skb_queue_len(&tx->sdata->bss->ps_bc_buf) >=
		    AP_MAX_BC_BUFFER) {
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: BC TX buffer full - "
				       "dropping the oldest frame\n",
				       tx->dev->name);
			}
#endif
			dev_kfree_skb(skb_dequeue(&tx->sdata->bss->ps_bc_buf));
		} else
			tx->local->total_ps_buffered++;
		skb_queue_tail(&tx->sdata->bss->ps_bc_buf, tx->skb);
		return TX_QUEUED;
	}

	/* buffered in hardware */
	info->flags |= IEEE80211_TX_CTL_SEND_AFTER_DTIM;

	return TX_CONTINUE;
}

static ieee80211_tx_result
ieee80211_tx_h_unicast_ps_buf(struct ieee80211_tx_data *tx)
{
	struct sta_info *sta = tx->sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	u32 staflags;
	DECLARE_MAC_BUF(mac);

	if (unlikely(!sta || ieee80211_is_probe_resp(hdr->frame_control)))
		return TX_CONTINUE;

	staflags = get_sta_flags(sta);

	if (unlikely((staflags & WLAN_STA_PS) &&
		     !(staflags & WLAN_STA_PSPOLL))) {
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		printk(KERN_DEBUG "STA %s aid %d: PS buffer (entries "
		       "before %d)\n",
		       print_mac(mac, sta->sta.addr), sta->sta.aid,
		       skb_queue_len(&sta->ps_tx_buf));
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
		if (tx->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
			purge_old_ps_buffers(tx->local);
		if (skb_queue_len(&sta->ps_tx_buf) >= STA_MAX_TX_BUFFER) {
			struct sk_buff *old = skb_dequeue(&sta->ps_tx_buf);
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: STA %s TX "
				       "buffer full - dropping oldest frame\n",
				       tx->dev->name, print_mac(mac, sta->sta.addr));
			}
#endif
			dev_kfree_skb(old);
		} else
			tx->local->total_ps_buffered++;

		/* Queue frame to be sent after STA sends an PS Poll frame */
		if (skb_queue_empty(&sta->ps_tx_buf))
			sta_info_set_tim_bit(sta);

		info->control.jiffies = jiffies;
		skb_queue_tail(&sta->ps_tx_buf, tx->skb);
		return TX_QUEUED;
	}
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	else if (unlikely(test_sta_flags(sta, WLAN_STA_PS))) {
		printk(KERN_DEBUG "%s: STA %s in PS mode, but pspoll "
		       "set -> send frame\n", tx->dev->name,
		       print_mac(mac, sta->sta.addr));
	}
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
	clear_sta_flags(sta, WLAN_STA_PSPOLL);

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_ps_buf(struct ieee80211_tx_data *tx)
{
	if (unlikely(tx->flags & IEEE80211_TX_PS_BUFFERED))
		return TX_CONTINUE;

	if (tx->flags & IEEE80211_TX_UNICAST)
		return ieee80211_tx_h_unicast_ps_buf(tx);
	else
		return ieee80211_tx_h_multicast_ps_buf(tx);
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_select_key(struct ieee80211_tx_data *tx)
{
	struct ieee80211_key *key;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;

	if (unlikely(tx->skb->do_not_encrypt))
		tx->key = NULL;
	else if (tx->sta && (key = rcu_dereference(tx->sta->key)))
		tx->key = key;
	else if ((key = rcu_dereference(tx->sdata->default_key)))
		tx->key = key;
	else if (tx->sdata->drop_unencrypted &&
		 (tx->skb->protocol != cpu_to_be16(ETH_P_PAE)) &&
		 !(info->flags & IEEE80211_TX_CTL_INJECTED)) {
		I802_DEBUG_INC(tx->local->tx_handlers_drop_unencrypted);
		return TX_DROP;
	} else
		tx->key = NULL;

	if (tx->key) {
		tx->key->tx_rx_count++;
		/* TODO: add threshold stuff again */

		switch (tx->key->conf.alg) {
		case ALG_WEP:
			if (ieee80211_is_auth(hdr->frame_control))
				break;
		case ALG_TKIP:
		case ALG_CCMP:
			if (!ieee80211_is_data_present(hdr->frame_control))
				tx->key = NULL;
			break;
		}
	}

	if (!tx->key || !(tx->key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
		tx->skb->do_not_encrypt = 1;

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_rate_ctrl(struct ieee80211_tx_data *tx)
{
	struct rate_selection rsel;
	struct ieee80211_supported_band *sband;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);

	sband = tx->local->hw.wiphy->bands[tx->channel->band];

	if (likely(tx->rate_idx < 0)) {
		rate_control_get_rate(tx->sdata, sband, tx->sta,
				      tx->skb, &rsel);
		if (tx->sta)
			tx->sta->last_txrate_idx = rsel.rate_idx;
		tx->rate_idx = rsel.rate_idx;
		if (unlikely(rsel.probe_idx >= 0)) {
			info->flags |= IEEE80211_TX_CTL_RATE_CTRL_PROBE;
			tx->flags |= IEEE80211_TX_PROBE_LAST_FRAG;
			info->control.alt_retry_rate_idx = tx->rate_idx;
			tx->rate_idx = rsel.probe_idx;
		} else
			info->control.alt_retry_rate_idx = -1;

		if (unlikely(tx->rate_idx < 0))
			return TX_DROP;
	} else
		info->control.alt_retry_rate_idx = -1;

	if (tx->sdata->bss_conf.use_cts_prot &&
	    (tx->flags & IEEE80211_TX_FRAGMENTED) && (rsel.nonerp_idx >= 0)) {
		tx->last_frag_rate_idx = tx->rate_idx;
		if (rsel.probe_idx >= 0)
			tx->flags &= ~IEEE80211_TX_PROBE_LAST_FRAG;
		else
			tx->flags |= IEEE80211_TX_PROBE_LAST_FRAG;
		tx->rate_idx = rsel.nonerp_idx;
		info->tx_rate_idx = rsel.nonerp_idx;
		info->flags &= ~IEEE80211_TX_CTL_RATE_CTRL_PROBE;
	} else {
		tx->last_frag_rate_idx = tx->rate_idx;
		info->tx_rate_idx = tx->rate_idx;
	}
	info->tx_rate_idx = tx->rate_idx;

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_misc(struct ieee80211_tx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_supported_band *sband;

	sband = tx->local->hw.wiphy->bands[tx->channel->band];

	if (tx->sta)
		info->control.sta = &tx->sta->sta;

	if (!info->control.retry_limit) {
		if (!is_multicast_ether_addr(hdr->addr1)) {
			int len = min_t(int, tx->skb->len + FCS_LEN,
					tx->local->fragmentation_threshold);
			if (len > tx->local->rts_threshold
			    && tx->local->rts_threshold <
						IEEE80211_MAX_RTS_THRESHOLD) {
				info->flags |= IEEE80211_TX_CTL_USE_RTS_CTS;
				info->flags |=
					IEEE80211_TX_CTL_LONG_RETRY_LIMIT;
				info->control.retry_limit =
					tx->local->long_retry_limit;
			} else {
				info->control.retry_limit =
					tx->local->short_retry_limit;
			}
		} else {
			info->control.retry_limit = 1;
		}
	}

	if (tx->flags & IEEE80211_TX_FRAGMENTED) {
		/* Do not use multiple retry rates when sending fragmented
		 * frames.
		 * TODO: The last fragment could still use multiple retry
		 * rates. */
		info->control.alt_retry_rate_idx = -1;
	}

	/* Use CTS protection for unicast frames sent using extended rates if
	 * there are associated non-ERP stations and RTS/CTS is not configured
	 * for the frame. */
	if ((tx->sdata->flags & IEEE80211_SDATA_OPERATING_GMODE) &&
	    (sband->bitrates[tx->rate_idx].flags & IEEE80211_RATE_ERP_G) &&
	    (tx->flags & IEEE80211_TX_UNICAST) &&
	    tx->sdata->bss_conf.use_cts_prot &&
	    !(info->flags & IEEE80211_TX_CTL_USE_RTS_CTS))
		info->flags |= IEEE80211_TX_CTL_USE_CTS_PROTECT;

	/* Transmit data frames using short preambles if the driver supports
	 * short preambles at the selected rate and short preambles are
	 * available on the network at the current point in time. */
	if (ieee80211_is_data(hdr->frame_control) &&
	    (sband->bitrates[tx->rate_idx].flags & IEEE80211_RATE_SHORT_PREAMBLE) &&
	    tx->sdata->bss_conf.use_short_preamble &&
	    (!tx->sta || test_sta_flags(tx->sta, WLAN_STA_SHORT_PREAMBLE))) {
		info->flags |= IEEE80211_TX_CTL_SHORT_PREAMBLE;
	}

	if ((info->flags & IEEE80211_TX_CTL_USE_RTS_CTS) ||
	    (info->flags & IEEE80211_TX_CTL_USE_CTS_PROTECT)) {
		struct ieee80211_rate *rate;
		s8 baserate = -1;
		int idx;

		/* Do not use multiple retry rates when using RTS/CTS */
		info->control.alt_retry_rate_idx = -1;

		/* Use min(data rate, max base rate) as CTS/RTS rate */
		rate = &sband->bitrates[tx->rate_idx];

		for (idx = 0; idx < sband->n_bitrates; idx++) {
			if (sband->bitrates[idx].bitrate > rate->bitrate)
				continue;
			if (tx->sdata->bss_conf.basic_rates & BIT(idx) &&
			    (baserate < 0 ||
			     (sband->bitrates[baserate].bitrate
			      < sband->bitrates[idx].bitrate)))
				baserate = idx;
		}

		if (baserate >= 0)
			info->control.rts_cts_rate_idx = baserate;
		else
			info->control.rts_cts_rate_idx = 0;
	}

	if (tx->sta)
		info->control.sta = &tx->sta->sta;

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_sequence(struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	u16 *seq;
	u8 *qc;
	int tid;

	/*
	 * Packet injection may want to control the sequence
	 * number, if we have no matching interface then we
	 * neither assign one ourselves nor ask the driver to.
	 */
	if (unlikely(!info->control.vif))
		return TX_CONTINUE;

	if (unlikely(ieee80211_is_ctl(hdr->frame_control)))
		return TX_CONTINUE;

	if (ieee80211_hdrlen(hdr->frame_control) < 24)
		return TX_CONTINUE;

	if (!ieee80211_is_data_qos(hdr->frame_control)) {
		info->flags |= IEEE80211_TX_CTL_ASSIGN_SEQ;
		return TX_CONTINUE;
	}

	/*
	 * This should be true for injected/management frames only, for
	 * management frames we have set the IEEE80211_TX_CTL_ASSIGN_SEQ
	 * above since they are not QoS-data frames.
	 */
	if (!tx->sta)
		return TX_CONTINUE;

	/* include per-STA, per-TID sequence counter */

	qc = ieee80211_get_qos_ctl(hdr);
	tid = *qc & IEEE80211_QOS_CTL_TID_MASK;
	seq = &tx->sta->tid_seq[tid];

	hdr->seq_ctrl = cpu_to_le16(*seq);

	/* Increase the sequence number. */
	*seq = (*seq + 0x10) & IEEE80211_SCTL_SEQ;

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_fragment(struct ieee80211_tx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	size_t hdrlen, per_fragm, num_fragm, payload_len, left;
	struct sk_buff **frags, *first, *frag;
	int i;
	u16 seq;
	u8 *pos;
	int frag_threshold = tx->local->fragmentation_threshold;

	if (!(tx->flags & IEEE80211_TX_FRAGMENTED))
		return TX_CONTINUE;

	/*
	 * Warn when submitting a fragmented A-MPDU frame and drop it.
	 * This scenario is handled in __ieee80211_tx_prepare but extra
	 * caution taken here as fragmented ampdu may cause Tx stop.
	 */
	if (WARN_ON(tx->flags & IEEE80211_TX_CTL_AMPDU ||
		    skb_get_queue_mapping(tx->skb) >=
			ieee80211_num_regular_queues(&tx->local->hw)))
		return TX_DROP;

	first = tx->skb;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	payload_len = first->len - hdrlen;
	per_fragm = frag_threshold - hdrlen - FCS_LEN;
	num_fragm = DIV_ROUND_UP(payload_len, per_fragm);

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
			dev_alloc_skb(tx->local->tx_headroom +
				      frag_threshold +
				      IEEE80211_ENCRYPT_HEADROOM +
				      IEEE80211_ENCRYPT_TAILROOM);
		if (!frag)
			goto fail;
		/* Make sure that all fragments use the same priority so
		 * that they end up using the same TX queue */
		frag->priority = first->priority;
		skb_reserve(frag, tx->local->tx_headroom +
				  IEEE80211_ENCRYPT_HEADROOM);
		fhdr = (struct ieee80211_hdr *) skb_put(frag, hdrlen);
		memcpy(fhdr, first->data, hdrlen);
		if (i == num_fragm - 2)
			fhdr->frame_control &= cpu_to_le16(~IEEE80211_FCTL_MOREFRAGS);
		fhdr->seq_ctrl = cpu_to_le16(seq | ((i + 1) & IEEE80211_SCTL_FRAG));
		copylen = left > per_fragm ? per_fragm : left;
		memcpy(skb_put(frag, copylen), pos, copylen);
		memcpy(frag->cb, first->cb, sizeof(frag->cb));
		skb_copy_queue_mapping(frag, first);
		frag->do_not_encrypt = first->do_not_encrypt;

		pos += copylen;
		left -= copylen;
	}
	skb_trim(first, hdrlen + per_fragm);

	tx->num_extra_frag = num_fragm - 1;
	tx->extra_frag = frags;

	return TX_CONTINUE;

 fail:
	if (frags) {
		for (i = 0; i < num_fragm - 1; i++)
			if (frags[i])
				dev_kfree_skb(frags[i]);
		kfree(frags);
	}
	I802_DEBUG_INC(tx->local->tx_handlers_drop_fragment);
	return TX_DROP;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_encrypt(struct ieee80211_tx_data *tx)
{
	if (!tx->key)
		return TX_CONTINUE;

	switch (tx->key->conf.alg) {
	case ALG_WEP:
		return ieee80211_crypto_wep_encrypt(tx);
	case ALG_TKIP:
		return ieee80211_crypto_tkip_encrypt(tx);
	case ALG_CCMP:
		return ieee80211_crypto_ccmp_encrypt(tx);
	}

	/* not reached */
	WARN_ON(1);
	return TX_DROP;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_calculate_duration(struct ieee80211_tx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	int next_len, i;
	int group_addr = is_multicast_ether_addr(hdr->addr1);

	if (!(tx->flags & IEEE80211_TX_FRAGMENTED)) {
		hdr->duration_id = ieee80211_duration(tx, group_addr, 0);
		return TX_CONTINUE;
	}

	hdr->duration_id = ieee80211_duration(tx, group_addr,
					      tx->extra_frag[0]->len);

	for (i = 0; i < tx->num_extra_frag; i++) {
		if (i + 1 < tx->num_extra_frag) {
			next_len = tx->extra_frag[i + 1]->len;
		} else {
			next_len = 0;
			tx->rate_idx = tx->last_frag_rate_idx;
		}

		hdr = (struct ieee80211_hdr *)tx->extra_frag[i]->data;
		hdr->duration_id = ieee80211_duration(tx, 0, next_len);
	}

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_stats(struct ieee80211_tx_data *tx)
{
	int i;

	if (!tx->sta)
		return TX_CONTINUE;

	tx->sta->tx_packets++;
	tx->sta->tx_fragments++;
	tx->sta->tx_bytes += tx->skb->len;
	if (tx->extra_frag) {
		tx->sta->tx_fragments += tx->num_extra_frag;
		for (i = 0; i < tx->num_extra_frag; i++)
			tx->sta->tx_bytes += tx->extra_frag[i]->len;
	}

	return TX_CONTINUE;
}


/* actual transmit path */

/*
 * deal with packet injection down monitor interface
 * with Radiotap Header -- only called for monitor mode interface
 */
static ieee80211_tx_result
__ieee80211_parse_tx_radiotap(struct ieee80211_tx_data *tx,
			      struct sk_buff *skb)
{
	/*
	 * this is the moment to interpret and discard the radiotap header that
	 * must be at the start of the packet injected in Monitor mode
	 *
	 * Need to take some care with endian-ness since radiotap
	 * args are little-endian
	 */

	struct ieee80211_radiotap_iterator iterator;
	struct ieee80211_radiotap_header *rthdr =
		(struct ieee80211_radiotap_header *) skb->data;
	struct ieee80211_supported_band *sband;
	int ret = ieee80211_radiotap_iterator_init(&iterator, rthdr, skb->len);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	sband = tx->local->hw.wiphy->bands[tx->channel->band];

	skb->do_not_encrypt = 1;
	tx->flags &= ~IEEE80211_TX_FRAGMENTED;

	/*
	 * for every radiotap entry that is present
	 * (ieee80211_radiotap_iterator_next returns -ENOENT when no more
	 * entries present, or -EINVAL on error)
	 */

	while (!ret) {
		int i, target_rate;

		ret = ieee80211_radiotap_iterator_next(&iterator);

		if (ret)
			continue;

		/* see if this argument is something we can use */
		switch (iterator.this_arg_index) {
		/*
		 * You must take care when dereferencing iterator.this_arg
		 * for multibyte types... the pointer is not aligned.  Use
		 * get_unaligned((type *)iterator.this_arg) to dereference
		 * iterator.this_arg for type "type" safely on all arches.
		*/
		case IEEE80211_RADIOTAP_RATE:
			/*
			 * radiotap rate u8 is in 500kbps units eg, 0x02=1Mbps
			 * ieee80211 rate int is in 100kbps units eg, 0x0a=1Mbps
			 */
			target_rate = (*iterator.this_arg) * 5;
			for (i = 0; i < sband->n_bitrates; i++) {
				struct ieee80211_rate *r;

				r = &sband->bitrates[i];

				if (r->bitrate == target_rate) {
					tx->rate_idx = i;
					break;
				}
			}
			break;

		case IEEE80211_RADIOTAP_ANTENNA:
			/*
			 * radiotap uses 0 for 1st ant, mac80211 is 1 for
			 * 1st ant
			 */
			info->antenna_sel_tx = (*iterator.this_arg) + 1;
			break;

#if 0
		case IEEE80211_RADIOTAP_DBM_TX_POWER:
			control->power_level = *iterator.this_arg;
			break;
#endif

		case IEEE80211_RADIOTAP_FLAGS:
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_FCS) {
				/*
				 * this indicates that the skb we have been
				 * handed has the 32-bit FCS CRC at the end...
				 * we should react to that by snipping it off
				 * because it will be recomputed and added
				 * on transmission
				 */
				if (skb->len < (iterator.max_length + FCS_LEN))
					return TX_DROP;

				skb_trim(skb, skb->len - FCS_LEN);
			}
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_WEP)
				tx->skb->do_not_encrypt = 0;
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_FRAG)
				tx->flags |= IEEE80211_TX_FRAGMENTED;
			break;

		/*
		 * Please update the file
		 * Documentation/networking/mac80211-injection.txt
		 * when parsing new fields here.
		 */

		default:
			break;
		}
	}

	if (ret != -ENOENT) /* ie, if we didn't simply run out of fields */
		return TX_DROP;

	/*
	 * remove the radiotap header
	 * iterator->max_length was sanity-checked against
	 * skb->len by iterator init
	 */
	skb_pull(skb, iterator.max_length);

	return TX_CONTINUE;
}

/*
 * initialises @tx
 */
static ieee80211_tx_result
__ieee80211_tx_prepare(struct ieee80211_tx_data *tx,
		       struct sk_buff *skb,
		       struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hdr *hdr;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	int hdrlen;

	memset(tx, 0, sizeof(*tx));
	tx->skb = skb;
	tx->dev = dev; /* use original interface */
	tx->local = local;
	tx->sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	tx->channel = local->hw.conf.channel;
	tx->rate_idx = -1;
	tx->last_frag_rate_idx = -1;
	/*
	 * Set this flag (used below to indicate "automatic fragmentation"),
	 * it will be cleared/left by radiotap as desired.
	 */
	tx->flags |= IEEE80211_TX_FRAGMENTED;

	/* process and remove the injection radiotap header */
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (unlikely(info->flags & IEEE80211_TX_CTL_INJECTED)) {
		if (__ieee80211_parse_tx_radiotap(tx, skb) == TX_DROP)
			return TX_DROP;

		/*
		 * __ieee80211_parse_tx_radiotap has now removed
		 * the radiotap header that was present and pre-filled
		 * 'tx' with tx control information.
		 */
	}

	hdr = (struct ieee80211_hdr *) skb->data;

	tx->sta = sta_info_get(local, hdr->addr1);

	if (is_multicast_ether_addr(hdr->addr1)) {
		tx->flags &= ~IEEE80211_TX_UNICAST;
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	} else {
		tx->flags |= IEEE80211_TX_UNICAST;
		info->flags &= ~IEEE80211_TX_CTL_NO_ACK;
	}

	if (tx->flags & IEEE80211_TX_FRAGMENTED) {
		if ((tx->flags & IEEE80211_TX_UNICAST) &&
		    skb->len + FCS_LEN > local->fragmentation_threshold &&
		    !local->ops->set_frag_threshold &&
		    !(info->flags & IEEE80211_TX_CTL_AMPDU))
			tx->flags |= IEEE80211_TX_FRAGMENTED;
		else
			tx->flags &= ~IEEE80211_TX_FRAGMENTED;
	}

	if (!tx->sta)
		info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT;
	else if (test_and_clear_sta_flags(tx->sta, WLAN_STA_CLEAR_PS_FILT))
		info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	if (skb->len > hdrlen + sizeof(rfc1042_header) + 2) {
		u8 *pos = &skb->data[hdrlen + sizeof(rfc1042_header)];
		tx->ethertype = (pos[0] << 8) | pos[1];
	}
	info->flags |= IEEE80211_TX_CTL_FIRST_FRAGMENT;

	return TX_CONTINUE;
}

/*
 * NB: @tx is uninitialised when passed in here
 */
static int ieee80211_tx_prepare(struct ieee80211_local *local,
				struct ieee80211_tx_data *tx,
				struct sk_buff *skb)
{
	struct net_device *dev;

	dev = dev_get_by_index(&init_net, skb->iif);
	if (unlikely(dev && !is_ieee80211_device(local, dev))) {
		dev_put(dev);
		dev = NULL;
	}
	if (unlikely(!dev))
		return -ENODEV;
	/* initialises tx with control */
	__ieee80211_tx_prepare(tx, skb, dev);
	dev_put(dev);
	return 0;
}

static int __ieee80211_tx(struct ieee80211_local *local, struct sk_buff *skb,
			  struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info;
	int ret, i;

	if (skb) {
		if (netif_subqueue_stopped(local->mdev, skb))
			return IEEE80211_TX_AGAIN;
		info =  IEEE80211_SKB_CB(skb);

		ret = local->ops->tx(local_to_hw(local), skb);
		if (ret)
			return IEEE80211_TX_AGAIN;
		local->mdev->trans_start = jiffies;
		ieee80211_led_tx(local, 1);
	}
	if (tx->extra_frag) {
		for (i = 0; i < tx->num_extra_frag; i++) {
			if (!tx->extra_frag[i])
				continue;
			info = IEEE80211_SKB_CB(tx->extra_frag[i]);
			info->flags &= ~(IEEE80211_TX_CTL_USE_RTS_CTS |
					 IEEE80211_TX_CTL_USE_CTS_PROTECT |
					 IEEE80211_TX_CTL_CLEAR_PS_FILT |
					 IEEE80211_TX_CTL_FIRST_FRAGMENT);
			if (netif_subqueue_stopped(local->mdev,
						   tx->extra_frag[i]))
				return IEEE80211_TX_FRAG_AGAIN;
			if (i == tx->num_extra_frag) {
				info->tx_rate_idx = tx->last_frag_rate_idx;

				if (tx->flags & IEEE80211_TX_PROBE_LAST_FRAG)
					info->flags |=
						IEEE80211_TX_CTL_RATE_CTRL_PROBE;
				else
					info->flags &=
						~IEEE80211_TX_CTL_RATE_CTRL_PROBE;
			}

			ret = local->ops->tx(local_to_hw(local),
					    tx->extra_frag[i]);
			if (ret)
				return IEEE80211_TX_FRAG_AGAIN;
			local->mdev->trans_start = jiffies;
			ieee80211_led_tx(local, 1);
			tx->extra_frag[i] = NULL;
		}
		kfree(tx->extra_frag);
		tx->extra_frag = NULL;
	}
	return IEEE80211_TX_OK;
}

/*
 * Invoke TX handlers, return 0 on success and non-zero if the
 * frame was dropped or queued.
 */
static int invoke_tx_handlers(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb = tx->skb;
	ieee80211_tx_result res = TX_DROP;
	int i;

#define CALL_TXH(txh)		\
	res = txh(tx);		\
	if (res != TX_CONTINUE)	\
		goto txh_done;

	CALL_TXH(ieee80211_tx_h_check_assoc)
	CALL_TXH(ieee80211_tx_h_ps_buf)
	CALL_TXH(ieee80211_tx_h_select_key)
	CALL_TXH(ieee80211_tx_h_michael_mic_add)
	CALL_TXH(ieee80211_tx_h_rate_ctrl)
	CALL_TXH(ieee80211_tx_h_misc)
	CALL_TXH(ieee80211_tx_h_sequence)
	CALL_TXH(ieee80211_tx_h_fragment)
	/* handlers after fragment must be aware of tx info fragmentation! */
	CALL_TXH(ieee80211_tx_h_encrypt)
	CALL_TXH(ieee80211_tx_h_calculate_duration)
	CALL_TXH(ieee80211_tx_h_stats)
#undef CALL_TXH

 txh_done:
	if (unlikely(res == TX_DROP)) {
		I802_DEBUG_INC(tx->local->tx_handlers_drop);
		dev_kfree_skb(skb);
		for (i = 0; i < tx->num_extra_frag; i++)
			if (tx->extra_frag[i])
				dev_kfree_skb(tx->extra_frag[i]);
		kfree(tx->extra_frag);
		return -1;
	} else if (unlikely(res == TX_QUEUED)) {
		I802_DEBUG_INC(tx->local->tx_handlers_queued);
		return -1;
	}

	return 0;
}

static int ieee80211_tx(struct net_device *dev, struct sk_buff *skb)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	struct ieee80211_tx_data tx;
	ieee80211_tx_result res_prepare;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int ret, i;
	u16 queue;

	queue = skb_get_queue_mapping(skb);

	WARN_ON(test_bit(queue, local->queues_pending));

	if (unlikely(skb->len < 10)) {
		dev_kfree_skb(skb);
		return 0;
	}

	rcu_read_lock();

	/* initialises tx */
	res_prepare = __ieee80211_tx_prepare(&tx, skb, dev);

	if (res_prepare == TX_DROP) {
		dev_kfree_skb(skb);
		rcu_read_unlock();
		return 0;
	}

	sta = tx.sta;
	tx.channel = local->hw.conf.channel;
	info->band = tx.channel->band;

	if (invoke_tx_handlers(&tx))
		goto out;

retry:
	ret = __ieee80211_tx(local, skb, &tx);
	if (ret) {
		struct ieee80211_tx_stored_packet *store;

		/*
		 * Since there are no fragmented frames on A-MPDU
		 * queues, there's no reason for a driver to reject
		 * a frame there, warn and drop it.
		 */
		if (WARN_ON(queue >= ieee80211_num_regular_queues(&local->hw)))
			goto drop;

		store = &local->pending_packet[queue];

		if (ret == IEEE80211_TX_FRAG_AGAIN)
			skb = NULL;

		set_bit(queue, local->queues_pending);
		smp_mb();
		/*
		 * When the driver gets out of buffers during sending of
		 * fragments and calls ieee80211_stop_queue, the netif
		 * subqueue is stopped. There is, however, a small window
		 * in which the PENDING bit is not yet set. If a buffer
		 * gets available in that window (i.e. driver calls
		 * ieee80211_wake_queue), we would end up with ieee80211_tx
		 * called with the PENDING bit still set. Prevent this by
		 * continuing transmitting here when that situation is
		 * possible to have happened.
		 */
		if (!__netif_subqueue_stopped(local->mdev, queue)) {
			clear_bit(queue, local->queues_pending);
			goto retry;
		}
		store->skb = skb;
		store->extra_frag = tx.extra_frag;
		store->num_extra_frag = tx.num_extra_frag;
		store->last_frag_rate_idx = tx.last_frag_rate_idx;
		store->last_frag_rate_ctrl_probe =
			!!(tx.flags & IEEE80211_TX_PROBE_LAST_FRAG);
	}
 out:
	rcu_read_unlock();
	return 0;

 drop:
	if (skb)
		dev_kfree_skb(skb);
	for (i = 0; i < tx.num_extra_frag; i++)
		if (tx.extra_frag[i])
			dev_kfree_skb(tx.extra_frag[i]);
	kfree(tx.extra_frag);
	rcu_read_unlock();
	return 0;
}

/* device xmit handlers */

static int ieee80211_skb_resize(struct ieee80211_local *local,
				struct sk_buff *skb,
				int head_need, bool may_encrypt)
{
	int tail_need = 0;

	/*
	 * This could be optimised, devices that do full hardware
	 * crypto (including TKIP MMIC) need no tailroom... But we
	 * have no drivers for such devices currently.
	 */
	if (may_encrypt) {
		tail_need = IEEE80211_ENCRYPT_TAILROOM;
		tail_need -= skb_tailroom(skb);
		tail_need = max_t(int, tail_need, 0);
	}

	if (head_need || tail_need) {
		/* Sorry. Can't account for this any more */
		skb_orphan(skb);
	}

	if (skb_header_cloned(skb))
		I802_DEBUG_INC(local->tx_expand_skb_head_cloned);
	else
		I802_DEBUG_INC(local->tx_expand_skb_head);

	if (pskb_expand_head(skb, head_need, tail_need, GFP_ATOMIC)) {
		printk(KERN_DEBUG "%s: failed to reallocate TX buffer\n",
		       wiphy_name(local->hw.wiphy));
		return -ENOMEM;
	}

	/* update truesize too */
	skb->truesize += head_need + tail_need;

	return 0;
}

int ieee80211_master_start_xmit(struct sk_buff *skb,
				struct net_device *dev)
{
	struct ieee80211_master_priv *mpriv = netdev_priv(dev);
	struct ieee80211_local *local = mpriv->local;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct net_device *odev = NULL;
	struct ieee80211_sub_if_data *osdata;
	int headroom;
	bool may_encrypt;
	enum {
		NOT_MONITOR,
		FOUND_SDATA,
		UNKNOWN_ADDRESS,
	} monitor_iface = NOT_MONITOR;
	int ret;

	if (skb->iif)
		odev = dev_get_by_index(&init_net, skb->iif);
	if (unlikely(odev && !is_ieee80211_device(local, odev))) {
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

	memset(info, 0, sizeof(*info));

	info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;

	osdata = IEEE80211_DEV_TO_SUB_IF(odev);

	if (ieee80211_vif_is_mesh(&osdata->vif) &&
	    ieee80211_is_data(hdr->frame_control)) {
		if (ieee80211_is_data(hdr->frame_control)) {
			if (is_multicast_ether_addr(hdr->addr3))
				memcpy(hdr->addr1, hdr->addr3, ETH_ALEN);
			else
				if (mesh_nexthop_lookup(skb, osdata))
					return  0;
			if (memcmp(odev->dev_addr, hdr->addr4, ETH_ALEN) != 0)
				IEEE80211_IFSTA_MESH_CTR_INC(&osdata->u.mesh,
							     fwded_frames);
		}
	} else if (unlikely(osdata->vif.type == NL80211_IFTYPE_MONITOR)) {
		struct ieee80211_sub_if_data *sdata;
		struct ieee80211_local *local = osdata->local;
		struct ieee80211_hdr *hdr;
		int hdrlen;
		u16 len_rthdr;

		info->flags |= IEEE80211_TX_CTL_INJECTED;
		monitor_iface = UNKNOWN_ADDRESS;

		len_rthdr = ieee80211_get_radiotap_len(skb->data);
		hdr = (struct ieee80211_hdr *)skb->data + len_rthdr;
		hdrlen = ieee80211_hdrlen(hdr->frame_control);

		/* check the header is complete in the frame */
		if (likely(skb->len >= len_rthdr + hdrlen)) {
			/*
			 * We process outgoing injected frames that have a
			 * local address we handle as though they are our
			 * own frames.
			 * This code here isn't entirely correct, the local
			 * MAC address is not necessarily enough to find
			 * the interface to use; for that proper VLAN/WDS
			 * support we will need a different mechanism.
			 */

			rcu_read_lock();
			list_for_each_entry_rcu(sdata, &local->interfaces,
						list) {
				if (!netif_running(sdata->dev))
					continue;
				if (compare_ether_addr(sdata->dev->dev_addr,
						       hdr->addr2)) {
					dev_hold(sdata->dev);
					dev_put(odev);
					osdata = sdata;
					odev = osdata->dev;
					skb->iif = sdata->dev->ifindex;
					monitor_iface = FOUND_SDATA;
					break;
				}
			}
			rcu_read_unlock();
		}
	}

	may_encrypt = !skb->do_not_encrypt;

	headroom = osdata->local->tx_headroom;
	if (may_encrypt)
		headroom += IEEE80211_ENCRYPT_HEADROOM;
	headroom -= skb_headroom(skb);
	headroom = max_t(int, 0, headroom);

	if (ieee80211_skb_resize(osdata->local, skb, headroom, may_encrypt)) {
		dev_kfree_skb(skb);
		dev_put(odev);
		return 0;
	}

	if (osdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		osdata = container_of(osdata->bss,
				      struct ieee80211_sub_if_data,
				      u.ap);
	if (likely(monitor_iface != UNKNOWN_ADDRESS))
		info->control.vif = &osdata->vif;
	ret = ieee80211_tx(odev, skb);
	dev_put(odev);

	return ret;
}

int ieee80211_monitor_start_xmit(struct sk_buff *skb,
				 struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_radiotap_header *prthdr =
		(struct ieee80211_radiotap_header *)skb->data;
	u16 len_rthdr;

	/* check for not even having the fixed radiotap header part */
	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header)))
		goto fail; /* too short to be possibly valid */

	/* is it a header version we can trust to find length from? */
	if (unlikely(prthdr->it_version))
		goto fail; /* only version 0 is supported */

	/* then there must be a radiotap header with a length we can use */
	len_rthdr = ieee80211_get_radiotap_len(skb->data);

	/* does the skb contain enough to deliver on the alleged length? */
	if (unlikely(skb->len < len_rthdr))
		goto fail; /* skb too short for claimed rt header extent */

	skb->dev = local->mdev;

	/* needed because we set skb device to master */
	skb->iif = dev->ifindex;

	/* sometimes we do encrypt injected frames, will be fixed
	 * up in radiotap parser if not wanted */
	skb->do_not_encrypt = 0;

	/*
	 * fix up the pointers accounting for the radiotap
	 * header still being in there.  We are being given
	 * a precooked IEEE80211 header so no need for
	 * normal processing
	 */
	skb_set_mac_header(skb, len_rthdr);
	/*
	 * these are just fixed to the end of the rt area since we
	 * don't have any better information and at this point, nobody cares
	 */
	skb_set_network_header(skb, len_rthdr);
	skb_set_transport_header(skb, len_rthdr);

	/* pass the radiotap header up to the next stage intact */
	dev_queue_xmit(skb);
	return NETDEV_TX_OK;

fail:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK; /* meaning, we dealt with the skb */
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
int ieee80211_subif_start_xmit(struct sk_buff *skb,
			       struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	int ret = 1, head_need;
	u16 ethertype, hdrlen,  meshhdrlen = 0;
	__le16 fc;
	struct ieee80211_hdr hdr;
	struct ieee80211s_hdr mesh_hdr;
	const u8 *encaps_data;
	int encaps_len, skip_header_bytes;
	int nh_pos, h_pos;
	struct sta_info *sta;
	u32 sta_flags = 0;

	if (unlikely(skb->len < ETH_HLEN)) {
		ret = 0;
		goto fail;
	}

	nh_pos = skb_network_header(skb) - skb->data;
	h_pos = skb_transport_header(skb) - skb->data;

	/* convert Ethernet header to proper 802.11 header (based on
	 * operation mode) */
	ethertype = (skb->data[12] << 8) | skb->data[13];
	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA);

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
		fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS);
		/* DA BSSID SA */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, dev->dev_addr, ETH_ALEN);
		memcpy(hdr.addr3, skb->data + ETH_ALEN, ETH_ALEN);
		hdrlen = 24;
		break;
	case NL80211_IFTYPE_WDS:
		fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS);
		/* RA TA DA SA */
		memcpy(hdr.addr1, sdata->u.wds.remote_addr, ETH_ALEN);
		memcpy(hdr.addr2, dev->dev_addr, ETH_ALEN);
		memcpy(hdr.addr3, skb->data, ETH_ALEN);
		memcpy(hdr.addr4, skb->data + ETH_ALEN, ETH_ALEN);
		hdrlen = 30;
		break;
#ifdef CONFIG_MAC80211_MESH
	case NL80211_IFTYPE_MESH_POINT:
		fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS);
		if (!sdata->u.mesh.mshcfg.dot11MeshTTL) {
			/* Do not send frames with mesh_ttl == 0 */
			sdata->u.mesh.mshstats.dropped_frames_ttl++;
			ret = 0;
			goto fail;
		}
		memset(&mesh_hdr, 0, sizeof(mesh_hdr));

		if (compare_ether_addr(dev->dev_addr,
					  skb->data + ETH_ALEN) == 0) {
			/* RA TA DA SA */
			memset(hdr.addr1, 0, ETH_ALEN);
			memcpy(hdr.addr2, dev->dev_addr, ETH_ALEN);
			memcpy(hdr.addr3, skb->data, ETH_ALEN);
			memcpy(hdr.addr4, skb->data + ETH_ALEN, ETH_ALEN);
			meshhdrlen = ieee80211_new_mesh_header(&mesh_hdr, sdata);
		} else {
			/* packet from other interface */
			struct mesh_path *mppath;

			memset(hdr.addr1, 0, ETH_ALEN);
			memcpy(hdr.addr2, dev->dev_addr, ETH_ALEN);
			memcpy(hdr.addr4, dev->dev_addr, ETH_ALEN);

			if (is_multicast_ether_addr(skb->data))
				memcpy(hdr.addr3, skb->data, ETH_ALEN);
			else {
				rcu_read_lock();
				mppath = mpp_path_lookup(skb->data, sdata);
				if (mppath)
					memcpy(hdr.addr3, mppath->mpp, ETH_ALEN);
				else
					memset(hdr.addr3, 0xff, ETH_ALEN);
				rcu_read_unlock();
			}

			mesh_hdr.flags |= MESH_FLAGS_AE_A5_A6;
			mesh_hdr.ttl = sdata->u.mesh.mshcfg.dot11MeshTTL;
			put_unaligned(cpu_to_le32(sdata->u.mesh.mesh_seqnum), &mesh_hdr.seqnum);
			memcpy(mesh_hdr.eaddr1, skb->data, ETH_ALEN);
			memcpy(mesh_hdr.eaddr2, skb->data + ETH_ALEN, ETH_ALEN);
			sdata->u.mesh.mesh_seqnum++;
			meshhdrlen = 18;
		}
		hdrlen = 30;
		break;
#endif
	case NL80211_IFTYPE_STATION:
		fc |= cpu_to_le16(IEEE80211_FCTL_TODS);
		/* BSSID SA DA */
		memcpy(hdr.addr1, sdata->u.sta.bssid, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, skb->data, ETH_ALEN);
		hdrlen = 24;
		break;
	case NL80211_IFTYPE_ADHOC:
		/* DA SA BSSID */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, sdata->u.sta.bssid, ETH_ALEN);
		hdrlen = 24;
		break;
	default:
		ret = 0;
		goto fail;
	}

	/*
	 * There's no need to try to look up the destination
	 * if it is a multicast address (which can only happen
	 * in AP mode)
	 */
	if (!is_multicast_ether_addr(hdr.addr1)) {
		rcu_read_lock();
		sta = sta_info_get(local, hdr.addr1);
		if (sta)
			sta_flags = get_sta_flags(sta);
		rcu_read_unlock();
	}

	/* receiver and we are QoS enabled, use a QoS type frame */
	if (sta_flags & WLAN_STA_WME &&
	    ieee80211_num_regular_queues(&local->hw) >= 4) {
		fc |= cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
		hdrlen += 2;
	}

	/*
	 * Drop unicast frames to unauthorised stations unless they are
	 * EAPOL frames from the local station.
	 */
	if (!ieee80211_vif_is_mesh(&sdata->vif) &&
		unlikely(!is_multicast_ether_addr(hdr.addr1) &&
		      !(sta_flags & WLAN_STA_AUTHORIZED) &&
		      !(ethertype == ETH_P_PAE &&
		       compare_ether_addr(dev->dev_addr,
					  skb->data + ETH_ALEN) == 0))) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		DECLARE_MAC_BUF(mac);

		if (net_ratelimit())
			printk(KERN_DEBUG "%s: dropped frame to %s"
			       " (unauthorized port)\n", dev->name,
			       print_mac(mac, hdr.addr1));
#endif

		I802_DEBUG_INC(local->tx_handlers_drop_unauth_port);

		ret = 0;
		goto fail;
	}

	hdr.frame_control = fc;
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

	head_need = hdrlen + encaps_len + meshhdrlen - skb_headroom(skb);

	/*
	 * So we need to modify the skb header and hence need a copy of
	 * that. The head_need variable above doesn't, so far, include
	 * the needed header space that we don't need right away. If we
	 * can, then we don't reallocate right now but only after the
	 * frame arrives at the master device (if it does...)
	 *
	 * If we cannot, however, then we will reallocate to include all
	 * the ever needed space. Also, if we need to reallocate it anyway,
	 * make it big enough for everything we may ever need.
	 */

	if (head_need > 0 || skb_cloned(skb)) {
		head_need += IEEE80211_ENCRYPT_HEADROOM;
		head_need += local->tx_headroom;
		head_need = max_t(int, 0, head_need);
		if (ieee80211_skb_resize(local, skb, head_need, true))
			goto fail;
	}

	if (encaps_data) {
		memcpy(skb_push(skb, encaps_len), encaps_data, encaps_len);
		nh_pos += encaps_len;
		h_pos += encaps_len;
	}

	if (meshhdrlen > 0) {
		memcpy(skb_push(skb, meshhdrlen), &mesh_hdr, meshhdrlen);
		nh_pos += meshhdrlen;
		h_pos += meshhdrlen;
	}

	if (ieee80211_is_data_qos(fc)) {
		__le16 *qos_control;

		qos_control = (__le16*) skb_push(skb, 2);
		memcpy(skb_push(skb, hdrlen - 2), &hdr, hdrlen - 2);
		/*
		 * Maybe we could actually set some fields here, for now just
		 * initialise to zero to indicate no special operation.
		 */
		*qos_control = 0;
	} else
		memcpy(skb_push(skb, hdrlen), &hdr, hdrlen);

	nh_pos += hdrlen;
	h_pos += hdrlen;

	skb->iif = dev->ifindex;

	skb->dev = local->mdev;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

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
 * ieee80211_clear_tx_pending may not be called in a context where
 * it is possible that it packets could come in again.
 */
void ieee80211_clear_tx_pending(struct ieee80211_local *local)
{
	int i, j;
	struct ieee80211_tx_stored_packet *store;

	for (i = 0; i < ieee80211_num_regular_queues(&local->hw); i++) {
		if (!test_bit(i, local->queues_pending))
			continue;
		store = &local->pending_packet[i];
		kfree_skb(store->skb);
		for (j = 0; j < store->num_extra_frag; j++)
			kfree_skb(store->extra_frag[j]);
		kfree(store->extra_frag);
		clear_bit(i, local->queues_pending);
	}
}

/*
 * Transmit all pending packets. Called from tasklet, locks master device
 * TX lock so that no new packets can come in.
 */
void ieee80211_tx_pending(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *)data;
	struct net_device *dev = local->mdev;
	struct ieee80211_tx_stored_packet *store;
	struct ieee80211_tx_data tx;
	int i, ret;

	netif_tx_lock_bh(dev);
	for (i = 0; i < ieee80211_num_regular_queues(&local->hw); i++) {
		/* Check that this queue is ok */
		if (__netif_subqueue_stopped(local->mdev, i) &&
		    !test_bit(i, local->queues_pending_run))
			continue;

		if (!test_bit(i, local->queues_pending)) {
			clear_bit(i, local->queues_pending_run);
			ieee80211_wake_queue(&local->hw, i);
			continue;
		}

		clear_bit(i, local->queues_pending_run);
		netif_start_subqueue(local->mdev, i);

		store = &local->pending_packet[i];
		tx.extra_frag = store->extra_frag;
		tx.num_extra_frag = store->num_extra_frag;
		tx.last_frag_rate_idx = store->last_frag_rate_idx;
		tx.flags = 0;
		if (store->last_frag_rate_ctrl_probe)
			tx.flags |= IEEE80211_TX_PROBE_LAST_FRAG;
		ret = __ieee80211_tx(local, store->skb, &tx);
		if (ret) {
			if (ret == IEEE80211_TX_FRAG_AGAIN)
				store->skb = NULL;
		} else {
			clear_bit(i, local->queues_pending);
			ieee80211_wake_queue(&local->hw, i);
		}
	}
	netif_tx_unlock_bh(dev);
}

/* functions for drivers to get certain frames */

static void ieee80211_beacon_add_tim(struct ieee80211_local *local,
				     struct ieee80211_if_ap *bss,
				     struct sk_buff *skb,
				     struct beacon_data *beacon)
{
	u8 *pos, *tim;
	int aid0 = 0;
	int i, have_bits = 0, n1, n2;

	/* Generate bitmap for TIM only if there are any STAs in power save
	 * mode. */
	if (atomic_read(&bss->num_sta_ps) > 0)
		/* in the hope that this is faster than
		 * checking byte-for-byte */
		have_bits = !bitmap_empty((unsigned long*)bss->tim,
					  IEEE80211_MAX_AID+1);

	if (bss->dtim_count == 0)
		bss->dtim_count = beacon->dtim_period - 1;
	else
		bss->dtim_count--;

	tim = pos = (u8 *) skb_put(skb, 6);
	*pos++ = WLAN_EID_TIM;
	*pos++ = 4;
	*pos++ = bss->dtim_count;
	*pos++ = beacon->dtim_period;

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
}

struct sk_buff *ieee80211_beacon_get(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sk_buff *skb = NULL;
	struct ieee80211_tx_info *info;
	struct net_device *bdev;
	struct ieee80211_sub_if_data *sdata = NULL;
	struct ieee80211_if_ap *ap = NULL;
	struct ieee80211_if_sta *ifsta = NULL;
	struct rate_selection rsel;
	struct beacon_data *beacon;
	struct ieee80211_supported_band *sband;
	enum ieee80211_band band = local->hw.conf.channel->band;

	sband = local->hw.wiphy->bands[band];

	rcu_read_lock();

	sdata = vif_to_sdata(vif);
	bdev = sdata->dev;

	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		ap = &sdata->u.ap;
		beacon = rcu_dereference(ap->beacon);
		if (ap && beacon) {
			/*
			 * headroom, head length,
			 * tail length and maximum TIM length
			 */
			skb = dev_alloc_skb(local->tx_headroom +
					    beacon->head_len +
					    beacon->tail_len + 256);
			if (!skb)
				goto out;

			skb_reserve(skb, local->tx_headroom);
			memcpy(skb_put(skb, beacon->head_len), beacon->head,
			       beacon->head_len);

			/*
			 * Not very nice, but we want to allow the driver to call
			 * ieee80211_beacon_get() as a response to the set_tim()
			 * callback. That, however, is already invoked under the
			 * sta_lock to guarantee consistent and race-free update
			 * of the tim bitmap in mac80211 and the driver.
			 */
			if (local->tim_in_locked_section) {
				ieee80211_beacon_add_tim(local, ap, skb, beacon);
			} else {
				unsigned long flags;

				spin_lock_irqsave(&local->sta_lock, flags);
				ieee80211_beacon_add_tim(local, ap, skb, beacon);
				spin_unlock_irqrestore(&local->sta_lock, flags);
			}

			if (beacon->tail)
				memcpy(skb_put(skb, beacon->tail_len),
				       beacon->tail, beacon->tail_len);
		} else
			goto out;
	} else if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		struct ieee80211_hdr *hdr;
		ifsta = &sdata->u.sta;

		if (!ifsta->probe_resp)
			goto out;

		skb = skb_copy(ifsta->probe_resp, GFP_ATOMIC);
		if (!skb)
			goto out;

		hdr = (struct ieee80211_hdr *) skb->data;
		hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						 IEEE80211_STYPE_BEACON);

	} else if (ieee80211_vif_is_mesh(&sdata->vif)) {
		struct ieee80211_mgmt *mgmt;
		u8 *pos;

		/* headroom, head length, tail length and maximum TIM length */
		skb = dev_alloc_skb(local->tx_headroom + 400);
		if (!skb)
			goto out;

		skb_reserve(skb, local->hw.extra_tx_headroom);
		mgmt = (struct ieee80211_mgmt *)
			skb_put(skb, 24 + sizeof(mgmt->u.beacon));
		memset(mgmt, 0, 24 + sizeof(mgmt->u.beacon));
		mgmt->frame_control =
		    cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
		memset(mgmt->da, 0xff, ETH_ALEN);
		memcpy(mgmt->sa, sdata->dev->dev_addr, ETH_ALEN);
		/* BSSID is left zeroed, wildcard value */
		mgmt->u.beacon.beacon_int =
			cpu_to_le16(local->hw.conf.beacon_int);
		mgmt->u.beacon.capab_info = 0x0; /* 0x0 for MPs */

		pos = skb_put(skb, 2);
		*pos++ = WLAN_EID_SSID;
		*pos++ = 0x0;

		mesh_mgmt_ies_add(skb, sdata);
	} else {
		WARN_ON(1);
		goto out;
	}

	info = IEEE80211_SKB_CB(skb);

	skb->do_not_encrypt = 1;

	info->band = band;
	rate_control_get_rate(sdata, sband, NULL, skb, &rsel);

	if (unlikely(rsel.rate_idx < 0)) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: ieee80211_beacon_get: "
			       "no rate found\n",
			       wiphy_name(local->hw.wiphy));
		}
		dev_kfree_skb_any(skb);
		skb = NULL;
		goto out;
	}

	info->control.vif = vif;
	info->tx_rate_idx = rsel.rate_idx;

	info->flags |= IEEE80211_TX_CTL_NO_ACK;
	info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT;
	info->flags |= IEEE80211_TX_CTL_ASSIGN_SEQ;
	if (sdata->bss_conf.use_short_preamble &&
	    sband->bitrates[rsel.rate_idx].flags & IEEE80211_RATE_SHORT_PREAMBLE)
		info->flags |= IEEE80211_TX_CTL_SHORT_PREAMBLE;

	info->antenna_sel_tx = local->hw.conf.antenna_sel_tx;
	info->control.retry_limit = 1;

out:
	rcu_read_unlock();
	return skb;
}
EXPORT_SYMBOL(ieee80211_beacon_get);

void ieee80211_rts_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       const void *frame, size_t frame_len,
		       const struct ieee80211_tx_info *frame_txctl,
		       struct ieee80211_rts *rts)
{
	const struct ieee80211_hdr *hdr = frame;

	rts->frame_control =
	    cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);
	rts->duration = ieee80211_rts_duration(hw, vif, frame_len,
					       frame_txctl);
	memcpy(rts->ra, hdr->addr1, sizeof(rts->ra));
	memcpy(rts->ta, hdr->addr2, sizeof(rts->ta));
}
EXPORT_SYMBOL(ieee80211_rts_get);

void ieee80211_ctstoself_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     const void *frame, size_t frame_len,
			     const struct ieee80211_tx_info *frame_txctl,
			     struct ieee80211_cts *cts)
{
	const struct ieee80211_hdr *hdr = frame;

	cts->frame_control =
	    cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS);
	cts->duration = ieee80211_ctstoself_duration(hw, vif,
						     frame_len, frame_txctl);
	memcpy(cts->ra, hdr->addr1, sizeof(cts->ra));
}
EXPORT_SYMBOL(ieee80211_ctstoself_get);

struct sk_buff *
ieee80211_get_buffered_bc(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sk_buff *skb = NULL;
	struct sta_info *sta;
	struct ieee80211_tx_data tx;
	struct net_device *bdev;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_ap *bss = NULL;
	struct beacon_data *beacon;
	struct ieee80211_tx_info *info;

	sdata = vif_to_sdata(vif);
	bdev = sdata->dev;
	bss = &sdata->u.ap;

	if (!bss)
		return NULL;

	rcu_read_lock();
	beacon = rcu_dereference(bss->beacon);

	if (sdata->vif.type != NL80211_IFTYPE_AP || !beacon || !beacon->head)
		goto out;

	if (bss->dtim_count != 0)
		goto out; /* send buffered bc/mc only after DTIM beacon */

	while (1) {
		skb = skb_dequeue(&bss->ps_bc_buf);
		if (!skb)
			goto out;
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

		if (!ieee80211_tx_prepare(local, &tx, skb))
			break;
		dev_kfree_skb_any(skb);
	}

	info = IEEE80211_SKB_CB(skb);

	sta = tx.sta;
	tx.flags |= IEEE80211_TX_PS_BUFFERED;
	tx.channel = local->hw.conf.channel;
	info->band = tx.channel->band;

	if (invoke_tx_handlers(&tx))
		skb = NULL;
 out:
	rcu_read_unlock();

	return skb;
}
EXPORT_SYMBOL(ieee80211_get_buffered_bc);
