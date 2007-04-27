/*
 * Wireless configuration interface internals.
 *
 * Copyright 2006, 2007 Johannes Berg <johannes@sipsolutions.net>
 */
#ifndef __NET_WIRELESS_CORE_H
#define __NET_WIRELESS_CORE_H
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <net/genetlink.h>
#include <net/wireless.h>
#include <net/cfg80211.h>

struct cfg80211_registered_device {
	struct cfg80211_ops *ops;
	struct list_head list;
	/* we hold this mutex during any call so that
	 * we cannot do multiple calls at once, and also
	 * to avoid the deregister call to proceed while
	 * any call is in progress */
	struct mutex mtx;

	/* wiphy index, internal only */
	int idx;

	/* associate netdev list */
	struct mutex devlist_mtx;
	struct list_head netdev_list;

	/* must be last because of the way we do wiphy_priv(),
	 * and it should at least be aligned to NETDEV_ALIGN */
	struct wiphy wiphy __attribute__((__aligned__(NETDEV_ALIGN)));
};

static inline
struct cfg80211_registered_device *wiphy_to_dev(struct wiphy *wiphy)
{
	BUG_ON(!wiphy);
	return container_of(wiphy, struct cfg80211_registered_device, wiphy);
}

extern struct mutex cfg80211_drv_mutex;
extern struct list_head cfg80211_drv_list;

/* free object */
extern void cfg80211_dev_free(struct cfg80211_registered_device *drv);

#endif /* __NET_WIRELESS_CORE_H */
