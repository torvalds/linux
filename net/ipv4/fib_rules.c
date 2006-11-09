/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 Forwarding Information Base: policy rules.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 * 		Thomas Graf <tgraf@suug.ch>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Fixes:
 * 		Rani Assaf	:	local_rule cannot be deleted
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
#include <net/ip.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/ip_fib.h>
#include <net/fib_rules.h>

static struct fib_rules_ops fib4_rules_ops;

struct fib4_rule
{
	struct fib_rule		common;
	u8			dst_len;
	u8			src_len;
	u8			tos;
	__be32			src;
	__be32			srcmask;
	__be32			dst;
	__be32			dstmask;
	u32			fwmark;
	u32			fwmask;
#ifdef CONFIG_NET_CLS_ROUTE
	u32			tclassid;
#endif
};

static struct fib4_rule default_rule = {
	.common = {
		.refcnt =	ATOMIC_INIT(2),
		.pref =		0x7FFF,
		.table =	RT_TABLE_DEFAULT,
		.action =	FR_ACT_TO_TBL,
	},
};

static struct fib4_rule main_rule = {
	.common = {
		.refcnt =	ATOMIC_INIT(2),
		.pref =		0x7FFE,
		.table =	RT_TABLE_MAIN,
		.action =	FR_ACT_TO_TBL,
	},
};

static struct fib4_rule local_rule = {
	.common = {
		.refcnt =	ATOMIC_INIT(2),
		.table =	RT_TABLE_LOCAL,
		.action =	FR_ACT_TO_TBL,
		.flags =	FIB_RULE_PERMANENT,
	},
};

static LIST_HEAD(fib4_rules);

#ifdef CONFIG_NET_CLS_ROUTE
u32 fib_rules_tclass(struct fib_result *res)
{
	return res->r ? ((struct fib4_rule *) res->r)->tclassid : 0;
}
#endif

int fib_lookup(struct flowi *flp, struct fib_result *res)
{
	struct fib_lookup_arg arg = {
		.result = res,
	};
	int err;

	err = fib_rules_lookup(&fib4_rules_ops, flp, 0, &arg);
	res->r = arg.rule;

	return err;
}

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

	if ((tbl = fib_get_table(rule->table)) == NULL)
		goto errout;

	err = tbl->tb_lookup(tbl, flp, (struct fib_result *) arg->result);
	if (err > 0)
		err = -EAGAIN;
errout:
	return err;
}


void fib_select_default(const struct flowi *flp, struct fib_result *res)
{
	if (res->r && res->r->action == FR_ACT_TO_TBL &&
	    FIB_RES_GW(*res) && FIB_RES_NH(*res).nh_scope == RT_SCOPE_LINK) {
		struct fib_table *tb;
		if ((tb = fib_get_table(res->r->table)) != NULL)
			tb->tb_select_default(tb, flp, res);
	}
}

static int fib4_rule_match(struct fib_rule *rule, struct flowi *fl, int flags)
{
	struct fib4_rule *r = (struct fib4_rule *) rule;
	__be32 daddr = fl->fl4_dst;
	__be32 saddr = fl->fl4_src;

	if (((saddr ^ r->src) & r->srcmask) ||
	    ((daddr ^ r->dst) & r->dstmask))
		return 0;

	if (r->tos && (r->tos != fl->fl4_tos))
		return 0;

	if ((r->fwmark ^ fl->mark) & r->fwmask)
		return 0;

	return 1;
}

static struct fib_table *fib_empty_table(void)
{
	u32 id;

	for (id = 1; id <= RT_TABLE_MAX; id++)
		if (fib_get_table(id) == NULL)
			return fib_new_table(id);
	return NULL;
}

static struct nla_policy fib4_rule_policy[FRA_MAX+1] __read_mostly = {
	[FRA_IFNAME]	= { .type = NLA_STRING, .len = IFNAMSIZ - 1 },
	[FRA_PRIORITY]	= { .type = NLA_U32 },
	[FRA_SRC]	= { .type = NLA_U32 },
	[FRA_DST]	= { .type = NLA_U32 },
	[FRA_FWMARK]	= { .type = NLA_U32 },
	[FRA_FWMASK]	= { .type = NLA_U32 },
	[FRA_FLOW]	= { .type = NLA_U32 },
	[FRA_TABLE]	= { .type = NLA_U32 },
};

static int fib4_rule_configure(struct fib_rule *rule, struct sk_buff *skb,
			       struct nlmsghdr *nlh, struct fib_rule_hdr *frh,
			       struct nlattr **tb)
{
	int err = -EINVAL;
	struct fib4_rule *rule4 = (struct fib4_rule *) rule;

	if (frh->src_len > 32 || frh->dst_len > 32 ||
	    (frh->tos & ~IPTOS_TOS_MASK))
		goto errout;

	if (rule->table == RT_TABLE_UNSPEC) {
		if (rule->action == FR_ACT_TO_TBL) {
			struct fib_table *table;

			table = fib_empty_table();
			if (table == NULL) {
				err = -ENOBUFS;
				goto errout;
			}

			rule->table = table->tb_id;
		}
	}

	if (tb[FRA_SRC])
		rule4->src = nla_get_be32(tb[FRA_SRC]);

	if (tb[FRA_DST])
		rule4->dst = nla_get_be32(tb[FRA_DST]);

	if (tb[FRA_FWMARK]) {
		rule4->fwmark = nla_get_u32(tb[FRA_FWMARK]);
		if (rule4->fwmark)
			/* compatibility: if the mark value is non-zero all bits
			 * are compared unless a mask is explicitly specified.
			 */
			rule4->fwmask = 0xFFFFFFFF;
	}

	if (tb[FRA_FWMASK])
		rule4->fwmask = nla_get_u32(tb[FRA_FWMASK]);

#ifdef CONFIG_NET_CLS_ROUTE
	if (tb[FRA_FLOW])
		rule4->tclassid = nla_get_u32(tb[FRA_FLOW]);
#endif

	rule4->src_len = frh->src_len;
	rule4->srcmask = inet_make_mask(rule4->src_len);
	rule4->dst_len = frh->dst_len;
	rule4->dstmask = inet_make_mask(rule4->dst_len);
	rule4->tos = frh->tos;

	err = 0;
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

	if (frh->tos && (rule4->tos != frh->tos))
		return 0;

	if (tb[FRA_FWMARK] && (rule4->fwmark != nla_get_u32(tb[FRA_FWMARK])))
		return 0;

	if (tb[FRA_FWMASK] && (rule4->fwmask != nla_get_u32(tb[FRA_FWMASK])))
		return 0;

#ifdef CONFIG_NET_CLS_ROUTE
	if (tb[FRA_FLOW] && (rule4->tclassid != nla_get_u32(tb[FRA_FLOW])))
		return 0;
#endif

	if (tb[FRA_SRC] && (rule4->src != nla_get_be32(tb[FRA_SRC])))
		return 0;

	if (tb[FRA_DST] && (rule4->dst != nla_get_be32(tb[FRA_DST])))
		return 0;

	return 1;
}

static int fib4_rule_fill(struct fib_rule *rule, struct sk_buff *skb,
			  struct nlmsghdr *nlh, struct fib_rule_hdr *frh)
{
	struct fib4_rule *rule4 = (struct fib4_rule *) rule;

	frh->family = AF_INET;
	frh->dst_len = rule4->dst_len;
	frh->src_len = rule4->src_len;
	frh->tos = rule4->tos;

	if (rule4->fwmark)
		NLA_PUT_U32(skb, FRA_FWMARK, rule4->fwmark);

	if (rule4->fwmask || rule4->fwmark)
		NLA_PUT_U32(skb, FRA_FWMASK, rule4->fwmask);

	if (rule4->dst_len)
		NLA_PUT_BE32(skb, FRA_DST, rule4->dst);

	if (rule4->src_len)
		NLA_PUT_BE32(skb, FRA_SRC, rule4->src);

#ifdef CONFIG_NET_CLS_ROUTE
	if (rule4->tclassid)
		NLA_PUT_U32(skb, FRA_FLOW, rule4->tclassid);
#endif
	return 0;

nla_put_failure:
	return -ENOBUFS;
}

int fib4_rules_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	return fib_rules_dump(skb, cb, AF_INET);
}

static u32 fib4_rule_default_pref(void)
{
	struct list_head *pos;
	struct fib_rule *rule;

	if (!list_empty(&fib4_rules)) {
		pos = fib4_rules.next;
		if (pos->next != &fib4_rules) {
			rule = list_entry(pos->next, struct fib_rule, list);
			if (rule->pref)
				return rule->pref - 1;
		}
	}

	return 0;
}

static struct fib_rules_ops fib4_rules_ops = {
	.family		= AF_INET,
	.rule_size	= sizeof(struct fib4_rule),
	.action		= fib4_rule_action,
	.match		= fib4_rule_match,
	.configure	= fib4_rule_configure,
	.compare	= fib4_rule_compare,
	.fill		= fib4_rule_fill,
	.default_pref	= fib4_rule_default_pref,
	.nlgroup	= RTNLGRP_IPV4_RULE,
	.policy		= fib4_rule_policy,
	.rules_list	= &fib4_rules,
	.owner		= THIS_MODULE,
};

void __init fib4_rules_init(void)
{
	list_add_tail(&local_rule.common.list, &fib4_rules);
	list_add_tail(&main_rule.common.list, &fib4_rules);
	list_add_tail(&default_rule.common.list, &fib4_rules);

	fib_rules_register(&fib4_rules_ops);
}
