/*
 * net/ipv6/fib6_rules.c	IPv6 Routing Policy Rules
 *
 * Copyright (C)2003-2006 Helsinki University of Technology
 * Copyright (C)2003-2006 USAGI/WIDE Project
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * Authors
 *	Thomas Graf		<tgraf@suug.ch>
 *	Ville Nuorvala		<vnuorval@tcs.hut.fi>
 */

#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/export.h>

#include <net/fib_rules.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/netlink.h>

struct fib6_rule {
	struct fib_rule		common;
	struct rt6key		src;
	struct rt6key		dst;
	u8			tclass;
};

static bool fib6_rule_matchall(const struct fib_rule *rule)
{
	struct fib6_rule *r = container_of(rule, struct fib6_rule, common);

	if (r->dst.plen || r->src.plen || r->tclass)
		return false;
	return fib_rule_matchall(rule);
}

bool fib6_rule_default(const struct fib_rule *rule)
{
	if (!fib6_rule_matchall(rule) || rule->action != FR_ACT_TO_TBL ||
	    rule->l3mdev)
		return false;
	if (rule->table != RT6_TABLE_LOCAL && rule->table != RT6_TABLE_MAIN)
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(fib6_rule_default);

int fib6_rules_dump(struct net *net, struct notifier_block *nb)
{
	return fib_rules_dump(net, nb, AF_INET6);
}

unsigned int fib6_rules_seq_read(struct net *net)
{
	return fib_rules_seq_read(net, AF_INET6);
}

struct dst_entry *fib6_rule_lookup(struct net *net, struct flowi6 *fl6,
				   const struct sk_buff *skb,
				   int flags, pol_lookup_t lookup)
{
	if (net->ipv6.fib6_has_custom_rules) {
		struct fib_lookup_arg arg = {
			.lookup_ptr = lookup,
			.lookup_data = skb,
			.flags = FIB_LOOKUP_NOREF,
		};

		/* update flow if oif or iif point to device enslaved to l3mdev */
		l3mdev_update_flow(net, flowi6_to_flowi(fl6));

		fib_rules_lookup(net->ipv6.fib6_rules_ops,
				 flowi6_to_flowi(fl6), flags, &arg);

		if (arg.result)
			return arg.result;
	} else {
		struct rt6_info *rt;

		rt = lookup(net, net->ipv6.fib6_local_tbl, fl6, skb, flags);
		if (rt != net->ipv6.ip6_null_entry && rt->dst.error != -EAGAIN)
			return &rt->dst;
		ip6_rt_put(rt);
		rt = lookup(net, net->ipv6.fib6_main_tbl, fl6, skb, flags);
		if (rt->dst.error != -EAGAIN)
			return &rt->dst;
		ip6_rt_put(rt);
	}

	dst_hold(&net->ipv6.ip6_null_entry->dst);
	return &net->ipv6.ip6_null_entry->dst;
}

static int fib6_rule_saddr(struct net *net, struct fib_rule *rule, int flags,
			   struct flowi6 *flp6, const struct net_device *dev)
{
	struct fib6_rule *r = (struct fib6_rule *)rule;

	/* If we need to find a source address for this traffic,
	 * we check the result if it meets requirement of the rule.
	 */
	if ((rule->flags & FIB_RULE_FIND_SADDR) &&
	    r->src.plen && !(flags & RT6_LOOKUP_F_HAS_SADDR)) {
		struct in6_addr saddr;

		if (ipv6_dev_get_saddr(net, dev, &flp6->daddr,
				       rt6_flags2srcprefs(flags), &saddr))
			return -EAGAIN;

		if (!ipv6_prefix_equal(&saddr, &r->src.addr, r->src.plen))
			return -EAGAIN;

		flp6->saddr = saddr;
	}

	return 0;
}

static int fib6_rule_action(struct fib_rule *rule, struct flowi *flp,
			    int flags, struct fib_lookup_arg *arg)
{
	struct flowi6 *flp6 = &flp->u.ip6;
	struct rt6_info *rt = NULL;
	struct fib6_table *table;
	struct net *net = rule->fr_net;
	pol_lookup_t lookup = arg->lookup_ptr;
	int err = 0;
	u32 tb_id;

	switch (rule->action) {
	case FR_ACT_TO_TBL:
		break;
	case FR_ACT_UNREACHABLE:
		err = -ENETUNREACH;
		rt = net->ipv6.ip6_null_entry;
		goto discard_pkt;
	default:
	case FR_ACT_BLACKHOLE:
		err = -EINVAL;
		rt = net->ipv6.ip6_blk_hole_entry;
		goto discard_pkt;
	case FR_ACT_PROHIBIT:
		err = -EACCES;
		rt = net->ipv6.ip6_prohibit_entry;
		goto discard_pkt;
	}

	tb_id = fib_rule_get_table(rule, arg);
	table = fib6_get_table(net, tb_id);
	if (!table) {
		err = -EAGAIN;
		goto out;
	}

	rt = lookup(net, table, flp6, arg->lookup_data, flags);
	if (rt != net->ipv6.ip6_null_entry) {
		err = fib6_rule_saddr(net, rule, flags, flp6,
				      ip6_dst_idev(&rt->dst)->dev);

		if (err == -EAGAIN)
			goto again;

		err = rt->dst.error;
		if (err != -EAGAIN)
			goto out;
	}
again:
	ip6_rt_put(rt);
	err = -EAGAIN;
	rt = NULL;
	goto out;

discard_pkt:
	dst_hold(&rt->dst);
out:
	arg->result = rt;
	return err;
}

static bool fib6_rule_suppress(struct fib_rule *rule, struct fib_lookup_arg *arg)
{
	struct rt6_info *rt = (struct rt6_info *) arg->result;
	struct net_device *dev = NULL;

	if (rt->rt6i_idev)
		dev = rt->rt6i_idev->dev;

	/* do not accept result if the route does
	 * not meet the required prefix length
	 */
	if (rt->rt6i_dst.plen <= rule->suppress_prefixlen)
		goto suppress_route;

	/* do not accept result if the route uses a device
	 * belonging to a forbidden interface group
	 */
	if (rule->suppress_ifgroup != -1 && dev && dev->group == rule->suppress_ifgroup)
		goto suppress_route;

	return false;

suppress_route:
	ip6_rt_put(rt);
	return true;
}

static int fib6_rule_match(struct fib_rule *rule, struct flowi *fl, int flags)
{
	struct fib6_rule *r = (struct fib6_rule *) rule;
	struct flowi6 *fl6 = &fl->u.ip6;

	if (r->dst.plen &&
	    !ipv6_prefix_equal(&fl6->daddr, &r->dst.addr, r->dst.plen))
		return 0;

	/*
	 * If FIB_RULE_FIND_SADDR is set and we do not have a
	 * source address for the traffic, we defer check for
	 * source address.
	 */
	if (r->src.plen) {
		if (flags & RT6_LOOKUP_F_HAS_SADDR) {
			if (!ipv6_prefix_equal(&fl6->saddr, &r->src.addr,
					       r->src.plen))
				return 0;
		} else if (!(r->common.flags & FIB_RULE_FIND_SADDR))
			return 0;
	}

	if (r->tclass && r->tclass != ip6_tclass(fl6->flowlabel))
		return 0;

	if (rule->ip_proto && (rule->ip_proto != fl6->flowi6_proto))
		return 0;

	if (fib_rule_port_range_set(&rule->sport_range) &&
	    !fib_rule_port_inrange(&rule->sport_range, fl6->fl6_sport))
		return 0;

	if (fib_rule_port_range_set(&rule->dport_range) &&
	    !fib_rule_port_inrange(&rule->dport_range, fl6->fl6_dport))
		return 0;

	return 1;
}

static const struct nla_policy fib6_rule_policy[FRA_MAX+1] = {
	FRA_GENERIC_POLICY,
};

static int fib6_rule_configure(struct fib_rule *rule, struct sk_buff *skb,
			       struct fib_rule_hdr *frh,
			       struct nlattr **tb,
			       struct netlink_ext_ack *extack)
{
	int err = -EINVAL;
	struct net *net = sock_net(skb->sk);
	struct fib6_rule *rule6 = (struct fib6_rule *) rule;

	if (rule->action == FR_ACT_TO_TBL && !rule->l3mdev) {
		if (rule->table == RT6_TABLE_UNSPEC) {
			NL_SET_ERR_MSG(extack, "Invalid table");
			goto errout;
		}

		if (fib6_new_table(net, rule->table) == NULL) {
			err = -ENOBUFS;
			goto errout;
		}
	}

	if (frh->src_len)
		rule6->src.addr = nla_get_in6_addr(tb[FRA_SRC]);

	if (frh->dst_len)
		rule6->dst.addr = nla_get_in6_addr(tb[FRA_DST]);

	rule6->src.plen = frh->src_len;
	rule6->dst.plen = frh->dst_len;
	rule6->tclass = frh->tos;

	if (fib_rule_requires_fldissect(rule))
		net->ipv6.fib6_rules_require_fldissect++;

	net->ipv6.fib6_has_custom_rules = true;
	err = 0;
errout:
	return err;
}

static int fib6_rule_delete(struct fib_rule *rule)
{
	struct net *net = rule->fr_net;

	if (net->ipv6.fib6_rules_require_fldissect &&
	    fib_rule_requires_fldissect(rule))
		net->ipv6.fib6_rules_require_fldissect--;

	return 0;
}

static int fib6_rule_compare(struct fib_rule *rule, struct fib_rule_hdr *frh,
			     struct nlattr **tb)
{
	struct fib6_rule *rule6 = (struct fib6_rule *) rule;

	if (frh->src_len && (rule6->src.plen != frh->src_len))
		return 0;

	if (frh->dst_len && (rule6->dst.plen != frh->dst_len))
		return 0;

	if (frh->tos && (rule6->tclass != frh->tos))
		return 0;

	if (frh->src_len &&
	    nla_memcmp(tb[FRA_SRC], &rule6->src.addr, sizeof(struct in6_addr)))
		return 0;

	if (frh->dst_len &&
	    nla_memcmp(tb[FRA_DST], &rule6->dst.addr, sizeof(struct in6_addr)))
		return 0;

	return 1;
}

static int fib6_rule_fill(struct fib_rule *rule, struct sk_buff *skb,
			  struct fib_rule_hdr *frh)
{
	struct fib6_rule *rule6 = (struct fib6_rule *) rule;

	frh->dst_len = rule6->dst.plen;
	frh->src_len = rule6->src.plen;
	frh->tos = rule6->tclass;

	if ((rule6->dst.plen &&
	     nla_put_in6_addr(skb, FRA_DST, &rule6->dst.addr)) ||
	    (rule6->src.plen &&
	     nla_put_in6_addr(skb, FRA_SRC, &rule6->src.addr)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -ENOBUFS;
}

static size_t fib6_rule_nlmsg_payload(struct fib_rule *rule)
{
	return nla_total_size(16) /* dst */
	       + nla_total_size(16); /* src */
}

static const struct fib_rules_ops __net_initconst fib6_rules_ops_template = {
	.family			= AF_INET6,
	.rule_size		= sizeof(struct fib6_rule),
	.addr_size		= sizeof(struct in6_addr),
	.action			= fib6_rule_action,
	.match			= fib6_rule_match,
	.suppress		= fib6_rule_suppress,
	.configure		= fib6_rule_configure,
	.delete			= fib6_rule_delete,
	.compare		= fib6_rule_compare,
	.fill			= fib6_rule_fill,
	.nlmsg_payload		= fib6_rule_nlmsg_payload,
	.nlgroup		= RTNLGRP_IPV6_RULE,
	.policy			= fib6_rule_policy,
	.owner			= THIS_MODULE,
	.fro_net		= &init_net,
};

static int __net_init fib6_rules_net_init(struct net *net)
{
	struct fib_rules_ops *ops;
	int err = -ENOMEM;

	ops = fib_rules_register(&fib6_rules_ops_template, net);
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	err = fib_default_rule_add(ops, 0, RT6_TABLE_LOCAL, 0);
	if (err)
		goto out_fib6_rules_ops;

	err = fib_default_rule_add(ops, 0x7FFE, RT6_TABLE_MAIN, 0);
	if (err)
		goto out_fib6_rules_ops;

	net->ipv6.fib6_rules_ops = ops;
	net->ipv6.fib6_rules_require_fldissect = 0;
out:
	return err;

out_fib6_rules_ops:
	fib_rules_unregister(ops);
	goto out;
}

static void __net_exit fib6_rules_net_exit(struct net *net)
{
	rtnl_lock();
	fib_rules_unregister(net->ipv6.fib6_rules_ops);
	rtnl_unlock();
}

static struct pernet_operations fib6_rules_net_ops = {
	.init = fib6_rules_net_init,
	.exit = fib6_rules_net_exit,
};

int __init fib6_rules_init(void)
{
	return register_pernet_subsys(&fib6_rules_net_ops);
}


void fib6_rules_cleanup(void)
{
	unregister_pernet_subsys(&fib6_rules_net_ops);
}
