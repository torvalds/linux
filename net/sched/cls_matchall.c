/*
 * net/sched/cls_matchll.c		Match-all classifier
 *
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

#include <net/sch_generic.h>
#include <net/pkt_cls.h>

struct cls_mall_head {
	struct tcf_exts exts;
	struct tcf_result res;
	u32 handle;
	u32 flags;
	struct rcu_head	rcu;
};

static int mall_classify(struct sk_buff *skb, const struct tcf_proto *tp,
			 struct tcf_result *res)
{
	struct cls_mall_head *head = rcu_dereference_bh(tp->root);

	if (tc_skip_sw(head->flags))
		return -1;

	return tcf_exts_exec(skb, &head->exts, res);
}

static int mall_init(struct tcf_proto *tp)
{
	return 0;
}

static void mall_destroy_rcu(struct rcu_head *rcu)
{
	struct cls_mall_head *head = container_of(rcu, struct cls_mall_head,
						  rcu);

	tcf_exts_destroy(&head->exts);
	kfree(head);
}

static int mall_replace_hw_filter(struct tcf_proto *tp,
				  struct cls_mall_head *head,
				  unsigned long cookie)
{
	struct net_device *dev = tp->q->dev_queue->dev;
	struct tc_to_netdev offload;
	struct tc_cls_matchall_offload mall_offload = {0};

	offload.type = TC_SETUP_MATCHALL;
	offload.cls_mall = &mall_offload;
	offload.cls_mall->command = TC_CLSMATCHALL_REPLACE;
	offload.cls_mall->exts = &head->exts;
	offload.cls_mall->cookie = cookie;

	return dev->netdev_ops->ndo_setup_tc(dev, tp->q->handle, tp->protocol,
					     &offload);
}

static void mall_destroy_hw_filter(struct tcf_proto *tp,
				   struct cls_mall_head *head,
				   unsigned long cookie)
{
	struct net_device *dev = tp->q->dev_queue->dev;
	struct tc_to_netdev offload;
	struct tc_cls_matchall_offload mall_offload = {0};

	offload.type = TC_SETUP_MATCHALL;
	offload.cls_mall = &mall_offload;
	offload.cls_mall->command = TC_CLSMATCHALL_DESTROY;
	offload.cls_mall->exts = NULL;
	offload.cls_mall->cookie = cookie;

	dev->netdev_ops->ndo_setup_tc(dev, tp->q->handle, tp->protocol,
					     &offload);
}

static bool mall_destroy(struct tcf_proto *tp, bool force)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);
	struct net_device *dev = tp->q->dev_queue->dev;

	if (!head)
		return true;

	if (tc_should_offload(dev, tp, head->flags))
		mall_destroy_hw_filter(tp, head, (unsigned long) head);

	call_rcu(&head->rcu, mall_destroy_rcu);
	return true;
}

static unsigned long mall_get(struct tcf_proto *tp, u32 handle)
{
	return 0UL;
}

static const struct nla_policy mall_policy[TCA_MATCHALL_MAX + 1] = {
	[TCA_MATCHALL_UNSPEC]		= { .type = NLA_UNSPEC },
	[TCA_MATCHALL_CLASSID]		= { .type = NLA_U32 },
};

static int mall_set_parms(struct net *net, struct tcf_proto *tp,
			  struct cls_mall_head *head,
			  unsigned long base, struct nlattr **tb,
			  struct nlattr *est, bool ovr)
{
	struct tcf_exts e;
	int err;

	tcf_exts_init(&e, TCA_MATCHALL_ACT, 0);
	err = tcf_exts_validate(net, tp, tb, est, &e, ovr);
	if (err < 0)
		return err;

	if (tb[TCA_MATCHALL_CLASSID]) {
		head->res.classid = nla_get_u32(tb[TCA_MATCHALL_CLASSID]);
		tcf_bind_filter(tp, &head->res, base);
	}

	tcf_exts_change(tp, &head->exts, &e);

	return 0;
}

static int mall_change(struct net *net, struct sk_buff *in_skb,
		       struct tcf_proto *tp, unsigned long base,
		       u32 handle, struct nlattr **tca,
		       unsigned long *arg, bool ovr)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);
	struct net_device *dev = tp->q->dev_queue->dev;
	struct nlattr *tb[TCA_MATCHALL_MAX + 1];
	struct cls_mall_head *new;
	u32 flags = 0;
	int err;

	if (!tca[TCA_OPTIONS])
		return -EINVAL;

	if (head)
		return -EEXIST;

	err = nla_parse_nested(tb, TCA_MATCHALL_MAX,
			       tca[TCA_OPTIONS], mall_policy);
	if (err < 0)
		return err;

	if (tb[TCA_MATCHALL_FLAGS]) {
		flags = nla_get_u32(tb[TCA_MATCHALL_FLAGS]);
		if (!tc_flags_valid(flags))
			return -EINVAL;
	}

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOBUFS;

	tcf_exts_init(&new->exts, TCA_MATCHALL_ACT, 0);

	if (!handle)
		handle = 1;
	new->handle = handle;
	new->flags = flags;

	err = mall_set_parms(net, tp, new, base, tb, tca[TCA_RATE], ovr);
	if (err)
		goto errout;

	if (tc_should_offload(dev, tp, flags)) {
		err = mall_replace_hw_filter(tp, new, (unsigned long) new);
		if (err) {
			if (tc_skip_sw(flags))
				goto errout;
			else
				err = 0;
		}
	}

	*arg = (unsigned long) head;
	rcu_assign_pointer(tp->root, new);
	if (head)
		call_rcu(&head->rcu, mall_destroy_rcu);
	return 0;

errout:
	kfree(new);
	return err;
}

static int mall_delete(struct tcf_proto *tp, unsigned long arg)
{
	return -EOPNOTSUPP;
}

static void mall_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);

	if (arg->count < arg->skip)
		goto skip;
	if (arg->fn(tp, (unsigned long) head, arg) < 0)
		arg->stop = 1;
skip:
	arg->count++;
}

static int mall_dump(struct net *net, struct tcf_proto *tp, unsigned long fh,
		     struct sk_buff *skb, struct tcmsg *t)
{
	struct cls_mall_head *head = (struct cls_mall_head *) fh;
	struct nlattr *nest;

	if (!head)
		return skb->len;

	t->tcm_handle = head->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (!nest)
		goto nla_put_failure;

	if (head->res.classid &&
	    nla_put_u32(skb, TCA_MATCHALL_CLASSID, head->res.classid))
		goto nla_put_failure;

	if (tcf_exts_dump(skb, &head->exts))
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	if (tcf_exts_dump_stats(skb, &head->exts) < 0)
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static struct tcf_proto_ops cls_mall_ops __read_mostly = {
	.kind		= "matchall",
	.classify	= mall_classify,
	.init		= mall_init,
	.destroy	= mall_destroy,
	.get		= mall_get,
	.change		= mall_change,
	.delete		= mall_delete,
	.walk		= mall_walk,
	.dump		= mall_dump,
	.owner		= THIS_MODULE,
};

static int __init cls_mall_init(void)
{
	return register_tcf_proto_ops(&cls_mall_ops);
}

static void __exit cls_mall_exit(void)
{
	unregister_tcf_proto_ops(&cls_mall_ops);
}

module_init(cls_mall_init);
module_exit(cls_mall_exit);

MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Match-all classifier");
MODULE_LICENSE("GPL v2");
