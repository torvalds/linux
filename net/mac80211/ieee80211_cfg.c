/*
 * mac80211 configuration hooks for cfg80211
 *
 * Copyright 2006	Johannes Berg <johannes@sipsolutions.net>
 *
 * This file is GPLv2 as found in COPYING.
 */

#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "ieee80211_cfg.h"

static int ieee80211_add_iface(struct wiphy *wiphy, char *name,
			       unsigned int type)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	int itype;

	if (unlikely(local->reg_state != IEEE80211_DEV_REGISTERED))
		return -ENODEV;

	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		itype = IEEE80211_IF_TYPE_STA;
		break;
	case NL80211_IFTYPE_ADHOC:
		itype = IEEE80211_IF_TYPE_IBSS;
		break;
	case NL80211_IFTYPE_STATION:
		itype = IEEE80211_IF_TYPE_STA;
		break;
	case NL80211_IFTYPE_MONITOR:
		itype = IEEE80211_IF_TYPE_MNTR;
		break;
	default:
		return -EINVAL;
	}

	return ieee80211_if_add(local->mdev, name, NULL, itype);
}

static int ieee80211_del_iface(struct wiphy *wiphy, int ifindex)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct net_device *dev;
	char *name;

	if (unlikely(local->reg_state != IEEE80211_DEV_REGISTERED))
		return -ENODEV;

	dev = dev_get_by_index(ifindex);
	if (!dev)
		return 0;

	name = dev->name;
	dev_put(dev);

	return ieee80211_if_remove(local->mdev, name, -1);
}

struct cfg80211_ops mac80211_config_ops = {
	.add_virtual_intf = ieee80211_add_iface,
	.del_virtual_intf = ieee80211_del_iface,
};
