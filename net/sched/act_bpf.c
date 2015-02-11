/*
 * Copyright (c) 2015 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/filter.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>

#include <linux/tc_act/tc_bpf.h>
#include <net/tc_act/tc_bpf.h>

#define BPF_TAB_MASK     15

static int tcf_bpf(struct sk_buff *skb, const struct tc_action *a,
		   struct tcf_result *res)
{
	struct tcf_bpf *b = a->priv;
	int action;
	int filter_res;

	spin_lock(&b->tcf_lock);
	b->tcf_tm.lastuse = jiffies;
	bstats_update(&b->tcf_bstats, skb);
	action = b->tcf_action;

	filter_res = BPF_PROG_RUN(b->filter, skb);
	if (filter_res == 0) {
		/* Return code 0 from the BPF program
		 * is being interpreted as a drop here.
		 */
		action = TC_ACT_SHOT;
		b->tcf_qstats.drops++;
	}

	spin_unlock(&b->tcf_lock);
	return action;
}

static int tcf_bpf_dump(struct sk_buff *skb, struct tc_action *a,
			int bind, int ref)
{
	unsigned char *tp = skb_tail_pointer(skb);
	struct tcf_bpf *b = a->priv;
	struct tc_act_bpf opt = {
		.index    = b->tcf_index,
		.refcnt   = b->tcf_refcnt - ref,
		.bindcnt  = b->tcf_bindcnt - bind,
		.action   = b->tcf_action,
	};
	struct tcf_t t;
	struct nlattr *nla;

	if (nla_put(skb, TCA_ACT_BPF_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	if (nla_put_u16(skb, TCA_ACT_BPF_OPS_LEN, b->bpf_num_ops))
		goto nla_put_failure;

	nla = nla_reserve(skb, TCA_ACT_BPF_OPS, b->bpf_num_ops *
			  sizeof(struct sock_filter));
	if (!nla)
		goto nla_put_failure;

	memcpy(nla_data(nla), b->bpf_ops, nla_len(nla));

	t.install = jiffies_to_clock_t(jiffies - b->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - b->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(b->tcf_tm.expires);
	if (nla_put(skb, TCA_ACT_BPF_TM, sizeof(t), &t))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, tp);
	return -1;
}

static const struct nla_policy act_bpf_policy[TCA_ACT_BPF_MAX + 1] = {
	[TCA_ACT_BPF_PARMS]	= { .len = sizeof(struct tc_act_bpf) },
	[TCA_ACT_BPF_OPS_LEN]	= { .type = NLA_U16 },
	[TCA_ACT_BPF_OPS]	= { .type = NLA_BINARY,
				    .len = sizeof(struct sock_filter) * BPF_MAXINSNS },
};

static int tcf_bpf_init(struct net *net, struct nlattr *nla,
			struct nlattr *est, struct tc_action *a,
			int ovr, int bind)
{
	struct nlattr *tb[TCA_ACT_BPF_MAX + 1];
	struct tc_act_bpf *parm;
	struct tcf_bpf *b;
	u16 bpf_size, bpf_num_ops;
	struct sock_filter *bpf_ops;
	struct sock_fprog_kern tmp;
	struct bpf_prog *fp;
	int ret;

	if (!nla)
		return -EINVAL;

	ret = nla_parse_nested(tb, TCA_ACT_BPF_MAX, nla, act_bpf_policy);
	if (ret < 0)
		return ret;

	if (!tb[TCA_ACT_BPF_PARMS] ||
	    !tb[TCA_ACT_BPF_OPS_LEN] || !tb[TCA_ACT_BPF_OPS])
		return -EINVAL;
	parm = nla_data(tb[TCA_ACT_BPF_PARMS]);

	bpf_num_ops = nla_get_u16(tb[TCA_ACT_BPF_OPS_LEN]);
	if (bpf_num_ops	> BPF_MAXINSNS || bpf_num_ops == 0)
		return -EINVAL;

	bpf_size = bpf_num_ops * sizeof(*bpf_ops);
	if (bpf_size != nla_len(tb[TCA_ACT_BPF_OPS]))
		return -EINVAL;

	bpf_ops = kzalloc(bpf_size, GFP_KERNEL);
	if (!bpf_ops)
		return -ENOMEM;

	memcpy(bpf_ops, nla_data(tb[TCA_ACT_BPF_OPS]), bpf_size);

	tmp.len = bpf_num_ops;
	tmp.filter = bpf_ops;

	ret = bpf_prog_create(&fp, &tmp);
	if (ret)
		goto free_bpf_ops;

	if (!tcf_hash_check(parm->index, a, bind)) {
		ret = tcf_hash_create(parm->index, est, a, sizeof(*b), bind);
		if (ret)
			goto destroy_fp;

		ret = ACT_P_CREATED;
	} else {
		if (bind)
			goto destroy_fp;
		tcf_hash_release(a, bind);
		if (!ovr) {
			ret = -EEXIST;
			goto destroy_fp;
		}
	}

	b = to_bpf(a);
	spin_lock_bh(&b->tcf_lock);
	b->tcf_action = parm->action;
	b->bpf_num_ops = bpf_num_ops;
	b->bpf_ops = bpf_ops;
	b->filter = fp;
	spin_unlock_bh(&b->tcf_lock);

	if (ret == ACT_P_CREATED)
		tcf_hash_insert(a);
	return ret;

destroy_fp:
	bpf_prog_destroy(fp);
free_bpf_ops:
	kfree(bpf_ops);
	return ret;
}

static void tcf_bpf_cleanup(struct tc_action *a, int bind)
{
	struct tcf_bpf *b = a->priv;

	bpf_prog_destroy(b->filter);
}

static struct tc_action_ops act_bpf_ops = {
	.kind =		"bpf",
	.type =		TCA_ACT_BPF,
	.owner =	THIS_MODULE,
	.act =		tcf_bpf,
	.dump =		tcf_bpf_dump,
	.cleanup =	tcf_bpf_cleanup,
	.init =		tcf_bpf_init,
};

static int __init bpf_init_module(void)
{
	return tcf_register_action(&act_bpf_ops, BPF_TAB_MASK);
}

static void __exit bpf_cleanup_module(void)
{
	tcf_unregister_action(&act_bpf_ops);
}

module_init(bpf_init_module);
module_exit(bpf_cleanup_module);

MODULE_AUTHOR("Jiri Pirko <jiri@resnulli.us>");
MODULE_DESCRIPTION("TC BPF based action");
MODULE_LICENSE("GPL v2");
