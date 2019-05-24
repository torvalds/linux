/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Generic nexthop implementation
 *
 * Copyright (c) 2017-19 Cumulus Networks
 * Copyright (c) 2017-19 David Ahern <dsa@cumulusnetworks.com>
 */

#ifndef __LINUX_NEXTHOP_H
#define __LINUX_NEXTHOP_H

#include <linux/netdevice.h>
#include <linux/types.h>
#include <net/ip_fib.h>
#include <net/ip6_fib.h>
#include <net/netlink.h>

#define NEXTHOP_VALID_USER_FLAGS RTNH_F_ONLINK

struct nexthop;

struct nh_config {
	u32		nh_id;

	u8		nh_family;
	u8		nh_protocol;
	u8		nh_blackhole;
	u32		nh_flags;

	int		nh_ifindex;
	struct net_device *dev;

	union {
		__be32		ipv4;
		struct in6_addr	ipv6;
	} gw;

	u32		nlflags;
	struct nl_info	nlinfo;
};

struct nh_info {
	struct hlist_node	dev_hash;    /* entry on netns devhash */
	struct nexthop		*nh_parent;

	u8			family;
	bool			reject_nh;

	union {
		struct fib_nh_common	fib_nhc;
		struct fib_nh		fib_nh;
		struct fib6_nh		fib6_nh;
	};
};

struct nexthop {
	struct rb_node		rb_node;    /* entry on netns rbtree */
	struct net		*net;

	u32			id;

	u8			protocol;   /* app managing this nh */
	u8			nh_flags;

	refcount_t		refcnt;
	struct rcu_head		rcu;

	union {
		struct nh_info	__rcu *nh_info;
	};
};

/* caller is holding rcu or rtnl; no reference taken to nexthop */
struct nexthop *nexthop_find_by_id(struct net *net, u32 id);
void nexthop_free_rcu(struct rcu_head *head);

static inline bool nexthop_get(struct nexthop *nh)
{
	return refcount_inc_not_zero(&nh->refcnt);
}

static inline void nexthop_put(struct nexthop *nh)
{
	if (refcount_dec_and_test(&nh->refcnt))
		call_rcu(&nh->rcu, nexthop_free_rcu);
}

/* called with rcu lock */
static inline bool nexthop_is_blackhole(const struct nexthop *nh)
{
	const struct nh_info *nhi;

	nhi = rcu_dereference(nh->nh_info);
	return nhi->reject_nh;
}
#endif
