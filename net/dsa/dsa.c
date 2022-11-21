// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/dsa/dsa.c - Hardware switch handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2013 Florian Fainelli <florian@openwrt.org>
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/sysfs.h>

#include "dsa_priv.h"
#include "slave.h"
#include "tag.h"

static int dev_is_class(struct device *dev, void *class)
{
	if (dev->class != NULL && !strcmp(dev->class->name, class))
		return 1;

	return 0;
}

static struct device *dev_find_class(struct device *parent, char *class)
{
	if (dev_is_class(parent, class)) {
		get_device(parent);
		return parent;
	}

	return device_find_child(parent, class, dev_is_class);
}

struct net_device *dsa_dev_to_net_device(struct device *dev)
{
	struct device *d;

	d = dev_find_class(dev, "net");
	if (d != NULL) {
		struct net_device *nd;

		nd = to_net_dev(d);
		dev_hold(nd);
		put_device(d);

		return nd;
	}

	return NULL;
}

#ifdef CONFIG_PM_SLEEP
static bool dsa_port_is_initialized(const struct dsa_port *dp)
{
	return dp->type == DSA_PORT_TYPE_USER && dp->slave;
}

int dsa_switch_suspend(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	int ret = 0;

	/* Suspend slave network devices */
	dsa_switch_for_each_port(dp, ds) {
		if (!dsa_port_is_initialized(dp))
			continue;

		ret = dsa_slave_suspend(dp->slave);
		if (ret)
			return ret;
	}

	if (ds->ops->suspend)
		ret = ds->ops->suspend(ds);

	return ret;
}
EXPORT_SYMBOL_GPL(dsa_switch_suspend);

int dsa_switch_resume(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	int ret = 0;

	if (ds->ops->resume)
		ret = ds->ops->resume(ds);

	if (ret)
		return ret;

	/* Resume slave network devices */
	dsa_switch_for_each_port(dp, ds) {
		if (!dsa_port_is_initialized(dp))
			continue;

		ret = dsa_slave_resume(dp->slave);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dsa_switch_resume);
#endif

static struct workqueue_struct *dsa_owq;

bool dsa_schedule_work(struct work_struct *work)
{
	return queue_work(dsa_owq, work);
}

void dsa_flush_workqueue(void)
{
	flush_workqueue(dsa_owq);
}
EXPORT_SYMBOL_GPL(dsa_flush_workqueue);

struct dsa_port *dsa_port_from_netdev(struct net_device *netdev)
{
	if (!netdev || !dsa_slave_dev_check(netdev))
		return ERR_PTR(-ENODEV);

	return dsa_slave_to_port(netdev);
}
EXPORT_SYMBOL_GPL(dsa_port_from_netdev);

bool dsa_db_equal(const struct dsa_db *a, const struct dsa_db *b)
{
	if (a->type != b->type)
		return false;

	switch (a->type) {
	case DSA_DB_PORT:
		return a->dp == b->dp;
	case DSA_DB_LAG:
		return a->lag.dev == b->lag.dev;
	case DSA_DB_BRIDGE:
		return a->bridge.num == b->bridge.num;
	default:
		WARN_ON(1);
		return false;
	}
}

bool dsa_fdb_present_in_other_db(struct dsa_switch *ds, int port,
				 const unsigned char *addr, u16 vid,
				 struct dsa_db db)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct dsa_mac_addr *a;

	lockdep_assert_held(&dp->addr_lists_lock);

	list_for_each_entry(a, &dp->fdbs, list) {
		if (!ether_addr_equal(a->addr, addr) || a->vid != vid)
			continue;

		if (a->db.type == db.type && !dsa_db_equal(&a->db, &db))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(dsa_fdb_present_in_other_db);

bool dsa_mdb_present_in_other_db(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb,
				 struct dsa_db db)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct dsa_mac_addr *a;

	lockdep_assert_held(&dp->addr_lists_lock);

	list_for_each_entry(a, &dp->mdbs, list) {
		if (!ether_addr_equal(a->addr, mdb->addr) || a->vid != mdb->vid)
			continue;

		if (a->db.type == db.type && !dsa_db_equal(&a->db, &db))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(dsa_mdb_present_in_other_db);

static int __init dsa_init_module(void)
{
	int rc;

	dsa_owq = alloc_ordered_workqueue("dsa_ordered",
					  WQ_MEM_RECLAIM);
	if (!dsa_owq)
		return -ENOMEM;

	rc = dsa_slave_register_notifier();
	if (rc)
		goto register_notifier_fail;

	dev_add_pack(&dsa_pack_type);

	rc = rtnl_link_register(&dsa_link_ops);
	if (rc)
		goto netlink_register_fail;

	return 0;

netlink_register_fail:
	dsa_slave_unregister_notifier();
	dev_remove_pack(&dsa_pack_type);
register_notifier_fail:
	destroy_workqueue(dsa_owq);

	return rc;
}
module_init(dsa_init_module);

static void __exit dsa_cleanup_module(void)
{
	rtnl_link_unregister(&dsa_link_ops);

	dsa_slave_unregister_notifier();
	dev_remove_pack(&dsa_pack_type);
	destroy_workqueue(dsa_owq);
}
module_exit(dsa_cleanup_module);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_DESCRIPTION("Driver for Distributed Switch Architecture switch chips");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dsa");
