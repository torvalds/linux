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
/* caller has already checked netif_is_l3_master(dev) */
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
static inline struct rtable *vrf_dev_get_rth(const struct net_device *dev)
{
	return ERR_PTR(-ENETUNREACH);
}
#endif

#endif /* __LINUX_NET_VRF_H */
