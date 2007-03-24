
/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Routing Forwarding Information Base (Rules)
 *
 * Author:      Steve Whitehouse <SteveW@ACM.org>
 *              Mostly copied from Alexey Kuznetsov's ipv4/fib_rules.c
 *
 *
 * Changes:
 *              Steve Whitehouse <steve@chygwyn.com>
 *              Updated for Thomas Graf's generic rules
 *
 */
#include <linux/net.h>
#include <linux/init.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/fib_rules.h>
#include <net/dn.h>
#include <net/dn_fib.h>
#include <net/dn_neigh.h>
#include <net/dn_dev.h>

static struct fib_rules_ops dn_fib_rules_ops;

struct dn_fib_rule
{
	struct fib_rule		common;
	unsigned char		dst_len;
	unsigned char		src_len;
	__le16			src;
	__le16			srcmask;
	__le16			dst;
	__le16			dstmask;
	__le16			srcmap;
	u8			flags;
};

static struct dn_fib_rule default_rule = {
	.common = {
		.refcnt =		ATOMIC_INIT(2),
		.pref =			0x7fff,
		.table =		RT_TABLE_MAIN,
		.action =		FR_ACT_TO_TBL,
	},
};

static LIST_HEAD(dn_fib_rules);


int dn_fib_lookup(struct flowi *flp, struct dn_fib_res *res)
{
	struct fib_lookup_arg arg = {
		.result = res,
	};
	int err;

	err = fib_rules_lookup(&dn_fib_rules_ops, flp, 0, &arg);
	res->r = arg.rule;

	return err;
}

static int dn_fib_rule_action(struct fib_rule *rule, struct flowi *flp,
			      int flags, struct fib_lookup_arg *arg)
{
	int err = -EAGAIN;
	struct dn_fib_table *tbl;

	switch(rule->action) {
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

	tbl = dn_fib_get_table(rule->table, 0);
	if (tbl == NULL)
		goto errout;

	err = tbl->lookup(tbl, flp, (struct dn_fib_res *)arg->result);
	if (err > 0)
		err = -EAGAIN;
errout:
	return err;
}

static struct nla_policy dn_fib_rule_policy[FRA_MAX+1] __read_mostly = {
	FRA_GENERIC_POLICY,
};

static int dn_fib_rule_match(struct fib_rule *rule, struct flowi *fl, int flags)
{
	struct dn_fib_rule *r = (struct dn_fib_rule *)rule;
	__le16 daddr = fl->fld_dst;
	__le16 saddr = fl->fld_src;

	if (((saddr ^ r->src) & r->srcmask) ||
	    ((daddr ^ r->dst) & r->dstmask))
		return 0;

	return 1;
}

static int dn_fib_rule_configure(struct fib_rule *rule, struct sk_buff *skb,
				 struct nlmsghdr *nlh, struct fib_rule_hdr *frh,
				 struct nlattr **tb)
{
	int err = -EINVAL;
	struct dn_fib_rule *r = (struct dn_fib_rule *)rule;

	if (frh->tos)
		goto  errout;

	if (rule->table == RT_TABLE_UNSPEC) {
		if (rule->action == FR_ACT_TO_TBL) {
			struct dn_fib_table *table;

			table = dn_fib_empty_table();
			if (table == NULL) {
				err = -ENOBUFS;
				goto errout;
			}

			rule->table = table->n;
		}
	}

	if (frh->src_len)
		r->src = nla_get_le16(tb[FRA_SRC]);

	if (frh->dst_len)
		r->dst = nla_get_le16(tb[FRA_DST]);

	r->src_len = frh->src_len;
	r->srcmask = dnet_make_mask(r->src_len);
	r->dst_len = frh->dst_len;
	r->dstmask = dnet_make_mask(r->dst_len);
	err = 0;
errout:
	return err;
}

static int dn_fib_rule_compare(struct fib_rule *rule, struct fib_rule_hdr *frh,
			       struct nlattr **tb)
{
	struct dn_fib_rule *r = (struct dn_fib_rule *)rule;

	if (frh->src_len && (r->src_len != frh->src_len))
		return 0;

	if (frh->dst_len && (r->dst_len != frh->dst_len))
		return 0;

	if (frh->src_len && (r->src != nla_get_le16(tb[FRA_SRC])))
		return 0;

	if (frh->dst_len && (r->dst != nla_get_le16(tb[FRA_DST])))
		return 0;

	return 1;
}

unsigned dnet_addr_type(__le16 addr)
{
	struct flowi fl = { .nl_u = { .dn_u = { .daddr = addr } } };
	struct dn_fib_res res;
	unsigned ret = RTN_UNICAST;
	struct dn_fib_table *tb = dn_fib_get_table(RT_TABLE_LOCAL, 0);

	res.r = NULL;

	if (tb) {
		if (!tb->lookup(tb, &fl, &res)) {
			ret = res.type;
			dn_fib_res_put(&res);
		}
	}
	return ret;
}

static int dn_fib_rule_fill(struct fib_rule *rule, struct sk_buff *skb,
			    struct nlmsghdr *nlh, struct fib_rule_hdr *frh)
{
	struct dn_fib_rule *r = (struct dn_fib_rule *)rule;

	frh->family = AF_DECnet;
	frh->dst_len = r->dst_len;
	frh->src_len = r->src_len;
	frh->tos = 0;

	if (r->dst_len)
		NLA_PUT_LE16(skb, FRA_DST, r->dst);
	if (r->src_len)
		NLA_PUT_LE16(skb, FRA_SRC, r->src);

	return 0;

nla_put_failure:
	return -ENOBUFS;
}

static u32 dn_fib_rule_default_pref(void)
{
	struct list_head *pos;
	struct fib_rule *rule;

	if (!list_empty(&dn_fib_rules)) {
		pos = dn_fib_rules.next;
		if (pos->next != &dn_fib_rules) {
			rule = list_entry(pos->next, struct fib_rule, list);
			if (rule->pref)
				return rule->pref - 1;
		}
	}

	return 0;
}

int dn_fib_dump_rules(struct sk_buff *skb, struct netlink_callback *cb)
{
	return fib_rules_dump(skb, cb, AF_DECnet);
}

static struct fib_rules_ops dn_fib_rules_ops = {
	.family		= AF_DECnet,
	.rule_size	= sizeof(struct dn_fib_rule),
	.addr_size	= sizeof(u16),
	.action		= dn_fib_rule_action,
	.match		= dn_fib_rule_match,
	.configure	= dn_fib_rule_configure,
	.compare	= dn_fib_rule_compare,
	.fill		= dn_fib_rule_fill,
	.default_pref	= dn_fib_rule_default_pref,
	.nlgroup	= RTNLGRP_DECnet_RULE,
	.policy		= dn_fib_rule_policy,
	.rules_list	= &dn_fib_rules,
	.owner		= THIS_MODULE,
};

void __init dn_fib_rules_init(void)
{
	list_add_tail(&default_rule.common.list, &dn_fib_rules);
	fib_rules_register(&dn_fib_rules_ops);
}

void __exit dn_fib_rules_cleanup(void)
{
	fib_rules_unregister(&dn_fib_rules_ops);
}


