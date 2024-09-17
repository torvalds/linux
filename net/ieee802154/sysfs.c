// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Authors:
 * Alexander Aring <aar@pengutronix.de>
 *
 * Based on: net/wireless/sysfs.c
 */

#include <linux/device.h>
#include <linux/rtnetlink.h>

#include <net/cfg802154.h>

#include "core.h"
#include "sysfs.h"
#include "rdev-ops.h"

static inline struct cfg802154_registered_device *
dev_to_rdev(struct device *dev)
{
	return container_of(dev, struct cfg802154_registered_device,
			    wpan_phy.dev);
}

#define SHOW_FMT(name, fmt, member)					\
static ssize_t name ## _show(struct device *dev,			\
			     struct device_attribute *attr,		\
			     char *buf)					\
{									\
	return sprintf(buf, fmt "\n", dev_to_rdev(dev)->member);	\
}									\
static DEVICE_ATTR_RO(name)

SHOW_FMT(index, "%d", wpan_phy_idx);

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct wpan_phy *wpan_phy = &dev_to_rdev(dev)->wpan_phy;

	return sprintf(buf, "%s\n", dev_name(&wpan_phy->dev));
}
static DEVICE_ATTR_RO(name);

static void wpan_phy_release(struct device *dev)
{
	struct cfg802154_registered_device *rdev = dev_to_rdev(dev);

	cfg802154_dev_free(rdev);
}

static struct attribute *pmib_attrs[] = {
	&dev_attr_index.attr,
	&dev_attr_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pmib);

#ifdef CONFIG_PM_SLEEP
static int wpan_phy_suspend(struct device *dev)
{
	struct cfg802154_registered_device *rdev = dev_to_rdev(dev);
	int ret = 0;

	if (rdev->ops->suspend) {
		rtnl_lock();
		ret = rdev_suspend(rdev);
		rtnl_unlock();
	}

	return ret;
}

static int wpan_phy_resume(struct device *dev)
{
	struct cfg802154_registered_device *rdev = dev_to_rdev(dev);
	int ret = 0;

	if (rdev->ops->resume) {
		rtnl_lock();
		ret = rdev_resume(rdev);
		rtnl_unlock();
	}

	return ret;
}

static SIMPLE_DEV_PM_OPS(wpan_phy_pm_ops, wpan_phy_suspend, wpan_phy_resume);
#define WPAN_PHY_PM_OPS (&wpan_phy_pm_ops)
#else
#define WPAN_PHY_PM_OPS NULL
#endif

struct class wpan_phy_class = {
	.name = "ieee802154",
	.dev_release = wpan_phy_release,
	.dev_groups = pmib_groups,
	.pm = WPAN_PHY_PM_OPS,
};

int wpan_phy_sysfs_init(void)
{
	return class_register(&wpan_phy_class);
}

void wpan_phy_sysfs_exit(void)
{
	class_unregister(&wpan_phy_class);
}
