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
#include <linux/notifier.h>
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
	u8		nh_fdb;
	u32		nh_flags;

	int		nh_ifindex;
	struct net_device *dev;

	union {
		__be32		ipv4;
		struct in6_addr	ipv6;
	} gw;

	struct nlattr	*nh_grp;
	u16		nh_grp_type;
	u16		nh_grp_res_num_buckets;
	unsigned long	nh_grp_res_idle_timer;
	unsigned long	nh_grp_res_unbalanced_timer;
	bool		nh_grp_res_has_num_buckets;
	bool		nh_grp_res_has_idle_timer;
	bool		nh_grp_res_has_unbalanced_timer;

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
	bool			fdb_nh;

	union {
		struct fib_nh_common	fib_nhc;
		struct fib_nh		fib_nh;
		struct fib6_nh		fib6_nh;
	};
};

struct nh_res_bucket {
	struct nh_grp_entry __rcu *nh_entry;
	atomic_long_t		used_time;
	unsigned long		migrated_time;
	bool			occupied;
	u8			nh_flags;
};

struct nh_res_table {
	struct net		*net;
	u32			nhg_id;
	struct delayed_work	upkeep_dw;

	/* List of NHGEs that have too few buckets ("uw" for underweight).
	 * Reclaimed buckets will be given to entries in this list.
	 */
	struct list_head	uw_nh_entries;
	unsigned long		unbalanced_since;

	u32			idle_timer;
	u32			unbalanced_timer;

	u16			num_nh_buckets;
	struct nh_res_bucket	nh_buckets[];
};

struct nh_grp_entry {
	struct nexthop	*nh;
	u8		weight;

	union {
		struct {
			atomic_t	upper_bound;
		} hthr;
		struct {
			/* Member on uw_nh_entries. */
			struct list_head	uw_nh_entry;

			u16			count_buckets;
			u16			wants_buckets;
		} res;
	};

	struct list_head nh_list;
	struct nexthop	*nh_parent;  /* nexthop of group with this entry */
};

struct nh_group {
	struct nh_group		*spare; /* spare group for removals */
	u16			num_nh;
	bool			is_multipath;
	bool			hash_threshold;
	bool			resilient;
	bool			fdb_nh;
	bool			has_v4;

	struct nh_res_table __rcu *res_table;
	struct nh_grp_entry	nh_entries[];
};

struct nexthop {
	struct rb_node		rb_node;    /* entry on netns rbtree */
	struct list_head	fi_list;    /* v4 entries using nh */
	struct list_head	f6i_list;   /* v6 entries using nh */
	struct list_head        fdb_list;   /* fdb entries using this nh */
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

enum nexthop_event_type {
	NEXTHOP_EVENT_DEL,
	NEXTHOP_EVENT_REPLACE,
	NEXTHOP_EVENT_RES_TABLE_PRE_REPLACE,
	NEXTHOP_EVENT_BUCKET_REPLACE,
};

enum nh_notifier_info_type {
	NH_NOTIFIER_INFO_TYPE_SINGLE,
	NH_NOTIFIER_INFO_TYPE_GRP,
	NH_NOTIFIER_INFO_TYPE_RES_TABLE,
	NH_NOTIFIER_INFO_TYPE_RES_BUCKET,
};

struct nh_notifier_single_info {
	struct net_device *dev;
	u8 gw_family;
	union {
		__be32 ipv4;
		struct in6_addr ipv6;
	};
	u8 is_reject:1,
	   is_fdb:1,
	   has_encap:1;
};

struct nh_notifier_grp_entry_info {
	u8 weight;
	u32 id;
	struct nh_notifier_single_info nh;
};

struct nh_notifier_grp_info {
	u16 num_nh;
	bool is_fdb;
	struct nh_notifier_grp_entry_info nh_entries[];
};

struct nh_notifier_res_bucket_info {
	u16 bucket_index;
	unsigned int idle_timer_ms;
	bool force;
	struct nh_notifier_single_info old_nh;
	struct nh_notifier_single_info new_nh;
};

struct nh_notifier_res_table_info {
	u16 num_nh_buckets;
	struct nh_notifier_single_info nhs[];
};

struct nh_notifier_info {
	struct net *net;
	struct netlink_ext_ack *extack;
	u32 id;
	enum nh_notifier_info_type type;
	union {
		struct nh_notifier_single_info *nh;
		struct nh_notifier_grp_info *nh_grp;
		struct nh_notifier_res_table_info *nh_res_table;
		struct nh_notifier_res_bucket_info *nh_res_bucket;
	};
};

int register_nexthop_notifier(struct net *net, struct notifier_block *nb,
			      struct netlink_ext_ack *extack);
int unregister_nexthop_notifier(struct net *net, struct notifier_block *nb);
void nexthop_set_hw_flags(struct net *net, u32 id, bool offload, bool trap);
void nexthop_bucket_set_hw_flags(struct net *net, u32 id, u16 bucket_index,
				 bool offload, bool trap);
void nexthop_res_grp_activity_update(struct net *net, u32 id, u16 num_buckets,
				     unsigned long *activity);

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

static inline bool nexthop_is_fdb(const struct nexthop *nh)
{
	if (nh->is_group) {
		const struct nh_group *nh_grp;

		nh_grp = rcu_dereference_rtnl(nh->nh_grp);
		return nh_grp->fdb_nh;
	} else {
		const struct nh_info *nhi;

		nhi = rcu_dereference_rtnl(nh->nh_info);
		return nhi->fdb_nh;
	}
}

static inline bool nexthop_has_v4(const struct nexthop *nh)
{
	if (nh->is_group) {
		struct nh_group *nh_grp;

		nh_grp = rcu_dereference_rtnl(nh->nh_grp);
		return nh_grp->has_v4;
	}
	return false;
}

static inline bool nexthop_is_multipath(const struct nexthop *nh)
{
	if (nh->is_group) {
		struct nh_group *nh_grp;

		nh_grp = rcu_dereference_rtnl(nh->nh_grp);
		return nh_grp->is_multipath;
	}
	return false;
}

struct nexthop *nexthop_select_path(struct nexthop *nh, int hash);

static inline unsigned int nexthop_num_path(const struct nexthop *nh)
{
	unsigned int rc = 1;

	if (nh->is_group) {
		struct nh_group *nh_grp;

		nh_grp = rcu_dereference_rtnl(nh->nh_grp);
		if (nh_grp->is_multipath)
			rc = nh_grp->num_nh;
	}

	return rc;
}

static inline
struct nexthop *nexthop_mpath_select(const struct nh_group *nhg, int nhsel)
{
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

		if (fib_add_nexthop(skb, nhc, weight, rt_family, 0) < 0)
			return -EMSGSIZE;
	}

	return 0;
}

/* called with rcu lock */
static inline bool nexthop_is_blackhole(const struct nexthop *nh)
{
	const struct nh_info *nhi;

	if (nh->is_group) {
		struct nh_group *nh_grp;

		nh_grp = rcu_dereference_rtnl(nh->nh_grp);
		if (nh_grp->num_nh > 1)
			return false;

		nh = nh_grp->nh_entries[0].nh;
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

	if (nh->is_group) {
		struct nh_group *nh_grp;

		nh_grp = rcu_dereference_rtnl(nh->nh_grp);
		if (nh_grp->is_multipath) {
			nh = nexthop_mpath_select(nh_grp, nhsel);
			if (!nh)
				return NULL;
		}
	}

	nhi = rcu_dereference_rtnl(nh->nh_info);
	return &nhi->fib_nhc;
}

/* called from fib_table_lookup with rcu_lock */
static inline
struct fib_nh_common *nexthop_get_nhc_lookup(const struct nexthop *nh,
					     int fib_flags,
					     const struct flowi4 *flp,
					     int *nhsel)
{
	struct nh_info *nhi;

	if (nh->is_group) {
		struct nh_group *nhg = rcu_dereference(nh->nh_grp);
		int i;

		for (i = 0; i < nhg->num_nh; i++) {
			struct nexthop *nhe = nhg->nh_entries[i].nh;

			nhi = rcu_dereference(nhe->nh_info);
			if (fib_lookup_good_nhc(&nhi->fib_nhc, fib_flags, flp)) {
				*nhsel = i;
				return &nhi->fib_nhc;
			}
		}
	} else {
		nhi = rcu_dereference(nh->nh_info);
		if (fib_lookup_good_nhc(&nhi->fib_nhc, fib_flags, flp)) {
			*nhsel = 0;
			return &nhi->fib_nhc;
		}
	}

	return NULL;
}

static inline bool nexthop_uses_dev(const struct nexthop *nh,
				    const struct net_device *dev)
{
	struct nh_info *nhi;

	if (nh->is_group) {
		struct nh_group *nhg = rcu_dereference(nh->nh_grp);
		int i;

		for (i = 0; i < nhg->num_nh; i++) {
			struct nexthop *nhe = nhg->nh_entries[i].nh;

			nhi = rcu_dereference(nhe->nh_info);
			if (nhc_l3mdev_matches_dev(&nhi->fib_nhc, dev))
				return true;
		}
	} else {
		nhi = rcu_dereference(nh->nh_info);
		if (nhc_l3mdev_matches_dev(&nhi->fib_nhc, dev))
			return true;
	}

	return false;
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

/* Caller should either hold rcu_read_lock(), or RTNL. */
static inline struct fib6_nh *nexthop_fib6_nh(struct nexthop *nh)
{
	struct nh_info *nhi;

	if (nh->is_group) {
		struct nh_group *nh_grp;

		nh_grp = rcu_dereference_rtnl(nh->nh_grp);
		nh = nexthop_mpath_select(nh_grp, 0);
		if (!nh)
			return NULL;
	}

	nhi = rcu_dereference_rtnl(nh->nh_info);
	if (nhi->family == AF_INET6)
		return &nhi->fib6_nh;

	return NULL;
}

/* Variant of nexthop_fib6_nh().
 * Caller should either hold rcu_read_lock(), or RTNL.
 */
static inline struct fib6_nh *nexthop_fib6_nh_bh(struct nexthop *nh)
{
	struct nh_info *nhi;

	if (nh->is_group) {
		struct nh_group *nh_grp;

		nh_grp = rcu_dereference_rtnl(nh->nh_grp);
		nh = nexthop_mpath_select(nh_grp, 0);
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

static inline int nexthop_get_family(struct nexthop *nh)
{
	struct nh_info *nhi = rcu_dereference_rtnl(nh->nh_info);

	return nhi->family;
}

static inline
struct fib_nh_common *nexthop_fdb_nhc(struct nexthop *nh)
{
	struct nh_info *nhi = rcu_dereference_rtnl(nh->nh_info);

	return &nhi->fib_nhc;
}

static inline struct fib_nh_common *nexthop_path_fdb_result(struct nexthop *nh,
							    int hash)
{
	struct nh_info *nhi;
	struct nexthop *nhp;

	nhp = nexthop_select_path(nh, hash);
	if (unlikely(!nhp))
		return NULL;
	nhi = rcu_dereference(nhp->nh_info);
	return &nhi->fib_nhc;
}
#endif
