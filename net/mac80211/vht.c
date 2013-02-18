/*
 * VHT handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ieee80211.h>
#include <linux/export.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "rate.h"


void
ieee80211_vht_cap_ie_to_sta_vht_cap(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_supported_band *sband,
				    const struct ieee80211_vht_cap *vht_cap_ie,
				    struct sta_info *sta)
{
	struct ieee80211_sta_vht_cap *vht_cap = &sta->sta.vht_cap;

	memset(vht_cap, 0, sizeof(*vht_cap));

	if (!sta->sta.ht_cap.ht_supported)
		return;

	if (!vht_cap_ie || !sband->vht_cap.vht_supported)
		return;

	/* A VHT STA must support 40 MHz */
	if (!(sta->sta.ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40))
		return;

	vht_cap->vht_supported = true;

	vht_cap->cap = le32_to_cpu(vht_cap_ie->vht_cap_info);

	/* Copy peer MCS info, the driver might need them. */
	memcpy(&vht_cap->vht_mcs, &vht_cap_ie->supp_mcs,
	       sizeof(struct ieee80211_vht_mcs_info));

	switch (vht_cap->cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) {
	case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ:
	case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ:
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_160;
		break;
	default:
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_80;
	}

	sta->sta.bandwidth = ieee80211_sta_cur_vht_bw(sta);
}

enum ieee80211_sta_rx_bandwidth ieee80211_sta_cur_vht_bw(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u32 cap = sta->sta.vht_cap.cap;
	enum ieee80211_sta_rx_bandwidth bw;

	if (!sta->sta.vht_cap.vht_supported) {
		bw = sta->sta.ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 ?
				IEEE80211_STA_RX_BW_40 : IEEE80211_STA_RX_BW_20;
		goto check_max;
	}

	switch (sdata->vif.bss_conf.chandef.width) {
	default:
		WARN_ON_ONCE(1);
		/* fall through */
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_40:
		bw = sta->sta.ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 ?
				IEEE80211_STA_RX_BW_40 : IEEE80211_STA_RX_BW_20;
		break;
	case NL80211_CHAN_WIDTH_160:
		if ((cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) ==
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ) {
			bw = IEEE80211_STA_RX_BW_160;
			break;
		}
		/* fall through */
	case NL80211_CHAN_WIDTH_80P80:
		if ((cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) ==
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ) {
			bw = IEEE80211_STA_RX_BW_160;
			break;
		}
		/* fall through */
	case NL80211_CHAN_WIDTH_80:
		bw = IEEE80211_STA_RX_BW_80;
	}

 check_max:
	if (bw > sta->cur_max_bandwidth)
		bw = sta->cur_max_bandwidth;
	return bw;
}

void ieee80211_sta_set_rx_nss(struct sta_info *sta)
{
	u8 ht_rx_nss = 0, vht_rx_nss = 0;

	/* if we received a notification already don't overwrite it */
	if (sta->sta.rx_nss)
		return;

	if (sta->sta.ht_cap.ht_supported) {
		if (sta->sta.ht_cap.mcs.rx_mask[0])
			ht_rx_nss++;
		if (sta->sta.ht_cap.mcs.rx_mask[1])
			ht_rx_nss++;
		if (sta->sta.ht_cap.mcs.rx_mask[2])
			ht_rx_nss++;
		if (sta->sta.ht_cap.mcs.rx_mask[3])
			ht_rx_nss++;
		/* FIXME: consider rx_highest? */
	}

	if (sta->sta.vht_cap.vht_supported) {
		int i;
		u16 rx_mcs_map;

		rx_mcs_map = le16_to_cpu(sta->sta.vht_cap.vht_mcs.rx_mcs_map);

		for (i = 7; i >= 0; i--) {
			u8 mcs = (rx_mcs_map >> (2 * i)) & 3;

			if (mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
				vht_rx_nss = i + 1;
				break;
			}
		}
		/* FIXME: consider rx_highest? */
	}

	ht_rx_nss = max(ht_rx_nss, vht_rx_nss);
	sta->sta.rx_nss = max_t(u8, 1, ht_rx_nss);
}

void ieee80211_vht_handle_opmode(struct ieee80211_sub_if_data *sdata,
				 struct sta_info *sta, u8 opmode,
				 enum ieee80211_band band, bool nss_only)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	enum ieee80211_sta_rx_bandwidth new_bw;
	u32 changed = 0;
	u8 nss;

	sband = local->hw.wiphy->bands[band];

	/* ignore - no support for BF yet */
	if (opmode & IEEE80211_OPMODE_NOTIF_RX_NSS_TYPE_BF)
		return;

	nss = opmode & IEEE80211_OPMODE_NOTIF_RX_NSS_MASK;
	nss >>= IEEE80211_OPMODE_NOTIF_RX_NSS_SHIFT;
	nss += 1;

	if (sta->sta.rx_nss != nss) {
		sta->sta.rx_nss = nss;
		changed |= IEEE80211_RC_NSS_CHANGED;
	}

	if (nss_only)
		goto change;

	switch (opmode & IEEE80211_OPMODE_NOTIF_CHANWIDTH_MASK) {
	case IEEE80211_OPMODE_NOTIF_CHANWIDTH_20MHZ:
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_20;
		break;
	case IEEE80211_OPMODE_NOTIF_CHANWIDTH_40MHZ:
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_40;
		break;
	case IEEE80211_OPMODE_NOTIF_CHANWIDTH_80MHZ:
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_80;
		break;
	case IEEE80211_OPMODE_NOTIF_CHANWIDTH_160MHZ:
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_160;
		break;
	}

	new_bw = ieee80211_sta_cur_vht_bw(sta);
	if (new_bw != sta->sta.bandwidth) {
		sta->sta.bandwidth = new_bw;
		changed |= IEEE80211_RC_NSS_CHANGED;
	}

 change:
	if (changed)
		rate_control_rate_update(local, sband, sta, changed);
}
