/*
 * IBSS mode implementation
 * Copyright 2003-2008, Jouni Malinen <j@w1.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2009, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 * Copyright(c) 2018-2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <net/mac80211.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"

#define IEEE80211_SCAN_INTERVAL (2 * HZ)
#define IEEE80211_IBSS_JOIN_TIMEOUT (7 * HZ)

#define IEEE80211_IBSS_MERGE_INTERVAL (30 * HZ)
#define IEEE80211_IBSS_INACTIVITY_LIMIT (60 * HZ)
#define IEEE80211_IBSS_RSN_INACTIVITY_LIMIT (10 * HZ)

#define IEEE80211_IBSS_MAX_STA_ENTRIES 128

static struct beacon_data *
ieee80211_ibss_build_presp(struct ieee80211_sub_if_data *sdata,
			   const int beacon_int, const u32 basic_rates,
			   const u16 capability, u64 tsf,
			   struct cfg80211_chan_def *chandef,
			   bool *have_higher_than_11mbit,
			   struct cfg80211_csa_settings *csa_settings)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	struct ieee80211_local *local = sdata->local;
	int rates_n = 0, i, ri;
	struct ieee80211_mgmt *mgmt;
	u8 *pos;
	struct ieee80211_supported_band *sband;
	u32 rate_flags, rates = 0, rates_added = 0;
	struct beacon_data *presp;
	int frame_len;
	int shift;

	/* Build IBSS probe response */
	frame_len = sizeof(struct ieee80211_hdr_3addr) +
		    12 /* struct ieee80211_mgmt.u.beacon */ +
		    2 + IEEE80211_MAX_SSID_LEN /* max SSID */ +
		    2 + 8 /* max Supported Rates */ +
		    3 /* max DS params */ +
		    4 /* IBSS params */ +
		    5 /* Channel Switch Announcement */ +
		    2 + (IEEE80211_MAX_SUPP_RATES - 8) +
		    2 + sizeof(struct ieee80211_ht_cap) +
		    2 + sizeof(struct ieee80211_ht_operation) +
		    2 + sizeof(struct ieee80211_vht_cap) +
		    2 + sizeof(struct ieee80211_vht_operation) +
		    ifibss->ie_len;
	presp = kzalloc(sizeof(*presp) + frame_len, GFP_KERNEL);
	if (!presp)
		return NULL;

	presp->head = (void *)(presp + 1);

	mgmt = (void *) presp->head;
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_PROBE_RESP);
	eth_broadcast_addr(mgmt->da);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifibss->bssid, ETH_ALEN);
	mgmt->u.beacon.beacon_int = cpu_to_le16(beacon_int);
	mgmt->u.beacon.timestamp = cpu_to_le64(tsf);
	mgmt->u.beacon.capab_info = cpu_to_le16(capability);

	pos = (u8 *)mgmt + offsetof(struct ieee80211_mgmt, u.beacon.variable);

	*pos++ = WLAN_EID_SSID;
	*pos++ = ifibss->ssid_len;
	memcpy(pos, ifibss->ssid, ifibss->ssid_len);
	pos += ifibss->ssid_len;

	sband = local->hw.wiphy->bands[chandef->chan->band];
	rate_flags = ieee80211_chandef_rate_flags(chandef);
	shift = ieee80211_chandef_get_shift(chandef);
	rates_n = 0;
	if (have_higher_than_11mbit)
		*have_higher_than_11mbit = false;

	for (i = 0; i < sband->n_bitrates; i++) {
		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			continue;
		if (sband->bitrates[i].bitrate > 110 &&
		    have_higher_than_11mbit)
			*have_higher_than_11mbit = true;

		rates |= BIT(i);
		rates_n++;
	}

	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = min_t(int, 8, rates_n);
	for (ri = 0; ri < sband->n_bitrates; ri++) {
		int rate = DIV_ROUND_UP(sband->bitrates[ri].bitrate,
					5 * (1 << shift));
		u8 basic = 0;
		if (!(rates & BIT(ri)))
			continue;

		if (basic_rates & BIT(ri))
			basic = 0x80;
		*pos++ = basic | (u8) rate;
		if (++rates_added == 8) {
			ri++; /* continue at next rate for EXT_SUPP_RATES */
			break;
		}
	}

	if (sband->band == NL80211_BAND_2GHZ) {
		*pos++ = WLAN_EID_DS_PARAMS;
		*pos++ = 1;
		*pos++ = ieee80211_frequency_to_channel(
				chandef->chan->center_freq);
	}

	*pos++ = WLAN_EID_IBSS_PARAMS;
	*pos++ = 2;
	/* FIX: set ATIM window based on scan results */
	*pos++ = 0;
	*pos++ = 0;

	if (csa_settings) {
		*pos++ = WLAN_EID_CHANNEL_SWITCH;
		*pos++ = 3;
		*pos++ = csa_settings->block_tx ? 1 : 0;
		*pos++ = ieee80211_frequency_to_channel(
				csa_settings->chandef.chan->center_freq);
		presp->csa_counter_offsets[0] = (pos - presp->head);
		*pos++ = csa_settings->count;
		presp->csa_current_counter = csa_settings->count;
	}

	/* put the remaining rates in WLAN_EID_EXT_SUPP_RATES */
	if (rates_n > 8) {
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = rates_n - 8;
		for (; ri < sband->n_bitrates; ri++) {
			int rate = DIV_ROUND_UP(sband->bitrates[ri].bitrate,
						5 * (1 << shift));
			u8 basic = 0;
			if (!(rates & BIT(ri)))
				continue;

			if (basic_rates & BIT(ri))
				basic = 0x80;
			*pos++ = basic | (u8) rate;
		}
	}

	if (ifibss->ie_len) {
		memcpy(pos, ifibss->ie, ifibss->ie_len);
		pos += ifibss->ie_len;
	}

	/* add HT capability and information IEs */
	if (chandef->width != NL80211_CHAN_WIDTH_20_NOHT &&
	    chandef->width != NL80211_CHAN_WIDTH_5 &&
	    chandef->width != NL80211_CHAN_WIDTH_10 &&
	    sband->ht_cap.ht_supported) {
		struct ieee80211_sta_ht_cap ht_cap;

		memcpy(&ht_cap, &sband->ht_cap, sizeof(ht_cap));
		ieee80211_apply_htcap_overrides(sdata, &ht_cap);

		pos = ieee80211_ie_build_ht_cap(pos, &ht_cap, ht_cap.cap);
		/*
		 * Note: According to 802.11n-2009 9.13.3.1, HT Protection
		 * field and RIFS Mode are reserved in IBSS mode, therefore
		 * keep them at 0
		 */
		pos = ieee80211_ie_build_ht_oper(pos, &sband->ht_cap,
						 chandef, 0, false);

		/* add VHT capability and information IEs */
		if (chandef->width != NL80211_CHAN_WIDTH_20 &&
		    chandef->width != NL80211_CHAN_WIDTH_40 &&
		    sband->vht_cap.vht_supported) {
			pos = ieee80211_ie_build_vht_cap(pos, &sband->vht_cap,
							 sband->vht_cap.cap);
			pos = ieee80211_ie_build_vht_oper(pos, &sband->vht_cap,
							  chandef);
		}
	}

	if (local->hw.queues >= IEEE80211_NUM_ACS)
		pos = ieee80211_add_wmm_info_ie(pos, 0); /* U-APSD not in use */

	presp->head_len = pos - presp->head;
	if (WARN_ON(presp->head_len > frame_len))
		goto error;

	return presp;
error:
	kfree(presp);
	return NULL;
}

static void __ieee80211_sta_join_ibss(struct ieee80211_sub_if_data *sdata,
				      const u8 *bssid, const int beacon_int,
				      struct cfg80211_chan_def *req_chandef,
				      const u32 basic_rates,
				      const u16 capability, u64 tsf,
				      bool creator)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_mgmt *mgmt;
	struct cfg80211_bss *bss;
	u32 bss_change;
	struct cfg80211_chan_def chandef;
	struct ieee80211_channel *chan;
	struct beacon_data *presp;
	struct cfg80211_inform_bss bss_meta = {};
	bool have_higher_than_11mbit;
	bool radar_required;
	int err;

	sdata_assert_lock(sdata);

	/* Reset own TSF to allow time synchronization work. */
	drv_reset_tsf(local, sdata);

	if (!ether_addr_equal(ifibss->bssid, bssid))
		sta_info_flush(sdata);

	/* if merging, indicate to driver that we leave the old IBSS */
	if (sdata->vif.bss_conf.ibss_joined) {
		sdata->vif.bss_conf.ibss_joined = false;
		sdata->vif.bss_conf.ibss_creator = false;
		sdata->vif.bss_conf.enable_beacon = false;
		netif_carrier_off(sdata->dev);
		ieee80211_bss_info_change_notify(sdata,
						 BSS_CHANGED_IBSS |
						 BSS_CHANGED_BEACON_ENABLED);
		drv_leave_ibss(local, sdata);
	}

	presp = rcu_dereference_protected(ifibss->presp,
					  lockdep_is_held(&sdata->wdev.mtx));
	RCU_INIT_POINTER(ifibss->presp, NULL);
	if (presp)
		kfree_rcu(presp, rcu_head);

	/* make a copy of the chandef, it could be modified below. */
	chandef = *req_chandef;
	chan = chandef.chan;
	if (!cfg80211_reg_can_beacon(local->hw.wiphy, &chandef,
				     NL80211_IFTYPE_ADHOC)) {
		if (chandef.width == NL80211_CHAN_WIDTH_5 ||
		    chandef.width == NL80211_CHAN_WIDTH_10 ||
		    chandef.width == NL80211_CHAN_WIDTH_20_NOHT ||
		    chandef.width == NL80211_CHAN_WIDTH_20) {
			sdata_info(sdata,
				   "Failed to join IBSS, beacons forbidden\n");
			return;
		}
		chandef.width = NL80211_CHAN_WIDTH_20;
		chandef.center_freq1 = chan->center_freq;
		/* check again for downgraded chandef */
		if (!cfg80211_reg_can_beacon(local->hw.wiphy, &chandef,
					     NL80211_IFTYPE_ADHOC)) {
			sdata_info(sdata,
				   "Failed to join IBSS, beacons forbidden\n");
			return;
		}
	}

	err = cfg80211_chandef_dfs_required(sdata->local->hw.wiphy,
					    &chandef, NL80211_IFTYPE_ADHOC);
	if (err < 0) {
		sdata_info(sdata,
			   "Failed to join IBSS, invalid chandef\n");
		return;
	}
	if (err > 0 && !ifibss->userspace_handles_dfs) {
		sdata_info(sdata,
			   "Failed to join IBSS, DFS channel without control program\n");
		return;
	}

	radar_required = err;

	mutex_lock(&local->mtx);
	if (ieee80211_vif_use_channel(sdata, &chandef,
				      ifibss->fixed_channel ?
					IEEE80211_CHANCTX_SHARED :
					IEEE80211_CHANCTX_EXCLUSIVE)) {
		sdata_info(sdata, "Failed to join IBSS, no channel context\n");
		mutex_unlock(&local->mtx);
		return;
	}
	sdata->radar_required = radar_required;
	mutex_unlock(&local->mtx);

	memcpy(ifibss->bssid, bssid, ETH_ALEN);

	presp = ieee80211_ibss_build_presp(sdata, beacon_int, basic_rates,
					   capability, tsf, &chandef,
					   &have_higher_than_11mbit, NULL);
	if (!presp)
		return;

	rcu_assign_pointer(ifibss->presp, presp);
	mgmt = (void *)presp->head;

	sdata->vif.bss_conf.enable_beacon = true;
	sdata->vif.bss_conf.beacon_int = beacon_int;
	sdata->vif.bss_conf.basic_rates = basic_rates;
	sdata->vif.bss_conf.ssid_len = ifibss->ssid_len;
	memcpy(sdata->vif.bss_conf.ssid, ifibss->ssid, ifibss->ssid_len);
	bss_change = BSS_CHANGED_BEACON_INT;
	bss_change |= ieee80211_reset_erp_info(sdata);
	bss_change |= BSS_CHANGED_BSSID;
	bss_change |= BSS_CHANGED_BEACON;
	bss_change |= BSS_CHANGED_BEACON_ENABLED;
	bss_change |= BSS_CHANGED_BASIC_RATES;
	bss_change |= BSS_CHANGED_HT;
	bss_change |= BSS_CHANGED_IBSS;
	bss_change |= BSS_CHANGED_SSID;

	/*
	 * In 5 GHz/802.11a, we can always use short slot time.
	 * (IEEE 802.11-2012 18.3.8.7)
	 *
	 * In 2.4GHz, we must always use long slots in IBSS for compatibility
	 * reasons.
	 * (IEEE 802.11-2012 19.4.5)
	 *
	 * HT follows these specifications (IEEE 802.11-2012 20.3.18)
	 */
	sdata->vif.bss_conf.use_short_slot = chan->band == NL80211_BAND_5GHZ;
	bss_change |= BSS_CHANGED_ERP_SLOT;

	/* cf. IEEE 802.11 9.2.12 */
	if (chan->band == NL80211_BAND_2GHZ && have_higher_than_11mbit)
		sdata->flags |= IEEE80211_SDATA_OPERATING_GMODE;
	else
		sdata->flags &= ~IEEE80211_SDATA_OPERATING_GMODE;

	ieee80211_set_wmm_default(sdata, true, false);

	sdata->vif.bss_conf.ibss_joined = true;
	sdata->vif.bss_conf.ibss_creator = creator;

	err = drv_join_ibss(local, sdata);
	if (err) {
		sdata->vif.bss_conf.ibss_joined = false;
		sdata->vif.bss_conf.ibss_creator = false;
		sdata->vif.bss_conf.enable_beacon = false;
		sdata->vif.bss_conf.ssid_len = 0;
		RCU_INIT_POINTER(ifibss->presp, NULL);
		kfree_rcu(presp, rcu_head);
		mutex_lock(&local->mtx);
		ieee80211_vif_release_channel(sdata);
		mutex_unlock(&local->mtx);
		sdata_info(sdata, "Failed to join IBSS, driver failure: %d\n",
			   err);
		return;
	}

	ieee80211_bss_info_change_notify(sdata, bss_change);

	ifibss->state = IEEE80211_IBSS_MLME_JOINED;
	mod_timer(&ifibss->timer,
		  round_jiffies(jiffies + IEEE80211_IBSS_MERGE_INTERVAL));

	bss_meta.chan = chan;
	bss_meta.scan_width = cfg80211_chandef_to_scan_width(&chandef);
	bss = cfg80211_inform_bss_frame_data(local->hw.wiphy, &bss_meta, mgmt,
					     presp->head_len, GFP_KERNEL);

	cfg80211_put_bss(local->hw.wiphy, bss);
	netif_carrier_on(sdata->dev);
	cfg80211_ibss_joined(sdata->dev, ifibss->bssid, chan, GFP_KERNEL);
}

static void ieee80211_sta_join_ibss(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_bss *bss)
{
	struct cfg80211_bss *cbss =
		container_of((void *)bss, struct cfg80211_bss, priv);
	struct ieee80211_supported_band *sband;
	struct cfg80211_chan_def chandef;
	u32 basic_rates;
	int i, j;
	u16 beacon_int = cbss->beacon_interval;
	const struct cfg80211_bss_ies *ies;
	enum nl80211_channel_type chan_type;
	u64 tsf;
	u32 rate_flags;
	int shift;

	sdata_assert_lock(sdata);

	if (beacon_int < 10)
		beacon_int = 10;

	switch (sdata->u.ibss.chandef.width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_40:
		chan_type = cfg80211_get_chandef_type(&sdata->u.ibss.chandef);
		cfg80211_chandef_create(&chandef, cbss->channel, chan_type);
		break;
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
		cfg80211_chandef_create(&chandef, cbss->channel,
					NL80211_CHAN_NO_HT);
		chandef.width = sdata->u.ibss.chandef.width;
		break;
	case NL80211_CHAN_WIDTH_80:
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
		chandef = sdata->u.ibss.chandef;
		chandef.chan = cbss->channel;
		break;
	default:
		/* fall back to 20 MHz for unsupported modes */
		cfg80211_chandef_create(&chandef, cbss->channel,
					NL80211_CHAN_NO_HT);
		break;
	}

	sband = sdata->local->hw.wiphy->bands[cbss->channel->band];
	rate_flags = ieee80211_chandef_rate_flags(&sdata->u.ibss.chandef);
	shift = ieee80211_vif_get_shift(&sdata->vif);

	basic_rates = 0;

	for (i = 0; i < bss->supp_rates_len; i++) {
		int rate = bss->supp_rates[i] & 0x7f;
		bool is_basic = !!(bss->supp_rates[i] & 0x80);

		for (j = 0; j < sband->n_bitrates; j++) {
			int brate;
			if ((rate_flags & sband->bitrates[j].flags)
			    != rate_flags)
				continue;

			brate = DIV_ROUND_UP(sband->bitrates[j].bitrate,
					     5 * (1 << shift));
			if (brate == rate) {
				if (is_basic)
					basic_rates |= BIT(j);
				break;
			}
		}
	}

	rcu_read_lock();
	ies = rcu_dereference(cbss->ies);
	tsf = ies->tsf;
	rcu_read_unlock();

	__ieee80211_sta_join_ibss(sdata, cbss->bssid,
				  beacon_int,
				  &chandef,
				  basic_rates,
				  cbss->capability,
				  tsf, false);
}

int ieee80211_ibss_csa_beacon(struct ieee80211_sub_if_data *sdata,
			      struct cfg80211_csa_settings *csa_settings)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	struct beacon_data *presp, *old_presp;
	struct cfg80211_bss *cbss;
	const struct cfg80211_bss_ies *ies;
	u16 capability = WLAN_CAPABILITY_IBSS;
	u64 tsf;
	int ret = 0;

	sdata_assert_lock(sdata);

	if (ifibss->privacy)
		capability |= WLAN_CAPABILITY_PRIVACY;

	cbss = cfg80211_get_bss(sdata->local->hw.wiphy, ifibss->chandef.chan,
				ifibss->bssid, ifibss->ssid,
				ifibss->ssid_len, IEEE80211_BSS_TYPE_IBSS,
				IEEE80211_PRIVACY(ifibss->privacy));

	if (WARN_ON(!cbss)) {
		ret = -EINVAL;
		goto out;
	}

	rcu_read_lock();
	ies = rcu_dereference(cbss->ies);
	tsf = ies->tsf;
	rcu_read_unlock();
	cfg80211_put_bss(sdata->local->hw.wiphy, cbss);

	old_presp = rcu_dereference_protected(ifibss->presp,
					  lockdep_is_held(&sdata->wdev.mtx));

	presp = ieee80211_ibss_build_presp(sdata,
					   sdata->vif.bss_conf.beacon_int,
					   sdata->vif.bss_conf.basic_rates,
					   capability, tsf, &ifibss->chandef,
					   NULL, csa_settings);
	if (!presp) {
		ret = -ENOMEM;
		goto out;
	}

	rcu_assign_pointer(ifibss->presp, presp);
	if (old_presp)
		kfree_rcu(old_presp, rcu_head);

	return BSS_CHANGED_BEACON;
 out:
	return ret;
}

int ieee80211_ibss_finish_csa(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	struct cfg80211_bss *cbss;
	int err, changed = 0;

	sdata_assert_lock(sdata);

	/* update cfg80211 bss information with the new channel */
	if (!is_zero_ether_addr(ifibss->bssid)) {
		cbss = cfg80211_get_bss(sdata->local->hw.wiphy,
					ifibss->chandef.chan,
					ifibss->bssid, ifibss->ssid,
					ifibss->ssid_len,
					IEEE80211_BSS_TYPE_IBSS,
					IEEE80211_PRIVACY(ifibss->privacy));
		/* XXX: should not really modify cfg80211 data */
		if (cbss) {
			cbss->channel = sdata->csa_chandef.chan;
			cfg80211_put_bss(sdata->local->hw.wiphy, cbss);
		}
	}

	ifibss->chandef = sdata->csa_chandef;

	/* generate the beacon */
	err = ieee80211_ibss_csa_beacon(sdata, NULL);
	if (err < 0)
		return err;

	changed |= err;

	return changed;
}

void ieee80211_ibss_stop(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;

	cancel_work_sync(&ifibss->csa_connection_drop_work);
}

static struct sta_info *ieee80211_ibss_finish_sta(struct sta_info *sta)
	__acquires(RCU)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u8 addr[ETH_ALEN];

	memcpy(addr, sta->sta.addr, ETH_ALEN);

	ibss_dbg(sdata, "Adding new IBSS station %pM\n", addr);

	sta_info_pre_move_state(sta, IEEE80211_STA_AUTH);
	sta_info_pre_move_state(sta, IEEE80211_STA_ASSOC);
	/* authorize the station only if the network is not RSN protected. If
	 * not wait for the userspace to authorize it */
	if (!sta->sdata->u.ibss.control_port)
		sta_info_pre_move_state(sta, IEEE80211_STA_AUTHORIZED);

	rate_control_rate_init(sta);

	/* If it fails, maybe we raced another insertion? */
	if (sta_info_insert_rcu(sta))
		return sta_info_get(sdata, addr);
	return sta;
}

static struct sta_info *
ieee80211_ibss_add_sta(struct ieee80211_sub_if_data *sdata, const u8 *bssid,
		       const u8 *addr, u32 supp_rates)
	__acquires(RCU)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_supported_band *sband;
	enum nl80211_bss_scan_width scan_width;
	int band;

	/*
	 * XXX: Consider removing the least recently used entry and
	 * 	allow new one to be added.
	 */
	if (local->num_sta >= IEEE80211_IBSS_MAX_STA_ENTRIES) {
		net_info_ratelimited("%s: No room for a new IBSS STA entry %pM\n",
				    sdata->name, addr);
		rcu_read_lock();
		return NULL;
	}

	if (ifibss->state == IEEE80211_IBSS_MLME_SEARCH) {
		rcu_read_lock();
		return NULL;
	}

	if (!ether_addr_equal(bssid, sdata->u.ibss.bssid)) {
		rcu_read_lock();
		return NULL;
	}

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (WARN_ON_ONCE(!chanctx_conf))
		return NULL;
	band = chanctx_conf->def.chan->band;
	scan_width = cfg80211_chandef_to_scan_width(&chanctx_conf->def);
	rcu_read_unlock();

	sta = sta_info_alloc(sdata, addr, GFP_KERNEL);
	if (!sta) {
		rcu_read_lock();
		return NULL;
	}

	/* make sure mandatory rates are always added */
	sband = local->hw.wiphy->bands[band];
	sta->sta.supp_rates[band] = supp_rates |
			ieee80211_mandatory_rates(sband, scan_width);

	return ieee80211_ibss_finish_sta(sta);
}

static int ieee80211_sta_active_ibss(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	int active = 0;
	struct sta_info *sta;

	sdata_assert_lock(sdata);

	rcu_read_lock();

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		unsigned long last_active = ieee80211_sta_last_active(sta);

		if (sta->sdata == sdata &&
		    time_is_after_jiffies(last_active +
					  IEEE80211_IBSS_MERGE_INTERVAL)) {
			active++;
			break;
		}
	}

	rcu_read_unlock();

	return active;
}

static void ieee80211_ibss_disconnect(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	struct ieee80211_local *local = sdata->local;
	struct cfg80211_bss *cbss;
	struct beacon_data *presp;
	struct sta_info *sta;

	if (!is_zero_ether_addr(ifibss->bssid)) {
		cbss = cfg80211_get_bss(local->hw.wiphy, ifibss->chandef.chan,
					ifibss->bssid, ifibss->ssid,
					ifibss->ssid_len,
					IEEE80211_BSS_TYPE_IBSS,
					IEEE80211_PRIVACY(ifibss->privacy));

		if (cbss) {
			cfg80211_unlink_bss(local->hw.wiphy, cbss);
			cfg80211_put_bss(sdata->local->hw.wiphy, cbss);
		}
	}

	ifibss->state = IEEE80211_IBSS_MLME_SEARCH;

	sta_info_flush(sdata);

	spin_lock_bh(&ifibss->incomplete_lock);
	while (!list_empty(&ifibss->incomplete_stations)) {
		sta = list_first_entry(&ifibss->incomplete_stations,
				       struct sta_info, list);
		list_del(&sta->list);
		spin_unlock_bh(&ifibss->incomplete_lock);

		sta_info_free(local, sta);
		spin_lock_bh(&ifibss->incomplete_lock);
	}
	spin_unlock_bh(&ifibss->incomplete_lock);

	netif_carrier_off(sdata->dev);

	sdata->vif.bss_conf.ibss_joined = false;
	sdata->vif.bss_conf.ibss_creator = false;
	sdata->vif.bss_conf.enable_beacon = false;
	sdata->vif.bss_conf.ssid_len = 0;

	/* remove beacon */
	presp = rcu_dereference_protected(ifibss->presp,
					  lockdep_is_held(&sdata->wdev.mtx));
	RCU_INIT_POINTER(sdata->u.ibss.presp, NULL);
	if (presp)
		kfree_rcu(presp, rcu_head);

	clear_bit(SDATA_STATE_OFFCHANNEL_BEACON_STOPPED, &sdata->state);
	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BEACON_ENABLED |
						BSS_CHANGED_IBSS);
	drv_leave_ibss(local, sdata);
	mutex_lock(&local->mtx);
	ieee80211_vif_release_channel(sdata);
	mutex_unlock(&local->mtx);
}

static void ieee80211_csa_connection_drop_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     u.ibss.csa_connection_drop_work);

	sdata_lock(sdata);

	ieee80211_ibss_disconnect(sdata);
	synchronize_rcu();
	skb_queue_purge(&sdata->skb_queue);

	/* trigger a scan to find another IBSS network to join */
	ieee80211_queue_work(&sdata->local->hw, &sdata->work);

	sdata_unlock(sdata);
}

static void ieee80211_ibss_csa_mark_radar(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	int err;

	/* if the current channel is a DFS channel, mark the channel as
	 * unavailable.
	 */
	err = cfg80211_chandef_dfs_required(sdata->local->hw.wiphy,
					    &ifibss->chandef,
					    NL80211_IFTYPE_ADHOC);
	if (err > 0)
		cfg80211_radar_event(sdata->local->hw.wiphy, &ifibss->chandef,
				     GFP_ATOMIC);
}

static bool
ieee80211_ibss_process_chanswitch(struct ieee80211_sub_if_data *sdata,
				  struct ieee802_11_elems *elems,
				  bool beacon)
{
	struct cfg80211_csa_settings params;
	struct ieee80211_csa_ie csa_ie;
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	enum nl80211_channel_type ch_type;
	int err;
	u32 sta_flags;

	sdata_assert_lock(sdata);

	sta_flags = IEEE80211_STA_DISABLE_VHT;
	switch (ifibss->chandef.width) {
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
	case NL80211_CHAN_WIDTH_20_NOHT:
		sta_flags |= IEEE80211_STA_DISABLE_HT;
		/* fall through */
	case NL80211_CHAN_WIDTH_20:
		sta_flags |= IEEE80211_STA_DISABLE_40MHZ;
		break;
	default:
		break;
	}

	memset(&params, 0, sizeof(params));
	err = ieee80211_parse_ch_switch_ie(sdata, elems,
					   ifibss->chandef.chan->band,
					   sta_flags, ifibss->bssid, &csa_ie);
	/* can't switch to destination channel, fail */
	if (err < 0)
		goto disconnect;

	/* did not contain a CSA */
	if (err)
		return false;

	/* channel switch is not supported, disconnect */
	if (!(sdata->local->hw.wiphy->flags & WIPHY_FLAG_HAS_CHANNEL_SWITCH))
		goto disconnect;

	params.count = csa_ie.count;
	params.chandef = csa_ie.chandef;

	switch (ifibss->chandef.width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_40:
		/* keep our current HT mode (HT20/HT40+/HT40-), even if
		 * another mode  has been announced. The mode is not adopted
		 * within the beacon while doing CSA and we should therefore
		 * keep the mode which we announce.
		 */
		ch_type = cfg80211_get_chandef_type(&ifibss->chandef);
		cfg80211_chandef_create(&params.chandef, params.chandef.chan,
					ch_type);
		break;
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
		if (params.chandef.width != ifibss->chandef.width) {
			sdata_info(sdata,
				   "IBSS %pM received channel switch from incompatible channel width (%d MHz, width:%d, CF1/2: %d/%d MHz), disconnecting\n",
				   ifibss->bssid,
				   params.chandef.chan->center_freq,
				   params.chandef.width,
				   params.chandef.center_freq1,
				   params.chandef.center_freq2);
			goto disconnect;
		}
		break;
	default:
		/* should not happen, sta_flags should prevent VHT modes. */
		WARN_ON(1);
		goto disconnect;
	}

	if (!cfg80211_reg_can_beacon(sdata->local->hw.wiphy, &params.chandef,
				     NL80211_IFTYPE_ADHOC)) {
		sdata_info(sdata,
			   "IBSS %pM switches to unsupported channel (%d MHz, width:%d, CF1/2: %d/%d MHz), disconnecting\n",
			   ifibss->bssid,
			   params.chandef.chan->center_freq,
			   params.chandef.width,
			   params.chandef.center_freq1,
			   params.chandef.center_freq2);
		goto disconnect;
	}

	err = cfg80211_chandef_dfs_required(sdata->local->hw.wiphy,
					    &params.chandef,
					    NL80211_IFTYPE_ADHOC);
	if (err < 0)
		goto disconnect;
	if (err > 0 && !ifibss->userspace_handles_dfs) {
		/* IBSS-DFS only allowed with a control program */
		goto disconnect;
	}

	params.radar_required = err;

	if (cfg80211_chandef_identical(&params.chandef,
				       &sdata->vif.bss_conf.chandef)) {
		ibss_dbg(sdata,
			 "received csa with an identical chandef, ignoring\n");
		return true;
	}

	/* all checks done, now perform the channel switch. */
	ibss_dbg(sdata,
		 "received channel switch announcement to go to channel %d MHz\n",
		 params.chandef.chan->center_freq);

	params.block_tx = !!csa_ie.mode;

	if (ieee80211_channel_switch(sdata->local->hw.wiphy, sdata->dev,
				     &params))
		goto disconnect;

	ieee80211_ibss_csa_mark_radar(sdata);

	return true;
disconnect:
	ibss_dbg(sdata, "Can't handle channel switch, disconnect\n");
	ieee80211_queue_work(&sdata->local->hw,
			     &ifibss->csa_connection_drop_work);

	ieee80211_ibss_csa_mark_radar(sdata);

	return true;
}

static void
ieee80211_rx_mgmt_spectrum_mgmt(struct ieee80211_sub_if_data *sdata,
				struct ieee80211_mgmt *mgmt, size_t len,
				struct ieee80211_rx_status *rx_status,
				struct ieee802_11_elems *elems)
{
	int required_len;

	if (len < IEEE80211_MIN_ACTION_SIZE + 1)
		return;

	/* CSA is the only action we handle for now */
	if (mgmt->u.action.u.measurement.action_code !=
	    WLAN_ACTION_SPCT_CHL_SWITCH)
		return;

	required_len = IEEE80211_MIN_ACTION_SIZE +
		       sizeof(mgmt->u.action.u.chan_switch);
	if (len < required_len)
		return;

	if (!sdata->vif.csa_active)
		ieee80211_ibss_process_chanswitch(sdata, elems, false);
}

static void ieee80211_rx_mgmt_deauth_ibss(struct ieee80211_sub_if_data *sdata,
					  struct ieee80211_mgmt *mgmt,
					  size_t len)
{
	u16 reason = le16_to_cpu(mgmt->u.deauth.reason_code);

	if (len < IEEE80211_DEAUTH_FRAME_LEN)
		return;

	ibss_dbg(sdata, "RX DeAuth SA=%pM DA=%pM\n", mgmt->sa, mgmt->da);
	ibss_dbg(sdata, "\tBSSID=%pM (reason: %d)\n", mgmt->bssid, reason);
	sta_info_destroy_addr(sdata, mgmt->sa);
}

static void ieee80211_rx_mgmt_auth_ibss(struct ieee80211_sub_if_data *sdata,
					struct ieee80211_mgmt *mgmt,
					size_t len)
{
	u16 auth_alg, auth_transaction;

	sdata_assert_lock(sdata);

	if (len < 24 + 6)
		return;

	auth_alg = le16_to_cpu(mgmt->u.auth.auth_alg);
	auth_transaction = le16_to_cpu(mgmt->u.auth.auth_transaction);

	ibss_dbg(sdata, "RX Auth SA=%pM DA=%pM\n", mgmt->sa, mgmt->da);
	ibss_dbg(sdata, "\tBSSID=%pM (auth_transaction=%d)\n",
		 mgmt->bssid, auth_transaction);

	if (auth_alg != WLAN_AUTH_OPEN || auth_transaction != 1)
		return;

	/*
	 * IEEE 802.11 standard does not require authentication in IBSS
	 * networks and most implementations do not seem to use it.
	 * However, try to reply to authentication attempts if someone
	 * has actually implemented this.
	 */
	ieee80211_send_auth(sdata, 2, WLAN_AUTH_OPEN, 0, NULL, 0,
			    mgmt->sa, sdata->u.ibss.bssid, NULL, 0, 0, 0);
}

static void ieee80211_update_sta_info(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_mgmt *mgmt, size_t len,
				      struct ieee80211_rx_status *rx_status,
				      struct ieee802_11_elems *elems,
				      struct ieee80211_channel *channel)
{
	struct sta_info *sta;
	enum nl80211_band band = rx_status->band;
	enum nl80211_bss_scan_width scan_width;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	bool rates_updated = false;
	u32 supp_rates = 0;

	if (sdata->vif.type != NL80211_IFTYPE_ADHOC)
		return;

	if (!ether_addr_equal(mgmt->bssid, sdata->u.ibss.bssid))
		return;

	sband = local->hw.wiphy->bands[band];
	if (WARN_ON(!sband))
		return;

	rcu_read_lock();
	sta = sta_info_get(sdata, mgmt->sa);

	if (elems->supp_rates) {
		supp_rates = ieee80211_sta_get_rates(sdata, elems,
						     band, NULL);
		if (sta) {
			u32 prev_rates;

			prev_rates = sta->sta.supp_rates[band];
			/* make sure mandatory rates are always added */
			scan_width = NL80211_BSS_CHAN_WIDTH_20;
			if (rx_status->bw == RATE_INFO_BW_5)
				scan_width = NL80211_BSS_CHAN_WIDTH_5;
			else if (rx_status->bw == RATE_INFO_BW_10)
				scan_width = NL80211_BSS_CHAN_WIDTH_10;

			sta->sta.supp_rates[band] = supp_rates |
				ieee80211_mandatory_rates(sband, scan_width);
			if (sta->sta.supp_rates[band] != prev_rates) {
				ibss_dbg(sdata,
					 "updated supp_rates set for %pM based on beacon/probe_resp (0x%x -> 0x%x)\n",
					 sta->sta.addr, prev_rates,
					 sta->sta.supp_rates[band]);
				rates_updated = true;
			}
		} else {
			rcu_read_unlock();
			sta = ieee80211_ibss_add_sta(sdata, mgmt->bssid,
						     mgmt->sa, supp_rates);
		}
	}

	if (sta && !sta->sta.wme &&
	    elems->wmm_info && local->hw.queues >= IEEE80211_NUM_ACS) {
		sta->sta.wme = true;
		ieee80211_check_fast_xmit(sta);
	}

	if (sta && elems->ht_operation && elems->ht_cap_elem &&
	    sdata->u.ibss.chandef.width != NL80211_CHAN_WIDTH_20_NOHT &&
	    sdata->u.ibss.chandef.width != NL80211_CHAN_WIDTH_5 &&
	    sdata->u.ibss.chandef.width != NL80211_CHAN_WIDTH_10) {
		/* we both use HT */
		struct ieee80211_ht_cap htcap_ie;
		struct cfg80211_chan_def chandef;
		enum ieee80211_sta_rx_bandwidth bw = sta->sta.bandwidth;

		cfg80211_chandef_create(&chandef, channel, NL80211_CHAN_NO_HT);
		ieee80211_chandef_ht_oper(elems->ht_operation, &chandef);

		memcpy(&htcap_ie, elems->ht_cap_elem, sizeof(htcap_ie));
		rates_updated |= ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband,
								   &htcap_ie,
								   sta);

		if (elems->vht_operation && elems->vht_cap_elem &&
		    sdata->u.ibss.chandef.width != NL80211_CHAN_WIDTH_20 &&
		    sdata->u.ibss.chandef.width != NL80211_CHAN_WIDTH_40) {
			/* we both use VHT */
			struct ieee80211_vht_cap cap_ie;
			struct ieee80211_sta_vht_cap cap = sta->sta.vht_cap;

			ieee80211_chandef_vht_oper(&local->hw,
						   elems->vht_operation,
						   elems->ht_operation,
						   &chandef);
			memcpy(&cap_ie, elems->vht_cap_elem, sizeof(cap_ie));
			ieee80211_vht_cap_ie_to_sta_vht_cap(sdata, sband,
							    &cap_ie, sta);
			if (memcmp(&cap, &sta->sta.vht_cap, sizeof(cap)))
				rates_updated |= true;
		}

		if (bw != sta->sta.bandwidth)
			rates_updated |= true;

		if (!cfg80211_chandef_compatible(&sdata->u.ibss.chandef,
						 &chandef))
			WARN_ON_ONCE(1);
	}

	if (sta && rates_updated) {
		u32 changed = IEEE80211_RC_SUPP_RATES_CHANGED;
		u8 rx_nss = sta->sta.rx_nss;

		/* Force rx_nss recalculation */
		sta->sta.rx_nss = 0;
		rate_control_rate_init(sta);
		if (sta->sta.rx_nss != rx_nss)
			changed |= IEEE80211_RC_NSS_CHANGED;

		drv_sta_rc_update(local, sdata, &sta->sta, changed);
	}

	rcu_read_unlock();
}

static void ieee80211_rx_bss_info(struct ieee80211_sub_if_data *sdata,
				  struct ieee80211_mgmt *mgmt, size_t len,
				  struct ieee80211_rx_status *rx_status,
				  struct ieee802_11_elems *elems)
{
	struct ieee80211_local *local = sdata->local;
	struct cfg80211_bss *cbss;
	struct ieee80211_bss *bss;
	struct ieee80211_channel *channel;
	u64 beacon_timestamp, rx_timestamp;
	u32 supp_rates = 0;
	enum nl80211_band band = rx_status->band;

	channel = ieee80211_get_channel(local->hw.wiphy, rx_status->freq);
	if (!channel)
		return;

	ieee80211_update_sta_info(sdata, mgmt, len, rx_status, elems, channel);

	bss = ieee80211_bss_info_update(local, rx_status, mgmt, len, channel);
	if (!bss)
		return;

	cbss = container_of((void *)bss, struct cfg80211_bss, priv);

	/* same for beacon and probe response */
	beacon_timestamp = le64_to_cpu(mgmt->u.beacon.timestamp);

	/* check if we need to merge IBSS */

	/* not an IBSS */
	if (!(cbss->capability & WLAN_CAPABILITY_IBSS))
		goto put_bss;

	/* different channel */
	if (sdata->u.ibss.fixed_channel &&
	    sdata->u.ibss.chandef.chan != cbss->channel)
		goto put_bss;

	/* different SSID */
	if (elems->ssid_len != sdata->u.ibss.ssid_len ||
	    memcmp(elems->ssid, sdata->u.ibss.ssid,
				sdata->u.ibss.ssid_len))
		goto put_bss;

	/* process channel switch */
	if (sdata->vif.csa_active ||
	    ieee80211_ibss_process_chanswitch(sdata, elems, true))
		goto put_bss;

	/* same BSSID */
	if (ether_addr_equal(cbss->bssid, sdata->u.ibss.bssid))
		goto put_bss;

	/* we use a fixed BSSID */
	if (sdata->u.ibss.fixed_bssid)
		goto put_bss;

	if (ieee80211_have_rx_timestamp(rx_status)) {
		/* time when timestamp field was received */
		rx_timestamp =
			ieee80211_calculate_rx_timestamp(local, rx_status,
							 len + FCS_LEN, 24);
	} else {
		/*
		 * second best option: get current TSF
		 * (will return -1 if not supported)
		 */
		rx_timestamp = drv_get_tsf(local, sdata);
	}

	ibss_dbg(sdata, "RX beacon SA=%pM BSSID=%pM TSF=0x%llx\n",
		 mgmt->sa, mgmt->bssid,
		 (unsigned long long)rx_timestamp);
	ibss_dbg(sdata, "\tBCN=0x%llx diff=%lld @%lu\n",
		 (unsigned long long)beacon_timestamp,
		 (unsigned long long)(rx_timestamp - beacon_timestamp),
		 jiffies);

	if (beacon_timestamp > rx_timestamp) {
		ibss_dbg(sdata,
			 "beacon TSF higher than local TSF - IBSS merge with BSSID %pM\n",
			 mgmt->bssid);
		ieee80211_sta_join_ibss(sdata, bss);
		supp_rates = ieee80211_sta_get_rates(sdata, elems, band, NULL);
		ieee80211_ibss_add_sta(sdata, mgmt->bssid, mgmt->sa,
				       supp_rates);
		rcu_read_unlock();
	}

 put_bss:
	ieee80211_rx_bss_put(local, bss);
}

void ieee80211_ibss_rx_no_sta(struct ieee80211_sub_if_data *sdata,
			      const u8 *bssid, const u8 *addr,
			      u32 supp_rates)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_supported_band *sband;
	enum nl80211_bss_scan_width scan_width;
	int band;

	/*
	 * XXX: Consider removing the least recently used entry and
	 * 	allow new one to be added.
	 */
	if (local->num_sta >= IEEE80211_IBSS_MAX_STA_ENTRIES) {
		net_info_ratelimited("%s: No room for a new IBSS STA entry %pM\n",
				    sdata->name, addr);
		return;
	}

	if (ifibss->state == IEEE80211_IBSS_MLME_SEARCH)
		return;

	if (!ether_addr_equal(bssid, sdata->u.ibss.bssid))
		return;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (WARN_ON_ONCE(!chanctx_conf)) {
		rcu_read_unlock();
		return;
	}
	band = chanctx_conf->def.chan->band;
	scan_width = cfg80211_chandef_to_scan_width(&chanctx_conf->def);
	rcu_read_unlock();

	sta = sta_info_alloc(sdata, addr, GFP_ATOMIC);
	if (!sta)
		return;

	/* make sure mandatory rates are always added */
	sband = local->hw.wiphy->bands[band];
	sta->sta.supp_rates[band] = supp_rates |
			ieee80211_mandatory_rates(sband, scan_width);

	spin_lock(&ifibss->incomplete_lock);
	list_add(&sta->list, &ifibss->incomplete_stations);
	spin_unlock(&ifibss->incomplete_lock);
	ieee80211_queue_work(&local->hw, &sdata->work);
}

static void ieee80211_ibss_sta_expire(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta, *tmp;
	unsigned long exp_time = IEEE80211_IBSS_INACTIVITY_LIMIT;
	unsigned long exp_rsn = IEEE80211_IBSS_RSN_INACTIVITY_LIMIT;

	mutex_lock(&local->sta_mtx);

	list_for_each_entry_safe(sta, tmp, &local->sta_list, list) {
		unsigned long last_active = ieee80211_sta_last_active(sta);

		if (sdata != sta->sdata)
			continue;

		if (time_is_before_jiffies(last_active + exp_time) ||
		    (time_is_before_jiffies(last_active + exp_rsn) &&
		     sta->sta_state != IEEE80211_STA_AUTHORIZED)) {
			sta_dbg(sta->sdata, "expiring inactive %sSTA %pM\n",
				sta->sta_state != IEEE80211_STA_AUTHORIZED ?
				"not authorized " : "", sta->sta.addr);

			WARN_ON(__sta_info_destroy(sta));
		}
	}

	mutex_unlock(&local->sta_mtx);
}

/*
 * This function is called with state == IEEE80211_IBSS_MLME_JOINED
 */

static void ieee80211_sta_merge_ibss(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	enum nl80211_bss_scan_width scan_width;

	sdata_assert_lock(sdata);

	mod_timer(&ifibss->timer,
		  round_jiffies(jiffies + IEEE80211_IBSS_MERGE_INTERVAL));

	ieee80211_ibss_sta_expire(sdata);

	if (time_before(jiffies, ifibss->last_scan_completed +
		       IEEE80211_IBSS_MERGE_INTERVAL))
		return;

	if (ieee80211_sta_active_ibss(sdata))
		return;

	if (ifibss->fixed_channel)
		return;

	sdata_info(sdata,
		   "No active IBSS STAs - trying to scan for other IBSS networks with same SSID (merge)\n");

	scan_width = cfg80211_chandef_to_scan_width(&ifibss->chandef);
	ieee80211_request_ibss_scan(sdata, ifibss->ssid, ifibss->ssid_len,
				    NULL, 0, scan_width);
}

static void ieee80211_sta_create_ibss(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	u8 bssid[ETH_ALEN];
	u16 capability;
	int i;

	sdata_assert_lock(sdata);

	if (ifibss->fixed_bssid) {
		memcpy(bssid, ifibss->bssid, ETH_ALEN);
	} else {
		/* Generate random, not broadcast, locally administered BSSID. Mix in
		 * own MAC address to make sure that devices that do not have proper
		 * random number generator get different BSSID. */
		get_random_bytes(bssid, ETH_ALEN);
		for (i = 0; i < ETH_ALEN; i++)
			bssid[i] ^= sdata->vif.addr[i];
		bssid[0] &= ~0x01;
		bssid[0] |= 0x02;
	}

	sdata_info(sdata, "Creating new IBSS network, BSSID %pM\n", bssid);

	capability = WLAN_CAPABILITY_IBSS;

	if (ifibss->privacy)
		capability |= WLAN_CAPABILITY_PRIVACY;

	__ieee80211_sta_join_ibss(sdata, bssid, sdata->vif.bss_conf.beacon_int,
				  &ifibss->chandef, ifibss->basic_rates,
				  capability, 0, true);
}

static unsigned ibss_setup_channels(struct wiphy *wiphy,
				    struct ieee80211_channel **channels,
				    unsigned int channels_max,
				    u32 center_freq, u32 width)
{
	struct ieee80211_channel *chan = NULL;
	unsigned int n_chan = 0;
	u32 start_freq, end_freq, freq;

	if (width <= 20) {
		start_freq = center_freq;
		end_freq = center_freq;
	} else {
		start_freq = center_freq - width / 2 + 10;
		end_freq = center_freq + width / 2 - 10;
	}

	for (freq = start_freq; freq <= end_freq; freq += 20) {
		chan = ieee80211_get_channel(wiphy, freq);
		if (!chan)
			continue;
		if (n_chan >= channels_max)
			return n_chan;

		channels[n_chan] = chan;
		n_chan++;
	}

	return n_chan;
}

static unsigned int
ieee80211_ibss_setup_scan_channels(struct wiphy *wiphy,
				   const struct cfg80211_chan_def *chandef,
				   struct ieee80211_channel **channels,
				   unsigned int channels_max)
{
	unsigned int n_chan = 0;
	u32 width, cf1, cf2 = 0;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		width = 40;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		cf2 = chandef->center_freq2;
		/* fall through */
	case NL80211_CHAN_WIDTH_80:
		width = 80;
		break;
	case NL80211_CHAN_WIDTH_160:
		width = 160;
		break;
	default:
		width = 20;
		break;
	}

	cf1 = chandef->center_freq1;

	n_chan = ibss_setup_channels(wiphy, channels, channels_max, cf1, width);

	if (cf2)
		n_chan += ibss_setup_channels(wiphy, &channels[n_chan],
					      channels_max - n_chan, cf2,
					      width);

	return n_chan;
}

/*
 * This function is called with state == IEEE80211_IBSS_MLME_SEARCH
 */

static void ieee80211_sta_find_ibss(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	struct ieee80211_local *local = sdata->local;
	struct cfg80211_bss *cbss;
	struct ieee80211_channel *chan = NULL;
	const u8 *bssid = NULL;
	enum nl80211_bss_scan_width scan_width;
	int active_ibss;

	sdata_assert_lock(sdata);

	active_ibss = ieee80211_sta_active_ibss(sdata);
	ibss_dbg(sdata, "sta_find_ibss (active_ibss=%d)\n", active_ibss);

	if (active_ibss)
		return;

	if (ifibss->fixed_bssid)
		bssid = ifibss->bssid;
	if (ifibss->fixed_channel)
		chan = ifibss->chandef.chan;
	if (!is_zero_ether_addr(ifibss->bssid))
		bssid = ifibss->bssid;
	cbss = cfg80211_get_bss(local->hw.wiphy, chan, bssid,
				ifibss->ssid, ifibss->ssid_len,
				IEEE80211_BSS_TYPE_IBSS,
				IEEE80211_PRIVACY(ifibss->privacy));

	if (cbss) {
		struct ieee80211_bss *bss;

		bss = (void *)cbss->priv;
		ibss_dbg(sdata,
			 "sta_find_ibss: selected %pM current %pM\n",
			 cbss->bssid, ifibss->bssid);
		sdata_info(sdata,
			   "Selected IBSS BSSID %pM based on configured SSID\n",
			   cbss->bssid);

		ieee80211_sta_join_ibss(sdata, bss);
		ieee80211_rx_bss_put(local, bss);
		return;
	}

	/* if a fixed bssid and a fixed freq have been provided create the IBSS
	 * directly and do not waste time scanning
	 */
	if (ifibss->fixed_bssid && ifibss->fixed_channel) {
		sdata_info(sdata, "Created IBSS using preconfigured BSSID %pM\n",
			   bssid);
		ieee80211_sta_create_ibss(sdata);
		return;
	}


	ibss_dbg(sdata, "sta_find_ibss: did not try to join ibss\n");

	/* Selected IBSS not found in current scan results - try to scan */
	if (time_after(jiffies, ifibss->last_scan_completed +
					IEEE80211_SCAN_INTERVAL)) {
		struct ieee80211_channel *channels[8];
		unsigned int num;

		sdata_info(sdata, "Trigger new scan to find an IBSS to join\n");

		scan_width = cfg80211_chandef_to_scan_width(&ifibss->chandef);

		if (ifibss->fixed_channel) {
			num = ieee80211_ibss_setup_scan_channels(local->hw.wiphy,
								 &ifibss->chandef,
								 channels,
								 ARRAY_SIZE(channels));
			ieee80211_request_ibss_scan(sdata, ifibss->ssid,
						    ifibss->ssid_len, channels,
						    num, scan_width);
		} else {
			ieee80211_request_ibss_scan(sdata, ifibss->ssid,
						    ifibss->ssid_len, NULL,
						    0, scan_width);
		}
	} else {
		int interval = IEEE80211_SCAN_INTERVAL;

		if (time_after(jiffies, ifibss->ibss_join_req +
			       IEEE80211_IBSS_JOIN_TIMEOUT))
			ieee80211_sta_create_ibss(sdata);

		mod_timer(&ifibss->timer,
			  round_jiffies(jiffies + interval));
	}
}

static void ieee80211_rx_mgmt_probe_req(struct ieee80211_sub_if_data *sdata,
					struct sk_buff *req)
{
	struct ieee80211_mgmt *mgmt = (void *)req->data;
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	struct ieee80211_local *local = sdata->local;
	int tx_last_beacon, len = req->len;
	struct sk_buff *skb;
	struct beacon_data *presp;
	u8 *pos, *end;

	sdata_assert_lock(sdata);

	presp = rcu_dereference_protected(ifibss->presp,
					  lockdep_is_held(&sdata->wdev.mtx));

	if (ifibss->state != IEEE80211_IBSS_MLME_JOINED ||
	    len < 24 + 2 || !presp)
		return;

	tx_last_beacon = drv_tx_last_beacon(local);

	ibss_dbg(sdata, "RX ProbeReq SA=%pM DA=%pM\n", mgmt->sa, mgmt->da);
	ibss_dbg(sdata, "\tBSSID=%pM (tx_last_beacon=%d)\n",
		 mgmt->bssid, tx_last_beacon);

	if (!tx_last_beacon && is_multicast_ether_addr(mgmt->da))
		return;

	if (!ether_addr_equal(mgmt->bssid, ifibss->bssid) &&
	    !is_broadcast_ether_addr(mgmt->bssid))
		return;

	end = ((u8 *) mgmt) + len;
	pos = mgmt->u.probe_req.variable;
	if (pos[0] != WLAN_EID_SSID ||
	    pos + 2 + pos[1] > end) {
		ibss_dbg(sdata, "Invalid SSID IE in ProbeReq from %pM\n",
			 mgmt->sa);
		return;
	}
	if (pos[1] != 0 &&
	    (pos[1] != ifibss->ssid_len ||
	     memcmp(pos + 2, ifibss->ssid, ifibss->ssid_len))) {
		/* Ignore ProbeReq for foreign SSID */
		return;
	}

	/* Reply with ProbeResp */
	skb = dev_alloc_skb(local->tx_headroom + presp->head_len);
	if (!skb)
		return;

	skb_reserve(skb, local->tx_headroom);
	skb_put_data(skb, presp->head, presp->head_len);

	memcpy(((struct ieee80211_mgmt *) skb->data)->da, mgmt->sa, ETH_ALEN);
	ibss_dbg(sdata, "Sending ProbeResp to %pM\n", mgmt->sa);
	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;

	/* avoid excessive retries for probe request to wildcard SSIDs */
	if (pos[1] == 0)
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_NO_ACK;

	ieee80211_tx_skb(sdata, skb);
}

static
void ieee80211_rx_mgmt_probe_beacon(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_mgmt *mgmt, size_t len,
				    struct ieee80211_rx_status *rx_status)
{
	size_t baselen;
	struct ieee802_11_elems elems;

	BUILD_BUG_ON(offsetof(typeof(mgmt->u.probe_resp), variable) !=
		     offsetof(typeof(mgmt->u.beacon), variable));

	/*
	 * either beacon or probe_resp but the variable field is at the
	 * same offset
	 */
	baselen = (u8 *) mgmt->u.probe_resp.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	ieee802_11_parse_elems(mgmt->u.probe_resp.variable, len - baselen,
			       false, &elems, mgmt->bssid, NULL);

	ieee80211_rx_bss_info(sdata, mgmt, len, rx_status, &elems);
}

void ieee80211_ibss_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				   struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_mgmt *mgmt;
	u16 fc;
	struct ieee802_11_elems elems;
	int ies_len;

	rx_status = IEEE80211_SKB_RXCB(skb);
	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	sdata_lock(sdata);

	if (!sdata->u.ibss.ssid_len)
		goto mgmt_out; /* not ready to merge yet */

	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_PROBE_REQ:
		ieee80211_rx_mgmt_probe_req(sdata, skb);
		break;
	case IEEE80211_STYPE_PROBE_RESP:
	case IEEE80211_STYPE_BEACON:
		ieee80211_rx_mgmt_probe_beacon(sdata, mgmt, skb->len,
					       rx_status);
		break;
	case IEEE80211_STYPE_AUTH:
		ieee80211_rx_mgmt_auth_ibss(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_DEAUTH:
		ieee80211_rx_mgmt_deauth_ibss(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_ACTION:
		switch (mgmt->u.action.category) {
		case WLAN_CATEGORY_SPECTRUM_MGMT:
			ies_len = skb->len -
				  offsetof(struct ieee80211_mgmt,
					   u.action.u.chan_switch.variable);

			if (ies_len < 0)
				break;

			ieee802_11_parse_elems(
				mgmt->u.action.u.chan_switch.variable,
				ies_len, true, &elems, mgmt->bssid, NULL);

			if (elems.parse_error)
				break;

			ieee80211_rx_mgmt_spectrum_mgmt(sdata, mgmt, skb->len,
							rx_status, &elems);
			break;
		}
	}

 mgmt_out:
	sdata_unlock(sdata);
}

void ieee80211_ibss_work(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
	struct sta_info *sta;

	sdata_lock(sdata);

	/*
	 * Work could be scheduled after scan or similar
	 * when we aren't even joined (or trying) with a
	 * network.
	 */
	if (!ifibss->ssid_len)
		goto out;

	spin_lock_bh(&ifibss->incomplete_lock);
	while (!list_empty(&ifibss->incomplete_stations)) {
		sta = list_first_entry(&ifibss->incomplete_stations,
				       struct sta_info, list);
		list_del(&sta->list);
		spin_unlock_bh(&ifibss->incomplete_lock);

		ieee80211_ibss_finish_sta(sta);
		rcu_read_unlock();
		spin_lock_bh(&ifibss->incomplete_lock);
	}
	spin_unlock_bh(&ifibss->incomplete_lock);

	switch (ifibss->state) {
	case IEEE80211_IBSS_MLME_SEARCH:
		ieee80211_sta_find_ibss(sdata);
		break;
	case IEEE80211_IBSS_MLME_JOINED:
		ieee80211_sta_merge_ibss(sdata);
		break;
	default:
		WARN_ON(1);
		break;
	}

 out:
	sdata_unlock(sdata);
}

static void ieee80211_ibss_timer(struct timer_list *t)
{
	struct ieee80211_sub_if_data *sdata =
		from_timer(sdata, t, u.ibss.timer);

	ieee80211_queue_work(&sdata->local->hw, &sdata->work);
}

void ieee80211_ibss_setup_sdata(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;

	timer_setup(&ifibss->timer, ieee80211_ibss_timer, 0);
	INIT_LIST_HEAD(&ifibss->incomplete_stations);
	spin_lock_init(&ifibss->incomplete_lock);
	INIT_WORK(&ifibss->csa_connection_drop_work,
		  ieee80211_csa_connection_drop_work);
}

/* scan finished notification */
void ieee80211_ibss_notify_scan_completed(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;
		if (sdata->vif.type != NL80211_IFTYPE_ADHOC)
			continue;
		sdata->u.ibss.last_scan_completed = jiffies;
	}
	mutex_unlock(&local->iflist_mtx);
}

int ieee80211_ibss_join(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_ibss_params *params)
{
	u32 changed = 0;
	u32 rate_flags;
	struct ieee80211_supported_band *sband;
	enum ieee80211_chanctx_mode chanmode;
	struct ieee80211_local *local = sdata->local;
	int radar_detect_width = 0;
	int i;
	int ret;

	ret = cfg80211_chandef_dfs_required(local->hw.wiphy,
					    &params->chandef,
					    sdata->wdev.iftype);
	if (ret < 0)
		return ret;

	if (ret > 0) {
		if (!params->userspace_handles_dfs)
			return -EINVAL;
		radar_detect_width = BIT(params->chandef.width);
	}

	chanmode = (params->channel_fixed && !ret) ?
		IEEE80211_CHANCTX_SHARED : IEEE80211_CHANCTX_EXCLUSIVE;

	mutex_lock(&local->chanctx_mtx);
	ret = ieee80211_check_combinations(sdata, &params->chandef, chanmode,
					   radar_detect_width);
	mutex_unlock(&local->chanctx_mtx);
	if (ret < 0)
		return ret;

	if (params->bssid) {
		memcpy(sdata->u.ibss.bssid, params->bssid, ETH_ALEN);
		sdata->u.ibss.fixed_bssid = true;
	} else
		sdata->u.ibss.fixed_bssid = false;

	sdata->u.ibss.privacy = params->privacy;
	sdata->u.ibss.control_port = params->control_port;
	sdata->u.ibss.userspace_handles_dfs = params->userspace_handles_dfs;
	sdata->u.ibss.basic_rates = params->basic_rates;
	sdata->u.ibss.last_scan_completed = jiffies;

	/* fix basic_rates if channel does not support these rates */
	rate_flags = ieee80211_chandef_rate_flags(&params->chandef);
	sband = local->hw.wiphy->bands[params->chandef.chan->band];
	for (i = 0; i < sband->n_bitrates; i++) {
		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			sdata->u.ibss.basic_rates &= ~BIT(i);
	}
	memcpy(sdata->vif.bss_conf.mcast_rate, params->mcast_rate,
	       sizeof(params->mcast_rate));

	sdata->vif.bss_conf.beacon_int = params->beacon_interval;

	sdata->u.ibss.chandef = params->chandef;
	sdata->u.ibss.fixed_channel = params->channel_fixed;

	if (params->ie) {
		sdata->u.ibss.ie = kmemdup(params->ie, params->ie_len,
					   GFP_KERNEL);
		if (sdata->u.ibss.ie)
			sdata->u.ibss.ie_len = params->ie_len;
	}

	sdata->u.ibss.state = IEEE80211_IBSS_MLME_SEARCH;
	sdata->u.ibss.ibss_join_req = jiffies;

	memcpy(sdata->u.ibss.ssid, params->ssid, params->ssid_len);
	sdata->u.ibss.ssid_len = params->ssid_len;

	memcpy(&sdata->u.ibss.ht_capa, &params->ht_capa,
	       sizeof(sdata->u.ibss.ht_capa));
	memcpy(&sdata->u.ibss.ht_capa_mask, &params->ht_capa_mask,
	       sizeof(sdata->u.ibss.ht_capa_mask));

	/*
	 * 802.11n-2009 9.13.3.1: In an IBSS, the HT Protection field is
	 * reserved, but an HT STA shall protect HT transmissions as though
	 * the HT Protection field were set to non-HT mixed mode.
	 *
	 * In an IBSS, the RIFS Mode field of the HT Operation element is
	 * also reserved, but an HT STA shall operate as though this field
	 * were set to 1.
	 */

	sdata->vif.bss_conf.ht_operation_mode |=
		  IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED
		| IEEE80211_HT_PARAM_RIFS_MODE;

	changed |= BSS_CHANGED_HT | BSS_CHANGED_MCAST_RATE;
	ieee80211_bss_info_change_notify(sdata, changed);

	sdata->smps_mode = IEEE80211_SMPS_OFF;
	sdata->needed_rx_chains = local->rx_chains;
	sdata->control_port_over_nl80211 = params->control_port_over_nl80211;

	ieee80211_queue_work(&local->hw, &sdata->work);

	return 0;
}

int ieee80211_ibss_leave(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;

	ieee80211_ibss_disconnect(sdata);
	ifibss->ssid_len = 0;
	eth_zero_addr(ifibss->bssid);

	/* remove beacon */
	kfree(sdata->u.ibss.ie);

	/* on the next join, re-program HT parameters */
	memset(&ifibss->ht_capa, 0, sizeof(ifibss->ht_capa));
	memset(&ifibss->ht_capa_mask, 0, sizeof(ifibss->ht_capa_mask));

	synchronize_rcu();

	skb_queue_purge(&sdata->skb_queue);

	del_timer_sync(&sdata->u.ibss.timer);

	return 0;
}
