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
#include <linux/percpu.h>

#include <net/sch_generic.h>
#include <net/pkt_cls.h>

struct cls_mall_head {
	struct tcf_exts exts;
	struct tcf_result res;
	u32 handle;
	u32 flags;
	unsigned int in_hw_count;
	struct tc_matchall_pcnt __percpu *pf;
	struct rcu_work rwork;
};

static int mall_classify(struct sk_buff *skb, const struct tcf_proto *tp,
			 struct tcf_result *res)
{
	struct cls_mall_head *head = rcu_dereference_bh(tp->root);

	if (tc_skip_sw(head->flags))
		return -1;

	*res = head->res;
	__this_cpu_inc(head->pf->rhit);
	return tcf_exts_exec(skb, &head->exts, res);
}

static int mall_init(struct tcf_proto *tp)
{
	return 0;
}

static void __mall_destroy(struct cls_mall_head *head)
{
	tcf_exts_destroy(&head->exts);
	tcf_exts_put_net(&head->exts);
	free_percpu(head->pf);
	kfree(head);
}

static void mall_destroy_work(struct work_struct *work)
{
	struct cls_mall_head *head = container_of(to_rcu_work(work),
						  struct cls_mall_head,
						  rwork);
	rtnl_lock();
	__mall_destroy(head);
	rtnl_unlock();
}

static void mall_destroy_hw_filter(struct tcf_proto *tp,
				   struct cls_mall_head *head,
				   unsigned long cookie,
				   struct netlink_ext_ack *extack)
{
	struct tc_cls_matchall_offload cls_mall = {};
	struct tcf_block *block = tp->chain->block;

	tc_cls_common_offload_init(&cls_mall.common, tp, head->flags, extack);
	cls_mall.command = TC_CLSMATCHALL_DESTROY;
	cls_mall.cookie = cookie;

	tc_setup_cb_call(block, TC_SETUP_CLSMATCHALL, &cls_mall, false);
	tcf_block_offload_dec(block, &head->flags);
}

static int mall_replace_hw_filter(struct tcf_proto *tp,
				  struct cls_mall_head *head,
				  unsigned long cookie,
				  struct netlink_ext_ack *extack)
{
	struct tc_cls_matchall_offload cls_mall = {};
	struct tcf_block *block = tp->chain->block;
	bool skip_sw = tc_skip_sw(head->flags);
	int err;

	tc_cls_common_offload_init(&cls_mall.common, tp, head->flags, extack);
	cls_mall.command = TC_CLSMATCHALL_REPLACE;
	cls_mall.exts = &head->exts;
	cls_mall.cookie = cookie;

	err = tc_setup_cb_call(block, TC_SETUP_CLSMATCHALL, &cls_mall, skip_sw);
	if (err < 0) {
		mall_destroy_hw_filter(tp, head, cookie, NULL);
		return err;
	} else if (err > 0) {
		head->in_hw_count = err;
		tcf_block_offload_inc(block, &head->flags);
	}

	if (skip_sw && !(head->flags & TCA_CLS_FLAGS_IN_HW))
		return -EINVAL;

	return 0;
}

static void mall_destroy(struct tcf_proto *tp, bool rtnl_held,
			 struct netlink_ext_ack *extack)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);

	if (!head)
		return;

	tcf_unbind_filter(tp, &head->res);

	if (!tc_skip_hw(head->flags))
		mall_destroy_hw_filter(tp, head, (unsigned long) head, extack);

	if (tcf_exts_get_net(&head->exts))
		tcf_queue_work(&head->rwork, mall_destroy_work);
	else
		__mall_destroy(head);
}

static void *mall_get(struct tcf_proto *tp, u32 handle)
{
	return NULL;
}

static const struct nla_policy mall_policy[TCA_MATCHALL_MAX + 1] = {
	[TCA_MATCHALL_UNSPEC]		= { .type = NLA_UNSPEC },
	[TCA_MATCHALL_CLASSID]		= { .type = NLA_U32 },
};

static int mall_set_parms(struct net *net, struct tcf_proto *tp,
			  struct cls_mall_head *head,
			  unsigned long base, struct nlattr **tb,
			  struct nlattr *est, bool ovr,
			  struct netlink_ext_ack *extack)
{
	int err;

	err = tcf_exts_validate(net, tp, tb, est, &head->exts, ovr, true,
				extack);
	if (err < 0)
		return err;

	if (tb[TCA_MATCHALL_CLASSID]) {
		head->res.classid = nla_get_u32(tb[TCA_MATCHALL_CLASSID]);
		tcf_bind_filter(tp, &head->res, base);
	}
	return 0;
}

static int mall_change(struct net *net, struct sk_buff *in_skb,
		       struct tcf_proto *tp, unsigned long base,
		       u32 handle, struct nlattr **tca,
		       void **arg, bool ovr, bool rtnl_held,
		       struct netlink_ext_ack *extack)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);
	struct nlattr *tb[TCA_MATCHALL_MAX + 1];
	struct cls_mall_head *new;
	u32 flags = 0;
	int err;

	if (!tca[TCA_OPTIONS])
		return -EINVAL;

	if (head)
		return -EEXIST;

	err = nla_parse_nested(tb, TCA_MATCHALL_MAX, tca[TCA_OPTIONS],
			       mall_policy, NULL);
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

	err = tcf_exts_init(&new->exts, TCA_MATCHALL_ACT, 0);
	if (err)
		goto err_exts_init;

	if (!handle)
		handle = 1;
	new->handle = handle;
	new->flags = flags;
	new->pf = alloc_percpu(struct tc_matchall_pcnt);
	if (!new->pf) {
		err = -ENOMEM;
		goto err_alloc_percpu;
	}

	err = mall_set_parms(net, tp, new, base, tb, tca[TCA_RATE], ovr,
			     extack);
	if (err)
		goto err_set_parms;

	if (!tc_skip_hw(new->flags)) {
		err = mall_replace_hw_filter(tp, new, (unsigned long)new,
					     extack);
		if (err)
			goto err_replace_hw_filter;
	}

	if (!tc_in_hw(new->flags))
		new->flags |= TCA_CLS_FLAGS_NOT_IN_HW;

	*arg = head;
	rcu_assign_pointer(tp->root, new);
	return 0;

err_replace_hw_filter:
err_set_parms:
	free_percpu(new->pf);
err_alloc_percpu:
	tcf_exts_destroy(&new->exts);
err_exts_init:
	kfree(new);
	return err;
}

static int mall_delete(struct tcf_proto *tp, void *arg, bool *last,
		       bool rtnl_held, struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static void mall_walk(struct tcf_proto *tp, struct tcf_walker *arg,
		      bool rtnl_held)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);

	if (arg->count < arg->skip)
		goto skip;

	if (!head)
		return;
	if (arg->fn(tp, head, arg) < 0)
		arg->stop = 1;
skip:
	arg->count++;
}

static int mall_reoffload(struct tcf_proto *tp, bool add, tc_setup_cb_t *cb,
			  void *cb_priv, struct netlink_ext_ack *extack)
{
	struct cls_mall_head *head = rtnl_dereference(tp->root);
	struct tc_cls_matchall_offload cls_mall = {};
	struct tcf_block *block = tp->chain->block;
	int err;

	if (tc_skip_hw(head->flags))
		return 0;

	tc_cls_common_offload_init(&cls_mall.common, tp, head->flags, extack);
	cls_mall.command = add ?
		TC_CLSMATCHALL_REPLACE : TC_CLSMATCHALL_DESTROY;
	cls_mall.exts = &head->exts;
	cls_mall.cookie = (unsigned long)head;

	err = cb(TC_SETUP_CLSMATCHALL, &cls_mall, cb_priv);
	if (err) {
		if (add && tc_skip_sw(head->flags))
			return err;
		return 0;
	}

	tc_cls_offload_cnt_update(block, &head->in_hw_count, &head->flags, add);

	return 0;
}

static int mall_dump(struct net *net, struct tcf_proto *tp, void *fh,
		     struct sk_buff *skb, struct tcmsg *t, bool rtnl_held)
{
	struct tc_matchall_pcnt gpf = {};
	struct cls_mall_head *head = fh;
	struct nlattr *nest;
	int cpu;

	if (!head)
		return skb->len;

	t->tcm_handle = head->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (!nest)
		goto nla_put_failure;

	if (head->res.classid &&
	    nla_put_u32(skb, TCA_MATCHALL_CLASSID, head->res.classid))
		goto nla_put_failure;

	if (head->flags && nla_put_u32(skb, TCA_MATCHALL_FLAGS, head->flags))
		goto nla_put_failure;

	for_each_possible_cpu(cpu) {
		struct tc_matchall_pcnt *pf = per_cpu_ptr(head->pf, cpu);

		gpf.rhit += pf->rhit;
	}

	if (nla_put_64bit(skb, TCA_MATCHALL_PCNT,
			  sizeof(struct tc_matchall_pcnt),
			  &gpf, TCA_MATCHALL_PAD))
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

static void mall_bind_class(void *fh, u32 classid, unsigned long cl)
{
	struct cls_mall_head *head = fh;

	if (head && head->res.classid == classid)
		head->res.class = cl;
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
	.reoffload	= mall_reoffload,
	.dump		= mall_dump,
	.bind_class	= mall_bind_class,
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
