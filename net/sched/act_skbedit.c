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

static int tcf_skbedit(struct sk_buff *skb, const struct tc_action *a,
		       struct tcf_result *res)
{
	struct tcf_skbedit *d = a->priv;

	spin_lock(&d->tcf_lock);
	d->tcf_tm.lastuse = jiffies;
	bstats_update(&d->tcf_bstats, skb);

	if (d->flags & SKBEDIT_F_PRIORITY)
		skb->priority = d->priority;
	if (d->flags & SKBEDIT_F_QUEUE_MAPPING &&
	    skb->dev->real_num_tx_queues > d->queue_mapping)
		skb_set_queue_mapping(skb, d->queue_mapping);
	if (d->flags & SKBEDIT_F_MARK)
		skb->mark = d->mark;

	spin_unlock(&d->tcf_lock);
	return d->tcf_action;
}

static const struct nla_policy skbedit_policy[TCA_SKBEDIT_MAX + 1] = {
	[TCA_SKBEDIT_PARMS]		= { .len = sizeof(struct tc_skbedit) },
	[TCA_SKBEDIT_PRIORITY]		= { .len = sizeof(u32) },
	[TCA_SKBEDIT_QUEUE_MAPPING]	= { .len = sizeof(u16) },
	[TCA_SKBEDIT_MARK]		= { .len = sizeof(u32) },
};

static int tcf_skbedit_init(struct net *net, struct nlattr *nla,
			    struct nlattr *est, struct tc_action *a,
			    int ovr, int bind)
{
	struct nlattr *tb[TCA_SKBEDIT_MAX + 1];
	struct tc_skbedit *parm;
	struct tcf_skbedit *d;
	u32 flags = 0, *priority = NULL, *mark = NULL;
	u16 *queue_mapping = NULL;
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

	if (tb[TCA_SKBEDIT_MARK] != NULL) {
		flags |= SKBEDIT_F_MARK;
		mark = nla_data(tb[TCA_SKBEDIT_MARK]);
	}

	if (!flags)
		return -EINVAL;

	parm = nla_data(tb[TCA_SKBEDIT_PARMS]);

	if (!tcf_hash_check(parm->index, a, bind)) {
		ret = tcf_hash_create(parm->index, est, a, sizeof(*d), bind);
		if (ret)
			return ret;

		d = to_skbedit(a);
		ret = ACT_P_CREATED;
	} else {
		d = to_skbedit(a);
		if (bind)
			return 0;
		tcf_hash_release(a, bind);
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

	d->tcf_action = parm->action;

	spin_unlock_bh(&d->tcf_lock);

	if (ret == ACT_P_CREATED)
		tcf_hash_insert(a);
	return ret;
}

static int tcf_skbedit_dump(struct sk_buff *skb, struct tc_action *a,
			    int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_skbedit *d = a->priv;
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
	    nla_put(skb, TCA_SKBEDIT_PRIORITY, sizeof(d->priority),
		    &d->priority))
		goto nla_put_failure;
	if ((d->flags & SKBEDIT_F_QUEUE_MAPPING) &&
	    nla_put(skb, TCA_SKBEDIT_QUEUE_MAPPING,
		    sizeof(d->queue_mapping), &d->queue_mapping))
		goto nla_put_failure;
	if ((d->flags & SKBEDIT_F_MARK) &&
	    nla_put(skb, TCA_SKBEDIT_MARK, sizeof(d->mark),
		    &d->mark))
		goto nla_put_failure;
	t.install = jiffies_to_clock_t(jiffies - d->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - d->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(d->tcf_tm.expires);
	if (nla_put(skb, TCA_SKBEDIT_TM, sizeof(t), &t))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct tc_action_ops act_skbedit_ops = {
	.kind		=	"skbedit",
	.type		=	TCA_ACT_SKBEDIT,
	.owner		=	THIS_MODULE,
	.act		=	tcf_skbedit,
	.dump		=	tcf_skbedit_dump,
	.init		=	tcf_skbedit_init,
};

MODULE_AUTHOR("Alexander Duyck, <alexander.h.duyck@intel.com>");
MODULE_DESCRIPTION("SKB Editing");
MODULE_LICENSE("GPL");

static int __init skbedit_init_module(void)
{
	return tcf_register_action(&act_skbedit_ops, SKBEDIT_TAB_MASK);
}

static void __exit skbedit_cleanup_module(void)
{
	tcf_unregister_action(&act_skbedit_ops);
}

module_init(skbedit_init_module);
module_exit(skbedit_cleanup_module);
