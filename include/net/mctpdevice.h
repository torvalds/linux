/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Management Component Transport Protocol (MCTP) - device
 * definitions.
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#ifndef __NET_MCTPDEVICE_H
#define __NET_MCTPDEVICE_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/refcount.h>

struct mctp_sk_key;

struct mctp_dev {
	struct net_device	*dev;

	refcount_t		refs;

	unsigned int		net;
	enum mctp_phys_binding	binding;

	const struct mctp_netdev_ops *ops;

	/* Only modified under RTNL. Reads have addrs_lock held */
	u8			*addrs;
	size_t			num_addrs;
	spinlock_t		addrs_lock;

	struct rcu_head		rcu;
};

struct mctp_netdev_ops {
	void			(*release_flow)(struct mctp_dev *dev,
						struct mctp_sk_key *key);
};

#define MCTP_INITIAL_DEFAULT_NET	1

struct mctp_dev *mctp_dev_get_rtnl(const struct net_device *dev);
struct mctp_dev *__mctp_dev_get(const struct net_device *dev);

int mctp_register_netdev(struct net_device *dev,
			 const struct mctp_netdev_ops *ops,
			 enum mctp_phys_binding binding);
void mctp_unregister_netdev(struct net_device *dev);

void mctp_dev_hold(struct mctp_dev *mdev);
void mctp_dev_put(struct mctp_dev *mdev);

void mctp_dev_set_key(struct mctp_dev *dev, struct mctp_sk_key *key);
void mctp_dev_release_key(struct mctp_dev *dev, struct mctp_sk_key *key);

#endif /* __NET_MCTPDEVICE_H */
