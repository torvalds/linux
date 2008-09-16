/*
 * This is the linux wireless configuration interface.
 *
 * Copyright 2006-2008		Johannes Berg <johannes@sipsolutions.net>
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
#include <linux/list.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include <net/wireless.h>
#include "nl80211.h"
#include "core.h"
#include "sysfs.h"
#include "reg.h"

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

/* requires cfg80211_drv_mutex to be held! */
static struct cfg80211_registered_device *cfg80211_drv_by_wiphy(int wiphy)
{
	struct cfg80211_registered_device *result = NULL, *drv;

	list_for_each_entry(drv, &cfg80211_drv_list, list) {
		if (drv->idx == wiphy) {
			result = drv;
			break;
		}
	}

	return result;
}

/* requires cfg80211_drv_mutex to be held! */
static struct cfg80211_registered_device *
__cfg80211_drv_from_info(struct genl_info *info)
{
	int ifindex;
	struct cfg80211_registered_device *bywiphy = NULL, *byifidx = NULL;
	struct net_device *dev;
	int err = -EINVAL;

	if (info->attrs[NL80211_ATTR_WIPHY]) {
		bywiphy = cfg80211_drv_by_wiphy(
				nla_get_u32(info->attrs[NL80211_ATTR_WIPHY]));
		err = -ENODEV;
	}

	if (info->attrs[NL80211_ATTR_IFINDEX]) {
		ifindex = nla_get_u32(info->attrs[NL80211_ATTR_IFINDEX]);
		dev = dev_get_by_index(&init_net, ifindex);
		if (dev) {
			if (dev->ieee80211_ptr)
				byifidx =
					wiphy_to_dev(dev->ieee80211_ptr->wiphy);
			dev_put(dev);
		}
		err = -ENODEV;
	}

	if (bywiphy && byifidx) {
		if (bywiphy != byifidx)
			return ERR_PTR(-EINVAL);
		else
			return bywiphy; /* == byifidx */
	}
	if (bywiphy)
		return bywiphy;

	if (byifidx)
		return byifidx;

	return ERR_PTR(err);
}

struct cfg80211_registered_device *
cfg80211_get_dev_from_info(struct genl_info *info)
{
	struct cfg80211_registered_device *drv;

	mutex_lock(&cfg80211_drv_mutex);
	drv = __cfg80211_drv_from_info(info);

	/* if it is not an error we grab the lock on
	 * it to assure it won't be going away while
	 * we operate on it */
	if (!IS_ERR(drv))
		mutex_lock(&drv->mtx);

	mutex_unlock(&cfg80211_drv_mutex);

	return drv;
}

struct cfg80211_registered_device *
cfg80211_get_dev_from_ifindex(int ifindex)
{
	struct cfg80211_registered_device *drv = ERR_PTR(-ENODEV);
	struct net_device *dev;

	mutex_lock(&cfg80211_drv_mutex);
	dev = dev_get_by_index(&init_net, ifindex);
	if (!dev)
		goto out;
	if (dev->ieee80211_ptr) {
		drv = wiphy_to_dev(dev->ieee80211_ptr->wiphy);
		mutex_lock(&drv->mtx);
	} else
		drv = ERR_PTR(-ENODEV);
	dev_put(dev);
 out:
	mutex_unlock(&cfg80211_drv_mutex);
	return drv;
}

void cfg80211_put_dev(struct cfg80211_registered_device *drv)
{
	BUG_ON(IS_ERR(drv));
	mutex_unlock(&drv->mtx);
}

int cfg80211_dev_rename(struct cfg80211_registered_device *rdev,
			char *newname)
{
	struct cfg80211_registered_device *drv;
	int idx, taken = -1, result, digits;

	mutex_lock(&cfg80211_drv_mutex);

	/* prohibit calling the thing phy%d when %d is not its number */
	sscanf(newname, PHY_NAME "%d%n", &idx, &taken);
	if (taken == strlen(newname) && idx != rdev->idx) {
		/* count number of places needed to print idx */
		digits = 1;
		while (idx /= 10)
			digits++;
		/*
		 * deny the name if it is phy<idx> where <idx> is printed
		 * without leading zeroes. taken == strlen(newname) here
		 */
		result = -EINVAL;
		if (taken == strlen(PHY_NAME) + digits)
			goto out_unlock;
	}


	/* Ignore nop renames */
	result = 0;
	if (strcmp(newname, dev_name(&rdev->wiphy.dev)) == 0)
		goto out_unlock;

	/* Ensure another device does not already have this name. */
	list_for_each_entry(drv, &cfg80211_drv_list, list) {
		result = -EINVAL;
		if (strcmp(newname, dev_name(&drv->wiphy.dev)) == 0)
			goto out_unlock;
	}

	/* this will only check for collisions in sysfs
	 * which is not even always compiled in.
	 */
	result = device_rename(&rdev->wiphy.dev, newname);
	if (result)
		goto out_unlock;

	if (!debugfs_rename(rdev->wiphy.debugfsdir->d_parent,
			    rdev->wiphy.debugfsdir,
			    rdev->wiphy.debugfsdir->d_parent,
			    newname))
		printk(KERN_ERR "cfg80211: failed to rename debugfs dir to %s!\n",
		       newname);

	result = 0;
out_unlock:
	mutex_unlock(&cfg80211_drv_mutex);
	if (result == 0)
		nl80211_notify_dev_rename(rdev);

	return result;
}

/* exported functions */

struct wiphy *wiphy_new(struct cfg80211_ops *ops, int sizeof_priv)
{
	struct cfg80211_registered_device *drv;
	int alloc_size;

	WARN_ON(!ops->add_key && ops->del_key);
	WARN_ON(ops->add_key && !ops->del_key);

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
	enum ieee80211_band band;
	struct ieee80211_supported_band *sband;
	bool have_band = false;
	int i;
	u16 ifmodes = wiphy->interface_modes;

	/* sanity check ifmodes */
	WARN_ON(!ifmodes);
	ifmodes &= ((1 << __NL80211_IFTYPE_AFTER_LAST) - 1) & ~1;
	if (WARN_ON(ifmodes != wiphy->interface_modes))
		wiphy->interface_modes = ifmodes;

	/* sanity check supported bands/channels */
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		sband = wiphy->bands[band];
		if (!sband)
			continue;

		sband->band = band;

		if (!sband->n_channels || !sband->n_bitrates) {
			WARN_ON(1);
			return -EINVAL;
		}

		for (i = 0; i < sband->n_channels; i++) {
			sband->channels[i].orig_flags =
				sband->channels[i].flags;
			sband->channels[i].orig_mag =
				sband->channels[i].max_antenna_gain;
			sband->channels[i].orig_mpwr =
				sband->channels[i].max_power;
			sband->channels[i].band = band;
		}

		have_band = true;
	}

	if (!have_band) {
		WARN_ON(1);
		return -EINVAL;
	}

	/* check and set up bitrates */
	ieee80211_set_bitrate_flags(wiphy);

	/* set up regulatory info */
	mutex_lock(&cfg80211_reg_mutex);
	wiphy_update_regulatory(wiphy, REGDOM_SET_BY_CORE);
	mutex_unlock(&cfg80211_reg_mutex);

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

	WARN_ON(dev->ieee80211_ptr->iftype == NL80211_IFTYPE_UNSPECIFIED);

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
	int err;

	err = wiphy_sysfs_init();
	if (err)
		goto out_fail_sysfs;

	err = register_netdevice_notifier(&cfg80211_netdev_notifier);
	if (err)
		goto out_fail_notifier;

	err = nl80211_init();
	if (err)
		goto out_fail_nl80211;

	ieee80211_debugfs_dir = debugfs_create_dir("ieee80211", NULL);

	err = regulatory_init();
	if (err)
		goto out_fail_reg;

	return 0;

out_fail_reg:
	debugfs_remove(ieee80211_debugfs_dir);
out_fail_nl80211:
	unregister_netdevice_notifier(&cfg80211_netdev_notifier);
out_fail_notifier:
	wiphy_sysfs_exit();
out_fail_sysfs:
	return err;
}

subsys_initcall(cfg80211_init);

static void cfg80211_exit(void)
{
	debugfs_remove(ieee80211_debugfs_dir);
	nl80211_exit();
	unregister_netdevice_notifier(&cfg80211_netdev_notifier);
	wiphy_sysfs_exit();
	regulatory_exit();
}
module_exit(cfg80211_exit);
