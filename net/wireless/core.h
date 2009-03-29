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
#include <linux/kref.h>
#include <linux/rbtree.h>
#include <linux/mutex.h>
#include <net/genetlink.h>
#include <net/wireless.h>
#include <net/cfg80211.h>
#include "reg.h"

struct cfg80211_registered_device {
	struct cfg80211_ops *ops;
	struct list_head list;
	/* we hold this mutex during any call so that
	 * we cannot do multiple calls at once, and also
	 * to avoid the deregister call to proceed while
	 * any call is in progress */
	struct mutex mtx;

	/* ISO / IEC 3166 alpha2 for which this device is receiving
	 * country IEs on, this can help disregard country IEs from APs
	 * on the same alpha2 quickly. The alpha2 may differ from
	 * cfg80211_regdomain's alpha2 when an intersection has occurred.
	 * If the AP is reconfigured this can also be used to tell us if
	 * the country on the country IE changed. */
	char country_ie_alpha2[2];

	/* If a Country IE has been received this tells us the environment
	 * which its telling us its in. This defaults to ENVIRON_ANY */
	enum environment_cap env;

	/* wiphy index, internal only */
	int wiphy_idx;

	/* associate netdev list */
	struct mutex devlist_mtx;
	struct list_head netdev_list;

	/* BSSes/scanning */
	spinlock_t bss_lock;
	struct list_head bss_list;
	struct rb_root bss_tree;
	u32 bss_generation;
	struct cfg80211_scan_request *scan_req; /* protected by RTNL */
	unsigned long suspend_at;

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

/* Note 0 is valid, hence phy0 */
static inline
bool wiphy_idx_valid(int wiphy_idx)
{
	return (wiphy_idx >= 0);
}

extern struct mutex cfg80211_mutex;
extern struct list_head cfg80211_drv_list;

static inline void assert_cfg80211_lock(void)
{
	WARN_ON(!mutex_is_locked(&cfg80211_mutex));
}

/*
 * You can use this to mark a wiphy_idx as not having an associated wiphy.
 * It guarantees cfg80211_drv_by_wiphy_idx(wiphy_idx) will return NULL
 */
#define WIPHY_IDX_STALE -1

struct cfg80211_internal_bss {
	struct list_head list;
	struct rb_node rbn;
	unsigned long ts;
	struct kref ref;
	bool hold;

	/* must be last because of priv member */
	struct cfg80211_bss pub;
};

struct cfg80211_registered_device *cfg80211_drv_by_wiphy_idx(int wiphy_idx);
int get_wiphy_idx(struct wiphy *wiphy);

struct cfg80211_registered_device *
__cfg80211_drv_from_info(struct genl_info *info);

/*
 * This function returns a pointer to the driver
 * that the genl_info item that is passed refers to.
 * If successful, it returns non-NULL and also locks
 * the driver's mutex!
 *
 * This means that you need to call cfg80211_put_dev()
 * before being allowed to acquire &cfg80211_mutex!
 *
 * This is necessary because we need to lock the global
 * mutex to get an item off the list safely, and then
 * we lock the drv mutex so it doesn't go away under us.
 *
 * We don't want to keep cfg80211_mutex locked
 * for all the time in order to allow requests on
 * other interfaces to go through at the same time.
 *
 * The result of this can be a PTR_ERR and hence must
 * be checked with IS_ERR() for errors.
 */
extern struct cfg80211_registered_device *
cfg80211_get_dev_from_info(struct genl_info *info);

/* requires cfg80211_drv_mutex to be held! */
struct wiphy *wiphy_idx_to_wiphy(int wiphy_idx);

/* identical to cfg80211_get_dev_from_info but only operate on ifindex */
extern struct cfg80211_registered_device *
cfg80211_get_dev_from_ifindex(int ifindex);

extern void cfg80211_put_dev(struct cfg80211_registered_device *drv);

/* free object */
extern void cfg80211_dev_free(struct cfg80211_registered_device *drv);

extern int cfg80211_dev_rename(struct cfg80211_registered_device *drv,
			       char *newname);

void ieee80211_set_bitrate_flags(struct wiphy *wiphy);
void wiphy_update_regulatory(struct wiphy *wiphy,
			     enum nl80211_reg_initiator setby);

void cfg80211_bss_expire(struct cfg80211_registered_device *dev);
void cfg80211_bss_age(struct cfg80211_registered_device *dev,
                      unsigned long age_secs);

#endif /* __NET_WIRELESS_CORE_H */
