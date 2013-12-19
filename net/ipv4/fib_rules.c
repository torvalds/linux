/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 Forwarding Information Base: policy rules.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *		Thomas Graf <tgraf@suug.ch>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Fixes:
 *		Rani Assaf	:	local_rule cannot be deleted
 *		Marc Boucher	:	routing by fwmark
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/ip_fib.h>
#include <net/fib_rules.h>

struct fib4_rule {
	struct fib_rule		common;
	u8			dst_len;
	u8			src_len;
	u8			tos;
	__be32			src;
	__be32			srcmask;
	__be32			dst;
	__be32			dstmask;
#ifdef CONFIG_IP_ROUTE_CLASSID
	u32			tclassid;
#endif
};

int __fib_lookup(struct net *net, struct flowi4 *flp, struct fib_result *res)
{
	struct fib_lookup_arg arg = {
		.result = res,
		.flags = FIB_LOOKUP_NOREF,
	};
	int err;

	err = fib_rules_lookup(net->ipv4.rules_ops, flowi4_to_flowi(flp), 0, &arg);
#ifdef CONFIG_IP_ROUTE_CLASSID
	if (arg.rule)
		res->tclassid = ((struct fib4_rule *)arg.rule)->tclassid;
	else
		res->tclassid = 0;
#endif
	return err;
}
EXPORT_SYMBOL_GPL(__fib_lookup);

static int fib4_rule_action(struct fib_rule *rule, struct flowi *flp,
			    int flags, struct fib_lookup_arg *arg)
{
	int err = -EAGAIN;
	struct fib_table *tbl;

	switch (rule->action) {
	case FR_ACT_TO_TBL:
		break;

	case FR_ACT_UNREACHABLE:
		err = -ENETUNREACH;
		goto errout;

	case FR_ACT_PROHIBIT:
		err = -EACCES;
		goto errout;

	case FR_ACT_BLACKHOLE:
	default:
		err = -EINVAL;
		goto errout;
	}

	tbl = fib_get_table(rule->fr_net, rule->table);
	if (!tbl)
		goto errout;

	err = fib_table_lookup(tbl, &flp->u.ip4, (struct fib_result *) arg->result, arg->flags);
	if (err > 0)
		err = -EAGAIN;
errout:
	return err;
}

static bool fib4_rule_suppress(struct fib_rule *rule, struct fib_lookup_arg *arg)
{
	struct fib_result *result = (struct fib_result *) arg->result;
	struct net_device *dev = NULL;

	if (result->fi)
		dev = result->fi->fib_dev;

	/* do not accept result if the route does
	 * not meet the required prefix length
	 */
	if (result->prefixlen <= rule->suppress_prefixlen)
		goto suppress_route;

	/* do not accept result if the route uses a device
	 * belonging to a forbidden interface group
	 */
	if (rule->suppress_ifgroup != -1 && dev && dev->group == rule->suppress_ifgroup)
		goto suppress_route;

	return false;

suppress_route:
	if (!(arg->flags & FIB_LOOKUP_NOREF))
		fib_info_put(result->fi);
	return true;
}

static int fib4_rule_match(struct fib_rule *rule, struct flowi *fl, int flags)
{
	struct fib4_rule *r = (struct fib4_rule *) rule;
	struct flowi4 *fl4 = &fl->u.ip4;
	__be32 daddr = fl4->daddr;
	__be32 saddr = fl4->saddr;

	if (((saddr ^ r->src) & r->srcmask) ||
	    ((daddr ^ r->dst) & r->dstmask))
		return 0;

	if (r->tos && (r->tos != fl4->flowi4_tos))
		return 0;

	return 1;
}

static struct fib_table *fib_empty_table(struct net *net)
{
	u32 id;

	for (id = 1; id <= RT_TABLE_MAX; id++)
		if (fib_get_table(net, id) == NULL)
			return fib_new_table(net, id);
	return NULL;
}

static const struct nla_policy fib4_rule_policy[FRA_MAX+1] = {
	FRA_GENERIC_POLICY,
	[FRA_FLOW]	= { .type = NLA_U32 },
};

static int fib4_rule_configure(struct fib_rule *rule, struct sk_buff *skb,
			       struct fib_rule_hdr *frh,
			       struct nlattr **tb)
{
	struct net *net = sock_net(skb->sk);
	int err = -EINVAL;
	struct fib4_rule *rule4 = (struct fib4_rule *) rule;

	if (frh->tos & ~IPTOS_TOS_MASK)
		goto errout;

	if (rule->table == RT_TABLE_UNSPEC) {
		if (rule->action == FR_ACT_TO_TBL) {
			struct fib_table *table;

			table = fib_empty_table(net);
			if (table == NULL) {
				err = -ENOBUFS;
				goto errout;
			}

			rule->table = table->tb_id;
		}
	}

	if (frh->src_len)
		rule4->src = nla_get_be32(tb[FRA_SRC]);

	if (frh->dst_len)
		rule4->dst = nla_get_be32(tb[FRA_DST]);

#ifdef CONFIG_IP_ROUTE_CLASSID
	if (tb[FRA_FLOW]) {
		rule4->tclassid = nla_get_u32(tb[FRA_FLOW]);
		if (rule4->tclassid)
			net->ipv4.fib_num_tclassid_users++;
	}
#endif

	rule4->src_len = frh->src_len;
	rule4->srcmask = inet_make_mask(rule4->src_len);
	rule4->dst_len = frh->dst_len;
	rule4->dstmask = inet_make_mask(rule4->dst_len);
	rule4->tos = frh->tos;

	net->ipv4.fib_has_custom_rules = true;
	err = 0;
errout:
	return err;
}

static void fib4_rule_delete(struct fib_rule *rule)
{
	struct net *net = rule->fr_net;
#ifdef CONFIG_IP_ROUTE_CLASSID
	struct fib4_rule *rule4 = (struct fib4_rule *) rule;

	if (rule4->tclassid)
		net->ipv4.fib_num_tclassid_users--;
#endif
	net->ipv4.fib_has_custom_rules = true;
}

static int fib4_rule_compare(struct fib_rule *rule, struct fib_rule_hdr *frh,
			     struct nlattr **tb)
{
	struct fib4_rule *rule4 = (struct fib4_rule *) rule;

	if (frh->src_len && (rule4->src_len != frh->src_len))
		return 0;

	if (frh->dst_len && (rule4->dst_len != frh->dst_len))
		return 0;

	if (frh->tos && (rule4->tos != frh->tos))
		return 0;

#ifdef CONFIG_IP_ROUTE_CLASSID
	if (tb[FRA_FLOW] && (rule4->tclassid != nla_get_u32(tb[FRA_FLOW])))
		return 0;
#endif

	if (frh->src_len && (rule4->src != nla_get_be32(tb[FRA_SRC])))
		return 0;

	if (frh->dst_len && (rule4->dst != nla_get_be32(tb[FRA_DST])))
		return 0;

	return 1;
}

static int fib4_rule_fill(struct fib_rule *rule, struct sk_buff *skb,
			  struct fib_rule_hdr *frh)
{
	struct fib4_rule *rule4 = (struct fib4_rule *) rule;

	frh->dst_len = rule4->dst_len;
	frh->src_len = rule4->src_len;
	frh->tos = rule4->tos;

	if ((rule4->dst_len &&
	     nla_put_be32(skb, FRA_DST, rule4->dst)) ||
	    (rule4->src_len &&
	     nla_put_be32(skb, FRA_SRC, rule4->src)))
		goto nla_put_failure;
#ifdef CONFIG_IP_ROUTE_CLASSID
	if (rule4->tclassid &&
	    nla_put_u32(skb, FRA_FLOW, rule4->tclassid))
		goto nla_put_failure;
#endif
	return 0;

nla_put_failure:
	return -ENOBUFS;
}

static size_t fib4_rule_nlmsg_payload(struct fib_rule *rule)
{
	return nla_total_size(4) /* dst */
	       + nla_total_size(4) /* src */
	       + nla_total_size(4); /* flow */
}

static void fib4_rule_flush_cache(struct fib_rules_ops *ops)
{
	rt_cache_flush(ops->fro_net);
}

static const struct fib_rules_ops __net_initconst fib4_rules_ops_template = {
	.family		= AF_INET,
	.rule_size	= sizeof(struct fib4_rule),
	.addr_size	= sizeof(u32),
	.action		= fib4_rule_action,
	.suppress	= fib4_rule_suppress,
	.match		= fib4_rule_match,
	.configure	= fib4_rule_configure,
	.delete		= fib4_rule_delete,
	.compare	= fib4_rule_compare,
	.fill		= fib4_rule_fill,
	.default_pref	= fib_default_rule_pref,
	.nlmsg_payload	= fib4_rule_nlmsg_payload,
	.flush_cache	= fib4_rule_flush_cache,
	.nlgroup	= RTNLGRP_IPV4_RULE,
	.policy		= fib4_rule_policy,
	.owner		= THIS_MODULE,
};

static int fib_default_rules_init(struct fib_rules_ops *ops)
{
	int err;

	err = fib_default_rule_add(ops, 0, RT_TABLE_LOCAL, 0);
	if (err < 0)
		return err;
	err = fib_default_rule_add(ops, 0x7FFE, RT_TABLE_MAIN, 0);
	if (err < 0)
		return err;
	err = fib_default_rule_add(ops, 0x7FFF, RT_TABLE_DEFAULT, 0);
	if (err < 0)
		return err;
	return 0;
}

int __net_init fib4_rules_init(struct net *net)
{
	int err;
	struct fib_rules_ops *ops;

	ops = fib_rules_register(&fib4_rules_ops_template, net);
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	err = fib_default_rules_init(ops);
	if (err < 0)
		goto fail;
	net->ipv4.rules_ops = ops;
	net->ipv4.fib_has_custom_rules = false;
	return 0;

fail:
	/* also cleans all rules already added */
	fib_rules_unregister(ops);
	return err;
}

void __net_exit fib4_rules_exit(struct net *net)
{
	fib_rules_unregister(net->ipv4.rules_ops);
}
