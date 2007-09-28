/*
 * mac80211 configuration hooks for cfg80211
 *
 * Copyright 2006	Johannes Berg <johannes@sipsolutions.net>
 *
 * This file is GPLv2 as found in COPYING.
 */

#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "cfg.h"

static enum ieee80211_if_types
nl80211_type_to_mac80211_type(enum nl80211_iftype type)
{
	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		return IEEE80211_IF_TYPE_STA;
	case NL80211_IFTYPE_ADHOC:
		return IEEE80211_IF_TYPE_IBSS;
	case NL80211_IFTYPE_STATION:
		return IEEE80211_IF_TYPE_STA;
	case NL80211_IFTYPE_MONITOR:
		return IEEE80211_IF_TYPE_MNTR;
	default:
		return IEEE80211_IF_TYPE_INVALID;
	}
}

static int ieee80211_add_iface(struct wiphy *wiphy, char *name,
			       enum nl80211_iftype type)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	enum ieee80211_if_types itype;

	if (unlikely(local->reg_state != IEEE80211_DEV_REGISTERED))
		return -ENODEV;

	itype = nl80211_type_to_mac80211_type(type);
	if (itype == IEEE80211_IF_TYPE_INVALID)
		return -EINVAL;

	return ieee80211_if_add(local->mdev, name, NULL, itype);
}

static int ieee80211_del_iface(struct wiphy *wiphy, int ifindex)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct net_device *dev;
	char *name;

	if (unlikely(local->reg_state != IEEE80211_DEV_REGISTERED))
		return -ENODEV;

	/* we're under RTNL */
	dev = __dev_get_by_index(&init_net, ifindex);
	if (!dev)
		return 0;

	name = dev->name;

	return ieee80211_if_remove(local->mdev, name, -1);
}

static int ieee80211_change_iface(struct wiphy *wiphy, int ifindex,
				  enum nl80211_iftype type)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct net_device *dev;
	enum ieee80211_if_types itype;
	struct ieee80211_sub_if_data *sdata;

	if (unlikely(local->reg_state != IEEE80211_DEV_REGISTERED))
		return -ENODEV;

	/* we're under RTNL */
	dev = __dev_get_by_index(&init_net, ifindex);
	if (!dev)
		return -ENODEV;

	if (netif_running(dev))
		return -EBUSY;

	itype = nl80211_type_to_mac80211_type(type);
	if (itype == IEEE80211_IF_TYPE_INVALID)
		return -EINVAL;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

        if (sdata->type == IEEE80211_IF_TYPE_VLAN)
		return -EOPNOTSUPP;

	ieee80211_if_reinit(dev);
	ieee80211_if_set_type(dev, itype);

	return 0;
}

struct cfg80211_ops mac80211_config_ops = {
	.add_virtual_intf = ieee80211_add_iface,
	.del_virtual_intf = ieee80211_del_iface,
	.change_virtual_intf = ieee80211_change_iface,
};
