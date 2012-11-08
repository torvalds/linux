/*
 * This file contains helper code to handle channel
 * settings and keeping track of what is possible at
 * any point in time.
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/export.h>
#include <net/cfg80211.h>
#include "core.h"
#include "rdev-ops.h"

bool cfg80211_reg_can_beacon(struct wiphy *wiphy,
			     struct cfg80211_chan_def *chandef)
{
	struct ieee80211_channel *sec_chan;
	int diff;

	trace_cfg80211_reg_can_beacon(wiphy, chandef);

	switch (chandef->_type) {
	case NL80211_CHAN_HT40PLUS:
		diff = 20;
		break;
	case NL80211_CHAN_HT40MINUS:
		diff = -20;
		break;
	default:
		trace_cfg80211_return_bool(true);
		return true;
	}

	sec_chan = ieee80211_get_channel(wiphy,
					 chandef->chan->center_freq + diff);
	if (!sec_chan) {
		trace_cfg80211_return_bool(false);
		return false;
	}

	/* we'll need a DFS capability later */
	if (sec_chan->flags & (IEEE80211_CHAN_DISABLED |
			       IEEE80211_CHAN_PASSIVE_SCAN |
			       IEEE80211_CHAN_NO_IBSS |
			       IEEE80211_CHAN_RADAR)) {
		trace_cfg80211_return_bool(false);
		return false;
	}
	trace_cfg80211_return_bool(true);
	return true;
}
EXPORT_SYMBOL(cfg80211_reg_can_beacon);

int cfg80211_set_monitor_channel(struct cfg80211_registered_device *rdev,
				 struct cfg80211_chan_def *chandef)
{
	if (!rdev->ops->set_monitor_channel)
		return -EOPNOTSUPP;
	if (!cfg80211_has_monitors_only(rdev))
		return -EBUSY;

	return rdev_set_monitor_channel(rdev, chandef);
}

void
cfg80211_get_chan_state(struct wireless_dev *wdev,
		        struct ieee80211_channel **chan,
		        enum cfg80211_chan_mode *chanmode)
{
	*chan = NULL;
	*chanmode = CHAN_MODE_UNDEFINED;

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->netdev && !netif_running(wdev->netdev))
		return;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_ADHOC:
		if (wdev->current_bss) {
			*chan = wdev->current_bss->pub.channel;
			*chanmode = wdev->ibss_fixed
				  ? CHAN_MODE_SHARED
				  : CHAN_MODE_EXCLUSIVE;
			return;
		}
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (wdev->current_bss) {
			*chan = wdev->current_bss->pub.channel;
			*chanmode = CHAN_MODE_SHARED;
			return;
		}
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		if (wdev->beacon_interval) {
			*chan = wdev->channel;
			*chanmode = CHAN_MODE_SHARED;
		}
		return;
	case NL80211_IFTYPE_MESH_POINT:
		if (wdev->mesh_id_len) {
			*chan = wdev->channel;
			*chanmode = CHAN_MODE_SHARED;
		}
		return;
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
		/* these interface types don't really have a channel */
		return;
	case NL80211_IFTYPE_P2P_DEVICE:
		if (wdev->wiphy->features &
				NL80211_FEATURE_P2P_DEVICE_NEEDS_CHANNEL)
			*chanmode = CHAN_MODE_EXCLUSIVE;
		return;
	case NL80211_IFTYPE_UNSPECIFIED:
	case NUM_NL80211_IFTYPES:
		WARN_ON(1);
	}

	return;
}
