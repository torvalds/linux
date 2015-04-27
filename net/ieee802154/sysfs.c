/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 * Alexander Aring <aar@pengutronix.de>
 *
 * Based on: net/wireless/sysfs.c
 */

#include <linux/device.h>

#include <net/cfg802154.h>

#include "core.h"
#include "sysfs.h"

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

struct class wpan_phy_class = {
	.name = "ieee802154",
	.dev_release = wpan_phy_release,
	.dev_groups = pmib_groups,
};

int wpan_phy_sysfs_init(void)
{
	return class_register(&wpan_phy_class);
}

void wpan_phy_sysfs_exit(void)
{
	class_unregister(&wpan_phy_class);
}
