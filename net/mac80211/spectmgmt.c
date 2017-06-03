/*
 * spectrum management
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007  Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007-2008, Intel Corporation
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "sta_info.h"
#include "wme.h"

int ieee80211_parse_ch_switch_ie(struct ieee80211_sub_if_data *sdata,
				 struct ieee802_11_elems *elems,
				 enum nl80211_band current_band,
				 u32 sta_flags, u8 *bssid,
				 struct ieee80211_csa_ie *csa_ie)
{
	enum nl80211_band new_band;
	int new_freq;
	u8 new_chan_no;
	struct ieee80211_channel *new_chan;
	struct cfg80211_chan_def new_vht_chandef = {};
	const struct ieee80211_sec_chan_offs_ie *sec_chan_offs;
	const struct ieee80211_wide_bw_chansw_ie *wide_bw_chansw_ie;
	int secondary_channel_offset = -1;

	sec_chan_offs = elems->sec_chan_offs;
	wide_bw_chansw_ie = elems->wide_bw_chansw_ie;

	if (sta_flags & (IEEE80211_STA_DISABLE_HT |
			 IEEE80211_STA_DISABLE_40MHZ)) {
		sec_chan_offs = NULL;
		wide_bw_chansw_ie = NULL;
	}

	if (sta_flags & IEEE80211_STA_DISABLE_VHT)
		wide_bw_chansw_ie = NULL;

	if (elems->ext_chansw_ie) {
		if (!ieee80211_operating_class_to_band(
				elems->ext_chansw_ie->new_operating_class,
				&new_band)) {
			sdata_info(sdata,
				   "cannot understand ECSA IE operating class %d, disconnecting\n",
				   elems->ext_chansw_ie->new_operating_class);
			return -EINVAL;
		}
		new_chan_no = elems->ext_chansw_ie->new_ch_num;
		csa_ie->count = elems->ext_chansw_ie->count;
		csa_ie->mode = elems->ext_chansw_ie->mode;
	} else if (elems->ch_switch_ie) {
		new_band = current_band;
		new_chan_no = elems->ch_switch_ie->new_ch_num;
		csa_ie->count = elems->ch_switch_ie->count;
		csa_ie->mode = elems->ch_switch_ie->mode;
	} else {
		/* nothing here we understand */
		return 1;
	}

	/* Mesh Channel Switch Parameters Element */
	if (elems->mesh_chansw_params_ie) {
		csa_ie->ttl = elems->mesh_chansw_params_ie->mesh_ttl;
		csa_ie->mode = elems->mesh_chansw_params_ie->mesh_flags;
		csa_ie->pre_value = le16_to_cpu(
				elems->mesh_chansw_params_ie->mesh_pre_value);
	}

	new_freq = ieee80211_channel_to_frequency(new_chan_no, new_band);
	new_chan = ieee80211_get_channel(sdata->local->hw.wiphy, new_freq);
	if (!new_chan || new_chan->flags & IEEE80211_CHAN_DISABLED) {
		sdata_info(sdata,
			   "BSS %pM switches to unsupported channel (%d MHz), disconnecting\n",
			   bssid, new_freq);
		return -EINVAL;
	}

	if (sec_chan_offs) {
		secondary_channel_offset = sec_chan_offs->sec_chan_offs;
	} else if (!(sta_flags & IEEE80211_STA_DISABLE_HT)) {
		/* If the secondary channel offset IE is not present,
		 * we can't know what's the post-CSA offset, so the
		 * best we can do is use 20MHz.
		*/
		secondary_channel_offset = IEEE80211_HT_PARAM_CHA_SEC_NONE;
	}

	switch (secondary_channel_offset) {
	default:
		/* secondary_channel_offset was present but is invalid */
	case IEEE80211_HT_PARAM_CHA_SEC_NONE:
		cfg80211_chandef_create(&csa_ie->chandef, new_chan,
					NL80211_CHAN_HT20);
		break;
	case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
		cfg80211_chandef_create(&csa_ie->chandef, new_chan,
					NL80211_CHAN_HT40PLUS);
		break;
	case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
		cfg80211_chandef_create(&csa_ie->chandef, new_chan,
					NL80211_CHAN_HT40MINUS);
		break;
	case -1:
		cfg80211_chandef_create(&csa_ie->chandef, new_chan,
					NL80211_CHAN_NO_HT);
		/* keep width for 5/10 MHz channels */
		switch (sdata->vif.bss_conf.chandef.width) {
		case NL80211_CHAN_WIDTH_5:
		case NL80211_CHAN_WIDTH_10:
			csa_ie->chandef.width =
				sdata->vif.bss_conf.chandef.width;
			break;
		default:
			break;
		}
		break;
	}

	if (wide_bw_chansw_ie) {
		struct ieee80211_vht_operation vht_oper = {
			.chan_width =
				wide_bw_chansw_ie->new_channel_width,
			.center_freq_seg0_idx =
				wide_bw_chansw_ie->new_center_freq_seg0,
			.center_freq_seg1_idx =
				wide_bw_chansw_ie->new_center_freq_seg1,
			/* .basic_mcs_set doesn't matter */
		};

		/* default, for the case of IEEE80211_VHT_CHANWIDTH_USE_HT,
		 * to the previously parsed chandef
		 */
		new_vht_chandef = csa_ie->chandef;

		/* ignore if parsing fails */
		if (!ieee80211_chandef_vht_oper(&vht_oper, &new_vht_chandef))
			new_vht_chandef.chan = NULL;

		if (sta_flags & IEEE80211_STA_DISABLE_80P80MHZ &&
		    new_vht_chandef.width == NL80211_CHAN_WIDTH_80P80)
			ieee80211_chandef_downgrade(&new_vht_chandef);
		if (sta_flags & IEEE80211_STA_DISABLE_160MHZ &&
		    new_vht_chandef.width == NL80211_CHAN_WIDTH_160)
			ieee80211_chandef_downgrade(&new_vht_chandef);
	}

	/* if VHT data is there validate & use it */
	if (new_vht_chandef.chan) {
		if (!cfg80211_chandef_compatible(&new_vht_chandef,
						 &csa_ie->chandef)) {
			sdata_info(sdata,
				   "BSS %pM: CSA has inconsistent channel data, disconnecting\n",
				   bssid);
			return -EINVAL;
		}
		csa_ie->chandef = new_vht_chandef;
	}

	return 0;
}

static void ieee80211_send_refuse_measurement_request(struct ieee80211_sub_if_data *sdata,
					struct ieee80211_msrment_ie *request_ie,
					const u8 *da, const u8 *bssid,
					u8 dialog_token)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *msr_report;

	skb = dev_alloc_skb(sizeof(*msr_report) + local->hw.extra_tx_headroom +
				sizeof(struct ieee80211_msrment_ie));
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);
	msr_report = (struct ieee80211_mgmt *)skb_put(skb, 24);
	memset(msr_report, 0, 24);
	memcpy(msr_report->da, da, ETH_ALEN);
	memcpy(msr_report->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(msr_report->bssid, bssid, ETH_ALEN);
	msr_report->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						IEEE80211_STYPE_ACTION);

	skb_put(skb, 1 + sizeof(msr_report->u.action.u.measurement));
	msr_report->u.action.category = WLAN_CATEGORY_SPECTRUM_MGMT;
	msr_report->u.action.u.measurement.action_code =
				WLAN_ACTION_SPCT_MSR_RPRT;
	msr_report->u.action.u.measurement.dialog_token = dialog_token;

	msr_report->u.action.u.measurement.element_id = WLAN_EID_MEASURE_REPORT;
	msr_report->u.action.u.measurement.length =
			sizeof(struct ieee80211_msrment_ie);

	memset(&msr_report->u.action.u.measurement.msr_elem, 0,
		sizeof(struct ieee80211_msrment_ie));
	msr_report->u.action.u.measurement.msr_elem.token = request_ie->token;
	msr_report->u.action.u.measurement.msr_elem.mode |=
			IEEE80211_SPCT_MSR_RPRT_MODE_REFUSED;
	msr_report->u.action.u.measurement.msr_elem.type = request_ie->type;

	ieee80211_tx_skb(sdata, skb);
}

void ieee80211_process_measurement_req(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_mgmt *mgmt,
				       size_t len)
{
	/*
	 * Ignoring measurement request is spec violation.
	 * Mandatory measurements must be reported optional
	 * measurements might be refused or reported incapable
	 * For now just refuse
	 * TODO: Answer basic measurement as unmeasured
	 */
	ieee80211_send_refuse_measurement_request(sdata,
			&mgmt->u.action.u.measurement.msr_elem,
			mgmt->sa, mgmt->bssid,
			mgmt->u.action.u.measurement.dialog_token);
}
