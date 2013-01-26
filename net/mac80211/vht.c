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


void ieee80211_vht_cap_ie_to_sta_vht_cap(struct ieee80211_sub_if_data *sdata,
					 struct ieee80211_supported_band *sband,
					 struct ieee80211_vht_cap *vht_cap_ie,
					 struct ieee80211_sta_vht_cap *vht_cap)
{
	if (WARN_ON_ONCE(!vht_cap))
		return;

	memset(vht_cap, 0, sizeof(*vht_cap));

	if (!vht_cap_ie || !sband->vht_cap.vht_supported)
		return;

	vht_cap->vht_supported = true;

	vht_cap->cap = le32_to_cpu(vht_cap_ie->vht_cap_info);

	/* Copy peer MCS info, the driver might need them. */
	memcpy(&vht_cap->vht_mcs, &vht_cap_ie->supp_mcs,
	       sizeof(struct ieee80211_vht_mcs_info));
}
