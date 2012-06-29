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

struct ieee80211_channel *
rdev_freq_to_chan(struct cfg80211_registered_device *rdev,
		  int freq, enum nl80211_channel_type channel_type)
{
	struct ieee80211_channel *chan;
	struct ieee80211_sta_ht_cap *ht_cap;

	chan = ieee80211_get_channel(&rdev->wiphy, freq);

	/* Primary channel not allowed */
	if (!chan || chan->flags & IEEE80211_CHAN_DISABLED)
		return NULL;

	if (channel_type == NL80211_CHAN_HT40MINUS &&
	    chan->flags & IEEE80211_CHAN_NO_HT40MINUS)
		return NULL;
	else if (channel_type == NL80211_CHAN_HT40PLUS &&
		 chan->flags & IEEE80211_CHAN_NO_HT40PLUS)
		return NULL;

	ht_cap = &rdev->wiphy.bands[chan->band]->ht_cap;

	if (channel_type != NL80211_CHAN_NO_HT) {
		if (!ht_cap->ht_supported)
			return NULL;

		if (channel_type != NL80211_CHAN_HT20 &&
		    (!(ht_cap->cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) ||
		    ht_cap->cap & IEEE80211_HT_CAP_40MHZ_INTOLERANT))
			return NULL;
	}

	return chan;
}

bool cfg80211_can_beacon_sec_chan(struct wiphy *wiphy,
				  struct ieee80211_channel *chan,
				  enum nl80211_channel_type channel_type)
{
	struct ieee80211_channel *sec_chan;
	int diff;

	switch (channel_type) {
	case NL80211_CHAN_HT40PLUS:
		diff = 20;
		break;
	case NL80211_CHAN_HT40MINUS:
		diff = -20;
		break;
	default:
		return true;
	}

	sec_chan = ieee80211_get_channel(wiphy, chan->center_freq + diff);
	if (!sec_chan)
		return false;

	/* we'll need a DFS capability later */
	if (sec_chan->flags & (IEEE80211_CHAN_DISABLED |
			       IEEE80211_CHAN_PASSIVE_SCAN |
			       IEEE80211_CHAN_NO_IBSS |
			       IEEE80211_CHAN_RADAR))
		return false;

	return true;
}
EXPORT_SYMBOL(cfg80211_can_beacon_sec_chan);

int cfg80211_set_monitor_channel(struct cfg80211_registered_device *rdev,
				 int freq, enum nl80211_channel_type chantype)
{
	struct ieee80211_channel *chan;

	if (!rdev->ops->set_monitor_channel)
		return -EOPNOTSUPP;
	if (!cfg80211_has_monitors_only(rdev))
		return -EBUSY;

	chan = rdev_freq_to_chan(rdev, freq, chantype);
	if (!chan)
		return -EINVAL;

	return rdev->ops->set_monitor_channel(&rdev->wiphy, chan, chantype);
}

void
cfg80211_get_chan_state(struct cfg80211_registered_device *rdev,
		        struct wireless_dev *wdev,
		        struct ieee80211_channel **chan,
		        enum cfg80211_chan_mode *chanmode)
{
	*chan = NULL;
	*chanmode = CHAN_MODE_UNDEFINED;

	ASSERT_RDEV_LOCK(rdev);
	ASSERT_WDEV_LOCK(wdev);

	if (!netif_running(wdev->netdev))
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
	case NL80211_IFTYPE_MESH_POINT:
		*chan = wdev->channel;
		*chanmode = CHAN_MODE_SHARED;
		return;
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
		/* these interface types don't really have a channel */
		return;
	case NL80211_IFTYPE_UNSPECIFIED:
	case NUM_NL80211_IFTYPES:
		WARN_ON(1);
	}

	return;
}
