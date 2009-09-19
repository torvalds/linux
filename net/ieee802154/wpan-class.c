/*
 * Copyright (C) 2007, 2008, 2009 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>

#include <net/wpan-phy.h>

#define MASTER_SHOW_COMPLEX(name, format_string, args...)		\
static ssize_t name ## _show(struct device *dev,			\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct wpan_phy *phy = container_of(dev, struct wpan_phy, dev);	\
	int ret;							\
									\
	mutex_lock(&phy->pib_lock);					\
	ret = sprintf(buf, format_string "\n", args);			\
	mutex_unlock(&phy->pib_lock);					\
	return ret;							\
}

#define MASTER_SHOW(field, format_string)				\
	MASTER_SHOW_COMPLEX(field, format_string, phy->field)

MASTER_SHOW(current_channel, "%d");
MASTER_SHOW(current_page, "%d");
MASTER_SHOW(channels_supported, "%#x");
MASTER_SHOW_COMPLEX(transmit_power, "%d +- %d dB",
	((signed char) (phy->transmit_power << 2)) >> 2,
	(phy->transmit_power >> 6) ? (phy->transmit_power >> 6) * 3 : 1 );
MASTER_SHOW(cca_mode, "%d");

static struct device_attribute pmib_attrs[] = {
	__ATTR_RO(current_channel),
	__ATTR_RO(current_page),
	__ATTR_RO(channels_supported),
	__ATTR_RO(transmit_power),
	__ATTR_RO(cca_mode),
	{},
};

static void wpan_phy_release(struct device *d)
{
	struct wpan_phy *phy = container_of(d, struct wpan_phy, dev);
	kfree(phy);
}

static struct class wpan_phy_class = {
	.name = "ieee802154",
	.dev_release = wpan_phy_release,
	.dev_attrs = pmib_attrs,
};

static DEFINE_MUTEX(wpan_phy_mutex);
static int wpan_phy_idx;

static int wpan_phy_match(struct device *dev, void *data)
{
	return !strcmp(dev_name(dev), (const char *)data);
}

struct wpan_phy *wpan_phy_find(const char *str)
{
	struct device *dev;

	if (WARN_ON(!str))
		return NULL;

	dev = class_find_device(&wpan_phy_class, NULL,
			(void *)str, wpan_phy_match);
	if (!dev)
		return NULL;

	return container_of(dev, struct wpan_phy, dev);
}
EXPORT_SYMBOL(wpan_phy_find);

static int wpan_phy_idx_valid(int idx)
{
	return idx >= 0;
}

struct wpan_phy *wpan_phy_alloc(size_t priv_size)
{
	struct wpan_phy *phy = kzalloc(sizeof(*phy) + priv_size,
			GFP_KERNEL);

	mutex_lock(&wpan_phy_mutex);
	phy->idx = wpan_phy_idx++;
	if (unlikely(!wpan_phy_idx_valid(phy->idx))) {
		wpan_phy_idx--;
		mutex_unlock(&wpan_phy_mutex);
		kfree(phy);
		return NULL;
	}
	mutex_unlock(&wpan_phy_mutex);

	mutex_init(&phy->pib_lock);

	device_initialize(&phy->dev);
	dev_set_name(&phy->dev, "wpan-phy%d", phy->idx);

	phy->dev.class = &wpan_phy_class;

	return phy;
}
EXPORT_SYMBOL(wpan_phy_alloc);

int wpan_phy_register(struct device *parent, struct wpan_phy *phy)
{
	phy->dev.parent = parent;

	return device_add(&phy->dev);
}
EXPORT_SYMBOL(wpan_phy_register);

void wpan_phy_unregister(struct wpan_phy *phy)
{
	device_del(&phy->dev);
}
EXPORT_SYMBOL(wpan_phy_unregister);

void wpan_phy_free(struct wpan_phy *phy)
{
	put_device(&phy->dev);
}
EXPORT_SYMBOL(wpan_phy_free);

static int __init wpan_phy_class_init(void)
{
	return class_register(&wpan_phy_class);
}
subsys_initcall(wpan_phy_class_init);

static void __exit wpan_phy_class_exit(void)
{
	class_unregister(&wpan_phy_class);
}
module_exit(wpan_phy_class_exit);

MODULE_DESCRIPTION("IEEE 802.15.4 device class");
MODULE_LICENSE("GPL v2");

