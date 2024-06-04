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
 * Copyright (C) 2018-2024 Intel Corporation
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

struct inform_bss_update_data {
	struct ieee80211_rx_status *rx_status;
	bool beacon;
};

void ieee80211_inform_bss(struct wiphy *wiphy,
			  struct cfg80211_bss *cbss,
			  const struct cfg80211_bss_ies *ies,
			  void *data)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct inform_bss_update_data *update_data = data;
	struct ieee80211_bss *bss = (void *)cbss->priv;
	struct ieee80211_rx_status *rx_status;
	struct ieee802_11_elems *elems;
	int clen, srlen;

	/* This happens while joining an IBSS */
	if (!update_data)
		return;

	elems = ieee802_11_parse_elems(ies->data, ies->len, false, NULL);
	if (!elems)
		return;

	rx_status = update_data->rx_status;

	if (update_data->beacon)
		bss->device_ts_beacon = rx_status->device_timestamp;
	else
		bss->device_ts_presp = rx_status->device_timestamp;

	if (elems->parse_error) {
		if (update_data->beacon)
			bss->corrupt_data |= IEEE80211_BSS_CORRUPT_BEACON;
		else
			bss->corrupt_data |= IEEE80211_BSS_CORRUPT_PROBE_RESP;
	} else {
		if (update_data->beacon)
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

	if (update_data->beacon) {
		struct ieee80211_supported_band *sband =
			local->hw.wiphy->bands[rx_status->band];
		if (!(rx_status->encoding == RX_ENC_HT) &&
		    !(rx_status->encoding == RX_ENC_VHT))
			bss->beacon_rate =
				&sband->bitrates[rx_status->rate_idx];
	}

	if (elems->vht_cap_elem)
		bss->vht_cap_info =
			le32_to_cpu(elems->vht_cap_elem->vht_cap_info);
	else
		bss->vht_cap_info = 0;

	kfree(elems);
}

struct ieee80211_bss *
ieee80211_bss_info_update(struct ieee80211_local *local,
			  struct ieee80211_rx_status *rx_status,
			  struct ieee80211_mgmt *mgmt, size_t len,
			  struct ieee80211_channel *channel)
{
	bool beacon = ieee80211_is_beacon(mgmt->frame_control) ||
		      ieee80211_is_s1g_beacon(mgmt->frame_control);
	struct cfg80211_bss *cbss;
	struct inform_bss_update_data update_data = {
		.rx_status = rx_status,
		.beacon = beacon,
	};
	struct cfg80211_inform_bss bss_meta = {
		.boottime_ns = rx_status->boottime_ns,
		.drv_data = (void *)&update_data,
	};
	bool signal_valid;
	struct ieee80211_sub_if_data *scan_sdata;

	if (rx_status->flag & RX_FLAG_NO_SIGNAL_VAL)
		bss_meta.signal = 0; /* invalid signal indication */
	else if (ieee80211_hw_check(&local->hw, SIGNAL_DBM))
		bss_meta.signal = rx_status->signal * 100;
	else if (ieee80211_hw_check(&local->hw, SIGNAL_UNSPEC))
		bss_meta.signal = (rx_status->signal * 100) / local->hw.max_signal;

	bss_meta.chan = channel;

	rcu_read_lock();
	scan_sdata = rcu_dereference(local->scan_sdata);
	if (scan_sdata && scan_sdata->vif.type == NL80211_IFTYPE_STATION &&
	    scan_sdata->vif.cfg.assoc &&
	    ieee80211_have_rx_timestamp(rx_status)) {
		struct ieee80211_bss_conf *link_conf = NULL;

		/* for an MLO connection, set the TSF data only in case we have
		 * an indication on which of the links the frame was received
		 */
		if (ieee80211_vif_is_mld(&scan_sdata->vif)) {
			if (rx_status->link_valid) {
				s8 link_id = rx_status->link_id;

				link_conf =
					rcu_dereference(scan_sdata->vif.link_conf[link_id]);
			}
		} else {
			link_conf = &scan_sdata->vif.bss_conf;
		}

		if (link_conf) {
			bss_meta.parent_tsf =
				ieee80211_calculate_rx_timestamp(local,
								 rx_status,
								 len + FCS_LEN,
								 24);

			ether_addr_copy(bss_meta.parent_bssid,
					link_conf->bssid);
		}
	}
	rcu_read_unlock();

	cbss = cfg80211_inform_bss_frame_data(local->hw.wiphy, &bss_meta,
					      mgmt, len, GFP_ATOMIC);
	if (!cbss)
		return NULL;

	/* In case the signal is invalid update the status */
	signal_valid = channel == cbss->channel;
	if (!signal_valid)
		rx_status->flag |= RX_FLAG_NO_SIGNAL_VAL;

	return (void *)cbss->priv;
}

static bool ieee80211_scan_accept_presp(struct ieee80211_sub_if_data *sdata,
					struct ieee80211_channel *channel,
					u32 scan_flags, const u8 *da)
{
	if (!sdata)
		return false;

	/* accept broadcast on 6 GHz and for OCE */
	if (is_broadcast_ether_addr(da) &&
	    (channel->band == NL80211_BAND_6GHZ ||
	     scan_flags & NL80211_SCAN_FLAG_ACCEPT_BCAST_PROBE_RESP))
		return true;

	if (scan_flags & NL80211_SCAN_FLAG_RANDOM_ADDR)
		return true;
	return ether_addr_equal(da, sdata->vif.addr);
}

void ieee80211_scan_rx(struct ieee80211_local *local, struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct ieee80211_bss *bss;
	struct ieee80211_channel *channel;
	size_t min_hdr_len = offsetof(struct ieee80211_mgmt,
				      u.probe_resp.variable);

	if (!ieee80211_is_probe_resp(mgmt->frame_control) &&
	    !ieee80211_is_beacon(mgmt->frame_control) &&
	    !ieee80211_is_s1g_beacon(mgmt->frame_control))
		return;

	if (ieee80211_is_s1g_beacon(mgmt->frame_control)) {
		if (ieee80211_is_s1g_short_beacon(mgmt->frame_control))
			min_hdr_len = offsetof(struct ieee80211_ext,
					       u.s1g_short_beacon.variable);
		else
			min_hdr_len = offsetof(struct ieee80211_ext,
					       u.s1g_beacon);
	}

	if (skb->len < min_hdr_len)
		return;

	if (test_and_clear_bit(SCAN_BEACON_WAIT, &local->scanning)) {
		/*
		 * we were passive scanning because of radar/no-IR, but
		 * the beacon/proberesp rx gives us an opportunity to upgrade
		 * to active scan
		 */
		set_bit(SCAN_BEACON_DONE, &local->scanning);
		wiphy_delayed_work_queue(local->hw.wiphy, &local->scan_work, 0);
	}

	channel = ieee80211_get_channel_khz(local->hw.wiphy,
					    ieee80211_rx_status_to_khz(rx_status));

	if (!channel || channel->flags & IEEE80211_CHAN_DISABLED)
		return;

	if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		struct ieee80211_sub_if_data *sdata1, *sdata2;
		struct cfg80211_scan_request *scan_req;
		struct cfg80211_sched_scan_request *sched_scan_req;
		u32 scan_req_flags = 0, sched_scan_req_flags = 0;

		sdata1 = rcu_dereference(local->scan_sdata);
		sdata2 = rcu_dereference(local->sched_scan_sdata);

		if (likely(!sdata1 && !sdata2))
			return;

		scan_req = rcu_dereference(local->scan_req);
		sched_scan_req = rcu_dereference(local->sched_scan_req);

		if (scan_req)
			scan_req_flags = scan_req->flags;

		if (sched_scan_req)
			sched_scan_req_flags = sched_scan_req->flags;

		/* ignore ProbeResp to foreign address or non-bcast (OCE)
		 * unless scanning with randomised address
		 */
		if (!ieee80211_scan_accept_presp(sdata1, channel,
						 scan_req_flags,
						 mgmt->da) &&
		    !ieee80211_scan_accept_presp(sdata2, channel,
						 sched_scan_req_flags,
						 mgmt->da))
			return;
	} else {
		/* Beacons are expected only with broadcast address */
		if (!is_broadcast_ether_addr(mgmt->da))
			return;
	}

	/* Do not update the BSS table in case of only monitor interfaces */
	if (local->open_count == local->monitors)
		return;

	bss = ieee80211_bss_info_update(local, rx_status,
					mgmt, skb->len,
					channel);
	if (bss)
		ieee80211_rx_bss_put(local, bss);
}

static void ieee80211_prepare_scan_chandef(struct cfg80211_chan_def *chandef)
{
	memset(chandef, 0, sizeof(*chandef));

	chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
}

/* return false if no more work */
static bool ieee80211_prep_hw_scan(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct cfg80211_scan_request *req;
	struct cfg80211_chan_def chandef;
	u8 bands_used = 0;
	int i, ielen, n_chans;
	u32 flags = 0;

	req = rcu_dereference_protected(local->scan_req,
					lockdep_is_held(&local->hw.wiphy->mtx));

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
	ieee80211_prepare_scan_chandef(&chandef);

	if (req->flags & NL80211_SCAN_FLAG_MIN_PREQ_CONTENT)
		flags |= IEEE80211_PROBE_FLAG_MIN_CONTENT;

	ielen = ieee80211_build_preq_ies(sdata,
					 (u8 *)local->hw_scan_req->req.ie,
					 local->hw_scan_ies_bufsize,
					 &local->hw_scan_req->ies,
					 req->ie, req->ie_len,
					 bands_used, req->rates, &chandef,
					 flags);
	if (ielen < 0)
		return false;
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

	lockdep_assert_wiphy(local->hw.wiphy);

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

	scan_sdata = rcu_dereference_protected(local->scan_sdata,
					       lockdep_is_held(&local->hw.wiphy->mtx));

	if (hw_scan && !aborted &&
	    !ieee80211_hw_check(&local->hw, SINGLE_SCAN_ON_ALL_BANDS) &&
	    ieee80211_prep_hw_scan(scan_sdata)) {
		int rc;

		rc = drv_hw_scan(local,
			rcu_dereference_protected(local->scan_sdata,
						  lockdep_is_held(&local->hw.wiphy->mtx)),
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
					     lockdep_is_held(&local->hw.wiphy->mtx));

	RCU_INIT_POINTER(local->scan_req, NULL);
	RCU_INIT_POINTER(local->scan_sdata, NULL);

	local->scanning = 0;
	local->scan_chandef.chan = NULL;

	synchronize_rcu();

	if (scan_req != local->int_scan_req) {
		local->scan_info.aborted = aborted;
		cfg80211_scan_done(scan_req, &local->scan_info);
	}

	/* Set power back to normal operating levels. */
	ieee80211_hw_conf_chan(local);

	if (!hw_scan && was_scanning) {
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
			wiphy_work_queue(sdata->local->hw.wiphy, &sdata->work);
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

	wiphy_delayed_work_queue(local->hw.wiphy, &local->scan_work, 0);
}
EXPORT_SYMBOL(ieee80211_scan_completed);

static int ieee80211_start_sw_scan(struct ieee80211_local *local,
				   struct ieee80211_sub_if_data *sdata)
{
	/* Software scan is not supported in multi-channel cases */
	if (!local->emulate_chanctx)
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
	ieee80211_hw_conf_chan(local);

	wiphy_delayed_work_queue(local->hw.wiphy, &local->scan_work, 0);

	return 0;
}

static bool __ieee80211_can_leave_ch(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *sdata_iter;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (!ieee80211_is_radar_required(local))
		return true;

	if (!regulatory_pre_cac_allowed(local->hw.wiphy))
		return false;

	list_for_each_entry(sdata_iter, &local->interfaces, list) {
		if (sdata_iter->wdev.cac_started)
			return false;
	}

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
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!local->scan_req || local->scanning)
		return;

	if (!ieee80211_can_scan(local,
				rcu_dereference_protected(
					local->scan_sdata,
					lockdep_is_held(&local->hw.wiphy->mtx))))
		return;

	wiphy_delayed_work_queue(local->hw.wiphy, &local->scan_work,
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

	skb = ieee80211_build_probe_req(sdata, src, dst, ratemask, channel,
					ssid, ssid_len,
					ie, ie_len, flags);

	if (skb) {
		if (flags & IEEE80211_PROBE_FLAG_RANDOM_SN) {
			struct ieee80211_hdr *hdr = (void *)skb->data;
			struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
			u16 sn = get_random_u16();

			info->control.flags |= IEEE80211_TX_CTRL_NO_SEQNO;
			hdr->seq_ctrl =
				cpu_to_le16(IEEE80211_SN_TO_SEQ(sn));
		}
		IEEE80211_SKB_CB(skb)->flags |= tx_flags;
		IEEE80211_SKB_CB(skb)->control.flags |= IEEE80211_TX_CTRL_SCAN_TX;
		ieee80211_tx_skb_tid_band(sdata, skb, 7, channel->band);
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
					     lockdep_is_held(&local->hw.wiphy->mtx));

	tx_flags = IEEE80211_TX_INTFL_OFFCHAN_TX_OK;
	if (scan_req->no_cck)
		tx_flags |= IEEE80211_TX_CTL_NO_CCK_RATE;
	if (scan_req->flags & NL80211_SCAN_FLAG_MIN_PREQ_CONTENT)
		flags |= IEEE80211_PROBE_FLAG_MIN_CONTENT;
	if (scan_req->flags & NL80211_SCAN_FLAG_RANDOM_SN)
		flags |= IEEE80211_PROBE_FLAG_RANDOM_SN;

	sdata = rcu_dereference_protected(local->scan_sdata,
					  lockdep_is_held(&local->hw.wiphy->mtx));

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
	*next_delay = msecs_to_jiffies(scan_req->duration) >
		      IEEE80211_PROBE_DELAY + IEEE80211_CHANNEL_TIME ?
		      msecs_to_jiffies(scan_req->duration) - IEEE80211_PROBE_DELAY :
		      IEEE80211_CHANNEL_TIME;
	local->next_scan_state = SCAN_DECISION;
}

static int __ieee80211_start_scan(struct ieee80211_sub_if_data *sdata,
				  struct cfg80211_scan_request *req)
{
	struct ieee80211_local *local = sdata->local;
	bool hw_scan = local->ops->hw_scan;
	int rc;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (local->scan_req)
		return -EBUSY;

	/* For an MLO connection, if a link ID was specified, validate that it
	 * is indeed active.
	 */
	if (ieee80211_vif_is_mld(&sdata->vif) && req->tsf_report_link_id >= 0 &&
	    !(sdata->vif.active_links & BIT(req->tsf_report_link_id)))
		return -EINVAL;

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

		local->hw_scan_req = kmalloc(struct_size(local->hw_scan_req,
							 req.channels,
							 req->n_channels) +
					     local->hw_scan_ies_bufsize,
					     GFP_KERNEL);
		if (!local->hw_scan_req)
			return -ENOMEM;

		local->hw_scan_req->req.ssids = req->ssids;
		local->hw_scan_req->req.n_ssids = req->n_ssids;
		/* None of the channels are actually set
		 * up but let UBSAN know the boundaries.
		 */
		local->hw_scan_req->req.n_channels = req->n_channels;

		ies = (u8 *)local->hw_scan_req +
			sizeof(*local->hw_scan_req) +
			req->n_channels * sizeof(req->channels[0]);
		local->hw_scan_req->req.ie = ies;
		local->hw_scan_req->req.flags = req->flags;
		eth_broadcast_addr(local->hw_scan_req->req.bssid);
		local->hw_scan_req->req.duration = req->duration;
		local->hw_scan_req->req.duration_mandatory =
			req->duration_mandatory;
		local->hw_scan_req->req.tsf_report_link_id =
			req->tsf_report_link_id;

		local->hw_scan_band = 0;
		local->hw_scan_req->req.n_6ghz_params = req->n_6ghz_params;
		local->hw_scan_req->req.scan_6ghz_params =
			req->scan_6ghz_params;
		local->hw_scan_req->req.scan_6ghz = req->scan_6ghz;

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
		   (req->channels[0] == local->hw.conf.chandef.chan)) {
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
		ieee80211_hw_conf_chan(local);

		if ((req->channels[0]->flags & (IEEE80211_CHAN_NO_IR |
						IEEE80211_CHAN_RADAR)) ||
		    !req->n_ssids) {
			next_delay = IEEE80211_PASSIVE_CHANNEL_TIME;
			if (req->n_ssids)
				set_bit(SCAN_BEACON_WAIT, &local->scanning);
		} else {
			ieee80211_scan_state_send_probe(local, &next_delay);
			next_delay = IEEE80211_CHANNEL_TIME;
		}

		/* Now, just wait a bit and we are all done! */
		wiphy_delayed_work_queue(local->hw.wiphy, &local->scan_work,
					 next_delay);
		return 0;
	} else {
		/* Do normal software scan */
		__set_bit(SCAN_SW_SCANNING, &local->scanning);
	}

	ieee80211_recalc_idle(local);

	if (hw_scan) {
		WARN_ON(!ieee80211_prep_hw_scan(sdata));
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

	lockdep_assert_wiphy(local->hw.wiphy);

	/*
	 * check if at least one STA interface is associated,
	 * check if at least one STA interface has pending tx frames
	 * and grab the lowest used beacon interval
	 */
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

	scan_req = rcu_dereference_protected(local->scan_req,
					     lockdep_is_held(&local->hw.wiphy->mtx));

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
	struct cfg80211_scan_request *scan_req;

	scan_req = rcu_dereference_protected(local->scan_req,
					     lockdep_is_held(&local->hw.wiphy->mtx));

	skip = 0;
	chan = scan_req->channels[local->scan_channel_idx];

	local->scan_chandef.chan = chan;
	local->scan_chandef.center_freq1 = chan->center_freq;
	local->scan_chandef.freq1_offset = chan->freq_offset;
	local->scan_chandef.center_freq2 = 0;

	/* For scanning on the S1G band, detect the channel width according to
	 * the channel being scanned.
	 */
	if (chan->band == NL80211_BAND_S1GHZ) {
		local->scan_chandef.width = ieee80211_s1g_channel_width(chan);
		goto set_channel;
	}

	/* If scanning on oper channel, use whatever channel-type
	 * is currently in use.
	 */
	if (chan == local->hw.conf.chandef.chan)
		local->scan_chandef = local->hw.conf.chandef;
	else
		local->scan_chandef.width = NL80211_CHAN_WIDTH_20_NOHT;

set_channel:
	if (ieee80211_hw_conf_chan(local))
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
		*next_delay = msecs_to_jiffies(scan_req->duration) >
			      IEEE80211_PASSIVE_CHANNEL_TIME ?
			      msecs_to_jiffies(scan_req->duration) :
			      IEEE80211_PASSIVE_CHANNEL_TIME;
		local->next_scan_state = SCAN_DECISION;
		if (scan_req->n_ssids)
			set_bit(SCAN_BEACON_WAIT, &local->scanning);
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
	ieee80211_hw_conf_chan(local);

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

void ieee80211_scan_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, scan_work.work);
	struct ieee80211_sub_if_data *sdata;
	struct cfg80211_scan_request *scan_req;
	unsigned long next_delay = 0;
	bool aborted;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (!ieee80211_can_run_worker(local)) {
		aborted = true;
		goto out_complete;
	}

	sdata = rcu_dereference_protected(local->scan_sdata,
					  lockdep_is_held(&local->hw.wiphy->mtx));
	scan_req = rcu_dereference_protected(local->scan_req,
					     lockdep_is_held(&local->hw.wiphy->mtx));

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
		return;

	if (!local->scanning) {
		int rc;

		RCU_INIT_POINTER(local->scan_req, NULL);
		RCU_INIT_POINTER(local->scan_sdata, NULL);

		rc = __ieee80211_start_scan(sdata, scan_req);
		if (!rc)
			return;
		/* need to complete scan in cfg80211 */
		rcu_assign_pointer(local->scan_req, scan_req);
		aborted = true;
		goto out_complete;
	}

	clear_bit(SCAN_BEACON_WAIT, &local->scanning);

	/*
	 * as long as no delay is required advance immediately
	 * without scheduling a new work
	 */
	do {
		if (!ieee80211_sdata_running(sdata)) {
			aborted = true;
			goto out_complete;
		}

		if (test_and_clear_bit(SCAN_BEACON_DONE, &local->scanning) &&
		    local->next_scan_state == SCAN_DECISION)
			local->next_scan_state = SCAN_SEND_PROBE;

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

	wiphy_delayed_work_queue(local->hw.wiphy, &local->scan_work,
				 next_delay);
	return;

out_complete:
	__ieee80211_scan_completed(&local->hw, aborted);
}

int ieee80211_request_scan(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_scan_request *req)
{
	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	return __ieee80211_start_scan(sdata, req);
}

int ieee80211_request_ibss_scan(struct ieee80211_sub_if_data *sdata,
				const u8 *ssid, u8 ssid_len,
				struct ieee80211_channel **channels,
				unsigned int n_channels)
{
	struct ieee80211_local *local = sdata->local;
	int ret = -EBUSY, i, n_ch = 0;
	enum nl80211_band band;

	lockdep_assert_wiphy(local->hw.wiphy);

	/* busy scanning */
	if (local->scan_req)
		goto unlock;

	/* fill internal scan request */
	if (!channels) {
		int max_n;

		for (band = 0; band < NUM_NL80211_BANDS; band++) {
			if (!local->hw.wiphy->bands[band] ||
			    band == NL80211_BAND_6GHZ)
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
	memcpy(local->int_scan_req->ssids[0].ssid, ssid, IEEE80211_MAX_SSID_LEN);
	local->int_scan_req->ssids[0].ssid_len = ssid_len;

	ret = __ieee80211_start_scan(sdata, sdata->local->int_scan_req);
 unlock:
	return ret;
}

void ieee80211_scan_cancel(struct ieee80211_local *local)
{
	/* ensure a new scan cannot be queued */
	lockdep_assert_wiphy(local->hw.wiphy);

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

	if (!local->scan_req)
		return;

	/*
	 * We have a scan running and the driver already reported completion,
	 * but the worker hasn't run yet or is stuck on the mutex - mark it as
	 * cancelled.
	 */
	if (test_bit(SCAN_HW_SCANNING, &local->scanning) &&
	    test_bit(SCAN_COMPLETED, &local->scanning)) {
		set_bit(SCAN_HW_CANCELLED, &local->scanning);
		return;
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
						lockdep_is_held(&local->hw.wiphy->mtx)));
		return;
	}

	wiphy_delayed_work_cancel(local->hw.wiphy, &local->scan_work);
	/* and clean up */
	memset(&local->scan_info, 0, sizeof(local->scan_info));
	__ieee80211_scan_completed(&local->hw, true);
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

	lockdep_assert_wiphy(local->hw.wiphy);

	iebufsz = local->scan_ies_len + req->ie_len;

	if (!local->ops->sched_scan_start)
		return -EOPNOTSUPP;

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

	ieee80211_prepare_scan_chandef(&chandef);

	ret = ieee80211_build_preq_ies(sdata, ie, num_bands * iebufsz,
				       &sched_scan_ies, req->ie,
				       req->ie_len, bands_used, rate_masks,
				       &chandef, flags);
	if (ret < 0)
		goto error;

	ret = drv_sched_scan_start(local, sdata, req, &sched_scan_ies);
	if (ret == 0) {
		rcu_assign_pointer(local->sched_scan_sdata, sdata);
		rcu_assign_pointer(local->sched_scan_req, req);
	}

error:
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

	lockdep_assert_wiphy(local->hw.wiphy);

	if (rcu_access_pointer(local->sched_scan_sdata))
		return -EBUSY;

	return __ieee80211_request_sched_scan_start(sdata, req);
}

int ieee80211_request_sched_scan_stop(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sched_scan_sdata;
	int ret = -ENOENT;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (!local->ops->sched_scan_stop)
		return -EOPNOTSUPP;

	/* We don't want to restart sched scan anymore. */
	RCU_INIT_POINTER(local->sched_scan_req, NULL);

	sched_scan_sdata = rcu_dereference_protected(local->sched_scan_sdata,
						lockdep_is_held(&local->hw.wiphy->mtx));
	if (sched_scan_sdata) {
		ret = drv_sched_scan_stop(local, sched_scan_sdata);
		if (!ret)
			RCU_INIT_POINTER(local->sched_scan_sdata, NULL);
	}

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
	lockdep_assert_wiphy(local->hw.wiphy);

	if (!rcu_access_pointer(local->sched_scan_sdata))
		return;

	RCU_INIT_POINTER(local->sched_scan_sdata, NULL);

	/* If sched scan was aborted by the driver. */
	RCU_INIT_POINTER(local->sched_scan_req, NULL);

	cfg80211_sched_scan_stopped_locked(local->hw.wiphy, 0);
}

void ieee80211_sched_scan_stopped_work(struct wiphy *wiphy,
				       struct wiphy_work *work)
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

	wiphy_work_queue(hw->wiphy, &local->sched_scan_stopped_work);
}
EXPORT_SYMBOL(ieee80211_sched_scan_stopped);
