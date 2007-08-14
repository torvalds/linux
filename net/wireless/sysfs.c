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

static ssize_t _show_index(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return sprintf(buf, "%d\n", dev_to_rdev(dev)->idx);
}

static ssize_t _show_permaddr(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	unsigned char *addr = dev_to_rdev(dev)->wiphy.perm_addr;

	return sprintf(buf, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
		       addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static struct device_attribute ieee80211_dev_attrs[] = {
	__ATTR(index, S_IRUGO, _show_index, NULL),
	__ATTR(macaddress, S_IRUGO, _show_permaddr, NULL),
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
