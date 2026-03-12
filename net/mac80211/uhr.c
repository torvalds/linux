// SPDX-License-Identifier: GPL-2.0-only
/*
 * UHR handling
 *
 * Copyright(c) 2025-2026 Intel Corporation
 */

#include "ieee80211_i.h"

void
ieee80211_uhr_cap_ie_to_sta_uhr_cap(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_supported_band *sband,
				    const struct ieee80211_uhr_cap *uhr_cap,
				    u8 uhr_cap_len,
				    struct link_sta_info *link_sta)
{
	struct ieee80211_sta_uhr_cap *sta_uhr_cap = &link_sta->pub->uhr_cap;
	bool from_ap;

	memset(sta_uhr_cap, 0, sizeof(*sta_uhr_cap));

	if (!ieee80211_get_uhr_iftype_cap_vif(sband, &sdata->vif))
		return;

	sta_uhr_cap->has_uhr = true;

	sta_uhr_cap->mac = uhr_cap->mac;
	from_ap = sdata->vif.type == NL80211_IFTYPE_STATION;
	sta_uhr_cap->phy = *ieee80211_uhr_phy_cap(uhr_cap, from_ap);
}
