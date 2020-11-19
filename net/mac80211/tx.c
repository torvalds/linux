/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2018, 2020 Intel Corporation
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
#include <linux/if_vlan.h>
#include <linux/etherdevice.h>
#include <linux/bitmap.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/ieee80211_radiotap.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/codel.h>
#include <net/codel_impl.h>
#include <asm/unaligned.h>
#include <net/fq_impl.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "led.h"
#include "mesh.h"
#include "wep.h"
#include "wpa.h"
#include "wme.h"
#include "rate.h"

/* misc utils */

static inline void ieee80211_tx_stats(struct net_device *dev, u32 len)
{
	struct pcpu_sw_netstats *tstats = this_cpu_ptr(dev->tstats);

	u64_stats_update_begin(&tstats->syncp);
	tstats->tx_packets++;
	tstats->tx_bytes += len;
	u64_stats_update_end(&tstats->syncp);
}

static __le16 ieee80211_duration(struct ieee80211_tx_data *tx,
				 struct sk_buff *skb, int group_addr,
				 int next_frag_len)
{
	int rate, mrate, erp, dur, i, shift = 0;
	struct ieee80211_rate *txrate;
	struct ieee80211_local *local = tx->local;
	struct ieee80211_supported_band *sband;
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_chanctx_conf *chanctx_conf;
	u32 rate_flags = 0;

	/* assume HW handles this */
	if (tx->rate.flags & (IEEE80211_TX_RC_MCS | IEEE80211_TX_RC_VHT_MCS))
		return 0;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(tx->sdata->vif.chanctx_conf);
	if (chanctx_conf) {
		shift = ieee80211_chandef_get_shift(&chanctx_conf->def);
		rate_flags = ieee80211_chandef_rate_flags(&chanctx_conf->def);
	}
	rcu_read_unlock();

	/* uh huh? */
	if (WARN_ON_ONCE(tx->rate.idx < 0))
		return 0;

	sband = local->hw.wiphy->bands[info->band];
	txrate = &sband->bitrates[tx->rate.idx];

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
	hdr = (struct ieee80211_hdr *)skb->data;
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

		if ((rate_flags & r->flags) != rate_flags)
			continue;

		if (tx->sdata->vif.bss_conf.basic_rates & BIT(i))
			rate = DIV_ROUND_UP(r->bitrate, 1 << shift);

		switch (sband->band) {
		case NL80211_BAND_2GHZ: {
			u32 flag;
			if (tx->sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
				flag = IEEE80211_RATE_MANDATORY_G;
			else
				flag = IEEE80211_RATE_MANDATORY_B;
			if (r->flags & flag)
				mrate = r->bitrate;
			break;
		}
		case NL80211_BAND_5GHZ:
			if (r->flags & IEEE80211_RATE_MANDATORY_A)
				mrate = r->bitrate;
			break;
		case NL80211_BAND_60GHZ:
			/* TODO, for now fall through */
		case NUM_NL80211_BANDS:
			WARN_ON(1);
			break;
		}
	}
	if (rate == -1) {
		/* No matching basic rate found; use highest suitable mandatory
		 * PHY rate */
		rate = DIV_ROUND_UP(mrate, 1 << shift);
	}

	/* Don't calculate ACKs for QoS Frames with NoAck Policy set */
	if (ieee80211_is_data_qos(hdr->frame_control) &&
	    *(ieee80211_get_qos_ctl(hdr)) & IEEE80211_QOS_CTL_ACK_POLICY_NOACK)
		dur = 0;
	else
		/* Time needed to transmit ACK
		 * (10 bytes + 4-byte FCS = 112 bits) plus SIFS; rounded up
		 * to closest integer */
		dur = ieee80211_frame_duration(sband->band, 10, rate, erp,
				tx->sdata->vif.bss_conf.use_short_preamble,
				shift);

	if (next_frag_len) {
		/* Frame is fragmented: duration increases with time needed to
		 * transmit next fragment plus ACK and 2 x SIFS. */
		dur *= 2; /* ACK + SIFS */
		/* next fragment */
		dur += ieee80211_frame_duration(sband->band, next_frag_len,
				txrate->bitrate, erp,
				tx->sdata->vif.bss_conf.use_short_preamble,
				shift);
	}

	return cpu_to_le16(dur);
}

/* tx handlers */
static ieee80211_tx_result debug_noinline
ieee80211_tx_h_dynamic_ps(struct ieee80211_tx_data *tx)
{
	struct ieee80211_local *local = tx->local;
	struct ieee80211_if_managed *ifmgd;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);

	/* driver doesn't support power save */
	if (!ieee80211_hw_check(&local->hw, SUPPORTS_PS))
		return TX_CONTINUE;

	/* hardware does dynamic power save */
	if (ieee80211_hw_check(&local->hw, SUPPORTS_DYNAMIC_PS))
		return TX_CONTINUE;

	/* dynamic power save disabled */
	if (local->hw.conf.dynamic_ps_timeout <= 0)
		return TX_CONTINUE;

	/* we are scanning, don't enable power save */
	if (local->scanning)
		return TX_CONTINUE;

	if (!local->ps_sdata)
		return TX_CONTINUE;

	/* No point if we're going to suspend */
	if (local->quiescing)
		return TX_CONTINUE;

	/* dynamic ps is supported only in managed mode */
	if (tx->sdata->vif.type != NL80211_IFTYPE_STATION)
		return TX_CONTINUE;

	if (unlikely(info->flags & IEEE80211_TX_INTFL_OFFCHAN_TX_OK))
		return TX_CONTINUE;

	ifmgd = &tx->sdata->u.mgd;

	/*
	 * Don't wakeup from power save if u-apsd is enabled, voip ac has
	 * u-apsd enabled and the frame is in voip class. This effectively
	 * means that even if all access categories have u-apsd enabled, in
	 * practise u-apsd is only used with the voip ac. This is a
	 * workaround for the case when received voip class packets do not
	 * have correct qos tag for some reason, due the network or the
	 * peer application.
	 *
	 * Note: ifmgd->uapsd_queues access is racy here. If the value is
	 * changed via debugfs, user needs to reassociate manually to have
	 * everything in sync.
	 */
	if ((ifmgd->flags & IEEE80211_STA_UAPSD_ENABLED) &&
	    (ifmgd->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VO) &&
	    skb_get_queue_mapping(tx->skb) == IEEE80211_AC_VO)
		return TX_CONTINUE;

	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		ieee80211_stop_queues_by_reason(&local->hw,
						IEEE80211_MAX_QUEUE_MAP,
						IEEE80211_QUEUE_STOP_REASON_PS,
						false);
		ifmgd->flags &= ~IEEE80211_STA_NULLFUNC_ACKED;
		ieee80211_queue_work(&local->hw,
				     &local->dynamic_ps_disable_work);
	}

	/* Don't restart the timer if we're not disassociated */
	if (!ifmgd->associated)
		return TX_CONTINUE;

	mod_timer(&local->dynamic_ps_timer, jiffies +
		  msecs_to_jiffies(local->hw.conf.dynamic_ps_timeout));

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_check_assoc(struct ieee80211_tx_data *tx)
{

	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	bool assoc = false;

	if (unlikely(info->flags & IEEE80211_TX_CTL_INJECTED))
		return TX_CONTINUE;

	if (unlikely(test_bit(SCAN_SW_SCANNING, &tx->local->scanning)) &&
	    test_bit(SDATA_STATE_OFFCHANNEL, &tx->sdata->state) &&
	    !ieee80211_is_probe_req(hdr->frame_control) &&
	    !ieee80211_is_any_nullfunc(hdr->frame_control))
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

	if (tx->sdata->vif.type == NL80211_IFTYPE_OCB)
		return TX_CONTINUE;

	if (tx->sdata->vif.type == NL80211_IFTYPE_WDS)
		return TX_CONTINUE;

	if (tx->flags & IEEE80211_TX_PS_BUFFERED)
		return TX_CONTINUE;

	if (tx->sta)
		assoc = test_sta_flag(tx->sta, WLAN_STA_ASSOC);

	if (likely(tx->flags & IEEE80211_TX_UNICAST)) {
		if (unlikely(!assoc &&
			     ieee80211_is_data(hdr->frame_control))) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
			sdata_info(tx->sdata,
				   "dropped data frame to not associated station %pM\n",
				   hdr->addr1);
#endif
			I802_DEBUG_INC(tx->local->tx_handlers_drop_not_assoc);
			return TX_DROP;
		}
	} else if (unlikely(ieee80211_is_data(hdr->frame_control) &&
			    ieee80211_vif_get_num_mcast_if(tx->sdata) == 0)) {
		/*
		 * No associated STAs - no need to send multicast
		 * frames.
		 */
		return TX_DROP;
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

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		struct ps_data *ps;

		if (sdata->vif.type == NL80211_IFTYPE_AP)
			ps = &sdata->u.ap.ps;
		else if (ieee80211_vif_is_mesh(&sdata->vif))
			ps = &sdata->u.mesh.ps;
		else
			continue;

		skb = skb_dequeue(&ps->bc_buf);
		if (skb) {
			purged++;
			ieee80211_free_txskb(&local->hw, skb);
		}
		total += skb_queue_len(&ps->bc_buf);
	}

	/*
	 * Drop one frame from each station from the lowest-priority
	 * AC that has frames at all.
	 */
	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		int ac;

		for (ac = IEEE80211_AC_BK; ac >= IEEE80211_AC_VO; ac--) {
			skb = skb_dequeue(&sta->ps_tx_buf[ac]);
			total += skb_queue_len(&sta->ps_tx_buf[ac]);
			if (skb) {
				purged++;
				ieee80211_free_txskb(&local->hw, skb);
				break;
			}
		}
	}

	local->total_ps_buffered = total;
	ps_dbg_hw(&local->hw, "PS buffers full - purged %d frames\n", purged);
}

static ieee80211_tx_result
ieee80211_tx_h_multicast_ps_buf(struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	struct ps_data *ps;

	/*
	 * broadcast/multicast frame
	 *
	 * If any of the associated/peer stations is in power save mode,
	 * the frame is buffered to be sent after DTIM beacon frame.
	 * This is done either by the hardware or us.
	 */

	/* powersaving STAs currently only in AP/VLAN/mesh mode */
	if (tx->sdata->vif.type == NL80211_IFTYPE_AP ||
	    tx->sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		if (!tx->sdata->bss)
			return TX_CONTINUE;

		ps = &tx->sdata->bss->ps;
	} else if (ieee80211_vif_is_mesh(&tx->sdata->vif)) {
		ps = &tx->sdata->u.mesh.ps;
	} else {
		return TX_CONTINUE;
	}


	/* no buffering for ordered frames */
	if (ieee80211_has_order(hdr->frame_control))
		return TX_CONTINUE;

	if (ieee80211_is_probe_req(hdr->frame_control))
		return TX_CONTINUE;

	if (ieee80211_hw_check(&tx->local->hw, QUEUE_CONTROL))
		info->hw_queue = tx->sdata->vif.cab_queue;

	/* no stations in PS mode and no buffered packets */
	if (!atomic_read(&ps->num_sta_ps) && skb_queue_empty(&ps->bc_buf))
		return TX_CONTINUE;

	info->flags |= IEEE80211_TX_CTL_SEND_AFTER_DTIM;

	/* device releases frame after DTIM beacon */
	if (!ieee80211_hw_check(&tx->local->hw, HOST_BROADCAST_PS_BUFFERING))
		return TX_CONTINUE;

	/* buffered in mac80211 */
	if (tx->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
		purge_old_ps_buffers(tx->local);

	if (skb_queue_len(&ps->bc_buf) >= AP_MAX_BC_BUFFER) {
		ps_dbg(tx->sdata,
		       "BC TX buffer full - dropping the oldest frame\n");
		ieee80211_free_txskb(&tx->local->hw, skb_dequeue(&ps->bc_buf));
	} else
		tx->local->total_ps_buffered++;

	skb_queue_tail(&ps->bc_buf, tx->skb);

	return TX_QUEUED;
}

static int ieee80211_use_mfp(__le16 fc, struct sta_info *sta,
			     struct sk_buff *skb)
{
	if (!ieee80211_is_mgmt(fc))
		return 0;

	if (sta == NULL || !test_sta_flag(sta, WLAN_STA_MFP))
		return 0;

	if (!ieee80211_is_robust_mgmt_frame(skb))
		return 0;

	return 1;
}

static ieee80211_tx_result
ieee80211_tx_h_unicast_ps_buf(struct ieee80211_tx_data *tx)
{
	struct sta_info *sta = tx->sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	struct ieee80211_local *local = tx->local;

	if (unlikely(!sta))
		return TX_CONTINUE;

	if (unlikely((test_sta_flag(sta, WLAN_STA_PS_STA) ||
		      test_sta_flag(sta, WLAN_STA_PS_DRIVER) ||
		      test_sta_flag(sta, WLAN_STA_PS_DELIVER)) &&
		     !(info->flags & IEEE80211_TX_CTL_NO_PS_BUFFER))) {
		int ac = skb_get_queue_mapping(tx->skb);

		if (ieee80211_is_mgmt(hdr->frame_control) &&
		    !ieee80211_is_bufferable_mmpdu(hdr->frame_control)) {
			info->flags |= IEEE80211_TX_CTL_NO_PS_BUFFER;
			return TX_CONTINUE;
		}

		ps_dbg(sta->sdata, "STA %pM aid %d: PS buffer for AC %d\n",
		       sta->sta.addr, sta->sta.aid, ac);
		if (tx->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
			purge_old_ps_buffers(tx->local);

		/* sync with ieee80211_sta_ps_deliver_wakeup */
		spin_lock(&sta->ps_lock);
		/*
		 * STA woke up the meantime and all the frames on ps_tx_buf have
		 * been queued to pending queue. No reordering can happen, go
		 * ahead and Tx the packet.
		 */
		if (!test_sta_flag(sta, WLAN_STA_PS_STA) &&
		    !test_sta_flag(sta, WLAN_STA_PS_DRIVER) &&
		    !test_sta_flag(sta, WLAN_STA_PS_DELIVER)) {
			spin_unlock(&sta->ps_lock);
			return TX_CONTINUE;
		}

		if (skb_queue_len(&sta->ps_tx_buf[ac]) >= STA_MAX_TX_BUFFER) {
			struct sk_buff *old = skb_dequeue(&sta->ps_tx_buf[ac]);
			ps_dbg(tx->sdata,
			       "STA %pM TX buffer for AC %d full - dropping oldest frame\n",
			       sta->sta.addr, ac);
			ieee80211_free_txskb(&local->hw, old);
		} else
			tx->local->total_ps_buffered++;

		info->control.jiffies = jiffies;
		info->control.vif = &tx->sdata->vif;
		info->flags |= IEEE80211_TX_INTFL_NEED_TXPROCESSING;
		info->flags &= ~IEEE80211_TX_TEMPORARY_FLAGS;
		skb_queue_tail(&sta->ps_tx_buf[ac], tx->skb);
		spin_unlock(&sta->ps_lock);

		if (!timer_pending(&local->sta_cleanup))
			mod_timer(&local->sta_cleanup,
				  round_jiffies(jiffies +
						STA_INFO_CLEANUP_INTERVAL));

		/*
		 * We queued up some frames, so the TIM bit might
		 * need to be set, recalculate it.
		 */
		sta_info_recalc_tim(sta);

		return TX_QUEUED;
	} else if (unlikely(test_sta_flag(sta, WLAN_STA_PS_STA))) {
		ps_dbg(tx->sdata,
		       "STA %pM in PS mode, but polling/in SP -> send frame\n",
		       sta->sta.addr);
	}

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
ieee80211_tx_h_check_control_port_protocol(struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);

	if (unlikely(tx->sdata->control_port_protocol == tx->skb->protocol)) {
		if (tx->sdata->control_port_no_encrypt)
			info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
		info->control.flags |= IEEE80211_TX_CTRL_PORT_CTRL_PROTO;
		info->flags |= IEEE80211_TX_CTL_USE_MINRATE;
	}

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_select_key(struct ieee80211_tx_data *tx)
{
	struct ieee80211_key *key;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;

	if (unlikely(info->flags & IEEE80211_TX_INTFL_DONT_ENCRYPT))
		tx->key = NULL;
	else if (tx->sta &&
		 (key = rcu_dereference(tx->sta->ptk[tx->sta->ptk_idx])))
		tx->key = key;
	else if (ieee80211_is_group_privacy_action(tx->skb) &&
		(key = rcu_dereference(tx->sdata->default_multicast_key)))
		tx->key = key;
	else if (ieee80211_is_mgmt(hdr->frame_control) &&
		 is_multicast_ether_addr(hdr->addr1) &&
		 ieee80211_is_robust_mgmt_frame(tx->skb) &&
		 (key = rcu_dereference(tx->sdata->default_mgmt_key)))
		tx->key = key;
	else if (is_multicast_ether_addr(hdr->addr1) &&
		 (key = rcu_dereference(tx->sdata->default_multicast_key)))
		tx->key = key;
	else if (!is_multicast_ether_addr(hdr->addr1) &&
		 (key = rcu_dereference(tx->sdata->default_unicast_key)))
		tx->key = key;
	else
		tx->key = NULL;

	if (tx->key) {
		bool skip_hw = false;

		/* TODO: add threshold stuff again */

		switch (tx->key->conf.cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
		case WLAN_CIPHER_SUITE_TKIP:
			if (!ieee80211_is_data_present(hdr->frame_control))
				tx->key = NULL;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
			if (!ieee80211_is_data_present(hdr->frame_control) &&
			    !ieee80211_use_mfp(hdr->frame_control, tx->sta,
					       tx->skb) &&
			    !ieee80211_is_group_privacy_action(tx->skb))
				tx->key = NULL;
			else
				skip_hw = (tx->key->conf.flags &
					   IEEE80211_KEY_FLAG_SW_MGMT_TX) &&
					ieee80211_is_mgmt(hdr->frame_control);
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
		case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		case WLAN_CIPHER_SUITE_BIP_GMAC_256:
			if (!ieee80211_is_mgmt(hdr->frame_control))
				tx->key = NULL;
			break;
		}

		if (unlikely(tx->key && tx->key->flags & KEY_FLAG_TAINTED &&
			     !ieee80211_is_deauth(hdr->frame_control)))
			return TX_DROP;

		if (!skip_hw && tx->key &&
		    tx->key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE)
			info->control.hw_key = &tx->key->conf;
	}

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_rate_ctrl(struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (void *)tx->skb->data;
	struct ieee80211_supported_band *sband;
	u32 len;
	struct ieee80211_tx_rate_control txrc;
	struct ieee80211_sta_rates *ratetbl = NULL;
	bool assoc = false;

	memset(&txrc, 0, sizeof(txrc));

	sband = tx->local->hw.wiphy->bands[info->band];

	len = min_t(u32, tx->skb->len + FCS_LEN,
			 tx->local->hw.wiphy->frag_threshold);

	/* set up the tx rate control struct we give the RC algo */
	txrc.hw = &tx->local->hw;
	txrc.sband = sband;
	txrc.bss_conf = &tx->sdata->vif.bss_conf;
	txrc.skb = tx->skb;
	txrc.reported_rate.idx = -1;
	txrc.rate_idx_mask = tx->sdata->rc_rateidx_mask[info->band];

	if (tx->sdata->rc_has_mcs_mask[info->band])
		txrc.rate_idx_mcs_mask =
			tx->sdata->rc_rateidx_mcs_mask[info->band];

	txrc.bss = (tx->sdata->vif.type == NL80211_IFTYPE_AP ||
		    tx->sdata->vif.type == NL80211_IFTYPE_MESH_POINT ||
		    tx->sdata->vif.type == NL80211_IFTYPE_ADHOC ||
		    tx->sdata->vif.type == NL80211_IFTYPE_OCB);

	/* set up RTS protection if desired */
	if (len > tx->local->hw.wiphy->rts_threshold) {
		txrc.rts = true;
	}

	info->control.use_rts = txrc.rts;
	info->control.use_cts_prot = tx->sdata->vif.bss_conf.use_cts_prot;

	/*
	 * Use short preamble if the BSS can handle it, but not for
	 * management frames unless we know the receiver can handle
	 * that -- the management frame might be to a station that
	 * just wants a probe response.
	 */
	if (tx->sdata->vif.bss_conf.use_short_preamble &&
	    (ieee80211_is_data(hdr->frame_control) ||
	     (tx->sta && test_sta_flag(tx->sta, WLAN_STA_SHORT_PREAMBLE))))
		txrc.short_preamble = true;

	info->control.short_preamble = txrc.short_preamble;

	/* don't ask rate control when rate already injected via radiotap */
	if (info->control.flags & IEEE80211_TX_CTRL_RATE_INJECT)
		return TX_CONTINUE;

	if (tx->sta)
		assoc = test_sta_flag(tx->sta, WLAN_STA_ASSOC);

	/*
	 * Lets not bother rate control if we're associated and cannot
	 * talk to the sta. This should not happen.
	 */
	if (WARN(test_bit(SCAN_SW_SCANNING, &tx->local->scanning) && assoc &&
		 !rate_usable_index_exists(sband, &tx->sta->sta),
		 "%s: Dropped data frame as no usable bitrate found while "
		 "scanning and associated. Target station: "
		 "%pM on %d GHz band\n",
		 tx->sdata->name, hdr->addr1,
		 info->band ? 5 : 2))
		return TX_DROP;

	/*
	 * If we're associated with the sta at this point we know we can at
	 * least send the frame at the lowest bit rate.
	 */
	rate_control_get_rate(tx->sdata, tx->sta, &txrc);

	if (tx->sta && !info->control.skip_table)
		ratetbl = rcu_dereference(tx->sta->sta.rates);

	if (unlikely(info->control.rates[0].idx < 0)) {
		if (ratetbl) {
			struct ieee80211_tx_rate rate = {
				.idx = ratetbl->rate[0].idx,
				.flags = ratetbl->rate[0].flags,
				.count = ratetbl->rate[0].count
			};

			if (ratetbl->rate[0].idx < 0)
				return TX_DROP;

			tx->rate = rate;
		} else {
			return TX_DROP;
		}
	} else {
		tx->rate = info->control.rates[0];
	}

	if (txrc.reported_rate.idx < 0) {
		txrc.reported_rate = tx->rate;
		if (tx->sta && ieee80211_is_data(hdr->frame_control))
			tx->sta->tx_stats.last_rate = txrc.reported_rate;
	} else if (tx->sta)
		tx->sta->tx_stats.last_rate = txrc.reported_rate;

	if (ratetbl)
		return TX_CONTINUE;

	if (unlikely(!info->control.rates[0].count))
		info->control.rates[0].count = 1;

	if (WARN_ON_ONCE((info->control.rates[0].count > 1) &&
			 (info->flags & IEEE80211_TX_CTL_NO_ACK)))
		info->control.rates[0].count = 1;

	return TX_CONTINUE;
}

static __le16 ieee80211_tx_next_seq(struct sta_info *sta, int tid)
{
	u16 *seq = &sta->tid_seq[tid];
	__le16 ret = cpu_to_le16(*seq);

	/* Increase the sequence number. */
	*seq = (*seq + 0x10) & IEEE80211_SCTL_SEQ;

	return ret;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_sequence(struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
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

	if (ieee80211_is_qos_nullfunc(hdr->frame_control))
		return TX_CONTINUE;

	/*
	 * Anything but QoS data that has a sequence number field
	 * (is long enough) gets a sequence number from the global
	 * counter.  QoS data frames with a multicast destination
	 * also use the global counter (802.11-2012 9.3.2.10).
	 */
	if (!ieee80211_is_data_qos(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1)) {
		if (tx->flags & IEEE80211_TX_NO_SEQNO)
			return TX_CONTINUE;
		/* driver should assign sequence number */
		info->flags |= IEEE80211_TX_CTL_ASSIGN_SEQ;
		/* for pure STA mode without beacons, we can do it */
		hdr->seq_ctrl = cpu_to_le16(tx->sdata->sequence_number);
		tx->sdata->sequence_number += 0x10;
		if (tx->sta)
			tx->sta->tx_stats.msdu[IEEE80211_NUM_TIDS]++;
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
	tid = ieee80211_get_tid(hdr);
	tx->sta->tx_stats.msdu[tid]++;

	hdr->seq_ctrl = ieee80211_tx_next_seq(tx->sta, tid);

	return TX_CONTINUE;
}

static int ieee80211_fragment(struct ieee80211_tx_data *tx,
			      struct sk_buff *skb, int hdrlen,
			      int frag_threshold)
{
	struct ieee80211_local *local = tx->local;
	struct ieee80211_tx_info *info;
	struct sk_buff *tmp;
	int per_fragm = frag_threshold - hdrlen - FCS_LEN;
	int pos = hdrlen + per_fragm;
	int rem = skb->len - hdrlen - per_fragm;

	if (WARN_ON(rem < 0))
		return -EINVAL;

	/* first fragment was already added to queue by caller */

	while (rem) {
		int fraglen = per_fragm;

		if (fraglen > rem)
			fraglen = rem;
		rem -= fraglen;
		tmp = dev_alloc_skb(local->tx_headroom +
				    frag_threshold +
				    tx->sdata->encrypt_headroom +
				    IEEE80211_ENCRYPT_TAILROOM);
		if (!tmp)
			return -ENOMEM;

		__skb_queue_tail(&tx->skbs, tmp);

		skb_reserve(tmp,
			    local->tx_headroom + tx->sdata->encrypt_headroom);

		/* copy control information */
		memcpy(tmp->cb, skb->cb, sizeof(tmp->cb));

		info = IEEE80211_SKB_CB(tmp);
		info->flags &= ~(IEEE80211_TX_CTL_CLEAR_PS_FILT |
				 IEEE80211_TX_CTL_FIRST_FRAGMENT);

		if (rem)
			info->flags |= IEEE80211_TX_CTL_MORE_FRAMES;

		skb_copy_queue_mapping(tmp, skb);
		tmp->priority = skb->priority;
		tmp->dev = skb->dev;

		/* copy header and data */
		skb_put_data(tmp, skb->data, hdrlen);
		skb_put_data(tmp, skb->data + pos, fraglen);

		pos += fraglen;
	}

	/* adjust first fragment's length */
	skb_trim(skb, hdrlen + per_fragm);
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

	/* no matter what happens, tx->skb moves to tx->skbs */
	__skb_queue_tail(&tx->skbs, skb);
	tx->skb = NULL;

	if (info->flags & IEEE80211_TX_CTL_DONTFRAG)
		return TX_CONTINUE;

	if (ieee80211_hw_check(&tx->local->hw, SUPPORTS_TX_FRAG))
		return TX_CONTINUE;

	/*
	 * Warn when submitting a fragmented A-MPDU frame and drop it.
	 * This scenario is handled in ieee80211_tx_prepare but extra
	 * caution taken here as fragmented ampdu may cause Tx stop.
	 */
	if (WARN_ON(info->flags & IEEE80211_TX_CTL_AMPDU))
		return TX_DROP;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	/* internal error, why isn't DONTFRAG set? */
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
	if (ieee80211_fragment(tx, skb, hdrlen, frag_threshold))
		return TX_DROP;

	/* update duration/seq/flags of fragments */
	fragnum = 0;

	skb_queue_walk(&tx->skbs, skb) {
		const __le16 morefrags = cpu_to_le16(IEEE80211_FCTL_MOREFRAGS);

		hdr = (void *)skb->data;
		info = IEEE80211_SKB_CB(skb);

		if (!skb_queue_is_last(&tx->skbs, skb)) {
			hdr->frame_control |= morefrags;
			/*
			 * No multi-rate retries for fragmented frames, that
			 * would completely throw off the NAV at other STAs.
			 */
			info->control.rates[1].idx = -1;
			info->control.rates[2].idx = -1;
			info->control.rates[3].idx = -1;
			BUILD_BUG_ON(IEEE80211_TX_MAX_RATES != 4);
			info->flags &= ~IEEE80211_TX_CTL_RATE_CTRL_PROBE;
		} else {
			hdr->frame_control &= ~morefrags;
		}
		hdr->seq_ctrl |= cpu_to_le16(fragnum & IEEE80211_SCTL_FRAG);
		fragnum++;
	}

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_stats(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;
	int ac = -1;

	if (!tx->sta)
		return TX_CONTINUE;

	skb_queue_walk(&tx->skbs, skb) {
		ac = skb_get_queue_mapping(skb);
		tx->sta->tx_stats.bytes[ac] += skb->len;
	}
	if (ac >= 0)
		tx->sta->tx_stats.packets[ac]++;

	return TX_CONTINUE;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_encrypt(struct ieee80211_tx_data *tx)
{
	if (!tx->key)
		return TX_CONTINUE;

	switch (tx->key->conf.cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		return ieee80211_crypto_wep_encrypt(tx);
	case WLAN_CIPHER_SUITE_TKIP:
		return ieee80211_crypto_tkip_encrypt(tx);
	case WLAN_CIPHER_SUITE_CCMP:
		return ieee80211_crypto_ccmp_encrypt(
			tx, IEEE80211_CCMP_MIC_LEN);
	case WLAN_CIPHER_SUITE_CCMP_256:
		return ieee80211_crypto_ccmp_encrypt(
			tx, IEEE80211_CCMP_256_MIC_LEN);
	case WLAN_CIPHER_SUITE_AES_CMAC:
		return ieee80211_crypto_aes_cmac_encrypt(tx);
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		return ieee80211_crypto_aes_cmac_256_encrypt(tx);
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		return ieee80211_crypto_aes_gmac_encrypt(tx);
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		return ieee80211_crypto_gcmp_encrypt(tx);
	default:
		return ieee80211_crypto_hw_encrypt(tx);
	}

	return TX_DROP;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_calculate_duration(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	int next_len;
	bool group_addr;

	skb_queue_walk(&tx->skbs, skb) {
		hdr = (void *) skb->data;
		if (unlikely(ieee80211_is_pspoll(hdr->frame_control)))
			break; /* must not overwrite AID */
		if (!skb_queue_is_last(&tx->skbs, skb)) {
			struct sk_buff *next = skb_queue_next(&tx->skbs, skb);
			next_len = next->len;
		} else
			next_len = 0;
		group_addr = is_multicast_ether_addr(hdr->addr1);

		hdr->duration_id =
			ieee80211_duration(tx, skb, group_addr, next_len);
	}

	return TX_CONTINUE;
}

/* actual transmit path */

static bool ieee80211_tx_prep_agg(struct ieee80211_tx_data *tx,
				  struct sk_buff *skb,
				  struct ieee80211_tx_info *info,
				  struct tid_ampdu_tx *tid_tx,
				  int tid)
{
	bool queued = false;
	bool reset_agg_timer = false;
	struct sk_buff *purge_skb = NULL;

	if (test_bit(HT_AGG_STATE_OPERATIONAL, &tid_tx->state)) {
		info->flags |= IEEE80211_TX_CTL_AMPDU;
		reset_agg_timer = true;
	} else if (test_bit(HT_AGG_STATE_WANT_START, &tid_tx->state)) {
		/*
		 * nothing -- this aggregation session is being started
		 * but that might still fail with the driver
		 */
	} else if (!tx->sta->sta.txq[tid]) {
		spin_lock(&tx->sta->lock);
		/*
		 * Need to re-check now, because we may get here
		 *
		 *  1) in the window during which the setup is actually
		 *     already done, but not marked yet because not all
		 *     packets are spliced over to the driver pending
		 *     queue yet -- if this happened we acquire the lock
		 *     either before or after the splice happens, but
		 *     need to recheck which of these cases happened.
		 *
		 *  2) during session teardown, if the OPERATIONAL bit
		 *     was cleared due to the teardown but the pointer
		 *     hasn't been assigned NULL yet (or we loaded it
		 *     before it was assigned) -- in this case it may
		 *     now be NULL which means we should just let the
		 *     packet pass through because splicing the frames
		 *     back is already done.
		 */
		tid_tx = rcu_dereference_protected_tid_tx(tx->sta, tid);

		if (!tid_tx) {
			/* do nothing, let packet pass through */
		} else if (test_bit(HT_AGG_STATE_OPERATIONAL, &tid_tx->state)) {
			info->flags |= IEEE80211_TX_CTL_AMPDU;
			reset_agg_timer = true;
		} else {
			queued = true;
			if (info->flags & IEEE80211_TX_CTL_NO_PS_BUFFER) {
				clear_sta_flag(tx->sta, WLAN_STA_SP);
				ps_dbg(tx->sta->sdata,
				       "STA %pM aid %d: SP frame queued, close the SP w/o telling the peer\n",
				       tx->sta->sta.addr, tx->sta->sta.aid);
			}
			info->control.vif = &tx->sdata->vif;
			info->flags |= IEEE80211_TX_INTFL_NEED_TXPROCESSING;
			info->flags &= ~IEEE80211_TX_TEMPORARY_FLAGS;
			__skb_queue_tail(&tid_tx->pending, skb);
			if (skb_queue_len(&tid_tx->pending) > STA_MAX_TX_BUFFER)
				purge_skb = __skb_dequeue(&tid_tx->pending);
		}
		spin_unlock(&tx->sta->lock);

		if (purge_skb)
			ieee80211_free_txskb(&tx->local->hw, purge_skb);
	}

	/* reset session timer */
	if (reset_agg_timer)
		tid_tx->last_tx = jiffies;

	return queued;
}

/*
 * initialises @tx
 * pass %NULL for the station if unknown, a valid pointer if known
 * or an ERR_PTR() if the station is known not to exist
 */
static ieee80211_tx_result
ieee80211_tx_prepare(struct ieee80211_sub_if_data *sdata,
		     struct ieee80211_tx_data *tx,
		     struct sta_info *sta, struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int tid;

	memset(tx, 0, sizeof(*tx));
	tx->skb = skb;
	tx->local = local;
	tx->sdata = sdata;
	__skb_queue_head_init(&tx->skbs);

	/*
	 * If this flag is set to true anywhere, and we get here,
	 * we are doing the needed processing, so remove the flag
	 * now.
	 */
	info->flags &= ~IEEE80211_TX_INTFL_NEED_TXPROCESSING;

	hdr = (struct ieee80211_hdr *) skb->data;

	if (likely(sta)) {
		if (!IS_ERR(sta))
			tx->sta = sta;
	} else {
		if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
			tx->sta = rcu_dereference(sdata->u.vlan.sta);
			if (!tx->sta && sdata->wdev.use_4addr)
				return TX_DROP;
		} else if (info->flags & (IEEE80211_TX_INTFL_NL80211_FRAME_TX |
					  IEEE80211_TX_CTL_INJECTED) ||
			   tx->sdata->control_port_protocol == tx->skb->protocol) {
			tx->sta = sta_info_get_bss(sdata, hdr->addr1);
		}
		if (!tx->sta && !is_multicast_ether_addr(hdr->addr1))
			tx->sta = sta_info_get(sdata, hdr->addr1);
	}

	if (tx->sta && ieee80211_is_data_qos(hdr->frame_control) &&
	    !ieee80211_is_qos_nullfunc(hdr->frame_control) &&
	    ieee80211_hw_check(&local->hw, AMPDU_AGGREGATION) &&
	    !ieee80211_hw_check(&local->hw, TX_AMPDU_SETUP_IN_HW)) {
		struct tid_ampdu_tx *tid_tx;

		tid = ieee80211_get_tid(hdr);

		tid_tx = rcu_dereference(tx->sta->ampdu_mlme.tid_tx[tid]);
		if (tid_tx) {
			bool queued;

			queued = ieee80211_tx_prep_agg(tx, skb, info,
						       tid_tx, tid);

			if (unlikely(queued))
				return TX_QUEUED;
		}
	}

	if (is_multicast_ether_addr(hdr->addr1)) {
		tx->flags &= ~IEEE80211_TX_UNICAST;
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	} else
		tx->flags |= IEEE80211_TX_UNICAST;

	if (!(info->flags & IEEE80211_TX_CTL_DONTFRAG)) {
		if (!(tx->flags & IEEE80211_TX_UNICAST) ||
		    skb->len + FCS_LEN <= local->hw.wiphy->frag_threshold ||
		    info->flags & IEEE80211_TX_CTL_AMPDU)
			info->flags |= IEEE80211_TX_CTL_DONTFRAG;
	}

	if (!tx->sta)
		info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT;
	else if (test_and_clear_sta_flag(tx->sta, WLAN_STA_CLEAR_PS_FILT)) {
		info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT;
		ieee80211_check_fast_xmit(tx->sta);
	}

	info->flags |= IEEE80211_TX_CTL_FIRST_FRAGMENT;

	return TX_CONTINUE;
}

static struct txq_info *ieee80211_get_txq(struct ieee80211_local *local,
					  struct ieee80211_vif *vif,
					  struct sta_info *sta,
					  struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_txq *txq = NULL;

	if ((info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM) ||
	    (info->control.flags & IEEE80211_TX_CTRL_PS_RESPONSE))
		return NULL;

	if (!ieee80211_is_data_present(hdr->frame_control))
		return NULL;

	if (sta) {
		u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;

		if (!sta->uploaded)
			return NULL;

		txq = sta->sta.txq[tid];
	} else if (vif) {
		txq = vif->txq;
	}

	if (!txq)
		return NULL;

	return to_txq_info(txq);
}

static void ieee80211_set_skb_enqueue_time(struct sk_buff *skb)
{
	IEEE80211_SKB_CB(skb)->control.enqueue_time = codel_get_time();
}

static u32 codel_skb_len_func(const struct sk_buff *skb)
{
	return skb->len;
}

static codel_time_t codel_skb_time_func(const struct sk_buff *skb)
{
	const struct ieee80211_tx_info *info;

	info = (const struct ieee80211_tx_info *)skb->cb;
	return info->control.enqueue_time;
}

static struct sk_buff *codel_dequeue_func(struct codel_vars *cvars,
					  void *ctx)
{
	struct ieee80211_local *local;
	struct txq_info *txqi;
	struct fq *fq;
	struct fq_flow *flow;

	txqi = ctx;
	local = vif_to_sdata(txqi->txq.vif)->local;
	fq = &local->fq;

	if (cvars == &txqi->def_cvars)
		flow = &txqi->def_flow;
	else
		flow = &fq->flows[cvars - local->cvars];

	return fq_flow_dequeue(fq, flow);
}

static void codel_drop_func(struct sk_buff *skb,
			    void *ctx)
{
	struct ieee80211_local *local;
	struct ieee80211_hw *hw;
	struct txq_info *txqi;

	txqi = ctx;
	local = vif_to_sdata(txqi->txq.vif)->local;
	hw = &local->hw;

	ieee80211_free_txskb(hw, skb);
}

static struct sk_buff *fq_tin_dequeue_func(struct fq *fq,
					   struct fq_tin *tin,
					   struct fq_flow *flow)
{
	struct ieee80211_local *local;
	struct txq_info *txqi;
	struct codel_vars *cvars;
	struct codel_params *cparams;
	struct codel_stats *cstats;

	local = container_of(fq, struct ieee80211_local, fq);
	txqi = container_of(tin, struct txq_info, tin);
	cstats = &txqi->cstats;

	if (txqi->txq.sta) {
		struct sta_info *sta = container_of(txqi->txq.sta,
						    struct sta_info, sta);
		cparams = &sta->cparams;
	} else {
		cparams = &local->cparams;
	}

	if (flow == &txqi->def_flow)
		cvars = &txqi->def_cvars;
	else
		cvars = &local->cvars[flow - fq->flows];

	return codel_dequeue(txqi,
			     &flow->backlog,
			     cparams,
			     cvars,
			     cstats,
			     codel_skb_len_func,
			     codel_skb_time_func,
			     codel_drop_func,
			     codel_dequeue_func);
}

static void fq_skb_free_func(struct fq *fq,
			     struct fq_tin *tin,
			     struct fq_flow *flow,
			     struct sk_buff *skb)
{
	struct ieee80211_local *local;

	local = container_of(fq, struct ieee80211_local, fq);
	ieee80211_free_txskb(&local->hw, skb);
}

static struct fq_flow *fq_flow_get_default_func(struct fq *fq,
						struct fq_tin *tin,
						int idx,
						struct sk_buff *skb)
{
	struct txq_info *txqi;

	txqi = container_of(tin, struct txq_info, tin);
	return &txqi->def_flow;
}

static void ieee80211_txq_enqueue(struct ieee80211_local *local,
				  struct txq_info *txqi,
				  struct sk_buff *skb)
{
	struct fq *fq = &local->fq;
	struct fq_tin *tin = &txqi->tin;

	ieee80211_set_skb_enqueue_time(skb);
	fq_tin_enqueue(fq, tin, skb,
		       fq_skb_free_func,
		       fq_flow_get_default_func);
}

static bool fq_vlan_filter_func(struct fq *fq, struct fq_tin *tin,
				struct fq_flow *flow, struct sk_buff *skb,
				void *data)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	return info->control.vif == data;
}

void ieee80211_txq_remove_vlan(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata)
{
	struct fq *fq = &local->fq;
	struct txq_info *txqi;
	struct fq_tin *tin;
	struct ieee80211_sub_if_data *ap;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_AP_VLAN))
		return;

	ap = container_of(sdata->bss, struct ieee80211_sub_if_data, u.ap);

	if (!ap->vif.txq)
		return;

	txqi = to_txq_info(ap->vif.txq);
	tin = &txqi->tin;

	spin_lock_bh(&fq->lock);
	fq_tin_filter(fq, tin, fq_vlan_filter_func, &sdata->vif,
		      fq_skb_free_func);
	spin_unlock_bh(&fq->lock);
}

void ieee80211_txq_init(struct ieee80211_sub_if_data *sdata,
			struct sta_info *sta,
			struct txq_info *txqi, int tid)
{
	fq_tin_init(&txqi->tin);
	fq_flow_init(&txqi->def_flow);
	codel_vars_init(&txqi->def_cvars);
	codel_stats_init(&txqi->cstats);
	__skb_queue_head_init(&txqi->frags);

	txqi->txq.vif = &sdata->vif;

	if (sta) {
		txqi->txq.sta = &sta->sta;
		sta->sta.txq[tid] = &txqi->txq;
		txqi->txq.tid = tid;
		txqi->txq.ac = ieee80211_ac_from_tid(tid);
	} else {
		sdata->vif.txq = &txqi->txq;
		txqi->txq.tid = 0;
		txqi->txq.ac = IEEE80211_AC_BE;
	}
}

void ieee80211_txq_purge(struct ieee80211_local *local,
			 struct txq_info *txqi)
{
	struct fq *fq = &local->fq;
	struct fq_tin *tin = &txqi->tin;

	fq_tin_reset(fq, tin, fq_skb_free_func);
	ieee80211_purge_tx_queue(&local->hw, &txqi->frags);
}

void ieee80211_txq_set_params(struct ieee80211_local *local)
{
	if (local->hw.wiphy->txq_limit)
		local->fq.limit = local->hw.wiphy->txq_limit;
	else
		local->hw.wiphy->txq_limit = local->fq.limit;

	if (local->hw.wiphy->txq_memory_limit)
		local->fq.memory_limit = local->hw.wiphy->txq_memory_limit;
	else
		local->hw.wiphy->txq_memory_limit = local->fq.memory_limit;

	if (local->hw.wiphy->txq_quantum)
		local->fq.quantum = local->hw.wiphy->txq_quantum;
	else
		local->hw.wiphy->txq_quantum = local->fq.quantum;
}

int ieee80211_txq_setup_flows(struct ieee80211_local *local)
{
	struct fq *fq = &local->fq;
	int ret;
	int i;
	bool supp_vht = false;
	enum nl80211_band band;

	if (!local->ops->wake_tx_queue)
		return 0;

	ret = fq_init(fq, 4096);
	if (ret)
		return ret;

	/*
	 * If the hardware doesn't support VHT, it is safe to limit the maximum
	 * queue size. 4 Mbytes is 64 max-size aggregates in 802.11n.
	 */
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		struct ieee80211_supported_band *sband;

		sband = local->hw.wiphy->bands[band];
		if (!sband)
			continue;

		supp_vht = supp_vht || sband->vht_cap.vht_supported;
	}

	if (!supp_vht)
		fq->memory_limit = 4 << 20; /* 4 Mbytes */

	codel_params_init(&local->cparams);
	local->cparams.interval = MS2TIME(100);
	local->cparams.target = MS2TIME(20);
	local->cparams.ecn = true;

	local->cvars = kcalloc(fq->flows_cnt, sizeof(local->cvars[0]),
			       GFP_KERNEL);
	if (!local->cvars) {
		spin_lock_bh(&fq->lock);
		fq_reset(fq, fq_skb_free_func);
		spin_unlock_bh(&fq->lock);
		return -ENOMEM;
	}

	for (i = 0; i < fq->flows_cnt; i++)
		codel_vars_init(&local->cvars[i]);

	ieee80211_txq_set_params(local);

	return 0;
}

void ieee80211_txq_teardown_flows(struct ieee80211_local *local)
{
	struct fq *fq = &local->fq;

	if (!local->ops->wake_tx_queue)
		return;

	kfree(local->cvars);
	local->cvars = NULL;

	spin_lock_bh(&fq->lock);
	fq_reset(fq, fq_skb_free_func);
	spin_unlock_bh(&fq->lock);
}

static bool ieee80211_queue_skb(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata,
				struct sta_info *sta,
				struct sk_buff *skb)
{
	struct fq *fq = &local->fq;
	struct ieee80211_vif *vif;
	struct txq_info *txqi;

	if (!local->ops->wake_tx_queue ||
	    sdata->vif.type == NL80211_IFTYPE_MONITOR)
		return false;

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		sdata = container_of(sdata->bss,
				     struct ieee80211_sub_if_data, u.ap);

	vif = &sdata->vif;
	txqi = ieee80211_get_txq(local, vif, sta, skb);

	if (!txqi)
		return false;

	spin_lock_bh(&fq->lock);
	ieee80211_txq_enqueue(local, txqi, skb);
	spin_unlock_bh(&fq->lock);

	drv_wake_tx_queue(local, txqi);

	return true;
}

static bool ieee80211_tx_frags(struct ieee80211_local *local,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct sk_buff_head *skbs,
			       bool txpending)
{
	struct ieee80211_tx_control control = {};
	struct sk_buff *skb, *tmp;
	unsigned long flags;

	skb_queue_walk_safe(skbs, skb, tmp) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		int q = info->hw_queue;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (WARN_ON_ONCE(q >= local->hw.queues)) {
			__skb_unlink(skb, skbs);
			ieee80211_free_txskb(&local->hw, skb);
			continue;
		}
#endif

		spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
		if (local->queue_stop_reasons[q] ||
		    (!txpending && !skb_queue_empty(&local->pending[q]))) {
			if (unlikely(info->flags &
				     IEEE80211_TX_INTFL_OFFCHAN_TX_OK)) {
				if (local->queue_stop_reasons[q] &
				    ~BIT(IEEE80211_QUEUE_STOP_REASON_OFFCHANNEL)) {
					/*
					 * Drop off-channel frames if queues
					 * are stopped for any reason other
					 * than off-channel operation. Never
					 * queue them.
					 */
					spin_unlock_irqrestore(
						&local->queue_stop_reason_lock,
						flags);
					ieee80211_purge_tx_queue(&local->hw,
								 skbs);
					return true;
				}
			} else {

				/*
				 * Since queue is stopped, queue up frames for
				 * later transmission from the tx-pending
				 * tasklet when the queue is woken again.
				 */
				if (txpending)
					skb_queue_splice_init(skbs,
							      &local->pending[q]);
				else
					skb_queue_splice_tail_init(skbs,
								   &local->pending[q]);

				spin_unlock_irqrestore(&local->queue_stop_reason_lock,
						       flags);
				return false;
			}
		}
		spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);

		info->control.vif = vif;
		control.sta = sta;

		__skb_unlink(skb, skbs);
		drv_tx(local, &control, skb);
	}

	return true;
}

/*
 * Returns false if the frame couldn't be transmitted but was queued instead.
 */
static bool __ieee80211_tx(struct ieee80211_local *local,
			   struct sk_buff_head *skbs, int led_len,
			   struct sta_info *sta, bool txpending)
{
	struct ieee80211_tx_info *info;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_vif *vif;
	struct ieee80211_sta *pubsta;
	struct sk_buff *skb;
	bool result = true;
	__le16 fc;

	if (WARN_ON(skb_queue_empty(skbs)))
		return true;

	skb = skb_peek(skbs);
	fc = ((struct ieee80211_hdr *)skb->data)->frame_control;
	info = IEEE80211_SKB_CB(skb);
	sdata = vif_to_sdata(info->control.vif);
	if (sta && !sta->uploaded)
		sta = NULL;

	if (sta)
		pubsta = &sta->sta;
	else
		pubsta = NULL;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_MONITOR:
		if (sdata->u.mntr.flags & MONITOR_FLAG_ACTIVE) {
			vif = &sdata->vif;
			break;
		}
		sdata = rcu_dereference(local->monitor_sdata);
		if (sdata) {
			vif = &sdata->vif;
			info->hw_queue =
				vif->hw_queue[skb_get_queue_mapping(skb)];
		} else if (ieee80211_hw_check(&local->hw, QUEUE_CONTROL)) {
			ieee80211_purge_tx_queue(&local->hw, skbs);
			return true;
		} else
			vif = NULL;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		sdata = container_of(sdata->bss,
				     struct ieee80211_sub_if_data, u.ap);
		/* fall through */
	default:
		vif = &sdata->vif;
		break;
	}

	result = ieee80211_tx_frags(local, vif, pubsta, skbs,
				    txpending);

	ieee80211_tpt_led_trig_tx(local, fc, led_len);

	WARN_ON_ONCE(!skb_queue_empty(skbs));

	return result;
}

/*
 * Invoke TX handlers, return 0 on success and non-zero if the
 * frame was dropped or queued.
 *
 * The handlers are split into an early and late part. The latter is everything
 * that can be sensitive to reordering, and will be deferred to after packets
 * are dequeued from the intermediate queues (when they are enabled).
 */
static int invoke_tx_handlers_early(struct ieee80211_tx_data *tx)
{
	ieee80211_tx_result res = TX_DROP;

#define CALL_TXH(txh) \
	do {				\
		res = txh(tx);		\
		if (res != TX_CONTINUE)	\
			goto txh_done;	\
	} while (0)

	CALL_TXH(ieee80211_tx_h_dynamic_ps);
	CALL_TXH(ieee80211_tx_h_check_assoc);
	CALL_TXH(ieee80211_tx_h_ps_buf);
	CALL_TXH(ieee80211_tx_h_check_control_port_protocol);
	CALL_TXH(ieee80211_tx_h_select_key);
	if (!ieee80211_hw_check(&tx->local->hw, HAS_RATE_CONTROL))
		CALL_TXH(ieee80211_tx_h_rate_ctrl);

 txh_done:
	if (unlikely(res == TX_DROP)) {
		I802_DEBUG_INC(tx->local->tx_handlers_drop);
		if (tx->skb)
			ieee80211_free_txskb(&tx->local->hw, tx->skb);
		else
			ieee80211_purge_tx_queue(&tx->local->hw, &tx->skbs);
		return -1;
	} else if (unlikely(res == TX_QUEUED)) {
		I802_DEBUG_INC(tx->local->tx_handlers_queued);
		return -1;
	}

	return 0;
}

/*
 * Late handlers can be called while the sta lock is held. Handlers that can
 * cause packets to be generated will cause deadlock!
 */
static int invoke_tx_handlers_late(struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	ieee80211_tx_result res = TX_CONTINUE;

	if (unlikely(info->flags & IEEE80211_TX_INTFL_RETRANSMISSION)) {
		__skb_queue_tail(&tx->skbs, tx->skb);
		tx->skb = NULL;
		goto txh_done;
	}

	CALL_TXH(ieee80211_tx_h_michael_mic_add);
	CALL_TXH(ieee80211_tx_h_sequence);
	CALL_TXH(ieee80211_tx_h_fragment);
	/* handlers after fragment must be aware of tx info fragmentation! */
	CALL_TXH(ieee80211_tx_h_stats);
	CALL_TXH(ieee80211_tx_h_encrypt);
	if (!ieee80211_hw_check(&tx->local->hw, HAS_RATE_CONTROL))
		CALL_TXH(ieee80211_tx_h_calculate_duration);
#undef CALL_TXH

 txh_done:
	if (unlikely(res == TX_DROP)) {
		I802_DEBUG_INC(tx->local->tx_handlers_drop);
		if (tx->skb)
			ieee80211_free_txskb(&tx->local->hw, tx->skb);
		else
			ieee80211_purge_tx_queue(&tx->local->hw, &tx->skbs);
		return -1;
	} else if (unlikely(res == TX_QUEUED)) {
		I802_DEBUG_INC(tx->local->tx_handlers_queued);
		return -1;
	}

	return 0;
}

static int invoke_tx_handlers(struct ieee80211_tx_data *tx)
{
	int r = invoke_tx_handlers_early(tx);

	if (r)
		return r;
	return invoke_tx_handlers_late(tx);
}

bool ieee80211_tx_prepare_skb(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif, struct sk_buff *skb,
			      int band, struct ieee80211_sta **sta)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_data tx;
	struct sk_buff *skb2;

	if (ieee80211_tx_prepare(sdata, &tx, NULL, skb) == TX_DROP)
		return false;

	info->band = band;
	info->control.vif = vif;
	info->hw_queue = vif->hw_queue[skb_get_queue_mapping(skb)];

	if (invoke_tx_handlers(&tx))
		return false;

	if (sta) {
		if (tx.sta)
			*sta = &tx.sta->sta;
		else
			*sta = NULL;
	}

	/* this function isn't suitable for fragmented data frames */
	skb2 = __skb_dequeue(&tx.skbs);
	if (WARN_ON(skb2 != skb || !skb_queue_empty(&tx.skbs))) {
		ieee80211_free_txskb(hw, skb2);
		ieee80211_purge_tx_queue(hw, &tx.skbs);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(ieee80211_tx_prepare_skb);

/*
 * Returns false if the frame couldn't be transmitted but was queued instead.
 */
static bool ieee80211_tx(struct ieee80211_sub_if_data *sdata,
			 struct sta_info *sta, struct sk_buff *skb,
			 bool txpending, u32 txdata_flags)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_data tx;
	ieee80211_tx_result res_prepare;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	bool result = true;
	int led_len;

	if (unlikely(skb->len < 10)) {
		dev_kfree_skb(skb);
		return true;
	}

	/* initialises tx */
	led_len = skb->len;
	res_prepare = ieee80211_tx_prepare(sdata, &tx, sta, skb);

	tx.flags |= txdata_flags;

	if (unlikely(res_prepare == TX_DROP)) {
		ieee80211_free_txskb(&local->hw, skb);
		return true;
	} else if (unlikely(res_prepare == TX_QUEUED)) {
		return true;
	}

	/* set up hw_queue value early */
	if (!(info->flags & IEEE80211_TX_CTL_TX_OFFCHAN) ||
	    !ieee80211_hw_check(&local->hw, QUEUE_CONTROL))
		info->hw_queue =
			sdata->vif.hw_queue[skb_get_queue_mapping(skb)];

	if (invoke_tx_handlers_early(&tx))
		return true;

	if (ieee80211_queue_skb(local, sdata, tx.sta, tx.skb))
		return true;

	if (!invoke_tx_handlers_late(&tx))
		result = __ieee80211_tx(local, &tx.skbs, led_len,
					tx.sta, txpending);

	return result;
}

/* device xmit handlers */

enum ieee80211_encrypt {
	ENCRYPT_NO,
	ENCRYPT_MGMT,
	ENCRYPT_DATA,
};

static int ieee80211_skb_resize(struct ieee80211_sub_if_data *sdata,
				struct sk_buff *skb,
				int head_need,
				enum ieee80211_encrypt encrypt)
{
	struct ieee80211_local *local = sdata->local;
	bool enc_tailroom;
	int tail_need = 0;

	enc_tailroom = encrypt == ENCRYPT_MGMT ||
		       (encrypt == ENCRYPT_DATA &&
			sdata->crypto_tx_tailroom_needed_cnt);

	if (enc_tailroom) {
		tail_need = IEEE80211_ENCRYPT_TAILROOM;
		tail_need -= skb_tailroom(skb);
		tail_need = max_t(int, tail_need, 0);
	}

	if (skb_cloned(skb) &&
	    (!ieee80211_hw_check(&local->hw, SUPPORTS_CLONED_SKBS) ||
	     !skb_clone_writable(skb, ETH_HLEN) || enc_tailroom))
		I802_DEBUG_INC(local->tx_expand_skb_head_cloned);
	else if (head_need || tail_need)
		I802_DEBUG_INC(local->tx_expand_skb_head);
	else
		return 0;

	if (pskb_expand_head(skb, head_need, tail_need, GFP_ATOMIC)) {
		wiphy_debug(local->hw.wiphy,
			    "failed to reallocate TX buffer\n");
		return -ENOMEM;
	}

	return 0;
}

void ieee80211_xmit(struct ieee80211_sub_if_data *sdata,
		    struct sta_info *sta, struct sk_buff *skb,
		    u32 txdata_flags)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	int headroom;
	enum ieee80211_encrypt encrypt;

	if (info->flags & IEEE80211_TX_INTFL_DONT_ENCRYPT)
		encrypt = ENCRYPT_NO;
	else if (ieee80211_is_mgmt(hdr->frame_control))
		encrypt = ENCRYPT_MGMT;
	else
		encrypt = ENCRYPT_DATA;

	headroom = local->tx_headroom;
	if (encrypt != ENCRYPT_NO)
		headroom += sdata->encrypt_headroom;
	headroom -= skb_headroom(skb);
	headroom = max_t(int, 0, headroom);

	if (ieee80211_skb_resize(sdata, skb, headroom, encrypt)) {
		ieee80211_free_txskb(&local->hw, skb);
		return;
	}

	/* reload after potential resize */
	hdr = (struct ieee80211_hdr *) skb->data;
	info->control.vif = &sdata->vif;

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		if (ieee80211_is_data(hdr->frame_control) &&
		    is_unicast_ether_addr(hdr->addr1)) {
			if (mesh_nexthop_resolve(sdata, skb))
				return; /* skb queued: don't free */
		} else {
			ieee80211_mps_set_frame_flags(sdata, NULL, hdr);
		}
	}

	ieee80211_set_qos_hdr(sdata, skb);
	ieee80211_tx(sdata, sta, skb, false, txdata_flags);
}

static bool ieee80211_parse_tx_radiotap(struct ieee80211_local *local,
					struct sk_buff *skb)
{
	struct ieee80211_radiotap_iterator iterator;
	struct ieee80211_radiotap_header *rthdr =
		(struct ieee80211_radiotap_header *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_supported_band *sband =
		local->hw.wiphy->bands[info->band];
	int ret = ieee80211_radiotap_iterator_init(&iterator, rthdr, skb->len,
						   NULL);
	u16 txflags;
	u16 rate = 0;
	bool rate_found = false;
	u8 rate_retries = 0;
	u16 rate_flags = 0;
	u8 mcs_known, mcs_flags, mcs_bw;
	u16 vht_known;
	u8 vht_mcs = 0, vht_nss = 0;
	int i;

	info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
		       IEEE80211_TX_CTL_DONTFRAG;

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
				if (skb->len < (iterator._max_length + FCS_LEN))
					return false;

				skb_trim(skb, skb->len - FCS_LEN);
			}
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_WEP)
				info->flags &= ~IEEE80211_TX_INTFL_DONT_ENCRYPT;
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_FRAG)
				info->flags &= ~IEEE80211_TX_CTL_DONTFRAG;
			break;

		case IEEE80211_RADIOTAP_TX_FLAGS:
			txflags = get_unaligned_le16(iterator.this_arg);
			if (txflags & IEEE80211_RADIOTAP_F_TX_NOACK)
				info->flags |= IEEE80211_TX_CTL_NO_ACK;
			break;

		case IEEE80211_RADIOTAP_RATE:
			rate = *iterator.this_arg;
			rate_flags = 0;
			rate_found = true;
			break;

		case IEEE80211_RADIOTAP_DATA_RETRIES:
			rate_retries = *iterator.this_arg;
			break;

		case IEEE80211_RADIOTAP_MCS:
			mcs_known = iterator.this_arg[0];
			mcs_flags = iterator.this_arg[1];
			if (!(mcs_known & IEEE80211_RADIOTAP_MCS_HAVE_MCS))
				break;

			rate_found = true;
			rate = iterator.this_arg[2];
			rate_flags = IEEE80211_TX_RC_MCS;

			if (mcs_known & IEEE80211_RADIOTAP_MCS_HAVE_GI &&
			    mcs_flags & IEEE80211_RADIOTAP_MCS_SGI)
				rate_flags |= IEEE80211_TX_RC_SHORT_GI;

			mcs_bw = mcs_flags & IEEE80211_RADIOTAP_MCS_BW_MASK;
			if (mcs_known & IEEE80211_RADIOTAP_MCS_HAVE_BW &&
			    mcs_bw == IEEE80211_RADIOTAP_MCS_BW_40)
				rate_flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
			break;

		case IEEE80211_RADIOTAP_VHT:
			vht_known = get_unaligned_le16(iterator.this_arg);
			rate_found = true;

			rate_flags = IEEE80211_TX_RC_VHT_MCS;
			if ((vht_known & IEEE80211_RADIOTAP_VHT_KNOWN_GI) &&
			    (iterator.this_arg[2] &
			     IEEE80211_RADIOTAP_VHT_FLAG_SGI))
				rate_flags |= IEEE80211_TX_RC_SHORT_GI;
			if (vht_known &
			    IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH) {
				if (iterator.this_arg[3] == 1)
					rate_flags |=
						IEEE80211_TX_RC_40_MHZ_WIDTH;
				else if (iterator.this_arg[3] == 4)
					rate_flags |=
						IEEE80211_TX_RC_80_MHZ_WIDTH;
				else if (iterator.this_arg[3] == 11)
					rate_flags |=
						IEEE80211_TX_RC_160_MHZ_WIDTH;
			}

			vht_mcs = iterator.this_arg[4] >> 4;
			vht_nss = iterator.this_arg[4] & 0xF;
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

	if (rate_found) {
		info->control.flags |= IEEE80211_TX_CTRL_RATE_INJECT;

		for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
			info->control.rates[i].idx = -1;
			info->control.rates[i].flags = 0;
			info->control.rates[i].count = 0;
		}

		if (rate_flags & IEEE80211_TX_RC_MCS) {
			info->control.rates[0].idx = rate;
		} else if (rate_flags & IEEE80211_TX_RC_VHT_MCS) {
			ieee80211_rate_set_vht(info->control.rates, vht_mcs,
					       vht_nss);
		} else {
			for (i = 0; i < sband->n_bitrates; i++) {
				if (rate * 5 != sband->bitrates[i].bitrate)
					continue;

				info->control.rates[0].idx = i;
				break;
			}
		}

		if (info->control.rates[0].idx < 0)
			info->control.flags &= ~IEEE80211_TX_CTRL_RATE_INJECT;

		info->control.rates[0].flags = rate_flags;
		info->control.rates[0].count = min_t(u8, rate_retries + 1,
						     local->hw.max_rate_tries);
	}

	/*
	 * remove the radiotap header
	 * iterator->_max_length was sanity-checked against
	 * skb->len by iterator init
	 */
	skb_pull(skb, iterator._max_length);

	return true;
}

netdev_tx_t ieee80211_monitor_start_xmit(struct sk_buff *skb,
					 struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_radiotap_header *prthdr =
		(struct ieee80211_radiotap_header *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr;
	struct ieee80211_sub_if_data *tmp_sdata, *sdata;
	struct cfg80211_chan_def *chandef;
	u16 len_rthdr;
	int hdrlen;

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

	if (skb->len < len_rthdr + 2)
		goto fail;

	hdr = (struct ieee80211_hdr *)(skb->data + len_rthdr);
	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	if (skb->len < len_rthdr + hdrlen)
		goto fail;

	/*
	 * Initialize skb->protocol if the injected frame is a data frame
	 * carrying a rfc1042 header
	 */
	if (ieee80211_is_data(hdr->frame_control) &&
	    skb->len >= len_rthdr + hdrlen + sizeof(rfc1042_header) + 2) {
		u8 *payload = (u8 *)hdr + hdrlen;

		if (ether_addr_equal(payload, rfc1042_header))
			skb->protocol = cpu_to_be16((payload[6] << 8) |
						    payload[7]);
	}

	memset(info, 0, sizeof(*info));

	info->flags = IEEE80211_TX_CTL_REQ_TX_STATUS |
		      IEEE80211_TX_CTL_INJECTED;

	rcu_read_lock();

	/*
	 * We process outgoing injected frames that have a local address
	 * we handle as though they are non-injected frames.
	 * This code here isn't entirely correct, the local MAC address
	 * isn't always enough to find the interface to use; for proper
	 * VLAN/WDS support we will need a different mechanism (which
	 * likely isn't going to be monitor interfaces).
	 */
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	list_for_each_entry_rcu(tmp_sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(tmp_sdata))
			continue;
		if (tmp_sdata->vif.type == NL80211_IFTYPE_MONITOR ||
		    tmp_sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
		    tmp_sdata->vif.type == NL80211_IFTYPE_WDS)
			continue;
		if (ether_addr_equal(tmp_sdata->vif.addr, hdr->addr2)) {
			sdata = tmp_sdata;
			break;
		}
	}

	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (!chanctx_conf) {
		tmp_sdata = rcu_dereference(local->monitor_sdata);
		if (tmp_sdata)
			chanctx_conf =
				rcu_dereference(tmp_sdata->vif.chanctx_conf);
	}

	if (chanctx_conf)
		chandef = &chanctx_conf->def;
	else if (!local->use_chanctx)
		chandef = &local->_oper_chandef;
	else
		goto fail_rcu;

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
	if (!cfg80211_reg_can_beacon(local->hw.wiphy, chandef,
				     sdata->vif.type))
		goto fail_rcu;

	info->band = chandef->chan->band;

	/* process and remove the injection radiotap header */
	if (!ieee80211_parse_tx_radiotap(local, skb))
		goto fail_rcu;

	ieee80211_xmit(sdata, NULL, skb, 0);
	rcu_read_unlock();

	return NETDEV_TX_OK;

fail_rcu:
	rcu_read_unlock();
fail:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK; /* meaning, we dealt with the skb */
}

static inline bool ieee80211_is_tdls_setup(struct sk_buff *skb)
{
	u16 ethertype = (skb->data[12] << 8) | skb->data[13];

	return ethertype == ETH_P_TDLS &&
	       skb->len > 14 &&
	       skb->data[14] == WLAN_TDLS_SNAP_RFTYPE;
}

static int ieee80211_lookup_ra_sta(struct ieee80211_sub_if_data *sdata,
				   struct sk_buff *skb,
				   struct sta_info **sta_out)
{
	struct sta_info *sta;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		sta = rcu_dereference(sdata->u.vlan.sta);
		if (sta) {
			*sta_out = sta;
			return 0;
		} else if (sdata->wdev.use_4addr) {
			return -ENOLINK;
		}
		/* fall through */
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_OCB:
	case NL80211_IFTYPE_ADHOC:
		if (is_multicast_ether_addr(skb->data)) {
			*sta_out = ERR_PTR(-ENOENT);
			return 0;
		}
		sta = sta_info_get_bss(sdata, skb->data);
		break;
	case NL80211_IFTYPE_WDS:
		sta = sta_info_get(sdata, sdata->u.wds.remote_addr);
		break;
#ifdef CONFIG_MAC80211_MESH
	case NL80211_IFTYPE_MESH_POINT:
		/* determined much later */
		*sta_out = NULL;
		return 0;
#endif
	case NL80211_IFTYPE_STATION:
		if (sdata->wdev.wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS) {
			sta = sta_info_get(sdata, skb->data);
			if (sta && test_sta_flag(sta, WLAN_STA_TDLS_PEER)) {
				if (test_sta_flag(sta,
						  WLAN_STA_TDLS_PEER_AUTH)) {
					*sta_out = sta;
					return 0;
				}

				/*
				 * TDLS link during setup - throw out frames to
				 * peer. Allow TDLS-setup frames to unauthorized
				 * peers for the special case of a link teardown
				 * after a TDLS sta is removed due to being
				 * unreachable.
				 */
				if (!ieee80211_is_tdls_setup(skb))
					return -EINVAL;
			}

		}

		sta = sta_info_get(sdata, sdata->u.mgd.bssid);
		if (!sta)
			return -ENOLINK;
		break;
	default:
		return -EINVAL;
	}

	*sta_out = sta ?: ERR_PTR(-ENOENT);
	return 0;
}

/**
 * ieee80211_build_hdr - build 802.11 header in the given frame
 * @sdata: virtual interface to build the header for
 * @skb: the skb to build the header in
 * @info_flags: skb flags to set
 * @ctrl_flags: info control flags to set
 *
 * This function takes the skb with 802.3 header and reformats the header to
 * the appropriate IEEE 802.11 header based on which interface the packet is
 * being transmitted on.
 *
 * Note that this function also takes care of the TX status request and
 * potential unsharing of the SKB - this needs to be interleaved with the
 * header building.
 *
 * The function requires the read-side RCU lock held
 *
 * Returns: the (possibly reallocated) skb or an ERR_PTR() code
 */
static struct sk_buff *ieee80211_build_hdr(struct ieee80211_sub_if_data *sdata,
					   struct sk_buff *skb, u32 info_flags,
					   struct sta_info *sta, u32 ctrl_flags)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_info *info;
	int head_need;
	u16 ethertype, hdrlen,  meshhdrlen = 0;
	__le16 fc;
	struct ieee80211_hdr hdr;
	struct ieee80211s_hdr mesh_hdr __maybe_unused;
	struct mesh_path __maybe_unused *mppath = NULL, *mpath = NULL;
	const u8 *encaps_data;
	int encaps_len, skip_header_bytes;
	bool wme_sta = false, authorized = false;
	bool tdls_peer;
	bool multicast;
	u16 info_id = 0;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_sub_if_data *ap_sdata;
	enum nl80211_band band;
	int ret;

	if (IS_ERR(sta))
		sta = NULL;

	/* convert Ethernet header to proper 802.11 header (based on
	 * operation mode) */
	ethertype = (skb->data[12] << 8) | skb->data[13];
	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA);

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		if (sdata->wdev.use_4addr) {
			fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS);
			/* RA TA DA SA */
			memcpy(hdr.addr1, sta->sta.addr, ETH_ALEN);
			memcpy(hdr.addr2, sdata->vif.addr, ETH_ALEN);
			memcpy(hdr.addr3, skb->data, ETH_ALEN);
			memcpy(hdr.addr4, skb->data + ETH_ALEN, ETH_ALEN);
			hdrlen = 30;
			authorized = test_sta_flag(sta, WLAN_STA_AUTHORIZED);
			wme_sta = sta->sta.wme;
		}
		ap_sdata = container_of(sdata->bss, struct ieee80211_sub_if_data,
					u.ap);
		chanctx_conf = rcu_dereference(ap_sdata->vif.chanctx_conf);
		if (!chanctx_conf) {
			ret = -ENOTCONN;
			goto free;
		}
		band = chanctx_conf->def.chan->band;
		if (sdata->wdev.use_4addr)
			break;
		/* fall through */
	case NL80211_IFTYPE_AP:
		if (sdata->vif.type == NL80211_IFTYPE_AP)
			chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
		if (!chanctx_conf) {
			ret = -ENOTCONN;
			goto free;
		}
		fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS);
		/* DA BSSID SA */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, sdata->vif.addr, ETH_ALEN);
		memcpy(hdr.addr3, skb->data + ETH_ALEN, ETH_ALEN);
		hdrlen = 24;
		band = chanctx_conf->def.chan->band;
		break;
	case NL80211_IFTYPE_WDS:
		fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS);
		/* RA TA DA SA */
		memcpy(hdr.addr1, sdata->u.wds.remote_addr, ETH_ALEN);
		memcpy(hdr.addr2, sdata->vif.addr, ETH_ALEN);
		memcpy(hdr.addr3, skb->data, ETH_ALEN);
		memcpy(hdr.addr4, skb->data + ETH_ALEN, ETH_ALEN);
		hdrlen = 30;
		/*
		 * This is the exception! WDS style interfaces are prohibited
		 * when channel contexts are in used so this must be valid
		 */
		band = local->hw.conf.chandef.chan->band;
		break;
#ifdef CONFIG_MAC80211_MESH
	case NL80211_IFTYPE_MESH_POINT:
		if (!is_multicast_ether_addr(skb->data)) {
			struct sta_info *next_hop;
			bool mpp_lookup = true;

			mpath = mesh_path_lookup(sdata, skb->data);
			if (mpath) {
				mpp_lookup = false;
				next_hop = rcu_dereference(mpath->next_hop);
				if (!next_hop ||
				    !(mpath->flags & (MESH_PATH_ACTIVE |
						      MESH_PATH_RESOLVING)))
					mpp_lookup = true;
			}

			if (mpp_lookup) {
				mppath = mpp_path_lookup(sdata, skb->data);
				if (mppath)
					mppath->exp_time = jiffies;
			}

			if (mppath && mpath)
				mesh_path_del(sdata, mpath->dst);
		}

		/*
		 * Use address extension if it is a packet from
		 * another interface or if we know the destination
		 * is being proxied by a portal (i.e. portal address
		 * differs from proxied address)
		 */
		if (ether_addr_equal(sdata->vif.addr, skb->data + ETH_ALEN) &&
		    !(mppath && !ether_addr_equal(mppath->mpp, skb->data))) {
			hdrlen = ieee80211_fill_mesh_addresses(&hdr, &fc,
					skb->data, skb->data + ETH_ALEN);
			meshhdrlen = ieee80211_new_mesh_header(sdata, &mesh_hdr,
							       NULL, NULL);
		} else {
			/* DS -> MBSS (802.11-2012 13.11.3.3).
			 * For unicast with unknown forwarding information,
			 * destination might be in the MBSS or if that fails
			 * forwarded to another mesh gate. In either case
			 * resolution will be handled in ieee80211_xmit(), so
			 * leave the original DA. This also works for mcast */
			const u8 *mesh_da = skb->data;

			if (mppath)
				mesh_da = mppath->mpp;
			else if (mpath)
				mesh_da = mpath->dst;

			hdrlen = ieee80211_fill_mesh_addresses(&hdr, &fc,
					mesh_da, sdata->vif.addr);
			if (is_multicast_ether_addr(mesh_da))
				/* DA TA mSA AE:SA */
				meshhdrlen = ieee80211_new_mesh_header(
						sdata, &mesh_hdr,
						skb->data + ETH_ALEN, NULL);
			else
				/* RA TA mDA mSA AE:DA SA */
				meshhdrlen = ieee80211_new_mesh_header(
						sdata, &mesh_hdr, skb->data,
						skb->data + ETH_ALEN);

		}
		chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
		if (!chanctx_conf) {
			ret = -ENOTCONN;
			goto free;
		}
		band = chanctx_conf->def.chan->band;
		break;
#endif
	case NL80211_IFTYPE_STATION:
		/* we already did checks when looking up the RA STA */
		tdls_peer = test_sta_flag(sta, WLAN_STA_TDLS_PEER);

		if (tdls_peer) {
			/* DA SA BSSID */
			memcpy(hdr.addr1, skb->data, ETH_ALEN);
			memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
			memcpy(hdr.addr3, sdata->u.mgd.bssid, ETH_ALEN);
			hdrlen = 24;
		}  else if (sdata->u.mgd.use_4addr &&
			    cpu_to_be16(ethertype) != sdata->control_port_protocol) {
			fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS |
					  IEEE80211_FCTL_TODS);
			/* RA TA DA SA */
			memcpy(hdr.addr1, sdata->u.mgd.bssid, ETH_ALEN);
			memcpy(hdr.addr2, sdata->vif.addr, ETH_ALEN);
			memcpy(hdr.addr3, skb->data, ETH_ALEN);
			memcpy(hdr.addr4, skb->data + ETH_ALEN, ETH_ALEN);
			hdrlen = 30;
		} else {
			fc |= cpu_to_le16(IEEE80211_FCTL_TODS);
			/* BSSID SA DA */
			memcpy(hdr.addr1, sdata->u.mgd.bssid, ETH_ALEN);
			memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
			memcpy(hdr.addr3, skb->data, ETH_ALEN);
			hdrlen = 24;
		}
		chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
		if (!chanctx_conf) {
			ret = -ENOTCONN;
			goto free;
		}
		band = chanctx_conf->def.chan->band;
		break;
	case NL80211_IFTYPE_OCB:
		/* DA SA BSSID */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		eth_broadcast_addr(hdr.addr3);
		hdrlen = 24;
		chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
		if (!chanctx_conf) {
			ret = -ENOTCONN;
			goto free;
		}
		band = chanctx_conf->def.chan->band;
		break;
	case NL80211_IFTYPE_ADHOC:
		/* DA SA BSSID */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, sdata->u.ibss.bssid, ETH_ALEN);
		hdrlen = 24;
		chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
		if (!chanctx_conf) {
			ret = -ENOTCONN;
			goto free;
		}
		band = chanctx_conf->def.chan->band;
		break;
	default:
		ret = -EINVAL;
		goto free;
	}

	multicast = is_multicast_ether_addr(hdr.addr1);

	/* sta is always NULL for mesh */
	if (sta) {
		authorized = test_sta_flag(sta, WLAN_STA_AUTHORIZED);
		wme_sta = sta->sta.wme;
	} else if (ieee80211_vif_is_mesh(&sdata->vif)) {
		/* For mesh, the use of the QoS header is mandatory */
		wme_sta = true;
	}

	/* receiver does QoS (which also means we do) use it */
	if (wme_sta) {
		fc |= cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
		hdrlen += 2;
	}

	/*
	 * Drop unicast frames to unauthorised stations unless they are
	 * EAPOL frames from the local station.
	 */
	if (unlikely(!ieee80211_vif_is_mesh(&sdata->vif) &&
		     (sdata->vif.type != NL80211_IFTYPE_OCB) &&
		     !multicast && !authorized &&
		     (cpu_to_be16(ethertype) != sdata->control_port_protocol ||
		      !ether_addr_equal(sdata->vif.addr, skb->data + ETH_ALEN)))) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		net_info_ratelimited("%s: dropped frame to %pM (unauthorized port)\n",
				    sdata->name, hdr.addr1);
#endif

		I802_DEBUG_INC(local->tx_handlers_drop_unauth_port);

		ret = -EPERM;
		goto free;
	}

	if (unlikely(!multicast && skb->sk &&
		     skb_shinfo(skb)->tx_flags & SKBTX_WIFI_STATUS)) {
		struct sk_buff *ack_skb = skb_clone_sk(skb);

		if (ack_skb) {
			unsigned long flags;
			int id;

			spin_lock_irqsave(&local->ack_status_lock, flags);
			id = idr_alloc(&local->ack_status_frames, ack_skb,
				       1, 0x10000, GFP_ATOMIC);
			spin_unlock_irqrestore(&local->ack_status_lock, flags);

			if (id >= 0) {
				info_id = id;
				info_flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
			} else {
				kfree_skb(ack_skb);
			}
		}
	}

	/*
	 * If the skb is shared we need to obtain our own copy.
	 */
	if (skb_shared(skb)) {
		struct sk_buff *tmp_skb = skb;

		/* can't happen -- skb is a clone if info_id != 0 */
		WARN_ON(info_id);

		skb = skb_clone(skb, GFP_ATOMIC);
		kfree_skb(tmp_skb);

		if (!skb) {
			ret = -ENOMEM;
			goto free;
		}
	}

	hdr.frame_control = fc;
	hdr.duration_id = 0;
	hdr.seq_ctrl = 0;

	skip_header_bytes = ETH_HLEN;
	if (ethertype == ETH_P_AARP || ethertype == ETH_P_IPX) {
		encaps_data = bridge_tunnel_header;
		encaps_len = sizeof(bridge_tunnel_header);
		skip_header_bytes -= 2;
	} else if (ethertype >= ETH_P_802_3_MIN) {
		encaps_data = rfc1042_header;
		encaps_len = sizeof(rfc1042_header);
		skip_header_bytes -= 2;
	} else {
		encaps_data = NULL;
		encaps_len = 0;
	}

	skb_pull(skb, skip_header_bytes);
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
		head_need += sdata->encrypt_headroom;
		head_need += local->tx_headroom;
		head_need = max_t(int, 0, head_need);
		if (ieee80211_skb_resize(sdata, skb, head_need, ENCRYPT_DATA)) {
			ieee80211_free_txskb(&local->hw, skb);
			skb = NULL;
			return ERR_PTR(-ENOMEM);
		}
	}

	if (encaps_data)
		memcpy(skb_push(skb, encaps_len), encaps_data, encaps_len);

#ifdef CONFIG_MAC80211_MESH
	if (meshhdrlen > 0)
		memcpy(skb_push(skb, meshhdrlen), &mesh_hdr, meshhdrlen);
#endif

	if (ieee80211_is_data_qos(fc)) {
		__le16 *qos_control;

		qos_control = skb_push(skb, 2);
		memcpy(skb_push(skb, hdrlen - 2), &hdr, hdrlen - 2);
		/*
		 * Maybe we could actually set some fields here, for now just
		 * initialise to zero to indicate no special operation.
		 */
		*qos_control = 0;
	} else
		memcpy(skb_push(skb, hdrlen), &hdr, hdrlen);

	skb_reset_mac_header(skb);

	info = IEEE80211_SKB_CB(skb);
	memset(info, 0, sizeof(*info));

	info->flags = info_flags;
	info->ack_frame_id = info_id;
	info->band = band;
	info->control.flags = ctrl_flags;

	return skb;
 free:
	kfree_skb(skb);
	return ERR_PTR(ret);
}

/*
 * fast-xmit overview
 *
 * The core idea of this fast-xmit is to remove per-packet checks by checking
 * them out of band. ieee80211_check_fast_xmit() implements the out-of-band
 * checks that are needed to get the sta->fast_tx pointer assigned, after which
 * much less work can be done per packet. For example, fragmentation must be
 * disabled or the fast_tx pointer will not be set. All the conditions are seen
 * in the code here.
 *
 * Once assigned, the fast_tx data structure also caches the per-packet 802.11
 * header and other data to aid packet processing in ieee80211_xmit_fast().
 *
 * The most difficult part of this is that when any of these assumptions
 * change, an external trigger (i.e. a call to ieee80211_clear_fast_xmit(),
 * ieee80211_check_fast_xmit() or friends) is required to reset the data,
 * since the per-packet code no longer checks the conditions. This is reflected
 * by the calls to these functions throughout the rest of the code, and must be
 * maintained if any of the TX path checks change.
 */

void ieee80211_check_fast_xmit(struct sta_info *sta)
{
	struct ieee80211_fast_tx build = {}, *fast_tx = NULL, *old;
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_hdr *hdr = (void *)build.hdr;
	struct ieee80211_chanctx_conf *chanctx_conf;
	__le16 fc;

	if (!ieee80211_hw_check(&local->hw, SUPPORT_FAST_XMIT))
		return;

	/* Locking here protects both the pointer itself, and against concurrent
	 * invocations winning data access races to, e.g., the key pointer that
	 * is used.
	 * Without it, the invocation of this function right after the key
	 * pointer changes wouldn't be sufficient, as another CPU could access
	 * the pointer, then stall, and then do the cache update after the CPU
	 * that invalidated the key.
	 * With the locking, such scenarios cannot happen as the check for the
	 * key and the fast-tx assignment are done atomically, so the CPU that
	 * modifies the key will either wait or other one will see the key
	 * cleared/changed already.
	 */
	spin_lock_bh(&sta->lock);
	if (ieee80211_hw_check(&local->hw, SUPPORTS_PS) &&
	    !ieee80211_hw_check(&local->hw, SUPPORTS_DYNAMIC_PS) &&
	    sdata->vif.type == NL80211_IFTYPE_STATION)
		goto out;

	if (!test_sta_flag(sta, WLAN_STA_AUTHORIZED))
		goto out;

	if (test_sta_flag(sta, WLAN_STA_PS_STA) ||
	    test_sta_flag(sta, WLAN_STA_PS_DRIVER) ||
	    test_sta_flag(sta, WLAN_STA_PS_DELIVER) ||
	    test_sta_flag(sta, WLAN_STA_CLEAR_PS_FILT))
		goto out;

	if (sdata->noack_map)
		goto out;

	/* fast-xmit doesn't handle fragmentation at all */
	if (local->hw.wiphy->frag_threshold != (u32)-1 &&
	    !ieee80211_hw_check(&local->hw, SUPPORTS_TX_FRAG))
		goto out;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (!chanctx_conf) {
		rcu_read_unlock();
		goto out;
	}
	build.band = chanctx_conf->def.chan->band;
	rcu_read_unlock();

	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA);

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_ADHOC:
		/* DA SA BSSID */
		build.da_offs = offsetof(struct ieee80211_hdr, addr1);
		build.sa_offs = offsetof(struct ieee80211_hdr, addr2);
		memcpy(hdr->addr3, sdata->u.ibss.bssid, ETH_ALEN);
		build.hdr_len = 24;
		break;
	case NL80211_IFTYPE_STATION:
		if (test_sta_flag(sta, WLAN_STA_TDLS_PEER)) {
			/* DA SA BSSID */
			build.da_offs = offsetof(struct ieee80211_hdr, addr1);
			build.sa_offs = offsetof(struct ieee80211_hdr, addr2);
			memcpy(hdr->addr3, sdata->u.mgd.bssid, ETH_ALEN);
			build.hdr_len = 24;
			break;
		}

		if (sdata->u.mgd.use_4addr) {
			/* non-regular ethertype cannot use the fastpath */
			fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS |
					  IEEE80211_FCTL_TODS);
			/* RA TA DA SA */
			memcpy(hdr->addr1, sdata->u.mgd.bssid, ETH_ALEN);
			memcpy(hdr->addr2, sdata->vif.addr, ETH_ALEN);
			build.da_offs = offsetof(struct ieee80211_hdr, addr3);
			build.sa_offs = offsetof(struct ieee80211_hdr, addr4);
			build.hdr_len = 30;
			break;
		}
		fc |= cpu_to_le16(IEEE80211_FCTL_TODS);
		/* BSSID SA DA */
		memcpy(hdr->addr1, sdata->u.mgd.bssid, ETH_ALEN);
		build.da_offs = offsetof(struct ieee80211_hdr, addr3);
		build.sa_offs = offsetof(struct ieee80211_hdr, addr2);
		build.hdr_len = 24;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		if (sdata->wdev.use_4addr) {
			fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS |
					  IEEE80211_FCTL_TODS);
			/* RA TA DA SA */
			memcpy(hdr->addr1, sta->sta.addr, ETH_ALEN);
			memcpy(hdr->addr2, sdata->vif.addr, ETH_ALEN);
			build.da_offs = offsetof(struct ieee80211_hdr, addr3);
			build.sa_offs = offsetof(struct ieee80211_hdr, addr4);
			build.hdr_len = 30;
			break;
		}
		/* fall through */
	case NL80211_IFTYPE_AP:
		fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS);
		/* DA BSSID SA */
		build.da_offs = offsetof(struct ieee80211_hdr, addr1);
		memcpy(hdr->addr2, sdata->vif.addr, ETH_ALEN);
		build.sa_offs = offsetof(struct ieee80211_hdr, addr3);
		build.hdr_len = 24;
		break;
	default:
		/* not handled on fast-xmit */
		goto out;
	}

	if (sta->sta.wme) {
		build.hdr_len += 2;
		fc |= cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
	}

	/* We store the key here so there's no point in using rcu_dereference()
	 * but that's fine because the code that changes the pointers will call
	 * this function after doing so. For a single CPU that would be enough,
	 * for multiple see the comment above.
	 */
	build.key = rcu_access_pointer(sta->ptk[sta->ptk_idx]);
	if (!build.key)
		build.key = rcu_access_pointer(sdata->default_unicast_key);
	if (build.key) {
		bool gen_iv, iv_spc, mmic;

		gen_iv = build.key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV;
		iv_spc = build.key->conf.flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		mmic = build.key->conf.flags &
			(IEEE80211_KEY_FLAG_GENERATE_MMIC |
			 IEEE80211_KEY_FLAG_PUT_MIC_SPACE);

		/* don't handle software crypto */
		if (!(build.key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
			goto out;

		switch (build.key->conf.cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
			/* add fixed key ID */
			if (gen_iv) {
				(build.hdr + build.hdr_len)[3] =
					0x20 | (build.key->conf.keyidx << 6);
				build.pn_offs = build.hdr_len;
			}
			if (gen_iv || iv_spc)
				build.hdr_len += IEEE80211_CCMP_HDR_LEN;
			break;
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
			/* add fixed key ID */
			if (gen_iv) {
				(build.hdr + build.hdr_len)[3] =
					0x20 | (build.key->conf.keyidx << 6);
				build.pn_offs = build.hdr_len;
			}
			if (gen_iv || iv_spc)
				build.hdr_len += IEEE80211_GCMP_HDR_LEN;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			/* cannot handle MMIC or IV generation in xmit-fast */
			if (mmic || gen_iv)
				goto out;
			if (iv_spc)
				build.hdr_len += IEEE80211_TKIP_IV_LEN;
			break;
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			/* cannot handle IV generation in fast-xmit */
			if (gen_iv)
				goto out;
			if (iv_spc)
				build.hdr_len += IEEE80211_WEP_IV_LEN;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
		case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		case WLAN_CIPHER_SUITE_BIP_GMAC_256:
			WARN(1,
			     "management cipher suite 0x%x enabled for data\n",
			     build.key->conf.cipher);
			goto out;
		default:
			/* we don't know how to generate IVs for this at all */
			if (WARN_ON(gen_iv))
				goto out;
			/* pure hardware keys are OK, of course */
			if (!(build.key->flags & KEY_FLAG_CIPHER_SCHEME))
				break;
			/* cipher scheme might require space allocation */
			if (iv_spc &&
			    build.key->conf.iv_len > IEEE80211_FAST_XMIT_MAX_IV)
				goto out;
			if (iv_spc)
				build.hdr_len += build.key->conf.iv_len;
		}

		fc |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	}

	hdr->frame_control = fc;

	memcpy(build.hdr + build.hdr_len,
	       rfc1042_header,  sizeof(rfc1042_header));
	build.hdr_len += sizeof(rfc1042_header);

	fast_tx = kmemdup(&build, sizeof(build), GFP_ATOMIC);
	/* if the kmemdup fails, continue w/o fast_tx */
	if (!fast_tx)
		goto out;

 out:
	/* we might have raced against another call to this function */
	old = rcu_dereference_protected(sta->fast_tx,
					lockdep_is_held(&sta->lock));
	rcu_assign_pointer(sta->fast_tx, fast_tx);
	if (old)
		kfree_rcu(old, rcu_head);
	spin_unlock_bh(&sta->lock);
}

void ieee80211_check_fast_xmit_all(struct ieee80211_local *local)
{
	struct sta_info *sta;

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &local->sta_list, list)
		ieee80211_check_fast_xmit(sta);
	rcu_read_unlock();
}

void ieee80211_check_fast_xmit_iface(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	rcu_read_lock();

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sdata != sta->sdata &&
		    (!sta->sdata->bss || sta->sdata->bss != sdata->bss))
			continue;
		ieee80211_check_fast_xmit(sta);
	}

	rcu_read_unlock();
}

void ieee80211_clear_fast_xmit(struct sta_info *sta)
{
	struct ieee80211_fast_tx *fast_tx;

	spin_lock_bh(&sta->lock);
	fast_tx = rcu_dereference_protected(sta->fast_tx,
					    lockdep_is_held(&sta->lock));
	RCU_INIT_POINTER(sta->fast_tx, NULL);
	spin_unlock_bh(&sta->lock);

	if (fast_tx)
		kfree_rcu(fast_tx, rcu_head);
}

static bool ieee80211_amsdu_realloc_pad(struct ieee80211_local *local,
					struct sk_buff *skb, int headroom)
{
	if (skb_headroom(skb) < headroom) {
		I802_DEBUG_INC(local->tx_expand_skb_head);

		if (pskb_expand_head(skb, headroom, 0, GFP_ATOMIC)) {
			wiphy_debug(local->hw.wiphy,
				    "failed to reallocate TX buffer\n");
			return false;
		}
	}

	return true;
}

static bool ieee80211_amsdu_prepare_head(struct ieee80211_sub_if_data *sdata,
					 struct ieee80211_fast_tx *fast_tx,
					 struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr;
	struct ethhdr *amsdu_hdr;
	int hdr_len = fast_tx->hdr_len - sizeof(rfc1042_header);
	int subframe_len = skb->len - hdr_len;
	void *data;
	u8 *qc, *h_80211_src, *h_80211_dst;
	const u8 *bssid;

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
		return false;

	if (info->control.flags & IEEE80211_TX_CTRL_AMSDU)
		return true;

	if (!ieee80211_amsdu_realloc_pad(local, skb, sizeof(*amsdu_hdr)))
		return false;

	data = skb_push(skb, sizeof(*amsdu_hdr));
	memmove(data, data + sizeof(*amsdu_hdr), hdr_len);
	hdr = data;
	amsdu_hdr = data + hdr_len;
	/* h_80211_src/dst is addr* field within hdr */
	h_80211_src = data + fast_tx->sa_offs;
	h_80211_dst = data + fast_tx->da_offs;

	amsdu_hdr->h_proto = cpu_to_be16(subframe_len);
	ether_addr_copy(amsdu_hdr->h_source, h_80211_src);
	ether_addr_copy(amsdu_hdr->h_dest, h_80211_dst);

	/* according to IEEE 802.11-2012 8.3.2 table 8-19, the outer SA/DA
	 * fields needs to be changed to BSSID for A-MSDU frames depending
	 * on FromDS/ToDS values.
	 */
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
		bssid = sdata->u.mgd.bssid;
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
		bssid = sdata->vif.addr;
		break;
	default:
		bssid = NULL;
	}

	if (bssid && ieee80211_has_fromds(hdr->frame_control))
		ether_addr_copy(h_80211_src, bssid);

	if (bssid && ieee80211_has_tods(hdr->frame_control))
		ether_addr_copy(h_80211_dst, bssid);

	qc = ieee80211_get_qos_ctl(hdr);
	*qc |= IEEE80211_QOS_CTL_A_MSDU_PRESENT;

	info->control.flags |= IEEE80211_TX_CTRL_AMSDU;

	return true;
}

static bool ieee80211_amsdu_aggregate(struct ieee80211_sub_if_data *sdata,
				      struct sta_info *sta,
				      struct ieee80211_fast_tx *fast_tx,
				      struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct fq *fq = &local->fq;
	struct fq_tin *tin;
	struct fq_flow *flow;
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
	struct ieee80211_txq *txq = sta->sta.txq[tid];
	struct txq_info *txqi;
	struct sk_buff **frag_tail, *head;
	int subframe_len = skb->len - ETH_ALEN;
	u8 max_subframes = sta->sta.max_amsdu_subframes;
	int max_frags = local->hw.max_tx_fragments;
	int max_amsdu_len = sta->sta.max_amsdu_len;
	int orig_truesize;
	__be16 len;
	void *data;
	bool ret = false;
	unsigned int orig_len;
	int n = 2, nfrags, pad = 0;
	u16 hdrlen;

	if (!ieee80211_hw_check(&local->hw, TX_AMSDU))
		return false;

	if (!txq)
		return false;

	txqi = to_txq_info(txq);
	if (test_bit(IEEE80211_TXQ_NO_AMSDU, &txqi->flags))
		return false;

	if (sta->sta.max_rc_amsdu_len)
		max_amsdu_len = min_t(int, max_amsdu_len,
				      sta->sta.max_rc_amsdu_len);

	spin_lock_bh(&fq->lock);

	/* TODO: Ideally aggregation should be done on dequeue to remain
	 * responsive to environment changes.
	 */

	tin = &txqi->tin;
	flow = fq_flow_classify(fq, tin, skb, fq_flow_get_default_func);
	head = skb_peek_tail(&flow->queue);
	if (!head)
		goto out;

	orig_truesize = head->truesize;
	orig_len = head->len;

	if (skb->len + head->len > max_amsdu_len)
		goto out;

	nfrags = 1 + skb_shinfo(skb)->nr_frags;
	nfrags += 1 + skb_shinfo(head)->nr_frags;
	frag_tail = &skb_shinfo(head)->frag_list;
	while (*frag_tail) {
		nfrags += 1 + skb_shinfo(*frag_tail)->nr_frags;
		frag_tail = &(*frag_tail)->next;
		n++;
	}

	if (max_subframes && n > max_subframes)
		goto out;

	if (max_frags && nfrags > max_frags)
		goto out;

	if (!ieee80211_amsdu_prepare_head(sdata, fast_tx, head))
		goto out;

	/*
	 * Pad out the previous subframe to a multiple of 4 by adding the
	 * padding to the next one, that's being added. Note that head->len
	 * is the length of the full A-MSDU, but that works since each time
	 * we add a new subframe we pad out the previous one to a multiple
	 * of 4 and thus it no longer matters in the next round.
	 */
	hdrlen = fast_tx->hdr_len - sizeof(rfc1042_header);
	if ((head->len - hdrlen) & 3)
		pad = 4 - ((head->len - hdrlen) & 3);

	if (!ieee80211_amsdu_realloc_pad(local, skb, sizeof(rfc1042_header) +
						     2 + pad))
		goto out_recalc;

	ret = true;
	data = skb_push(skb, ETH_ALEN + 2);
	memmove(data, data + ETH_ALEN + 2, 2 * ETH_ALEN);

	data += 2 * ETH_ALEN;
	len = cpu_to_be16(subframe_len);
	memcpy(data, &len, 2);
	memcpy(data + 2, rfc1042_header, sizeof(rfc1042_header));

	memset(skb_push(skb, pad), 0, pad);

	head->len += skb->len;
	head->data_len += skb->len;
	*frag_tail = skb;

out_recalc:
	fq->memory_usage += head->truesize - orig_truesize;
	if (head->len != orig_len) {
		flow->backlog += head->len - orig_len;
		tin->backlog_bytes += head->len - orig_len;

		fq_recalc_backlog(fq, tin, flow);
	}
out:
	spin_unlock_bh(&fq->lock);

	return ret;
}

/*
 * Can be called while the sta lock is held. Anything that can cause packets to
 * be generated will cause deadlock!
 */
static void ieee80211_xmit_fast_finish(struct ieee80211_sub_if_data *sdata,
				       struct sta_info *sta, u8 pn_offs,
				       struct ieee80211_key *key,
				       struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	u8 tid = IEEE80211_NUM_TIDS;

	if (key)
		info->control.hw_key = &key->conf;

	ieee80211_tx_stats(skb->dev, skb->len);

	if (hdr->frame_control & cpu_to_le16(IEEE80211_STYPE_QOS_DATA)) {
		tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
		hdr->seq_ctrl = ieee80211_tx_next_seq(sta, tid);
	} else {
		info->flags |= IEEE80211_TX_CTL_ASSIGN_SEQ;
		hdr->seq_ctrl = cpu_to_le16(sdata->sequence_number);
		sdata->sequence_number += 0x10;
	}

	if (skb_shinfo(skb)->gso_size)
		sta->tx_stats.msdu[tid] +=
			DIV_ROUND_UP(skb->len, skb_shinfo(skb)->gso_size);
	else
		sta->tx_stats.msdu[tid]++;

	info->hw_queue = sdata->vif.hw_queue[skb_get_queue_mapping(skb)];

	/* statistics normally done by ieee80211_tx_h_stats (but that
	 * has to consider fragmentation, so is more complex)
	 */
	sta->tx_stats.bytes[skb_get_queue_mapping(skb)] += skb->len;
	sta->tx_stats.packets[skb_get_queue_mapping(skb)]++;

	if (pn_offs) {
		u64 pn;
		u8 *crypto_hdr = skb->data + pn_offs;

		switch (key->conf.cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
			pn = atomic64_inc_return(&key->conf.tx_pn);
			crypto_hdr[0] = pn;
			crypto_hdr[1] = pn >> 8;
			crypto_hdr[4] = pn >> 16;
			crypto_hdr[5] = pn >> 24;
			crypto_hdr[6] = pn >> 32;
			crypto_hdr[7] = pn >> 40;
			break;
		}
	}
}

static bool ieee80211_xmit_fast(struct ieee80211_sub_if_data *sdata,
				struct sta_info *sta,
				struct ieee80211_fast_tx *fast_tx,
				struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	u16 ethertype = (skb->data[12] << 8) | skb->data[13];
	int extra_head = fast_tx->hdr_len - (ETH_HLEN - 2);
	int hw_headroom = sdata->local->hw.extra_tx_headroom;
	struct ethhdr eth;
	struct ieee80211_tx_info *info;
	struct ieee80211_hdr *hdr = (void *)fast_tx->hdr;
	struct ieee80211_tx_data tx;
	ieee80211_tx_result r;
	struct tid_ampdu_tx *tid_tx = NULL;
	u8 tid = IEEE80211_NUM_TIDS;

	/* control port protocol needs a lot of special handling */
	if (cpu_to_be16(ethertype) == sdata->control_port_protocol)
		return false;

	/* only RFC 1042 SNAP */
	if (ethertype < ETH_P_802_3_MIN)
		return false;

	/* don't handle TX status request here either */
	if (skb->sk && skb_shinfo(skb)->tx_flags & SKBTX_WIFI_STATUS)
		return false;

	if (hdr->frame_control & cpu_to_le16(IEEE80211_STYPE_QOS_DATA)) {
		tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
		tid_tx = rcu_dereference(sta->ampdu_mlme.tid_tx[tid]);
		if (tid_tx) {
			if (!test_bit(HT_AGG_STATE_OPERATIONAL, &tid_tx->state))
				return false;
			if (tid_tx->timeout)
				tid_tx->last_tx = jiffies;
		}
	}

	/* after this point (skb is modified) we cannot return false */

	if (skb_shared(skb)) {
		struct sk_buff *tmp_skb = skb;

		skb = skb_clone(skb, GFP_ATOMIC);
		kfree_skb(tmp_skb);

		if (!skb)
			return true;
	}

	if ((hdr->frame_control & cpu_to_le16(IEEE80211_STYPE_QOS_DATA)) &&
	    ieee80211_amsdu_aggregate(sdata, sta, fast_tx, skb))
		return true;

	/* will not be crypto-handled beyond what we do here, so use false
	 * as the may-encrypt argument for the resize to not account for
	 * more room than we already have in 'extra_head'
	 */
	if (unlikely(ieee80211_skb_resize(sdata, skb,
					  max_t(int, extra_head + hw_headroom -
						     skb_headroom(skb), 0),
					  ENCRYPT_NO))) {
		kfree_skb(skb);
		return true;
	}

	memcpy(&eth, skb->data, ETH_HLEN - 2);
	hdr = skb_push(skb, extra_head);
	memcpy(skb->data, fast_tx->hdr, fast_tx->hdr_len);
	memcpy(skb->data + fast_tx->da_offs, eth.h_dest, ETH_ALEN);
	memcpy(skb->data + fast_tx->sa_offs, eth.h_source, ETH_ALEN);

	info = IEEE80211_SKB_CB(skb);
	memset(info, 0, sizeof(*info));
	info->band = fast_tx->band;
	info->control.vif = &sdata->vif;
	info->flags = IEEE80211_TX_CTL_FIRST_FRAGMENT |
		      IEEE80211_TX_CTL_DONTFRAG |
		      (tid_tx ? IEEE80211_TX_CTL_AMPDU : 0);
	info->control.flags = IEEE80211_TX_CTRL_FAST_XMIT;

	if (hdr->frame_control & cpu_to_le16(IEEE80211_STYPE_QOS_DATA)) {
		tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
		*ieee80211_get_qos_ctl(hdr) = tid;
	}

	__skb_queue_head_init(&tx.skbs);

	tx.flags = IEEE80211_TX_UNICAST;
	tx.local = local;
	tx.sdata = sdata;
	tx.sta = sta;
	tx.key = fast_tx->key;

	if (!ieee80211_hw_check(&local->hw, HAS_RATE_CONTROL)) {
		tx.skb = skb;
		r = ieee80211_tx_h_rate_ctrl(&tx);
		skb = tx.skb;
		tx.skb = NULL;

		if (r != TX_CONTINUE) {
			if (r != TX_QUEUED)
				kfree_skb(skb);
			return true;
		}
	}

	if (ieee80211_queue_skb(local, sdata, sta, skb))
		return true;

	ieee80211_xmit_fast_finish(sdata, sta, fast_tx->pn_offs,
				   fast_tx->key, skb);

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		sdata = container_of(sdata->bss,
				     struct ieee80211_sub_if_data, u.ap);

	__skb_queue_tail(&tx.skbs, skb);
	ieee80211_tx_frags(local, &sdata->vif, &sta->sta, &tx.skbs, false);
	return true;
}

struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw,
				     struct ieee80211_txq *txq)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct txq_info *txqi = container_of(txq, struct txq_info, txq);
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb = NULL;
	struct fq *fq = &local->fq;
	struct fq_tin *tin = &txqi->tin;
	struct ieee80211_tx_info *info;
	struct ieee80211_tx_data tx;
	ieee80211_tx_result r;
	struct ieee80211_vif *vif;

	spin_lock_bh(&fq->lock);

	if (test_bit(IEEE80211_TXQ_STOP, &txqi->flags))
		goto out;

	/* Make sure fragments stay together. */
	skb = __skb_dequeue(&txqi->frags);
	if (skb)
		goto out;

begin:
	skb = fq_tin_dequeue(fq, tin, fq_tin_dequeue_func);
	if (!skb)
		goto out;

	hdr = (struct ieee80211_hdr *)skb->data;
	info = IEEE80211_SKB_CB(skb);

	memset(&tx, 0, sizeof(tx));
	__skb_queue_head_init(&tx.skbs);
	tx.local = local;
	tx.skb = skb;
	tx.sdata = vif_to_sdata(info->control.vif);

	if (txq->sta) {
		tx.sta = container_of(txq->sta, struct sta_info, sta);
		/*
		 * Drop unicast frames to unauthorised stations unless they are
		 * EAPOL frames from the local station.
		 */
		if (unlikely(ieee80211_is_data(hdr->frame_control) &&
			     !ieee80211_vif_is_mesh(&tx.sdata->vif) &&
			     tx.sdata->vif.type != NL80211_IFTYPE_OCB &&
			     !is_multicast_ether_addr(hdr->addr1) &&
			     !test_sta_flag(tx.sta, WLAN_STA_AUTHORIZED) &&
			     (!(info->control.flags &
				IEEE80211_TX_CTRL_PORT_CTRL_PROTO) ||
			      !ether_addr_equal(tx.sdata->vif.addr,
						hdr->addr2)))) {
			I802_DEBUG_INC(local->tx_handlers_drop_unauth_port);
			ieee80211_free_txskb(&local->hw, skb);
			goto begin;
		}
	}

	/*
	 * The key can be removed while the packet was queued, so need to call
	 * this here to get the current key.
	 */
	r = ieee80211_tx_h_select_key(&tx);
	if (r != TX_CONTINUE) {
		ieee80211_free_txskb(&local->hw, skb);
		goto begin;
	}

	if (test_bit(IEEE80211_TXQ_AMPDU, &txqi->flags))
		info->flags |= IEEE80211_TX_CTL_AMPDU;
	else
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;

	if (info->control.flags & IEEE80211_TX_CTRL_FAST_XMIT) {
		struct sta_info *sta = container_of(txq->sta, struct sta_info,
						    sta);
		u8 pn_offs = 0;

		if (tx.key &&
		    (tx.key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV))
			pn_offs = ieee80211_hdrlen(hdr->frame_control);

		ieee80211_xmit_fast_finish(sta->sdata, sta, pn_offs,
					   tx.key, skb);
	} else {
		if (invoke_tx_handlers_late(&tx))
			goto begin;

		skb = __skb_dequeue(&tx.skbs);

		if (!skb_queue_empty(&tx.skbs))
			skb_queue_splice_tail(&tx.skbs, &txqi->frags);
	}

	if (skb && skb_has_frag_list(skb) &&
	    !ieee80211_hw_check(&local->hw, TX_FRAG_LIST)) {
		if (skb_linearize(skb)) {
			ieee80211_free_txskb(&local->hw, skb);
			goto begin;
		}
	}

	switch (tx.sdata->vif.type) {
	case NL80211_IFTYPE_MONITOR:
		if (tx.sdata->u.mntr.flags & MONITOR_FLAG_ACTIVE) {
			vif = &tx.sdata->vif;
			break;
		}
		tx.sdata = rcu_dereference(local->monitor_sdata);
		if (tx.sdata) {
			vif = &tx.sdata->vif;
			info->hw_queue =
				vif->hw_queue[skb_get_queue_mapping(skb)];
		} else if (ieee80211_hw_check(&local->hw, QUEUE_CONTROL)) {
			ieee80211_free_txskb(&local->hw, skb);
			goto begin;
		} else {
			vif = NULL;
		}
		break;
	case NL80211_IFTYPE_AP_VLAN:
		tx.sdata = container_of(tx.sdata->bss,
					struct ieee80211_sub_if_data, u.ap);
		/* fall through */
	default:
		vif = &tx.sdata->vif;
		break;
	}

	IEEE80211_SKB_CB(skb)->control.vif = vif;
out:
	spin_unlock_bh(&fq->lock);

	return skb;
}
EXPORT_SYMBOL(ieee80211_tx_dequeue);

void __ieee80211_subif_start_xmit(struct sk_buff *skb,
				  struct net_device *dev,
				  u32 info_flags,
				  u32 ctrl_flags)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta;
	struct sk_buff *next;

	if (unlikely(skb->len < ETH_HLEN)) {
		kfree_skb(skb);
		return;
	}

	rcu_read_lock();

	if (ieee80211_lookup_ra_sta(sdata, skb, &sta))
		goto out_free;

	if (!IS_ERR_OR_NULL(sta)) {
		struct ieee80211_fast_tx *fast_tx;

		/* We need a bit of data queued to build aggregates properly, so
		 * instruct the TCP stack to allow more than a single ms of data
		 * to be queued in the stack. The value is a bit-shift of 1
		 * second, so 7 is ~8ms of queued data. Only affects local TCP
		 * sockets.
		 */
		sk_pacing_shift_update(skb->sk, 7);

		fast_tx = rcu_dereference(sta->fast_tx);

		if (fast_tx &&
		    ieee80211_xmit_fast(sdata, sta, fast_tx, skb))
			goto out;
	}

	if (skb_is_gso(skb)) {
		struct sk_buff *segs;

		segs = skb_gso_segment(skb, 0);
		if (IS_ERR(segs)) {
			goto out_free;
		} else if (segs) {
			consume_skb(skb);
			skb = segs;
		}
	} else {
		/* we cannot process non-linear frames on this path */
		if (skb_linearize(skb)) {
			kfree_skb(skb);
			goto out;
		}

		/* the frame could be fragmented, software-encrypted, and other
		 * things so we cannot really handle checksum offload with it -
		 * fix it up in software before we handle anything else.
		 */
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			skb_set_transport_header(skb,
						 skb_checksum_start_offset(skb));
			if (skb_checksum_help(skb))
				goto out_free;
		}
	}

	next = skb;
	while (next) {
		skb = next;
		next = skb->next;

		skb->prev = NULL;
		skb->next = NULL;

		skb = ieee80211_build_hdr(sdata, skb, info_flags,
					  sta, ctrl_flags);
		if (IS_ERR(skb))
			goto out;

		ieee80211_tx_stats(dev, skb->len);

		ieee80211_xmit(sdata, sta, skb, 0);
	}
	goto out;
 out_free:
	kfree_skb(skb);
 out:
	rcu_read_unlock();
}

static int ieee80211_change_da(struct sk_buff *skb, struct sta_info *sta)
{
	struct ethhdr *eth;
	int err;

	err = skb_ensure_writable(skb, ETH_HLEN);
	if (unlikely(err))
		return err;

	eth = (void *)skb->data;
	ether_addr_copy(eth->h_dest, sta->sta.addr);

	return 0;
}

static bool ieee80211_multicast_to_unicast(struct sk_buff *skb,
					   struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	const struct ethhdr *eth = (void *)skb->data;
	const struct vlan_ethhdr *ethvlan = (void *)skb->data;
	__be16 ethertype;

	if (likely(!is_multicast_ether_addr(eth->h_dest)))
		return false;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		if (sdata->u.vlan.sta)
			return false;
		if (sdata->wdev.use_4addr)
			return false;
		/* fall through */
	case NL80211_IFTYPE_AP:
		/* check runtime toggle for this bss */
		if (!sdata->bss->multicast_to_unicast)
			return false;
		break;
	default:
		return false;
	}

	/* multicast to unicast conversion only for some payload */
	ethertype = eth->h_proto;
	if (ethertype == htons(ETH_P_8021Q) && skb->len >= VLAN_ETH_HLEN)
		ethertype = ethvlan->h_vlan_encapsulated_proto;
	switch (ethertype) {
	case htons(ETH_P_ARP):
	case htons(ETH_P_IP):
	case htons(ETH_P_IPV6):
		break;
	default:
		return false;
	}

	return true;
}

static void
ieee80211_convert_to_unicast(struct sk_buff *skb, struct net_device *dev,
			     struct sk_buff_head *queue)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	const struct ethhdr *eth = (struct ethhdr *)skb->data;
	struct sta_info *sta, *first = NULL;
	struct sk_buff *cloned_skb;

	rcu_read_lock();

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sdata != sta->sdata)
			/* AP-VLAN mismatch */
			continue;
		if (unlikely(ether_addr_equal(eth->h_source, sta->sta.addr)))
			/* do not send back to source */
			continue;
		if (!first) {
			first = sta;
			continue;
		}
		cloned_skb = skb_clone(skb, GFP_ATOMIC);
		if (!cloned_skb)
			goto multicast;
		if (unlikely(ieee80211_change_da(cloned_skb, sta))) {
			dev_kfree_skb(cloned_skb);
			goto multicast;
		}
		__skb_queue_tail(queue, cloned_skb);
	}

	if (likely(first)) {
		if (unlikely(ieee80211_change_da(skb, first)))
			goto multicast;
		__skb_queue_tail(queue, skb);
	} else {
		/* no STA connected, drop */
		kfree_skb(skb);
		skb = NULL;
	}

	goto out;
multicast:
	__skb_queue_purge(queue);
	__skb_queue_tail(queue, skb);
out:
	rcu_read_unlock();
}

/**
 * ieee80211_subif_start_xmit - netif start_xmit function for 802.3 vifs
 * @skb: packet to be sent
 * @dev: incoming interface
 *
 * On failure skb will be freed.
 */
netdev_tx_t ieee80211_subif_start_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	if (unlikely(ieee80211_multicast_to_unicast(skb, dev))) {
		struct sk_buff_head queue;

		__skb_queue_head_init(&queue);
		ieee80211_convert_to_unicast(skb, dev, &queue);
		while ((skb = __skb_dequeue(&queue)))
			__ieee80211_subif_start_xmit(skb, dev, 0, 0);
	} else {
		__ieee80211_subif_start_xmit(skb, dev, 0, 0);
	}

	return NETDEV_TX_OK;
}

struct sk_buff *
ieee80211_build_data_template(struct ieee80211_sub_if_data *sdata,
			      struct sk_buff *skb, u32 info_flags)
{
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_data tx = {
		.local = sdata->local,
		.sdata = sdata,
	};
	struct sta_info *sta;

	rcu_read_lock();

	if (ieee80211_lookup_ra_sta(sdata, skb, &sta)) {
		kfree_skb(skb);
		skb = ERR_PTR(-EINVAL);
		goto out;
	}

	skb = ieee80211_build_hdr(sdata, skb, info_flags, sta, 0);
	if (IS_ERR(skb))
		goto out;

	hdr = (void *)skb->data;
	tx.sta = sta_info_get(sdata, hdr->addr1);
	tx.skb = skb;

	if (ieee80211_tx_h_select_key(&tx) != TX_CONTINUE) {
		rcu_read_unlock();
		kfree_skb(skb);
		return ERR_PTR(-EINVAL);
	}

out:
	rcu_read_unlock();
	return skb;
}

/*
 * ieee80211_clear_tx_pending may not be called in a context where
 * it is possible that it packets could come in again.
 */
void ieee80211_clear_tx_pending(struct ieee80211_local *local)
{
	struct sk_buff *skb;
	int i;

	for (i = 0; i < local->hw.queues; i++) {
		while ((skb = skb_dequeue(&local->pending[i])) != NULL)
			ieee80211_free_txskb(&local->hw, skb);
	}
}

/*
 * Returns false if the frame couldn't be transmitted but was queued instead,
 * which in this case means re-queued -- take as an indication to stop sending
 * more pending frames.
 */
static bool ieee80211_tx_pending_skb(struct ieee80211_local *local,
				     struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	struct ieee80211_hdr *hdr;
	bool result;
	struct ieee80211_chanctx_conf *chanctx_conf;

	sdata = vif_to_sdata(info->control.vif);

	if (info->flags & IEEE80211_TX_INTFL_NEED_TXPROCESSING) {
		chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
		if (unlikely(!chanctx_conf)) {
			dev_kfree_skb(skb);
			return true;
		}
		info->band = chanctx_conf->def.chan->band;
		result = ieee80211_tx(sdata, NULL, skb, true, 0);
	} else {
		struct sk_buff_head skbs;

		__skb_queue_head_init(&skbs);
		__skb_queue_tail(&skbs, skb);

		hdr = (struct ieee80211_hdr *)skb->data;
		sta = sta_info_get(sdata, hdr->addr1);

		result = __ieee80211_tx(local, &skbs, skb->len, sta, true);
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

			if (WARN_ON(!info->control.vif)) {
				ieee80211_free_txskb(&local->hw, skb);
				continue;
			}

			spin_unlock_irqrestore(&local->queue_stop_reason_lock,
						flags);

			txok = ieee80211_tx_pending_skb(local, skb);
			spin_lock_irqsave(&local->queue_stop_reason_lock,
					  flags);
			if (!txok)
				break;
		}

		if (skb_queue_empty(&local->pending[i]))
			ieee80211_propagate_queue_wake(local, i);
	}
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);

	rcu_read_unlock();
}

/* functions for drivers to get certain frames */

static void __ieee80211_beacon_add_tim(struct ieee80211_sub_if_data *sdata,
				       struct ps_data *ps, struct sk_buff *skb,
				       bool is_template)
{
	u8 *pos, *tim;
	int aid0 = 0;
	int i, have_bits = 0, n1, n2;

	/* Generate bitmap for TIM only if there are any STAs in power save
	 * mode. */
	if (atomic_read(&ps->num_sta_ps) > 0)
		/* in the hope that this is faster than
		 * checking byte-for-byte */
		have_bits = !bitmap_empty((unsigned long *)ps->tim,
					  IEEE80211_MAX_AID+1);
	if (!is_template) {
		if (ps->dtim_count == 0)
			ps->dtim_count = sdata->vif.bss_conf.dtim_period - 1;
		else
			ps->dtim_count--;
	}

	tim = pos = skb_put(skb, 6);
	*pos++ = WLAN_EID_TIM;
	*pos++ = 4;
	*pos++ = ps->dtim_count;
	*pos++ = sdata->vif.bss_conf.dtim_period;

	if (ps->dtim_count == 0 && !skb_queue_empty(&ps->bc_buf))
		aid0 = 1;

	ps->dtim_bc_mc = aid0 == 1;

	if (have_bits) {
		/* Find largest even number N1 so that bits numbered 1 through
		 * (N1 x 8) - 1 in the bitmap are 0 and number N2 so that bits
		 * (N2 + 1) x 8 through 2007 are 0. */
		n1 = 0;
		for (i = 0; i < IEEE80211_MAX_TIM_LEN; i++) {
			if (ps->tim[i]) {
				n1 = i & 0xfe;
				break;
			}
		}
		n2 = n1;
		for (i = IEEE80211_MAX_TIM_LEN - 1; i >= n1; i--) {
			if (ps->tim[i]) {
				n2 = i;
				break;
			}
		}

		/* Bitmap control */
		*pos++ = n1 | aid0;
		/* Part Virt Bitmap */
		skb_put(skb, n2 - n1);
		memcpy(pos, ps->tim + n1, n2 - n1 + 1);

		tim[1] = n2 - n1 + 4;
	} else {
		*pos++ = aid0; /* Bitmap control */
		*pos++ = 0; /* Part Virt Bitmap */
	}
}

static int ieee80211_beacon_add_tim(struct ieee80211_sub_if_data *sdata,
				    struct ps_data *ps, struct sk_buff *skb,
				    bool is_template)
{
	struct ieee80211_local *local = sdata->local;

	/*
	 * Not very nice, but we want to allow the driver to call
	 * ieee80211_beacon_get() as a response to the set_tim()
	 * callback. That, however, is already invoked under the
	 * sta_lock to guarantee consistent and race-free update
	 * of the tim bitmap in mac80211 and the driver.
	 */
	if (local->tim_in_locked_section) {
		__ieee80211_beacon_add_tim(sdata, ps, skb, is_template);
	} else {
		spin_lock_bh(&local->tim_lock);
		__ieee80211_beacon_add_tim(sdata, ps, skb, is_template);
		spin_unlock_bh(&local->tim_lock);
	}

	return 0;
}

static void ieee80211_set_csa(struct ieee80211_sub_if_data *sdata,
			      struct beacon_data *beacon)
{
	struct probe_resp *resp;
	u8 *beacon_data;
	size_t beacon_data_len;
	int i;
	u8 count = beacon->csa_current_counter;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP:
		beacon_data = beacon->tail;
		beacon_data_len = beacon->tail_len;
		break;
	case NL80211_IFTYPE_ADHOC:
		beacon_data = beacon->head;
		beacon_data_len = beacon->head_len;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		beacon_data = beacon->head;
		beacon_data_len = beacon->head_len;
		break;
	default:
		return;
	}

	rcu_read_lock();
	for (i = 0; i < IEEE80211_MAX_CSA_COUNTERS_NUM; ++i) {
		resp = rcu_dereference(sdata->u.ap.probe_resp);

		if (beacon->csa_counter_offsets[i]) {
			if (WARN_ON_ONCE(beacon->csa_counter_offsets[i] >=
					 beacon_data_len)) {
				rcu_read_unlock();
				return;
			}

			beacon_data[beacon->csa_counter_offsets[i]] = count;
		}

		if (sdata->vif.type == NL80211_IFTYPE_AP && resp)
			resp->data[resp->csa_counter_offsets[i]] = count;
	}
	rcu_read_unlock();
}

static u8 __ieee80211_csa_update_counter(struct beacon_data *beacon)
{
	beacon->csa_current_counter--;

	/* the counter should never reach 0 */
	WARN_ON_ONCE(!beacon->csa_current_counter);

	return beacon->csa_current_counter;
}

u8 ieee80211_csa_update_counter(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct beacon_data *beacon = NULL;
	u8 count = 0;

	rcu_read_lock();

	if (sdata->vif.type == NL80211_IFTYPE_AP)
		beacon = rcu_dereference(sdata->u.ap.beacon);
	else if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		beacon = rcu_dereference(sdata->u.ibss.presp);
	else if (ieee80211_vif_is_mesh(&sdata->vif))
		beacon = rcu_dereference(sdata->u.mesh.beacon);

	if (!beacon)
		goto unlock;

	count = __ieee80211_csa_update_counter(beacon);

unlock:
	rcu_read_unlock();
	return count;
}
EXPORT_SYMBOL(ieee80211_csa_update_counter);

void ieee80211_csa_set_counter(struct ieee80211_vif *vif, u8 counter)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct beacon_data *beacon = NULL;

	rcu_read_lock();

	if (sdata->vif.type == NL80211_IFTYPE_AP)
		beacon = rcu_dereference(sdata->u.ap.beacon);
	else if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		beacon = rcu_dereference(sdata->u.ibss.presp);
	else if (ieee80211_vif_is_mesh(&sdata->vif))
		beacon = rcu_dereference(sdata->u.mesh.beacon);

	if (!beacon)
		goto unlock;

	if (counter < beacon->csa_current_counter)
		beacon->csa_current_counter = counter;

unlock:
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_csa_set_counter);

bool ieee80211_csa_is_complete(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct beacon_data *beacon = NULL;
	u8 *beacon_data;
	size_t beacon_data_len;
	int ret = false;

	if (!ieee80211_sdata_running(sdata))
		return false;

	rcu_read_lock();
	if (vif->type == NL80211_IFTYPE_AP) {
		struct ieee80211_if_ap *ap = &sdata->u.ap;

		beacon = rcu_dereference(ap->beacon);
		if (WARN_ON(!beacon || !beacon->tail))
			goto out;
		beacon_data = beacon->tail;
		beacon_data_len = beacon->tail_len;
	} else if (vif->type == NL80211_IFTYPE_ADHOC) {
		struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;

		beacon = rcu_dereference(ifibss->presp);
		if (!beacon)
			goto out;

		beacon_data = beacon->head;
		beacon_data_len = beacon->head_len;
	} else if (vif->type == NL80211_IFTYPE_MESH_POINT) {
		struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;

		beacon = rcu_dereference(ifmsh->beacon);
		if (!beacon)
			goto out;

		beacon_data = beacon->head;
		beacon_data_len = beacon->head_len;
	} else {
		WARN_ON(1);
		goto out;
	}

	if (!beacon->csa_counter_offsets[0])
		goto out;

	if (WARN_ON_ONCE(beacon->csa_counter_offsets[0] > beacon_data_len))
		goto out;

	if (beacon_data[beacon->csa_counter_offsets[0]] == 1)
		ret = true;
 out:
	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL(ieee80211_csa_is_complete);

static struct sk_buff *
__ieee80211_beacon_get(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif,
		       struct ieee80211_mutable_offsets *offs,
		       bool is_template)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct beacon_data *beacon = NULL;
	struct sk_buff *skb = NULL;
	struct ieee80211_tx_info *info;
	struct ieee80211_sub_if_data *sdata = NULL;
	enum nl80211_band band;
	struct ieee80211_tx_rate_control txrc;
	struct ieee80211_chanctx_conf *chanctx_conf;
	int csa_off_base = 0;

	rcu_read_lock();

	sdata = vif_to_sdata(vif);
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);

	if (!ieee80211_sdata_running(sdata) || !chanctx_conf)
		goto out;

	if (offs)
		memset(offs, 0, sizeof(*offs));

	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		struct ieee80211_if_ap *ap = &sdata->u.ap;

		beacon = rcu_dereference(ap->beacon);
		if (beacon) {
			if (beacon->csa_counter_offsets[0]) {
				if (!is_template)
					__ieee80211_csa_update_counter(beacon);

				ieee80211_set_csa(sdata, beacon);
			}

			/*
			 * headroom, head length,
			 * tail length and maximum TIM length
			 */
			skb = dev_alloc_skb(local->tx_headroom +
					    beacon->head_len +
					    beacon->tail_len + 256 +
					    local->hw.extra_beacon_tailroom);
			if (!skb)
				goto out;

			skb_reserve(skb, local->tx_headroom);
			skb_put_data(skb, beacon->head, beacon->head_len);

			ieee80211_beacon_add_tim(sdata, &ap->ps, skb,
						 is_template);

			if (offs) {
				offs->tim_offset = beacon->head_len;
				offs->tim_length = skb->len - beacon->head_len;

				/* for AP the csa offsets are from tail */
				csa_off_base = skb->len;
			}

			if (beacon->tail)
				skb_put_data(skb, beacon->tail,
					     beacon->tail_len);
		} else
			goto out;
	} else if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
		struct ieee80211_hdr *hdr;

		beacon = rcu_dereference(ifibss->presp);
		if (!beacon)
			goto out;

		if (beacon->csa_counter_offsets[0]) {
			if (!is_template)
				__ieee80211_csa_update_counter(beacon);

			ieee80211_set_csa(sdata, beacon);
		}

		skb = dev_alloc_skb(local->tx_headroom + beacon->head_len +
				    local->hw.extra_beacon_tailroom);
		if (!skb)
			goto out;
		skb_reserve(skb, local->tx_headroom);
		skb_put_data(skb, beacon->head, beacon->head_len);

		hdr = (struct ieee80211_hdr *) skb->data;
		hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						 IEEE80211_STYPE_BEACON);
	} else if (ieee80211_vif_is_mesh(&sdata->vif)) {
		struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;

		beacon = rcu_dereference(ifmsh->beacon);
		if (!beacon)
			goto out;

		if (beacon->csa_counter_offsets[0]) {
			if (!is_template)
				/* TODO: For mesh csa_counter is in TU, so
				 * decrementing it by one isn't correct, but
				 * for now we leave it consistent with overall
				 * mac80211's behavior.
				 */
				__ieee80211_csa_update_counter(beacon);

			ieee80211_set_csa(sdata, beacon);
		}

		if (ifmsh->sync_ops)
			ifmsh->sync_ops->adjust_tsf(sdata, beacon);

		skb = dev_alloc_skb(local->tx_headroom +
				    beacon->head_len +
				    256 + /* TIM IE */
				    beacon->tail_len +
				    local->hw.extra_beacon_tailroom);
		if (!skb)
			goto out;
		skb_reserve(skb, local->tx_headroom);
		skb_put_data(skb, beacon->head, beacon->head_len);
		ieee80211_beacon_add_tim(sdata, &ifmsh->ps, skb, is_template);

		if (offs) {
			offs->tim_offset = beacon->head_len;
			offs->tim_length = skb->len - beacon->head_len;
		}

		skb_put_data(skb, beacon->tail, beacon->tail_len);
	} else {
		WARN_ON(1);
		goto out;
	}

	/* CSA offsets */
	if (offs && beacon) {
		int i;

		for (i = 0; i < IEEE80211_MAX_CSA_COUNTERS_NUM; i++) {
			u16 csa_off = beacon->csa_counter_offsets[i];

			if (!csa_off)
				continue;

			offs->csa_counter_offs[i] = csa_off_base + csa_off;
		}
	}

	band = chanctx_conf->def.chan->band;

	info = IEEE80211_SKB_CB(skb);

	info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	info->flags |= IEEE80211_TX_CTL_NO_ACK;
	info->band = band;

	memset(&txrc, 0, sizeof(txrc));
	txrc.hw = hw;
	txrc.sband = local->hw.wiphy->bands[band];
	txrc.bss_conf = &sdata->vif.bss_conf;
	txrc.skb = skb;
	txrc.reported_rate.idx = -1;
	txrc.rate_idx_mask = sdata->rc_rateidx_mask[band];
	txrc.bss = true;
	rate_control_get_rate(sdata, NULL, &txrc);

	info->control.vif = vif;

	info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT |
			IEEE80211_TX_CTL_ASSIGN_SEQ |
			IEEE80211_TX_CTL_FIRST_FRAGMENT;
 out:
	rcu_read_unlock();
	return skb;

}

struct sk_buff *
ieee80211_beacon_get_template(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_mutable_offsets *offs)
{
	return __ieee80211_beacon_get(hw, vif, offs, true);
}
EXPORT_SYMBOL(ieee80211_beacon_get_template);

struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 u16 *tim_offset, u16 *tim_length)
{
	struct ieee80211_mutable_offsets offs = {};
	struct sk_buff *bcn = __ieee80211_beacon_get(hw, vif, &offs, false);
	struct sk_buff *copy;
	struct ieee80211_supported_band *sband;
	int shift;

	if (!bcn)
		return bcn;

	if (tim_offset)
		*tim_offset = offs.tim_offset;

	if (tim_length)
		*tim_length = offs.tim_length;

	if (ieee80211_hw_check(hw, BEACON_TX_STATUS) ||
	    !hw_to_local(hw)->monitors)
		return bcn;

	/* send a copy to monitor interfaces */
	copy = skb_copy(bcn, GFP_ATOMIC);
	if (!copy)
		return bcn;

	shift = ieee80211_vif_get_shift(vif);
	sband = ieee80211_get_sband(vif_to_sdata(vif));
	if (!sband)
		return bcn;

	ieee80211_tx_monitor(hw_to_local(hw), copy, sband, 1, shift, false);

	return bcn;
}
EXPORT_SYMBOL(ieee80211_beacon_get_tim);

struct sk_buff *ieee80211_proberesp_get(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif)
{
	struct ieee80211_if_ap *ap = NULL;
	struct sk_buff *skb = NULL;
	struct probe_resp *presp = NULL;
	struct ieee80211_hdr *hdr;
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	if (sdata->vif.type != NL80211_IFTYPE_AP)
		return NULL;

	rcu_read_lock();

	ap = &sdata->u.ap;
	presp = rcu_dereference(ap->probe_resp);
	if (!presp)
		goto out;

	skb = dev_alloc_skb(presp->len);
	if (!skb)
		goto out;

	skb_put_data(skb, presp->data, presp->len);

	hdr = (struct ieee80211_hdr *) skb->data;
	memset(hdr->addr1, 0, sizeof(hdr->addr1));

out:
	rcu_read_unlock();
	return skb;
}
EXPORT_SYMBOL(ieee80211_proberesp_get);

struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_managed *ifmgd;
	struct ieee80211_pspoll *pspoll;
	struct ieee80211_local *local;
	struct sk_buff *skb;

	if (WARN_ON(vif->type != NL80211_IFTYPE_STATION))
		return NULL;

	sdata = vif_to_sdata(vif);
	ifmgd = &sdata->u.mgd;
	local = sdata->local;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*pspoll));
	if (!skb)
		return NULL;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	pspoll = skb_put_zero(skb, sizeof(*pspoll));
	pspoll->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
					    IEEE80211_STYPE_PSPOLL);
	pspoll->aid = cpu_to_le16(ifmgd->aid);

	/* aid in PS-Poll has its two MSBs each set to 1 */
	pspoll->aid |= cpu_to_le16(1 << 15 | 1 << 14);

	memcpy(pspoll->bssid, ifmgd->bssid, ETH_ALEN);
	memcpy(pspoll->ta, vif->addr, ETH_ALEN);

	return skb;
}
EXPORT_SYMBOL(ieee80211_pspoll_get);

struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       bool qos_ok)
{
	struct ieee80211_hdr_3addr *nullfunc;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_managed *ifmgd;
	struct ieee80211_local *local;
	struct sk_buff *skb;
	bool qos = false;

	if (WARN_ON(vif->type != NL80211_IFTYPE_STATION))
		return NULL;

	sdata = vif_to_sdata(vif);
	ifmgd = &sdata->u.mgd;
	local = sdata->local;

	if (qos_ok) {
		struct sta_info *sta;

		rcu_read_lock();
		sta = sta_info_get(sdata, ifmgd->bssid);
		qos = sta && sta->sta.wme;
		rcu_read_unlock();
	}

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(*nullfunc) + 2);
	if (!skb)
		return NULL;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = skb_put_zero(skb, sizeof(*nullfunc));
	nullfunc->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					      IEEE80211_STYPE_NULLFUNC |
					      IEEE80211_FCTL_TODS);
	if (qos) {
		__le16 qos = cpu_to_le16(7);

		BUILD_BUG_ON((IEEE80211_STYPE_QOS_NULLFUNC |
			      IEEE80211_STYPE_NULLFUNC) !=
			     IEEE80211_STYPE_QOS_NULLFUNC);
		nullfunc->frame_control |=
			cpu_to_le16(IEEE80211_STYPE_QOS_NULLFUNC);
		skb->priority = 7;
		skb_set_queue_mapping(skb, IEEE80211_AC_VO);
		skb_put_data(skb, &qos, sizeof(qos));
	}

	memcpy(nullfunc->addr1, ifmgd->bssid, ETH_ALEN);
	memcpy(nullfunc->addr2, vif->addr, ETH_ALEN);
	memcpy(nullfunc->addr3, ifmgd->bssid, ETH_ALEN);

	return skb;
}
EXPORT_SYMBOL(ieee80211_nullfunc_get);

struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *hw,
				       const u8 *src_addr,
				       const u8 *ssid, size_t ssid_len,
				       size_t tailroom)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_hdr_3addr *hdr;
	struct sk_buff *skb;
	size_t ie_ssid_len;
	u8 *pos;

	ie_ssid_len = 2 + ssid_len;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*hdr) +
			    ie_ssid_len + tailroom);
	if (!skb)
		return NULL;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	hdr = skb_put_zero(skb, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					 IEEE80211_STYPE_PROBE_REQ);
	eth_broadcast_addr(hdr->addr1);
	memcpy(hdr->addr2, src_addr, ETH_ALEN);
	eth_broadcast_addr(hdr->addr3);

	pos = skb_put(skb, ie_ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = ssid_len;
	if (ssid_len)
		memcpy(pos, ssid, ssid_len);
	pos += ssid_len;

	return skb;
}
EXPORT_SYMBOL(ieee80211_probereq_get);

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
	struct ieee80211_tx_data tx;
	struct ieee80211_sub_if_data *sdata;
	struct ps_data *ps;
	struct ieee80211_tx_info *info;
	struct ieee80211_chanctx_conf *chanctx_conf;

	sdata = vif_to_sdata(vif);

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);

	if (!chanctx_conf)
		goto out;

	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		struct beacon_data *beacon =
				rcu_dereference(sdata->u.ap.beacon);

		if (!beacon || !beacon->head)
			goto out;

		ps = &sdata->u.ap.ps;
	} else if (ieee80211_vif_is_mesh(&sdata->vif)) {
		ps = &sdata->u.mesh.ps;
	} else {
		goto out;
	}

	if (ps->dtim_count != 0 || !ps->dtim_bc_mc)
		goto out; /* send buffered bc/mc only after DTIM beacon */

	while (1) {
		skb = skb_dequeue(&ps->bc_buf);
		if (!skb)
			goto out;
		local->total_ps_buffered--;

		if (!skb_queue_empty(&ps->bc_buf) && skb->len >= 2) {
			struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *) skb->data;
			/* more buffered multicast/broadcast frames ==> set
			 * MoreData flag in IEEE 802.11 header to inform PS
			 * STAs */
			hdr->frame_control |=
				cpu_to_le16(IEEE80211_FCTL_MOREDATA);
		}

		if (sdata->vif.type == NL80211_IFTYPE_AP)
			sdata = IEEE80211_DEV_TO_SUB_IF(skb->dev);
		if (!ieee80211_tx_prepare(sdata, &tx, NULL, skb))
			break;
		ieee80211_free_txskb(hw, skb);
	}

	info = IEEE80211_SKB_CB(skb);

	tx.flags |= IEEE80211_TX_PS_BUFFERED;
	info->band = chanctx_conf->def.chan->band;

	if (invoke_tx_handlers(&tx))
		skb = NULL;
 out:
	rcu_read_unlock();

	return skb;
}
EXPORT_SYMBOL(ieee80211_get_buffered_bc);

int ieee80211_reserve_tid(struct ieee80211_sta *pubsta, u8 tid)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	int ret;
	u32 queues;

	lockdep_assert_held(&local->sta_mtx);

	/* only some cases are supported right now */
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	if (WARN_ON(tid >= IEEE80211_NUM_UPS))
		return -EINVAL;

	if (sta->reserved_tid == tid) {
		ret = 0;
		goto out;
	}

	if (sta->reserved_tid != IEEE80211_TID_UNRESERVED) {
		sdata_err(sdata, "TID reservation already active\n");
		ret = -EALREADY;
		goto out;
	}

	ieee80211_stop_vif_queues(sdata->local, sdata,
				  IEEE80211_QUEUE_STOP_REASON_RESERVE_TID);

	synchronize_net();

	/* Tear down BA sessions so we stop aggregating on this TID */
	if (ieee80211_hw_check(&local->hw, AMPDU_AGGREGATION)) {
		set_sta_flag(sta, WLAN_STA_BLOCK_BA);
		__ieee80211_stop_tx_ba_session(sta, tid,
					       AGG_STOP_LOCAL_REQUEST);
	}

	queues = BIT(sdata->vif.hw_queue[ieee802_1d_to_ac[tid]]);
	__ieee80211_flush_queues(local, sdata, queues, false);

	sta->reserved_tid = tid;

	ieee80211_wake_vif_queues(local, sdata,
				  IEEE80211_QUEUE_STOP_REASON_RESERVE_TID);

	if (ieee80211_hw_check(&local->hw, AMPDU_AGGREGATION))
		clear_sta_flag(sta, WLAN_STA_BLOCK_BA);

	ret = 0;
 out:
	return ret;
}
EXPORT_SYMBOL(ieee80211_reserve_tid);

void ieee80211_unreserve_tid(struct ieee80211_sta *pubsta, u8 tid)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	struct ieee80211_sub_if_data *sdata = sta->sdata;

	lockdep_assert_held(&sdata->local->sta_mtx);

	/* only some cases are supported right now */
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
		break;
	default:
		WARN_ON(1);
		return;
	}

	if (tid != sta->reserved_tid) {
		sdata_err(sdata, "TID to unreserve (%d) isn't reserved\n", tid);
		return;
	}

	sta->reserved_tid = IEEE80211_TID_UNRESERVED;
}
EXPORT_SYMBOL(ieee80211_unreserve_tid);

void __ieee80211_tx_skb_tid_band(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb, int tid,
				 enum nl80211_band band, u32 txdata_flags)
{
	int ac = ieee80211_ac_from_tid(tid);

	skb_reset_mac_header(skb);
	skb_set_queue_mapping(skb, ac);
	skb->priority = tid;

	skb->dev = sdata->dev;

	/*
	 * The other path calling ieee80211_xmit is from the tasklet,
	 * and while we can handle concurrent transmissions locking
	 * requirements are that we do not come into tx with bhs on.
	 */
	local_bh_disable();
	IEEE80211_SKB_CB(skb)->band = band;
	ieee80211_xmit(sdata, NULL, skb, txdata_flags);
	local_bh_enable();
}

int ieee80211_tx_control_port(struct wiphy *wiphy, struct net_device *dev,
			      const u8 *buf, size_t len,
			      const u8 *dest, __be16 proto, bool unencrypted)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ethhdr *ehdr;
	u32 ctrl_flags = 0;
	u32 flags;

	/* Only accept CONTROL_PORT_PROTOCOL configured in CONNECT/ASSOCIATE
	 * or Pre-Authentication
	 */
	if (proto != sdata->control_port_protocol &&
	    proto != cpu_to_be16(ETH_P_PREAUTH))
		return -EINVAL;

	if (proto == sdata->control_port_protocol)
		ctrl_flags |= IEEE80211_TX_CTRL_PORT_CTRL_PROTO;

	if (unencrypted)
		flags = IEEE80211_TX_INTFL_DONT_ENCRYPT;
	else
		flags = 0;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(struct ethhdr) + len);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, local->hw.extra_tx_headroom + sizeof(struct ethhdr));

	skb_put_data(skb, buf, len);

	ehdr = skb_push(skb, sizeof(struct ethhdr));
	memcpy(ehdr->h_dest, dest, ETH_ALEN);
	memcpy(ehdr->h_source, sdata->vif.addr, ETH_ALEN);
	ehdr->h_proto = proto;

	skb->dev = dev;
	skb->protocol = htons(ETH_P_802_3);
	skb_reset_network_header(skb);
	skb_reset_mac_header(skb);

	local_bh_disable();
	__ieee80211_subif_start_xmit(skb, skb->dev, flags, ctrl_flags);
	local_bh_enable();

	return 0;
}
