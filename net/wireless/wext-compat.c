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
	int ret;

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

	if (type == wdev->iftype)
		return 0;

	memset(&vifparams, 0, sizeof(vifparams));

	ret = rdev->ops->change_virtual_intf(wdev->wiphy, dev->ifindex, type,
					     NULL, &vifparams);
	WARN_ON(!ret && wdev->iftype != type);

	return ret;
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


int cfg80211_wext_giwrange(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct iw_range *range = (struct iw_range *) extra;
	enum ieee80211_band band;
	int c = 0;

	if (!wdev)
		return -EOPNOTSUPP;

	data->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 21;
	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;
	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;
	range->num_encoding_sizes = 2;
	range->max_encoding_tokens = 4;

	range->max_qual.updated = IW_QUAL_NOISE_INVALID;

	switch (wdev->wiphy->signal_type) {
	case CFG80211_SIGNAL_TYPE_NONE:
		break;
	case CFG80211_SIGNAL_TYPE_MBM:
		range->max_qual.level = -110;
		range->max_qual.qual = 70;
		range->avg_qual.qual = 35;
		range->max_qual.updated |= IW_QUAL_DBM;
		range->max_qual.updated |= IW_QUAL_QUAL_UPDATED;
		range->max_qual.updated |= IW_QUAL_LEVEL_UPDATED;
		break;
	case CFG80211_SIGNAL_TYPE_UNSPEC:
		range->max_qual.level = 100;
		range->max_qual.qual = 100;
		range->avg_qual.qual = 50;
		range->max_qual.updated |= IW_QUAL_QUAL_UPDATED;
		range->max_qual.updated |= IW_QUAL_LEVEL_UPDATED;
		break;
	}

	range->avg_qual.level = range->max_qual.level / 2;
	range->avg_qual.noise = range->max_qual.noise / 2;
	range->avg_qual.updated = range->max_qual.updated;

	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;


	for (band = 0; band < IEEE80211_NUM_BANDS; band ++) {
		int i;
		struct ieee80211_supported_band *sband;

		sband = wdev->wiphy->bands[band];

		if (!sband)
			continue;

		for (i = 0; i < sband->n_channels && c < IW_MAX_FREQUENCIES; i++) {
			struct ieee80211_channel *chan = &sband->channels[i];

			if (!(chan->flags & IEEE80211_CHAN_DISABLED)) {
				range->freq[c].i =
					ieee80211_frequency_to_channel(
						chan->center_freq);
				range->freq[c].m = chan->center_freq;
				range->freq[c].e = 6;
				c++;
			}
		}
	}
	range->num_channels = c;
	range->num_frequency = c;

	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);

	range->scan_capa |= IW_SCAN_CAPA_ESSID;

	return 0;
}
EXPORT_SYMBOL(cfg80211_wext_giwrange);
