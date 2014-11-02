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
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>

#include <net/cfg802154.h>

#include "ieee802154.h"
#include "sysfs.h"
#include "core.h"

static DEFINE_MUTEX(wpan_phy_mutex);
static int wpan_phy_idx;

static int wpan_phy_match(struct device *dev, const void *data)
{
	return !strcmp(dev_name(dev), (const char *)data);
}

struct wpan_phy *wpan_phy_find(const char *str)
{
	struct device *dev;

	if (WARN_ON(!str))
		return NULL;

	dev = class_find_device(&wpan_phy_class, NULL, str, wpan_phy_match);
	if (!dev)
		return NULL;

	return container_of(dev, struct wpan_phy, dev);
}
EXPORT_SYMBOL(wpan_phy_find);

struct wpan_phy_iter_data {
	int (*fn)(struct wpan_phy *phy, void *data);
	void *data;
};

static int wpan_phy_iter(struct device *dev, void *_data)
{
	struct wpan_phy_iter_data *wpid = _data;
	struct wpan_phy *phy = container_of(dev, struct wpan_phy, dev);

	return wpid->fn(phy, wpid->data);
}

int wpan_phy_for_each(int (*fn)(struct wpan_phy *phy, void *data),
		      void *data)
{
	struct wpan_phy_iter_data wpid = {
		.fn = fn,
		.data = data,
	};

	return class_for_each_device(&wpan_phy_class, NULL,
			&wpid, wpan_phy_iter);
}
EXPORT_SYMBOL(wpan_phy_for_each);

static int wpan_phy_idx_valid(int idx)
{
	return idx >= 0;
}

struct wpan_phy *
wpan_phy_alloc(const struct cfg802154_ops *ops, size_t priv_size)
{
	struct cfg802154_registered_device *rdev;
	size_t alloc_size;

	alloc_size = sizeof(*rdev) + priv_size;
	rdev = kzalloc(alloc_size, GFP_KERNEL);
	if (!rdev)
		return NULL;

	rdev->ops = ops;

	mutex_lock(&wpan_phy_mutex);
	rdev->wpan_phy.idx = wpan_phy_idx++;
	if (unlikely(!wpan_phy_idx_valid(rdev->wpan_phy.idx))) {
		wpan_phy_idx--;
		mutex_unlock(&wpan_phy_mutex);
		kfree(rdev);
		goto out;
	}
	mutex_unlock(&wpan_phy_mutex);

	mutex_init(&rdev->wpan_phy.pib_lock);

	device_initialize(&rdev->wpan_phy.dev);
	dev_set_name(&rdev->wpan_phy.dev, "wpan-phy%d", rdev->wpan_phy.idx);

	rdev->wpan_phy.dev.class = &wpan_phy_class;
	rdev->wpan_phy.dev.platform_data = rdev;

	return &rdev->wpan_phy;

out:
	return NULL;
}
EXPORT_SYMBOL(wpan_phy_alloc);

int wpan_phy_register(struct wpan_phy *phy)
{
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

void cfg802154_dev_free(struct cfg802154_registered_device *rdev)
{
	kfree(rdev);
}

static int __init wpan_phy_class_init(void)
{
	int rc;

	rc = wpan_phy_sysfs_init();
	if (rc)
		goto err;

	rc = ieee802154_nl_init();
	if (rc)
		goto err_nl;

	return 0;
err_nl:
	wpan_phy_sysfs_exit();
err:
	return rc;
}
subsys_initcall(wpan_phy_class_init);

static void __exit wpan_phy_class_exit(void)
{
	ieee802154_nl_exit();
	wpan_phy_sysfs_exit();
}
module_exit(wpan_phy_class_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("IEEE 802.15.4 configuration interface");
MODULE_AUTHOR("Dmitry Eremin-Solenikov");

