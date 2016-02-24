/*
 * include/net/l3mdev.h - L3 master device API
 * Copyright (c) 2015 Cumulus Networks
 * Copyright (c) 2015 David Ahern <dsa@cumulusnetworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _NET_L3MDEV_H_
#define _NET_L3MDEV_H_

/**
 * struct l3mdev_ops - l3mdev operations
 *
 * @l3mdev_fib_table: Get FIB table id to use for lookups
 *
 * @l3mdev_get_rtable: Get cached IPv4 rtable (dst_entry) for device
 *
 * @l3mdev_get_saddr: Get source address for a flow
 *
 * @l3mdev_get_rt6_dst: Get cached IPv6 rt6_info (dst_entry) for device
 */

struct l3mdev_ops {
	u32		(*l3mdev_fib_table)(const struct net_device *dev);

	/* IPv4 ops */
	struct rtable *	(*l3mdev_get_rtable)(const struct net_device *dev,
					     const struct flowi4 *fl4);
	int		(*l3mdev_get_saddr)(struct net_device *dev,
					    struct flowi4 *fl4);

	/* IPv6 ops */
	struct dst_entry * (*l3mdev_get_rt6_dst)(const struct net_device *dev,
						 const struct flowi6 *fl6);
};

#ifdef CONFIG_NET_L3_MASTER_DEV

int l3mdev_master_ifindex_rcu(const struct net_device *dev);
static inline int l3mdev_master_ifindex(struct net_device *dev)
{
	int ifindex;

	rcu_read_lock();
	ifindex = l3mdev_master_ifindex_rcu(dev);
	rcu_read_unlock();

	return ifindex;
}

static inline int l3mdev_master_ifindex_by_index(struct net *net, int ifindex)
{
	struct net_device *dev;
	int rc = 0;

	if (likely(ifindex)) {
		rcu_read_lock();

		dev = dev_get_by_index_rcu(net, ifindex);
		if (dev)
			rc = l3mdev_master_ifindex_rcu(dev);

		rcu_read_unlock();
	}

	return rc;
}

/* get index of an interface to use for FIB lookups. For devices
 * enslaved to an L3 master device FIB lookups are based on the
 * master index
 */
static inline int l3mdev_fib_oif_rcu(struct net_device *dev)
{
	return l3mdev_master_ifindex_rcu(dev) ? : dev->ifindex;
}

static inline int l3mdev_fib_oif(struct net_device *dev)
{
	int oif;

	rcu_read_lock();
	oif = l3mdev_fib_oif_rcu(dev);
	rcu_read_unlock();

	return oif;
}

u32 l3mdev_fib_table_rcu(const struct net_device *dev);
u32 l3mdev_fib_table_by_index(struct net *net, int ifindex);
static inline u32 l3mdev_fib_table(const struct net_device *dev)
{
	u32 tb_id;

	rcu_read_lock();
	tb_id = l3mdev_fib_table_rcu(dev);
	rcu_read_unlock();

	return tb_id;
}

static inline struct rtable *l3mdev_get_rtable(const struct net_device *dev,
					       const struct flowi4 *fl4)
{
	if (netif_is_l3_master(dev) && dev->l3mdev_ops->l3mdev_get_rtable)
		return dev->l3mdev_ops->l3mdev_get_rtable(dev, fl4);

	return NULL;
}

static inline bool netif_index_is_l3_master(struct net *net, int ifindex)
{
	struct net_device *dev;
	bool rc = false;

	if (ifindex == 0)
		return false;

	rcu_read_lock();

	dev = dev_get_by_index_rcu(net, ifindex);
	if (dev)
		rc = netif_is_l3_master(dev);

	rcu_read_unlock();

	return rc;
}

static inline int l3mdev_get_saddr(struct net *net, int ifindex,
				   struct flowi4 *fl4)
{
	struct net_device *dev;
	int rc = 0;

	if (ifindex) {

		rcu_read_lock();

		dev = dev_get_by_index_rcu(net, ifindex);
		if (dev && netif_is_l3_master(dev) &&
		    dev->l3mdev_ops->l3mdev_get_saddr) {
			rc = dev->l3mdev_ops->l3mdev_get_saddr(dev, fl4);
		}

		rcu_read_unlock();
	}

	return rc;
}

static inline struct dst_entry *l3mdev_get_rt6_dst(const struct net_device *dev,
						   const struct flowi6 *fl6)
{
	if (netif_is_l3_master(dev) && dev->l3mdev_ops->l3mdev_get_rt6_dst)
		return dev->l3mdev_ops->l3mdev_get_rt6_dst(dev, fl6);

	return NULL;
}

static inline
struct dst_entry *l3mdev_rt6_dst_by_oif(struct net *net,
					const struct flowi6 *fl6)
{
	struct dst_entry *dst = NULL;
	struct net_device *dev;

	dev = dev_get_by_index(net, fl6->flowi6_oif);
	if (dev) {
		dst = l3mdev_get_rt6_dst(dev, fl6);
		dev_put(dev);
	}

	return dst;
}

#else

static inline int l3mdev_master_ifindex_rcu(const struct net_device *dev)
{
	return 0;
}
static inline int l3mdev_master_ifindex(struct net_device *dev)
{
	return 0;
}

static inline int l3mdev_master_ifindex_by_index(struct net *net, int ifindex)
{
	return 0;
}

static inline int l3mdev_fib_oif_rcu(struct net_device *dev)
{
	return dev ? dev->ifindex : 0;
}
static inline int l3mdev_fib_oif(struct net_device *dev)
{
	return dev ? dev->ifindex : 0;
}

static inline u32 l3mdev_fib_table_rcu(const struct net_device *dev)
{
	return 0;
}
static inline u32 l3mdev_fib_table(const struct net_device *dev)
{
	return 0;
}
static inline u32 l3mdev_fib_table_by_index(struct net *net, int ifindex)
{
	return 0;
}

static inline struct rtable *l3mdev_get_rtable(const struct net_device *dev,
					       const struct flowi4 *fl4)
{
	return NULL;
}

static inline bool netif_index_is_l3_master(struct net *net, int ifindex)
{
	return false;
}

static inline int l3mdev_get_saddr(struct net *net, int ifindex,
				   struct flowi4 *fl4)
{
	return 0;
}

static inline
struct dst_entry *l3mdev_get_rt6_dst(const struct net_device *dev,
				     const struct flowi6 *fl6)
{
	return NULL;
}
static inline
struct dst_entry *l3mdev_rt6_dst_by_oif(struct net *net,
					const struct flowi6 *fl6)
{
	return NULL;
}
#endif

#endif /* _NET_L3MDEV_H_ */
