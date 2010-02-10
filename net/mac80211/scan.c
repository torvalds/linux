/*
 * Scanning implementation
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/wireless.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <net/mac80211.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "mesh.h"

#define IEEE80211_PROBE_DELAY (HZ / 33)
#define IEEE80211_CHANNEL_TIME (HZ / 33)
#define IEEE80211_PASSIVE_CHANNEL_TIME (HZ / 8)

struct ieee80211_bss *
ieee80211_rx_bss_get(struct ieee80211_local *local, u8 *bssid, int freq,
		     u8 *ssid, u8 ssid_len)
{
	return (void *)cfg80211_get_bss(local->hw.wiphy,
					ieee80211_get_channel(local->hw.wiphy,
							      freq),
					bssid, ssid, ssid_len,
					0, 0);
}

static void ieee80211_rx_bss_free(struct cfg80211_bss *cbss)
{
	struct ieee80211_bss *bss = (void *)cbss;

	kfree(bss_mesh_id(bss));
	kfree(bss_mesh_cfg(bss));
}

void ieee80211_rx_bss_put(struct ieee80211_local *local,
			  struct ieee80211_bss *bss)
{
	cfg80211_put_bss((struct cfg80211_bss *)bss);
}

struct ieee80211_bss *
ieee80211_bss_info_update(struct ieee80211_local *local,
			  struct ieee80211_rx_status *rx_status,
			  struct ieee80211_mgmt *mgmt,
			  size_t len,
			  struct ieee802_11_elems *elems,
			  struct ieee80211_channel *channel,
			  bool beacon)
{
	struct ieee80211_bss *bss;
	int clen;
	s32 signal = 0;

	if (local->hw.flags & IEEE80211_HW_SIGNAL_DBM)
		signal = rx_status->signal * 100;
	else if (local->hw.flags & IEEE80211_HW_SIGNAL_UNSPEC)
		signal = (rx_status->signal * 100) / local->hw.max_signal;

	bss = (void *)cfg80211_inform_bss_frame(local->hw.wiphy, channel,
						mgmt, len, signal, GFP_ATOMIC);

	if (!bss)
		return NULL;

	bss->cbss.free_priv = ieee80211_rx_bss_free;

	/* save the ERP value so that it is available at association time */
	if (elems->erp_info && elems->erp_info_len >= 1) {
		bss->erp_value = elems->erp_info[0];
		bss->has_erp_value = 1;
	}

	if (elems->tim) {
		struct ieee80211_tim_ie *tim_ie =
			(struct ieee80211_tim_ie *)elems->tim;
		bss->dtim_period = tim_ie->dtim_period;
	}

	/* set default value for buggy AP/no TIM element */
	if (bss->dtim_period == 0)
		bss->dtim_period = 1;

	bss->supp_rates_len = 0;
	if (elems->supp_rates) {
		clen = IEEE80211_MAX_SUPP_RATES - bss->supp_rates_len;
		if (clen > elems->supp_rates_len)
			clen = elems->supp_rates_len;
		memcpy(&bss->supp_rates[bss->supp_rates_len], elems->supp_rates,
		       clen);
		bss->supp_rates_len += clen;
	}
	if (elems->ext_supp_rates) {
		clen = IEEE80211_MAX_SUPP_RATES - bss->supp_rates_len;
		if (clen > elems->ext_supp_rates_len)
			clen = elems->ext_supp_rates_len;
		memcpy(&bss->supp_rates[bss->supp_rates_len],
		       elems->ext_supp_rates, clen);
		bss->supp_rates_len += clen;
	}

	bss->wmm_used = elems->wmm_param || elems->wmm_info;

	if (!beacon)
		bss->last_probe_resp = jiffies;

	return bss;
}

ieee80211_rx_result
ieee80211_scan_rx(struct ieee80211_sub_if_data *sdata, struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_bss *bss;
	u8 *elements;
	struct ieee80211_channel *channel;
	size_t baselen;
	int freq;
	__le16 fc;
	bool presp, beacon = false;
	struct ieee802_11_elems elems;

	if (skb->len < 2)
		return RX_DROP_UNUSABLE;

	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = mgmt->frame_control;

	if (ieee80211_is_ctl(fc))
		return RX_CONTINUE;

	if (skb->len < 24)
		return RX_DROP_MONITOR;

	presp = ieee80211_is_probe_resp(fc);
	if (presp) {
		/* ignore ProbeResp to foreign address */
		if (memcmp(mgmt->da, sdata->dev->dev_addr, ETH_ALEN))
			return RX_DROP_MONITOR;

		presp = true;
		elements = mgmt->u.probe_resp.variable;
		baselen = offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
	} else {
		beacon = ieee80211_is_beacon(fc);
		baselen = offsetof(struct ieee80211_mgmt, u.beacon.variable);
		elements = mgmt->u.beacon.variable;
	}

	if (!presp && !beacon)
		return RX_CONTINUE;

	if (baselen > skb->len)
		return RX_DROP_MONITOR;

	ieee802_11_parse_elems(elements, skb->len - baselen, &elems);

	if (elems.ds_params && elems.ds_params_len == 1)
		freq = ieee80211_channel_to_frequency(elems.ds_params[0]);
	else
		freq = rx_status->freq;

	channel = ieee80211_get_channel(sdata->local->hw.wiphy, freq);

	if (!channel || channel->flags & IEEE80211_CHAN_DISABLED)
		return RX_DROP_MONITOR;

	bss = ieee80211_bss_info_update(sdata->local, rx_status,
					mgmt, skb->len, &elems,
					channel, beacon);
	if (bss)
		ieee80211_rx_bss_put(sdata->local, bss);

	dev_kfree_skb(skb);
	return RX_QUEUED;
}

/* return false if no more work */
static bool ieee80211_prep_hw_scan(struct ieee80211_local *local)
{
	struct cfg80211_scan_request *req = local->scan_req;
	enum ieee80211_band band;
	int i, ielen, n_chans;

	do {
		if (local->hw_scan_band == IEEE80211_NUM_BANDS)
			return false;

		band = local->hw_scan_band;
		n_chans = 0;
		for (i = 0; i < req->n_channels; i++) {
			if (req->channels[i]->band == band) {
				local->hw_scan_req->channels[n_chans] =
							req->channels[i];
				n_chans++;
			}
		}

		local->hw_scan_band++;
	} while (!n_chans);

	local->hw_scan_req->n_channels = n_chans;

	ielen = ieee80211_build_preq_ies(local, (u8 *)local->hw_scan_req->ie,
					 req->ie, req->ie_len, band);
	local->hw_scan_req->ie_len = ielen;

	return true;
}

/*
 * inform AP that we will go to sleep so that it will buffer the frames
 * while we scan
 */
static void ieee80211_scan_ps_enable(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;

	local->scan_ps_enabled = false;

	/* FIXME: what to do when local->pspolling is true? */

	del_timer_sync(&local->dynamic_ps_timer);
	cancel_work_sync(&local->dynamic_ps_enable_work);

	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		local->scan_ps_enabled = true;
		local->hw.conf.flags &= ~IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}

	if (!(local->scan_ps_enabled) ||
	    !(local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK))
		/*
		 * If power save was enabled, no need to send a nullfunc
		 * frame because AP knows that we are sleeping. But if the
		 * hardware is creating the nullfunc frame for power save
		 * status (ie. IEEE80211_HW_PS_NULLFUNC_STACK is not
		 * enabled) and power save was enabled, the firmware just
		 * sent a null frame with power save disabled. So we need
		 * to send a new nullfunc frame to inform the AP that we
		 * are again sleeping.
		 */
		ieee80211_send_nullfunc(local, sdata, 1);
}

/* inform AP that we are awake again, unless power save is enabled */
static void ieee80211_scan_ps_disable(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;

	if (!local->ps_sdata)
		ieee80211_send_nullfunc(local, sdata, 0);
	else if (local->scan_ps_enabled) {
		/*
		 * In !IEEE80211_HW_PS_NULLFUNC_STACK case the hardware
		 * will send a nullfunc frame with the powersave bit set
		 * even though the AP already knows that we are sleeping.
		 * This could be avoided by sending a null frame with power
		 * save bit disabled before enabling the power save, but
		 * this doesn't gain anything.
		 *
		 * When IEEE80211_HW_PS_NULLFUNC_STACK is enabled, no need
		 * to send a nullfunc frame because AP already knows that
		 * we are sleeping, let's just enable power save mode in
		 * hardware.
		 */
		local->hw.conf.flags |= IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	} else if (local->hw.conf.dynamic_ps_timeout > 0) {
		/*
		 * If IEEE80211_CONF_PS was not set and the dynamic_ps_timer
		 * had been running before leaving the operating channel,
		 * restart the timer now and send a nullfunc frame to inform
		 * the AP that we are awake.
		 */
		ieee80211_send_nullfunc(local, sdata, 0);
		mod_timer(&local->dynamic_ps_timer, jiffies +
			  msecs_to_jiffies(local->hw.conf.dynamic_ps_timeout));
	}
}

void ieee80211_scan_completed(struct ieee80211_hw *hw, bool aborted)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;
	bool was_hw_scan;

	mutex_lock(&local->scan_mtx);

	/*
	 * It's ok to abort a not-yet-running scan (that
	 * we have one at all will be verified by checking
	 * local->scan_req next), but not to complete it
	 * successfully.
	 */
	if (WARN_ON(!local->scanning && !aborted))
		aborted = true;

	if (WARN_ON(!local->scan_req)) {
		mutex_unlock(&local->scan_mtx);
		return;
	}

	was_hw_scan = test_bit(SCAN_HW_SCANNING, &local->scanning);
	if (was_hw_scan && !aborted && ieee80211_prep_hw_scan(local)) {
		ieee80211_queue_delayed_work(&local->hw,
					     &local->scan_work, 0);
		mutex_unlock(&local->scan_mtx);
		return;
	}

	kfree(local->hw_scan_req);
	local->hw_scan_req = NULL;

	if (local->scan_req != local->int_scan_req)
		cfg80211_scan_done(local->scan_req, aborted);
	local->scan_req = NULL;
	local->scan_sdata = NULL;

	local->scanning = 0;
	local->scan_channel = NULL;

	/* we only have to protect scan_req and hw/sw scan */
	mutex_unlock(&local->scan_mtx);

	ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);
	if (was_hw_scan)
		goto done;

	ieee80211_configure_filter(local);

	drv_sw_scan_complete(local);

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;

		/* Tell AP we're back */
		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			if (sdata->u.mgd.associated) {
				ieee80211_scan_ps_disable(sdata);
				netif_tx_wake_all_queues(sdata->dev);
			}
		} else
			netif_tx_wake_all_queues(sdata->dev);

		/* re-enable beaconing */
		if (sdata->vif.type == NL80211_IFTYPE_AP ||
		    sdata->vif.type == NL80211_IFTYPE_ADHOC ||
		    sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
			ieee80211_bss_info_change_notify(
				sdata, BSS_CHANGED_BEACON_ENABLED);
	}
	mutex_unlock(&local->iflist_mtx);

 done:
	ieee80211_recalc_idle(local);
	ieee80211_mlme_notify_scan_completed(local);
	ieee80211_ibss_notify_scan_completed(local);
	ieee80211_mesh_notify_scan_completed(local);
}
EXPORT_SYMBOL(ieee80211_scan_completed);

static int ieee80211_start_sw_scan(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

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
	drv_sw_scan_start(local);

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;

		/* disable beaconing */
		if (sdata->vif.type == NL80211_IFTYPE_AP ||
		    sdata->vif.type == NL80211_IFTYPE_ADHOC ||
		    sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
			ieee80211_bss_info_change_notify(
				sdata, BSS_CHANGED_BEACON_ENABLED);

		/*
		 * only handle non-STA interfaces here, STA interfaces
		 * are handled in the scan state machine
		 */
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			netif_tx_stop_all_queues(sdata->dev);
	}
	mutex_unlock(&local->iflist_mtx);

	local->next_scan_state = SCAN_DECISION;
	local->scan_channel_idx = 0;

	ieee80211_configure_filter(local);

	/* TODO: start scan as soon as all nullfunc frames are ACKed */
	ieee80211_queue_delayed_work(&local->hw,
				     &local->scan_work,
				     IEEE80211_CHANNEL_TIME);

	return 0;
}


static int __ieee80211_start_scan(struct ieee80211_sub_if_data *sdata,
				  struct cfg80211_scan_request *req)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	int rc;

	if (local->scan_req)
		return -EBUSY;

	if (req != local->int_scan_req &&
	    sdata->vif.type == NL80211_IFTYPE_STATION &&
	    !list_empty(&ifmgd->work_list)) {
		/* actually wait for the work it's doing to finish/time out */
		set_bit(IEEE80211_STA_REQ_SCAN, &ifmgd->request);
		local->scan_req = req;
		local->scan_sdata = sdata;
		return 0;
	}

	if (local->ops->hw_scan) {
		u8 *ies;

		local->hw_scan_req = kmalloc(
				sizeof(*local->hw_scan_req) +
				req->n_channels * sizeof(req->channels[0]) +
				2 + IEEE80211_MAX_SSID_LEN + local->scan_ies_len +
				req->ie_len, GFP_KERNEL);
		if (!local->hw_scan_req)
			return -ENOMEM;

		local->hw_scan_req->ssids = req->ssids;
		local->hw_scan_req->n_ssids = req->n_ssids;
		ies = (u8 *)local->hw_scan_req +
			sizeof(*local->hw_scan_req) +
			req->n_channels * sizeof(req->channels[0]);
		local->hw_scan_req->ie = ies;

		local->hw_scan_band = 0;
	}

	local->scan_req = req;
	local->scan_sdata = sdata;

	if (local->ops->hw_scan)
		__set_bit(SCAN_HW_SCANNING, &local->scanning);
	else
		__set_bit(SCAN_SW_SCANNING, &local->scanning);
	/*
	 * Kicking off the scan need not be protected,
	 * only the scan variable stuff, since now
	 * local->scan_req is assigned and other callers
	 * will abort their scan attempts.
	 *
	 * This avoids getting a scan_mtx -> iflist_mtx
	 * dependency, so that the scan completed calls
	 * have more locking freedom.
	 */

	ieee80211_recalc_idle(local);
	mutex_unlock(&local->scan_mtx);

	if (local->ops->hw_scan) {
		WARN_ON(!ieee80211_prep_hw_scan(local));
		rc = drv_hw_scan(local, local->hw_scan_req);
	} else
		rc = ieee80211_start_sw_scan(local);

	mutex_lock(&local->scan_mtx);

	if (rc) {
		kfree(local->hw_scan_req);
		local->hw_scan_req = NULL;
		local->scanning = 0;

		ieee80211_recalc_idle(local);

		local->scan_req = NULL;
		local->scan_sdata = NULL;
	}

	return rc;
}

static int ieee80211_scan_state_decision(struct ieee80211_local *local,
					 unsigned long *next_delay)
{
	bool associated = false;
	struct ieee80211_sub_if_data *sdata;

	/* if no more bands/channels left, complete scan and advance to the idle state */
	if (local->scan_channel_idx >= local->scan_req->n_channels) {
		ieee80211_scan_completed(&local->hw, false);
		return 1;
	}

	/* check if at least one STA interface is associated */
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			if (sdata->u.mgd.associated) {
				associated = true;
				break;
			}
		}
	}
	mutex_unlock(&local->iflist_mtx);

	if (local->scan_channel) {
		/*
		 * we're currently scanning a different channel, let's
		 * switch back to the operating channel now if at least
		 * one interface is associated. Otherwise just scan the
		 * next channel
		 */
		if (associated)
			local->next_scan_state = SCAN_ENTER_OPER_CHANNEL;
		else
			local->next_scan_state = SCAN_SET_CHANNEL;
	} else {
		/*
		 * we're on the operating channel currently, let's
		 * leave that channel now to scan another one
		 */
		local->next_scan_state = SCAN_LEAVE_OPER_CHANNEL;
	}

	*next_delay = 0;
	return 0;
}

static void ieee80211_scan_state_leave_oper_channel(struct ieee80211_local *local,
						    unsigned long *next_delay)
{
	struct ieee80211_sub_if_data *sdata;

	/*
	 * notify the AP about us leaving the channel and stop all STA interfaces
	 */
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			netif_tx_stop_all_queues(sdata->dev);
			if (sdata->u.mgd.associated)
				ieee80211_scan_ps_enable(sdata);
		}
	}
	mutex_unlock(&local->iflist_mtx);

	__set_bit(SCAN_OFF_CHANNEL, &local->scanning);

	/* advance to the next channel to be scanned */
	*next_delay = HZ / 10;
	local->next_scan_state = SCAN_SET_CHANNEL;
}

static void ieee80211_scan_state_enter_oper_channel(struct ieee80211_local *local,
						    unsigned long *next_delay)
{
	struct ieee80211_sub_if_data *sdata = local->scan_sdata;

	/* switch back to the operating channel */
	local->scan_channel = NULL;
	ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);

	/*
	 * notify the AP about us being back and restart all STA interfaces
	 */
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;

		/* Tell AP we're back */
		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			if (sdata->u.mgd.associated)
				ieee80211_scan_ps_disable(sdata);
			netif_tx_wake_all_queues(sdata->dev);
		}
	}
	mutex_unlock(&local->iflist_mtx);

	__clear_bit(SCAN_OFF_CHANNEL, &local->scanning);

	*next_delay = HZ / 5;
	local->next_scan_state = SCAN_DECISION;
}

static void ieee80211_scan_state_set_channel(struct ieee80211_local *local,
					     unsigned long *next_delay)
{
	int skip;
	struct ieee80211_channel *chan;

	skip = 0;
	chan = local->scan_req->channels[local->scan_channel_idx];

	local->scan_channel = chan;
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
	if (chan->flags & IEEE80211_CHAN_PASSIVE_SCAN ||
	    !local->scan_req->n_ssids) {
		*next_delay = IEEE80211_PASSIVE_CHANNEL_TIME;
		local->next_scan_state = SCAN_DECISION;
		return;
	}

	/* active scan, send probes */
	*next_delay = IEEE80211_PROBE_DELAY;
	local->next_scan_state = SCAN_SEND_PROBE;
}

static void ieee80211_scan_state_send_probe(struct ieee80211_local *local,
					    unsigned long *next_delay)
{
	int i;
	struct ieee80211_sub_if_data *sdata = local->scan_sdata;

	for (i = 0; i < local->scan_req->n_ssids; i++)
		ieee80211_send_probe_req(
			sdata, NULL,
			local->scan_req->ssids[i].ssid,
			local->scan_req->ssids[i].ssid_len,
			local->scan_req->ie, local->scan_req->ie_len);

	/*
	 * After sending probe requests, wait for probe responses
	 * on the channel.
	 */
	*next_delay = IEEE80211_CHANNEL_TIME;
	local->next_scan_state = SCAN_DECISION;
}

void ieee80211_scan_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, scan_work.work);
	struct ieee80211_sub_if_data *sdata = local->scan_sdata;
	unsigned long next_delay = 0;

	mutex_lock(&local->scan_mtx);
	if (!sdata || !local->scan_req) {
		mutex_unlock(&local->scan_mtx);
		return;
	}

	if (local->hw_scan_req) {
		int rc = drv_hw_scan(local, local->hw_scan_req);
		mutex_unlock(&local->scan_mtx);
		if (rc)
			ieee80211_scan_completed(&local->hw, true);
		return;
	}

	if (local->scan_req && !local->scanning) {
		struct cfg80211_scan_request *req = local->scan_req;
		int rc;

		local->scan_req = NULL;
		local->scan_sdata = NULL;

		rc = __ieee80211_start_scan(sdata, req);
		mutex_unlock(&local->scan_mtx);

		if (rc)
			ieee80211_scan_completed(&local->hw, true);
		return;
	}

	mutex_unlock(&local->scan_mtx);

	/*
	 * Avoid re-scheduling when the sdata is going away.
	 */
	if (!netif_running(sdata->dev)) {
		ieee80211_scan_completed(&local->hw, true);
		return;
	}

	/*
	 * as long as no delay is required advance immediately
	 * without scheduling a new work
	 */
	do {
		switch (local->next_scan_state) {
		case SCAN_DECISION:
			if (ieee80211_scan_state_decision(local, &next_delay))
				return;
			break;
		case SCAN_SET_CHANNEL:
			ieee80211_scan_state_set_channel(local, &next_delay);
			break;
		case SCAN_SEND_PROBE:
			ieee80211_scan_state_send_probe(local, &next_delay);
			break;
		case SCAN_LEAVE_OPER_CHANNEL:
			ieee80211_scan_state_leave_oper_channel(local, &next_delay);
			break;
		case SCAN_ENTER_OPER_CHANNEL:
			ieee80211_scan_state_enter_oper_channel(local, &next_delay);
			break;
		}
	} while (next_delay == 0);

	ieee80211_queue_delayed_work(&local->hw, &local->scan_work, next_delay);
}

int ieee80211_request_scan(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_scan_request *req)
{
	int res;

	mutex_lock(&sdata->local->scan_mtx);
	res = __ieee80211_start_scan(sdata, req);
	mutex_unlock(&sdata->local->scan_mtx);

	return res;
}

int ieee80211_request_internal_scan(struct ieee80211_sub_if_data *sdata,
				    const u8 *ssid, u8 ssid_len)
{
	struct ieee80211_local *local = sdata->local;
	int ret = -EBUSY;

	mutex_lock(&local->scan_mtx);

	/* busy scanning */
	if (local->scan_req)
		goto unlock;

	memcpy(local->int_scan_req->ssids[0].ssid, ssid, IEEE80211_MAX_SSID_LEN);
	local->int_scan_req->ssids[0].ssid_len = ssid_len;

	ret = __ieee80211_start_scan(sdata, sdata->local->int_scan_req);
 unlock:
	mutex_unlock(&local->scan_mtx);
	return ret;
}

void ieee80211_scan_cancel(struct ieee80211_local *local)
{
	bool abortscan;

	cancel_delayed_work_sync(&local->scan_work);

	/*
	 * Only call this function when a scan can't be
	 * queued -- mostly at suspend under RTNL.
	 */
	mutex_lock(&local->scan_mtx);
	abortscan = test_bit(SCAN_SW_SCANNING, &local->scanning) ||
		    (!local->scanning && local->scan_req);
	mutex_unlock(&local->scan_mtx);

	if (abortscan)
		ieee80211_scan_completed(&local->hw, true);
}
