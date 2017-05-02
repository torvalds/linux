/*
 * This file provides /sys/class/ieee80211/<wiphy name>/
 * and some default attributes.
 *
 * Copyright 2005-2006	Jiri Benc <jbenc@suse.cz>
 * Copyright 2006	Johannes Berg <johannes@sipsolutions.net>
 *
 * This file is GPLv2 as found in COPYING.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <net/cfg80211.h>
#include "sysfs.h"
#include "core.h"
#include "rdev-ops.h"

static inline struct cfg80211_registered_device *dev_to_rdev(
	struct device *dev)
{
	return container_of(dev, struct cfg80211_registered_device, wiphy.dev);
}

#define SHOW_FMT(name, fmt, member)					\
static ssize_t name ## _show(struct device *dev,			\
			      struct device_attribute *attr,		\
			      char *buf)				\
{									\
	return sprintf(buf, fmt "\n", dev_to_rdev(dev)->member);	\
}									\
static DEVICE_ATTR_RO(name)

SHOW_FMT(index, "%d", wiphy_idx);
SHOW_FMT(macaddress, "%pM", wiphy.perm_addr);
SHOW_FMT(address_mask, "%pM", wiphy.addr_mask);

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct wiphy *wiphy = &dev_to_rdev(dev)->wiphy;

	return sprintf(buf, "%s\n", wiphy_name(wiphy));
}
static DEVICE_ATTR_RO(name);

static ssize_t addresses_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct wiphy *wiphy = &dev_to_rdev(dev)->wiphy;
	char *start = buf;
	int i;

	if (!wiphy->addresses)
		return sprintf(buf, "%pM\n", wiphy->perm_addr);

	for (i = 0; i < wiphy->n_addresses; i++)
		buf += sprintf(buf, "%pM\n", wiphy->addresses[i].addr);

	return buf - start;
}
static DEVICE_ATTR_RO(addresses);

static struct attribute *ieee80211_attrs[] = {
	&dev_attr_index.attr,
	&dev_attr_macaddress.attr,
	&dev_attr_address_mask.attr,
	&dev_attr_addresses.attr,
	&dev_attr_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ieee80211);

static void wiphy_dev_release(struct device *dev)
{
	struct cfg80211_registered_device *rdev = dev_to_rdev(dev);

	cfg80211_dev_free(rdev);
}

static int wiphy_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/* TODO, we probably need stuff here */
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static void cfg80211_leave_all(struct cfg80211_registered_device *rdev)
{
	struct wireless_dev *wdev;

	list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list)
		cfg80211_leave(rdev, wdev);
}

static int wiphy_suspend(struct device *dev)
{
	struct cfg80211_registered_device *rdev = dev_to_rdev(dev);
	int ret = 0;

	rdev->suspend_at = get_seconds();

	rtnl_lock();
	if (rdev->wiphy.registered) {
		if (!rdev->wiphy.wowlan_config) {
			cfg80211_leave_all(rdev);
			cfg80211_process_rdev_events(rdev);
		}
		if (rdev->ops->suspend)
			ret = rdev_suspend(rdev, rdev->wiphy.wowlan_config);
		if (ret == 1) {
			/* Driver refuse to configure wowlan */
			cfg80211_leave_all(rdev);
			cfg80211_process_rdev_events(rdev);
			ret = rdev_suspend(rdev, NULL);
		}
	}
	rtnl_unlock();

	return ret;
}

static int wiphy_resume(struct device *dev)
{
	struct cfg80211_registered_device *rdev = dev_to_rdev(dev);
	int ret = 0;

	/* Age scan results with time spent in suspend */
	cfg80211_bss_age(rdev, get_seconds() - rdev->suspend_at);

	if (rdev->ops->resume) {
		rtnl_lock();
		if (rdev->wiphy.registered)
			ret = rdev_resume(rdev);
		rtnl_unlock();
	}

	return ret;
}

static SIMPLE_DEV_PM_OPS(wiphy_pm_ops, wiphy_suspend, wiphy_resume);
#define WIPHY_PM_OPS (&wiphy_pm_ops)
#else
#define WIPHY_PM_OPS NULL
#endif

static const void *wiphy_namespace(struct device *d)
{
	struct wiphy *wiphy = container_of(d, struct wiphy, dev);

	return wiphy_net(wiphy);
}

struct class ieee80211_class = {
	.name = "ieee80211",
	.owner = THIS_MODULE,
	.dev_release = wiphy_dev_release,
	.dev_groups = ieee80211_groups,
	.dev_uevent = wiphy_uevent,
	.pm = WIPHY_PM_OPS,
	.ns_type = &net_ns_type_operations,
	.namespace = wiphy_namespace,
};

int wiphy_sysfs_init(void)
{
	return class_register(&ieee80211_class);
}

void wiphy_sysfs_exit(void)
{
	class_unregister(&ieee80211_class);
}
