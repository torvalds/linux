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
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
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
static struct tcf_common *tcf_skbedit_ht[SKBEDIT_TAB_MASK + 1];
static u32 skbedit_idx_gen;
static DEFINE_RWLOCK(skbedit_lock);

static struct tcf_hashinfo skbedit_hash_info = {
	.htab	=	tcf_skbedit_ht,
	.hmask	=	SKBEDIT_TAB_MASK,
	.lock	=	&skbedit_lock,
};

static int tcf_skbedit(struct sk_buff *skb, struct tc_action *a,
		       struct tcf_result *res)
{
	struct tcf_skbedit *d = a->priv;

	spin_lock(&d->tcf_lock);
	d->tcf_tm.lastuse = jiffies;
	d->tcf_bstats.bytes += qdisc_pkt_len(skb);
	d->tcf_bstats.packets++;

	if (d->flags & SKBEDIT_F_PRIORITY)
		skb->priority = d->priority;
	if (d->flags & SKBEDIT_F_QUEUE_MAPPING &&
	    skb->dev->real_num_tx_queues > d->queue_mapping)
		skb_set_queue_mapping(skb, d->queue_mapping);

	spin_unlock(&d->tcf_lock);
	return d->tcf_action;
}

static const struct nla_policy skbedit_policy[TCA_SKBEDIT_MAX + 1] = {
	[TCA_SKBEDIT_PARMS]		= { .len = sizeof(struct tc_skbedit) },
	[TCA_SKBEDIT_PRIORITY]		= { .len = sizeof(u32) },
	[TCA_SKBEDIT_QUEUE_MAPPING]	= { .len = sizeof(u16) },
};

static int tcf_skbedit_init(struct nlattr *nla, struct nlattr *est,
			 struct tc_action *a, int ovr, int bind)
{
	struct nlattr *tb[TCA_SKBEDIT_MAX + 1];
	struct tc_skbedit *parm;
	struct tcf_skbedit *d;
	struct tcf_common *pc;
	u32 flags = 0, *priority = NULL;
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
	if (!flags)
		return -EINVAL;

	parm = nla_data(tb[TCA_SKBEDIT_PARMS]);

	pc = tcf_hash_check(parm->index, a, bind, &skbedit_hash_info);
	if (!pc) {
		pc = tcf_hash_create(parm->index, est, a, sizeof(*d), bind,
				     &skbedit_idx_gen, &skbedit_hash_info);
		if (IS_ERR(pc))
		    return PTR_ERR(pc);

		d = to_skbedit(pc);
		ret = ACT_P_CREATED;
	} else {
		d = to_skbedit(pc);
		if (!ovr) {
			tcf_hash_release(pc, bind, &skbedit_hash_info);
			return -EEXIST;
		}
	}

	spin_lock_bh(&d->tcf_lock);

	d->flags = flags;
	if (flags & SKBEDIT_F_PRIORITY)
		d->priority = *priority;
	if (flags & SKBEDIT_F_QUEUE_MAPPING)
		d->queue_mapping = *queue_mapping;
	d->tcf_action = parm->action;

	spin_unlock_bh(&d->tcf_lock);

	if (ret == ACT_P_CREATED)
		tcf_hash_insert(pc, &skbedit_hash_info);
	return ret;
}

static inline int tcf_skbedit_cleanup(struct tc_action *a, int bind)
{
	struct tcf_skbedit *d = a->priv;

	if (d)
		return tcf_hash_release(&d->common, bind, &skbedit_hash_info);
	return 0;
}

static inline int tcf_skbedit_dump(struct sk_buff *skb, struct tc_action *a,
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

	NLA_PUT(skb, TCA_SKBEDIT_PARMS, sizeof(opt), &opt);
	if (d->flags & SKBEDIT_F_PRIORITY)
		NLA_PUT(skb, TCA_SKBEDIT_PRIORITY, sizeof(d->priority),
			&d->priority);
	if (d->flags & SKBEDIT_F_QUEUE_MAPPING)
		NLA_PUT(skb, TCA_SKBEDIT_QUEUE_MAPPING,
			sizeof(d->queue_mapping), &d->queue_mapping);
	t.install = jiffies_to_clock_t(jiffies - d->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - d->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(d->tcf_tm.expires);
	NLA_PUT(skb, TCA_SKBEDIT_TM, sizeof(t), &t);
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct tc_action_ops act_skbedit_ops = {
	.kind		=	"skbedit",
	.hinfo		=	&skbedit_hash_info,
	.type		=	TCA_ACT_SKBEDIT,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_skbedit,
	.dump		=	tcf_skbedit_dump,
	.cleanup	=	tcf_skbedit_cleanup,
	.init		=	tcf_skbedit_init,
	.walk		=	tcf_generic_walker,
};

MODULE_AUTHOR("Alexander Duyck, <alexander.h.duyck@intel.com>");
MODULE_DESCRIPTION("SKB Editing");
MODULE_LICENSE("GPL");

static int __init skbedit_init_module(void)
{
	return tcf_register_action(&act_skbedit_ops);
}

static void __exit skbedit_cleanup_module(void)
{
	tcf_unregister_action(&act_skbedit_ops);
}

module_init(skbedit_init_module);
module_exit(skbedit_cleanup_module);
