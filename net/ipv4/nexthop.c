// SPDX-License-Identifier: GPL-2.0
/* Generic nexthop implementation
 *
 * Copyright (c) 2017-19 Cumulus Networks
 * Copyright (c) 2017-19 David Ahern <dsa@cumulusnetworks.com>
 */

#include <linux/nexthop.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <net/ipv6_stubs.h>
#include <net/nexthop.h>
#include <net/route.h>
#include <net/sock.h>

#define NH_DEV_HASHBITS  8
#define NH_DEV_HASHSIZE (1U << NH_DEV_HASHBITS)

static const struct nla_policy rtm_nh_policy[NHA_MAX + 1] = {
	[NHA_UNSPEC]		= { .strict_start_type = NHA_UNSPEC + 1 },
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
};

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

void nexthop_free_rcu(struct rcu_head *head)
{
	struct nexthop *nh = container_of(head, struct nexthop, rcu);
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

	kfree(nh);
}
EXPORT_SYMBOL_GPL(nexthop_free_rcu);

static struct nexthop *nexthop_alloc(void)
{
	struct nexthop *nh;

	nh = kzalloc(sizeof(struct nexthop), GFP_KERNEL);
	return nh;
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

	nhi = rtnl_dereference(nh->nh_info);
	nhm->nh_family = nhi->family;
	if (nhi->reject_nh) {
		if (nla_put_flag(skb, NHA_BLACKHOLE))
			goto nla_put_failure;
		goto out;
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
		    nla_put_u32(skb, NHA_GATEWAY, fib_nh->fib_nh_gw4))
			goto nla_put_failure;
		break;

	case AF_INET6:
		fib6_nh = &nhi->fib6_nh;
		if (fib6_nh->fib_nh_gw_family &&
		    nla_put_in6_addr(skb, NHA_GATEWAY, &fib6_nh->fib_nh_gw6))
			goto nla_put_failure;
		break;
	}

out:
	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static size_t nh_nlmsg_size(struct nexthop *nh)
{
	struct nh_info *nhi = rtnl_dereference(nh->nh_info);
	size_t sz = nla_total_size(4);    /* NHA_ID */

	/* covers NHA_BLACKHOLE since NHA_OIF and BLACKHOLE
	 * are mutually exclusive
	 */
	sz += nla_total_size(4);  /* NHA_OIF */

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

static void __remove_nexthop(struct net *net, struct nexthop *nh)
{
	struct nh_info *nhi;

	nhi = rtnl_dereference(nh->nh_info);
	if (nhi->fib_nhc.nhc_dev)
		hlist_del(&nhi->dev_hash);
}

static void remove_nexthop(struct net *net, struct nexthop *nh,
			   bool skip_fib, struct nl_info *nlinfo)
{
	/* remove from the tree */
	rb_erase(&nh->rb_node, &net->nexthop.rb_root);

	if (nlinfo)
		nexthop_notify(RTM_DELNEXTHOP, nh, nlinfo);

	__remove_nexthop(net, nh);
	nh_base_seq_inc(net);

	nexthop_put(nh);
}

static int replace_nexthop(struct net *net, struct nexthop *old,
			   struct nexthop *new, struct netlink_ext_ack *extack)
{
	return -EEXIST;
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
	int rc = -EEXIST;

	pp = &root->rb_node;
	while (1) {
		struct nexthop *nh;

		next = rtnl_dereference(*pp);
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
			if (!rc)
				new_nh = nh; /* send notification with old nh */
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

		remove_nexthop(net, nhi->nh_parent, false, NULL);
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
		remove_nexthop(net, nh, false, NULL);
		cond_resched();
	}
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
	};
	u32 tb_id = l3mdev_fib_table(cfg->dev);
	int err = -EINVAL;

	err = fib_nh_init(net, fib_nh, &fib_cfg, 1, extack);
	if (err) {
		fib_nh_release(net, fib_nh);
		goto out;
	}

	/* sets nh_dev if successful */
	err = fib_check_nh(net, fib_nh, tb_id, 0, extack);
	if (!err) {
		nh->nh_flags = fib_nh->fib_nh_flags;
		fib_info_update_nh_saddr(net, fib_nh, fib_nh->fib_nh_scope);
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
	};
	int err = -EINVAL;

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

	nh = nexthop_create(net, cfg, extack);
	if (IS_ERR(nh))
		return nh;

	refcount_set(&nh->refcnt, 1);
	nh->id = cfg->nh_id;
	nh->protocol = cfg->nh_protocol;
	nh->net = net;

	err = insert_nexthop(net, nh, cfg, extack);
	if (err) {
		__remove_nexthop(net, nh);
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

	if (tb[NHA_BLACKHOLE]) {
		if (tb[NHA_GATEWAY] || tb[NHA_OIF]) {
			NL_SET_ERR_MSG(extack, "Blackhole attribute can not be used with gateway or oif");
			goto out;
		}

		cfg->nh_blackhole = 1;
		err = 0;
		goto out;
	}

	if (!tb[NHA_OIF]) {
		NL_SET_ERR_MSG(extack, "Device attribute required for non-blackhole nexthops");
		goto out;
	}

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

	remove_nexthop(net, nh, false, &nlinfo);

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

static bool nh_dump_filtered(struct nexthop *nh, int dev_idx,
			     int master_idx, u8 family)
{
	const struct net_device *dev;
	const struct nh_info *nhi;

	if (!dev_idx && !master_idx && !family)
		return false;

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

static int nh_valid_dump_req(const struct nlmsghdr *nlh,
			     int *dev_idx, int *master_idx,
			     struct netlink_callback *cb)
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
	struct nhmsg *nhm = nlmsg_data(cb->nlh);
	int dev_filter_idx = 0, master_idx = 0;
	struct net *net = sock_net(skb->sk);
	struct rb_root *root = &net->nexthop.rb_root;
	struct rb_node *node;
	int idx = 0, s_idx;
	int err;

	err = nh_valid_dump_req(cb->nlh, &dev_filter_idx, &master_idx, cb);
	if (err < 0)
		return err;

	s_idx = cb->args[0];
	for (node = rb_first(root); node; node = rb_next(node)) {
		struct nexthop *nh;

		if (idx < s_idx)
			goto cont;

		nh = rb_entry(node, struct nexthop, rb_node);
		if (nh_dump_filtered(nh, dev_filter_idx, master_idx,
				     nhm->nh_family))
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
