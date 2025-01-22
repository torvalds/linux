// SPDX-License-Identifier: GPL-2.0-only
/*
 * HE handling
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2019 - 2024 Intel Corporation
 */

#include "ieee80211_i.h"
#include "rate.h"

static void
ieee80211_update_from_he_6ghz_capa(const struct ieee80211_he_6ghz_capa *he_6ghz_capa,
				   struct link_sta_info *link_sta)
{
	struct sta_info *sta = link_sta->sta;
	enum ieee80211_smps_mode smps_mode;

	if (sta->sdata->vif.type == NL80211_IFTYPE_AP ||
	    sta->sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		switch (le16_get_bits(he_6ghz_capa->capa,
				      IEEE80211_HE_6GHZ_CAP_SM_PS)) {
		case WLAN_HT_CAP_SM_PS_INVALID:
		case WLAN_HT_CAP_SM_PS_STATIC:
			smps_mode = IEEE80211_SMPS_STATIC;
			break;
		case WLAN_HT_CAP_SM_PS_DYNAMIC:
			smps_mode = IEEE80211_SMPS_DYNAMIC;
			break;
		case WLAN_HT_CAP_SM_PS_DISABLED:
			smps_mode = IEEE80211_SMPS_OFF;
			break;
		}

		link_sta->pub->smps_mode = smps_mode;
	} else {
		link_sta->pub->smps_mode = IEEE80211_SMPS_OFF;
	}

	switch (le16_get_bits(he_6ghz_capa->capa,
			      IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN)) {
	case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454:
		link_sta->pub->agg.max_amsdu_len = IEEE80211_MAX_MPDU_LEN_VHT_11454;
		break;
	case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991:
		link_sta->pub->agg.max_amsdu_len = IEEE80211_MAX_MPDU_LEN_VHT_7991;
		break;
	case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895:
	default:
		link_sta->pub->agg.max_amsdu_len = IEEE80211_MAX_MPDU_LEN_VHT_3895;
		break;
	}

	ieee80211_sta_recalc_aggregates(&sta->sta);

	link_sta->pub->he_6ghz_capa = *he_6ghz_capa;
}

static void ieee80211_he_mcs_disable(__le16 *he_mcs)
{
	u32 i;

	for (i = 0; i < 8; i++)
		*he_mcs |= cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << i * 2);
}

static void ieee80211_he_mcs_intersection(__le16 *he_own_rx, __le16 *he_peer_rx,
					  __le16 *he_own_tx, __le16 *he_peer_tx)
{
	u32 i;
	u16 own_rx, own_tx, peer_rx, peer_tx;

	for (i = 0; i < 8; i++) {
		own_rx = le16_to_cpu(*he_own_rx);
		own_rx = (own_rx >> i * 2) & IEEE80211_HE_MCS_NOT_SUPPORTED;

		own_tx = le16_to_cpu(*he_own_tx);
		own_tx = (own_tx >> i * 2) & IEEE80211_HE_MCS_NOT_SUPPORTED;

		peer_rx = le16_to_cpu(*he_peer_rx);
		peer_rx = (peer_rx >> i * 2) & IEEE80211_HE_MCS_NOT_SUPPORTED;

		peer_tx = le16_to_cpu(*he_peer_tx);
		peer_tx = (peer_tx >> i * 2) & IEEE80211_HE_MCS_NOT_SUPPORTED;

		if (peer_tx != IEEE80211_HE_MCS_NOT_SUPPORTED) {
			if (own_rx == IEEE80211_HE_MCS_NOT_SUPPORTED)
				peer_tx = IEEE80211_HE_MCS_NOT_SUPPORTED;
			else if (own_rx < peer_tx)
				peer_tx = own_rx;
		}

		if (peer_rx != IEEE80211_HE_MCS_NOT_SUPPORTED) {
			if (own_tx == IEEE80211_HE_MCS_NOT_SUPPORTED)
				peer_rx = IEEE80211_HE_MCS_NOT_SUPPORTED;
			else if (own_tx < peer_rx)
				peer_rx = own_tx;
		}

		*he_peer_rx &=
			~cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << i * 2);
		*he_peer_rx |= cpu_to_le16(peer_rx << i * 2);

		*he_peer_tx &=
			~cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << i * 2);
		*he_peer_tx |= cpu_to_le16(peer_tx << i * 2);
	}
}

void
ieee80211_he_cap_ie_to_sta_he_cap(struct ieee80211_sub_if_data *sdata,
				  struct ieee80211_supported_band *sband,
				  const u8 *he_cap_ie, u8 he_cap_len,
				  const struct ieee80211_he_6ghz_capa *he_6ghz_capa,
				  struct link_sta_info *link_sta)
{
	struct ieee80211_sta_he_cap *he_cap = &link_sta->pub->he_cap;
	const struct ieee80211_sta_he_cap *own_he_cap_ptr;
	struct ieee80211_sta_he_cap own_he_cap;
	struct ieee80211_he_cap_elem *he_cap_ie_elem = (void *)he_cap_ie;
	u8 he_ppe_size;
	u8 mcs_nss_size;
	u8 he_total_size;
	bool own_160, peer_160, own_80p80, peer_80p80;

	memset(he_cap, 0, sizeof(*he_cap));

	if (!he_cap_ie)
		return;

	own_he_cap_ptr =
		ieee80211_get_he_iftype_cap_vif(sband, &sdata->vif);
	if (!own_he_cap_ptr)
		return;

	own_he_cap = *own_he_cap_ptr;

	/* Make sure size is OK */
	mcs_nss_size = ieee80211_he_mcs_nss_size(he_cap_ie_elem);
	he_ppe_size =
		ieee80211_he_ppe_size(he_cap_ie[sizeof(he_cap->he_cap_elem) +
						mcs_nss_size],
				      he_cap_ie_elem->phy_cap_info);
	he_total_size = sizeof(he_cap->he_cap_elem) + mcs_nss_size +
			he_ppe_size;
	if (he_cap_len < he_total_size)
		return;

	memcpy(&he_cap->he_cap_elem, he_cap_ie, sizeof(he_cap->he_cap_elem));

	/* HE Tx/Rx HE MCS NSS Support Field */
	memcpy(&he_cap->he_mcs_nss_supp,
	       &he_cap_ie[sizeof(he_cap->he_cap_elem)], mcs_nss_size);

	/* Check if there are (optional) PPE Thresholds */
	if (he_cap->he_cap_elem.phy_cap_info[6] &
	    IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT)
		memcpy(he_cap->ppe_thres,
		       &he_cap_ie[sizeof(he_cap->he_cap_elem) + mcs_nss_size],
		       he_ppe_size);

	he_cap->has_he = true;

	link_sta->cur_max_bandwidth = ieee80211_sta_cap_rx_bw(link_sta);
	link_sta->pub->bandwidth = ieee80211_sta_cur_vht_bw(link_sta);

	if (sband->band == NL80211_BAND_6GHZ && he_6ghz_capa)
		ieee80211_update_from_he_6ghz_capa(he_6ghz_capa, link_sta);

	ieee80211_he_mcs_intersection(&own_he_cap.he_mcs_nss_supp.rx_mcs_80,
				      &he_cap->he_mcs_nss_supp.rx_mcs_80,
				      &own_he_cap.he_mcs_nss_supp.tx_mcs_80,
				      &he_cap->he_mcs_nss_supp.tx_mcs_80);

	own_160 = own_he_cap.he_cap_elem.phy_cap_info[0] &
		  IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
	peer_160 = he_cap->he_cap_elem.phy_cap_info[0] &
		   IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;

	if (peer_160 && own_160) {
		ieee80211_he_mcs_intersection(&own_he_cap.he_mcs_nss_supp.rx_mcs_160,
					      &he_cap->he_mcs_nss_supp.rx_mcs_160,
					      &own_he_cap.he_mcs_nss_supp.tx_mcs_160,
					      &he_cap->he_mcs_nss_supp.tx_mcs_160);
	} else if (peer_160 && !own_160) {
		ieee80211_he_mcs_disable(&he_cap->he_mcs_nss_supp.rx_mcs_160);
		ieee80211_he_mcs_disable(&he_cap->he_mcs_nss_supp.tx_mcs_160);
		he_cap->he_cap_elem.phy_cap_info[0] &=
			~IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
	}

	own_80p80 = own_he_cap.he_cap_elem.phy_cap_info[0] &
		    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G;
	peer_80p80 = he_cap->he_cap_elem.phy_cap_info[0] &
		     IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G;

	if (peer_80p80 && own_80p80) {
		ieee80211_he_mcs_intersection(&own_he_cap.he_mcs_nss_supp.rx_mcs_80p80,
					      &he_cap->he_mcs_nss_supp.rx_mcs_80p80,
					      &own_he_cap.he_mcs_nss_supp.tx_mcs_80p80,
					      &he_cap->he_mcs_nss_supp.tx_mcs_80p80);
	} else if (peer_80p80 && !own_80p80) {
		ieee80211_he_mcs_disable(&he_cap->he_mcs_nss_supp.rx_mcs_80p80);
		ieee80211_he_mcs_disable(&he_cap->he_mcs_nss_supp.tx_mcs_80p80);
		he_cap->he_cap_elem.phy_cap_info[0] &=
			~IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G;
	}
}

void
ieee80211_he_op_ie_to_bss_conf(struct ieee80211_vif *vif,
			const struct ieee80211_he_operation *he_op_ie)
{
	memset(&vif->bss_conf.he_oper, 0, sizeof(vif->bss_conf.he_oper));
	if (!he_op_ie)
		return;

	vif->bss_conf.he_oper.params = __le32_to_cpu(he_op_ie->he_oper_params);
	vif->bss_conf.he_oper.nss_set = __le16_to_cpu(he_op_ie->he_mcs_nss_set);
}

void
ieee80211_he_spr_ie_to_bss_conf(struct ieee80211_vif *vif,
				const struct ieee80211_he_spr *he_spr_ie_elem)
{
	struct ieee80211_he_obss_pd *he_obss_pd =
					&vif->bss_conf.he_obss_pd;
	const u8 *data;

	memset(he_obss_pd, 0, sizeof(*he_obss_pd));

	if (!he_spr_ie_elem)
		return;

	he_obss_pd->sr_ctrl = he_spr_ie_elem->he_sr_control;
	data = he_spr_ie_elem->optional;

	if (he_spr_ie_elem->he_sr_control &
	    IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT)
		he_obss_pd->non_srg_max_offset = *data++;

	if (he_spr_ie_elem->he_sr_control &
	    IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT) {
		he_obss_pd->min_offset = *data++;
		he_obss_pd->max_offset = *data++;
		memcpy(he_obss_pd->bss_color_bitmap, data, 8);
		data += 8;
		memcpy(he_obss_pd->partial_bssid_bitmap, data, 8);
		he_obss_pd->enable = true;
	}
}

static void ieee80211_link_sta_rc_update_omi(struct ieee80211_link_data *link,
					     struct link_sta_info *link_sta)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_supported_band *sband;
	enum ieee80211_sta_rx_bandwidth new_bw;
	enum nl80211_band band;

	band = link->conf->chanreq.oper.chan->band;
	sband = sdata->local->hw.wiphy->bands[band];

	new_bw = ieee80211_sta_cur_vht_bw(link_sta);
	if (link_sta->pub->bandwidth == new_bw)
		return;

	link_sta->pub->bandwidth = new_bw;
	rate_control_rate_update(sdata->local, sband, link_sta,
				 IEEE80211_RC_BW_CHANGED);
}

bool ieee80211_prepare_rx_omi_bw(struct ieee80211_link_sta *pub_link_sta,
				 enum ieee80211_sta_rx_bandwidth bw)
{
	struct sta_info *sta = container_of(pub_link_sta->sta,
					    struct sta_info, sta);
	struct ieee80211_local *local = sta->sdata->local;
	struct link_sta_info *link_sta =
		sdata_dereference(sta->link[pub_link_sta->link_id], sta->sdata);
	struct ieee80211_link_data *link =
		sdata_dereference(sta->sdata->link[pub_link_sta->link_id],
				  sta->sdata);
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *chanctx;
	bool ret;

	if (WARN_ON(!link || !link_sta || link_sta->pub != pub_link_sta))
		return false;

	conf = sdata_dereference(link->conf->chanctx_conf, sta->sdata);
	if (WARN_ON(!conf))
		return false;

	trace_api_prepare_rx_omi_bw(local, sta->sdata, link_sta, bw);

	chanctx = container_of(conf, typeof(*chanctx), conf);

	if (link_sta->rx_omi_bw_staging == bw) {
		ret = false;
		goto trace;
	}

	/* must call this API in pairs */
	if (WARN_ON(link_sta->rx_omi_bw_tx != link_sta->rx_omi_bw_staging ||
		    link_sta->rx_omi_bw_rx != link_sta->rx_omi_bw_staging)) {
		ret = false;
		goto trace;
	}

	if (bw < link_sta->rx_omi_bw_staging) {
		link_sta->rx_omi_bw_tx = bw;
		ieee80211_link_sta_rc_update_omi(link, link_sta);
	} else {
		link_sta->rx_omi_bw_rx = bw;
		ieee80211_recalc_chanctx_min_def(local, chanctx, NULL, false);
	}

	link_sta->rx_omi_bw_staging = bw;
	ret = true;
trace:
	trace_api_return_bool(local, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(ieee80211_prepare_rx_omi_bw);

void ieee80211_finalize_rx_omi_bw(struct ieee80211_link_sta *pub_link_sta)
{
	struct sta_info *sta = container_of(pub_link_sta->sta,
					    struct sta_info, sta);
	struct ieee80211_local *local = sta->sdata->local;
	struct link_sta_info *link_sta =
		sdata_dereference(sta->link[pub_link_sta->link_id], sta->sdata);
	struct ieee80211_link_data *link =
		sdata_dereference(sta->sdata->link[pub_link_sta->link_id],
				  sta->sdata);
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *chanctx;

	if (WARN_ON(!link || !link_sta || link_sta->pub != pub_link_sta))
		return;

	conf = sdata_dereference(link->conf->chanctx_conf, sta->sdata);
	if (WARN_ON(!conf))
		return;

	trace_api_finalize_rx_omi_bw(local, sta->sdata, link_sta);

	chanctx = container_of(conf, typeof(*chanctx), conf);

	if (link_sta->rx_omi_bw_tx != link_sta->rx_omi_bw_staging) {
		/* rate control in finalize only when widening bandwidth */
		WARN_ON(link_sta->rx_omi_bw_tx > link_sta->rx_omi_bw_staging);
		link_sta->rx_omi_bw_tx = link_sta->rx_omi_bw_staging;
		ieee80211_link_sta_rc_update_omi(link, link_sta);
	}

	if (link_sta->rx_omi_bw_rx != link_sta->rx_omi_bw_staging) {
		/* channel context in finalize only when narrowing bandwidth */
		WARN_ON(link_sta->rx_omi_bw_rx < link_sta->rx_omi_bw_staging);
		link_sta->rx_omi_bw_rx = link_sta->rx_omi_bw_staging;
		ieee80211_recalc_chanctx_min_def(local, chanctx, NULL, false);
	}

	trace_api_return_void(local);
}
EXPORT_SYMBOL_GPL(ieee80211_finalize_rx_omi_bw);
