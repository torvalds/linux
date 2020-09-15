// SPDX-License-Identifier: GPL-2.0
/* Generic nexthop implementation
 *
 * Copyright (c) 2017-19 Cumulus Networks
 * Copyright (c) 2017-19 David Ahern <dsa@cumulusnetworks.com>
 */

#include <linux/nexthop.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <net/arp.h>
#include <net/ipv6_stubs.h>
#include <net/lwtunnel.h>
#include <net/ndisc.h>
#include <net/nexthop.h>
#include <net/route.h>
#include <net/sock.h>

static void remove_nexthop(struct net *net, struct nexthop *nh,
			   struct nl_info *nlinfo);

#define NH_DEV_HASHBITS  8
#define NH_DEV_HASHSIZE (1U << NH_DEV_HASHBITS)

static const struct nla_policy rtm_nh_policy[NHA_MAX + 1] = {
	[NHA_ID]		= { .type = NLA_U32 },
	[NHA_GROUP]		= { .type = NLA_BINARY },
	[NHA_GROUP_TYPE]	= { .type = NLA_U16 },
	[NHA_BLACKHOLE]		= { .type = NLA_FLAG },
	[NHA_OIF]		= { .type = NLA_U32 },
	[NHA_GATEWAY]		= { .type = NLA_BINARY },
	[NHA_ENCAP_TYPE]	= { .type = NLA_U16 },
	[NHA_ENCAP]		= { .type = NLA_NESTED },
	[NHA_GROUPS]		= { .type = NLA_FLAG },
	[NHA_MASTER]		= { .type = NLA_U32 },
	[NHA_FDB]		= { .type = NLA_FLAG },
};

static int call_nexthop_notifiers(struct net *net,
				  enum nexthop_event_type event_type,
				  struct nexthop *nh)
{
	int err;

	err = blocking_notifier_call_chain(&net->nexthop.notifier_chain,
					   event_type, nh);
	return notifier_to_errno(err);
}

static unsigned int nh_dev_hashfn(unsigned int val)
{
	unsigned int mask = NH_DEV_HASHSIZE - 1;

	return (val ^
		(val >> NH_DEV_HASHBITS) ^
		(val >> (NH_DEV_HASHBITS * 2))) & mask;
}

static void nexthop_devhash_add(struct net *net, struct nh_info *nhi)
{
	struct net_device *dev = nhi->fib_nhc.nhc_dev;
	struct hlist_head *head;
	unsigned int hash;

	WARN_ON(!dev);

	hash = nh_dev_hashfn(dev->ifindex);
	head = &net->nexthop.devhash[hash];
	hlist_add_head(&nhi->dev_hash, head);
}

static void nexthop_free_mpath(struct nexthop *nh)
{
	struct nh_group *nhg;
	int i;

	nhg = rcu_dereference_raw(nh->nh_grp);
	for (i = 0; i < nhg->num_nh; ++i) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];

		WARN_ON(!list_empty(&nhge->nh_list));
		nexthop_put(nhge->nh);
	}

	WARN_ON(nhg->spare == nhg);

	kfree(nhg->spare);
	kfree(nhg);
}

static void nexthop_free_single(struct nexthop *nh)
{
	struct nh_info *nhi;

	nhi = rcu_dereference_raw(nh->nh_info);
	switch (nhi->family) {
	case AF_INET:
		fib_nh_release(nh->net, &nhi->fib_nh);
		break;
	case AF_INET6:
		ipv6_stub->fib6_nh_release(&nhi->fib6_nh);
		break;
	}
	kfree(nhi);
}

void nexthop_free_rcu(struct rcu_head *head)
{
	struct nexthop *nh = container_of(head, struct nexthop, rcu);

	if (nh->is_group)
		nexthop_free_mpath(nh);
	else
		nexthop_free_single(nh);

	kfree(nh);
}
EXPORT_SYMBOL_GPL(nexthop_free_rcu);

static struct nexthop *nexthop_alloc(void)
{
	struct nexthop *nh;

	nh = kzalloc(sizeof(struct nexthop), GFP_KERNEL);
	if (nh) {
		INIT_LIST_HEAD(&nh->fi_list);
		INIT_LIST_HEAD(&nh->f6i_list);
		INIT_LIST_HEAD(&nh->grp_list);
		INIT_LIST_HEAD(&nh->fdb_list);
	}
	return nh;
}

static struct nh_group *nexthop_grp_alloc(u16 num_nh)
{
	struct nh_group *nhg;

	nhg = kzalloc(struct_size(nhg, nh_entries, num_nh), GFP_KERNEL);
	if (nhg)
		nhg->num_nh = num_nh;

	return nhg;
}

static void nh_base_seq_inc(struct net *net)
{
	while (++net->nexthop.seq == 0)
		;
}

/* no reference taken; rcu lock or rtnl must be held */
struct nexthop *nexthop_find_by_id(struct net *net, u32 id)
{
	struct rb_node **pp, *parent = NULL, *next;

	pp = &net->nexthop.rb_root.rb_node;
	while (1) {
		struct nexthop *nh;

		next = rcu_dereference_raw(*pp);
		if (!next)
			break;
		parent = next;

		nh = rb_entry(parent, struct nexthop, rb_node);
		if (id < nh->id)
			pp = &next->rb_left;
		else if (id > nh->id)
			pp = &next->rb_right;
		else
			return nh;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(nexthop_find_by_id);

/* used for auto id allocation; called with rtnl held */
static u32 nh_find_unused_id(struct net *net)
{
	u32 id_start = net->nexthop.last_id_allocated;

	while (1) {
		net->nexthop.last_id_allocated++;
		if (net->nexthop.last_id_allocated == id_start)
			break;

		if (!nexthop_find_by_id(net, net->nexthop.last_id_allocated))
			return net->nexthop.last_id_allocated;
	}
	return 0;
}

static int nla_put_nh_group(struct sk_buff *skb, struct nh_group *nhg)
{
	struct nexthop_grp *p;
	size_t len = nhg->num_nh * sizeof(*p);
	struct nlattr *nla;
	u16 group_type = 0;
	int i;

	if (nhg->mpath)
		group_type = NEXTHOP_GRP_TYPE_MPATH;

	if (nla_put_u16(skb, NHA_GROUP_TYPE, group_type))
		goto nla_put_failure;

	nla = nla_reserve(skb, NHA_GROUP, len);
	if (!nla)
		goto nla_put_failure;

	p = nla_data(nla);
	for (i = 0; i < nhg->num_nh; ++i) {
		p->id = nhg->nh_entries[i].nh->id;
		p->weight = nhg->nh_entries[i].weight - 1;
		p += 1;
	}

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int nh_fill_node(struct sk_buff *skb, struct nexthop *nh,
			int event, u32 portid, u32 seq, unsigned int nlflags)
{
	struct fib6_nh *fib6_nh;
	struct fib_nh *fib_nh;
	struct nlmsghdr *nlh;
	struct nh_info *nhi;
	struct nhmsg *nhm;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nhm), nlflags);
	if (!nlh)
		return -EMSGSIZE;

	nhm = nlmsg_data(nlh);
	nhm->nh_family = AF_UNSPEC;
	nhm->nh_flags = nh->nh_flags;
	nhm->nh_protocol = nh->protocol;
	nhm->nh_scope = 0;
	nhm->resvd = 0;

	if (nla_put_u32(skb, NHA_ID, nh->id))
		goto nla_put_failure;

	if (nh->is_group) {
		struct nh_group *nhg = rtnl_dereference(nh->nh_grp);

		if (nhg->fdb_nh && nla_put_flag(skb, NHA_FDB))
			goto nla_put_failure;
		if (nla_put_nh_group(skb, nhg))
			goto nla_put_failure;
		goto out;
	}

	nhi = rtnl_dereference(nh->nh_info);
	nhm->nh_family = nhi->family;
	if (nhi->reject_nh) {
		if (nla_put_flag(skb, NHA_BLACKHOLE))
			goto nla_put_failure;
		goto out;
	} else if (nhi->fdb_nh) {
		if (nla_put_flag(skb, NHA_FDB))
			goto nla_put_failure;
	} else {
		const struct net_device *dev;

		dev = nhi->fib_nhc.nhc_dev;
		if (dev && nla_put_u32(skb, NHA_OIF, dev->ifindex))
			goto nla_put_failure;
	}

	nhm->nh_scope = nhi->fib_nhc.nhc_scope;
	switch (nhi->family) {
	case AF_INET:
		fib_nh = &nhi->fib_nh;
		if (fib_nh->fib_nh_gw_family &&
		    nla_put_be32(skb, NHA_GATEWAY, fib_nh->fib_nh_gw4))
			goto nla_put_failure;
		break;

	case AF_INET6:
		fib6_nh = &nhi->fib6_nh;
		if (fib6_nh->fib_nh_gw_family &&
		    nla_put_in6_addr(skb, NHA_GATEWAY, &fib6_nh->fib_nh_gw6))
			goto nla_put_failure;
		break;
	}

	if (nhi->fib_nhc.nhc_lwtstate &&
	    lwtunnel_fill_encap(skb, nhi->fib_nhc.nhc_lwtstate,
				NHA_ENCAP, NHA_ENCAP_TYPE) < 0)
		goto nla_put_failure;

out:
	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static size_t nh_nlmsg_size_grp(struct nexthop *nh)
{
	struct nh_group *nhg = rtnl_dereference(nh->nh_grp);
	size_t sz = sizeof(struct nexthop_grp) * nhg->num_nh;

	return nla_total_size(sz) +
	       nla_total_size(2);  /* NHA_GROUP_TYPE */
}

static size_t nh_nlmsg_size_single(struct nexthop *nh)
{
	struct nh_info *nhi = rtnl_dereference(nh->nh_info);
	size_t sz;

	/* covers NHA_BLACKHOLE since NHA_OIF and BLACKHOLE
	 * are mutually exclusive
	 */
	sz = nla_total_size(4);  /* NHA_OIF */

	switch (nhi->family) {
	case AF_INET:
		if (nhi->fib_nh.fib_nh_gw_family)
			sz += nla_total_size(4);  /* NHA_GATEWAY */
		break;

	case AF_INET6:
		/* NHA_GATEWAY */
		if (nhi->fib6_nh.fib_nh_gw_family)
			sz += nla_total_size(sizeof(const struct in6_addr));
		break;
	}

	if (nhi->fib_nhc.nhc_lwtstate) {
		sz += lwtunnel_get_encap_size(nhi->fib_nhc.nhc_lwtstate);
		sz += nla_total_size(2);  /* NHA_ENCAP_TYPE */
	}

	return sz;
}

static size_t nh_nlmsg_size(struct nexthop *nh)
{
	size_t sz = NLMSG_ALIGN(sizeof(struct nhmsg));

	sz += nla_total_size(4); /* NHA_ID */

	if (nh->is_group)
		sz += nh_nlmsg_size_grp(nh);
	else
		sz += nh_nlmsg_size_single(nh);

	return sz;
}

static void nexthop_notify(int event, struct nexthop *nh, struct nl_info *info)
{
	unsigned int nlflags = info->nlh ? info->nlh->nlmsg_flags : 0;
	u32 seq = info->nlh ? info->nlh->nlmsg_seq : 0;
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(nh_nlmsg_size(nh), gfp_any());
	if (!skb)
		goto errout;

	err = nh_fill_node(skb, nh, event, info->portid, seq, nlflags);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in nh_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, info->nl_net, info->portid, RTNLGRP_NEXTHOP,
		    info->nlh, gfp_any());
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(info->nl_net, RTNLGRP_NEXTHOP, err);
}

static bool valid_group_nh(struct nexthop *nh, unsigned int npaths,
			   bool *is_fdb, struct netlink_ext_ack *extack)
{
	if (nh->is_group) {
		struct nh_group *nhg = rtnl_dereference(nh->nh_grp);

		/* nested multipath (group within a group) is not
		 * supported
		 */
		if (nhg->mpath) {
			NL_SET_ERR_MSG(extack,
				       "Multipath group can not be a nexthop within a group");
			return false;
		}
		*is_fdb = nhg->fdb_nh;
	} else {
		struct nh_info *nhi = rtnl_dereference(nh->nh_info);

		if (nhi->reject_nh && npaths > 1) {
			NL_SET_ERR_MSG(extack,
				       "Blackhole nexthop can not be used in a group with more than 1 path");
			return false;
		}
		*is_fdb = nhi->fdb_nh;
	}

	return true;
}

static int nh_check_attr_fdb_group(struct nexthop *nh, u8 *nh_family,
				   struct netlink_ext_ack *extack)
{
	struct nh_info *nhi;

	nhi = rtnl_dereference(nh->nh_info);

	if (!nhi->fdb_nh) {
		NL_SET_ERR_MSG(extack, "FDB nexthop group can only have fdb nexthops");
		return -EINVAL;
	}

	if (*nh_family == AF_UNSPEC) {
		*nh_family = nhi->family;
	} else if (*nh_family != nhi->family) {
		NL_SET_ERR_MSG(extack, "FDB nexthop group cannot have mixed family nexthops");
		return -EINVAL;
	}

	return 0;
}

static int nh_check_attr_group(struct net *net, struct nlattr *tb[],
			       struct netlink_ext_ack *extack)
{
	unsigned int len = nla_len(tb[NHA_GROUP]);
	u8 nh_family = AF_UNSPEC;
	struct nexthop_grp *nhg;
	unsigned int i, j;
	u8 nhg_fdb = 0;

	if (!len || len & (sizeof(struct nexthop_grp) - 1)) {
		NL_SET_ERR_MSG(extack,
			       "Invalid length for nexthop group attribute");
		return -EINVAL;
	}

	/* convert len to number of nexthop ids */
	len /= sizeof(*nhg);

	nhg = nla_data(tb[NHA_GROUP]);
	for (i = 0; i < len; ++i) {
		if (nhg[i].resvd1 || nhg[i].resvd2) {
			NL_SET_ERR_MSG(extack, "Reserved fields in nexthop_grp must be 0");
			return -EINVAL;
		}
		if (nhg[i].weight > 254) {
			NL_SET_ERR_MSG(extack, "Invalid value for weight");
			return -EINVAL;
		}
		for (j = i + 1; j < len; ++j) {
			if (nhg[i].id == nhg[j].id) {
				NL_SET_ERR_MSG(extack, "Nexthop id can not be used twice in a group");
				return -EINVAL;
			}
		}
	}

	if (tb[NHA_FDB])
		nhg_fdb = 1;
	nhg = nla_data(tb[NHA_GROUP]);
	for (i = 0; i < len; ++i) {
		struct nexthop *nh;
		bool is_fdb_nh;

		nh = nexthop_find_by_id(net, nhg[i].id);
		if (!nh) {
			NL_SET_ERR_MSG(extack, "Invalid nexthop id");
			return -EINVAL;
		}
		if (!valid_group_nh(nh, len, &is_fdb_nh, extack))
			return -EINVAL;

		if (nhg_fdb && nh_check_attr_fdb_group(nh, &nh_family, extack))
			return -EINVAL;

		if (!nhg_fdb && is_fdb_nh) {
			NL_SET_ERR_MSG(extack, "Non FDB nexthop group cannot have fdb nexthops");
			return -EINVAL;
		}
	}
	for (i = NHA_GROUP_TYPE + 1; i < __NHA_MAX; ++i) {
		if (!tb[i])
			continue;
		if (tb[NHA_FDB])
			continue;
		NL_SET_ERR_MSG(extack,
			       "No other attributes can be set in nexthop groups");
		return -EINVAL;
	}

	return 0;
}

static bool ipv6_good_nh(const struct fib6_nh *nh)
{
	int state = NUD_REACHABLE;
	struct neighbour *n;

	rcu_read_lock_bh();

	n = __ipv6_neigh_lookup_noref_stub(nh->fib_nh_dev, &nh->fib_nh_gw6);
	if (n)
		state = n->nud_state;

	rcu_read_unlock_bh();

	return !!(state & NUD_VALID);
}

static bool ipv4_good_nh(const struct fib_nh *nh)
{
	int state = NUD_REACHABLE;
	struct neighbour *n;

	rcu_read_lock_bh();

	n = __ipv4_neigh_lookup_noref(nh->fib_nh_dev,
				      (__force u32)nh->fib_nh_gw4);
	if (n)
		state = n->nud_state;

	rcu_read_unlock_bh();

	return !!(state & NUD_VALID);
}

struct nexthop *nexthop_select_path(struct nexthop *nh, int hash)
{
	struct nexthop *rc = NULL;
	struct nh_group *nhg;
	int i;

	if (!nh->is_group)
		return nh;

	nhg = rcu_dereference(nh->nh_grp);
	for (i = 0; i < nhg->num_nh; ++i) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];
		struct nh_info *nhi;

		if (hash > atomic_read(&nhge->upper_bound))
			continue;

		nhi = rcu_dereference(nhge->nh->nh_info);
		if (nhi->fdb_nh)
			return nhge->nh;

		/* nexthops always check if it is good and does
		 * not rely on a sysctl for this behavior
		 */
		switch (nhi->family) {
		case AF_INET:
			if (ipv4_good_nh(&nhi->fib_nh))
				return nhge->nh;
			break;
		case AF_INET6:
			if (ipv6_good_nh(&nhi->fib6_nh))
				return nhge->nh;
			break;
		}

		if (!rc)
			rc = nhge->nh;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(nexthop_select_path);

int nexthop_for_each_fib6_nh(struct nexthop *nh,
			     int (*cb)(struct fib6_nh *nh, void *arg),
			     void *arg)
{
	struct nh_info *nhi;
	int err;

	if (nh->is_group) {
		struct nh_group *nhg;
		int i;

		nhg = rcu_dereference_rtnl(nh->nh_grp);
		for (i = 0; i < nhg->num_nh; i++) {
			struct nh_grp_entry *nhge = &nhg->nh_entries[i];

			nhi = rcu_dereference_rtnl(nhge->nh->nh_info);
			err = cb(&nhi->fib6_nh, arg);
			if (err)
				return err;
		}
	} else {
		nhi = rcu_dereference_rtnl(nh->nh_info);
		err = cb(&nhi->fib6_nh, arg);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nexthop_for_each_fib6_nh);

static int check_src_addr(const struct in6_addr *saddr,
			  struct netlink_ext_ack *extack)
{
	if (!ipv6_addr_any(saddr)) {
		NL_SET_ERR_MSG(extack, "IPv6 routes using source address can not use nexthop objects");
		return -EINVAL;
	}
	return 0;
}

int fib6_check_nexthop(struct nexthop *nh, struct fib6_config *cfg,
		       struct netlink_ext_ack *extack)
{
	struct nh_info *nhi;
	bool is_fdb_nh;

	/* fib6_src is unique to a fib6_info and limits the ability to cache
	 * routes in fib6_nh within a nexthop that is potentially shared
	 * across multiple fib entries. If the config wants to use source
	 * routing it can not use nexthop objects. mlxsw also does not allow
	 * fib6_src on routes.
	 */
	if (cfg && check_src_addr(&cfg->fc_src, extack) < 0)
		return -EINVAL;

	if (nh->is_group) {
		struct nh_group *nhg;

		nhg = rtnl_dereference(nh->nh_grp);
		if (nhg->has_v4)
			goto no_v4_nh;
		is_fdb_nh = nhg->fdb_nh;
	} else {
		nhi = rtnl_dereference(nh->nh_info);
		if (nhi->family == AF_INET)
			goto no_v4_nh;
		is_fdb_nh = nhi->fdb_nh;
	}

	if (is_fdb_nh) {
		NL_SET_ERR_MSG(extack, "Route cannot point to a fdb nexthop");
		return -EINVAL;
	}

	return 0;
no_v4_nh:
	NL_SET_ERR_MSG(extack, "IPv6 routes can not use an IPv4 nexthop");
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(fib6_check_nexthop);

/* if existing nexthop has ipv6 routes linked to it, need
 * to verify this new spec works with ipv6
 */
static int fib6_check_nh_list(struct nexthop *old, struct nexthop *new,
			      struct netlink_ext_ack *extack)
{
	struct fib6_info *f6i;

	if (list_empty(&old->f6i_list))
		return 0;

	list_for_each_entry(f6i, &old->f6i_list, nh_list) {
		if (check_src_addr(&f6i->fib6_src.addr, extack) < 0)
			return -EINVAL;
	}

	return fib6_check_nexthop(new, NULL, extack);
}

static int nexthop_check_scope(struct nh_info *nhi, u8 scope,
			       struct netlink_ext_ack *extack)
{
	if (scope == RT_SCOPE_HOST && nhi->fib_nhc.nhc_gw_family) {
		NL_SET_ERR_MSG(extack,
			       "Route with host scope can not have a gateway");
		return -EINVAL;
	}

	if (nhi->fib_nhc.nhc_flags & RTNH_F_ONLINK && scope >= RT_SCOPE_LINK) {
		NL_SET_ERR_MSG(extack, "Scope mismatch with nexthop");
		return -EINVAL;
	}

	return 0;
}

/* Invoked by fib add code to verify nexthop by id is ok with
 * config for prefix; parts of fib_check_nh not done when nexthop
 * object is used.
 */
int fib_check_nexthop(struct nexthop *nh, u8 scope,
		      struct netlink_ext_ack *extack)
{
	struct nh_info *nhi;
	int err = 0;

	if (nh->is_group) {
		struct nh_group *nhg;

		nhg = rtnl_dereference(nh->nh_grp);
		if (nhg->fdb_nh) {
			NL_SET_ERR_MSG(extack, "Route cannot point to a fdb nexthop");
			err = -EINVAL;
			goto out;
		}

		if (scope == RT_SCOPE_HOST) {
			NL_SET_ERR_MSG(extack, "Route with host scope can not have multiple nexthops");
			err = -EINVAL;
			goto out;
		}

		/* all nexthops in a group have the same scope */
		nhi = rtnl_dereference(nhg->nh_entries[0].nh->nh_info);
		err = nexthop_check_scope(nhi, scope, extack);
	} else {
		nhi = rtnl_dereference(nh->nh_info);
		if (nhi->fdb_nh) {
			NL_SET_ERR_MSG(extack, "Route cannot point to a fdb nexthop");
			err = -EINVAL;
			goto out;
		}
		err = nexthop_check_scope(nhi, scope, extack);
	}

out:
	return err;
}

static int fib_check_nh_list(struct nexthop *old, struct nexthop *new,
			     struct netlink_ext_ack *extack)
{
	struct fib_info *fi;

	list_for_each_entry(fi, &old->fi_list, nh_list) {
		int err;

		err = fib_check_nexthop(new, fi->fib_scope, extack);
		if (err)
			return err;
	}
	return 0;
}

static void nh_group_rebalance(struct nh_group *nhg)
{
	int total = 0;
	int w = 0;
	int i;

	for (i = 0; i < nhg->num_nh; ++i)
		total += nhg->nh_entries[i].weight;

	for (i = 0; i < nhg->num_nh; ++i) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];
		int upper_bound;

		w += nhge->weight;
		upper_bound = DIV_ROUND_CLOSEST_ULL((u64)w << 31, total) - 1;
		atomic_set(&nhge->upper_bound, upper_bound);
	}
}

static void remove_nh_grp_entry(struct net *net, struct nh_grp_entry *nhge,
				struct nl_info *nlinfo)
{
	struct nh_grp_entry *nhges, *new_nhges;
	struct nexthop *nhp = nhge->nh_parent;
	struct nexthop *nh = nhge->nh;
	struct nh_group *nhg, *newg;
	int i, j;

	WARN_ON(!nh);

	nhg = rtnl_dereference(nhp->nh_grp);
	newg = nhg->spare;

	/* last entry, keep it visible and remove the parent */
	if (nhg->num_nh == 1) {
		remove_nexthop(net, nhp, nlinfo);
		return;
	}

	newg->has_v4 = false;
	newg->mpath = nhg->mpath;
	newg->fdb_nh = nhg->fdb_nh;
	newg->num_nh = nhg->num_nh;

	/* copy old entries to new except the one getting removed */
	nhges = nhg->nh_entries;
	new_nhges = newg->nh_entries;
	for (i = 0, j = 0; i < nhg->num_nh; ++i) {
		struct nh_info *nhi;

		/* current nexthop getting removed */
		if (nhg->nh_entries[i].nh == nh) {
			newg->num_nh--;
			continue;
		}

		nhi = rtnl_dereference(nhges[i].nh->nh_info);
		if (nhi->family == AF_INET)
			newg->has_v4 = true;

		list_del(&nhges[i].nh_list);
		new_nhges[j].nh_parent = nhges[i].nh_parent;
		new_nhges[j].nh = nhges[i].nh;
		new_nhges[j].weight = nhges[i].weight;
		list_add(&new_nhges[j].nh_list, &new_nhges[j].nh->grp_list);
		j++;
	}

	nh_group_rebalance(newg);
	rcu_assign_pointer(nhp->nh_grp, newg);

	list_del(&nhge->nh_list);
	nexthop_put(nhge->nh);

	if (nlinfo)
		nexthop_notify(RTM_NEWNEXTHOP, nhp, nlinfo);
}

static void remove_nexthop_from_groups(struct net *net, struct nexthop *nh,
				       struct nl_info *nlinfo)
{
	struct nh_grp_entry *nhge, *tmp;

	list_for_each_entry_safe(nhge, tmp, &nh->grp_list, nh_list)
		remove_nh_grp_entry(net, nhge, nlinfo);

	/* make sure all see the newly published array before releasing rtnl */
	synchronize_rcu();
}

static void remove_nexthop_group(struct nexthop *nh, struct nl_info *nlinfo)
{
	struct nh_group *nhg = rcu_dereference_rtnl(nh->nh_grp);
	int i, num_nh = nhg->num_nh;

	for (i = 0; i < num_nh; ++i) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];

		if (WARN_ON(!nhge->nh))
			continue;

		list_del_init(&nhge->nh_list);
	}
}

/* not called for nexthop replace */
static void __remove_nexthop_fib(struct net *net, struct nexthop *nh)
{
	struct fib6_info *f6i, *tmp;
	bool do_flush = false;
	struct fib_info *fi;

	list_for_each_entry(fi, &nh->fi_list, nh_list) {
		fi->fib_flags |= RTNH_F_DEAD;
		do_flush = true;
	}
	if (do_flush)
		fib_flush(net);

	/* ip6_del_rt removes the entry from this list hence the _safe */
	list_for_each_entry_safe(f6i, tmp, &nh->f6i_list, nh_list) {
		/* __ip6_del_rt does a release, so do a hold here */
		fib6_info_hold(f6i);
		ipv6_stub->ip6_del_rt(net, f6i,
				      !net->ipv4.sysctl_nexthop_compat_mode);
	}
}

static void __remove_nexthop(struct net *net, struct nexthop *nh,
			     struct nl_info *nlinfo)
{
	__remove_nexthop_fib(net, nh);

	if (nh->is_group) {
		remove_nexthop_group(nh, nlinfo);
	} else {
		struct nh_info *nhi;

		nhi = rtnl_dereference(nh->nh_info);
		if (nhi->fib_nhc.nhc_dev)
			hlist_del(&nhi->dev_hash);

		remove_nexthop_from_groups(net, nh, nlinfo);
	}
}

static void remove_nexthop(struct net *net, struct nexthop *nh,
			   struct nl_info *nlinfo)
{
	call_nexthop_notifiers(net, NEXTHOP_EVENT_DEL, nh);

	/* remove from the tree */
	rb_erase(&nh->rb_node, &net->nexthop.rb_root);

	if (nlinfo)
		nexthop_notify(RTM_DELNEXTHOP, nh, nlinfo);

	__remove_nexthop(net, nh, nlinfo);
	nh_base_seq_inc(net);

	nexthop_put(nh);
}

/* if any FIB entries reference this nexthop, any dst entries
 * need to be regenerated
 */
static void nh_rt_cache_flush(struct net *net, struct nexthop *nh)
{
	struct fib6_info *f6i;

	if (!list_empty(&nh->fi_list))
		rt_cache_flush(net);

	list_for_each_entry(f6i, &nh->f6i_list, nh_list)
		ipv6_stub->fib6_update_sernum(net, f6i);
}

static int replace_nexthop_grp(struct net *net, struct nexthop *old,
			       struct nexthop *new,
			       struct netlink_ext_ack *extack)
{
	struct nh_group *oldg, *newg;
	int i;

	if (!new->is_group) {
		NL_SET_ERR_MSG(extack, "Can not replace a nexthop group with a nexthop.");
		return -EINVAL;
	}

	oldg = rtnl_dereference(old->nh_grp);
	newg = rtnl_dereference(new->nh_grp);

	/* update parents - used by nexthop code for cleanup */
	for (i = 0; i < newg->num_nh; i++)
		newg->nh_entries[i].nh_parent = old;

	rcu_assign_pointer(old->nh_grp, newg);

	for (i = 0; i < oldg->num_nh; i++)
		oldg->nh_entries[i].nh_parent = new;

	rcu_assign_pointer(new->nh_grp, oldg);

	return 0;
}

static void nh_group_v4_update(struct nh_group *nhg)
{
	struct nh_grp_entry *nhges;
	bool has_v4 = false;
	int i;

	nhges = nhg->nh_entries;
	for (i = 0; i < nhg->num_nh; i++) {
		struct nh_info *nhi;

		nhi = rtnl_dereference(nhges[i].nh->nh_info);
		if (nhi->family == AF_INET)
			has_v4 = true;
	}
	nhg->has_v4 = has_v4;
}

static int replace_nexthop_single(struct net *net, struct nexthop *old,
				  struct nexthop *new,
				  struct netlink_ext_ack *extack)
{
	struct nh_info *oldi, *newi;

	if (new->is_group) {
		NL_SET_ERR_MSG(extack, "Can not replace a nexthop with a nexthop group.");
		return -EINVAL;
	}

	oldi = rtnl_dereference(old->nh_info);
	newi = rtnl_dereference(new->nh_info);

	newi->nh_parent = old;
	oldi->nh_parent = new;

	old->protocol = new->protocol;
	old->nh_flags = new->nh_flags;

	rcu_assign_pointer(old->nh_info, newi);
	rcu_assign_pointer(new->nh_info, oldi);

	/* When replacing an IPv4 nexthop with an IPv6 nexthop, potentially
	 * update IPv4 indication in all the groups using the nexthop.
	 */
	if (oldi->family == AF_INET && newi->family == AF_INET6) {
		struct nh_grp_entry *nhge;

		list_for_each_entry(nhge, &old->grp_list, nh_list) {
			struct nexthop *nhp = nhge->nh_parent;
			struct nh_group *nhg;

			nhg = rtnl_dereference(nhp->nh_grp);
			nh_group_v4_update(nhg);
		}
	}

	return 0;
}

static void __nexthop_replace_notify(struct net *net, struct nexthop *nh,
				     struct nl_info *info)
{
	struct fib6_info *f6i;

	if (!list_empty(&nh->fi_list)) {
		struct fib_info *fi;

		/* expectation is a few fib_info per nexthop and then
		 * a lot of routes per fib_info. So mark the fib_info
		 * and then walk the fib tables once
		 */
		list_for_each_entry(fi, &nh->fi_list, nh_list)
			fi->nh_updated = true;

		fib_info_notify_update(net, info);

		list_for_each_entry(fi, &nh->fi_list, nh_list)
			fi->nh_updated = false;
	}

	list_for_each_entry(f6i, &nh->f6i_list, nh_list)
		ipv6_stub->fib6_rt_update(net, f6i, info);
}

/* send RTM_NEWROUTE with REPLACE flag set for all FIB entries
 * linked to this nexthop and for all groups that the nexthop
 * is a member of
 */
static void nexthop_replace_notify(struct net *net, struct nexthop *nh,
				   struct nl_info *info)
{
	struct nh_grp_entry *nhge;

	__nexthop_replace_notify(net, nh, info);

	list_for_each_entry(nhge, &nh->grp_list, nh_list)
		__nexthop_replace_notify(net, nhge->nh_parent, info);
}

static int replace_nexthop(struct net *net, struct nexthop *old,
			   struct nexthop *new, struct netlink_ext_ack *extack)
{
	bool new_is_reject = false;
	struct nh_grp_entry *nhge;
	int err;

	/* check that existing FIB entries are ok with the
	 * new nexthop definition
	 */
	err = fib_check_nh_list(old, new, extack);
	if (err)
		return err;

	err = fib6_check_nh_list(old, new, extack);
	if (err)
		return err;

	if (!new->is_group) {
		struct nh_info *nhi = rtnl_dereference(new->nh_info);

		new_is_reject = nhi->reject_nh;
	}

	list_for_each_entry(nhge, &old->grp_list, nh_list) {
		/* if new nexthop is a blackhole, any groups using this
		 * nexthop cannot have more than 1 path
		 */
		if (new_is_reject &&
		    nexthop_num_path(nhge->nh_parent) > 1) {
			NL_SET_ERR_MSG(extack, "Blackhole nexthop can not be a member of a group with more than one path");
			return -EINVAL;
		}

		err = fib_check_nh_list(nhge->nh_parent, new, extack);
		if (err)
			return err;

		err = fib6_check_nh_list(nhge->nh_parent, new, extack);
		if (err)
			return err;
	}

	if (old->is_group)
		err = replace_nexthop_grp(net, old, new, extack);
	else
		err = replace_nexthop_single(net, old, new, extack);

	if (!err) {
		nh_rt_cache_flush(net, old);

		__remove_nexthop(net, new, NULL);
		nexthop_put(new);
	}

	return err;
}

/* called with rtnl_lock held */
static int insert_nexthop(struct net *net, struct nexthop *new_nh,
			  struct nh_config *cfg, struct netlink_ext_ack *extack)
{
	struct rb_node **pp, *parent = NULL, *next;
	struct rb_root *root = &net->nexthop.rb_root;
	bool replace = !!(cfg->nlflags & NLM_F_REPLACE);
	bool create = !!(cfg->nlflags & NLM_F_CREATE);
	u32 new_id = new_nh->id;
	int replace_notify = 0;
	int rc = -EEXIST;

	pp = &root->rb_node;
	while (1) {
		struct nexthop *nh;

		next = *pp;
		if (!next)
			break;

		parent = next;

		nh = rb_entry(parent, struct nexthop, rb_node);
		if (new_id < nh->id) {
			pp = &next->rb_left;
		} else if (new_id > nh->id) {
			pp = &next->rb_right;
		} else if (replace) {
			rc = replace_nexthop(net, nh, new_nh, extack);
			if (!rc) {
				new_nh = nh; /* send notification with old nh */
				replace_notify = 1;
			}
			goto out;
		} else {
			/* id already exists and not a replace */
			goto out;
		}
	}

	if (replace && !create) {
		NL_SET_ERR_MSG(extack, "Replace specified without create and no entry exists");
		rc = -ENOENT;
		goto out;
	}

	rb_link_node_rcu(&new_nh->rb_node, parent, pp);
	rb_insert_color(&new_nh->rb_node, root);
	rc = 0;
out:
	if (!rc) {
		nh_base_seq_inc(net);
		nexthop_notify(RTM_NEWNEXTHOP, new_nh, &cfg->nlinfo);
		if (replace_notify && net->ipv4.sysctl_nexthop_compat_mode)
			nexthop_replace_notify(net, new_nh, &cfg->nlinfo);
	}

	return rc;
}

/* rtnl */
/* remove all nexthops tied to a device being deleted */
static void nexthop_flush_dev(struct net_device *dev)
{
	unsigned int hash = nh_dev_hashfn(dev->ifindex);
	struct net *net = dev_net(dev);
	struct hlist_head *head = &net->nexthop.devhash[hash];
	struct hlist_node *n;
	struct nh_info *nhi;

	hlist_for_each_entry_safe(nhi, n, head, dev_hash) {
		if (nhi->fib_nhc.nhc_dev != dev)
			continue;

		remove_nexthop(net, nhi->nh_parent, NULL);
	}
}

/* rtnl; called when net namespace is deleted */
static void flush_all_nexthops(struct net *net)
{
	struct rb_root *root = &net->nexthop.rb_root;
	struct rb_node *node;
	struct nexthop *nh;

	while ((node = rb_first(root))) {
		nh = rb_entry(node, struct nexthop, rb_node);
		remove_nexthop(net, nh, NULL);
		cond_resched();
	}
}

static struct nexthop *nexthop_create_group(struct net *net,
					    struct nh_config *cfg)
{
	struct nlattr *grps_attr = cfg->nh_grp;
	struct nexthop_grp *entry = nla_data(grps_attr);
	u16 num_nh = nla_len(grps_attr) / sizeof(*entry);
	struct nh_group *nhg;
	struct nexthop *nh;
	int i;

	if (WARN_ON(!num_nh))
		return ERR_PTR(-EINVAL);

	nh = nexthop_alloc();
	if (!nh)
		return ERR_PTR(-ENOMEM);

	nh->is_group = 1;

	nhg = nexthop_grp_alloc(num_nh);
	if (!nhg) {
		kfree(nh);
		return ERR_PTR(-ENOMEM);
	}

	/* spare group used for removals */
	nhg->spare = nexthop_grp_alloc(num_nh);
	if (!nhg->spare) {
		kfree(nhg);
		kfree(nh);
		return ERR_PTR(-ENOMEM);
	}
	nhg->spare->spare = nhg;

	for (i = 0; i < nhg->num_nh; ++i) {
		struct nexthop *nhe;
		struct nh_info *nhi;

		nhe = nexthop_find_by_id(net, entry[i].id);
		if (!nexthop_get(nhe))
			goto out_no_nh;

		nhi = rtnl_dereference(nhe->nh_info);
		if (nhi->family == AF_INET)
			nhg->has_v4 = true;

		nhg->nh_entries[i].nh = nhe;
		nhg->nh_entries[i].weight = entry[i].weight + 1;
		list_add(&nhg->nh_entries[i].nh_list, &nhe->grp_list);
		nhg->nh_entries[i].nh_parent = nh;
	}

	if (cfg->nh_grp_type == NEXTHOP_GRP_TYPE_MPATH) {
		nhg->mpath = 1;
		nh_group_rebalance(nhg);
	}

	if (cfg->nh_fdb)
		nhg->fdb_nh = 1;

	rcu_assign_pointer(nh->nh_grp, nhg);

	return nh;

out_no_nh:
	for (; i >= 0; --i)
		nexthop_put(nhg->nh_entries[i].nh);

	kfree(nhg->spare);
	kfree(nhg);
	kfree(nh);

	return ERR_PTR(-ENOENT);
}

static int nh_create_ipv4(struct net *net, struct nexthop *nh,
			  struct nh_info *nhi, struct nh_config *cfg,
			  struct netlink_ext_ack *extack)
{
	struct fib_nh *fib_nh = &nhi->fib_nh;
	struct fib_config fib_cfg = {
		.fc_oif   = cfg->nh_ifindex,
		.fc_gw4   = cfg->gw.ipv4,
		.fc_gw_family = cfg->gw.ipv4 ? AF_INET : 0,
		.fc_flags = cfg->nh_flags,
		.fc_encap = cfg->nh_encap,
		.fc_encap_type = cfg->nh_encap_type,
	};
	u32 tb_id = (cfg->dev ? l3mdev_fib_table(cfg->dev) : RT_TABLE_MAIN);
	int err;

	err = fib_nh_init(net, fib_nh, &fib_cfg, 1, extack);
	if (err) {
		fib_nh_release(net, fib_nh);
		goto out;
	}

	if (nhi->fdb_nh)
		goto out;

	/* sets nh_dev if successful */
	err = fib_check_nh(net, fib_nh, tb_id, 0, extack);
	if (!err) {
		nh->nh_flags = fib_nh->fib_nh_flags;
		fib_info_update_nhc_saddr(net, &fib_nh->nh_common,
					  fib_nh->fib_nh_scope);
	} else {
		fib_nh_release(net, fib_nh);
	}
out:
	return err;
}

static int nh_create_ipv6(struct net *net,  struct nexthop *nh,
			  struct nh_info *nhi, struct nh_config *cfg,
			  struct netlink_ext_ack *extack)
{
	struct fib6_nh *fib6_nh = &nhi->fib6_nh;
	struct fib6_config fib6_cfg = {
		.fc_table = l3mdev_fib_table(cfg->dev),
		.fc_ifindex = cfg->nh_ifindex,
		.fc_gateway = cfg->gw.ipv6,
		.fc_flags = cfg->nh_flags,
		.fc_encap = cfg->nh_encap,
		.fc_encap_type = cfg->nh_encap_type,
		.fc_is_fdb = cfg->nh_fdb,
	};
	int err;

	if (!ipv6_addr_any(&cfg->gw.ipv6))
		fib6_cfg.fc_flags |= RTF_GATEWAY;

	/* sets nh_dev if successful */
	err = ipv6_stub->fib6_nh_init(net, fib6_nh, &fib6_cfg, GFP_KERNEL,
				      extack);
	if (err)
		ipv6_stub->fib6_nh_release(fib6_nh);
	else
		nh->nh_flags = fib6_nh->fib_nh_flags;

	return err;
}

static struct nexthop *nexthop_create(struct net *net, struct nh_config *cfg,
				      struct netlink_ext_ack *extack)
{
	struct nh_info *nhi;
	struct nexthop *nh;
	int err = 0;

	nh = nexthop_alloc();
	if (!nh)
		return ERR_PTR(-ENOMEM);

	nhi = kzalloc(sizeof(*nhi), GFP_KERNEL);
	if (!nhi) {
		kfree(nh);
		return ERR_PTR(-ENOMEM);
	}

	nh->nh_flags = cfg->nh_flags;
	nh->net = net;

	nhi->nh_parent = nh;
	nhi->family = cfg->nh_family;
	nhi->fib_nhc.nhc_scope = RT_SCOPE_LINK;

	if (cfg->nh_fdb)
		nhi->fdb_nh = 1;

	if (cfg->nh_blackhole) {
		nhi->reject_nh = 1;
		cfg->nh_ifindex = net->loopback_dev->ifindex;
	}

	switch (cfg->nh_family) {
	case AF_INET:
		err = nh_create_ipv4(net, nh, nhi, cfg, extack);
		break;
	case AF_INET6:
		err = nh_create_ipv6(net, nh, nhi, cfg, extack);
		break;
	}

	if (err) {
		kfree(nhi);
		kfree(nh);
		return ERR_PTR(err);
	}

	/* add the entry to the device based hash */
	if (!nhi->fdb_nh)
		nexthop_devhash_add(net, nhi);

	rcu_assign_pointer(nh->nh_info, nhi);

	return nh;
}

/* called with rtnl lock held */
static struct nexthop *nexthop_add(struct net *net, struct nh_config *cfg,
				   struct netlink_ext_ack *extack)
{
	struct nexthop *nh;
	int err;

	if (cfg->nlflags & NLM_F_REPLACE && !cfg->nh_id) {
		NL_SET_ERR_MSG(extack, "Replace requires nexthop id");
		return ERR_PTR(-EINVAL);
	}

	if (!cfg->nh_id) {
		cfg->nh_id = nh_find_unused_id(net);
		if (!cfg->nh_id) {
			NL_SET_ERR_MSG(extack, "No unused id");
			return ERR_PTR(-EINVAL);
		}
	}

	if (cfg->nh_grp)
		nh = nexthop_create_group(net, cfg);
	else
		nh = nexthop_create(net, cfg, extack);

	if (IS_ERR(nh))
		return nh;

	refcount_set(&nh->refcnt, 1);
	nh->id = cfg->nh_id;
	nh->protocol = cfg->nh_protocol;
	nh->net = net;

	err = insert_nexthop(net, nh, cfg, extack);
	if (err) {
		__remove_nexthop(net, nh, NULL);
		nexthop_put(nh);
		nh = ERR_PTR(err);
	}

	return nh;
}

static int rtm_to_nh_config(struct net *net, struct sk_buff *skb,
			    struct nlmsghdr *nlh, struct nh_config *cfg,
			    struct netlink_ext_ack *extack)
{
	struct nhmsg *nhm = nlmsg_data(nlh);
	struct nlattr *tb[NHA_MAX + 1];
	int err;

	err = nlmsg_parse(nlh, sizeof(*nhm), tb, NHA_MAX, rtm_nh_policy,
			  extack);
	if (err < 0)
		return err;

	err = -EINVAL;
	if (nhm->resvd || nhm->nh_scope) {
		NL_SET_ERR_MSG(extack, "Invalid values in ancillary header");
		goto out;
	}
	if (nhm->nh_flags & ~NEXTHOP_VALID_USER_FLAGS) {
		NL_SET_ERR_MSG(extack, "Invalid nexthop flags in ancillary header");
		goto out;
	}

	switch (nhm->nh_family) {
	case AF_INET:
	case AF_INET6:
		break;
	case AF_UNSPEC:
		if (tb[NHA_GROUP])
			break;
		fallthrough;
	default:
		NL_SET_ERR_MSG(extack, "Invalid address family");
		goto out;
	}

	if (tb[NHA_GROUPS] || tb[NHA_MASTER]) {
		NL_SET_ERR_MSG(extack, "Invalid attributes in request");
		goto out;
	}

	memset(cfg, 0, sizeof(*cfg));
	cfg->nlflags = nlh->nlmsg_flags;
	cfg->nlinfo.portid = NETLINK_CB(skb).portid;
	cfg->nlinfo.nlh = nlh;
	cfg->nlinfo.nl_net = net;

	cfg->nh_family = nhm->nh_family;
	cfg->nh_protocol = nhm->nh_protocol;
	cfg->nh_flags = nhm->nh_flags;

	if (tb[NHA_ID])
		cfg->nh_id = nla_get_u32(tb[NHA_ID]);

	if (tb[NHA_FDB]) {
		if (tb[NHA_OIF] || tb[NHA_BLACKHOLE] ||
		    tb[NHA_ENCAP]   || tb[NHA_ENCAP_TYPE]) {
			NL_SET_ERR_MSG(extack, "Fdb attribute can not be used with encap, oif or blackhole");
			goto out;
		}
		if (nhm->nh_flags) {
			NL_SET_ERR_MSG(extack, "Unsupported nexthop flags in ancillary header");
			goto out;
		}
		cfg->nh_fdb = nla_get_flag(tb[NHA_FDB]);
	}

	if (tb[NHA_GROUP]) {
		if (nhm->nh_family != AF_UNSPEC) {
			NL_SET_ERR_MSG(extack, "Invalid family for group");
			goto out;
		}
		cfg->nh_grp = tb[NHA_GROUP];

		cfg->nh_grp_type = NEXTHOP_GRP_TYPE_MPATH;
		if (tb[NHA_GROUP_TYPE])
			cfg->nh_grp_type = nla_get_u16(tb[NHA_GROUP_TYPE]);

		if (cfg->nh_grp_type > NEXTHOP_GRP_TYPE_MAX) {
			NL_SET_ERR_MSG(extack, "Invalid group type");
			goto out;
		}
		err = nh_check_attr_group(net, tb, extack);

		/* no other attributes should be set */
		goto out;
	}

	if (tb[NHA_BLACKHOLE]) {
		if (tb[NHA_GATEWAY] || tb[NHA_OIF] ||
		    tb[NHA_ENCAP]   || tb[NHA_ENCAP_TYPE] || tb[NHA_FDB]) {
			NL_SET_ERR_MSG(extack, "Blackhole attribute can not be used with gateway, oif, encap or fdb");
			goto out;
		}

		cfg->nh_blackhole = 1;
		err = 0;
		goto out;
	}

	if (!cfg->nh_fdb && !tb[NHA_OIF]) {
		NL_SET_ERR_MSG(extack, "Device attribute required for non-blackhole and non-fdb nexthops");
		goto out;
	}

	if (!cfg->nh_fdb && tb[NHA_OIF]) {
		cfg->nh_ifindex = nla_get_u32(tb[NHA_OIF]);
		if (cfg->nh_ifindex)
			cfg->dev = __dev_get_by_index(net, cfg->nh_ifindex);

		if (!cfg->dev) {
			NL_SET_ERR_MSG(extack, "Invalid device index");
			goto out;
		} else if (!(cfg->dev->flags & IFF_UP)) {
			NL_SET_ERR_MSG(extack, "Nexthop device is not up");
			err = -ENETDOWN;
			goto out;
		} else if (!netif_carrier_ok(cfg->dev)) {
			NL_SET_ERR_MSG(extack, "Carrier for nexthop device is down");
			err = -ENETDOWN;
			goto out;
		}
	}

	err = -EINVAL;
	if (tb[NHA_GATEWAY]) {
		struct nlattr *gwa = tb[NHA_GATEWAY];

		switch (cfg->nh_family) {
		case AF_INET:
			if (nla_len(gwa) != sizeof(u32)) {
				NL_SET_ERR_MSG(extack, "Invalid gateway");
				goto out;
			}
			cfg->gw.ipv4 = nla_get_be32(gwa);
			break;
		case AF_INET6:
			if (nla_len(gwa) != sizeof(struct in6_addr)) {
				NL_SET_ERR_MSG(extack, "Invalid gateway");
				goto out;
			}
			cfg->gw.ipv6 = nla_get_in6_addr(gwa);
			break;
		default:
			NL_SET_ERR_MSG(extack,
				       "Unknown address family for gateway");
			goto out;
		}
	} else {
		/* device only nexthop (no gateway) */
		if (cfg->nh_flags & RTNH_F_ONLINK) {
			NL_SET_ERR_MSG(extack,
				       "ONLINK flag can not be set for nexthop without a gateway");
			goto out;
		}
	}

	if (tb[NHA_ENCAP]) {
		cfg->nh_encap = tb[NHA_ENCAP];

		if (!tb[NHA_ENCAP_TYPE]) {
			NL_SET_ERR_MSG(extack, "LWT encapsulation type is missing");
			goto out;
		}

		cfg->nh_encap_type = nla_get_u16(tb[NHA_ENCAP_TYPE]);
		err = lwtunnel_valid_encap_type(cfg->nh_encap_type, extack);
		if (err < 0)
			goto out;

	} else if (tb[NHA_ENCAP_TYPE]) {
		NL_SET_ERR_MSG(extack, "LWT encapsulation attribute is missing");
		goto out;
	}


	err = 0;
out:
	return err;
}

/* rtnl */
static int rtm_new_nexthop(struct sk_buff *skb, struct nlmsghdr *nlh,
			   struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nh_config cfg;
	struct nexthop *nh;
	int err;

	err = rtm_to_nh_config(net, skb, nlh, &cfg, extack);
	if (!err) {
		nh = nexthop_add(net, &cfg, extack);
		if (IS_ERR(nh))
			err = PTR_ERR(nh);
	}

	return err;
}

static int nh_valid_get_del_req(struct nlmsghdr *nlh, u32 *id,
				struct netlink_ext_ack *extack)
{
	struct nhmsg *nhm = nlmsg_data(nlh);
	struct nlattr *tb[NHA_MAX + 1];
	int err, i;

	err = nlmsg_parse(nlh, sizeof(*nhm), tb, NHA_MAX, rtm_nh_policy,
			  extack);
	if (err < 0)
		return err;

	err = -EINVAL;
	for (i = 0; i < __NHA_MAX; ++i) {
		if (!tb[i])
			continue;

		switch (i) {
		case NHA_ID:
			break;
		default:
			NL_SET_ERR_MSG_ATTR(extack, tb[i],
					    "Unexpected attribute in request");
			goto out;
		}
	}
	if (nhm->nh_protocol || nhm->resvd || nhm->nh_scope || nhm->nh_flags) {
		NL_SET_ERR_MSG(extack, "Invalid values in header");
		goto out;
	}

	if (!tb[NHA_ID]) {
		NL_SET_ERR_MSG(extack, "Nexthop id is missing");
		goto out;
	}

	*id = nla_get_u32(tb[NHA_ID]);
	if (!(*id))
		NL_SET_ERR_MSG(extack, "Invalid nexthop id");
	else
		err = 0;
out:
	return err;
}

/* rtnl */
static int rtm_del_nexthop(struct sk_buff *skb, struct nlmsghdr *nlh,
			   struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nl_info nlinfo = {
		.nlh = nlh,
		.nl_net = net,
		.portid = NETLINK_CB(skb).portid,
	};
	struct nexthop *nh;
	int err;
	u32 id;

	err = nh_valid_get_del_req(nlh, &id, extack);
	if (err)
		return err;

	nh = nexthop_find_by_id(net, id);
	if (!nh)
		return -ENOENT;

	remove_nexthop(net, nh, &nlinfo);

	return 0;
}

/* rtnl */
static int rtm_get_nexthop(struct sk_buff *in_skb, struct nlmsghdr *nlh,
			   struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(in_skb->sk);
	struct sk_buff *skb = NULL;
	struct nexthop *nh;
	int err;
	u32 id;

	err = nh_valid_get_del_req(nlh, &id, extack);
	if (err)
		return err;

	err = -ENOBUFS;
	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		goto out;

	err = -ENOENT;
	nh = nexthop_find_by_id(net, id);
	if (!nh)
		goto errout_free;

	err = nh_fill_node(skb, nh, RTM_NEWNEXTHOP, NETLINK_CB(in_skb).portid,
			   nlh->nlmsg_seq, 0);
	if (err < 0) {
		WARN_ON(err == -EMSGSIZE);
		goto errout_free;
	}

	err = rtnl_unicast(skb, net, NETLINK_CB(in_skb).portid);
out:
	return err;
errout_free:
	kfree_skb(skb);
	goto out;
}

static bool nh_dump_filtered(struct nexthop *nh, int dev_idx, int master_idx,
			     bool group_filter, u8 family)
{
	const struct net_device *dev;
	const struct nh_info *nhi;

	if (group_filter && !nh->is_group)
		return true;

	if (!dev_idx && !master_idx && !family)
		return false;

	if (nh->is_group)
		return true;

	nhi = rtnl_dereference(nh->nh_info);
	if (family && nhi->family != family)
		return true;

	dev = nhi->fib_nhc.nhc_dev;
	if (dev_idx && (!dev || dev->ifindex != dev_idx))
		return true;

	if (master_idx) {
		struct net_device *master;

		if (!dev)
			return true;

		master = netdev_master_upper_dev_get((struct net_device *)dev);
		if (!master || master->ifindex != master_idx)
			return true;
	}

	return false;
}

static int nh_valid_dump_req(const struct nlmsghdr *nlh, int *dev_idx,
			     int *master_idx, bool *group_filter,
			     bool *fdb_filter, struct netlink_callback *cb)
{
	struct netlink_ext_ack *extack = cb->extack;
	struct nlattr *tb[NHA_MAX + 1];
	struct nhmsg *nhm;
	int err, i;
	u32 idx;

	err = nlmsg_parse(nlh, sizeof(*nhm), tb, NHA_MAX, rtm_nh_policy,
			  NULL);
	if (err < 0)
		return err;

	for (i = 0; i <= NHA_MAX; ++i) {
		if (!tb[i])
			continue;

		switch (i) {
		case NHA_OIF:
			idx = nla_get_u32(tb[i]);
			if (idx > INT_MAX) {
				NL_SET_ERR_MSG(extack, "Invalid device index");
				return -EINVAL;
			}
			*dev_idx = idx;
			break;
		case NHA_MASTER:
			idx = nla_get_u32(tb[i]);
			if (idx > INT_MAX) {
				NL_SET_ERR_MSG(extack, "Invalid master device index");
				return -EINVAL;
			}
			*master_idx = idx;
			break;
		case NHA_GROUPS:
			*group_filter = true;
			break;
		case NHA_FDB:
			*fdb_filter = true;
			break;
		default:
			NL_SET_ERR_MSG(extack, "Unsupported attribute in dump request");
			return -EINVAL;
		}
	}

	nhm = nlmsg_data(nlh);
	if (nhm->nh_protocol || nhm->resvd || nhm->nh_scope || nhm->nh_flags) {
		NL_SET_ERR_MSG(extack, "Invalid values in header for nexthop dump request");
		return -EINVAL;
	}

	return 0;
}

/* rtnl */
static int rtm_dump_nexthop(struct sk_buff *skb, struct netlink_callback *cb)
{
	bool group_filter = false, fdb_filter = false;
	struct nhmsg *nhm = nlmsg_data(cb->nlh);
	int dev_filter_idx = 0, master_idx = 0;
	struct net *net = sock_net(skb->sk);
	struct rb_root *root = &net->nexthop.rb_root;
	struct rb_node *node;
	int idx = 0, s_idx;
	int err;

	err = nh_valid_dump_req(cb->nlh, &dev_filter_idx, &master_idx,
				&group_filter, &fdb_filter, cb);
	if (err < 0)
		return err;

	s_idx = cb->args[0];
	for (node = rb_first(root); node; node = rb_next(node)) {
		struct nexthop *nh;

		if (idx < s_idx)
			goto cont;

		nh = rb_entry(node, struct nexthop, rb_node);
		if (nh_dump_filtered(nh, dev_filter_idx, master_idx,
				     group_filter, nhm->nh_family))
			goto cont;

		err = nh_fill_node(skb, nh, RTM_NEWNEXTHOP,
				   NETLINK_CB(cb->skb).portid,
				   cb->nlh->nlmsg_seq, NLM_F_MULTI);
		if (err < 0) {
			if (likely(skb->len))
				goto out;

			goto out_err;
		}
cont:
		idx++;
	}

out:
	err = skb->len;
out_err:
	cb->args[0] = idx;
	cb->seq = net->nexthop.seq;
	nl_dump_check_consistent(cb, nlmsg_hdr(skb));

	return err;
}

static void nexthop_sync_mtu(struct net_device *dev, u32 orig_mtu)
{
	unsigned int hash = nh_dev_hashfn(dev->ifindex);
	struct net *net = dev_net(dev);
	struct hlist_head *head = &net->nexthop.devhash[hash];
	struct hlist_node *n;
	struct nh_info *nhi;

	hlist_for_each_entry_safe(nhi, n, head, dev_hash) {
		if (nhi->fib_nhc.nhc_dev == dev) {
			if (nhi->family == AF_INET)
				fib_nhc_update_mtu(&nhi->fib_nhc, dev->mtu,
						   orig_mtu);
		}
	}
}

/* rtnl */
static int nh_netdev_event(struct notifier_block *this,
			   unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_info_ext *info_ext;

	switch (event) {
	case NETDEV_DOWN:
	case NETDEV_UNREGISTER:
		nexthop_flush_dev(dev);
		break;
	case NETDEV_CHANGE:
		if (!(dev_get_flags(dev) & (IFF_RUNNING | IFF_LOWER_UP)))
			nexthop_flush_dev(dev);
		break;
	case NETDEV_CHANGEMTU:
		info_ext = ptr;
		nexthop_sync_mtu(dev, info_ext->ext.mtu);
		rt_cache_flush(dev_net(dev));
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block nh_netdev_notifier = {
	.notifier_call = nh_netdev_event,
};

int register_nexthop_notifier(struct net *net, struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&net->nexthop.notifier_chain,
						nb);
}
EXPORT_SYMBOL(register_nexthop_notifier);

int unregister_nexthop_notifier(struct net *net, struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&net->nexthop.notifier_chain,
						  nb);
}
EXPORT_SYMBOL(unregister_nexthop_notifier);

static void __net_exit nexthop_net_exit(struct net *net)
{
	rtnl_lock();
	flush_all_nexthops(net);
	rtnl_unlock();
	kfree(net->nexthop.devhash);
}

static int __net_init nexthop_net_init(struct net *net)
{
	size_t sz = sizeof(struct hlist_head) * NH_DEV_HASHSIZE;

	net->nexthop.rb_root = RB_ROOT;
	net->nexthop.devhash = kzalloc(sz, GFP_KERNEL);
	if (!net->nexthop.devhash)
		return -ENOMEM;
	BLOCKING_INIT_NOTIFIER_HEAD(&net->nexthop.notifier_chain);

	return 0;
}

static struct pernet_operations nexthop_net_ops = {
	.init = nexthop_net_init,
	.exit = nexthop_net_exit,
};

static int __init nexthop_init(void)
{
	register_pernet_subsys(&nexthop_net_ops);

	register_netdevice_notifier(&nh_netdev_notifier);

	rtnl_register(PF_UNSPEC, RTM_NEWNEXTHOP, rtm_new_nexthop, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_DELNEXTHOP, rtm_del_nexthop, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_GETNEXTHOP, rtm_get_nexthop,
		      rtm_dump_nexthop, 0);

	rtnl_register(PF_INET, RTM_NEWNEXTHOP, rtm_new_nexthop, NULL, 0);
	rtnl_register(PF_INET, RTM_GETNEXTHOP, NULL, rtm_dump_nexthop, 0);

	rtnl_register(PF_INET6, RTM_NEWNEXTHOP, rtm_new_nexthop, NULL, 0);
	rtnl_register(PF_INET6, RTM_GETNEXTHOP, NULL, rtm_dump_nexthop, 0);

	return 0;
}
subsys_initcall(nexthop_init);
