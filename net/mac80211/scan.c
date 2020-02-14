// SPDX-License-Identifier: GPL-2.0-only
/*
 * Scanning implementation
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2013-2015  Intel Mobile Communications GmbH
 * Copyright 2016-2017  Intel Deutschland GmbH
 * Copyright (C) 2018-2019 Intel Corporation
 */

#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <net/sch_generic.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/random.h>
#include <net/mac80211.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "mesh.h"

#define IEEE80211_PROBE_DELAY (HZ / 33)
#define IEEE80211_CHANNEL_TIME (HZ / 33)
#define IEEE80211_PASSIVE_CHANNEL_TIME (HZ / 9)

void ieee80211_rx_bss_put(struct ieee80211_local *local,
			  struct ieee80211_bss *bss)
{
	if (!bss)
		return;
	cfg80211_put_bss(local->hw.wiphy,
			 container_of((void *)bss, struct cfg80211_bss, priv));
}

static bool is_uapsd_supported(struct ieee802_11_elems *elems)
{
	u8 qos_info;

	if (elems->wmm_info && elems->wmm_info_len == 7
	    && elems->wmm_info[5] == 1)
		qos_info = elems->wmm_info[6];
	else if (elems->wmm_param && elems->wmm_param_len == 24
		 && elems->wmm_param[5] == 1)
		qos_info = elems->wmm_param[6];
	else
		/* no valid wmm information or parameter element found */
		return false;

	return qos_info & IEEE80211_WMM_IE_AP_QOSINFO_UAPSD;
}

static void
ieee80211_update_bss_from_elems(struct ieee80211_local *local,
				struct ieee80211_bss *bss,
				struct ieee802_11_elems *elems,
				struct ieee80211_rx_status *rx_status,
				bool beacon)
{
	int clen, srlen;

	if (beacon)
		bss->device_ts_beacon = rx_status->device_timestamp;
	else
		bss->device_ts_presp = rx_status->device_timestamp;

	if (elems->parse_error) {
		if (beacon)
			bss->corrupt_data |= IEEE80211_BSS_CORRUPT_BEACON;
		else
			bss->corrupt_data |= IEEE80211_BSS_CORRUPT_PROBE_RESP;
	} else {
		if (beacon)
			bss->corrupt_data &= ~IEEE80211_BSS_CORRUPT_BEACON;
		else
			bss->corrupt_data &= ~IEEE80211_BSS_CORRUPT_PROBE_RESP;
	}

	/* save the ERP value so that it is available at association time */
	if (elems->erp_info && (!elems->parse_error ||
				!(bss->valid_data & IEEE80211_BSS_VALID_ERP))) {
		bss->erp_value = elems->erp_info[0];
		bss->has_erp_value = true;
		if (!elems->parse_error)
			bss->valid_data |= IEEE80211_BSS_VALID_ERP;
	}

	/* replace old supported rates if we get new values */
	if (!elems->parse_error ||
	    !(bss->valid_data & IEEE80211_BSS_VALID_RATES)) {
		srlen = 0;
		if (elems->supp_rates) {
			clen = IEEE80211_MAX_SUPP_RATES;
			if (clen > elems->supp_rates_len)
				clen = elems->supp_rates_len;
			memcpy(bss->supp_rates, elems->supp_rates, clen);
			srlen += clen;
		}
		if (elems->ext_supp_rates) {
			clen = IEEE80211_MAX_SUPP_RATES - srlen;
			if (clen > elems->ext_supp_rates_len)
				clen = elems->ext_supp_rates_len;
			memcpy(bss->supp_rates + srlen, elems->ext_supp_rates,
			       clen);
			srlen += clen;
		}
		if (srlen) {
			bss->supp_rates_len = srlen;
			if (!elems->parse_error)
				bss->valid_data |= IEEE80211_BSS_VALID_RATES;
		}
	}

	if (!elems->parse_error ||
	    !(bss->valid_data & IEEE80211_BSS_VALID_WMM)) {
		bss->wmm_used = elems->wmm_param || elems->wmm_info;
		bss->uapsd_supported = is_uapsd_supported(elems);
		if (!elems->parse_error)
			bss->valid_data |= IEEE80211_BSS_VALID_WMM;
	}

	if (beacon) {
		struct ieee80211_supported_band *sband =
			local->hw.wiphy->bands[rx_status->band];
		if (!(rx_status->encoding == RX_ENC_HT) &&
		    !(rx_status->encoding == RX_ENC_VHT))
			bss->beacon_rate =
				&sband->bitrates[rx_status->rate_idx];
	}
}

struct ieee80211_bss *
ieee80211_bss_info_update(struct ieee80211_local *local,
			  struct ieee80211_rx_status *rx_status,
			  struct ieee80211_mgmt *mgmt, size_t len,
			  struct ieee80211_channel *channel)
{
	bool beacon = ieee80211_is_beacon(mgmt->frame_control);
	struct cfg80211_bss *cbss, *non_tx_cbss;
	struct ieee80211_bss *bss, *non_tx_bss;
	struct cfg80211_inform_bss bss_meta = {
		.boottime_ns = rx_status->boottime_ns,
	};
	bool signal_valid;
	struct ieee80211_sub_if_data *scan_sdata;
	struct ieee802_11_elems elems;
	size_t baselen;
	u8 *elements;

	if (rx_status->flag & RX_FLAG_NO_SIGNAL_VAL)
		bss_meta.signal = 0; /* invalid signal indication */
	else if (ieee80211_hw_check(&local->hw, SIGNAL_DBM))
		bss_meta.signal = rx_status->signal * 100;
	else if (ieee80211_hw_check(&local->hw, SIGNAL_UNSPEC))
		bss_meta.signal = (rx_status->signal * 100) / local->hw.max_signal;

	bss_meta.scan_width = NL80211_BSS_CHAN_WIDTH_20;
	if (rx_status->bw == RATE_INFO_BW_5)
		bss_meta.scan_width = NL80211_BSS_CHAN_WIDTH_5;
	else if (rx_status->bw == RATE_INFO_BW_10)
		bss_meta.scan_width = NL80211_BSS_CHAN_WIDTH_10;

	bss_meta.chan = channel;

	rcu_read_lock();
	scan_sdata = rcu_dereference(local->scan_sdata);
	if (scan_sdata && scan_sdata->vif.type == NL80211_IFTYPE_STATION &&
	    scan_sdata->vif.bss_conf.assoc &&
	    ieee80211_have_rx_timestamp(rx_status)) {
		bss_meta.parent_tsf =
			ieee80211_calculate_rx_timestamp(local, rx_status,
							 len + FCS_LEN, 24);
		ether_addr_copy(bss_meta.parent_bssid,
				scan_sdata->vif.bss_conf.bssid);
	}
	rcu_read_unlock();

	cbss = cfg80211_inform_bss_frame_data(local->hw.wiphy, &bss_meta,
					      mgmt, len, GFP_ATOMIC);
	if (!cbss)
		return NULL;

	if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		elements = mgmt->u.probe_resp.variable;
		baselen = offsetof(struct ieee80211_mgmt,
				   u.probe_resp.variable);
	} else {
		baselen = offsetof(struct ieee80211_mgmt, u.beacon.variable);
		elements = mgmt->u.beacon.variable;
	}

	if (baselen > len)
		return NULL;

	ieee802_11_parse_elems(elements, len - baselen, false, &elems,
			       mgmt->bssid, cbss->bssid);

	/* In case the signal is invalid update the status */
	signal_valid = channel == cbss->channel;
	if (!signal_valid)
		rx_status->flag |= RX_FLAG_NO_SIGNAL_VAL;

	bss = (void *)cbss->priv;
	ieee80211_update_bss_from_elems(local, bss, &elems, rx_status, beacon);

	list_for_each_entry(non_tx_cbss, &cbss->nontrans_list, nontrans_list) {
		non_tx_bss = (void *)non_tx_cbss->priv;

		ieee80211_update_bss_from_elems(local, non_tx_bss, &elems,
						rx_status, beacon);
	}

	return bss;
}

static bool ieee80211_scan_accept_presp(struct ieee80211_sub_if_data *sdata,
					u32 scan_flags, const u8 *da)
{
	if (!sdata)
		return false;
	/* accept broadcast for OCE */
	if (scan_flags & NL80211_SCAN_FLAG_ACCEPT_BCAST_PROBE_RESP &&
	    is_broadcast_ether_addr(da))
		return true;
	if (scan_flags & NL80211_SCAN_FLAG_RANDOM_ADDR)
		return true;
	return ether_addr_equal(da, sdata->vif.addr);
}

void ieee80211_scan_rx(struct ieee80211_local *local, struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_sub_if_data *sdata1, *sdata2;
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct ieee80211_bss *bss;
	struct ieee80211_channel *channel;

	if (skb->len < 24 ||
	    (!ieee80211_is_probe_resp(mgmt->frame_control) &&
	     !ieee80211_is_beacon(mgmt->frame_control)))
		return;

	sdata1 = rcu_dereference(local->scan_sdata);
	sdata2 = rcu_dereference(local->sched_scan_sdata);

	if (likely(!sdata1 && !sdata2))
		return;

	if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		struct cfg80211_scan_request *scan_req;
		struct cfg80211_sched_scan_request *sched_scan_req;
		u32 scan_req_flags = 0, sched_scan_req_flags = 0;

		scan_req = rcu_dereference(local->scan_req);
		sched_scan_req = rcu_dereference(local->sched_scan_req);

		if (scan_req)
			scan_req_flags = scan_req->flags;

		if (sched_scan_req)
			sched_scan_req_flags = sched_scan_req->flags;

		/* ignore ProbeResp to foreign address or non-bcast (OCE)
		 * unless scanning with randomised address
		 */
		if (!ieee80211_scan_accept_presp(sdata1, scan_req_flags,
						 mgmt->da) &&
		    !ieee80211_scan_accept_presp(sdata2, sched_scan_req_flags,
						 mgmt->da))
			return;
	}

	channel = ieee80211_get_channel(local->hw.wiphy, rx_status->freq);

	if (!channel || channel->flags & IEEE80211_CHAN_DISABLED)
		return;

	bss = ieee80211_bss_info_update(local, rx_status,
					mgmt, skb->len,
					channel);
	if (bss)
		ieee80211_rx_bss_put(local, bss);
}

static void
ieee80211_prepare_scan_chandef(struct cfg80211_chan_def *chandef,
			       enum nl80211_bss_scan_width scan_width)
{
	memset(chandef, 0, sizeof(*chandef));
	switch (scan_width) {
	case NL80211_BSS_CHAN_WIDTH_5:
		chandef->width = NL80211_CHAN_WIDTH_5;
		break;
	case NL80211_BSS_CHAN_WIDTH_10:
		chandef->width = NL80211_CHAN_WIDTH_10;
		break;
	default:
		chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
		break;
	}
}

/* return false if no more work */
static bool ieee80211_prep_hw_scan(struct ieee80211_local *local)
{
	struct cfg80211_scan_request *req;
	struct cfg80211_chan_def chandef;
	u8 bands_used = 0;
	int i, ielen, n_chans;
	u32 flags = 0;

	req = rcu_dereference_protected(local->scan_req,
					lockdep_is_held(&local->mtx));

	if (test_bit(SCAN_HW_CANCELLED, &local->scanning))
		return false;

	if (ieee80211_hw_check(&local->hw, SINGLE_SCAN_ON_ALL_BANDS)) {
		for (i = 0; i < req->n_channels; i++) {
			local->hw_scan_req->req.channels[i] = req->channels[i];
			bands_used |= BIT(req->channels[i]->band);
		}

		n_chans = req->n_channels;
	} else {
		do {
			if (local->hw_scan_band == NUM_NL80211_BANDS)
				return false;

			n_chans = 0;

			for (i = 0; i < req->n_channels; i++) {
				if (req->channels[i]->band !=
				    local->hw_scan_band)
					continue;
				local->hw_scan_req->req.channels[n_chans] =
							req->channels[i];
				n_chans++;
				bands_used |= BIT(req->channels[i]->band);
			}

			local->hw_scan_band++;
		} while (!n_chans);
	}

	local->hw_scan_req->req.n_channels = n_chans;
	ieee80211_prepare_scan_chandef(&chandef, req->scan_width);

	if (req->flags & NL80211_SCAN_FLAG_MIN_PREQ_CONTENT)
		flags |= IEEE80211_PROBE_FLAG_MIN_CONTENT;

	ielen = ieee80211_build_preq_ies(local,
					 (u8 *)local->hw_scan_req->req.ie,
					 local->hw_scan_ies_bufsize,
					 &local->hw_scan_req->ies,
					 req->ie, req->ie_len,
					 bands_used, req->rates, &chandef,
					 flags);
	local->hw_scan_req->req.ie_len = ielen;
	local->hw_scan_req->req.no_cck = req->no_cck;
	ether_addr_copy(local->hw_scan_req->req.mac_addr, req->mac_addr);
	ether_addr_copy(local->hw_scan_req->req.mac_addr_mask,
			req->mac_addr_mask);
	ether_addr_copy(local->hw_scan_req->req.bssid, req->bssid);

	return true;
}

static void __ieee80211_scan_completed(struct ieee80211_hw *hw, bool aborted)
{
	struct ieee80211_local *local = hw_to_local(hw);
	bool hw_scan = test_bit(SCAN_HW_SCANNING, &local->scanning);
	bool was_scanning = local->scanning;
	struct cfg80211_scan_request *scan_req;
	struct ieee80211_sub_if_data *scan_sdata;
	struct ieee80211_sub_if_data *sdata;

	lockdep_assert_held(&local->mtx);

	/*
	 * It's ok to abort a not-yet-running scan (that
	 * we have one at all will be verified by checking
	 * local->scan_req next), but not to complete it
	 * successfully.
	 */
	if (WARN_ON(!local->scanning && !aborted))
		aborted = true;

	if (WARN_ON(!local->scan_req))
		return;

	if (hw_scan && !aborted &&
	    !ieee80211_hw_check(&local->hw, SINGLE_SCAN_ON_ALL_BANDS) &&
	    ieee80211_prep_hw_scan(local)) {
		int rc;

		rc = drv_hw_scan(local,
			rcu_dereference_protected(local->scan_sdata,
						  lockdep_is_held(&local->mtx)),
			local->hw_scan_req);

		if (rc == 0)
			return;

		/* HW scan failed and is going to be reported as aborted,
		 * so clear old scan info.
		 */
		memset(&local->scan_info, 0, sizeof(local->scan_info));
		aborted = true;
	}

	kfree(local->hw_scan_req);
	local->hw_scan_req = NULL;

	scan_req = rcu_dereference_protected(local->scan_req,
					     lockdep_is_held(&local->mtx));

	if (scan_req != local->int_scan_req) {
		local->scan_info.aborted = aborted;
		cfg80211_scan_done(scan_req, &local->scan_info);
	}
	RCU_INIT_POINTER(local->scan_req, NULL);

	scan_sdata = rcu_dereference_protected(local->scan_sdata,
					       lockdep_is_held(&local->mtx));
	RCU_INIT_POINTER(local->scan_sdata, NULL);

	local->scanning = 0;
	local->scan_chandef.chan = NULL;

	/* Set power back to normal operating levels. */
	ieee80211_hw_config(local, 0);

	if (!hw_scan) {
		ieee80211_configure_filter(local);
		drv_sw_scan_complete(local, scan_sdata);
		ieee80211_offchannel_return(local);
	}

	ieee80211_recalc_idle(local);

	ieee80211_mlme_notify_scan_completed(local);
	ieee80211_ibss_notify_scan_completed(local);

	/* Requeue all the work that might have been ignored while
	 * the scan was in progress; if there was none this will
	 * just be a no-op for the particular interface.
	 */
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (ieee80211_sdata_running(sdata))
			ieee80211_queue_work(&sdata->local->hw, &sdata->work);
	}

	if (was_scanning)
		ieee80211_start_next_roc(local);
}

void ieee80211_scan_completed(struct ieee80211_hw *hw,
			      struct cfg80211_scan_info *info)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_scan_completed(local, info->aborted);

	set_bit(SCAN_COMPLETED, &local->scanning);
	if (info->aborted)
		set_bit(SCAN_ABORTED, &local->scanning);

	memcpy(&local->scan_info, info, sizeof(*info));

	ieee80211_queue_delayed_work(&local->hw, &local->scan_work, 0);
}
EXPORT_SYMBOL(ieee80211_scan_completed);

static int ieee80211_start_sw_scan(struct ieee80211_local *local,
				   struct ieee80211_sub_if_data *sdata)
{
	/* Software scan is not supported in multi-channel cases */
	if (local->use_chanctx)
		return -EOPNOTSUPP;

	/*
	 * Hardware/driver doesn't support hw_scan, so use software
	 * scanning instead. First send a nullfunc frame with power save
	 * bit on so that AP will buffer the frames for us while we are not
	 * listening, then send probe requests to each channel and wait for
	 * the responses. After all channels are scanned, tune back to the
	 * original channel and send a nullfunc frame with power save bit
	 * off to trigger the AP to send us all the buffered frames.
	 *
	 * Note that while local->sw_scanning is true everything else but
	 * nullfunc frames and probe requests will be dropped in
	 * ieee80211_tx_h_check_assoc().
	 */
	drv_sw_scan_start(local, sdata, local->scan_addr);

	local->leave_oper_channel_time = jiffies;
	local->next_scan_state = SCAN_DECISION;
	local->scan_channel_idx = 0;

	ieee80211_offchannel_stop_vifs(local);

	/* ensure nullfunc is transmitted before leaving operating channel */
	ieee80211_flush_queues(local, NULL, false);

	ieee80211_configure_filter(local);

	/* We need to set power level at maximum rate for scanning. */
	ieee80211_hw_config(local, 0);

	ieee80211_queue_delayed_work(&local->hw,
				     &local->scan_work, 0);

	return 0;
}

static bool __ieee80211_can_leave_ch(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *sdata_iter;

	if (!ieee80211_is_radar_required(local))
		return true;

	if (!regulatory_pre_cac_allowed(local->hw.wiphy))
		return false;

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata_iter, &local->interfaces, list) {
		if (sdata_iter->wdev.cac_started) {
			mutex_unlock(&local->iflist_mtx);
			return false;
		}
	}
	mutex_unlock(&local->iflist_mtx);

	return true;
}

static bool ieee80211_can_scan(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata)
{
	if (!__ieee80211_can_leave_ch(sdata))
		return false;

	if (!list_empty(&local->roc_list))
		return false;

	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    sdata->u.mgd.flags & IEEE80211_STA_CONNECTION_POLL)
		return false;

	return true;
}

void ieee80211_run_deferred_scan(struct ieee80211_local *local)
{
	lockdep_assert_held(&local->mtx);

	if (!local->scan_req || local->scanning)
		return;

	if (!ieee80211_can_scan(local,
				rcu_dereference_protected(
					local->scan_sdata,
					lockdep_is_held(&local->mtx))))
		return;

	ieee80211_queue_delayed_work(&local->hw, &local->scan_work,
				     round_jiffies_relative(0));
}

static void ieee80211_send_scan_probe_req(struct ieee80211_sub_if_data *sdata,
					  const u8 *src, const u8 *dst,
					  const u8 *ssid, size_t ssid_len,
					  const u8 *ie, size_t ie_len,
					  u32 ratemask, u32 flags, u32 tx_flags,
					  struct ieee80211_channel *channel)
{
	struct sk_buff *skb;
	u32 txdata_flags = 0;

	skb = ieee80211_build_probe_req(sdata, src, dst, ratemask, channel,
					ssid, ssid_len,
					ie, ie_len, flags);

	if (skb) {
		if (flags & IEEE80211_PROBE_FLAG_RANDOM_SN) {
			struct ieee80211_hdr *hdr = (void *)skb->data;
			u16 sn = get_random_u32();

			txdata_flags |= IEEE80211_TX_NO_SEQNO;
			hdr->seq_ctrl =
				cpu_to_le16(IEEE80211_SN_TO_SEQ(sn));
		}
		IEEE80211_SKB_CB(skb)->flags |= tx_flags;
		ieee80211_tx_skb_tid_band(sdata, skb, 7, channel->band,
					  txdata_flags);
	}
}

static void ieee80211_scan_state_send_probe(struct ieee80211_local *local,
					    unsigned long *next_delay)
{
	int i;
	struct ieee80211_sub_if_data *sdata;
	struct cfg80211_scan_request *scan_req;
	enum nl80211_band band = local->hw.conf.chandef.chan->band;
	u32 flags = 0, tx_flags;

	scan_req = rcu_dereference_protected(local->scan_req,
					     lockdep_is_held(&local->mtx));

	tx_flags = IEEE80211_TX_INTFL_OFFCHAN_TX_OK;
	if (scan_req->no_cck)
		tx_flags |= IEEE80211_TX_CTL_NO_CCK_RATE;
	if (scan_req->flags & NL80211_SCAN_FLAG_MIN_PREQ_CONTENT)
		flags |= IEEE80211_PROBE_FLAG_MIN_CONTENT;
	if (scan_req->flags & NL80211_SCAN_FLAG_RANDOM_SN)
		flags |= IEEE80211_PROBE_FLAG_RANDOM_SN;

	sdata = rcu_dereference_protected(local->scan_sdata,
					  lockdep_is_held(&local->mtx));

	for (i = 0; i < scan_req->n_ssids; i++)
		ieee80211_send_scan_probe_req(
			sdata, local->scan_addr, scan_req->bssid,
			scan_req->ssids[i].ssid, scan_req->ssids[i].ssid_len,
			scan_req->ie, scan_req->ie_len,
			scan_req->rates[band], flags,
			tx_flags, local->hw.conf.chandef.chan);

	/*
	 * After sending probe requests, wait for probe responses
	 * on the channel.
	 */
	*next_delay = IEEE80211_CHANNEL_TIME;
	local->next_scan_state = SCAN_DECISION;
}

static int __ieee80211_start_scan(struct ieee80211_sub_if_data *sdata,
				  struct cfg80211_scan_request *req)
{
	struct ieee80211_local *local = sdata->local;
	bool hw_scan = local->ops->hw_scan;
	int rc;

	lockdep_assert_held(&local->mtx);

	if (local->scan_req)
		return -EBUSY;

	if (!__ieee80211_can_leave_ch(sdata))
		return -EBUSY;

	if (!ieee80211_can_scan(local, sdata)) {
		/* wait for the work to finish/time out */
		rcu_assign_pointer(local->scan_req, req);
		rcu_assign_pointer(local->scan_sdata, sdata);
		return 0;
	}

 again:
	if (hw_scan) {
		u8 *ies;

		local->hw_scan_ies_bufsize = local->scan_ies_len + req->ie_len;

		if (ieee80211_hw_check(&local->hw, SINGLE_SCAN_ON_ALL_BANDS)) {
			int i, n_bands = 0;
			u8 bands_counted = 0;

			for (i = 0; i < req->n_channels; i++) {
				if (bands_counted & BIT(req->channels[i]->band))
					continue;
				bands_counted |= BIT(req->channels[i]->band);
				n_bands++;
			}

			local->hw_scan_ies_bufsize *= n_bands;
		}

		local->hw_scan_req = kmalloc(
				sizeof(*local->hw_scan_req) +
				req->n_channels * sizeof(req->channels[0]) +
				local->hw_scan_ies_bufsize, GFP_KERNEL);
		if (!local->hw_scan_req)
			return -ENOMEM;

		local->hw_scan_req->req.ssids = req->ssids;
		local->hw_scan_req->req.n_ssids = req->n_ssids;
		ies = (u8 *)local->hw_scan_req +
			sizeof(*local->hw_scan_req) +
			req->n_channels * sizeof(req->channels[0]);
		local->hw_scan_req->req.ie = ies;
		local->hw_scan_req->req.flags = req->flags;
		eth_broadcast_addr(local->hw_scan_req->req.bssid);
		local->hw_scan_req->req.duration = req->duration;
		local->hw_scan_req->req.duration_mandatory =
			req->duration_mandatory;

		local->hw_scan_band = 0;

		/*
		 * After allocating local->hw_scan_req, we must
		 * go through until ieee80211_prep_hw_scan(), so
		 * anything that might be changed here and leave
		 * this function early must not go after this
		 * allocation.
		 */
	}

	rcu_assign_pointer(local->scan_req, req);
	rcu_assign_pointer(local->scan_sdata, sdata);

	if (req->flags & NL80211_SCAN_FLAG_RANDOM_ADDR)
		get_random_mask_addr(local->scan_addr,
				     req->mac_addr,
				     req->mac_addr_mask);
	else
		memcpy(local->scan_addr, sdata->vif.addr, ETH_ALEN);

	if (hw_scan) {
		__set_bit(SCAN_HW_SCANNING, &local->scanning);
	} else if ((req->n_channels == 1) &&
		   (req->channels[0] == local->_oper_chandef.chan)) {
		/*
		 * If we are scanning only on the operating channel
		 * then we do not need to stop normal activities
		 */
		unsigned long next_delay;

		__set_bit(SCAN_ONCHANNEL_SCANNING, &local->scanning);

		ieee80211_recalc_idle(local);

		/* Notify driver scan is starting, keep order of operations
		 * same as normal software scan, in case that matters. */
		drv_sw_scan_start(local, sdata, local->scan_addr);

		ieee80211_configure_filter(local); /* accept probe-responses */

		/* We need to ensure power level is at max for scanning. */
		ieee80211_hw_config(local, 0);

		if ((req->channels[0]->flags & (IEEE80211_CHAN_NO_IR |
						IEEE80211_CHAN_RADAR)) ||
		    !req->n_ssids) {
			next_delay = IEEE80211_PASSIVE_CHANNEL_TIME;
		} else {
			ieee80211_scan_state_send_probe(local, &next_delay);
			next_delay = IEEE80211_CHANNEL_TIME;
		}

		/* Now, just wait a bit and we are all done! */
		ieee80211_queue_delayed_work(&local->hw, &local->scan_work,
					     next_delay);
		return 0;
	} else {
		/* Do normal software scan */
		__set_bit(SCAN_SW_SCANNING, &local->scanning);
	}

	ieee80211_recalc_idle(local);

	if (hw_scan) {
		WARN_ON(!ieee80211_prep_hw_scan(local));
		rc = drv_hw_scan(local, sdata, local->hw_scan_req);
	} else {
		rc = ieee80211_start_sw_scan(local, sdata);
	}

	if (rc) {
		kfree(local->hw_scan_req);
		local->hw_scan_req = NULL;
		local->scanning = 0;

		ieee80211_recalc_idle(local);

		local->scan_req = NULL;
		RCU_INIT_POINTER(local->scan_sdata, NULL);
	}

	if (hw_scan && rc == 1) {
		/*
		 * we can't fall back to software for P2P-GO
		 * as it must update NoA etc.
		 */
		if (ieee80211_vif_type_p2p(&sdata->vif) ==
				NL80211_IFTYPE_P2P_GO)
			return -EOPNOTSUPP;
		hw_scan = false;
		goto again;
	}

	return rc;
}

static unsigned long
ieee80211_scan_get_channel_time(struct ieee80211_channel *chan)
{
	/*
	 * TODO: channel switching also consumes quite some time,
	 * add that delay as well to get a better estimation
	 */
	if (chan->flags & (IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_RADAR))
		return IEEE80211_PASSIVE_CHANNEL_TIME;
	return IEEE80211_PROBE_DELAY + IEEE80211_CHANNEL_TIME;
}

static void ieee80211_scan_state_decision(struct ieee80211_local *local,
					  unsigned long *next_delay)
{
	bool associated = false;
	bool tx_empty = true;
	bool bad_latency;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_channel *next_chan;
	enum mac80211_scan_state next_scan_state;
	struct cfg80211_scan_request *scan_req;

	/*
	 * check if at least one STA interface is associated,
	 * check if at least one STA interface has pending tx frames
	 * and grab the lowest used beacon interval
	 */
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			if (sdata->u.mgd.associated) {
				associated = true;

				if (!qdisc_all_tx_empty(sdata->dev)) {
					tx_empty = false;
					break;
				}
			}
		}
	}
	mutex_unlock(&local->iflist_mtx);

	scan_req = rcu_dereference_protected(local->scan_req,
					     lockdep_is_held(&local->mtx));

	next_chan = scan_req->channels[local->scan_channel_idx];

	/*
	 * we're currently scanning a different channel, let's
	 * see if we can scan another channel without interfering
	 * with the current traffic situation.
	 *
	 * Keep good latency, do not stay off-channel more than 125 ms.
	 */

	bad_latency = time_after(jiffies +
				 ieee80211_scan_get_channel_time(next_chan),
				 local->leave_oper_channel_time + HZ / 8);

	if (associated && !tx_empty) {
		if (scan_req->flags & NL80211_SCAN_FLAG_LOW_PRIORITY)
			next_scan_state = SCAN_ABORT;
		else
			next_scan_state = SCAN_SUSPEND;
	} else if (associated && bad_latency) {
		next_scan_state = SCAN_SUSPEND;
	} else {
		next_scan_state = SCAN_SET_CHANNEL;
	}

	local->next_scan_state = next_scan_state;

	*next_delay = 0;
}

static void ieee80211_scan_state_set_channel(struct ieee80211_local *local,
					     unsigned long *next_delay)
{
	int skip;
	struct ieee80211_channel *chan;
	enum nl80211_bss_scan_width oper_scan_width;
	struct cfg80211_scan_request *scan_req;

	scan_req = rcu_dereference_protected(local->scan_req,
					     lockdep_is_held(&local->mtx));

	skip = 0;
	chan = scan_req->channels[local->scan_channel_idx];

	local->scan_chandef.chan = chan;
	local->scan_chandef.center_freq1 = chan->center_freq;
	local->scan_chandef.center_freq2 = 0;
	switch (scan_req->scan_width) {
	case NL80211_BSS_CHAN_WIDTH_5:
		local->scan_chandef.width = NL80211_CHAN_WIDTH_5;
		break;
	case NL80211_BSS_CHAN_WIDTH_10:
		local->scan_chandef.width = NL80211_CHAN_WIDTH_10;
		break;
	case NL80211_BSS_CHAN_WIDTH_20:
		/* If scanning on oper channel, use whatever channel-type
		 * is currently in use.
		 */
		oper_scan_width = cfg80211_chandef_to_scan_width(
					&local->_oper_chandef);
		if (chan == local->_oper_chandef.chan &&
		    oper_scan_width == scan_req->scan_width)
			local->scan_chandef = local->_oper_chandef;
		else
			local->scan_chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
		break;
	}

	if (ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL))
		skip = 1;

	/* advance state machine to next channel/band */
	local->scan_channel_idx++;

	if (skip) {
		/* if we skip this channel return to the decision state */
		local->next_scan_state = SCAN_DECISION;
		return;
	}

	/*
	 * Probe delay is used to update the NAV, cf. 11.1.3.2.2
	 * (which unfortunately doesn't say _why_ step a) is done,
	 * but it waits for the probe delay or until a frame is
	 * received - and the received frame would update the NAV).
	 * For now, we do not support waiting until a frame is
	 * received.
	 *
	 * In any case, it is not necessary for a passive scan.
	 */
	if ((chan->flags & (IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_RADAR)) ||
	    !scan_req->n_ssids) {
		*next_delay = IEEE80211_PASSIVE_CHANNEL_TIME;
		local->next_scan_state = SCAN_DECISION;
		return;
	}

	/* active scan, send probes */
	*next_delay = IEEE80211_PROBE_DELAY;
	local->next_scan_state = SCAN_SEND_PROBE;
}

static void ieee80211_scan_state_suspend(struct ieee80211_local *local,
					 unsigned long *next_delay)
{
	/* switch back to the operating channel */
	local->scan_chandef.chan = NULL;
	ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);

	/* disable PS */
	ieee80211_offchannel_return(local);

	*next_delay = HZ / 5;
	/* afterwards, resume scan & go to next channel */
	local->next_scan_state = SCAN_RESUME;
}

static void ieee80211_scan_state_resume(struct ieee80211_local *local,
					unsigned long *next_delay)
{
	ieee80211_offchannel_stop_vifs(local);

	if (local->ops->flush) {
		ieee80211_flush_queues(local, NULL, false);
		*next_delay = 0;
	} else
		*next_delay = HZ / 10;

	/* remember when we left the operating channel */
	local->leave_oper_channel_time = jiffies;

	/* advance to the next channel to be scanned */
	local->next_scan_state = SCAN_SET_CHANNEL;
}

void ieee80211_scan_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, scan_work.work);
	struct ieee80211_sub_if_data *sdata;
	struct cfg80211_scan_request *scan_req;
	unsigned long next_delay = 0;
	bool aborted;

	mutex_lock(&local->mtx);

	if (!ieee80211_can_run_worker(local)) {
		aborted = true;
		goto out_complete;
	}

	sdata = rcu_dereference_protected(local->scan_sdata,
					  lockdep_is_held(&local->mtx));
	scan_req = rcu_dereference_protected(local->scan_req,
					     lockdep_is_held(&local->mtx));

	/* When scanning on-channel, the first-callback means completed. */
	if (test_bit(SCAN_ONCHANNEL_SCANNING, &local->scanning)) {
		aborted = test_and_clear_bit(SCAN_ABORTED, &local->scanning);
		goto out_complete;
	}

	if (test_and_clear_bit(SCAN_COMPLETED, &local->scanning)) {
		aborted = test_and_clear_bit(SCAN_ABORTED, &local->scanning);
		goto out_complete;
	}

	if (!sdata || !scan_req)
		goto out;

	if (!local->scanning) {
		int rc;

		RCU_INIT_POINTER(local->scan_req, NULL);
		RCU_INIT_POINTER(local->scan_sdata, NULL);

		rc = __ieee80211_start_scan(sdata, scan_req);
		if (rc) {
			/* need to complete scan in cfg80211 */
			rcu_assign_pointer(local->scan_req, scan_req);
			aborted = true;
			goto out_complete;
		} else
			goto out;
	}

	/*
	 * as long as no delay is required advance immediately
	 * without scheduling a new work
	 */
	do {
		if (!ieee80211_sdata_running(sdata)) {
			aborted = true;
			goto out_complete;
		}

		switch (local->next_scan_state) {
		case SCAN_DECISION:
			/* if no more bands/channels left, complete scan */
			if (local->scan_channel_idx >= scan_req->n_channels) {
				aborted = false;
				goto out_complete;
			}
			ieee80211_scan_state_decision(local, &next_delay);
			break;
		case SCAN_SET_CHANNEL:
			ieee80211_scan_state_set_channel(local, &next_delay);
			break;
		case SCAN_SEND_PROBE:
			ieee80211_scan_state_send_probe(local, &next_delay);
			break;
		case SCAN_SUSPEND:
			ieee80211_scan_state_suspend(local, &next_delay);
			break;
		case SCAN_RESUME:
			ieee80211_scan_state_resume(local, &next_delay);
			break;
		case SCAN_ABORT:
			aborted = true;
			goto out_complete;
		}
	} while (next_delay == 0);

	ieee80211_queue_delayed_work(&local->hw, &local->scan_work, next_delay);
	goto out;

out_complete:
	__ieee80211_scan_completed(&local->hw, aborted);
out:
	mutex_unlock(&local->mtx);
}

int ieee80211_request_scan(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_scan_request *req)
{
	int res;

	mutex_lock(&sdata->local->mtx);
	res = __ieee80211_start_scan(sdata, req);
	mutex_unlock(&sdata->local->mtx);

	return res;
}

int ieee80211_request_ibss_scan(struct ieee80211_sub_if_data *sdata,
				const u8 *ssid, u8 ssid_len,
				struct ieee80211_channel **channels,
				unsigned int n_channels,
				enum nl80211_bss_scan_width scan_width)
{
	struct ieee80211_local *local = sdata->local;
	int ret = -EBUSY, i, n_ch = 0;
	enum nl80211_band band;

	mutex_lock(&local->mtx);

	/* busy scanning */
	if (local->scan_req)
		goto unlock;

	/* fill internal scan request */
	if (!channels) {
		int max_n;

		for (band = 0; band < NUM_NL80211_BANDS; band++) {
			if (!local->hw.wiphy->bands[band])
				continue;

			max_n = local->hw.wiphy->bands[band]->n_channels;
			for (i = 0; i < max_n; i++) {
				struct ieee80211_channel *tmp_ch =
				    &local->hw.wiphy->bands[band]->channels[i];

				if (tmp_ch->flags & (IEEE80211_CHAN_NO_IR |
						     IEEE80211_CHAN_DISABLED))
					continue;

				local->int_scan_req->channels[n_ch] = tmp_ch;
				n_ch++;
			}
		}

		if (WARN_ON_ONCE(n_ch == 0))
			goto unlock;

		local->int_scan_req->n_channels = n_ch;
	} else {
		for (i = 0; i < n_channels; i++) {
			if (channels[i]->flags & (IEEE80211_CHAN_NO_IR |
						  IEEE80211_CHAN_DISABLED))
				continue;

			local->int_scan_req->channels[n_ch] = channels[i];
			n_ch++;
		}

		if (WARN_ON_ONCE(n_ch == 0))
			goto unlock;

		local->int_scan_req->n_channels = n_ch;
	}

	local->int_scan_req->ssids = &local->scan_ssid;
	local->int_scan_req->n_ssids = 1;
	local->int_scan_req->scan_width = scan_width;
	memcpy(local->int_scan_req->ssids[0].ssid, ssid, IEEE80211_MAX_SSID_LEN);
	local->int_scan_req->ssids[0].ssid_len = ssid_len;

	ret = __ieee80211_start_scan(sdata, sdata->local->int_scan_req);
 unlock:
	mutex_unlock(&local->mtx);
	return ret;
}

/*
 * Only call this function when a scan can't be queued -- under RTNL.
 */
void ieee80211_scan_cancel(struct ieee80211_local *local)
{
	/*
	 * We are canceling software scan, or deferred scan that was not
	 * yet really started (see __ieee80211_start_scan ).
	 *
	 * Regarding hardware scan:
	 * - we can not call  __ieee80211_scan_completed() as when
	 *   SCAN_HW_SCANNING bit is set this function change
	 *   local->hw_scan_req to operate on 5G band, what race with
	 *   driver which can use local->hw_scan_req
	 *
	 * - we can not cancel scan_work since driver can schedule it
	 *   by ieee80211_scan_completed(..., true) to finish scan
	 *
	 * Hence we only call the cancel_hw_scan() callback, but the low-level
	 * driver is still responsible for calling ieee80211_scan_completed()
	 * after the scan was completed/aborted.
	 */

	mutex_lock(&local->mtx);
	if (!local->scan_req)
		goto out;

	/*
	 * We have a scan running and the driver already reported completion,
	 * but the worker hasn't run yet or is stuck on the mutex - mark it as
	 * cancelled.
	 */
	if (test_bit(SCAN_HW_SCANNING, &local->scanning) &&
	    test_bit(SCAN_COMPLETED, &local->scanning)) {
		set_bit(SCAN_HW_CANCELLED, &local->scanning);
		goto out;
	}

	if (test_bit(SCAN_HW_SCANNING, &local->scanning)) {
		/*
		 * Make sure that __ieee80211_scan_completed doesn't trigger a
		 * scan on another band.
		 */
		set_bit(SCAN_HW_CANCELLED, &local->scanning);
		if (local->ops->cancel_hw_scan)
			drv_cancel_hw_scan(local,
				rcu_dereference_protected(local->scan_sdata,
						lockdep_is_held(&local->mtx)));
		goto out;
	}

	/*
	 * If the work is currently running, it must be blocked on
	 * the mutex, but we'll set scan_sdata = NULL and it'll
	 * simply exit once it acquires the mutex.
	 */
	cancel_delayed_work(&local->scan_work);
	/* and clean up */
	memset(&local->scan_info, 0, sizeof(local->scan_info));
	__ieee80211_scan_completed(&local->hw, true);
out:
	mutex_unlock(&local->mtx);
}

int __ieee80211_request_sched_scan_start(struct ieee80211_sub_if_data *sdata,
					struct cfg80211_sched_scan_request *req)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_scan_ies sched_scan_ies = {};
	struct cfg80211_chan_def chandef;
	int ret, i, iebufsz, num_bands = 0;
	u32 rate_masks[NUM_NL80211_BANDS] = {};
	u8 bands_used = 0;
	u8 *ie;
	u32 flags = 0;

	iebufsz = local->scan_ies_len + req->ie_len;

	lockdep_assert_held(&local->mtx);

	if (!local->ops->sched_scan_start)
		return -ENOTSUPP;

	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		if (local->hw.wiphy->bands[i]) {
			bands_used |= BIT(i);
			rate_masks[i] = (u32) -1;
			num_bands++;
		}
	}

	if (req->flags & NL80211_SCAN_FLAG_MIN_PREQ_CONTENT)
		flags |= IEEE80211_PROBE_FLAG_MIN_CONTENT;

	ie = kcalloc(iebufsz, num_bands, GFP_KERNEL);
	if (!ie) {
		ret = -ENOMEM;
		goto out;
	}

	ieee80211_prepare_scan_chandef(&chandef, req->scan_width);

	ieee80211_build_preq_ies(local, ie, num_bands * iebufsz,
				 &sched_scan_ies, req->ie,
				 req->ie_len, bands_used, rate_masks, &chandef,
				 flags);

	ret = drv_sched_scan_start(local, sdata, req, &sched_scan_ies);
	if (ret == 0) {
		rcu_assign_pointer(local->sched_scan_sdata, sdata);
		rcu_assign_pointer(local->sched_scan_req, req);
	}

	kfree(ie);

out:
	if (ret) {
		/* Clean in case of failure after HW restart or upon resume. */
		RCU_INIT_POINTER(local->sched_scan_sdata, NULL);
		RCU_INIT_POINTER(local->sched_scan_req, NULL);
	}

	return ret;
}

int ieee80211_request_sched_scan_start(struct ieee80211_sub_if_data *sdata,
				       struct cfg80211_sched_scan_request *req)
{
	struct ieee80211_local *local = sdata->local;
	int ret;

	mutex_lock(&local->mtx);

	if (rcu_access_pointer(local->sched_scan_sdata)) {
		mutex_unlock(&local->mtx);
		return -EBUSY;
	}

	ret = __ieee80211_request_sched_scan_start(sdata, req);

	mutex_unlock(&local->mtx);
	return ret;
}

int ieee80211_request_sched_scan_stop(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sched_scan_sdata;
	int ret = -ENOENT;

	mutex_lock(&local->mtx);

	if (!local->ops->sched_scan_stop) {
		ret = -ENOTSUPP;
		goto out;
	}

	/* We don't want to restart sched scan anymore. */
	RCU_INIT_POINTER(local->sched_scan_req, NULL);

	sched_scan_sdata = rcu_dereference_protected(local->sched_scan_sdata,
						lockdep_is_held(&local->mtx));
	if (sched_scan_sdata) {
		ret = drv_sched_scan_stop(local, sched_scan_sdata);
		if (!ret)
			RCU_INIT_POINTER(local->sched_scan_sdata, NULL);
	}
out:
	mutex_unlock(&local->mtx);

	return ret;
}

void ieee80211_sched_scan_results(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_sched_scan_results(local);

	cfg80211_sched_scan_results(hw->wiphy, 0);
}
EXPORT_SYMBOL(ieee80211_sched_scan_results);

void ieee80211_sched_scan_end(struct ieee80211_local *local)
{
	mutex_lock(&local->mtx);

	if (!rcu_access_pointer(local->sched_scan_sdata)) {
		mutex_unlock(&local->mtx);
		return;
	}

	RCU_INIT_POINTER(local->sched_scan_sdata, NULL);

	/* If sched scan was aborted by the driver. */
	RCU_INIT_POINTER(local->sched_scan_req, NULL);

	mutex_unlock(&local->mtx);

	cfg80211_sched_scan_stopped(local->hw.wiphy, 0);
}

void ieee80211_sched_scan_stopped_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local,
			     sched_scan_stopped_work);

	ieee80211_sched_scan_end(local);
}

void ieee80211_sched_scan_stopped(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_sched_scan_stopped(local);

	/*
	 * this shouldn't really happen, so for simplicity
	 * simply ignore it, and let mac80211 reconfigure
	 * the sched scan later on.
	 */
	if (local->in_reconfig)
		return;

	schedule_work(&local->sched_scan_stopped_work);
}
EXPORT_SYMBOL(ieee80211_sched_scan_stopped);
