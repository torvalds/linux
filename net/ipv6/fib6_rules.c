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

static struct fib_rules_ops fib6_rules_ops;

static struct fib6_rule main_rule = {
	.common = {
		.refcnt =	ATOMIC_INIT(2),
		.pref =		0x7FFE,
		.action =	FR_ACT_TO_TBL,
		.table =	RT6_TABLE_MAIN,
	},
};

static struct fib6_rule local_rule = {
	.common = {
		.refcnt =	ATOMIC_INIT(2),
		.pref =		0,
		.action =	FR_ACT_TO_TBL,
		.table =	RT6_TABLE_LOCAL,
		.flags =	FIB_RULE_PERMANENT,
	},
};

struct dst_entry *fib6_rule_lookup(struct flowi *fl, int flags,
				   pol_lookup_t lookup)
{
	struct fib_lookup_arg arg = {
		.lookup_ptr = lookup,
	};

	fib_rules_lookup(&fib6_rules_ops, fl, flags, &arg);
	if (arg.rule)
		fib_rule_put(arg.rule);

	if (arg.result)
		return arg.result;

	dst_hold(&ip6_null_entry.u.dst);
	return &ip6_null_entry.u.dst;
}

static int fib6_rule_action(struct fib_rule *rule, struct flowi *flp,
			    int flags, struct fib_lookup_arg *arg)
{
	struct rt6_info *rt = NULL;
	struct fib6_table *table;
	pol_lookup_t lookup = arg->lookup_ptr;

	switch (rule->action) {
	case FR_ACT_TO_TBL:
		break;
	case FR_ACT_UNREACHABLE:
		rt = &ip6_null_entry;
		goto discard_pkt;
	default:
	case FR_ACT_BLACKHOLE:
		rt = &ip6_blk_hole_entry;
		goto discard_pkt;
	case FR_ACT_PROHIBIT:
		rt = &ip6_prohibit_entry;
		goto discard_pkt;
	}

	table = fib6_get_table(rule->table);
	if (table)
		rt = lookup(table, flp, flags);

	if (rt != &ip6_null_entry) {
		struct fib6_rule *r = (struct fib6_rule *)rule;

		/*
		 * If we need to find a source address for this traffic,
		 * we check the result if it meets requirement of the rule.
		 */
		if ((rule->flags & FIB_RULE_FIND_SADDR) &&
		    r->src.plen && !(flags & RT6_LOOKUP_F_HAS_SADDR)) {
			struct in6_addr saddr;
			if (ipv6_get_saddr(&rt->u.dst, &flp->fl6_dst,
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
	struct fib6_rule *rule6 = (struct fib6_rule *) rule;

	if (rule->action == FR_ACT_TO_TBL) {
		if (rule->table == RT6_TABLE_UNSPEC)
			goto errout;

		if (fib6_new_table(rule->table) == NULL) {
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

static u32 fib6_rule_default_pref(void)
{
	return 0x3FFF;
}

static size_t fib6_rule_nlmsg_payload(struct fib_rule *rule)
{
	return nla_total_size(16) /* dst */
	       + nla_total_size(16); /* src */
}

static struct fib_rules_ops fib6_rules_ops = {
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
	.rules_list		= LIST_HEAD_INIT(fib6_rules_ops.rules_list),
	.owner			= THIS_MODULE,
};

void __init fib6_rules_init(void)
{
	list_add_tail(&local_rule.common.list, &fib6_rules_ops.rules_list);
	list_add_tail(&main_rule.common.list, &fib6_rules_ops.rules_list);

	fib_rules_register(&fib6_rules_ops);
}

void fib6_rules_cleanup(void)
{
	fib_rules_unregister(&fib6_rules_ops);
}
