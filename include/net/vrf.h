/*
 * include/net/net_vrf.h - adds vrf dev structure definitions
 * Copyright (c) 2015 Cumulus Networks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_NET_VRF_H
#define __LINUX_NET_VRF_H

struct net_vrf_dev {
	struct rcu_head		rcu;
	int                     ifindex; /* ifindex of master dev */
	u32                     tb_id;   /* table id for VRF */
};

struct slave {
	struct list_head	list;
	struct net_device	*dev;
};

struct slave_queue {
	struct list_head	all_slaves;
};

struct net_vrf {
	struct slave_queue	queue;
	struct rtable           *rth;
	u32			tb_id;
};


#if IS_ENABLED(CONFIG_NET_VRF)
/* called with rcu_read_lock() */
static inline int vrf_master_ifindex_rcu(const struct net_device *dev)
{
	struct net_vrf_dev *vrf_ptr;
	int ifindex = 0;

	if (!dev)
		return 0;

	if (netif_is_vrf(dev)) {
		ifindex = dev->ifindex;
	} else {
		vrf_ptr = rcu_dereference(dev->vrf_ptr);
		if (vrf_ptr)
			ifindex = vrf_ptr->ifindex;
	}

	return ifindex;
}

static inline int vrf_master_ifindex(const struct net_device *dev)
{
	int ifindex;

	rcu_read_lock();
	ifindex = vrf_master_ifindex_rcu(dev);
	rcu_read_unlock();

	return ifindex;
}

/* called with rcu_read_lock */
static inline u32 vrf_dev_table_rcu(const struct net_device *dev)
{
	u32 tb_id = 0;

	if (dev) {
		struct net_vrf_dev *vrf_ptr;

		vrf_ptr = rcu_dereference(dev->vrf_ptr);
		if (vrf_ptr)
			tb_id = vrf_ptr->tb_id;
	}
	return tb_id;
}

static inline u32 vrf_dev_table(const struct net_device *dev)
{
	u32 tb_id;

	rcu_read_lock();
	tb_id = vrf_dev_table_rcu(dev);
	rcu_read_unlock();

	return tb_id;
}

static inline u32 vrf_dev_table_ifindex(struct net *net, int ifindex)
{
	struct net_device *dev;
	u32 tb_id = 0;

	if (!ifindex)
		return 0;

	rcu_read_lock();

	dev = dev_get_by_index_rcu(net, ifindex);
	if (dev)
		tb_id = vrf_dev_table_rcu(dev);

	rcu_read_unlock();

	return tb_id;
}

/* called with rtnl */
static inline u32 vrf_dev_table_rtnl(const struct net_device *dev)
{
	u32 tb_id = 0;

	if (dev) {
		struct net_vrf_dev *vrf_ptr;

		vrf_ptr = rtnl_dereference(dev->vrf_ptr);
		if (vrf_ptr)
			tb_id = vrf_ptr->tb_id;
	}
	return tb_id;
}

/* caller has already checked netif_is_vrf(dev) */
static inline struct rtable *vrf_dev_get_rth(const struct net_device *dev)
{
	struct rtable *rth = ERR_PTR(-ENETUNREACH);
	struct net_vrf *vrf = netdev_priv(dev);

	if (vrf) {
		rth = vrf->rth;
		atomic_inc(&rth->dst.__refcnt);
	}
	return rth;
}

#else
static inline int vrf_master_ifindex_rcu(const struct net_device *dev)
{
	return 0;
}

static inline int vrf_master_ifindex(const struct net_device *dev)
{
	return 0;
}

static inline u32 vrf_dev_table_rcu(const struct net_device *dev)
{
	return 0;
}

static inline u32 vrf_dev_table(const struct net_device *dev)
{
	return 0;
}

static inline u32 vrf_dev_table_ifindex(struct net *net, int ifindex)
{
	return 0;
}

static inline u32 vrf_dev_table_rtnl(const struct net_device *dev)
{
	return 0;
}

static inline struct rtable *vrf_dev_get_rth(const struct net_device *dev)
{
	return ERR_PTR(-ENETUNREACH);
}
#endif

#endif /* __LINUX_NET_VRF_H */
