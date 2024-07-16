// SPDX-License-Identifier: GPL-2.0
/*
 * Parts of this file are
 * Copyright (C) 2022 Intel Corporation
 */
#include <linux/ieee80211.h>
#include <linux/export.h>
#include <net/cfg80211.h>
#include "nl80211.h"
#include "core.h"
#include "rdev-ops.h"


static int ___cfg80211_stop_ap(struct cfg80211_registered_device *rdev,
			       struct net_device *dev, unsigned int link_id,
			       bool notify)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (!rdev->ops->stop_ap)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
		return -EOPNOTSUPP;

	if (!wdev->links[link_id].ap.beacon_interval)
		return -ENOENT;

	err = rdev_stop_ap(rdev, dev, link_id);
	if (!err) {
		wdev->conn_owner_nlportid = 0;
		wdev->links[link_id].ap.beacon_interval = 0;
		memset(&wdev->links[link_id].ap.chandef, 0,
		       sizeof(wdev->links[link_id].ap.chandef));
		wdev->u.ap.ssid_len = 0;
		rdev_set_qos_map(rdev, dev, NULL);
		if (notify)
			nl80211_send_ap_stopped(wdev, link_id);

		/* Should we apply the grace period during beaconing interface
		 * shutdown also?
		 */
		cfg80211_sched_dfs_chan_update(rdev);
	}

	schedule_work(&cfg80211_disconnect_work);

	return err;
}

int __cfg80211_stop_ap(struct cfg80211_registered_device *rdev,
		       struct net_device *dev, int link_id,
		       bool notify)
{
	unsigned int link;
	int ret = 0;

	if (link_id >= 0)
		return ___cfg80211_stop_ap(rdev, dev, link_id, notify);

	for_each_valid_link(dev->ieee80211_ptr, link) {
		int ret1 = ___cfg80211_stop_ap(rdev, dev, link, notify);

		if (ret1)
			ret = ret1;
		/* try the next one also if one errored */
	}

	return ret;
}

int cfg80211_stop_ap(struct cfg80211_registered_device *rdev,
		     struct net_device *dev, int link_id,
		     bool notify)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_stop_ap(rdev, dev, link_id, notify);
	wdev_unlock(wdev);

	return err;
}
