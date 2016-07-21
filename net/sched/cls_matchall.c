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

struct cls_mall_filter {
	struct tcf_exts exts;
	struct tcf_result res;
	u32 handle;
	struct rcu_head	rcu;
};

struct cls_mall_head {
	struct cls_mall_filter *filter;
	struct rcu_head	rcu;
};

static int mall_classify(struct sk_buff *skb, const struct tcf_proto *tp,
			 struct tcf_result *res)
{
	struct cls_mall_head *head = rcu_dereference_bh(tp->root);
	struct cls_mall_filter *f = head->filter;

	return tcf_exts_exec(skb, &f->exts, res);
}

static int mall_init(struct tcf_proto *tp)
{
	struct cls_mall_head *head;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (!head)
		return -ENOBUFS;

	rcu_assign_pointer(tp->root, head);

	return 0;
}

static void mall_destroy_filter(struct rcu_head *head)
{
	struct cls_mall_filter *f = container_of(head, struct cls_mall_filter, rcu);

	tcf_exts_destroy(&f->exts);
	kfree(f);
}

static bool mall_destroy(struct tcf_proto *tp, bool force)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);

	if (!force && head->filter)
		return false;

	if (head->filter)
		call_rcu(&head->filter->rcu, mall_destroy_filter);
	RCU_INIT_POINTER(tp->root, NULL);
	kfree_rcu(head, rcu);
	return true;
}

static unsigned long mall_get(struct tcf_proto *tp, u32 handle)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);
	struct cls_mall_filter *f = head->filter;

	if (f && f->handle == handle)
		return (unsigned long) f;
	return 0;
}

static const struct nla_policy mall_policy[TCA_MATCHALL_MAX + 1] = {
	[TCA_MATCHALL_UNSPEC]		= { .type = NLA_UNSPEC },
	[TCA_MATCHALL_CLASSID]		= { .type = NLA_U32 },
};

static int mall_set_parms(struct net *net, struct tcf_proto *tp,
			  struct cls_mall_filter *f,
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
		f->res.classid = nla_get_u32(tb[TCA_MATCHALL_CLASSID]);
		tcf_bind_filter(tp, &f->res, base);
	}

	tcf_exts_change(tp, &f->exts, &e);

	return 0;
}

static int mall_change(struct net *net, struct sk_buff *in_skb,
		       struct tcf_proto *tp, unsigned long base,
		       u32 handle, struct nlattr **tca,
		       unsigned long *arg, bool ovr)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);
	struct cls_mall_filter *fold = (struct cls_mall_filter *) *arg;
	struct cls_mall_filter *f;
	struct nlattr *tb[TCA_MATCHALL_MAX + 1];
	int err;

	if (!tca[TCA_OPTIONS])
		return -EINVAL;

	if (head->filter)
		return -EBUSY;

	if (fold)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_MATCHALL_MAX,
			       tca[TCA_OPTIONS], mall_policy);
	if (err < 0)
		return err;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOBUFS;

	tcf_exts_init(&f->exts, TCA_MATCHALL_ACT, 0);

	if (!handle)
		handle = 1;
	f->handle = handle;

	err = mall_set_parms(net, tp, f, base, tb, tca[TCA_RATE], ovr);
	if (err)
		goto errout;

	*arg = (unsigned long) f;
	rcu_assign_pointer(head->filter, f);

	return 0;

errout:
	kfree(f);
	return err;
}

static int mall_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);
	struct cls_mall_filter *f = (struct cls_mall_filter *) arg;

	RCU_INIT_POINTER(head->filter, NULL);
	tcf_unbind_filter(tp, &f->res);
	call_rcu(&f->rcu, mall_destroy_filter);
	return 0;
}

static void mall_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);
	struct cls_mall_filter *f = head->filter;

	if (arg->count < arg->skip)
		goto skip;
	if (arg->fn(tp, (unsigned long) f, arg) < 0)
		arg->stop = 1;
skip:
	arg->count++;
}

static int mall_dump(struct net *net, struct tcf_proto *tp, unsigned long fh,
		     struct sk_buff *skb, struct tcmsg *t)
{
	struct cls_mall_filter *f = (struct cls_mall_filter *) fh;
	struct nlattr *nest;

	if (!f)
		return skb->len;

	t->tcm_handle = f->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (!nest)
		goto nla_put_failure;

	if (f->res.classid &&
	    nla_put_u32(skb, TCA_MATCHALL_CLASSID, f->res.classid))
		goto nla_put_failure;

	if (tcf_exts_dump(skb, &f->exts))
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	if (tcf_exts_dump_stats(skb, &f->exts) < 0)
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
