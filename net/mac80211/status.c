// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2008-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 */

#include <linux/export.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>
#include "ieee80211_i.h"
#include "rate.h"
#include "mesh.h"
#include "led.h"
#include "wme.h"


void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw,
				 struct sk_buff *skb)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int tmp;

	skb->pkt_type = IEEE80211_TX_STATUS_MSG;
	skb_queue_tail(info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS ?
		       &local->skb_queue : &local->skb_queue_unreliable, skb);
	tmp = skb_queue_len(&local->skb_queue) +
		skb_queue_len(&local->skb_queue_unreliable);
	while (tmp > IEEE80211_IRQSAFE_QUEUE_LIMIT &&
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		ieee80211_free_txskb(hw, skb);
		tmp--;
		I802_DEBUG_INC(local->tx_status_drop);
	}
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_tx_status_irqsafe);

static void ieee80211_handle_filtered_frame(struct ieee80211_local *local,
					    struct sta_info *sta,
					    struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	int ac;

	if (info->flags & (IEEE80211_TX_CTL_NO_PS_BUFFER |
			   IEEE80211_TX_CTL_AMPDU)) {
		ieee80211_free_txskb(&local->hw, skb);
		return;
	}

	/*
	 * This skb 'survived' a round-trip through the driver, and
	 * hopefully the driver didn't mangle it too badly. However,
	 * we can definitely not rely on the control information
	 * being correct. Clear it so we don't get junk there, and
	 * indicate that it needs new processing, but must not be
	 * modified/encrypted again.
	 */
	memset(&info->control, 0, sizeof(info->control));

	info->control.jiffies = jiffies;
	info->control.vif = &sta->sdata->vif;
	info->flags |= IEEE80211_TX_INTFL_NEED_TXPROCESSING |
		       IEEE80211_TX_INTFL_RETRANSMISSION;
	info->flags &= ~IEEE80211_TX_TEMPORARY_FLAGS;

	sta->status_stats.filtered++;

	/*
	 * Clear more-data bit on filtered frames, it might be set
	 * but later frames might time out so it might have to be
	 * clear again ... It's all rather unlikely (this frame
	 * should time out first, right?) but let's not confuse
	 * peers unnecessarily.
	 */
	if (hdr->frame_control & cpu_to_le16(IEEE80211_FCTL_MOREDATA))
		hdr->frame_control &= ~cpu_to_le16(IEEE80211_FCTL_MOREDATA);

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *p = ieee80211_get_qos_ctl(hdr);
		int tid = *p & IEEE80211_QOS_CTL_TID_MASK;

		/*
		 * Clear EOSP if set, this could happen e.g.
		 * if an absence period (us being a P2P GO)
		 * shortens the SP.
		 */
		if (*p & IEEE80211_QOS_CTL_EOSP)
			*p &= ~IEEE80211_QOS_CTL_EOSP;
		ac = ieee80211_ac_from_tid(tid);
	} else {
		ac = IEEE80211_AC_BE;
	}

	/*
	 * Clear the TX filter mask for this STA when sending the next
	 * packet. If the STA went to power save mode, this will happen
	 * when it wakes up for the next time.
	 */
	set_sta_flag(sta, WLAN_STA_CLEAR_PS_FILT);
	ieee80211_clear_fast_xmit(sta);

	/*
	 * This code races in the following way:
	 *
	 *  (1) STA sends frame indicating it will go to sleep and does so
	 *  (2) hardware/firmware adds STA to filter list, passes frame up
	 *  (3) hardware/firmware processes TX fifo and suppresses a frame
	 *  (4) we get TX status before having processed the frame and
	 *	knowing that the STA has gone to sleep.
	 *
	 * This is actually quite unlikely even when both those events are
	 * processed from interrupts coming in quickly after one another or
	 * even at the same time because we queue both TX status events and
	 * RX frames to be processed by a tasklet and process them in the
	 * same order that they were received or TX status last. Hence, there
	 * is no race as long as the frame RX is processed before the next TX
	 * status, which drivers can ensure, see below.
	 *
	 * Note that this can only happen if the hardware or firmware can
	 * actually add STAs to the filter list, if this is done by the
	 * driver in response to set_tim() (which will only reduce the race
	 * this whole filtering tries to solve, not completely solve it)
	 * this situation cannot happen.
	 *
	 * To completely solve this race drivers need to make sure that they
	 *  (a) don't mix the irq-safe/not irq-safe TX status/RX processing
	 *	functions and
	 *  (b) always process RX events before TX status events if ordering
	 *      can be unknown, for example with different interrupt status
	 *	bits.
	 *  (c) if PS mode transitions are manual (i.e. the flag
	 *      %IEEE80211_HW_AP_LINK_PS is set), always process PS state
	 *      changes before calling TX status events if ordering can be
	 *	unknown.
	 */
	if (test_sta_flag(sta, WLAN_STA_PS_STA) &&
	    skb_queue_len(&sta->tx_filtered[ac]) < STA_MAX_TX_BUFFER) {
		skb_queue_tail(&sta->tx_filtered[ac], skb);
		sta_info_recalc_tim(sta);

		if (!timer_pending(&local->sta_cleanup))
			mod_timer(&local->sta_cleanup,
				  round_jiffies(jiffies +
						STA_INFO_CLEANUP_INTERVAL));
		return;
	}

	if (!test_sta_flag(sta, WLAN_STA_PS_STA) &&
	    !(info->flags & IEEE80211_TX_INTFL_RETRIED)) {
		/* Software retry the packet once */
		info->flags |= IEEE80211_TX_INTFL_RETRIED;
		ieee80211_add_pending_skb(local, skb);
		return;
	}

	ps_dbg_ratelimited(sta->sdata,
			   "dropped TX filtered frame, queue_len=%d PS=%d @%lu\n",
			   skb_queue_len(&sta->tx_filtered[ac]),
			   !!test_sta_flag(sta, WLAN_STA_PS_STA), jiffies);
	ieee80211_free_txskb(&local->hw, skb);
}

static void ieee80211_check_pending_bar(struct sta_info *sta, u8 *addr, u8 tid)
{
	struct tid_ampdu_tx *tid_tx;

	tid_tx = rcu_dereference(sta->ampdu_mlme.tid_tx[tid]);
	if (!tid_tx || !tid_tx->bar_pending)
		return;

	tid_tx->bar_pending = false;
	ieee80211_send_bar(&sta->sdata->vif, addr, tid, tid_tx->failed_bar_ssn);
}

static void ieee80211_frame_acked(struct sta_info *sta, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *) skb->data;
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_tx_info *txinfo = IEEE80211_SKB_CB(skb);

	if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS)) {
		sta->status_stats.last_ack = jiffies;
		if (txinfo->status.is_valid_ack_signal) {
			sta->status_stats.last_ack_signal =
					 (s8)txinfo->status.ack_signal;
			sta->status_stats.ack_signal_filled = true;
			ewma_avg_signal_add(&sta->status_stats.avg_ack_signal,
					    -txinfo->status.ack_signal);
		}
	}

	if (ieee80211_is_data_qos(mgmt->frame_control)) {
		struct ieee80211_hdr *hdr = (void *) skb->data;
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		u16 tid = qc[0] & 0xf;

		ieee80211_check_pending_bar(sta, hdr->addr1, tid);
	}

	if (ieee80211_is_action(mgmt->frame_control) &&
	    !ieee80211_has_protected(mgmt->frame_control) &&
	    mgmt->u.action.category == WLAN_CATEGORY_HT &&
	    mgmt->u.action.u.ht_smps.action == WLAN_HT_ACTION_SMPS &&
	    ieee80211_sdata_running(sdata)) {
		enum ieee80211_smps_mode smps_mode;

		switch (mgmt->u.action.u.ht_smps.smps_control) {
		case WLAN_HT_SMPS_CONTROL_DYNAMIC:
			smps_mode = IEEE80211_SMPS_DYNAMIC;
			break;
		case WLAN_HT_SMPS_CONTROL_STATIC:
			smps_mode = IEEE80211_SMPS_STATIC;
			break;
		case WLAN_HT_SMPS_CONTROL_DISABLED:
		default: /* shouldn't happen since we don't send that */
			smps_mode = IEEE80211_SMPS_OFF;
			break;
		}

		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			/*
			 * This update looks racy, but isn't -- if we come
			 * here we've definitely got a station that we're
			 * talking to, and on a managed interface that can
			 * only be the AP. And the only other place updating
			 * this variable in managed mode is before association.
			 */
			sdata->smps_mode = smps_mode;
			ieee80211_queue_work(&local->hw, &sdata->recalc_smps);
		} else if (sdata->vif.type == NL80211_IFTYPE_AP ||
			   sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
			sta->known_smps_mode = smps_mode;
		}
	}
}

static void ieee80211_set_bar_pending(struct sta_info *sta, u8 tid, u16 ssn)
{
	struct tid_ampdu_tx *tid_tx;

	tid_tx = rcu_dereference(sta->ampdu_mlme.tid_tx[tid]);
	if (!tid_tx)
		return;

	tid_tx->failed_bar_ssn = ssn;
	tid_tx->bar_pending = true;
}

static int ieee80211_tx_radiotap_len(struct ieee80211_tx_info *info,
				     struct ieee80211_tx_status *status)
{
	int len = sizeof(struct ieee80211_radiotap_header);

	/* IEEE80211_RADIOTAP_RATE rate */
	if (status && status->rate && !(status->rate->flags &
					(RATE_INFO_FLAGS_MCS |
					 RATE_INFO_FLAGS_DMG |
					 RATE_INFO_FLAGS_EDMG |
					 RATE_INFO_FLAGS_VHT_MCS |
					 RATE_INFO_FLAGS_HE_MCS)))
		len += 2;
	else if (info->status.rates[0].idx >= 0 &&
		 !(info->status.rates[0].flags &
		   (IEEE80211_TX_RC_MCS | IEEE80211_TX_RC_VHT_MCS)))
		len += 2;

	/* IEEE80211_RADIOTAP_TX_FLAGS */
	len += 2;

	/* IEEE80211_RADIOTAP_DATA_RETRIES */
	len += 1;

	/* IEEE80211_RADIOTAP_MCS
	 * IEEE80211_RADIOTAP_VHT */
	if (status && status->rate) {
		if (status->rate->flags & RATE_INFO_FLAGS_MCS)
			len += 3;
		else if (status->rate->flags & RATE_INFO_FLAGS_VHT_MCS)
			len = ALIGN(len, 2) + 12;
		else if (status->rate->flags & RATE_INFO_FLAGS_HE_MCS)
			len = ALIGN(len, 2) + 12;
	} else if (info->status.rates[0].idx >= 0) {
		if (info->status.rates[0].flags & IEEE80211_TX_RC_MCS)
			len += 3;
		else if (info->status.rates[0].flags & IEEE80211_TX_RC_VHT_MCS)
			len = ALIGN(len, 2) + 12;
	}

	return len;
}

static void
ieee80211_add_tx_radiotap_header(struct ieee80211_local *local,
				 struct ieee80211_supported_band *sband,
				 struct sk_buff *skb, int retry_count,
				 int rtap_len, int shift,
				 struct ieee80211_tx_status *status)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_radiotap_header *rthdr;
	unsigned char *pos;
	u16 legacy_rate = 0;
	u16 txflags;

	rthdr = skb_push(skb, rtap_len);

	memset(rthdr, 0, rtap_len);
	rthdr->it_len = cpu_to_le16(rtap_len);
	rthdr->it_present =
		cpu_to_le32((1 << IEEE80211_RADIOTAP_TX_FLAGS) |
			    (1 << IEEE80211_RADIOTAP_DATA_RETRIES));
	pos = (unsigned char *)(rthdr + 1);

	/*
	 * XXX: Once radiotap gets the bitmap reset thing the vendor
	 *	extensions proposal contains, we can actually report
	 *	the whole set of tries we did.
	 */

	/* IEEE80211_RADIOTAP_RATE */

	if (status && status->rate) {
		if (!(status->rate->flags & (RATE_INFO_FLAGS_MCS |
					     RATE_INFO_FLAGS_DMG |
					     RATE_INFO_FLAGS_EDMG |
					     RATE_INFO_FLAGS_VHT_MCS |
					     RATE_INFO_FLAGS_HE_MCS)))
			legacy_rate = status->rate->legacy;
	} else if (info->status.rates[0].idx >= 0 &&
		 !(info->status.rates[0].flags & (IEEE80211_TX_RC_MCS |
						  IEEE80211_TX_RC_VHT_MCS)))
		legacy_rate =
			sband->bitrates[info->status.rates[0].idx].bitrate;

	if (legacy_rate) {
		rthdr->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_RATE);
		*pos = DIV_ROUND_UP(legacy_rate, 5 * (1 << shift));
		/* padding for tx flags */
		pos += 2;
	}

	/* IEEE80211_RADIOTAP_TX_FLAGS */
	txflags = 0;
	if (!(info->flags & IEEE80211_TX_STAT_ACK) &&
	    !is_multicast_ether_addr(hdr->addr1))
		txflags |= IEEE80211_RADIOTAP_F_TX_FAIL;

	if (info->status.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
		txflags |= IEEE80211_RADIOTAP_F_TX_CTS;
	if (info->status.rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS)
		txflags |= IEEE80211_RADIOTAP_F_TX_RTS;

	put_unaligned_le16(txflags, pos);
	pos += 2;

	/* IEEE80211_RADIOTAP_DATA_RETRIES */
	/* for now report the total retry_count */
	*pos = retry_count;
	pos++;

	if (status && status->rate &&
	    (status->rate->flags & RATE_INFO_FLAGS_MCS)) {
		rthdr->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_MCS);
		pos[0] = IEEE80211_RADIOTAP_MCS_HAVE_MCS |
			 IEEE80211_RADIOTAP_MCS_HAVE_GI |
			 IEEE80211_RADIOTAP_MCS_HAVE_BW;
		if (status->rate->flags & RATE_INFO_FLAGS_SHORT_GI)
			pos[1] |= IEEE80211_RADIOTAP_MCS_SGI;
		if (status->rate->bw == RATE_INFO_BW_40)
			pos[1] |= IEEE80211_RADIOTAP_MCS_BW_40;
		pos[2] = status->rate->mcs;
		pos += 3;
	} else if (status && status->rate &&
		   (status->rate->flags & RATE_INFO_FLAGS_VHT_MCS)) {
		u16 known = local->hw.radiotap_vht_details &
			(IEEE80211_RADIOTAP_VHT_KNOWN_GI |
			 IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH);

		rthdr->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_VHT);

		/* required alignment from rthdr */
		pos = (u8 *)rthdr + ALIGN(pos - (u8 *)rthdr, 2);

		/* u16 known - IEEE80211_RADIOTAP_VHT_KNOWN_* */
		put_unaligned_le16(known, pos);
		pos += 2;

		/* u8 flags - IEEE80211_RADIOTAP_VHT_FLAG_* */
		if (status->rate->flags & RATE_INFO_FLAGS_SHORT_GI)
			*pos |= IEEE80211_RADIOTAP_VHT_FLAG_SGI;
		pos++;

		/* u8 bandwidth */
		switch (status->rate->bw) {
		case RATE_INFO_BW_160:
			*pos = 11;
			break;
		case RATE_INFO_BW_80:
			*pos = 4;
			break;
		case RATE_INFO_BW_40:
			*pos = 1;
			break;
		default:
			*pos = 0;
			break;
		}
		pos++;

		/* u8 mcs_nss[4] */
		*pos = (status->rate->mcs << 4) | status->rate->nss;
		pos += 4;

		/* u8 coding */
		pos++;
		/* u8 group_id */
		pos++;
		/* u16 partial_aid */
		pos += 2;
	} else if (status && status->rate &&
		   (status->rate->flags & RATE_INFO_FLAGS_HE_MCS)) {
		struct ieee80211_radiotap_he *he;

		rthdr->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_HE);

		/* required alignment from rthdr */
		pos = (u8 *)rthdr + ALIGN(pos - (u8 *)rthdr, 2);
		he = (struct ieee80211_radiotap_he *)pos;

		he->data1 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_FORMAT_SU |
					IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN |
					IEEE80211_RADIOTAP_HE_DATA1_DATA_DCM_KNOWN |
					IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN);

		he->data2 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN);

#define HE_PREP(f, val) le16_encode_bits(val, IEEE80211_RADIOTAP_HE_##f)

		he->data6 |= HE_PREP(DATA6_NSTS, status->rate->nss);

#define CHECK_GI(s) \
	BUILD_BUG_ON(IEEE80211_RADIOTAP_HE_DATA5_GI_##s != \
	(int)NL80211_RATE_INFO_HE_GI_##s)

		CHECK_GI(0_8);
		CHECK_GI(1_6);
		CHECK_GI(3_2);

		he->data3 |= HE_PREP(DATA3_DATA_MCS, status->rate->mcs);
		he->data3 |= HE_PREP(DATA3_DATA_DCM, status->rate->he_dcm);

		he->data5 |= HE_PREP(DATA5_GI, status->rate->he_gi);

		switch (status->rate->bw) {
		case RATE_INFO_BW_20:
			he->data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
					     IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_20MHZ);
			break;
		case RATE_INFO_BW_40:
			he->data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
					     IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_40MHZ);
			break;
		case RATE_INFO_BW_80:
			he->data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
					     IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_80MHZ);
			break;
		case RATE_INFO_BW_160:
			he->data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
					     IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_160MHZ);
			break;
		case RATE_INFO_BW_HE_RU:
#define CHECK_RU_ALLOC(s) \
	BUILD_BUG_ON(IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_##s##T != \
	NL80211_RATE_INFO_HE_RU_ALLOC_##s + 4)

			CHECK_RU_ALLOC(26);
			CHECK_RU_ALLOC(52);
			CHECK_RU_ALLOC(106);
			CHECK_RU_ALLOC(242);
			CHECK_RU_ALLOC(484);
			CHECK_RU_ALLOC(996);
			CHECK_RU_ALLOC(2x996);

			he->data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
					     status->rate->he_ru_alloc + 4);
			break;
		default:
			WARN_ONCE(1, "Invalid SU BW %d\n", status->rate->bw);
		}

		pos += sizeof(struct ieee80211_radiotap_he);
	}

	if ((status && status->rate) || info->status.rates[0].idx < 0)
		return;

	/* IEEE80211_RADIOTAP_MCS
	 * IEEE80211_RADIOTAP_VHT */
	if (info->status.rates[0].flags & IEEE80211_TX_RC_MCS) {
		rthdr->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_MCS);
		pos[0] = IEEE80211_RADIOTAP_MCS_HAVE_MCS |
			 IEEE80211_RADIOTAP_MCS_HAVE_GI |
			 IEEE80211_RADIOTAP_MCS_HAVE_BW;
		if (info->status.rates[0].flags & IEEE80211_TX_RC_SHORT_GI)
			pos[1] |= IEEE80211_RADIOTAP_MCS_SGI;
		if (info->status.rates[0].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			pos[1] |= IEEE80211_RADIOTAP_MCS_BW_40;
		if (info->status.rates[0].flags & IEEE80211_TX_RC_GREEN_FIELD)
			pos[1] |= IEEE80211_RADIOTAP_MCS_FMT_GF;
		pos[2] = info->status.rates[0].idx;
		pos += 3;
	} else if (info->status.rates[0].flags & IEEE80211_TX_RC_VHT_MCS) {
		u16 known = local->hw.radiotap_vht_details &
			(IEEE80211_RADIOTAP_VHT_KNOWN_GI |
			 IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH);

		rthdr->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_VHT);

		/* required alignment from rthdr */
		pos = (u8 *)rthdr + ALIGN(pos - (u8 *)rthdr, 2);

		/* u16 known - IEEE80211_RADIOTAP_VHT_KNOWN_* */
		put_unaligned_le16(known, pos);
		pos += 2;

		/* u8 flags - IEEE80211_RADIOTAP_VHT_FLAG_* */
		if (info->status.rates[0].flags & IEEE80211_TX_RC_SHORT_GI)
			*pos |= IEEE80211_RADIOTAP_VHT_FLAG_SGI;
		pos++;

		/* u8 bandwidth */
		if (info->status.rates[0].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			*pos = 1;
		else if (info->status.rates[0].flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			*pos = 4;
		else if (info->status.rates[0].flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
			*pos = 11;
		else /* IEEE80211_TX_RC_{20_MHZ_WIDTH,FIXME:DUP_DATA} */
			*pos = 0;
		pos++;

		/* u8 mcs_nss[4] */
		*pos = (ieee80211_rate_get_vht_mcs(&info->status.rates[0]) << 4) |
			ieee80211_rate_get_vht_nss(&info->status.rates[0]);
		pos += 4;

		/* u8 coding */
		pos++;
		/* u8 group_id */
		pos++;
		/* u16 partial_aid */
		pos += 2;
	}
}

/*
 * Handles the tx for TDLS teardown frames.
 * If the frame wasn't ACKed by the peer - it will be re-sent through the AP
 */
static void ieee80211_tdls_td_tx_handle(struct ieee80211_local *local,
					struct ieee80211_sub_if_data *sdata,
					struct sk_buff *skb, u32 flags)
{
	struct sk_buff *teardown_skb;
	struct sk_buff *orig_teardown_skb;
	bool is_teardown = false;

	/* Get the teardown data we need and free the lock */
	spin_lock(&sdata->u.mgd.teardown_lock);
	teardown_skb = sdata->u.mgd.teardown_skb;
	orig_teardown_skb = sdata->u.mgd.orig_teardown_skb;
	if ((skb == orig_teardown_skb) && teardown_skb) {
		sdata->u.mgd.teardown_skb = NULL;
		sdata->u.mgd.orig_teardown_skb = NULL;
		is_teardown = true;
	}
	spin_unlock(&sdata->u.mgd.teardown_lock);

	if (is_teardown) {
		/* This mechanism relies on being able to get ACKs */
		WARN_ON(!ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS));

		/* Check if peer has ACKed */
		if (flags & IEEE80211_TX_STAT_ACK) {
			dev_kfree_skb_any(teardown_skb);
		} else {
			tdls_dbg(sdata,
				 "TDLS Resending teardown through AP\n");

			ieee80211_subif_start_xmit(teardown_skb, skb->dev);
		}
	}
}

static struct ieee80211_sub_if_data *
ieee80211_sdata_from_skb(struct ieee80211_local *local, struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata;

	if (skb->dev) {
		list_for_each_entry_rcu(sdata, &local->interfaces, list) {
			if (!sdata->dev)
				continue;

			if (skb->dev == sdata->dev)
				return sdata;
		}

		return NULL;
	}

	return rcu_dereference(local->p2p_sdata);
}

static void ieee80211_report_ack_skb(struct ieee80211_local *local,
				     struct ieee80211_tx_info *info,
				     bool acked, bool dropped)
{
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&local->ack_status_lock, flags);
	skb = idr_remove(&local->ack_status_frames, info->ack_frame_id);
	spin_unlock_irqrestore(&local->ack_status_lock, flags);

	if (!skb)
		return;

	if (info->flags & IEEE80211_TX_INTFL_NL80211_FRAME_TX) {
		u64 cookie = IEEE80211_SKB_CB(skb)->ack.cookie;
		struct ieee80211_sub_if_data *sdata;
		struct ieee80211_hdr *hdr = (void *)skb->data;

		rcu_read_lock();
		sdata = ieee80211_sdata_from_skb(local, skb);
		if (sdata) {
			if (ieee80211_is_nullfunc(hdr->frame_control) ||
			    ieee80211_is_qos_nullfunc(hdr->frame_control))
				cfg80211_probe_status(sdata->dev, hdr->addr1,
						      cookie, acked,
						      info->status.ack_signal,
						      info->status.is_valid_ack_signal,
						      GFP_ATOMIC);
			else
				cfg80211_mgmt_tx_status(&sdata->wdev, cookie,
							skb->data, skb->len,
							acked, GFP_ATOMIC);
		}
		rcu_read_unlock();

		dev_kfree_skb_any(skb);
	} else if (dropped) {
		dev_kfree_skb_any(skb);
	} else {
		/* consumes skb */
		skb_complete_wifi_ack(skb, acked);
	}
}

static void ieee80211_report_used_skb(struct ieee80211_local *local,
				      struct sk_buff *skb, bool dropped)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u16 tx_time_est = ieee80211_info_get_tx_time_est(info);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	bool acked = info->flags & IEEE80211_TX_STAT_ACK;

	if (dropped)
		acked = false;

	if (tx_time_est) {
		struct sta_info *sta;

		rcu_read_lock();

		sta = sta_info_get_by_addrs(local, hdr->addr1, hdr->addr2);
		ieee80211_sta_update_pending_airtime(local, sta,
						     skb_get_queue_mapping(skb),
						     tx_time_est,
						     true);
		rcu_read_unlock();
	}

	if (info->flags & IEEE80211_TX_INTFL_MLME_CONN_TX) {
		struct ieee80211_sub_if_data *sdata;

		rcu_read_lock();

		sdata = ieee80211_sdata_from_skb(local, skb);

		if (!sdata) {
			skb->dev = NULL;
		} else {
			unsigned int hdr_size =
				ieee80211_hdrlen(hdr->frame_control);

			/* Check to see if packet is a TDLS teardown packet */
			if (ieee80211_is_data(hdr->frame_control) &&
			    (ieee80211_get_tdls_action(skb, hdr_size) ==
			     WLAN_TDLS_TEARDOWN))
				ieee80211_tdls_td_tx_handle(local, sdata, skb,
							    info->flags);
			else
				ieee80211_mgd_conn_tx_status(sdata,
							     hdr->frame_control,
							     acked);
		}

		rcu_read_unlock();
	} else if (info->ack_frame_id) {
		ieee80211_report_ack_skb(local, info, acked, dropped);
	}

	if (!dropped && skb->destructor) {
		skb->wifi_acked_valid = 1;
		skb->wifi_acked = acked;
	}

	ieee80211_led_tx(local);

	if (skb_has_frag_list(skb)) {
		kfree_skb_list(skb_shinfo(skb)->frag_list);
		skb_shinfo(skb)->frag_list = NULL;
	}
}

/*
 * Use a static threshold for now, best value to be determined
 * by testing ...
 * Should it depend on:
 *  - on # of retransmissions
 *  - current throughput (higher value for higher tpt)?
 */
#define STA_LOST_PKT_THRESHOLD	50
#define STA_LOST_TDLS_PKT_THRESHOLD	10
#define STA_LOST_TDLS_PKT_TIME		(10*HZ) /* 10secs since last ACK */

static void ieee80211_lost_packet(struct sta_info *sta,
				  struct ieee80211_tx_info *info)
{
	/* If driver relies on its own algorithm for station kickout, skip
	 * mac80211 packet loss mechanism.
	 */
	if (ieee80211_hw_check(&sta->local->hw, REPORTS_LOW_ACK))
		return;

	/* This packet was aggregated but doesn't carry status info */
	if ((info->flags & IEEE80211_TX_CTL_AMPDU) &&
	    !(info->flags & IEEE80211_TX_STAT_AMPDU))
		return;

	sta->status_stats.lost_packets++;
	if (!sta->sta.tdls &&
	    sta->status_stats.lost_packets < STA_LOST_PKT_THRESHOLD)
		return;

	/*
	 * If we're in TDLS mode, make sure that all STA_LOST_TDLS_PKT_THRESHOLD
	 * of the last packets were lost, and that no ACK was received in the
	 * last STA_LOST_TDLS_PKT_TIME ms, before triggering the CQM packet-loss
	 * mechanism.
	 */
	if (sta->sta.tdls &&
	    (sta->status_stats.lost_packets < STA_LOST_TDLS_PKT_THRESHOLD ||
	     time_before(jiffies,
			 sta->status_stats.last_tdls_pkt_time +
			 STA_LOST_TDLS_PKT_TIME)))
		return;

	cfg80211_cqm_pktloss_notify(sta->sdata->dev, sta->sta.addr,
				    sta->status_stats.lost_packets, GFP_ATOMIC);
	sta->status_stats.lost_packets = 0;
}

static int ieee80211_tx_get_rates(struct ieee80211_hw *hw,
				  struct ieee80211_tx_info *info,
				  int *retry_count)
{
	int rates_idx = -1;
	int count = -1;
	int i;

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		if ((info->flags & IEEE80211_TX_CTL_AMPDU) &&
		    !(info->flags & IEEE80211_TX_STAT_AMPDU)) {
			/* just the first aggr frame carry status info */
			info->status.rates[i].idx = -1;
			info->status.rates[i].count = 0;
			break;
		} else if (info->status.rates[i].idx < 0) {
			break;
		} else if (i >= hw->max_report_rates) {
			/* the HW cannot have attempted that rate */
			info->status.rates[i].idx = -1;
			info->status.rates[i].count = 0;
			break;
		}

		count += info->status.rates[i].count;
	}
	rates_idx = i - 1;

	if (count < 0)
		count = 0;

	*retry_count = count;
	return rates_idx;
}

void ieee80211_tx_monitor(struct ieee80211_local *local, struct sk_buff *skb,
			  struct ieee80211_supported_band *sband,
			  int retry_count, int shift, bool send_to_cooked,
			  struct ieee80211_tx_status *status)
{
	struct sk_buff *skb2;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_sub_if_data *sdata;
	struct net_device *prev_dev = NULL;
	int rtap_len;

	/* send frame to monitor interfaces now */
	rtap_len = ieee80211_tx_radiotap_len(info, status);
	if (WARN_ON_ONCE(skb_headroom(skb) < rtap_len)) {
		pr_err("ieee80211_tx_status: headroom too small\n");
		dev_kfree_skb(skb);
		return;
	}
	ieee80211_add_tx_radiotap_header(local, sband, skb, retry_count,
					 rtap_len, shift, status);

	/* XXX: is this sufficient for BPF? */
	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (sdata->vif.type == NL80211_IFTYPE_MONITOR) {
			if (!ieee80211_sdata_running(sdata))
				continue;

			if ((sdata->u.mntr.flags & MONITOR_FLAG_COOK_FRAMES) &&
			    !send_to_cooked)
				continue;

			if (prev_dev) {
				skb2 = skb_clone(skb, GFP_ATOMIC);
				if (skb2) {
					skb2->dev = prev_dev;
					netif_rx(skb2);
				}
			}

			prev_dev = sdata->dev;
		}
	}
	if (prev_dev) {
		skb->dev = prev_dev;
		netif_rx(skb);
		skb = NULL;
	}
	rcu_read_unlock();
	dev_kfree_skb(skb);
}

static void __ieee80211_tx_status(struct ieee80211_hw *hw,
				  struct ieee80211_tx_status *status)
{
	struct sk_buff *skb = status->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_info *info = status->info;
	struct sta_info *sta;
	__le16 fc;
	struct ieee80211_supported_band *sband;
	int retry_count;
	int rates_idx;
	bool send_to_cooked;
	bool acked;
	struct ieee80211_bar *bar;
	int shift = 0;
	int tid = IEEE80211_NUM_TIDS;
	u16 tx_time_est;

	rates_idx = ieee80211_tx_get_rates(hw, info, &retry_count);

	sband = local->hw.wiphy->bands[info->band];
	fc = hdr->frame_control;

	if (status->sta) {
		sta = container_of(status->sta, struct sta_info, sta);
		shift = ieee80211_vif_get_shift(&sta->sdata->vif);

		if (info->flags & IEEE80211_TX_STATUS_EOSP)
			clear_sta_flag(sta, WLAN_STA_SP);

		acked = !!(info->flags & IEEE80211_TX_STAT_ACK);

		/* mesh Peer Service Period support */
		if (ieee80211_vif_is_mesh(&sta->sdata->vif) &&
		    ieee80211_is_data_qos(fc))
			ieee80211_mpsp_trigger_process(
				ieee80211_get_qos_ctl(hdr), sta, true, acked);

		if (!acked && test_sta_flag(sta, WLAN_STA_PS_STA)) {
			/*
			 * The STA is in power save mode, so assume
			 * that this TX packet failed because of that.
			 */
			ieee80211_handle_filtered_frame(local, sta, skb);
			return;
		}

		if (ieee80211_hw_check(&local->hw, HAS_RATE_CONTROL) &&
		    (ieee80211_is_data(hdr->frame_control)) &&
		    (rates_idx != -1))
			sta->tx_stats.last_rate =
				info->status.rates[rates_idx];

		if ((info->flags & IEEE80211_TX_STAT_AMPDU_NO_BACK) &&
		    (ieee80211_is_data_qos(fc))) {
			u16 ssn;
			u8 *qc;

			qc = ieee80211_get_qos_ctl(hdr);
			tid = qc[0] & 0xf;
			ssn = ((le16_to_cpu(hdr->seq_ctrl) + 0x10)
						& IEEE80211_SCTL_SEQ);
			ieee80211_send_bar(&sta->sdata->vif, hdr->addr1,
					   tid, ssn);
		} else if (ieee80211_is_data_qos(fc)) {
			u8 *qc = ieee80211_get_qos_ctl(hdr);

			tid = qc[0] & 0xf;
		}

		if (!acked && ieee80211_is_back_req(fc)) {
			u16 control;

			/*
			 * BAR failed, store the last SSN and retry sending
			 * the BAR when the next unicast transmission on the
			 * same TID succeeds.
			 */
			bar = (struct ieee80211_bar *) skb->data;
			control = le16_to_cpu(bar->control);
			if (!(control & IEEE80211_BAR_CTRL_MULTI_TID)) {
				u16 ssn = le16_to_cpu(bar->start_seq_num);

				tid = (control &
				       IEEE80211_BAR_CTRL_TID_INFO_MASK) >>
				      IEEE80211_BAR_CTRL_TID_INFO_SHIFT;

				ieee80211_set_bar_pending(sta, tid, ssn);
			}
		}

		if (info->flags & IEEE80211_TX_STAT_TX_FILTERED) {
			ieee80211_handle_filtered_frame(local, sta, skb);
			return;
		} else {
			if (!acked)
				sta->status_stats.retry_failed++;
			sta->status_stats.retry_count += retry_count;

			if (ieee80211_is_data_present(fc)) {
				if (!acked)
					sta->status_stats.msdu_failed[tid]++;

				sta->status_stats.msdu_retries[tid] +=
					retry_count;
			}
		}

		rate_control_tx_status(local, sband, status);
		if (ieee80211_vif_is_mesh(&sta->sdata->vif))
			ieee80211s_update_metric(local, sta, status);

		if (!(info->flags & IEEE80211_TX_CTL_INJECTED) && acked)
			ieee80211_frame_acked(sta, skb);

		if ((sta->sdata->vif.type == NL80211_IFTYPE_STATION) &&
		    ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
			ieee80211_sta_tx_notify(sta->sdata, (void *) skb->data,
						acked, info->status.tx_time);

		if (info->status.tx_time &&
		    wiphy_ext_feature_isset(local->hw.wiphy,
					    NL80211_EXT_FEATURE_AIRTIME_FAIRNESS))
			ieee80211_sta_register_airtime(&sta->sta, tid,
						       info->status.tx_time, 0);

		if ((tx_time_est = ieee80211_info_get_tx_time_est(info)) > 0) {
			/* Do this here to avoid the expensive lookup of the sta
			 * in ieee80211_report_used_skb().
			 */
			ieee80211_sta_update_pending_airtime(local, sta,
							     skb_get_queue_mapping(skb),
							     tx_time_est,
							     true);
			ieee80211_info_set_tx_time_est(info, 0);
		}

		if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS)) {
			if (info->flags & IEEE80211_TX_STAT_ACK) {
				if (sta->status_stats.lost_packets)
					sta->status_stats.lost_packets = 0;

				/* Track when last TDLS packet was ACKed */
				if (test_sta_flag(sta, WLAN_STA_TDLS_PEER_AUTH))
					sta->status_stats.last_tdls_pkt_time =
						jiffies;
			} else {
				ieee80211_lost_packet(sta, info);
			}
		}
	}

	/* SNMP counters
	 * Fragments are passed to low-level drivers as separate skbs, so these
	 * are actually fragments, not frames. Update frame counters only for
	 * the first fragment of the frame. */
	if ((info->flags & IEEE80211_TX_STAT_ACK) ||
	    (info->flags & IEEE80211_TX_STAT_NOACK_TRANSMITTED)) {
		if (ieee80211_is_first_frag(hdr->seq_ctrl)) {
			I802_DEBUG_INC(local->dot11TransmittedFrameCount);
			if (is_multicast_ether_addr(ieee80211_get_DA(hdr)))
				I802_DEBUG_INC(local->dot11MulticastTransmittedFrameCount);
			if (retry_count > 0)
				I802_DEBUG_INC(local->dot11RetryCount);
			if (retry_count > 1)
				I802_DEBUG_INC(local->dot11MultipleRetryCount);
		}

		/* This counter shall be incremented for an acknowledged MPDU
		 * with an individual address in the address 1 field or an MPDU
		 * with a multicast address in the address 1 field of type Data
		 * or Management. */
		if (!is_multicast_ether_addr(hdr->addr1) ||
		    ieee80211_is_data(fc) ||
		    ieee80211_is_mgmt(fc))
			I802_DEBUG_INC(local->dot11TransmittedFragmentCount);
	} else {
		if (ieee80211_is_first_frag(hdr->seq_ctrl))
			I802_DEBUG_INC(local->dot11FailedCount);
	}

	if ((ieee80211_is_nullfunc(fc) || ieee80211_is_qos_nullfunc(fc)) &&
	    ieee80211_has_pm(fc) &&
	    ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS) &&
	    !(info->flags & IEEE80211_TX_CTL_INJECTED) &&
	    local->ps_sdata && !(local->scanning)) {
		if (info->flags & IEEE80211_TX_STAT_ACK) {
			local->ps_sdata->u.mgd.flags |=
					IEEE80211_STA_NULLFUNC_ACKED;
		} else
			mod_timer(&local->dynamic_ps_timer, jiffies +
					msecs_to_jiffies(10));
	}

	ieee80211_report_used_skb(local, skb, false);

	/* this was a transmitted frame, but now we want to reuse it */
	skb_orphan(skb);

	/* Need to make a copy before skb->cb gets cleared */
	send_to_cooked = !!(info->flags & IEEE80211_TX_CTL_INJECTED) ||
			 !(ieee80211_is_data(fc));

	/*
	 * This is a bit racy but we can avoid a lot of work
	 * with this test...
	 */
	if (!local->monitors && (!send_to_cooked || !local->cooked_mntrs)) {
		dev_kfree_skb(skb);
		return;
	}

	/* send to monitor interfaces */
	ieee80211_tx_monitor(local, skb, sband, retry_count, shift,
			     send_to_cooked, status);
}

void ieee80211_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_status status = {
		.skb = skb,
		.info = IEEE80211_SKB_CB(skb),
	};
	struct sta_info *sta;

	rcu_read_lock();

	sta = sta_info_get_by_addrs(local, hdr->addr1, hdr->addr2);
	if (sta)
		status.sta = &sta->sta;

	__ieee80211_tx_status(hw, &status);
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_tx_status);

void ieee80211_tx_status_ext(struct ieee80211_hw *hw,
			     struct ieee80211_tx_status *status)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_info *info = status->info;
	struct ieee80211_sta *pubsta = status->sta;
	struct ieee80211_supported_band *sband;
	int retry_count;
	bool acked, noack_success;

	if (status->skb)
		return __ieee80211_tx_status(hw, status);

	if (!status->sta)
		return;

	ieee80211_tx_get_rates(hw, info, &retry_count);

	sband = hw->wiphy->bands[info->band];

	acked = !!(info->flags & IEEE80211_TX_STAT_ACK);
	noack_success = !!(info->flags & IEEE80211_TX_STAT_NOACK_TRANSMITTED);

	if (pubsta) {
		struct sta_info *sta;

		sta = container_of(pubsta, struct sta_info, sta);

		if (!acked)
			sta->status_stats.retry_failed++;
		sta->status_stats.retry_count += retry_count;

		if (acked) {
			sta->status_stats.last_ack = jiffies;

			if (sta->status_stats.lost_packets)
				sta->status_stats.lost_packets = 0;

			/* Track when last TDLS packet was ACKed */
			if (test_sta_flag(sta, WLAN_STA_TDLS_PEER_AUTH))
				sta->status_stats.last_tdls_pkt_time = jiffies;
		} else if (test_sta_flag(sta, WLAN_STA_PS_STA)) {
			return;
		} else {
			ieee80211_lost_packet(sta, info);
		}

		rate_control_tx_status(local, sband, status);
		if (ieee80211_vif_is_mesh(&sta->sdata->vif))
			ieee80211s_update_metric(local, sta, status);
	}

	if (acked || noack_success) {
		I802_DEBUG_INC(local->dot11TransmittedFrameCount);
		if (!pubsta)
			I802_DEBUG_INC(local->dot11MulticastTransmittedFrameCount);
		if (retry_count > 0)
			I802_DEBUG_INC(local->dot11RetryCount);
		if (retry_count > 1)
			I802_DEBUG_INC(local->dot11MultipleRetryCount);
	} else {
		I802_DEBUG_INC(local->dot11FailedCount);
	}
}
EXPORT_SYMBOL(ieee80211_tx_status_ext);

void ieee80211_tx_rate_update(struct ieee80211_hw *hw,
			      struct ieee80211_sta *pubsta,
			      struct ieee80211_tx_info *info)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_supported_band *sband = hw->wiphy->bands[info->band];
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	struct ieee80211_tx_status status = {
		.info = info,
		.sta = pubsta,
	};

	rate_control_tx_status(local, sband, &status);

	if (ieee80211_hw_check(&local->hw, HAS_RATE_CONTROL))
		sta->tx_stats.last_rate = info->status.rates[0];
}
EXPORT_SYMBOL(ieee80211_tx_rate_update);

void ieee80211_report_low_ack(struct ieee80211_sta *pubsta, u32 num_packets)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	cfg80211_cqm_pktloss_notify(sta->sdata->dev, sta->sta.addr,
				    num_packets, GFP_ATOMIC);
}
EXPORT_SYMBOL(ieee80211_report_low_ack);

void ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_local *local = hw_to_local(hw);

	ieee80211_report_used_skb(local, skb, true);
	dev_kfree_skb_any(skb);
}
EXPORT_SYMBOL(ieee80211_free_txskb);

void ieee80211_purge_tx_queue(struct ieee80211_hw *hw,
			      struct sk_buff_head *skbs)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(skbs)))
		ieee80211_free_txskb(hw, skb);
}
