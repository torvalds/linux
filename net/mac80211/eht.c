// SPDX-License-Identifier: GPL-2.0-only
/*
 * EHT handling
 *
 * Copyright(c) 2021-2022 Intel Corporation
 */

#include "ieee80211_i.h"

void
ieee80211_eht_cap_ie_to_sta_eht_cap(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_supported_band *sband,
				    const u8 *he_cap_ie, u8 he_cap_len,
				    const struct ieee80211_eht_cap_elem *eht_cap_ie_elem,
				    u8 eht_cap_len, struct sta_info *sta)
{
	struct ieee80211_sta_eht_cap *eht_cap = &sta->sta.deflink.eht_cap;
	struct ieee80211_he_cap_elem *he_cap_ie_elem = (void *)he_cap_ie;
	u8 eht_ppe_size = 0;
	u8 mcs_nss_size;
	u8 eht_total_size = sizeof(eht_cap->eht_cap_elem);
	u8 *pos = (u8 *)eht_cap_ie_elem;

	memset(eht_cap, 0, sizeof(*eht_cap));

	if (!eht_cap_ie_elem ||
	    !ieee80211_get_eht_iftype_cap(sband,
					 ieee80211_vif_type_p2p(&sdata->vif)))
		return;

	mcs_nss_size = ieee80211_eht_mcs_nss_size(he_cap_ie_elem,
						  &eht_cap_ie_elem->fixed);

	eht_total_size += mcs_nss_size;

	/* Calculate the PPE thresholds length only if the header is present */
	if (eht_cap_ie_elem->fixed.phy_cap_info[5] &
			IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT) {
		u16 eht_ppe_hdr;

		if (eht_cap_len < eht_total_size + sizeof(u16))
			return;

		eht_ppe_hdr = get_unaligned_le16(eht_cap_ie_elem->optional + mcs_nss_size);
		eht_ppe_size =
			ieee80211_eht_ppe_size(eht_ppe_hdr,
					       eht_cap_ie_elem->fixed.phy_cap_info);
		eht_total_size += eht_ppe_size;

		/* we calculate as if NSS > 8 are valid, but don't handle that */
		if (eht_ppe_size > sizeof(eht_cap->eht_ppe_thres))
			return;
	}

	if (eht_cap_len < eht_total_size)
		return;

	/* Copy the static portion of the EHT capabilities */
	memcpy(&eht_cap->eht_cap_elem, pos, sizeof(eht_cap->eht_cap_elem));
	pos += sizeof(eht_cap->eht_cap_elem);

	/* Copy MCS/NSS which depends on the peer capabilities */
	memset(&eht_cap->eht_mcs_nss_supp, 0,
	       sizeof(eht_cap->eht_mcs_nss_supp));
	memcpy(&eht_cap->eht_mcs_nss_supp, pos, mcs_nss_size);

	if (eht_ppe_size)
		memcpy(eht_cap->eht_ppe_thres,
		       &eht_cap_ie_elem->optional[mcs_nss_size],
		       eht_ppe_size);

	eht_cap->has_eht = true;

	sta->deflink.cur_max_bandwidth = ieee80211_sta_cap_rx_bw(sta);
	sta->sta.deflink.bandwidth = ieee80211_sta_cur_vht_bw(sta);
}
