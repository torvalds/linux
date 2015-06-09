/*
 * net/sched/cls_basic.c	Basic Packet Classifier.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/act_api.h>
#include <net/pkt_cls.h>

struct basic_head {
	u32			hgenerator;
	struct list_head	flist;
	struct rcu_head		rcu;
};

struct basic_filter {
	u32			handle;
	struct tcf_exts		exts;
	struct tcf_ematch_tree	ematches;
	struct tcf_result	res;
	struct tcf_proto	*tp;
	struct list_head	link;
	struct rcu_head		rcu;
};

static int basic_classify(struct sk_buff *skb, const struct tcf_proto *tp,
			  struct tcf_result *res)
{
	int r;
	struct basic_head *head = rcu_dereference_bh(tp->root);
	struct basic_filter *f;

	list_for_each_entry_rcu(f, &head->flist, link) {
		if (!tcf_em_tree_match(skb, &f->ematches, NULL))
			continue;
		*res = f->res;
		r = tcf_exts_exec(skb, &f->exts, res);
		if (r < 0)
			continue;
		return r;
	}
	return -1;
}

static unsigned long basic_get(struct tcf_proto *tp, u32 handle)
{
	unsigned long l = 0UL;
	struct basic_head *head = rtnl_dereference(tp->root);
	struct basic_filter *f;

	if (head == NULL)
		return 0UL;

	list_for_each_entry(f, &head->flist, link) {
		if (f->handle == handle) {
			l = (unsigned long) f;
			break;
		}
	}

	return l;
}

static int basic_init(struct tcf_proto *tp)
{
	struct basic_head *head;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (head == NULL)
		return -ENOBUFS;
	INIT_LIST_HEAD(&head->flist);
	rcu_assign_pointer(tp->root, head);
	return 0;
}

static void basic_delete_filter(struct rcu_head *head)
{
	struct basic_filter *f = container_of(head, struct basic_filter, rcu);

	tcf_exts_destroy(&f->exts);
	tcf_em_tree_destroy(&f->ematches);
	kfree(f);
}

static bool basic_destroy(struct tcf_proto *tp, bool force)
{
	struct basic_head *head = rtnl_dereference(tp->root);
	struct basic_filter *f, *n;

	if (!force && !list_empty(&head->flist))
		return false;

	list_for_each_entry_safe(f, n, &head->flist, link) {
		list_del_rcu(&f->link);
		tcf_unbind_filter(tp, &f->res);
		call_rcu(&f->rcu, basic_delete_filter);
	}
	RCU_INIT_POINTER(tp->root, NULL);
	kfree_rcu(head, rcu);
	return true;
}

static int basic_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct basic_filter *f = (struct basic_filter *) arg;

	list_del_rcu(&f->link);
	tcf_unbind_filter(tp, &f->res);
	call_rcu(&f->rcu, basic_delete_filter);
	return 0;
}

static const struct nla_policy basic_policy[TCA_BASIC_MAX + 1] = {
	[TCA_BASIC_CLASSID]	= { .type = NLA_U32 },
	[TCA_BASIC_EMATCHES]	= { .type = NLA_NESTED },
};

static int basic_set_parms(struct net *net, struct tcf_proto *tp,
			   struct basic_filter *f, unsigned long base,
			   struct nlattr **tb,
			   struct nlattr *est, bool ovr)
{
	int err;
	struct tcf_exts e;
	struct tcf_ematch_tree t;

	tcf_exts_init(&e, TCA_BASIC_ACT, TCA_BASIC_POLICE);
	err = tcf_exts_validate(net, tp, tb, est, &e, ovr);
	if (err < 0)
		return err;

	err = tcf_em_tree_validate(tp, tb[TCA_BASIC_EMATCHES], &t);
	if (err < 0)
		goto errout;

	if (tb[TCA_BASIC_CLASSID]) {
		f->res.classid = nla_get_u32(tb[TCA_BASIC_CLASSID]);
		tcf_bind_filter(tp, &f->res, base);
	}

	tcf_exts_change(tp, &f->exts, &e);
	tcf_em_tree_change(tp, &f->ematches, &t);
	f->tp = tp;

	return 0;
errout:
	tcf_exts_destroy(&e);
	return err;
}

static int basic_change(struct net *net, struct sk_buff *in_skb,
			struct tcf_proto *tp, unsigned long base, u32 handle,
			struct nlattr **tca, unsigned long *arg, bool ovr)
{
	int err;
	struct basic_head *head = rtnl_dereference(tp->root);
	struct nlattr *tb[TCA_BASIC_MAX + 1];
	struct basic_filter *fold = (struct basic_filter *) *arg;
	struct basic_filter *fnew;

	if (tca[TCA_OPTIONS] == NULL)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_BASIC_MAX, tca[TCA_OPTIONS],
			       basic_policy);
	if (err < 0)
		return err;

	if (fold != NULL) {
		if (handle && fold->handle != handle)
			return -EINVAL;
	}

	fnew = kzalloc(sizeof(*fnew), GFP_KERNEL);
	if (!fnew)
		return -ENOBUFS;

	tcf_exts_init(&fnew->exts, TCA_BASIC_ACT, TCA_BASIC_POLICE);
	err = -EINVAL;
	if (handle) {
		fnew->handle = handle;
	} else if (fold) {
		fnew->handle = fold->handle;
	} else {
		unsigned int i = 0x80000000;
		do {
			if (++head->hgenerator == 0x7FFFFFFF)
				head->hgenerator = 1;
		} while (--i > 0 && basic_get(tp, head->hgenerator));

		if (i <= 0) {
			pr_err("Insufficient number of handles\n");
			goto errout;
		}

		fnew->handle = head->hgenerator;
	}

	err = basic_set_parms(net, tp, fnew, base, tb, tca[TCA_RATE], ovr);
	if (err < 0)
		goto errout;

	*arg = (unsigned long)fnew;

	if (fold) {
		list_replace_rcu(&fold->link, &fnew->link);
		tcf_unbind_filter(tp, &fold->res);
		call_rcu(&fold->rcu, basic_delete_filter);
	} else {
		list_add_rcu(&fnew->link, &head->flist);
	}

	return 0;
errout:
	kfree(fnew);
	return err;
}

static void basic_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct basic_head *head = rtnl_dereference(tp->root);
	struct basic_filter *f;

	list_for_each_entry(f, &head->flist, link) {
		if (arg->count < arg->skip)
			goto skip;

		if (arg->fn(tp, (unsigned long) f, arg) < 0) {
			arg->stop = 1;
			break;
		}
skip:
		arg->count++;
	}
}

static int basic_dump(struct net *net, struct tcf_proto *tp, unsigned long fh,
		      struct sk_buff *skb, struct tcmsg *t)
{
	struct basic_filter *f = (struct basic_filter *) fh;
	struct nlattr *nest;

	if (f == NULL)
		return skb->len;

	t->tcm_handle = f->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;

	if (f->res.classid &&
	    nla_put_u32(skb, TCA_BASIC_CLASSID, f->res.classid))
		goto nla_put_failure;

	if (tcf_exts_dump(skb, &f->exts) < 0 ||
	    tcf_em_tree_dump(skb, &f->ematches, TCA_BASIC_EMATCHES) < 0)
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	if (tcf_exts_dump_stats(skb, &f->exts) < 0)
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static struct tcf_proto_ops cls_basic_ops __read_mostly = {
	.kind		=	"basic",
	.classify	=	basic_classify,
	.init		=	basic_init,
	.destroy	=	basic_destroy,
	.get		=	basic_get,
	.change		=	basic_change,
	.delete		=	basic_delete,
	.walk		=	basic_walk,
	.dump		=	basic_dump,
	.owner		=	THIS_MODULE,
};

static int __init init_basic(void)
{
	return register_tcf_proto_ops(&cls_basic_ops);
}

static void __exit exit_basic(void)
{
	unregister_tcf_proto_ops(&cls_basic_ops);
}

module_init(init_basic)
module_exit(exit_basic)
MODULE_LICENSE("GPL");

