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
#include <linux/route.h>
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
	struct list_head	fi_list;    /* v4 entries using nh */
	struct list_head	f6i_list;   /* v6 entries using nh */
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

static inline bool nexthop_cmp(const struct nexthop *nh1,
			       const struct nexthop *nh2)
{
	return nh1 == nh2;
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
	if (nhsel >= nhg->num_nh)
		return NULL;

	return nhg->nh_entries[nhsel].nh;
}

static inline
int nexthop_mpath_fill_node(struct sk_buff *skb, struct nexthop *nh,
			    u8 rt_family)
{
	struct nh_group *nhg = rtnl_dereference(nh->nh_grp);
	int i;

	for (i = 0; i < nhg->num_nh; i++) {
		struct nexthop *nhe = nhg->nh_entries[i].nh;
		struct nh_info *nhi = rcu_dereference_rtnl(nhe->nh_info);
		struct fib_nh_common *nhc = &nhi->fib_nhc;
		int weight = nhg->nh_entries[i].weight;

		if (fib_add_nexthop(skb, nhc, weight, rt_family) < 0)
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

static inline void nexthop_path_fib_result(struct fib_result *res, int hash)
{
	struct nh_info *nhi;
	struct nexthop *nh;

	nh = nexthop_select_path(res->fi->nh, hash);
	nhi = rcu_dereference(nh->nh_info);
	res->nhc = &nhi->fib_nhc;
}

/* called with rcu read lock or rtnl held */
static inline
struct fib_nh_common *nexthop_fib_nhc(struct nexthop *nh, int nhsel)
{
	struct nh_info *nhi;

	BUILD_BUG_ON(offsetof(struct fib_nh, nh_common) != 0);
	BUILD_BUG_ON(offsetof(struct fib6_nh, nh_common) != 0);

	if (nexthop_is_multipath(nh)) {
		nh = nexthop_mpath_select(nh, nhsel);
		if (!nh)
			return NULL;
	}

	nhi = rcu_dereference_rtnl(nh->nh_info);
	return &nhi->fib_nhc;
}

static inline unsigned int fib_info_num_path(const struct fib_info *fi)
{
	if (unlikely(fi->nh))
		return nexthop_num_path(fi->nh);

	return fi->fib_nhs;
}

int fib_check_nexthop(struct nexthop *nh, u8 scope,
		      struct netlink_ext_ack *extack);

static inline struct fib_nh_common *fib_info_nhc(struct fib_info *fi, int nhsel)
{
	if (unlikely(fi->nh))
		return nexthop_fib_nhc(fi->nh, nhsel);

	return &fi->fib_nh[nhsel].nh_common;
}

/* only used when fib_nh is built into fib_info */
static inline struct fib_nh *fib_info_nh(struct fib_info *fi, int nhsel)
{
	WARN_ON(fi->nh);

	return &fi->fib_nh[nhsel];
}

/*
 * IPv6 variants
 */
int fib6_check_nexthop(struct nexthop *nh, struct fib6_config *cfg,
		       struct netlink_ext_ack *extack);

static inline struct fib6_nh *nexthop_fib6_nh(struct nexthop *nh)
{
	struct nh_info *nhi;

	if (nexthop_is_multipath(nh)) {
		nh = nexthop_mpath_select(nh, 0);
		if (!nh)
			return NULL;
	}

	nhi = rcu_dereference_rtnl(nh->nh_info);
	if (nhi->family == AF_INET6)
		return &nhi->fib6_nh;

	return NULL;
}

static inline struct net_device *fib6_info_nh_dev(struct fib6_info *f6i)
{
	struct fib6_nh *fib6_nh;

	fib6_nh = f6i->nh ? nexthop_fib6_nh(f6i->nh) : f6i->fib6_nh;
	return fib6_nh->fib_nh_dev;
}

static inline void nexthop_path_fib6_result(struct fib6_result *res, int hash)
{
	struct nexthop *nh = res->f6i->nh;
	struct nh_info *nhi;

	nh = nexthop_select_path(nh, hash);

	nhi = rcu_dereference_rtnl(nh->nh_info);
	if (nhi->reject_nh) {
		res->fib6_type = RTN_BLACKHOLE;
		res->fib6_flags |= RTF_REJECT;
		res->nh = nexthop_fib6_nh(nh);
	} else {
		res->nh = &nhi->fib6_nh;
	}
}

int nexthop_for_each_fib6_nh(struct nexthop *nh,
			     int (*cb)(struct fib6_nh *nh, void *arg),
			     void *arg);
#endif
