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
#include "driver-ops.h"
#include "led.h"
#include "mesh.h"
#include "wep.h"
#include "wpa.h"
#include "wme.h"
#include "rate.h"

#define IEEE80211_TX_OK		0
#define IEEE80211_TX_AGAIN	1
#define IEEE80211_TX_PENDING	2

/* misc utils */

static __le16 ieee80211_duration(struct ieee80211_tx_data *tx, int group_addr,
				 int next_frag_len)
{
	int rate, mrate, erp, dur, i;
	struct ieee80211_rate *txrate;
	struct ieee80211_local *local = tx->local;
	struct ieee80211_supported_band *sband;
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);

	/* assume HW handles this */
	if (info->control.rates[0].flags & IEEE80211_TX_RC_MCS)
		return 0;

	/* uh huh? */
	if (WARN_ON_ONCE(info->control.rates[0].idx < 0))
		return 0;

	sband = local->hw.wiphy->bands[tx->channel->band];
	txrate = &sband->bitrates[info->control.rates[0].idx];

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

		if (tx->sdata->vif.bss_conf.basic_rates & BIT(i))
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
				tx->sdata->vif.bss_conf.use_short_preamble);

	if (next_frag_len) {
		/* Frame is fragmented: duration increases with time needed to
		 * transmit next fragment plus ACK and 2 x SIFS. */
		dur *= 2; /* ACK + SIFS */
		/* next fragment */
		dur += ieee80211_frame_duration(local, next_frag_len,
				txrate->bitrate, erp,
				tx->sdata->vif.bss_conf.use_short_preamble);
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

	if (unlikely(test_bit(SCAN_OFF_CHANNEL, &tx->local->scanning)) &&
	    !ieee80211_is_probe_req(hdr->frame_control) &&
	    !ieee80211_is_nullfunc(hdr->frame_control))
		/*
		 * When software scanning only nullfunc frames (to notify
		 * the sleep state to the AP) and probe requests (for the
		 * active scan) are allowed, all other frames should not be
		 * sent and we should not get here, but if we do
		 * nonetheless, drop them to avoid sending them
		 * off-channel. See the link below and
		 * ieee80211_start_scan() for more.
		 *
		 * http://article.gmane.org/gmane.linux.kernel.wireless.general/30089
		 */
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
			printk(KERN_DEBUG "%s: dropped data frame to not "
			       "associated station %pM\n",
			       tx->dev->name, hdr->addr1);
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

	/* buffered in hardware */
	if (!(tx->local->hw.flags & IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING)) {
		info->flags |= IEEE80211_TX_CTL_SEND_AFTER_DTIM;

		return TX_CONTINUE;
	}

	/* buffered in mac80211 */
	if (tx->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
		purge_old_ps_buffers(tx->local);

	if (skb_queue_len(&tx->sdata->bss->ps_bc_buf) >= AP_MAX_BC_BUFFER) {
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: BC TX buffer full - dropping the oldest frame\n",
			       tx->dev->name);
#endif
		dev_kfree_skb(skb_dequeue(&tx->sdata->bss->ps_bc_buf));
	} else
		tx->local->total_ps_buffered++;

	skb_queue_tail(&tx->sdata->bss->ps_bc_buf, tx->skb);

	return TX_QUEUED;
}

static int ieee80211_use_mfp(__le16 fc, struct sta_info *sta,
			     struct sk_buff *skb)
{
	if (!ieee80211_is_mgmt(fc))
		return 0;

	if (sta == NULL || !test_sta_flags(sta, WLAN_STA_MFP))
		return 0;

	if (!ieee80211_is_robust_mgmt_frame((struct ieee80211_hdr *)
					    skb->data))
		return 0;

	return 1;
}

static ieee80211_tx_result
ieee80211_tx_h_unicast_ps_buf(struct ieee80211_tx_data *tx)
{
	struct sta_info *sta = tx->sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	u32 staflags;

	if (unlikely(!sta || ieee80211_is_probe_resp(hdr->frame_control)
			|| ieee80211_is_auth(hdr->frame_control)
			|| ieee80211_is_assoc_resp(hdr->frame_control)
			|| ieee80211_is_reassoc_resp(hdr->frame_control)))
		return TX_CONTINUE;

	staflags = get_sta_flags(sta);

	if (unlikely((staflags & WLAN_STA_PS) &&
		     !(info->flags & IEEE80211_TX_CTL_PSPOLL_RESPONSE))) {
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		printk(KERN_DEBUG "STA %pM aid %d: PS buffer (entries "
		       "before %d)\n",
		       sta->sta.addr, sta->sta.aid,
		       skb_queue_len(&sta->ps_tx_buf));
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
		if (tx->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
			purge_old_ps_buffers(tx->local);
		if (skb_queue_len(&sta->ps_tx_buf) >= STA_MAX_TX_BUFFER) {
			struct sk_buff *old = skb_dequeue(&sta->ps_tx_buf);
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: STA %pM TX "
				       "buffer full - dropping oldest frame\n",
				       tx->dev->name, sta->sta.addr);
			}
#endif
			dev_kfree_skb(old);
		} else
			tx->local->total_ps_buffered++;

		/* Queue frame to be sent after STA sends an PS Poll frame */
		if (skb_queue_empty(&sta->ps_tx_buf))
			sta_info_set_tim_bit(sta);

		info->control.jiffies = jiffies;
		info->control.vif = &tx->sdata->vif;
		info->flags |= IEEE80211_TX_INTFL_NEED_TXPROCESSING;
		skb_queue_tail(&sta->ps_tx_buf, tx->skb);
		return TX_QUEUED;
	}
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	else if (unlikely(test_sta_flags(sta, WLAN_STA_PS))) {
		printk(KERN_DEBUG "%s: STA %pM in PS mode, but pspoll "
		       "set -> send frame\n", tx->dev->name,
		       sta->sta.addr);
	}
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */

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
	struct ieee80211_key *key = NULL;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;

	if (unlikely(info->flags & IEEE80211_TX_INTFL_DONT_ENCRYPT))
		tx->key = NULL;
	else if (tx->sta && (key = rcu_dereference(tx->sta->key)))
		tx->key = key;
	else if (ieee80211_is_mgmt(hdr->frame_control) &&
		 (key = rcu_dereference(tx->sdata->default_mgmt_key)))
		tx->key = key;
	else if ((key = rcu_dereference(tx->sdata->default_key)))
		tx->key = key;
	else if (tx->sdata->drop_unencrypted &&
		 (tx->skb->protocol != cpu_to_be16(ETH_P_PAE)) &&
		 !(info->flags & IEEE80211_TX_CTL_INJECTED) &&
		 (!ieee80211_is_robust_mgmt_frame(hdr) ||
		  (ieee80211_is_action(hdr->frame_control) &&
		   tx->sta && test_sta_flags(tx->sta, WLAN_STA_MFP)))) {
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
			if (!ieee80211_is_data_present(hdr->frame_control))
				tx->key = NULL;
			break;
		case ALG_CCMP:
			if (!ieee80211_is_data_present(hdr->frame_control) &&
			    !ieee80211_use_mfp(hdr->frame_control, tx->sta,
					       tx->skb))
				tx->key = NULL;
			break;
		case ALG_AES_CMAC:
			if (!ieee80211_is_mgmt(hdr->frame_control))
				tx->key = NULL;
			break;
		}
	}

	if (!tx->key || !(tx->key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
		info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_rate_ctrl(struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (void *)tx->skb->data;
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *rate;
	int i, len;
	bool inval = false, rts = false, short_preamble = false;
	struct ieee80211_tx_rate_control txrc;
	u32 sta_flags;

	memset(&txrc, 0, sizeof(txrc));

	sband = tx->local->hw.wiphy->bands[tx->channel->band];

	len = min_t(int, tx->skb->len + FCS_LEN,
			 tx->local->hw.wiphy->frag_threshold);

	/* set up the tx rate control struct we give the RC algo */
	txrc.hw = local_to_hw(tx->local);
	txrc.sband = sband;
	txrc.bss_conf = &tx->sdata->vif.bss_conf;
	txrc.skb = tx->skb;
	txrc.reported_rate.idx = -1;
	txrc.max_rate_idx = tx->sdata->max_ratectrl_rateidx;

	/* set up RTS protection if desired */
	if (len > tx->local->hw.wiphy->rts_threshold) {
		txrc.rts = rts = true;
	}

	/*
	 * Use short preamble if the BSS can handle it, but not for
	 * management frames unless we know the receiver can handle
	 * that -- the management frame might be to a station that
	 * just wants a probe response.
	 */
	if (tx->sdata->vif.bss_conf.use_short_preamble &&
	    (ieee80211_is_data(hdr->frame_control) ||
	     (tx->sta && test_sta_flags(tx->sta, WLAN_STA_SHORT_PREAMBLE))))
		txrc.short_preamble = short_preamble = true;

	sta_flags = tx->sta ? get_sta_flags(tx->sta) : 0;

	/*
	 * Lets not bother rate control if we're associated and cannot
	 * talk to the sta. This should not happen.
	 */
	if (WARN(test_bit(SCAN_SW_SCANNING, &tx->local->scanning) &&
		 (sta_flags & WLAN_STA_ASSOC) &&
		 !rate_usable_index_exists(sband, &tx->sta->sta),
		 "%s: Dropped data frame as no usable bitrate found while "
		 "scanning and associated. Target station: "
		 "%pM on %d GHz band\n",
		 tx->dev->name, hdr->addr1,
		 tx->channel->band ? 5 : 2))
		return TX_DROP;

	/*
	 * If we're associated with the sta at this point we know we can at
	 * least send the frame at the lowest bit rate.
	 */
	rate_control_get_rate(tx->sdata, tx->sta, &txrc);

	if (unlikely(info->control.rates[0].idx < 0))
		return TX_DROP;

	if (txrc.reported_rate.idx < 0)
		txrc.reported_rate = info->control.rates[0];

	if (tx->sta)
		tx->sta->last_tx_rate = txrc.reported_rate;

	if (unlikely(!info->control.rates[0].count))
		info->control.rates[0].count = 1;

	if (WARN_ON_ONCE((info->control.rates[0].count > 1) &&
			 (info->flags & IEEE80211_TX_CTL_NO_ACK)))
		info->control.rates[0].count = 1;

	if (is_multicast_ether_addr(hdr->addr1)) {
		/*
		 * XXX: verify the rate is in the basic rateset
		 */
		return TX_CONTINUE;
	}

	/*
	 * set up the RTS/CTS rate as the fastest basic rate
	 * that is not faster than the data rate
	 *
	 * XXX: Should this check all retry rates?
	 */
	if (!(info->control.rates[0].flags & IEEE80211_TX_RC_MCS)) {
		s8 baserate = 0;

		rate = &sband->bitrates[info->control.rates[0].idx];

		for (i = 0; i < sband->n_bitrates; i++) {
			/* must be a basic rate */
			if (!(tx->sdata->vif.bss_conf.basic_rates & BIT(i)))
				continue;
			/* must not be faster than the data rate */
			if (sband->bitrates[i].bitrate > rate->bitrate)
				continue;
			/* maximum */
			if (sband->bitrates[baserate].bitrate <
			     sband->bitrates[i].bitrate)
				baserate = i;
		}

		info->control.rts_cts_rate_idx = baserate;
	}

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		/*
		 * make sure there's no valid rate following
		 * an invalid one, just in case drivers don't
		 * take the API seriously to stop at -1.
		 */
		if (inval) {
			info->control.rates[i].idx = -1;
			continue;
		}
		if (info->control.rates[i].idx < 0) {
			inval = true;
			continue;
		}

		/*
		 * For now assume MCS is already set up correctly, this
		 * needs to be fixed.
		 */
		if (info->control.rates[i].flags & IEEE80211_TX_RC_MCS) {
			WARN_ON(info->control.rates[i].idx > 76);
			continue;
		}

		/* set up RTS protection if desired */
		if (rts)
			info->control.rates[i].flags |=
				IEEE80211_TX_RC_USE_RTS_CTS;

		/* RC is busted */
		if (WARN_ON_ONCE(info->control.rates[i].idx >=
				 sband->n_bitrates)) {
			info->control.rates[i].idx = -1;
			continue;
		}

		rate = &sband->bitrates[info->control.rates[i].idx];

		/* set up short preamble */
		if (short_preamble &&
		    rate->flags & IEEE80211_RATE_SHORT_PREAMBLE)
			info->control.rates[i].flags |=
				IEEE80211_TX_RC_USE_SHORT_PREAMBLE;

		/* set up G protection */
		if (!rts && tx->sdata->vif.bss_conf.use_cts_prot &&
		    rate->flags & IEEE80211_RATE_ERP_G)
			info->control.rates[i].flags |=
				IEEE80211_TX_RC_USE_CTS_PROTECT;
	}

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_misc(struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);

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
	if (unlikely(info->control.vif->type == NL80211_IFTYPE_MONITOR))
		return TX_CONTINUE;

	if (unlikely(ieee80211_is_ctl(hdr->frame_control)))
		return TX_CONTINUE;

	if (ieee80211_hdrlen(hdr->frame_control) < 24)
		return TX_CONTINUE;

	/*
	 * Anything but QoS data that has a sequence number field
	 * (is long enough) gets a sequence number from the global
	 * counter.
	 */
	if (!ieee80211_is_data_qos(hdr->frame_control)) {
		/* driver should assign sequence number */
		info->flags |= IEEE80211_TX_CTL_ASSIGN_SEQ;
		/* for pure STA mode without beacons, we can do it */
		hdr->seq_ctrl = cpu_to_le16(tx->sdata->sequence_number);
		tx->sdata->sequence_number += 0x10;
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

static int ieee80211_fragment(struct ieee80211_local *local,
			      struct sk_buff *skb, int hdrlen,
			      int frag_threshold)
{
	struct sk_buff *tail = skb, *tmp;
	int per_fragm = frag_threshold - hdrlen - FCS_LEN;
	int pos = hdrlen + per_fragm;
	int rem = skb->len - hdrlen - per_fragm;

	if (WARN_ON(rem < 0))
		return -EINVAL;

	while (rem) {
		int fraglen = per_fragm;

		if (fraglen > rem)
			fraglen = rem;
		rem -= fraglen;
		tmp = dev_alloc_skb(local->tx_headroom +
				    frag_threshold +
				    IEEE80211_ENCRYPT_HEADROOM +
				    IEEE80211_ENCRYPT_TAILROOM);
		if (!tmp)
			return -ENOMEM;
		tail->next = tmp;
		tail = tmp;
		skb_reserve(tmp, local->tx_headroom +
				 IEEE80211_ENCRYPT_HEADROOM);
		/* copy control information */
		memcpy(tmp->cb, skb->cb, sizeof(tmp->cb));
		skb_copy_queue_mapping(tmp, skb);
		tmp->priority = skb->priority;
		tmp->dev = skb->dev;

		/* copy header and data */
		memcpy(skb_put(tmp, hdrlen), skb->data, hdrlen);
		memcpy(skb_put(tmp, fraglen), skb->data + pos, fraglen);

		pos += fraglen;
	}

	skb->len = hdrlen + per_fragm;
	return 0;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_fragment(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb = tx->skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	int frag_threshold = tx->local->hw.wiphy->frag_threshold;
	int hdrlen;
	int fragnum;

	if (!(tx->flags & IEEE80211_TX_FRAGMENTED))
		return TX_CONTINUE;

	/*
	 * Warn when submitting a fragmented A-MPDU frame and drop it.
	 * This scenario is handled in ieee80211_tx_prepare but extra
	 * caution taken here as fragmented ampdu may cause Tx stop.
	 */
	if (WARN_ON(info->flags & IEEE80211_TX_CTL_AMPDU))
		return TX_DROP;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	/* internal error, why is TX_FRAGMENTED set? */
	if (WARN_ON(skb->len + FCS_LEN <= frag_threshold))
		return TX_DROP;

	/*
	 * Now fragment the frame. This will allocate all the fragments and
	 * chain them (using skb as the first fragment) to skb->next.
	 * During transmission, we will remove the successfully transmitted
	 * fragments from this list. When the low-level driver rejects one
	 * of the fragments then we will simply pretend to accept the skb
	 * but store it away as pending.
	 */
	if (ieee80211_fragment(tx->local, skb, hdrlen, frag_threshold))
		return TX_DROP;

	/* update duration/seq/flags of fragments */
	fragnum = 0;
	do {
		int next_len;
		const __le16 morefrags = cpu_to_le16(IEEE80211_FCTL_MOREFRAGS);

		hdr = (void *)skb->data;
		info = IEEE80211_SKB_CB(skb);

		if (skb->next) {
			hdr->frame_control |= morefrags;
			next_len = skb->next->len;
			/*
			 * No multi-rate retries for fragmented frames, that
			 * would completely throw off the NAV at other STAs.
			 */
			info->control.rates[1].idx = -1;
			info->control.rates[2].idx = -1;
			info->control.rates[3].idx = -1;
			info->control.rates[4].idx = -1;
			BUILD_BUG_ON(IEEE80211_TX_MAX_RATES != 5);
			info->flags &= ~IEEE80211_TX_CTL_RATE_CTRL_PROBE;
		} else {
			hdr->frame_control &= ~morefrags;
			next_len = 0;
		}
		hdr->duration_id = ieee80211_duration(tx, 0, next_len);
		hdr->seq_ctrl |= cpu_to_le16(fragnum & IEEE80211_SCTL_FRAG);
		fragnum++;
	} while ((skb = skb->next));

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_stats(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb = tx->skb;

	if (!tx->sta)
		return TX_CONTINUE;

	tx->sta->tx_packets++;
	do {
		tx->sta->tx_fragments++;
		tx->sta->tx_bytes += skb->len;
	} while ((skb = skb->next));

	return TX_CONTINUE;
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
	case ALG_AES_CMAC:
		return ieee80211_crypto_aes_cmac_encrypt(tx);
	}

	/* not reached */
	WARN_ON(1);
	return TX_DROP;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_calculate_duration(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb = tx->skb;
	struct ieee80211_hdr *hdr;
	int next_len;
	bool group_addr;

	do {
		hdr = (void *) skb->data;
		if (unlikely(ieee80211_is_pspoll(hdr->frame_control)))
			break; /* must not overwrite AID */
		next_len = skb->next ? skb->next->len : 0;
		group_addr = is_multicast_ether_addr(hdr->addr1);

		hdr->duration_id =
			ieee80211_duration(tx, group_addr, next_len);
	} while ((skb = skb->next));

	return TX_CONTINUE;
}

/* actual transmit path */

/*
 * deal with packet injection down monitor interface
 * with Radiotap Header -- only called for monitor mode interface
 */
static bool __ieee80211_parse_tx_radiotap(struct ieee80211_tx_data *tx,
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
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int ret = ieee80211_radiotap_iterator_init(&iterator, rthdr, skb->len);

	sband = tx->local->hw.wiphy->bands[tx->channel->band];

	info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	tx->flags &= ~IEEE80211_TX_FRAGMENTED;

	/*
	 * for every radiotap entry that is present
	 * (ieee80211_radiotap_iterator_next returns -ENOENT when no more
	 * entries present, or -EINVAL on error)
	 */

	while (!ret) {
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
					return false;

				skb_trim(skb, skb->len - FCS_LEN);
			}
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_WEP)
				info->flags &= ~IEEE80211_TX_INTFL_DONT_ENCRYPT;
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
		return false;

	/*
	 * remove the radiotap header
	 * iterator->max_length was sanity-checked against
	 * skb->len by iterator init
	 */
	skb_pull(skb, iterator.max_length);

	return true;
}

/*
 * initialises @tx
 */
static ieee80211_tx_result
ieee80211_tx_prepare(struct ieee80211_sub_if_data *sdata,
		     struct ieee80211_tx_data *tx,
		     struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int hdrlen, tid;
	u8 *qc, *state;
	bool queued = false;

	memset(tx, 0, sizeof(*tx));
	tx->skb = skb;
	tx->dev = sdata->dev; /* use original interface */
	tx->local = local;
	tx->sdata = sdata;
	tx->channel = local->hw.conf.channel;
	/*
	 * Set this flag (used below to indicate "automatic fragmentation"),
	 * it will be cleared/left by radiotap as desired.
	 */
	tx->flags |= IEEE80211_TX_FRAGMENTED;

	/* process and remove the injection radiotap header */
	if (unlikely(info->flags & IEEE80211_TX_CTL_INJECTED)) {
		if (!__ieee80211_parse_tx_radiotap(tx, skb))
			return TX_DROP;

		/*
		 * __ieee80211_parse_tx_radiotap has now removed
		 * the radiotap header that was present and pre-filled
		 * 'tx' with tx control information.
		 */
	}

	/*
	 * If this flag is set to true anywhere, and we get here,
	 * we are doing the needed processing, so remove the flag
	 * now.
	 */
	info->flags &= ~IEEE80211_TX_INTFL_NEED_TXPROCESSING;

	hdr = (struct ieee80211_hdr *) skb->data;

	tx->sta = sta_info_get(local, hdr->addr1);

	if (tx->sta && ieee80211_is_data_qos(hdr->frame_control) &&
	    (local->hw.flags & IEEE80211_HW_AMPDU_AGGREGATION)) {
		unsigned long flags;
		struct tid_ampdu_tx *tid_tx;

		qc = ieee80211_get_qos_ctl(hdr);
		tid = *qc & IEEE80211_QOS_CTL_TID_MASK;

		spin_lock_irqsave(&tx->sta->lock, flags);
		/*
		 * XXX: This spinlock could be fairly expensive, but see the
		 *	comment in agg-tx.c:ieee80211_agg_tx_operational().
		 *	One way to solve this would be to do something RCU-like
		 *	for managing the tid_tx struct and using atomic bitops
		 *	for the actual state -- by introducing an actual
		 *	'operational' bit that would be possible. It would
		 *	require changing ieee80211_agg_tx_operational() to
		 *	set that bit, and changing the way tid_tx is managed
		 *	everywhere, including races between that bit and
		 *	tid_tx going away (tid_tx being added can be easily
		 *	committed to memory before the 'operational' bit).
		 */
		tid_tx = tx->sta->ampdu_mlme.tid_tx[tid];
		state = &tx->sta->ampdu_mlme.tid_state_tx[tid];
		if (*state == HT_AGG_STATE_OPERATIONAL) {
			info->flags |= IEEE80211_TX_CTL_AMPDU;
		} else if (*state != HT_AGG_STATE_IDLE) {
			/* in progress */
			queued = true;
			info->control.vif = &sdata->vif;
			info->flags |= IEEE80211_TX_INTFL_NEED_TXPROCESSING;
			__skb_queue_tail(&tid_tx->pending, skb);
		}
		spin_unlock_irqrestore(&tx->sta->lock, flags);

		if (unlikely(queued))
			return TX_QUEUED;
	}

	if (is_multicast_ether_addr(hdr->addr1)) {
		tx->flags &= ~IEEE80211_TX_UNICAST;
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	} else {
		tx->flags |= IEEE80211_TX_UNICAST;
		if (unlikely(local->wifi_wme_noack_test))
			info->flags |= IEEE80211_TX_CTL_NO_ACK;
		else
			info->flags &= ~IEEE80211_TX_CTL_NO_ACK;
	}

	if (tx->flags & IEEE80211_TX_FRAGMENTED) {
		if ((tx->flags & IEEE80211_TX_UNICAST) &&
		    skb->len + FCS_LEN > local->hw.wiphy->frag_threshold &&
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

static int __ieee80211_tx(struct ieee80211_local *local,
			  struct sk_buff **skbp,
			  struct sta_info *sta,
			  bool txpending)
{
	struct sk_buff *skb = *skbp, *next;
	struct ieee80211_tx_info *info;
	struct ieee80211_sub_if_data *sdata;
	unsigned long flags;
	int ret, len;
	bool fragm = false;

	while (skb) {
		int q = skb_get_queue_mapping(skb);

		spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
		ret = IEEE80211_TX_OK;
		if (local->queue_stop_reasons[q] ||
		    (!txpending && !skb_queue_empty(&local->pending[q])))
			ret = IEEE80211_TX_PENDING;
		spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
		if (ret != IEEE80211_TX_OK)
			return ret;

		info = IEEE80211_SKB_CB(skb);

		if (fragm)
			info->flags &= ~(IEEE80211_TX_CTL_CLEAR_PS_FILT |
					 IEEE80211_TX_CTL_FIRST_FRAGMENT);

		next = skb->next;
		len = skb->len;

		if (next)
			info->flags |= IEEE80211_TX_CTL_MORE_FRAMES;

		sdata = vif_to_sdata(info->control.vif);

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_MONITOR:
			info->control.vif = NULL;
			break;
		case NL80211_IFTYPE_AP_VLAN:
			info->control.vif = &container_of(sdata->bss,
				struct ieee80211_sub_if_data, u.ap)->vif;
			break;
		default:
			/* keep */
			break;
		}

		ret = drv_tx(local, skb);
		if (WARN_ON(ret != NETDEV_TX_OK && skb->len != len)) {
			dev_kfree_skb(skb);
			ret = NETDEV_TX_OK;
		}
		if (ret != NETDEV_TX_OK) {
			info->control.vif = &sdata->vif;
			return IEEE80211_TX_AGAIN;
		}

		*skbp = skb = next;
		ieee80211_led_tx(local, 1);
		fragm = true;
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
	CALL_TXH(ieee80211_tx_h_stats)
	CALL_TXH(ieee80211_tx_h_encrypt)
	CALL_TXH(ieee80211_tx_h_calculate_duration)
#undef CALL_TXH

 txh_done:
	if (unlikely(res == TX_DROP)) {
		I802_DEBUG_INC(tx->local->tx_handlers_drop);
		while (skb) {
			struct sk_buff *next;

			next = skb->next;
			dev_kfree_skb(skb);
			skb = next;
		}
		return -1;
	} else if (unlikely(res == TX_QUEUED)) {
		I802_DEBUG_INC(tx->local->tx_handlers_queued);
		return -1;
	}

	return 0;
}

static void ieee80211_tx(struct ieee80211_sub_if_data *sdata,
			 struct sk_buff *skb, bool txpending)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_data tx;
	ieee80211_tx_result res_prepare;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct sk_buff *next;
	unsigned long flags;
	int ret, retries;
	u16 queue;

	queue = skb_get_queue_mapping(skb);

	if (unlikely(skb->len < 10)) {
		dev_kfree_skb(skb);
		return;
	}

	rcu_read_lock();

	/* initialises tx */
	res_prepare = ieee80211_tx_prepare(sdata, &tx, skb);

	if (unlikely(res_prepare == TX_DROP)) {
		dev_kfree_skb(skb);
		rcu_read_unlock();
		return;
	} else if (unlikely(res_prepare == TX_QUEUED)) {
		rcu_read_unlock();
		return;
	}

	tx.channel = local->hw.conf.channel;
	info->band = tx.channel->band;

	if (invoke_tx_handlers(&tx))
		goto out;

	retries = 0;
 retry:
	ret = __ieee80211_tx(local, &tx.skb, tx.sta, txpending);
	switch (ret) {
	case IEEE80211_TX_OK:
		break;
	case IEEE80211_TX_AGAIN:
		/*
		 * Since there are no fragmented frames on A-MPDU
		 * queues, there's no reason for a driver to reject
		 * a frame there, warn and drop it.
		 */
		if (WARN_ON(info->flags & IEEE80211_TX_CTL_AMPDU))
			goto drop;
		/* fall through */
	case IEEE80211_TX_PENDING:
		skb = tx.skb;

		spin_lock_irqsave(&local->queue_stop_reason_lock, flags);

		if (local->queue_stop_reasons[queue] ||
		    !skb_queue_empty(&local->pending[queue])) {
			/*
			 * if queue is stopped, queue up frames for later
			 * transmission from the tasklet
			 */
			do {
				next = skb->next;
				skb->next = NULL;
				if (unlikely(txpending))
					__skb_queue_head(&local->pending[queue],
							 skb);
				else
					__skb_queue_tail(&local->pending[queue],
							 skb);
			} while ((skb = next));

			spin_unlock_irqrestore(&local->queue_stop_reason_lock,
					       flags);
		} else {
			/*
			 * otherwise retry, but this is a race condition or
			 * a driver bug (which we warn about if it persists)
			 */
			spin_unlock_irqrestore(&local->queue_stop_reason_lock,
					       flags);

			retries++;
			if (WARN(retries > 10, "tx refused but queue active\n"))
				goto drop;
			goto retry;
		}
	}
 out:
	rcu_read_unlock();
	return;

 drop:
	rcu_read_unlock();

	skb = tx.skb;
	while (skb) {
		next = skb->next;
		dev_kfree_skb(skb);
		skb = next;
	}
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

static void ieee80211_xmit(struct ieee80211_sub_if_data *sdata,
			   struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_sub_if_data *tmp_sdata;
	int headroom;
	bool may_encrypt;

	dev_hold(sdata->dev);

	if ((local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK) &&
	    local->hw.conf.dynamic_ps_timeout > 0 &&
	    !(local->scanning) && local->ps_sdata) {
		if (local->hw.conf.flags & IEEE80211_CONF_PS) {
			ieee80211_stop_queues_by_reason(&local->hw,
					IEEE80211_QUEUE_STOP_REASON_PS);
			ieee80211_queue_work(&local->hw,
					&local->dynamic_ps_disable_work);
		}

		mod_timer(&local->dynamic_ps_timer, jiffies +
		        msecs_to_jiffies(local->hw.conf.dynamic_ps_timeout));
	}

	info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;

	if (unlikely(sdata->vif.type == NL80211_IFTYPE_MONITOR)) {
		int hdrlen;
		u16 len_rthdr;

		info->flags |= IEEE80211_TX_CTL_INJECTED;

		len_rthdr = ieee80211_get_radiotap_len(skb->data);
		hdr = (struct ieee80211_hdr *)(skb->data + len_rthdr);
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
			list_for_each_entry_rcu(tmp_sdata, &local->interfaces,
						list) {
				if (!netif_running(tmp_sdata->dev))
					continue;
				if (tmp_sdata->vif.type != NL80211_IFTYPE_AP)
					continue;
				if (compare_ether_addr(tmp_sdata->dev->dev_addr,
						       hdr->addr2)) {
					dev_hold(tmp_sdata->dev);
					dev_put(sdata->dev);
					sdata = tmp_sdata;
					break;
				}
			}
			rcu_read_unlock();
		}
	}

	may_encrypt = !(info->flags & IEEE80211_TX_INTFL_DONT_ENCRYPT);

	headroom = local->tx_headroom;
	if (may_encrypt)
		headroom += IEEE80211_ENCRYPT_HEADROOM;
	headroom -= skb_headroom(skb);
	headroom = max_t(int, 0, headroom);

	if (ieee80211_skb_resize(local, skb, headroom, may_encrypt)) {
		dev_kfree_skb(skb);
		dev_put(sdata->dev);
		return;
	}

	info->control.vif = &sdata->vif;

	if (ieee80211_vif_is_mesh(&sdata->vif) &&
	    ieee80211_is_data(hdr->frame_control) &&
		!is_multicast_ether_addr(hdr->addr1))
			if (mesh_nexthop_lookup(skb, sdata)) {
				/* skb queued: don't free */
				dev_put(sdata->dev);
				return;
			}

	ieee80211_select_queue(local, skb);
	ieee80211_tx(sdata, skb, false);
	dev_put(sdata->dev);
}

netdev_tx_t ieee80211_monitor_start_xmit(struct sk_buff *skb,
					 struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_channel *chan = local->hw.conf.channel;
	struct ieee80211_radiotap_header *prthdr =
		(struct ieee80211_radiotap_header *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u16 len_rthdr;

	/*
	 * Frame injection is not allowed if beaconing is not allowed
	 * or if we need radar detection. Beaconing is usually not allowed when
	 * the mode or operation (Adhoc, AP, Mesh) does not support DFS.
	 * Passive scan is also used in world regulatory domains where
	 * your country is not known and as such it should be treated as
	 * NO TX unless the channel is explicitly allowed in which case
	 * your current regulatory domain would not have the passive scan
	 * flag.
	 *
	 * Since AP mode uses monitor interfaces to inject/TX management
	 * frames we can make AP mode the exception to this rule once it
	 * supports radar detection as its implementation can deal with
	 * radar detection by itself. We can do that later by adding a
	 * monitor flag interfaces used for AP support.
	 */
	if ((chan->flags & (IEEE80211_CHAN_NO_IBSS | IEEE80211_CHAN_RADAR |
	     IEEE80211_CHAN_PASSIVE_SCAN)))
		goto fail;

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

	memset(info, 0, sizeof(*info));

	/* pass the radiotap header up to xmit */
	ieee80211_xmit(IEEE80211_DEV_TO_SUB_IF(dev), skb);
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
netdev_tx_t ieee80211_subif_start_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int ret = NETDEV_TX_BUSY, head_need;
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
		ret = NETDEV_TX_OK;
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
		if (!sdata->u.mesh.mshcfg.dot11MeshTTL) {
			/* Do not send frames with mesh_ttl == 0 */
			sdata->u.mesh.mshstats.dropped_frames_ttl++;
			ret = NETDEV_TX_OK;
			goto fail;
		}

		if (compare_ether_addr(dev->dev_addr,
					  skb->data + ETH_ALEN) == 0) {
			hdrlen = ieee80211_fill_mesh_addresses(&hdr, &fc,
					skb->data, skb->data + ETH_ALEN);
			meshhdrlen = ieee80211_new_mesh_header(&mesh_hdr,
					sdata, NULL, NULL, NULL);
		} else {
			/* packet from other interface */
			struct mesh_path *mppath;
			int is_mesh_mcast = 1;
			char *mesh_da;

			rcu_read_lock();
			if (is_multicast_ether_addr(skb->data))
				/* DA TA mSA AE:SA */
				mesh_da = skb->data;
			else {
				mppath = mpp_path_lookup(skb->data, sdata);
				if (mppath) {
					/* RA TA mDA mSA AE:DA SA */
					mesh_da = mppath->mpp;
					is_mesh_mcast = 0;
				} else
					/* DA TA mSA AE:SA */
					mesh_da = dev->broadcast;
			}
			hdrlen = ieee80211_fill_mesh_addresses(&hdr, &fc,
					mesh_da, dev->dev_addr);
			rcu_read_unlock();
			if (is_mesh_mcast)
				meshhdrlen =
					ieee80211_new_mesh_header(&mesh_hdr,
							sdata,
							skb->data + ETH_ALEN,
							NULL,
							NULL);
			else
				meshhdrlen =
					ieee80211_new_mesh_header(&mesh_hdr,
							sdata,
							NULL,
							skb->data,
							skb->data + ETH_ALEN);

		}
		break;
#endif
	case NL80211_IFTYPE_STATION:
		fc |= cpu_to_le16(IEEE80211_FCTL_TODS);
		/* BSSID SA DA */
		memcpy(hdr.addr1, sdata->u.mgd.bssid, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, skb->data, ETH_ALEN);
		hdrlen = 24;
		break;
	case NL80211_IFTYPE_ADHOC:
		/* DA SA BSSID */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, sdata->u.ibss.bssid, ETH_ALEN);
		hdrlen = 24;
		break;
	default:
		ret = NETDEV_TX_OK;
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
	if ((sta_flags & WLAN_STA_WME) && local->hw.queues >= 4) {
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
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: dropped frame to %pM"
			       " (unauthorized port)\n", dev->name,
			       hdr.addr1);
#endif

		I802_DEBUG_INC(local->tx_handlers_drop_unauth_port);

		ret = NETDEV_TX_OK;
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

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	/* Update skb pointers to various headers since this modified frame
	 * is going to go through Linux networking code that may potentially
	 * need things like pointer to IP header. */
	skb_set_mac_header(skb, 0);
	skb_set_network_header(skb, nh_pos);
	skb_set_transport_header(skb, h_pos);

	memset(info, 0, sizeof(*info));

	dev->trans_start = jiffies;
	ieee80211_xmit(sdata, skb);

	return NETDEV_TX_OK;

 fail:
	if (ret == NETDEV_TX_OK)
		dev_kfree_skb(skb);

	return ret;
}


/*
 * ieee80211_clear_tx_pending may not be called in a context where
 * it is possible that it packets could come in again.
 */
void ieee80211_clear_tx_pending(struct ieee80211_local *local)
{
	int i;

	for (i = 0; i < local->hw.queues; i++)
		skb_queue_purge(&local->pending[i]);
}

static bool ieee80211_tx_pending_skb(struct ieee80211_local *local,
				     struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	struct ieee80211_hdr *hdr;
	int ret;
	bool result = true;

	sdata = vif_to_sdata(info->control.vif);

	if (info->flags & IEEE80211_TX_INTFL_NEED_TXPROCESSING) {
		ieee80211_tx(sdata, skb, true);
	} else {
		hdr = (struct ieee80211_hdr *)skb->data;
		sta = sta_info_get(local, hdr->addr1);

		ret = __ieee80211_tx(local, &skb, sta, true);
		if (ret != IEEE80211_TX_OK)
			result = false;
	}

	return result;
}

/*
 * Transmit all pending packets. Called from tasklet.
 */
void ieee80211_tx_pending(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *)data;
	unsigned long flags;
	int i;
	bool txok;

	rcu_read_lock();

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	for (i = 0; i < local->hw.queues; i++) {
		/*
		 * If queue is stopped by something other than due to pending
		 * frames, or we have no pending frames, proceed to next queue.
		 */
		if (local->queue_stop_reasons[i] ||
		    skb_queue_empty(&local->pending[i]))
			continue;

		while (!skb_queue_empty(&local->pending[i])) {
			struct sk_buff *skb = __skb_dequeue(&local->pending[i]);
			struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
			struct ieee80211_sub_if_data *sdata;

			if (WARN_ON(!info->control.vif)) {
				kfree_skb(skb);
				continue;
			}

			sdata = vif_to_sdata(info->control.vif);
			dev_hold(sdata->dev);
			spin_unlock_irqrestore(&local->queue_stop_reason_lock,
						flags);

			txok = ieee80211_tx_pending_skb(local, skb);
			dev_put(sdata->dev);
			if (!txok)
				__skb_queue_head(&local->pending[i], skb);
			spin_lock_irqsave(&local->queue_stop_reason_lock,
					  flags);
			if (!txok)
				break;
		}
	}
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);

	rcu_read_unlock();
}

/* functions for drivers to get certain frames */

static void ieee80211_beacon_add_tim(struct ieee80211_if_ap *bss,
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
	struct ieee80211_sub_if_data *sdata = NULL;
	struct ieee80211_if_ap *ap = NULL;
	struct beacon_data *beacon;
	struct ieee80211_supported_band *sband;
	enum ieee80211_band band = local->hw.conf.channel->band;

	sband = local->hw.wiphy->bands[band];

	rcu_read_lock();

	sdata = vif_to_sdata(vif);

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
				ieee80211_beacon_add_tim(ap, skb, beacon);
			} else {
				unsigned long flags;

				spin_lock_irqsave(&local->sta_lock, flags);
				ieee80211_beacon_add_tim(ap, skb, beacon);
				spin_unlock_irqrestore(&local->sta_lock, flags);
			}

			if (beacon->tail)
				memcpy(skb_put(skb, beacon->tail_len),
				       beacon->tail, beacon->tail_len);
		} else
			goto out;
	} else if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
		struct ieee80211_hdr *hdr;
		struct sk_buff *presp = rcu_dereference(ifibss->presp);

		if (!presp)
			goto out;

		skb = skb_copy(presp, GFP_ATOMIC);
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
			cpu_to_le16(sdata->vif.bss_conf.beacon_int);
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

	info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	info->band = band;
	/*
	 * XXX: For now, always use the lowest rate
	 */
	info->control.rates[0].idx = 0;
	info->control.rates[0].count = 1;
	info->control.rates[1].idx = -1;
	info->control.rates[2].idx = -1;
	info->control.rates[3].idx = -1;
	info->control.rates[4].idx = -1;
	BUILD_BUG_ON(IEEE80211_TX_MAX_RATES != 5);

	info->control.vif = vif;

	info->flags |= IEEE80211_TX_CTL_NO_ACK;
	info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT;
	info->flags |= IEEE80211_TX_CTL_ASSIGN_SEQ;
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
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_ap *bss = NULL;
	struct beacon_data *beacon;
	struct ieee80211_tx_info *info;

	sdata = vif_to_sdata(vif);
	bss = &sdata->u.ap;

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

		if (!ieee80211_tx_prepare(sdata, &tx, skb))
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

void ieee80211_tx_skb(struct ieee80211_sub_if_data *sdata, struct sk_buff *skb,
		      int encrypt)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	skb_set_mac_header(skb, 0);
	skb_set_network_header(skb, 0);
	skb_set_transport_header(skb, 0);

	if (!encrypt)
		info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;

	/*
	 * The other path calling ieee80211_xmit is from the tasklet,
	 * and while we can handle concurrent transmissions locking
	 * requirements are that we do not come into tx with bhs on.
	 */
	local_bh_disable();
	ieee80211_xmit(sdata, skb);
	local_bh_enable();
}
