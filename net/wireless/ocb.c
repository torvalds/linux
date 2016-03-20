/*
 * OCB mode implementation
 *
 * Copyright: (c) 2014 Czech Technical University in Prague
 *            (c) 2014 Volkswagen Group Research
 * Author:    Rostislav Lisovy <rostislav.lisovy@fel.cvut.cz>
 * Funded by: Volkswagen Group Research
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include "nl80211.h"
#include "core.h"
#include "rdev-ops.h"

int __cfg80211_join_ocb(struct cfg80211_registered_device *rdev,
			struct net_device *dev,
			struct ocb_setup *setup)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_OCB)
		return -EOPNOTSUPP;

	if (!rdev->ops->join_ocb)
		return -EOPNOTSUPP;

	if (WARN_ON(!setup->chandef.chan))
		return -EINVAL;

	err = rdev_join_ocb(rdev, dev, setup);
	if (!err)
		wdev->chandef = setup->chandef;

	return err;
}

int cfg80211_join_ocb(struct cfg80211_registered_device *rdev,
		      struct net_device *dev,
		      struct ocb_setup *setup)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_join_ocb(rdev, dev, setup);
	wdev_unlock(wdev);

	return err;
}

int __cfg80211_leave_ocb(struct cfg80211_registered_device *rdev,
			 struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_OCB)
		return -EOPNOTSUPP;

	if (!rdev->ops->leave_ocb)
		return -EOPNOTSUPP;

	err = rdev_leave_ocb(rdev, dev);
	if (!err)
		memset(&wdev->chandef, 0, sizeof(wdev->chandef));

	return err;
}

int cfg80211_leave_ocb(struct cfg80211_registered_device *rdev,
		       struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_leave_ocb(rdev, dev);
	wdev_unlock(wdev);

	return err;
}
