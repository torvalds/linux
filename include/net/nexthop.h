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

	struct nlattr	*nh_grp;
	u16		nh_grp_type;

	struct nlattr	*nh_encap;
	u16		nh_encap_type;

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

struct nh_grp_entry {
	struct nexthop	*nh;
	u8		weight;
	atomic_t	upper_bound;

	struct list_head nh_list;
	struct nexthop	*nh_parent;  /* nexthop of group with this entry */
};

struct nh_group {
	u16			num_nh;
	bool			mpath;
	bool			has_v4;
	struct nh_grp_entry	nh_entries[0];
};

struct nexthop {
	struct rb_node		rb_node;    /* entry on netns rbtree */
	struct list_head	grp_list;   /* nh group entries using this nh */
	struct net		*net;

	u32			id;

	u8			protocol;   /* app managing this nh */
	u8			nh_flags;
	bool			is_group;

	refcount_t		refcnt;
	struct rcu_head		rcu;

	union {
		struct nh_info	__rcu *nh_info;
		struct nh_group __rcu *nh_grp;
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

static inline bool nexthop_is_multipath(const struct nexthop *nh)
{
	if (nh->is_group) {
		struct nh_group *nh_grp;

		nh_grp = rcu_dereference_rtnl(nh->nh_grp);
		return nh_grp->mpath;
	}
	return false;
}

struct nexthop *nexthop_select_path(struct nexthop *nh, int hash);

static inline unsigned int nexthop_num_path(const struct nexthop *nh)
{
	unsigned int rc = 1;

	if (nexthop_is_multipath(nh)) {
		struct nh_group *nh_grp;

		nh_grp = rcu_dereference_rtnl(nh->nh_grp);
		rc = nh_grp->num_nh;
	} else {
		const struct nh_info *nhi;

		nhi = rcu_dereference_rtnl(nh->nh_info);
		if (nhi->reject_nh)
			rc = 0;
	}

	return rc;
}

static inline
struct nexthop *nexthop_mpath_select(const struct nexthop *nh, int nhsel)
{
	const struct nh_group *nhg = rcu_dereference_rtnl(nh->nh_grp);

	/* for_nexthops macros in fib_semantics.c grabs a pointer to
	 * the nexthop before checking nhsel
	 */
	if (nhsel > nhg->num_nh)
		return NULL;

	return nhg->nh_entries[nhsel].nh;
}

static inline
int nexthop_mpath_fill_node(struct sk_buff *skb, struct nexthop *nh)
{
	struct nh_group *nhg = rtnl_dereference(nh->nh_grp);
	int i;

	for (i = 0; i < nhg->num_nh; i++) {
		struct nexthop *nhe = nhg->nh_entries[i].nh;
		struct nh_info *nhi = rcu_dereference_rtnl(nhe->nh_info);
		struct fib_nh_common *nhc = &nhi->fib_nhc;
		int weight = nhg->nh_entries[i].weight;

		if (fib_add_nexthop(skb, nhc, weight) < 0)
			return -EMSGSIZE;
	}

	return 0;
}

/* called with rcu lock */
static inline bool nexthop_is_blackhole(const struct nexthop *nh)
{
	const struct nh_info *nhi;

	if (nexthop_is_multipath(nh)) {
		if (nexthop_num_path(nh) > 1)
			return false;
		nh = nexthop_mpath_select(nh, 0);
		if (!nh)
			return false;
	}

	nhi = rcu_dereference_rtnl(nh->nh_info);
	return nhi->reject_nh;
}
#endif
