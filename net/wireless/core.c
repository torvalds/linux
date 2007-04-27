/*
 * This is the linux wireless configuration interface.
 *
 * Copyright 2006, 2007		Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/if.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/nl80211.h>
#include <linux/debugfs.h>
#include <linux/notifier.h>
#include <linux/device.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include <net/wireless.h>
#include "core.h"
#include "sysfs.h"

/* name for sysfs, %d is appended */
#define PHY_NAME "phy"

MODULE_AUTHOR("Johannes Berg");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("wireless configuration support");

/* RCU might be appropriate here since we usually
 * only read the list, and that can happen quite
 * often because we need to do it for each command */
LIST_HEAD(cfg80211_drv_list);
DEFINE_MUTEX(cfg80211_drv_mutex);
static int wiphy_counter;

/* for debugfs */
static struct dentry *ieee80211_debugfs_dir;

/* exported functions */

struct wiphy *wiphy_new(struct cfg80211_ops *ops, int sizeof_priv)
{
	struct cfg80211_registered_device *drv;
	int alloc_size;

	alloc_size = sizeof(*drv) + sizeof_priv;

	drv = kzalloc(alloc_size, GFP_KERNEL);
	if (!drv)
		return NULL;

	drv->ops = ops;

	mutex_lock(&cfg80211_drv_mutex);

	drv->idx = wiphy_counter;

	/* now increase counter for the next device unless
	 * it has wrapped previously */
	if (wiphy_counter >= 0)
		wiphy_counter++;

	mutex_unlock(&cfg80211_drv_mutex);

	if (unlikely(drv->idx < 0)) {
		/* ugh, wrapped! */
		kfree(drv);
		return NULL;
	}

	/* give it a proper name */
	snprintf(drv->wiphy.dev.bus_id, BUS_ID_SIZE,
		 PHY_NAME "%d", drv->idx);

	mutex_init(&drv->mtx);
	mutex_init(&drv->devlist_mtx);
	INIT_LIST_HEAD(&drv->netdev_list);

	device_initialize(&drv->wiphy.dev);
	drv->wiphy.dev.class = &ieee80211_class;
	drv->wiphy.dev.platform_data = drv;

	return &drv->wiphy;
}
EXPORT_SYMBOL(wiphy_new);

int wiphy_register(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *drv = wiphy_to_dev(wiphy);
	int res;

	mutex_lock(&cfg80211_drv_mutex);

	res = device_add(&drv->wiphy.dev);
	if (res)
		goto out_unlock;

	list_add(&drv->list, &cfg80211_drv_list);

	/* add to debugfs */
	drv->wiphy.debugfsdir =
		debugfs_create_dir(wiphy_name(&drv->wiphy),
				   ieee80211_debugfs_dir);

	res = 0;
out_unlock:
	mutex_unlock(&cfg80211_drv_mutex);
	return res;
}
EXPORT_SYMBOL(wiphy_register);

void wiphy_unregister(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *drv = wiphy_to_dev(wiphy);

	/* protect the device list */
	mutex_lock(&cfg80211_drv_mutex);

	BUG_ON(!list_empty(&drv->netdev_list));

	/*
	 * Try to grab drv->mtx. If a command is still in progress,
	 * hopefully the driver will refuse it since it's tearing
	 * down the device already. We wait for this command to complete
	 * before unlinking the item from the list.
	 * Note: as codified by the BUG_ON above we cannot get here if
	 * a virtual interface is still associated. Hence, we can only
	 * get to lock contention here if userspace issues a command
	 * that identified the hardware by wiphy index.
	 */
	mutex_lock(&drv->mtx);
	/* unlock again before freeing */
	mutex_unlock(&drv->mtx);

	list_del(&drv->list);
	device_del(&drv->wiphy.dev);
	debugfs_remove(drv->wiphy.debugfsdir);

	mutex_unlock(&cfg80211_drv_mutex);
}
EXPORT_SYMBOL(wiphy_unregister);

void cfg80211_dev_free(struct cfg80211_registered_device *drv)
{
	mutex_destroy(&drv->mtx);
	mutex_destroy(&drv->devlist_mtx);
	kfree(drv);
}

void wiphy_free(struct wiphy *wiphy)
{
	put_device(&wiphy->dev);
}
EXPORT_SYMBOL(wiphy_free);

static int cfg80211_netdev_notifier_call(struct notifier_block * nb,
					 unsigned long state,
					 void *ndev)
{
	struct net_device *dev = ndev;
	struct cfg80211_registered_device *rdev;

	if (!dev->ieee80211_ptr)
		return 0;

	rdev = wiphy_to_dev(dev->ieee80211_ptr->wiphy);

	switch (state) {
	case NETDEV_REGISTER:
		mutex_lock(&rdev->devlist_mtx);
		list_add(&dev->ieee80211_ptr->list, &rdev->netdev_list);
		if (sysfs_create_link(&dev->dev.kobj, &rdev->wiphy.dev.kobj,
				      "phy80211")) {
			printk(KERN_ERR "wireless: failed to add phy80211 "
				"symlink to netdev!\n");
		}
		dev->ieee80211_ptr->netdev = dev;
		mutex_unlock(&rdev->devlist_mtx);
		break;
	case NETDEV_UNREGISTER:
		mutex_lock(&rdev->devlist_mtx);
		if (!list_empty(&dev->ieee80211_ptr->list)) {
			sysfs_remove_link(&dev->dev.kobj, "phy80211");
			list_del_init(&dev->ieee80211_ptr->list);
		}
		mutex_unlock(&rdev->devlist_mtx);
		break;
	}

	return 0;
}

static struct notifier_block cfg80211_netdev_notifier = {
	.notifier_call = cfg80211_netdev_notifier_call,
};

static int cfg80211_init(void)
{
	int err = wiphy_sysfs_init();
	if (err)
		goto out_fail_sysfs;

	err = register_netdevice_notifier(&cfg80211_netdev_notifier);
	if (err)
		goto out_fail_notifier;

	ieee80211_debugfs_dir = debugfs_create_dir("ieee80211", NULL);

	return 0;

out_fail_notifier:
	wiphy_sysfs_exit();
out_fail_sysfs:
	return err;
}
module_init(cfg80211_init);

static void cfg80211_exit(void)
{
	debugfs_remove(ieee80211_debugfs_dir);
	unregister_netdevice_notifier(&cfg80211_netdev_notifier);
	wiphy_sysfs_exit();
}
module_exit(cfg80211_exit);
