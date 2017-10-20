/*
 * net/sched/act_sample.c - Packet sampling tc action
 * Copyright (c) 2017 Yotam Gigi <yotamg@mellanox.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_sample.h>
#include <net/tc_act/tc_sample.h>
#include <net/psample.h>

#include <linux/if_arp.h>

static unsigned int sample_net_id;
static struct tc_action_ops act_sample_ops;

static const struct nla_policy sample_policy[TCA_SAMPLE_MAX + 1] = {
	[TCA_SAMPLE_PARMS]		= { .len = sizeof(struct tc_sample) },
	[TCA_SAMPLE_RATE]		= { .type = NLA_U32 },
	[TCA_SAMPLE_TRUNC_SIZE]		= { .type = NLA_U32 },
	[TCA_SAMPLE_PSAMPLE_GROUP]	= { .type = NLA_U32 },
};

static int tcf_sample_init(struct net *net, struct nlattr *nla,
			   struct nlattr *est, struct tc_action **a, int ovr,
			   int bind)
{
	struct tc_action_net *tn = net_generic(net, sample_net_id);
	struct nlattr *tb[TCA_SAMPLE_MAX + 1];
	struct psample_group *psample_group;
	struct tc_sample *parm;
	struct tcf_sample *s;
	bool exists = false;
	int ret;

	if (!nla)
		return -EINVAL;
	ret = nla_parse_nested(tb, TCA_SAMPLE_MAX, nla, sample_policy, NULL);
	if (ret < 0)
		return ret;
	if (!tb[TCA_SAMPLE_PARMS] || !tb[TCA_SAMPLE_RATE] ||
	    !tb[TCA_SAMPLE_PSAMPLE_GROUP])
		return -EINVAL;

	parm = nla_data(tb[TCA_SAMPLE_PARMS]);

	exists = tcf_idr_check(tn, parm->index, a, bind);
	if (exists && bind)
		return 0;

	if (!exists) {
		ret = tcf_idr_create(tn, parm->index, est, a,
				     &act_sample_ops, bind, false);
		if (ret)
			return ret;
		ret = ACT_P_CREATED;
	} else {
		tcf_idr_release(*a, bind);
		if (!ovr)
			return -EEXIST;
	}
	s = to_sample(*a);

	s->tcf_action = parm->action;
	s->rate = nla_get_u32(tb[TCA_SAMPLE_RATE]);
	s->psample_group_num = nla_get_u32(tb[TCA_SAMPLE_PSAMPLE_GROUP]);
	psample_group = psample_group_get(net, s->psample_group_num);
	if (!psample_group) {
		if (ret == ACT_P_CREATED)
			tcf_idr_release(*a, bind);
		return -ENOMEM;
	}
	RCU_INIT_POINTER(s->psample_group, psample_group);

	if (tb[TCA_SAMPLE_TRUNC_SIZE]) {
		s->truncate = true;
		s->trunc_size = nla_get_u32(tb[TCA_SAMPLE_TRUNC_SIZE]);
	}

	if (ret == ACT_P_CREATED)
		tcf_idr_insert(tn, *a);
	return ret;
}

static void tcf_sample_cleanup_rcu(struct rcu_head *rcu)
{
	struct tcf_sample *s = container_of(rcu, struct tcf_sample, rcu);
	struct psample_group *psample_group;

	psample_group = rcu_dereference_protected(s->psample_group, 1);
	RCU_INIT_POINTER(s->psample_group, NULL);
	psample_group_put(psample_group);
}

static void tcf_sample_cleanup(struct tc_action *a, int bind)
{
	struct tcf_sample *s = to_sample(a);

	call_rcu(&s->rcu, tcf_sample_cleanup_rcu);
}

static bool tcf_sample_dev_ok_push(struct net_device *dev)
{
	switch (dev->type) {
	case ARPHRD_TUNNEL:
	case ARPHRD_TUNNEL6:
	case ARPHRD_SIT:
	case ARPHRD_IPGRE:
	case ARPHRD_VOID:
	case ARPHRD_NONE:
		return false;
	default:
		return true;
	}
}

static int tcf_sample_act(struct sk_buff *skb, const struct tc_action *a,
			  struct tcf_result *res)
{
	struct tcf_sample *s = to_sample(a);
	struct psample_group *psample_group;
	int retval;
	int size;
	int iif;
	int oif;

	tcf_lastuse_update(&s->tcf_tm);
	bstats_cpu_update(this_cpu_ptr(s->common.cpu_bstats), skb);
	retval = READ_ONCE(s->tcf_action);

	rcu_read_lock();
	psample_group = rcu_dereference(s->psample_group);

	/* randomly sample packets according to rate */
	if (psample_group && (prandom_u32() % s->rate == 0)) {
		if (!skb_at_tc_ingress(skb)) {
			iif = skb->skb_iif;
			oif = skb->dev->ifindex;
		} else {
			iif = skb->dev->ifindex;
			oif = 0;
		}

		/* on ingress, the mac header gets popped, so push it back */
		if (skb_at_tc_ingress(skb) && tcf_sample_dev_ok_push(skb->dev))
			skb_push(skb, skb->mac_len);

		size = s->truncate ? s->trunc_size : skb->len;
		psample_sample_packet(psample_group, skb, size, iif, oif,
				      s->rate);

		if (skb_at_tc_ingress(skb) && tcf_sample_dev_ok_push(skb->dev))
			skb_pull(skb, skb->mac_len);
	}

	rcu_read_unlock();
	return retval;
}

static int tcf_sample_dump(struct sk_buff *skb, struct tc_action *a,
			   int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_sample *s = to_sample(a);
	struct tc_sample opt = {
		.index      = s->tcf_index,
		.action     = s->tcf_action,
		.refcnt     = s->tcf_refcnt - ref,
		.bindcnt    = s->tcf_bindcnt - bind,
	};
	struct tcf_t t;

	if (nla_put(skb, TCA_SAMPLE_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	tcf_tm_dump(&t, &s->tcf_tm);
	if (nla_put_64bit(skb, TCA_SAMPLE_TM, sizeof(t), &t, TCA_SAMPLE_PAD))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_SAMPLE_RATE, s->rate))
		goto nla_put_failure;

	if (s->truncate)
		if (nla_put_u32(skb, TCA_SAMPLE_TRUNC_SIZE, s->trunc_size))
			goto nla_put_failure;

	if (nla_put_u32(skb, TCA_SAMPLE_PSAMPLE_GROUP, s->psample_group_num))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int tcf_sample_walker(struct net *net, struct sk_buff *skb,
			     struct netlink_callback *cb, int type,
			     const struct tc_action_ops *ops)
{
	struct tc_action_net *tn = net_generic(net, sample_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops);
}

static int tcf_sample_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, sample_net_id);

	return tcf_idr_search(tn, a, index);
}

static struct tc_action_ops act_sample_ops = {
	.kind	  = "sample",
	.type	  = TCA_ACT_SAMPLE,
	.owner	  = THIS_MODULE,
	.act	  = tcf_sample_act,
	.dump	  = tcf_sample_dump,
	.init	  = tcf_sample_init,
	.cleanup  = tcf_sample_cleanup,
	.walk	  = tcf_sample_walker,
	.lookup	  = tcf_sample_search,
	.size	  = sizeof(struct tcf_sample),
};

static __net_init int sample_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, sample_net_id);

	return tc_action_net_init(tn, &act_sample_ops);
}

static void __net_exit sample_exit_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, sample_net_id);

	tc_action_net_exit(tn);
}

static struct pernet_operations sample_net_ops = {
	.init = sample_init_net,
	.exit = sample_exit_net,
	.id   = &sample_net_id,
	.size = sizeof(struct tc_action_net),
};

static int __init sample_init_module(void)
{
	return tcf_register_action(&act_sample_ops, &sample_net_ops);
}

static void __exit sample_cleanup_module(void)
{
	tcf_unregister_action(&act_sample_ops, &sample_net_ops);
}

module_init(sample_init_module);
module_exit(sample_cleanup_module);

MODULE_AUTHOR("Yotam Gigi <yotamg@mellanox.com>");
MODULE_DESCRIPTION("Packet sampling action");
MODULE_LICENSE("GPL v2");
