#ifndef __NET_WIRELESS_H
#define __NET_WIRELESS_H

/*
 * 802.11 device management
 *
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <net/cfg80211.h>

/**
 * struct wiphy - wireless hardware description
 * @idx: the wiphy index assigned to this item
 * @class_dev: the class device representing /sys/class/ieee80211/<wiphy-name>
 */
struct wiphy {
	/* assign these fields before you register the wiphy */

	/* permanent MAC address */
	u8 perm_addr[ETH_ALEN];

	/* If multiple wiphys are registered and you're handed e.g.
	 * a regular netdev with assigned ieee80211_ptr, you won't
	 * know whether it points to a wiphy your driver has registered
	 * or not. Assign this to something global to your driver to
	 * help determine whether you own this wiphy or not. */
	void *privid;

	/* fields below are read-only, assigned by cfg80211 */

	/* the item in /sys/class/ieee80211/ points to this,
	 * you need use set_wiphy_dev() (see below) */
	struct device dev;

	/* dir in debugfs: ieee80211/<wiphyname> */
	struct dentry *debugfsdir;

	char priv[0] __attribute__((__aligned__(NETDEV_ALIGN)));
};

/** struct wireless_dev - wireless per-netdev state
 *
 * This structure must be allocated by the driver/stack
 * that uses the ieee80211_ptr field in struct net_device
 * (this is intentional so it can be allocated along with
 * the netdev.)
 *
 * @wiphy: pointer to hardware description
 */
struct wireless_dev {
	struct wiphy *wiphy;

	/* private to the generic wireless code */
	struct list_head list;
	struct net_device *netdev;
};

/**
 * wiphy_priv - return priv from wiphy
 */
static inline void *wiphy_priv(struct wiphy *wiphy)
{
	BUG_ON(!wiphy);
	return &wiphy->priv;
}

/**
 * set_wiphy_dev - set device pointer for wiphy
 */
static inline void set_wiphy_dev(struct wiphy *wiphy, struct device *dev)
{
	wiphy->dev.parent = dev;
}

/**
 * wiphy_dev - get wiphy dev pointer
 */
static inline struct device *wiphy_dev(struct wiphy *wiphy)
{
	return wiphy->dev.parent;
}

/**
 * wiphy_name - get wiphy name
 */
static inline char *wiphy_name(struct wiphy *wiphy)
{
	return wiphy->dev.bus_id;
}

/**
 * wdev_priv - return wiphy priv from wireless_dev
 */
static inline void *wdev_priv(struct wireless_dev *wdev)
{
	BUG_ON(!wdev);
	return wiphy_priv(wdev->wiphy);
}

/**
 * wiphy_new - create a new wiphy for use with cfg80211
 *
 * create a new wiphy and associate the given operations with it.
 * @sizeof_priv bytes are allocated for private use.
 *
 * the returned pointer must be assigned to each netdev's
 * ieee80211_ptr for proper operation.
 */
struct wiphy *wiphy_new(struct cfg80211_ops *ops, int sizeof_priv);

/**
 * wiphy_register - register a wiphy with cfg80211
 *
 * register the given wiphy
 *
 * Returns a non-negative wiphy index or a negative error code.
 */
extern int wiphy_register(struct wiphy *wiphy);

/**
 * wiphy_unregister - deregister a wiphy from cfg80211
 *
 * unregister a device with the given priv pointer.
 * After this call, no more requests can be made with this priv
 * pointer, but the call may sleep to wait for an outstanding
 * request that is being handled.
 */
extern void wiphy_unregister(struct wiphy *wiphy);

/**
 * wiphy_free - free wiphy
 */
extern void wiphy_free(struct wiphy *wiphy);

#endif /* __NET_WIRELESS_H */
