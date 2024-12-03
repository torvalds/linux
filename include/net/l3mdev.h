/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/net/l3mdev.h - L3 master device API
 * Copyright (c) 2015 Cumulus Networks
 * Copyright (c) 2015 David Ahern <dsa@cumulusnetworks.com>
 */
#ifndef _NET_L3MDEV_H_
#define _NET_L3MDEV_H_

#include <net/dst.h>
#include <net/fib_rules.h>

enum l3mdev_type {
	L3MDEV_TYPE_UNSPEC,
	L3MDEV_TYPE_VRF,
	__L3MDEV_TYPE_MAX
};

#define L3MDEV_TYPE_MAX (__L3MDEV_TYPE_MAX - 1)

typedef int (*lookup_by_table_id_t)(struct net *net, u32 table_d);

/**
 * struct l3mdev_ops - l3mdev operations
 *
 * @l3mdev_fib_table: Get FIB table id to use for lookups
 *
 * @l3mdev_l3_rcv:    Hook in L3 receive path
 *
 * @l3mdev_l3_out:    Hook in L3 output path
 *
 * @l3mdev_link_scope_lookup: IPv6 lookup for linklocal and mcast destinations
 */

struct l3mdev_ops {
	u32		(*l3mdev_fib_table)(const struct net_device *dev);
	struct sk_buff * (*l3mdev_l3_rcv)(struct net_device *dev,
					  struct sk_buff *skb, u16 proto);
	struct sk_buff * (*l3mdev_l3_out)(struct net_device *dev,
					  struct sock *sk, struct sk_buff *skb,
					  u16 proto);

	/* IPv6 ops */
	struct dst_entry * (*l3mdev_link_scope_lookup)(const struct net_device *dev,
						 struct flowi6 *fl6);
};

#ifdef CONFIG_NET_L3_MASTER_DEV

int l3mdev_table_lookup_register(enum l3mdev_type l3type,
				 lookup_by_table_id_t fn);

void l3mdev_table_lookup_unregister(enum l3mdev_type l3type,
				    lookup_by_table_id_t fn);

int l3mdev_ifindex_lookup_by_table_id(enum l3mdev_type l3type, struct net *net,
				      u32 table_id);

int l3mdev_fib_rule_match(struct net *net, struct flowi *fl,
			  struct fib_lookup_arg *arg);

void l3mdev_update_flow(struct net *net, struct flowi *fl);

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

	if (ifindex) {
		rcu_read_lock();

		dev = dev_get_by_index_rcu(net, ifindex);
		if (dev)
			rc = l3mdev_master_ifindex_rcu(dev);

		rcu_read_unlock();
	}

	return rc;
}

static inline
struct net_device *l3mdev_master_dev_rcu(const struct net_device *_dev)
{
	/* netdev_master_upper_dev_get_rcu calls
	 * list_first_or_null_rcu to walk the upper dev list.
	 * list_first_or_null_rcu does not handle a const arg. We aren't
	 * making changes, just want the master device from that list so
	 * typecast to remove the const
	 */
	struct net_device *dev = (struct net_device *)_dev;
	struct net_device *master;

	if (!dev)
		return NULL;

	if (netif_is_l3_master(dev))
		master = dev;
	else if (netif_is_l3_slave(dev))
		master = netdev_master_upper_dev_get_rcu(dev);
	else
		master = NULL;

	return master;
}

int l3mdev_master_upper_ifindex_by_index_rcu(struct net *net, int ifindex);
static inline
int l3mdev_master_upper_ifindex_by_index(struct net *net, int ifindex)
{
	rcu_read_lock();
	ifindex = l3mdev_master_upper_ifindex_by_index_rcu(net, ifindex);
	rcu_read_unlock();

	return ifindex;
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

struct dst_entry *l3mdev_link_scope_lookup(struct net *net, struct flowi6 *fl6);

static inline
struct sk_buff *l3mdev_l3_rcv(struct sk_buff *skb, u16 proto)
{
	struct net_device *master = NULL;

	if (netif_is_l3_slave(skb->dev))
		master = netdev_master_upper_dev_get_rcu(skb->dev);
	else if (netif_is_l3_master(skb->dev) ||
		 netif_has_l3_rx_handler(skb->dev))
		master = skb->dev;

	if (master && master->l3mdev_ops->l3mdev_l3_rcv)
		skb = master->l3mdev_ops->l3mdev_l3_rcv(master, skb, proto);

	return skb;
}

static inline
struct sk_buff *l3mdev_ip_rcv(struct sk_buff *skb)
{
	return l3mdev_l3_rcv(skb, AF_INET);
}

static inline
struct sk_buff *l3mdev_ip6_rcv(struct sk_buff *skb)
{
	return l3mdev_l3_rcv(skb, AF_INET6);
}

static inline
struct sk_buff *l3mdev_l3_out(struct sock *sk, struct sk_buff *skb, u16 proto)
{
	struct net_device *dev = skb_dst(skb)->dev;

	if (netif_is_l3_slave(dev)) {
		struct net_device *master;

		master = netdev_master_upper_dev_get_rcu(dev);
		if (master && master->l3mdev_ops->l3mdev_l3_out)
			skb = master->l3mdev_ops->l3mdev_l3_out(master, sk,
								skb, proto);
	}

	return skb;
}

static inline
struct sk_buff *l3mdev_ip_out(struct sock *sk, struct sk_buff *skb)
{
	return l3mdev_l3_out(sk, skb, AF_INET);
}

static inline
struct sk_buff *l3mdev_ip6_out(struct sock *sk, struct sk_buff *skb)
{
	return l3mdev_l3_out(sk, skb, AF_INET6);
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

static inline
int l3mdev_master_upper_ifindex_by_index_rcu(struct net *net, int ifindex)
{
	return 0;
}
static inline
int l3mdev_master_upper_ifindex_by_index(struct net *net, int ifindex)
{
	return 0;
}

static inline
struct net_device *l3mdev_master_dev_rcu(const struct net_device *dev)
{
	return NULL;
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

static inline bool netif_index_is_l3_master(struct net *net, int ifindex)
{
	return false;
}

static inline
struct dst_entry *l3mdev_link_scope_lookup(struct net *net, struct flowi6 *fl6)
{
	return NULL;
}

static inline
struct sk_buff *l3mdev_ip_rcv(struct sk_buff *skb)
{
	return skb;
}

static inline
struct sk_buff *l3mdev_ip6_rcv(struct sk_buff *skb)
{
	return skb;
}

static inline
struct sk_buff *l3mdev_ip_out(struct sock *sk, struct sk_buff *skb)
{
	return skb;
}

static inline
struct sk_buff *l3mdev_ip6_out(struct sock *sk, struct sk_buff *skb)
{
	return skb;
}

static inline
int l3mdev_table_lookup_register(enum l3mdev_type l3type,
				 lookup_by_table_id_t fn)
{
	return -EOPNOTSUPP;
}

static inline
void l3mdev_table_lookup_unregister(enum l3mdev_type l3type,
				    lookup_by_table_id_t fn)
{
}

static inline
int l3mdev_ifindex_lookup_by_table_id(enum l3mdev_type l3type, struct net *net,
				      u32 table_id)
{
	return -ENODEV;
}

static inline
int l3mdev_fib_rule_match(struct net *net, struct flowi *fl,
			  struct fib_lookup_arg *arg)
{
	return 1;
}
static inline
void l3mdev_update_flow(struct net *net, struct flowi *fl)
{
}
#endif

#endif /* _NET_L3MDEV_H_ */
