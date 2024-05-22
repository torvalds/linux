// SPDX-License-Identifier: GPL-2.0-only
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
 * Copyright (C) 2018, 2020, 2022-2023 Intel Corporation
 */

#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "sta_info.h"
#include "wme.h"

static bool
wbcs_elem_to_chandef(const struct ieee80211_wide_bw_chansw_ie *wbcs_elem,
		     struct cfg80211_chan_def *chandef)
{
	u8 ccfs0 = wbcs_elem->new_center_freq_seg0;
	u8 ccfs1 = wbcs_elem->new_center_freq_seg1;
	u32 cf0 = ieee80211_channel_to_frequency(ccfs0, chandef->chan->band);
	u32 cf1 = ieee80211_channel_to_frequency(ccfs1, chandef->chan->band);

	switch (wbcs_elem->new_channel_width) {
	case IEEE80211_VHT_CHANWIDTH_160MHZ:
		/* deprecated encoding */
		chandef->width = NL80211_CHAN_WIDTH_160;
		chandef->center_freq1 = cf0;
		break;
	case IEEE80211_VHT_CHANWIDTH_80P80MHZ:
		/* deprecated encoding */
		chandef->width = NL80211_CHAN_WIDTH_80P80;
		chandef->center_freq1 = cf0;
		chandef->center_freq2 = cf1;
		break;
	case IEEE80211_VHT_CHANWIDTH_80MHZ:
		chandef->width = NL80211_CHAN_WIDTH_80;
		chandef->center_freq1 = cf0;

		if (ccfs1) {
			u8 diff = abs(ccfs0 - ccfs1);

			if (diff == 8) {
				chandef->width = NL80211_CHAN_WIDTH_160;
				chandef->center_freq1 = cf1;
			} else if (diff > 8) {
				chandef->width = NL80211_CHAN_WIDTH_80P80;
				chandef->center_freq2 = cf1;
			}
		}
		break;
	case IEEE80211_VHT_CHANWIDTH_USE_HT:
	default:
		/* If the WBCS Element is present, new channel bandwidth is
		 * at least 40 MHz.
		 */
		chandef->width = NL80211_CHAN_WIDTH_40;
		chandef->center_freq1 = cf0;
		break;
	}

	return cfg80211_chandef_valid(chandef);
}

static void
validate_chandef_by_ht_vht_oper(struct ieee80211_sub_if_data *sdata,
				struct ieee80211_conn_settings *conn,
				u32 vht_cap_info,
				struct cfg80211_chan_def *chandef)
{
	u32 control_freq, center_freq1, center_freq2;
	enum nl80211_chan_width chan_width;
	struct ieee80211_ht_operation ht_oper;
	struct ieee80211_vht_operation vht_oper;

	if (conn->mode < IEEE80211_CONN_MODE_HT ||
	    conn->bw_limit < IEEE80211_CONN_BW_LIMIT_40) {
		chandef->chan = NULL;
		return;
	}

	control_freq = chandef->chan->center_freq;
	center_freq1 = chandef->center_freq1;
	center_freq2 = chandef->center_freq2;
	chan_width = chandef->width;

	ht_oper.primary_chan = ieee80211_frequency_to_channel(control_freq);
	if (control_freq != center_freq1)
		ht_oper.ht_param = control_freq > center_freq1 ?
			IEEE80211_HT_PARAM_CHA_SEC_BELOW :
			IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
	else
		ht_oper.ht_param = IEEE80211_HT_PARAM_CHA_SEC_NONE;

	ieee80211_chandef_ht_oper(&ht_oper, chandef);

	if (conn->mode < IEEE80211_CONN_MODE_VHT)
		return;

	vht_oper.center_freq_seg0_idx =
		ieee80211_frequency_to_channel(center_freq1);
	vht_oper.center_freq_seg1_idx = center_freq2 ?
		ieee80211_frequency_to_channel(center_freq2) : 0;

	switch (chan_width) {
	case NL80211_CHAN_WIDTH_320:
		WARN_ON(1);
		break;
	case NL80211_CHAN_WIDTH_160:
		vht_oper.chan_width = IEEE80211_VHT_CHANWIDTH_80MHZ;
		vht_oper.center_freq_seg1_idx = vht_oper.center_freq_seg0_idx;
		vht_oper.center_freq_seg0_idx +=
			control_freq < center_freq1 ? -8 : 8;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		vht_oper.chan_width = IEEE80211_VHT_CHANWIDTH_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
		vht_oper.chan_width = IEEE80211_VHT_CHANWIDTH_80MHZ;
		break;
	default:
		vht_oper.chan_width = IEEE80211_VHT_CHANWIDTH_USE_HT;
		break;
	}

	ht_oper.operation_mode =
		le16_encode_bits(vht_oper.center_freq_seg1_idx,
				 IEEE80211_HT_OP_MODE_CCFS2_MASK);

	if (!ieee80211_chandef_vht_oper(&sdata->local->hw, vht_cap_info,
					&vht_oper, &ht_oper, chandef))
		chandef->chan = NULL;
}

static void
validate_chandef_by_6ghz_he_eht_oper(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_conn_settings *conn,
				     struct cfg80211_chan_def *chandef)
{
	struct ieee80211_local *local = sdata->local;
	u32 control_freq, center_freq1, center_freq2;
	enum nl80211_chan_width chan_width;
	struct {
		struct ieee80211_he_operation _oper;
		struct ieee80211_he_6ghz_oper _6ghz_oper;
	} __packed he;
	struct {
		struct ieee80211_eht_operation _oper;
		struct ieee80211_eht_operation_info _oper_info;
	} __packed eht;
	const struct ieee80211_eht_operation *eht_oper;

	if (conn->mode < IEEE80211_CONN_MODE_HE) {
		chandef->chan = NULL;
		return;
	}

	control_freq = chandef->chan->center_freq;
	center_freq1 = chandef->center_freq1;
	center_freq2 = chandef->center_freq2;
	chan_width = chandef->width;

	he._oper.he_oper_params =
		le32_encode_bits(1, IEEE80211_HE_OPERATION_6GHZ_OP_INFO);
	he._6ghz_oper.primary =
		ieee80211_frequency_to_channel(control_freq);
	he._6ghz_oper.ccfs0 = ieee80211_frequency_to_channel(center_freq1);
	he._6ghz_oper.ccfs1 = center_freq2 ?
		ieee80211_frequency_to_channel(center_freq2) : 0;

	switch (chan_width) {
	case NL80211_CHAN_WIDTH_320:
		he._6ghz_oper.ccfs1 = he._6ghz_oper.ccfs0;
		he._6ghz_oper.ccfs0 += control_freq < center_freq1 ? -16 : 16;
		he._6ghz_oper.control = IEEE80211_EHT_OPER_CHAN_WIDTH_320MHZ;
		break;
	case NL80211_CHAN_WIDTH_160:
		he._6ghz_oper.ccfs1 = he._6ghz_oper.ccfs0;
		he._6ghz_oper.ccfs0 += control_freq < center_freq1 ? -8 : 8;
		fallthrough;
	case NL80211_CHAN_WIDTH_80P80:
		he._6ghz_oper.control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_160MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
		he._6ghz_oper.control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_40:
		he._6ghz_oper.control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_40MHZ;
		break;
	default:
		he._6ghz_oper.control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_20MHZ;
		break;
	}

	if (conn->mode < IEEE80211_CONN_MODE_EHT) {
		eht_oper = NULL;
	} else {
		eht._oper.params = IEEE80211_EHT_OPER_INFO_PRESENT;
		eht._oper_info.control = he._6ghz_oper.control;
		eht._oper_info.ccfs0 = he._6ghz_oper.ccfs0;
		eht._oper_info.ccfs1 = he._6ghz_oper.ccfs1;
		eht_oper = &eht._oper;
	}

	if (!ieee80211_chandef_he_6ghz_oper(local, &he._oper,
					    eht_oper, chandef))
		chandef->chan = NULL;
}

int ieee80211_parse_ch_switch_ie(struct ieee80211_sub_if_data *sdata,
				 struct ieee802_11_elems *elems,
				 enum nl80211_band current_band,
				 u32 vht_cap_info,
				 struct ieee80211_conn_settings *conn,
				 u8 *bssid,
				 struct ieee80211_csa_ie *csa_ie)
{
	enum nl80211_band new_band = current_band;
	int new_freq;
	u8 new_chan_no = 0, new_op_class = 0;
	struct ieee80211_channel *new_chan;
	struct cfg80211_chan_def new_chandef = {};
	const struct ieee80211_sec_chan_offs_ie *sec_chan_offs;
	const struct ieee80211_wide_bw_chansw_ie *wide_bw_chansw_ie;
	const struct ieee80211_bandwidth_indication *bwi;
	const struct ieee80211_ext_chansw_ie *ext_chansw_elem;
	int secondary_channel_offset = -1;

	memset(csa_ie, 0, sizeof(*csa_ie));

	sec_chan_offs = elems->sec_chan_offs;
	wide_bw_chansw_ie = elems->wide_bw_chansw_ie;
	bwi = elems->bandwidth_indication;
	ext_chansw_elem = elems->ext_chansw_ie;

	if (conn->mode < IEEE80211_CONN_MODE_HT ||
	    conn->bw_limit < IEEE80211_CONN_BW_LIMIT_40) {
		sec_chan_offs = NULL;
		wide_bw_chansw_ie = NULL;
	}

	if (conn->mode < IEEE80211_CONN_MODE_VHT)
		wide_bw_chansw_ie = NULL;

	if (ext_chansw_elem) {
		new_op_class = ext_chansw_elem->new_operating_class;

		if (!ieee80211_operating_class_to_band(new_op_class, &new_band)) {
			new_op_class = 0;
			sdata_info(sdata, "cannot understand ECSA IE operating class, %d, ignoring\n",
				   ext_chansw_elem->new_operating_class);
		} else {
			new_chan_no = ext_chansw_elem->new_ch_num;
			csa_ie->count = ext_chansw_elem->count;
			csa_ie->mode = ext_chansw_elem->mode;
		}
	}

	if (!new_op_class && elems->ch_switch_ie) {
		new_chan_no = elems->ch_switch_ie->new_ch_num;
		csa_ie->count = elems->ch_switch_ie->count;
		csa_ie->mode = elems->ch_switch_ie->mode;
	}

	/* nothing here we understand */
	if (!new_chan_no)
		return 1;

	/* Mesh Channel Switch Parameters Element */
	if (elems->mesh_chansw_params_ie) {
		csa_ie->ttl = elems->mesh_chansw_params_ie->mesh_ttl;
		csa_ie->mode = elems->mesh_chansw_params_ie->mesh_flags;
		csa_ie->pre_value = le16_to_cpu(
				elems->mesh_chansw_params_ie->mesh_pre_value);

		if (elems->mesh_chansw_params_ie->mesh_flags &
				WLAN_EID_CHAN_SWITCH_PARAM_REASON)
			csa_ie->reason_code = le16_to_cpu(
				elems->mesh_chansw_params_ie->mesh_reason);
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
	} else if (conn->mode >= IEEE80211_CONN_MODE_HT) {
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
		cfg80211_chandef_create(&csa_ie->chanreq.oper, new_chan,
					NL80211_CHAN_HT20);
		break;
	case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
		cfg80211_chandef_create(&csa_ie->chanreq.oper, new_chan,
					NL80211_CHAN_HT40PLUS);
		break;
	case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
		cfg80211_chandef_create(&csa_ie->chanreq.oper, new_chan,
					NL80211_CHAN_HT40MINUS);
		break;
	case -1:
		cfg80211_chandef_create(&csa_ie->chanreq.oper, new_chan,
					NL80211_CHAN_NO_HT);
		/* keep width for 5/10 MHz channels */
		switch (sdata->vif.bss_conf.chanreq.oper.width) {
		case NL80211_CHAN_WIDTH_5:
		case NL80211_CHAN_WIDTH_10:
			csa_ie->chanreq.oper.width =
				sdata->vif.bss_conf.chanreq.oper.width;
			break;
		default:
			break;
		}
		break;
	}

	/* parse one of the Elements to build a new chandef */
	memset(&new_chandef, 0, sizeof(new_chandef));
	new_chandef.chan = new_chan;
	if (bwi) {
		/* start with the CSA one */
		new_chandef = csa_ie->chanreq.oper;
		/* and update the width accordingly */
		ieee80211_chandef_eht_oper(&bwi->info, &new_chandef);

		if (bwi->params & IEEE80211_BW_IND_DIS_SUBCH_PRESENT)
			new_chandef.punctured =
				get_unaligned_le16(bwi->info.optional);
	} else if (!wide_bw_chansw_ie || !wbcs_elem_to_chandef(wide_bw_chansw_ie,
							       &new_chandef)) {
		if (!ieee80211_operating_class_to_chandef(new_op_class, new_chan,
							  &new_chandef))
			new_chandef = csa_ie->chanreq.oper;
	}

	/* check if the new chandef fits the capabilities */
	if (new_band == NL80211_BAND_6GHZ)
		validate_chandef_by_6ghz_he_eht_oper(sdata, conn, &new_chandef);
	else
		validate_chandef_by_ht_vht_oper(sdata, conn, vht_cap_info,
						&new_chandef);

	/* if data is there validate the bandwidth & use it */
	if (new_chandef.chan) {
		if (conn->bw_limit < IEEE80211_CONN_BW_LIMIT_320 &&
		    new_chandef.width == NL80211_CHAN_WIDTH_320)
			ieee80211_chandef_downgrade(&new_chandef, NULL);

		if (conn->bw_limit < IEEE80211_CONN_BW_LIMIT_160 &&
		    (new_chandef.width == NL80211_CHAN_WIDTH_80P80 ||
		     new_chandef.width == NL80211_CHAN_WIDTH_160))
			ieee80211_chandef_downgrade(&new_chandef, NULL);

		if (!cfg80211_chandef_compatible(&new_chandef,
						 &csa_ie->chanreq.oper)) {
			sdata_info(sdata,
				   "BSS %pM: CSA has inconsistent channel data, disconnecting\n",
				   bssid);
			return -EINVAL;
		}

		csa_ie->chanreq.oper = new_chandef;
	}

	if (elems->max_channel_switch_time)
		csa_ie->max_switch_time =
			(elems->max_channel_switch_time[0] << 0) |
			(elems->max_channel_switch_time[1] <<  8) |
			(elems->max_channel_switch_time[2] << 16);

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
	msr_report = skb_put_zero(skb, 24);
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
