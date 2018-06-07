/*
 * net/sched/cls_fw.c	Classifier mapping ipchains' fwmark to traffic class.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 * Karlis Peisenieks <karlis@mt.lv> : 990415 : fw_walk off by one
 * Karlis Peisenieks <karlis@mt.lv> : 990415 : fw_delete killed all the filter (and kernel).
 * Alex <alex@pilotsoft.com> : 2004xxyy: Added Action extension
 *
 * JHS: We should remove the CONFIG_NET_CLS_IND from here
 * eventually when the meta match extension is made available
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/act_api.h>
#include <net/pkt_cls.h>
#include <net/sch_generic.h>

#define HTSIZE 256

struct fw_head {
	u32			mask;
	struct fw_filter __rcu	*ht[HTSIZE];
	struct rcu_head		rcu;
};

struct fw_filter {
	struct fw_filter __rcu	*next;
	u32			id;
	struct tcf_result	res;
#ifdef CONFIG_NET_CLS_IND
	int			ifindex;
#endif /* CONFIG_NET_CLS_IND */
	struct tcf_exts		exts;
	struct tcf_proto	*tp;
	struct rcu_work		rwork;
};

static u32 fw_hash(u32 handle)
{
	handle ^= (handle >> 16);
	handle ^= (handle >> 8);
	return handle % HTSIZE;
}

static int fw_classify(struct sk_buff *skb, const struct tcf_proto *tp,
		       struct tcf_result *res)
{
	struct fw_head *head = rcu_dereference_bh(tp->root);
	struct fw_filter *f;
	int r;
	u32 id = skb->mark;

	if (head != NULL) {
		id &= head->mask;

		for (f = rcu_dereference_bh(head->ht[fw_hash(id)]); f;
		     f = rcu_dereference_bh(f->next)) {
			if (f->id == id) {
				*res = f->res;
#ifdef CONFIG_NET_CLS_IND
				if (!tcf_match_indev(skb, f->ifindex))
					continue;
#endif /* CONFIG_NET_CLS_IND */
				r = tcf_exts_exec(skb, &f->exts, res);
				if (r < 0)
					continue;

				return r;
			}
		}
	} else {
		struct Qdisc *q = tcf_block_q(tp->chain->block);

		/* Old method: classify the packet using its skb mark. */
		if (id && (TC_H_MAJ(id) == 0 ||
			   !(TC_H_MAJ(id ^ q->handle)))) {
			res->classid = id;
			res->class = 0;
			return 0;
		}
	}

	return -1;
}

static void *fw_get(struct tcf_proto *tp, u32 handle)
{
	struct fw_head *head = rtnl_dereference(tp->root);
	struct fw_filter *f;

	if (head == NULL)
		return NULL;

	f = rtnl_dereference(head->ht[fw_hash(handle)]);
	for (; f; f = rtnl_dereference(f->next)) {
		if (f->id == handle)
			return f;
	}
	return NULL;
}

static int fw_init(struct tcf_proto *tp)
{
	/* We don't allocate fw_head here, because in the old method
	 * we don't need it at all.
	 */
	return 0;
}

static void __fw_delete_filter(struct fw_filter *f)
{
	tcf_exts_destroy(&f->exts);
	tcf_exts_put_net(&f->exts);
	kfree(f);
}

static void fw_delete_filter_work(struct work_struct *work)
{
	struct fw_filter *f = container_of(to_rcu_work(work),
					   struct fw_filter,
					   rwork);
	rtnl_lock();
	__fw_delete_filter(f);
	rtnl_unlock();
}

static void fw_destroy(struct tcf_proto *tp, struct netlink_ext_ack *extack)
{
	struct fw_head *head = rtnl_dereference(tp->root);
	struct fw_filter *f;
	int h;

	if (head == NULL)
		return;

	for (h = 0; h < HTSIZE; h++) {
		while ((f = rtnl_dereference(head->ht[h])) != NULL) {
			RCU_INIT_POINTER(head->ht[h],
					 rtnl_dereference(f->next));
			tcf_unbind_filter(tp, &f->res);
			if (tcf_exts_get_net(&f->exts))
				tcf_queue_work(&f->rwork, fw_delete_filter_work);
			else
				__fw_delete_filter(f);
		}
	}
	kfree_rcu(head, rcu);
}

static int fw_delete(struct tcf_proto *tp, void *arg, bool *last,
		     struct netlink_ext_ack *extack)
{
	struct fw_head *head = rtnl_dereference(tp->root);
	struct fw_filter *f = arg;
	struct fw_filter __rcu **fp;
	struct fw_filter *pfp;
	int ret = -EINVAL;
	int h;

	if (head == NULL || f == NULL)
		goto out;

	fp = &head->ht[fw_hash(f->id)];

	for (pfp = rtnl_dereference(*fp); pfp;
	     fp = &pfp->next, pfp = rtnl_dereference(*fp)) {
		if (pfp == f) {
			RCU_INIT_POINTER(*fp, rtnl_dereference(f->next));
			tcf_unbind_filter(tp, &f->res);
			tcf_exts_get_net(&f->exts);
			tcf_queue_work(&f->rwork, fw_delete_filter_work);
			ret = 0;
			break;
		}
	}

	*last = true;
	for (h = 0; h < HTSIZE; h++) {
		if (rcu_access_pointer(head->ht[h])) {
			*last = false;
			break;
		}
	}

out:
	return ret;
}

static const struct nla_policy fw_policy[TCA_FW_MAX + 1] = {
	[TCA_FW_CLASSID]	= { .type = NLA_U32 },
	[TCA_FW_INDEV]		= { .type = NLA_STRING, .len = IFNAMSIZ },
	[TCA_FW_MASK]		= { .type = NLA_U32 },
};

static int fw_set_parms(struct net *net, struct tcf_proto *tp,
			struct fw_filter *f, struct nlattr **tb,
			struct nlattr **tca, unsigned long base, bool ovr,
			struct netlink_ext_ack *extack)
{
	struct fw_head *head = rtnl_dereference(tp->root);
	u32 mask;
	int err;

	err = tcf_exts_validate(net, tp, tb, tca[TCA_RATE], &f->exts, ovr,
				extack);
	if (err < 0)
		return err;

	if (tb[TCA_FW_CLASSID]) {
		f->res.classid = nla_get_u32(tb[TCA_FW_CLASSID]);
		tcf_bind_filter(tp, &f->res, base);
	}

#ifdef CONFIG_NET_CLS_IND
	if (tb[TCA_FW_INDEV]) {
		int ret;
		ret = tcf_change_indev(net, tb[TCA_FW_INDEV], extack);
		if (ret < 0)
			return ret;
		f->ifindex = ret;
	}
#endif /* CONFIG_NET_CLS_IND */

	err = -EINVAL;
	if (tb[TCA_FW_MASK]) {
		mask = nla_get_u32(tb[TCA_FW_MASK]);
		if (mask != head->mask)
			return err;
	} else if (head->mask != 0xFFFFFFFF)
		return err;

	return 0;
}

static int fw_change(struct net *net, struct sk_buff *in_skb,
		     struct tcf_proto *tp, unsigned long base,
		     u32 handle, struct nlattr **tca, void **arg,
		     bool ovr, struct netlink_ext_ack *extack)
{
	struct fw_head *head = rtnl_dereference(tp->root);
	struct fw_filter *f = *arg;
	struct nlattr *opt = tca[TCA_OPTIONS];
	struct nlattr *tb[TCA_FW_MAX + 1];
	int err;

	if (!opt)
		return handle ? -EINVAL : 0; /* Succeed if it is old method. */

	err = nla_parse_nested(tb, TCA_FW_MAX, opt, fw_policy, NULL);
	if (err < 0)
		return err;

	if (f) {
		struct fw_filter *pfp, *fnew;
		struct fw_filter __rcu **fp;

		if (f->id != handle && handle)
			return -EINVAL;

		fnew = kzalloc(sizeof(struct fw_filter), GFP_KERNEL);
		if (!fnew)
			return -ENOBUFS;

		fnew->id = f->id;
		fnew->res = f->res;
#ifdef CONFIG_NET_CLS_IND
		fnew->ifindex = f->ifindex;
#endif /* CONFIG_NET_CLS_IND */
		fnew->tp = f->tp;

		err = tcf_exts_init(&fnew->exts, TCA_FW_ACT, TCA_FW_POLICE);
		if (err < 0) {
			kfree(fnew);
			return err;
		}

		err = fw_set_parms(net, tp, fnew, tb, tca, base, ovr, extack);
		if (err < 0) {
			tcf_exts_destroy(&fnew->exts);
			kfree(fnew);
			return err;
		}

		fp = &head->ht[fw_hash(fnew->id)];
		for (pfp = rtnl_dereference(*fp); pfp;
		     fp = &pfp->next, pfp = rtnl_dereference(*fp))
			if (pfp == f)
				break;

		RCU_INIT_POINTER(fnew->next, rtnl_dereference(pfp->next));
		rcu_assign_pointer(*fp, fnew);
		tcf_unbind_filter(tp, &f->res);
		tcf_exts_get_net(&f->exts);
		tcf_queue_work(&f->rwork, fw_delete_filter_work);

		*arg = fnew;
		return err;
	}

	if (!handle)
		return -EINVAL;

	if (!head) {
		u32 mask = 0xFFFFFFFF;
		if (tb[TCA_FW_MASK])
			mask = nla_get_u32(tb[TCA_FW_MASK]);

		head = kzalloc(sizeof(*head), GFP_KERNEL);
		if (!head)
			return -ENOBUFS;
		head->mask = mask;

		rcu_assign_pointer(tp->root, head);
	}

	f = kzalloc(sizeof(struct fw_filter), GFP_KERNEL);
	if (f == NULL)
		return -ENOBUFS;

	err = tcf_exts_init(&f->exts, TCA_FW_ACT, TCA_FW_POLICE);
	if (err < 0)
		goto errout;
	f->id = handle;
	f->tp = tp;

	err = fw_set_parms(net, tp, f, tb, tca, base, ovr, extack);
	if (err < 0)
		goto errout;

	RCU_INIT_POINTER(f->next, head->ht[fw_hash(handle)]);
	rcu_assign_pointer(head->ht[fw_hash(handle)], f);

	*arg = f;
	return 0;

errout:
	tcf_exts_destroy(&f->exts);
	kfree(f);
	return err;
}

static void fw_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct fw_head *head = rtnl_dereference(tp->root);
	int h;

	if (head == NULL)
		arg->stop = 1;

	if (arg->stop)
		return;

	for (h = 0; h < HTSIZE; h++) {
		struct fw_filter *f;

		for (f = rtnl_dereference(head->ht[h]); f;
		     f = rtnl_dereference(f->next)) {
			if (arg->count < arg->skip) {
				arg->count++;
				continue;
			}
			if (arg->fn(tp, f, arg) < 0) {
				arg->stop = 1;
				return;
			}
			arg->count++;
		}
	}
}

static int fw_dump(struct net *net, struct tcf_proto *tp, void *fh,
		   struct sk_buff *skb, struct tcmsg *t)
{
	struct fw_head *head = rtnl_dereference(tp->root);
	struct fw_filter *f = fh;
	struct nlattr *nest;

	if (f == NULL)
		return skb->len;

	t->tcm_handle = f->id;

	if (!f->res.classid && !tcf_exts_has_actions(&f->exts))
		return skb->len;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;

	if (f->res.classid &&
	    nla_put_u32(skb, TCA_FW_CLASSID, f->res.classid))
		goto nla_put_failure;
#ifdef CONFIG_NET_CLS_IND
	if (f->ifindex) {
		struct net_device *dev;
		dev = __dev_get_by_index(net, f->ifindex);
		if (dev && nla_put_string(skb, TCA_FW_INDEV, dev->name))
			goto nla_put_failure;
	}
#endif /* CONFIG_NET_CLS_IND */
	if (head->mask != 0xFFFFFFFF &&
	    nla_put_u32(skb, TCA_FW_MASK, head->mask))
		goto nla_put_failure;

	if (tcf_exts_dump(skb, &f->exts) < 0)
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	if (tcf_exts_dump_stats(skb, &f->exts) < 0)
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static void fw_bind_class(void *fh, u32 classid, unsigned long cl)
{
	struct fw_filter *f = fh;

	if (f && f->res.classid == classid)
		f->res.class = cl;
}

static struct tcf_proto_ops cls_fw_ops __read_mostly = {
	.kind		=	"fw",
	.classify	=	fw_classify,
	.init		=	fw_init,
	.destroy	=	fw_destroy,
	.get		=	fw_get,
	.change		=	fw_change,
	.delete		=	fw_delete,
	.walk		=	fw_walk,
	.dump		=	fw_dump,
	.bind_class	=	fw_bind_class,
	.owner		=	THIS_MODULE,
};

static int __init init_fw(void)
{
	return register_tcf_proto_ops(&cls_fw_ops);
}

static void __exit exit_fw(void)
{
	unregister_tcf_proto_ops(&cls_fw_ops);
}

module_init(init_fw)
module_exit(exit_fw)
MODULE_LICENSE("GPL");
