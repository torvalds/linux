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
}

SHOW_FMT(index, "%d", idx);
SHOW_FMT(macaddress, "%pM", wiphy.perm_addr);

static struct device_attribute ieee80211_dev_attrs[] = {
	__ATTR_RO(index),
	__ATTR_RO(macaddress),
	{}
};

static void wiphy_dev_release(struct device *dev)
{
	struct cfg80211_registered_device *rdev = dev_to_rdev(dev);

	cfg80211_dev_free(rdev);
}

#ifdef CONFIG_HOTPLUG
static int wiphy_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/* TODO, we probably need stuff here */
	return 0;
}
#endif

struct class ieee80211_class = {
	.name = "ieee80211",
	.owner = THIS_MODULE,
	.dev_release = wiphy_dev_release,
	.dev_attrs = ieee80211_dev_attrs,
#ifdef CONFIG_HOTPLUG
	.dev_uevent = wiphy_uevent,
#endif
};

int wiphy_sysfs_init(void)
{
	return class_register(&ieee80211_class);
}

void wiphy_sysfs_exit(void)
{
	class_unregister(&ieee80211_class);
}
