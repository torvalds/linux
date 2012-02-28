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

int cfg80211_can_beacon_sec_chan(struct wiphy *wiphy,
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
		return false;
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

int cfg80211_set_freq(struct cfg80211_registered_device *rdev,
		      struct wireless_dev *wdev, int freq,
		      enum nl80211_channel_type channel_type)
{
	struct ieee80211_channel *chan;
	int result;

	if (wdev && wdev->iftype == NL80211_IFTYPE_MONITOR)
		wdev = NULL;

	if (wdev) {
		ASSERT_WDEV_LOCK(wdev);

		if (!netif_running(wdev->netdev))
			return -ENETDOWN;
	}

	if (!rdev->ops->set_channel)
		return -EOPNOTSUPP;

	chan = rdev_freq_to_chan(rdev, freq, channel_type);
	if (!chan)
		return -EINVAL;

	/* Both channels should be able to initiate communication */
	if (wdev && (wdev->iftype == NL80211_IFTYPE_ADHOC ||
		     wdev->iftype == NL80211_IFTYPE_AP ||
		     wdev->iftype == NL80211_IFTYPE_AP_VLAN ||
		     wdev->iftype == NL80211_IFTYPE_MESH_POINT ||
		     wdev->iftype == NL80211_IFTYPE_P2P_GO)) {
		switch (channel_type) {
		case NL80211_CHAN_HT40PLUS:
		case NL80211_CHAN_HT40MINUS:
			if (!cfg80211_can_beacon_sec_chan(&rdev->wiphy, chan,
							  channel_type)) {
				printk(KERN_DEBUG
				       "cfg80211: Secondary channel not "
				       "allowed to initiate communication\n");
				return -EINVAL;
			}
			break;
		default:
			break;
		}
	}

	result = rdev->ops->set_channel(&rdev->wiphy,
					wdev ? wdev->netdev : NULL,
					chan, channel_type);
	if (result)
		return result;

	if (wdev)
		wdev->channel = chan;

	return 0;
}
