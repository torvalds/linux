/*
 * net/l3mdev/l3mdev.c - L3 master device implementation
 * Copyright (c) 2015 Cumulus Networks
 * Copyright (c) 2015 David Ahern <dsa@cumulusnetworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/netdevice.h>
#include <net/l3mdev.h>

/**
 *	l3mdev_master_ifindex - get index of L3 master device
 *	@dev: targeted interface
 */

int l3mdev_master_ifindex_rcu(const struct net_device *dev)
{
	int ifindex = 0;

	if (!dev)
		return 0;

	if (netif_is_l3_master(dev)) {
		ifindex = dev->ifindex;
	} else if (netif_is_l3_slave(dev)) {
		struct net_device *master;
		struct net_device *_dev = (struct net_device *)dev;

		/* netdev_master_upper_dev_get_rcu calls
		 * list_first_or_null_rcu to walk the upper dev list.
		 * list_first_or_null_rcu does not handle a const arg. We aren't
		 * making changes, just want the master device from that list so
		 * typecast to remove the const
		 */
		master = netdev_master_upper_dev_get_rcu(_dev);
		if (master)
			ifindex = master->ifindex;
	}

	return ifindex;
}
EXPORT_SYMBOL_GPL(l3mdev_master_ifindex_rcu);

/**
 *	l3mdev_fib_table - get FIB table id associated with an L3
 *                             master interface
 *	@dev: targeted interface
 */

u32 l3mdev_fib_table_rcu(const struct net_device *dev)
{
	u32 tb_id = 0;

	if (!dev)
		return 0;

	if (netif_is_l3_master(dev)) {
		if (dev->l3mdev_ops->l3mdev_fib_table)
			tb_id = dev->l3mdev_ops->l3mdev_fib_table(dev);
	} else if (netif_is_l3_slave(dev)) {
		/* Users of netdev_master_upper_dev_get_rcu need non-const,
		 * but current inet_*type functions take a const
		 */
		struct net_device *_dev = (struct net_device *) dev;
		const struct net_device *master;

		master = netdev_master_upper_dev_get_rcu(_dev);
		if (master &&
		    master->l3mdev_ops->l3mdev_fib_table)
			tb_id = master->l3mdev_ops->l3mdev_fib_table(master);
	}

	return tb_id;
}
EXPORT_SYMBOL_GPL(l3mdev_fib_table_rcu);

u32 l3mdev_fib_table_by_index(struct net *net, int ifindex)
{
	struct net_device *dev;
	u32 tb_id = 0;

	if (!ifindex)
		return 0;

	rcu_read_lock();

	dev = dev_get_by_index_rcu(net, ifindex);
	if (dev)
		tb_id = l3mdev_fib_table_rcu(dev);

	rcu_read_unlock();

	return tb_id;
}
EXPORT_SYMBOL_GPL(l3mdev_fib_table_by_index);
