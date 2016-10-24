/*
 * Copyright (c) 2008, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Alexander Duyck <alexander.h.duyck@intel.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>

#include <linux/tc_act/tc_skbedit.h>
#include <net/tc_act/tc_skbedit.h>

#define SKBEDIT_TAB_MASK     15

static int skbedit_net_id;
static struct tc_action_ops act_skbedit_ops;

static int tcf_skbedit(struct sk_buff *skb, const struct tc_action *a,
		       struct tcf_result *res)
{
	struct tcf_skbedit *d = to_skbedit(a);

	spin_lock(&d->tcf_lock);
	tcf_lastuse_update(&d->tcf_tm);
	bstats_update(&d->tcf_bstats, skb);

	if (d->flags & SKBEDIT_F_PRIORITY)
		skb->priority = d->priority;
	if (d->flags & SKBEDIT_F_QUEUE_MAPPING &&
	    skb->dev->real_num_tx_queues > d->queue_mapping)
		skb_set_queue_mapping(skb, d->queue_mapping);
	if (d->flags & SKBEDIT_F_MARK) {
		skb->mark &= ~d->mask;
		skb->mark |= d->mark & d->mask;
	}
	if (d->flags & SKBEDIT_F_PTYPE)
		skb->pkt_type = d->ptype;

	spin_unlock(&d->tcf_lock);
	return d->tcf_action;
}

static const struct nla_policy skbedit_policy[TCA_SKBEDIT_MAX + 1] = {
	[TCA_SKBEDIT_PARMS]		= { .len = sizeof(struct tc_skbedit) },
	[TCA_SKBEDIT_PRIORITY]		= { .len = sizeof(u32) },
	[TCA_SKBEDIT_QUEUE_MAPPING]	= { .len = sizeof(u16) },
	[TCA_SKBEDIT_MARK]		= { .len = sizeof(u32) },
	[TCA_SKBEDIT_PTYPE]		= { .len = sizeof(u16) },
	[TCA_SKBEDIT_MASK]		= { .len = sizeof(u32) },
};

static int tcf_skbedit_init(struct net *net, struct nlattr *nla,
			    struct nlattr *est, struct tc_action **a,
			    int ovr, int bind)
{
	struct tc_action_net *tn = net_generic(net, skbedit_net_id);
	struct nlattr *tb[TCA_SKBEDIT_MAX + 1];
	struct tc_skbedit *parm;
	struct tcf_skbedit *d;
	u32 flags = 0, *priority = NULL, *mark = NULL, *mask = NULL;
	u16 *queue_mapping = NULL, *ptype = NULL;
	bool exists = false;
	int ret = 0, err;

	if (nla == NULL)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_SKBEDIT_MAX, nla, skbedit_policy);
	if (err < 0)
		return err;

	if (tb[TCA_SKBEDIT_PARMS] == NULL)
		return -EINVAL;

	if (tb[TCA_SKBEDIT_PRIORITY] != NULL) {
		flags |= SKBEDIT_F_PRIORITY;
		priority = nla_data(tb[TCA_SKBEDIT_PRIORITY]);
	}

	if (tb[TCA_SKBEDIT_QUEUE_MAPPING] != NULL) {
		flags |= SKBEDIT_F_QUEUE_MAPPING;
		queue_mapping = nla_data(tb[TCA_SKBEDIT_QUEUE_MAPPING]);
	}

	if (tb[TCA_SKBEDIT_PTYPE] != NULL) {
		ptype = nla_data(tb[TCA_SKBEDIT_PTYPE]);
		if (!skb_pkt_type_ok(*ptype))
			return -EINVAL;
		flags |= SKBEDIT_F_PTYPE;
	}

	if (tb[TCA_SKBEDIT_MARK] != NULL) {
		flags |= SKBEDIT_F_MARK;
		mark = nla_data(tb[TCA_SKBEDIT_MARK]);
	}

	if (tb[TCA_SKBEDIT_MASK] != NULL) {
		flags |= SKBEDIT_F_MASK;
		mask = nla_data(tb[TCA_SKBEDIT_MASK]);
	}

	parm = nla_data(tb[TCA_SKBEDIT_PARMS]);

	exists = tcf_hash_check(tn, parm->index, a, bind);
	if (exists && bind)
		return 0;

	if (!flags) {
		tcf_hash_release(*a, bind);
		return -EINVAL;
	}

	if (!exists) {
		ret = tcf_hash_create(tn, parm->index, est, a,
				      &act_skbedit_ops, bind, false);
		if (ret)
			return ret;

		d = to_skbedit(*a);
		ret = ACT_P_CREATED;
	} else {
		d = to_skbedit(*a);
		tcf_hash_release(*a, bind);
		if (!ovr)
			return -EEXIST;
	}

	spin_lock_bh(&d->tcf_lock);

	d->flags = flags;
	if (flags & SKBEDIT_F_PRIORITY)
		d->priority = *priority;
	if (flags & SKBEDIT_F_QUEUE_MAPPING)
		d->queue_mapping = *queue_mapping;
	if (flags & SKBEDIT_F_MARK)
		d->mark = *mark;
	if (flags & SKBEDIT_F_PTYPE)
		d->ptype = *ptype;
	/* default behaviour is to use all the bits */
	d->mask = 0xffffffff;
	if (flags & SKBEDIT_F_MASK)
		d->mask = *mask;

	d->tcf_action = parm->action;

	spin_unlock_bh(&d->tcf_lock);

	if (ret == ACT_P_CREATED)
		tcf_hash_insert(tn, *a);
	return ret;
}

static int tcf_skbedit_dump(struct sk_buff *skb, struct tc_action *a,
			    int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_skbedit *d = to_skbedit(a);
	struct tc_skbedit opt = {
		.index   = d->tcf_index,
		.refcnt  = d->tcf_refcnt - ref,
		.bindcnt = d->tcf_bindcnt - bind,
		.action  = d->tcf_action,
	};
	struct tcf_t t;

	if (nla_put(skb, TCA_SKBEDIT_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;
	if ((d->flags & SKBEDIT_F_PRIORITY) &&
	    nla_put_u32(skb, TCA_SKBEDIT_PRIORITY, d->priority))
		goto nla_put_failure;
	if ((d->flags & SKBEDIT_F_QUEUE_MAPPING) &&
	    nla_put_u16(skb, TCA_SKBEDIT_QUEUE_MAPPING, d->queue_mapping))
		goto nla_put_failure;
	if ((d->flags & SKBEDIT_F_MARK) &&
	    nla_put_u32(skb, TCA_SKBEDIT_MARK, d->mark))
		goto nla_put_failure;
	if ((d->flags & SKBEDIT_F_PTYPE) &&
	    nla_put_u16(skb, TCA_SKBEDIT_PTYPE, d->ptype))
		goto nla_put_failure;
	if ((d->flags & SKBEDIT_F_MASK) &&
	    nla_put_u32(skb, TCA_SKBEDIT_MASK, d->mask))
		goto nla_put_failure;

	tcf_tm_dump(&t, &d->tcf_tm);
	if (nla_put_64bit(skb, TCA_SKBEDIT_TM, sizeof(t), &t, TCA_SKBEDIT_PAD))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int tcf_skbedit_walker(struct net *net, struct sk_buff *skb,
			      struct netlink_callback *cb, int type,
			      const struct tc_action_ops *ops)
{
	struct tc_action_net *tn = net_generic(net, skbedit_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops);
}

static int tcf_skbedit_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, skbedit_net_id);

	return tcf_hash_search(tn, a, index);
}

static struct tc_action_ops act_skbedit_ops = {
	.kind		=	"skbedit",
	.type		=	TCA_ACT_SKBEDIT,
	.owner		=	THIS_MODULE,
	.act		=	tcf_skbedit,
	.dump		=	tcf_skbedit_dump,
	.init		=	tcf_skbedit_init,
	.walk		=	tcf_skbedit_walker,
	.lookup		=	tcf_skbedit_search,
	.size		=	sizeof(struct tcf_skbedit),
};

static __net_init int skbedit_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, skbedit_net_id);

	return tc_action_net_init(tn, &act_skbedit_ops, SKBEDIT_TAB_MASK);
}

static void __net_exit skbedit_exit_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, skbedit_net_id);

	tc_action_net_exit(tn);
}

static struct pernet_operations skbedit_net_ops = {
	.init = skbedit_init_net,
	.exit = skbedit_exit_net,
	.id   = &skbedit_net_id,
	.size = sizeof(struct tc_action_net),
};

MODULE_AUTHOR("Alexander Duyck, <alexander.h.duyck@intel.com>");
MODULE_DESCRIPTION("SKB Editing");
MODULE_LICENSE("GPL");

static int __init skbedit_init_module(void)
{
	return tcf_register_action(&act_skbedit_ops, &skbedit_net_ops);
}

static void __exit skbedit_cleanup_module(void)
{
	tcf_unregister_action(&act_skbedit_ops, &skbedit_net_ops);
}

module_init(skbedit_init_module);
module_exit(skbedit_cleanup_module);
