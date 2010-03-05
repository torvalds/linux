/*
 * This file contains helper code to handle channel
 * settings and keeping track of what is possible at
 * any point in time.
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 */

#include <net/cfg80211.h>
#include "core.h"

struct ieee80211_channel *
rdev_fixed_channel(struct cfg80211_registered_device *rdev,
		   struct wireless_dev *for_wdev)
{
	struct wireless_dev *wdev;
	struct ieee80211_channel *result = NULL;

	WARN_ON(!mutex_is_locked(&rdev->devlist_mtx));

	list_for_each_entry(wdev, &rdev->netdev_list, list) {
		if (wdev == for_wdev)
			continue;

		/*
		 * Lock manually to tell lockdep about allowed
		 * nesting here if for_wdev->mtx is held already.
		 * This is ok as it's all under the rdev devlist
		 * mutex and as such can only be done once at any
		 * given time.
		 */
		mutex_lock_nested(&wdev->mtx, SINGLE_DEPTH_NESTING);
		if (wdev->current_bss)
			result = wdev->current_bss->pub.channel;
		wdev_unlock(wdev);

		if (result)
			break;
	}

	return result;
}

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

		if (!(ht_cap->cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) ||
		    ht_cap->cap & IEEE80211_HT_CAP_40MHZ_INTOLERANT)
			return NULL;
	}

	return chan;
}

int rdev_set_freq(struct cfg80211_registered_device *rdev,
		  struct wireless_dev *for_wdev,
		  int freq, enum nl80211_channel_type channel_type)
{
	struct ieee80211_channel *chan;
	int result;

	if (rdev_fixed_channel(rdev, for_wdev))
		return -EBUSY;

	if (!rdev->ops->set_channel)
		return -EOPNOTSUPP;

	chan = rdev_freq_to_chan(rdev, freq, channel_type);
	if (!chan)
		return -EINVAL;

	result = rdev->ops->set_channel(&rdev->wiphy, chan, channel_type);
	if (result)
		return result;

	rdev->channel = chan;

	return 0;
}
