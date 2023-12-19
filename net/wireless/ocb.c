// SPDX-License-Identifier: GPL-2.0-only
/*
 * OCB mode implementation
 *
 * Copyright: (c) 2014 Czech Technical University in Prague
 *            (c) 2014 Volkswagen Group Research
 * Copyright (C) 2022-2023 Intel Corporation
 * Author:    Rostislav Lisovy <rostislav.lisovy@fel.cvut.cz>
 * Funded by: Volkswagen Group Research
 */

#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include "nl80211.h"
#include "core.h"
#include "rdev-ops.h"

int cfg80211_join_ocb(struct cfg80211_registered_device *rdev,
		      struct net_device *dev,
		      struct ocb_setup *setup)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	lockdep_assert_wiphy(wdev->wiphy);

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_OCB)
		return -EOPNOTSUPP;

	if (!rdev->ops->join_ocb)
		return -EOPNOTSUPP;

	if (WARN_ON(!setup->chandef.chan))
		return -EINVAL;

	err = rdev_join_ocb(rdev, dev, setup);
	if (!err)
		wdev->u.ocb.chandef = setup->chandef;

	return err;
}

int cfg80211_leave_ocb(struct cfg80211_registered_device *rdev,
		       struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	lockdep_assert_wiphy(wdev->wiphy);

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_OCB)
		return -EOPNOTSUPP;

	if (!rdev->ops->leave_ocb)
		return -EOPNOTSUPP;

	if (!wdev->u.ocb.chandef.chan)
		return -ENOTCONN;

	err = rdev_leave_ocb(rdev, dev);
	if (!err)
		memset(&wdev->u.ocb.chandef, 0, sizeof(wdev->u.ocb.chandef));

	return err;
}
