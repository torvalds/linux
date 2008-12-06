/*
 * cfg80211 - wext compat code
 *
 * This is temporary code until all wireless functionality is migrated
 * into cfg80211, when that happens all the exports here go away and
 * we directly assign the wireless handlers of wireless interfaces.
 *
 * Copyright 2008	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/wireless.h>
#include <linux/nl80211.h>
#include <net/iw_handler.h>
#include <net/wireless.h>
#include <net/cfg80211.h>
#include "core.h"

int cfg80211_wext_giwname(struct net_device *dev,
			  struct iw_request_info *info,
			  char *name, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct ieee80211_supported_band *sband;
	bool is_ht = false, is_a = false, is_b = false, is_g = false;

	if (!wdev)
		return -EOPNOTSUPP;

	sband = wdev->wiphy->bands[IEEE80211_BAND_5GHZ];
	if (sband) {
		is_a = true;
		is_ht |= sband->ht_cap.ht_supported;
	}

	sband = wdev->wiphy->bands[IEEE80211_BAND_2GHZ];
	if (sband) {
		int i;
		/* Check for mandatory rates */
		for (i = 0; i < sband->n_bitrates; i++) {
			if (sband->bitrates[i].bitrate == 10)
				is_b = true;
			if (sband->bitrates[i].bitrate == 60)
				is_g = true;
		}
		is_ht |= sband->ht_cap.ht_supported;
	}

	strcpy(name, "IEEE 802.11");
	if (is_a)
		strcat(name, "a");
	if (is_b)
		strcat(name, "b");
	if (is_g)
		strcat(name, "g");
	if (is_ht)
		strcat(name, "n");

	return 0;
}
EXPORT_SYMBOL(cfg80211_wext_giwname);

int cfg80211_wext_siwmode(struct net_device *dev, struct iw_request_info *info,
			  u32 *mode, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev;
	struct vif_params vifparams;
	enum nl80211_iftype type;

	if (!wdev)
		return -EOPNOTSUPP;

	rdev = wiphy_to_dev(wdev->wiphy);

	if (!rdev->ops->change_virtual_intf)
		return -EOPNOTSUPP;

	/* don't support changing VLANs, you just re-create them */
	if (wdev->iftype == NL80211_IFTYPE_AP_VLAN)
		return -EOPNOTSUPP;

	switch (*mode) {
	case IW_MODE_INFRA:
		type = NL80211_IFTYPE_STATION;
		break;
	case IW_MODE_ADHOC:
		type = NL80211_IFTYPE_ADHOC;
		break;
	case IW_MODE_REPEAT:
		type = NL80211_IFTYPE_WDS;
		break;
	case IW_MODE_MONITOR:
		type = NL80211_IFTYPE_MONITOR;
		break;
	default:
		return -EINVAL;
	}

	memset(&vifparams, 0, sizeof(vifparams));

	return rdev->ops->change_virtual_intf(wdev->wiphy, dev->ifindex, type,
					      NULL, &vifparams);
}
EXPORT_SYMBOL(cfg80211_wext_siwmode);

int cfg80211_wext_giwmode(struct net_device *dev, struct iw_request_info *info,
			  u32 *mode, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	if (!wdev)
		return -EOPNOTSUPP;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_AP:
		*mode = IW_MODE_MASTER;
		break;
	case NL80211_IFTYPE_STATION:
		*mode = IW_MODE_INFRA;
		break;
	case NL80211_IFTYPE_ADHOC:
		*mode = IW_MODE_ADHOC;
		break;
	case NL80211_IFTYPE_MONITOR:
		*mode = IW_MODE_MONITOR;
		break;
	case NL80211_IFTYPE_WDS:
		*mode = IW_MODE_REPEAT;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		*mode = IW_MODE_SECOND;		/* FIXME */
		break;
	default:
		*mode = IW_MODE_AUTO;
		break;
	}
	return 0;
}
EXPORT_SYMBOL(cfg80211_wext_giwmode);
