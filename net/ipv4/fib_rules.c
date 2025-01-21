// SPDX-License-Identifier: GPL-2.0-or-later
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
#include <net/inet_dscp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/ip_fib.h>
#include <net/nexthop.h>
#include <net/fib_rules.h>
#include <linux/indirect_call_wrapper.h>

struct fib4_rule {
	struct fib_rule		common;
	u8			dst_len;
	u8			src_len;
	dscp_t			dscp;
	u8			dscp_full:1;	/* DSCP or TOS selector */
	__be32			src;
	__be32			srcmask;
	__be32			dst;
	__be32			dstmask;
#ifdef CONFIG_IP_ROUTE_CLASSID
	u32			tclassid;
#endif
};

static bool fib4_rule_matchall(const struct fib_rule *rule)
{
	struct fib4_rule *r = container_of(rule, struct fib4_rule, common);

	if (r->dst_len || r->src_len || r->dscp)
		return false;
	return fib_rule_matchall(rule);
}

bool fib4_rule_default(const struct fib_rule *rule)
{
	if (!fib4_rule_matchall(rule) || rule->action != FR_ACT_TO_TBL ||
	    rule->l3mdev)
		return false;
	if (rule->table != RT_TABLE_LOCAL && rule->table != RT_TABLE_MAIN &&
	    rule->table != RT_TABLE_DEFAULT)
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(fib4_rule_default);

int fib4_rules_dump(struct net *net, struct notifier_block *nb,
		    struct netlink_ext_ack *extack)
{
	return fib_rules_dump(net, nb, AF_INET, extack);
}

unsigned int fib4_rules_seq_read(const struct net *net)
{
	return fib_rules_seq_read(net, AF_INET);
}

int __fib_lookup(struct net *net, struct flowi4 *flp,
		 struct fib_result *res, unsigned int flags)
{
	struct fib_lookup_arg arg = {
		.result = res,
		.flags = flags,
	};
	int err;

	/* update flow if oif or iif point to device enslaved to l3mdev */
	l3mdev_update_flow(net, flowi4_to_flowi(flp));

	err = fib_rules_lookup(net->ipv4.rules_ops, flowi4_to_flowi(flp), 0, &arg);
#ifdef CONFIG_IP_ROUTE_CLASSID
	if (arg.rule)
		res->tclassid = ((struct fib4_rule *)arg.rule)->tclassid;
	else
		res->tclassid = 0;
#endif

	if (err == -ESRCH)
		err = -ENETUNREACH;

	return err;
}
EXPORT_SYMBOL_GPL(__fib_lookup);

INDIRECT_CALLABLE_SCOPE int fib4_rule_action(struct fib_rule *rule,
					     struct flowi *flp, int flags,
					     struct fib_lookup_arg *arg)
{
	int err = -EAGAIN;
	struct fib_table *tbl;
	u32 tb_id;

	switch (rule->action) {
	case FR_ACT_TO_TBL:
		break;

	case FR_ACT_UNREACHABLE:
		return -ENETUNREACH;

	case FR_ACT_PROHIBIT:
		return -EACCES;

	case FR_ACT_BLACKHOLE:
	default:
		return -EINVAL;
	}

	rcu_read_lock();

	tb_id = fib_rule_get_table(rule, arg);
	tbl = fib_get_table(rule->fr_net, tb_id);
	if (tbl)
		err = fib_table_lookup(tbl, &flp->u.ip4,
				       (struct fib_result *)arg->result,
				       arg->flags);

	rcu_read_unlock();
	return err;
}

INDIRECT_CALLABLE_SCOPE bool fib4_rule_suppress(struct fib_rule *rule,
						int flags,
						struct fib_lookup_arg *arg)
{
	struct fib_result *result = arg->result;
	struct net_device *dev = NULL;

	if (result->fi) {
		struct fib_nh_common *nhc = fib_info_nhc(result->fi, 0);

		dev = nhc->nhc_dev;
	}

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

INDIRECT_CALLABLE_SCOPE int fib4_rule_match(struct fib_rule *rule,
					    struct flowi *fl, int flags)
{
	struct fib4_rule *r = (struct fib4_rule *) rule;
	struct flowi4 *fl4 = &fl->u.ip4;
	__be32 daddr = fl4->daddr;
	__be32 saddr = fl4->saddr;

	if (((saddr ^ r->src) & r->srcmask) ||
	    ((daddr ^ r->dst) & r->dstmask))
		return 0;

	/* When DSCP selector is used we need to match on the entire DSCP field
	 * in the flow information structure. When TOS selector is used we need
	 * to mask the upper three DSCP bits prior to matching to maintain
	 * legacy behavior.
	 */
	if (r->dscp_full && r->dscp != inet_dsfield_to_dscp(fl4->flowi4_tos))
		return 0;
	else if (!r->dscp_full && r->dscp &&
		 !fib_dscp_masked_match(r->dscp, fl4))
		return 0;

	if (rule->ip_proto && (rule->ip_proto != fl4->flowi4_proto))
		return 0;

	if (fib_rule_port_range_set(&rule->sport_range) &&
	    !fib_rule_port_inrange(&rule->sport_range, fl4->fl4_sport))
		return 0;

	if (fib_rule_port_range_set(&rule->dport_range) &&
	    !fib_rule_port_inrange(&rule->dport_range, fl4->fl4_dport))
		return 0;

	return 1;
}

static struct fib_table *fib_empty_table(struct net *net)
{
	u32 id = 1;

	while (1) {
		if (!fib_get_table(net, id))
			return fib_new_table(net, id);

		if (id++ == RT_TABLE_MAX)
			break;
	}
	return NULL;
}

static int fib4_nl2rule_dscp(const struct nlattr *nla, struct fib4_rule *rule4,
			     struct netlink_ext_ack *extack)
{
	if (rule4->dscp) {
		NL_SET_ERR_MSG(extack, "Cannot specify both TOS and DSCP");
		return -EINVAL;
	}

	rule4->dscp = inet_dsfield_to_dscp(nla_get_u8(nla) << 2);
	rule4->dscp_full = true;

	return 0;
}

static int fib4_rule_configure(struct fib_rule *rule, struct sk_buff *skb,
			       struct fib_rule_hdr *frh,
			       struct nlattr **tb,
			       struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	int err = -EINVAL;
	struct fib4_rule *rule4 = (struct fib4_rule *) rule;

	if (!inet_validate_dscp(frh->tos)) {
		NL_SET_ERR_MSG(extack,
			       "Invalid dsfield (tos): ECN bits must be 0");
		goto errout;
	}
	/* IPv4 currently doesn't handle high order DSCP bits correctly */
	if (frh->tos & ~IPTOS_TOS_MASK) {
		NL_SET_ERR_MSG(extack, "Invalid tos");
		goto errout;
	}
	rule4->dscp = inet_dsfield_to_dscp(frh->tos);

	if (tb[FRA_DSCP] &&
	    fib4_nl2rule_dscp(tb[FRA_DSCP], rule4, extack) < 0)
		goto errout;

	/* split local/main if they are not already split */
	err = fib_unmerge(net);
	if (err)
		goto errout;

	if (rule->table == RT_TABLE_UNSPEC && !rule->l3mdev) {
		if (rule->action == FR_ACT_TO_TBL) {
			struct fib_table *table;

			table = fib_empty_table(net);
			if (!table) {
				err = -ENOBUFS;
				goto errout;
			}

			rule->table = table->tb_id;
		}
	}

	if (frh->src_len)
		rule4->src = nla_get_in_addr(tb[FRA_SRC]);

	if (frh->dst_len)
		rule4->dst = nla_get_in_addr(tb[FRA_DST]);

#ifdef CONFIG_IP_ROUTE_CLASSID
	if (tb[FRA_FLOW]) {
		rule4->tclassid = nla_get_u32(tb[FRA_FLOW]);
		if (rule4->tclassid)
			atomic_inc(&net->ipv4.fib_num_tclassid_users);
	}
#endif

	if (fib_rule_requires_fldissect(rule))
		net->ipv4.fib_rules_require_fldissect++;

	rule4->src_len = frh->src_len;
	rule4->srcmask = inet_make_mask(rule4->src_len);
	rule4->dst_len = frh->dst_len;
	rule4->dstmask = inet_make_mask(rule4->dst_len);

	net->ipv4.fib_has_custom_rules = true;

	err = 0;
errout:
	return err;
}

static int fib4_rule_delete(struct fib_rule *rule)
{
	struct net *net = rule->fr_net;
	int err;

	/* split local/main if they are not already split */
	err = fib_unmerge(net);
	if (err)
		goto errout;

#ifdef CONFIG_IP_ROUTE_CLASSID
	if (((struct fib4_rule *)rule)->tclassid)
		atomic_dec(&net->ipv4.fib_num_tclassid_users);
#endif
	net->ipv4.fib_has_custom_rules = true;

	if (net->ipv4.fib_rules_require_fldissect &&
	    fib_rule_requires_fldissect(rule))
		net->ipv4.fib_rules_require_fldissect--;
errout:
	return err;
}

static int fib4_rule_compare(struct fib_rule *rule, struct fib_rule_hdr *frh,
			     struct nlattr **tb)
{
	struct fib4_rule *rule4 = (struct fib4_rule *) rule;

	if (frh->src_len && (rule4->src_len != frh->src_len))
		return 0;

	if (frh->dst_len && (rule4->dst_len != frh->dst_len))
		return 0;

	if (frh->tos &&
	    (rule4->dscp_full ||
	     inet_dscp_to_dsfield(rule4->dscp) != frh->tos))
		return 0;

	if (tb[FRA_DSCP]) {
		dscp_t dscp;

		dscp = inet_dsfield_to_dscp(nla_get_u8(tb[FRA_DSCP]) << 2);
		if (!rule4->dscp_full || rule4->dscp != dscp)
			return 0;
	}

#ifdef CONFIG_IP_ROUTE_CLASSID
	if (tb[FRA_FLOW] && (rule4->tclassid != nla_get_u32(tb[FRA_FLOW])))
		return 0;
#endif

	if (frh->src_len && (rule4->src != nla_get_in_addr(tb[FRA_SRC])))
		return 0;

	if (frh->dst_len && (rule4->dst != nla_get_in_addr(tb[FRA_DST])))
		return 0;

	return 1;
}

static int fib4_rule_fill(struct fib_rule *rule, struct sk_buff *skb,
			  struct fib_rule_hdr *frh)
{
	struct fib4_rule *rule4 = (struct fib4_rule *) rule;

	frh->dst_len = rule4->dst_len;
	frh->src_len = rule4->src_len;

	if (rule4->dscp_full) {
		frh->tos = 0;
		if (nla_put_u8(skb, FRA_DSCP,
			       inet_dscp_to_dsfield(rule4->dscp) >> 2))
			goto nla_put_failure;
	} else {
		frh->tos = inet_dscp_to_dsfield(rule4->dscp);
	}

	if ((rule4->dst_len &&
	     nla_put_in_addr(skb, FRA_DST, rule4->dst)) ||
	    (rule4->src_len &&
	     nla_put_in_addr(skb, FRA_SRC, rule4->src)))
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
	       + nla_total_size(4) /* flow */
	       + nla_total_size(1); /* dscp */
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
	.nlmsg_payload	= fib4_rule_nlmsg_payload,
	.flush_cache	= fib4_rule_flush_cache,
	.nlgroup	= RTNLGRP_IPV4_RULE,
	.owner		= THIS_MODULE,
};

static int fib_default_rules_init(struct fib_rules_ops *ops)
{
	int err;

	err = fib_default_rule_add(ops, 0, RT_TABLE_LOCAL);
	if (err < 0)
		return err;
	err = fib_default_rule_add(ops, 0x7FFE, RT_TABLE_MAIN);
	if (err < 0)
		return err;
	err = fib_default_rule_add(ops, 0x7FFF, RT_TABLE_DEFAULT);
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
	net->ipv4.fib_rules_require_fldissect = 0;
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
