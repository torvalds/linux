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

#include <net/fib_rules.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/netlink.h>

struct fib6_rule
{
	struct fib_rule		common;
	struct rt6key		src;
	struct rt6key		dst;
	u8			tclass;
};

struct dst_entry *fib6_rule_lookup(struct net *net, struct flowi *fl,
				   int flags, pol_lookup_t lookup)
{
	struct fib_lookup_arg arg = {
		.lookup_ptr = lookup,
	};

	fib_rules_lookup(net->ipv6.fib6_rules_ops, fl, flags, &arg);
	if (arg.rule)
		fib_rule_put(arg.rule);

	if (arg.result)
		return arg.result;

	dst_hold(&net->ipv6.ip6_null_entry->u.dst);
	return &net->ipv6.ip6_null_entry->u.dst;
}

static int fib6_rule_action(struct fib_rule *rule, struct flowi *flp,
			    int flags, struct fib_lookup_arg *arg)
{
	struct rt6_info *rt = NULL;
	struct fib6_table *table;
	struct net *net = rule->fr_net;
	pol_lookup_t lookup = arg->lookup_ptr;

	switch (rule->action) {
	case FR_ACT_TO_TBL:
		break;
	case FR_ACT_UNREACHABLE:
		rt = net->ipv6.ip6_null_entry;
		goto discard_pkt;
	default:
	case FR_ACT_BLACKHOLE:
		rt = net->ipv6.ip6_blk_hole_entry;
		goto discard_pkt;
	case FR_ACT_PROHIBIT:
		rt = net->ipv6.ip6_prohibit_entry;
		goto discard_pkt;
	}

	table = fib6_get_table(net, rule->table);
	if (table)
		rt = lookup(net, table, flp, flags);

	if (rt != net->ipv6.ip6_null_entry) {
		struct fib6_rule *r = (struct fib6_rule *)rule;

		/*
		 * If we need to find a source address for this traffic,
		 * we check the result if it meets requirement of the rule.
		 */
		if ((rule->flags & FIB_RULE_FIND_SADDR) &&
		    r->src.plen && !(flags & RT6_LOOKUP_F_HAS_SADDR)) {
			struct in6_addr saddr;
			unsigned int srcprefs = 0;

			if (flags & RT6_LOOKUP_F_SRCPREF_TMP)
				srcprefs |= IPV6_PREFER_SRC_TMP;
			if (flags & RT6_LOOKUP_F_SRCPREF_PUBLIC)
				srcprefs |= IPV6_PREFER_SRC_PUBLIC;
			if (flags & RT6_LOOKUP_F_SRCPREF_COA)
				srcprefs |= IPV6_PREFER_SRC_COA;

			if (ipv6_dev_get_saddr(ip6_dst_idev(&rt->u.dst)->dev,
					       &flp->fl6_dst, srcprefs,
					       &saddr))
				goto again;
			if (!ipv6_prefix_equal(&saddr, &r->src.addr,
					       r->src.plen))
				goto again;
			ipv6_addr_copy(&flp->fl6_src, &saddr);
		}
		goto out;
	}
again:
	dst_release(&rt->u.dst);
	rt = NULL;
	goto out;

discard_pkt:
	dst_hold(&rt->u.dst);
out:
	arg->result = rt;
	return rt == NULL ? -EAGAIN : 0;
}


static int fib6_rule_match(struct fib_rule *rule, struct flowi *fl, int flags)
{
	struct fib6_rule *r = (struct fib6_rule *) rule;

	if (r->dst.plen &&
	    !ipv6_prefix_equal(&fl->fl6_dst, &r->dst.addr, r->dst.plen))
		return 0;

	/*
	 * If FIB_RULE_FIND_SADDR is set and we do not have a
	 * source address for the traffic, we defer check for
	 * source address.
	 */
	if (r->src.plen) {
		if (flags & RT6_LOOKUP_F_HAS_SADDR) {
			if (!ipv6_prefix_equal(&fl->fl6_src, &r->src.addr,
					       r->src.plen))
				return 0;
		} else if (!(r->common.flags & FIB_RULE_FIND_SADDR))
			return 0;
	}

	if (r->tclass && r->tclass != ((ntohl(fl->fl6_flowlabel) >> 20) & 0xff))
		return 0;

	return 1;
}

static const struct nla_policy fib6_rule_policy[FRA_MAX+1] = {
	FRA_GENERIC_POLICY,
};

static int fib6_rule_configure(struct fib_rule *rule, struct sk_buff *skb,
			       struct nlmsghdr *nlh, struct fib_rule_hdr *frh,
			       struct nlattr **tb)
{
	int err = -EINVAL;
	struct net *net = sock_net(skb->sk);
	struct fib6_rule *rule6 = (struct fib6_rule *) rule;

	if (rule->action == FR_ACT_TO_TBL) {
		if (rule->table == RT6_TABLE_UNSPEC)
			goto errout;

		if (fib6_new_table(net, rule->table) == NULL) {
			err = -ENOBUFS;
			goto errout;
		}
	}

	if (frh->src_len)
		nla_memcpy(&rule6->src.addr, tb[FRA_SRC],
			   sizeof(struct in6_addr));

	if (frh->dst_len)
		nla_memcpy(&rule6->dst.addr, tb[FRA_DST],
			   sizeof(struct in6_addr));

	rule6->src.plen = frh->src_len;
	rule6->dst.plen = frh->dst_len;
	rule6->tclass = frh->tos;

	err = 0;
errout:
	return err;
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
			  struct nlmsghdr *nlh, struct fib_rule_hdr *frh)
{
	struct fib6_rule *rule6 = (struct fib6_rule *) rule;

	frh->family = AF_INET6;
	frh->dst_len = rule6->dst.plen;
	frh->src_len = rule6->src.plen;
	frh->tos = rule6->tclass;

	if (rule6->dst.plen)
		NLA_PUT(skb, FRA_DST, sizeof(struct in6_addr),
			&rule6->dst.addr);

	if (rule6->src.plen)
		NLA_PUT(skb, FRA_SRC, sizeof(struct in6_addr),
			&rule6->src.addr);

	return 0;

nla_put_failure:
	return -ENOBUFS;
}

static u32 fib6_rule_default_pref(struct fib_rules_ops *ops)
{
	return 0x3FFF;
}

static size_t fib6_rule_nlmsg_payload(struct fib_rule *rule)
{
	return nla_total_size(16) /* dst */
	       + nla_total_size(16); /* src */
}

static struct fib_rules_ops fib6_rules_ops_template = {
	.family			= AF_INET6,
	.rule_size		= sizeof(struct fib6_rule),
	.addr_size		= sizeof(struct in6_addr),
	.action			= fib6_rule_action,
	.match			= fib6_rule_match,
	.configure		= fib6_rule_configure,
	.compare		= fib6_rule_compare,
	.fill			= fib6_rule_fill,
	.default_pref		= fib6_rule_default_pref,
	.nlmsg_payload		= fib6_rule_nlmsg_payload,
	.nlgroup		= RTNLGRP_IPV6_RULE,
	.policy			= fib6_rule_policy,
	.owner			= THIS_MODULE,
	.fro_net		= &init_net,
};

static int fib6_rules_net_init(struct net *net)
{
	int err = -ENOMEM;

	net->ipv6.fib6_rules_ops = kmemdup(&fib6_rules_ops_template,
					   sizeof(*net->ipv6.fib6_rules_ops),
					   GFP_KERNEL);
	if (!net->ipv6.fib6_rules_ops)
		goto out;

	net->ipv6.fib6_rules_ops->fro_net = net;
	INIT_LIST_HEAD(&net->ipv6.fib6_rules_ops->rules_list);

	err = fib_default_rule_add(net->ipv6.fib6_rules_ops, 0,
				   RT6_TABLE_LOCAL, FIB_RULE_PERMANENT);
	if (err)
		goto out_fib6_rules_ops;

	err = fib_default_rule_add(net->ipv6.fib6_rules_ops,
				   0x7FFE, RT6_TABLE_MAIN, 0);
	if (err)
		goto out_fib6_default_rule_add;

	err = fib_rules_register(net->ipv6.fib6_rules_ops);
	if (err)
		goto out_fib6_default_rule_add;
out:
	return err;

out_fib6_default_rule_add:
	fib_rules_cleanup_ops(net->ipv6.fib6_rules_ops);
out_fib6_rules_ops:
	kfree(net->ipv6.fib6_rules_ops);
	goto out;
}

static void fib6_rules_net_exit(struct net *net)
{
	fib_rules_unregister(net->ipv6.fib6_rules_ops);
	kfree(net->ipv6.fib6_rules_ops);
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
