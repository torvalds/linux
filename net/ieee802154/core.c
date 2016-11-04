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
#include <net/rtnetlink.h>

#include "ieee802154.h"
#include "nl802154.h"
#include "sysfs.h"
#include "core.h"

/* name for sysfs, %d is appended */
#define PHY_NAME "phy"

/* RCU-protected (and RTNL for writers) */
LIST_HEAD(cfg802154_rdev_list);
int cfg802154_rdev_list_generation;

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

struct cfg802154_registered_device *
cfg802154_rdev_by_wpan_phy_idx(int wpan_phy_idx)
{
	struct cfg802154_registered_device *result = NULL, *rdev;

	ASSERT_RTNL();

	list_for_each_entry(rdev, &cfg802154_rdev_list, list) {
		if (rdev->wpan_phy_idx == wpan_phy_idx) {
			result = rdev;
			break;
		}
	}

	return result;
}

struct wpan_phy *wpan_phy_idx_to_wpan_phy(int wpan_phy_idx)
{
	struct cfg802154_registered_device *rdev;

	ASSERT_RTNL();

	rdev = cfg802154_rdev_by_wpan_phy_idx(wpan_phy_idx);
	if (!rdev)
		return NULL;
	return &rdev->wpan_phy;
}

struct wpan_phy *
wpan_phy_new(const struct cfg802154_ops *ops, size_t priv_size)
{
	static atomic_t wpan_phy_counter = ATOMIC_INIT(0);
	struct cfg802154_registered_device *rdev;
	size_t alloc_size;

	alloc_size = sizeof(*rdev) + priv_size;
	rdev = kzalloc(alloc_size, GFP_KERNEL);
	if (!rdev)
		return NULL;

	rdev->ops = ops;

	rdev->wpan_phy_idx = atomic_inc_return(&wpan_phy_counter);

	if (unlikely(rdev->wpan_phy_idx < 0)) {
		/* ugh, wrapped! */
		atomic_dec(&wpan_phy_counter);
		kfree(rdev);
		return NULL;
	}

	/* atomic_inc_return makes it start at 1, make it start at 0 */
	rdev->wpan_phy_idx--;

	INIT_LIST_HEAD(&rdev->wpan_dev_list);
	device_initialize(&rdev->wpan_phy.dev);
	dev_set_name(&rdev->wpan_phy.dev, PHY_NAME "%d", rdev->wpan_phy_idx);

	rdev->wpan_phy.dev.class = &wpan_phy_class;
	rdev->wpan_phy.dev.platform_data = rdev;

	wpan_phy_net_set(&rdev->wpan_phy, &init_net);

	init_waitqueue_head(&rdev->dev_wait);

	return &rdev->wpan_phy;
}
EXPORT_SYMBOL(wpan_phy_new);

int wpan_phy_register(struct wpan_phy *phy)
{
	struct cfg802154_registered_device *rdev = wpan_phy_to_rdev(phy);
	int ret;

	rtnl_lock();
	ret = device_add(&phy->dev);
	if (ret) {
		rtnl_unlock();
		return ret;
	}

	list_add_rcu(&rdev->list, &cfg802154_rdev_list);
	cfg802154_rdev_list_generation++;

	/* TODO phy registered lock */
	rtnl_unlock();

	/* TODO nl802154 phy notify */

	return 0;
}
EXPORT_SYMBOL(wpan_phy_register);

void wpan_phy_unregister(struct wpan_phy *phy)
{
	struct cfg802154_registered_device *rdev = wpan_phy_to_rdev(phy);

	wait_event(rdev->dev_wait, ({
		int __count;
		rtnl_lock();
		__count = rdev->opencount;
		rtnl_unlock();
		__count == 0; }));

	rtnl_lock();
	/* TODO nl802154 phy notify */
	/* TODO phy registered lock */

	WARN_ON(!list_empty(&rdev->wpan_dev_list));

	/* First remove the hardware from everywhere, this makes
	 * it impossible to find from userspace.
	 */
	list_del_rcu(&rdev->list);
	synchronize_rcu();

	cfg802154_rdev_list_generation++;

	device_del(&phy->dev);

	rtnl_unlock();
}
EXPORT_SYMBOL(wpan_phy_unregister);

void wpan_phy_free(struct wpan_phy *phy)
{
	put_device(&phy->dev);
}
EXPORT_SYMBOL(wpan_phy_free);

int cfg802154_switch_netns(struct cfg802154_registered_device *rdev,
			   struct net *net)
{
	struct wpan_dev *wpan_dev;
	int err = 0;

	list_for_each_entry(wpan_dev, &rdev->wpan_dev_list, list) {
		if (!wpan_dev->netdev)
			continue;
		wpan_dev->netdev->features &= ~NETIF_F_NETNS_LOCAL;
		err = dev_change_net_namespace(wpan_dev->netdev, net, "wpan%d");
		if (err)
			break;
		wpan_dev->netdev->features |= NETIF_F_NETNS_LOCAL;
	}

	if (err) {
		/* failed -- clean up to old netns */
		net = wpan_phy_net(&rdev->wpan_phy);

		list_for_each_entry_continue_reverse(wpan_dev,
						     &rdev->wpan_dev_list,
						     list) {
			if (!wpan_dev->netdev)
				continue;
			wpan_dev->netdev->features &= ~NETIF_F_NETNS_LOCAL;
			err = dev_change_net_namespace(wpan_dev->netdev, net,
						       "wpan%d");
			WARN_ON(err);
			wpan_dev->netdev->features |= NETIF_F_NETNS_LOCAL;
		}

		return err;
	}

	wpan_phy_net_set(&rdev->wpan_phy, net);

	err = device_rename(&rdev->wpan_phy.dev, dev_name(&rdev->wpan_phy.dev));
	WARN_ON(err);

	return 0;
}

void cfg802154_dev_free(struct cfg802154_registered_device *rdev)
{
	kfree(rdev);
}

static void
cfg802154_update_iface_num(struct cfg802154_registered_device *rdev,
			   int iftype, int num)
{
	ASSERT_RTNL();

	rdev->num_running_ifaces += num;
}

static int cfg802154_netdev_notifier_call(struct notifier_block *nb,
					  unsigned long state, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	struct cfg802154_registered_device *rdev;

	if (!wpan_dev)
		return NOTIFY_DONE;

	rdev = wpan_phy_to_rdev(wpan_dev->wpan_phy);

	/* TODO WARN_ON unspec type */

	switch (state) {
		/* TODO NETDEV_DEVTYPE */
	case NETDEV_REGISTER:
		dev->features |= NETIF_F_NETNS_LOCAL;
		wpan_dev->identifier = ++rdev->wpan_dev_id;
		list_add_rcu(&wpan_dev->list, &rdev->wpan_dev_list);
		rdev->devlist_generation++;

		wpan_dev->netdev = dev;
		break;
	case NETDEV_DOWN:
		cfg802154_update_iface_num(rdev, wpan_dev->iftype, -1);

		rdev->opencount--;
		wake_up(&rdev->dev_wait);
		break;
	case NETDEV_UP:
		cfg802154_update_iface_num(rdev, wpan_dev->iftype, 1);

		rdev->opencount++;
		break;
	case NETDEV_UNREGISTER:
		/* It is possible to get NETDEV_UNREGISTER
		 * multiple times. To detect that, check
		 * that the interface is still on the list
		 * of registered interfaces, and only then
		 * remove and clean it up.
		 */
		if (!list_empty(&wpan_dev->list)) {
			list_del_rcu(&wpan_dev->list);
			rdev->devlist_generation++;
		}
		/* synchronize (so that we won't find this netdev
		 * from other code any more) and then clear the list
		 * head so that the above code can safely check for
		 * !list_empty() to avoid double-cleanup.
		 */
		synchronize_rcu();
		INIT_LIST_HEAD(&wpan_dev->list);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block cfg802154_netdev_notifier = {
	.notifier_call = cfg802154_netdev_notifier_call,
};

static void __net_exit cfg802154_pernet_exit(struct net *net)
{
	struct cfg802154_registered_device *rdev;

	rtnl_lock();
	list_for_each_entry(rdev, &cfg802154_rdev_list, list) {
		if (net_eq(wpan_phy_net(&rdev->wpan_phy), net))
			WARN_ON(cfg802154_switch_netns(rdev, &init_net));
	}
	rtnl_unlock();
}

static struct pernet_operations cfg802154_pernet_ops = {
	.exit = cfg802154_pernet_exit,
};

static int __init wpan_phy_class_init(void)
{
	int rc;

	rc = register_pernet_device(&cfg802154_pernet_ops);
	if (rc)
		goto err;

	rc = wpan_phy_sysfs_init();
	if (rc)
		goto err_sysfs;

	rc = register_netdevice_notifier(&cfg802154_netdev_notifier);
	if (rc)
		goto err_nl;

	rc = ieee802154_nl_init();
	if (rc)
		goto err_notifier;

	rc = nl802154_init();
	if (rc)
		goto err_ieee802154_nl;

	return 0;

err_ieee802154_nl:
	ieee802154_nl_exit();

err_notifier:
	unregister_netdevice_notifier(&cfg802154_netdev_notifier);
err_nl:
	wpan_phy_sysfs_exit();
err_sysfs:
	unregister_pernet_device(&cfg802154_pernet_ops);
err:
	return rc;
}
subsys_initcall(wpan_phy_class_init);

static void __exit wpan_phy_class_exit(void)
{
	nl802154_exit();
	ieee802154_nl_exit();
	unregister_netdevice_notifier(&cfg802154_netdev_notifier);
	wpan_phy_sysfs_exit();
	unregister_pernet_device(&cfg802154_pernet_ops);
}
module_exit(wpan_phy_class_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("IEEE 802.15.4 configuration interface");
MODULE_AUTHOR("Dmitry Eremin-Solenikov");

