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

#define MASTER_SHOW_COMPLEX(name, format_string, args...)		\
static ssize_t name ## _show(struct device *dev,			\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct wpan_phy *phy = container_of(dev, struct wpan_phy, dev);	\
	int ret;							\
									\
	mutex_lock(&phy->pib_lock);					\
	ret = snprintf(buf, PAGE_SIZE, format_string "\n", args);	\
	mutex_unlock(&phy->pib_lock);					\
	return ret;							\
}									\
static DEVICE_ATTR_RO(name)

#define MASTER_SHOW(field, format_string)				\
	MASTER_SHOW_COMPLEX(field, format_string, phy->field)

MASTER_SHOW(current_channel, "%d");
MASTER_SHOW(current_page, "%d");
MASTER_SHOW(transmit_power, "%d +- 1 dB");
MASTER_SHOW_COMPLEX(cca_mode, "%d", phy->cca.mode);

static ssize_t channels_supported_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct wpan_phy *phy = container_of(dev, struct wpan_phy, dev);
	int ret;
	int i, len = 0;

	mutex_lock(&phy->pib_lock);
	for (i = 0; i < 32; i++) {
		ret = snprintf(buf + len, PAGE_SIZE - len,
			       "%#09x\n", phy->channels_supported[i]);
		if (ret < 0)
			break;
		len += ret;
	}
	mutex_unlock(&phy->pib_lock);
	return len;
}
static DEVICE_ATTR_RO(channels_supported);

static void wpan_phy_release(struct device *dev)
{
	struct cfg802154_registered_device *rdev = dev_to_rdev(dev);

	cfg802154_dev_free(rdev);
}

static struct attribute *pmib_attrs[] = {
	&dev_attr_index.attr,
	&dev_attr_name.attr,
	/* below will be removed soon */
	&dev_attr_current_channel.attr,
	&dev_attr_current_page.attr,
	&dev_attr_channels_supported.attr,
	&dev_attr_transmit_power.attr,
	&dev_attr_cca_mode.attr,
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
