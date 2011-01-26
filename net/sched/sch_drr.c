/*
 * net/sched/sch_drr.c         Deficit Round Robin scheduler
 *
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/pkt_sched.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>

struct drr_class {
	struct Qdisc_class_common	common;
	unsigned int			refcnt;
	unsigned int			filter_cnt;

	struct gnet_stats_basic_packed		bstats;
	struct gnet_stats_queue		qstats;
	struct gnet_stats_rate_est	rate_est;
	struct list_head		alist;
	struct Qdisc			*qdisc;

	u32				quantum;
	u32				deficit;
};

struct drr_sched {
	struct list_head		active;
	struct tcf_proto		*filter_list;
	struct Qdisc_class_hash		clhash;
};

static struct drr_class *drr_find_class(struct Qdisc *sch, u32 classid)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct Qdisc_class_common *clc;

	clc = qdisc_class_find(&q->clhash, classid);
	if (clc == NULL)
		return NULL;
	return container_of(clc, struct drr_class, common);
}

static void drr_purge_queue(struct drr_class *cl)
{
	unsigned int len = cl->qdisc->q.qlen;

	qdisc_reset(cl->qdisc);
	qdisc_tree_decrease_qlen(cl->qdisc, len);
}

static const struct nla_policy drr_policy[TCA_DRR_MAX + 1] = {
	[TCA_DRR_QUANTUM]	= { .type = NLA_U32 },
};

static int drr_change_class(struct Qdisc *sch, u32 classid, u32 parentid,
			    struct nlattr **tca, unsigned long *arg)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl = (struct drr_class *)*arg;
	struct nlattr *opt = tca[TCA_OPTIONS];
	struct nlattr *tb[TCA_DRR_MAX + 1];
	u32 quantum;
	int err;

	if (!opt)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_DRR_MAX, opt, drr_policy);
	if (err < 0)
		return err;

	if (tb[TCA_DRR_QUANTUM]) {
		quantum = nla_get_u32(tb[TCA_DRR_QUANTUM]);
		if (quantum == 0)
			return -EINVAL;
	} else
		quantum = psched_mtu(qdisc_dev(sch));

	if (cl != NULL) {
		if (tca[TCA_RATE]) {
			err = gen_replace_estimator(&cl->bstats, &cl->rate_est,
						    qdisc_root_sleeping_lock(sch),
						    tca[TCA_RATE]);
			if (err)
				return err;
		}

		sch_tree_lock(sch);
		if (tb[TCA_DRR_QUANTUM])
			cl->quantum = quantum;
		sch_tree_unlock(sch);

		return 0;
	}

	cl = kzalloc(sizeof(struct drr_class), GFP_KERNEL);
	if (cl == NULL)
		return -ENOBUFS;

	cl->refcnt	   = 1;
	cl->common.classid = classid;
	cl->quantum	   = quantum;
	cl->qdisc	   = qdisc_create_dflt(sch->dev_queue,
					       &pfifo_qdisc_ops, classid);
	if (cl->qdisc == NULL)
		cl->qdisc = &noop_qdisc;

	if (tca[TCA_RATE]) {
		err = gen_replace_estimator(&cl->bstats, &cl->rate_est,
					    qdisc_root_sleeping_lock(sch),
					    tca[TCA_RATE]);
		if (err) {
			qdisc_destroy(cl->qdisc);
			kfree(cl);
			return err;
		}
	}

	sch_tree_lock(sch);
	qdisc_class_hash_insert(&q->clhash, &cl->common);
	sch_tree_unlock(sch);

	qdisc_class_hash_grow(sch, &q->clhash);

	*arg = (unsigned long)cl;
	return 0;
}

static void drr_destroy_class(struct Qdisc *sch, struct drr_class *cl)
{
	gen_kill_estimator(&cl->bstats, &cl->rate_est);
	qdisc_destroy(cl->qdisc);
	kfree(cl);
}

static int drr_delete_class(struct Qdisc *sch, unsigned long arg)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl = (struct drr_class *)arg;

	if (cl->filter_cnt > 0)
		return -EBUSY;

	sch_tree_lock(sch);

	drr_purge_queue(cl);
	qdisc_class_hash_remove(&q->clhash, &cl->common);

	BUG_ON(--cl->refcnt == 0);
	/*
	 * This shouldn't happen: we "hold" one cops->get() when called
	 * from tc_ctl_tclass; the destroy method is done from cops->put().
	 */

	sch_tree_unlock(sch);
	return 0;
}

static unsigned long drr_get_class(struct Qdisc *sch, u32 classid)
{
	struct drr_class *cl = drr_find_class(sch, classid);

	if (cl != NULL)
		cl->refcnt++;

	return (unsigned long)cl;
}

static void drr_put_class(struct Qdisc *sch, unsigned long arg)
{
	struct drr_class *cl = (struct drr_class *)arg;

	if (--cl->refcnt == 0)
		drr_destroy_class(sch, cl);
}

static struct tcf_proto **drr_tcf_chain(struct Qdisc *sch, unsigned long cl)
{
	struct drr_sched *q = qdisc_priv(sch);

	if (cl)
		return NULL;

	return &q->filter_list;
}

static unsigned long drr_bind_tcf(struct Qdisc *sch, unsigned long parent,
				  u32 classid)
{
	struct drr_class *cl = drr_find_class(sch, classid);

	if (cl != NULL)
		cl->filter_cnt++;

	return (unsigned long)cl;
}

static void drr_unbind_tcf(struct Qdisc *sch, unsigned long arg)
{
	struct drr_class *cl = (struct drr_class *)arg;

	cl->filter_cnt--;
}

static int drr_graft_class(struct Qdisc *sch, unsigned long arg,
			   struct Qdisc *new, struct Qdisc **old)
{
	struct drr_class *cl = (struct drr_class *)arg;

	if (new == NULL) {
		new = qdisc_create_dflt(sch->dev_queue,
					&pfifo_qdisc_ops, cl->common.classid);
		if (new == NULL)
			new = &noop_qdisc;
	}

	sch_tree_lock(sch);
	drr_purge_queue(cl);
	*old = cl->qdisc;
	cl->qdisc = new;
	sch_tree_unlock(sch);
	return 0;
}

static struct Qdisc *drr_class_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct drr_class *cl = (struct drr_class *)arg;

	return cl->qdisc;
}

static void drr_qlen_notify(struct Qdisc *csh, unsigned long arg)
{
	struct drr_class *cl = (struct drr_class *)arg;

	if (cl->qdisc->q.qlen == 0)
		list_del(&cl->alist);
}

static int drr_dump_class(struct Qdisc *sch, unsigned long arg,
			  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct drr_class *cl = (struct drr_class *)arg;
	struct nlattr *nest;

	tcm->tcm_parent	= TC_H_ROOT;
	tcm->tcm_handle	= cl->common.classid;
	tcm->tcm_info	= cl->qdisc->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;
	NLA_PUT_U32(skb, TCA_DRR_QUANTUM, cl->quantum);
	return nla_nest_end(skb, nest);

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int drr_dump_class_stats(struct Qdisc *sch, unsigned long arg,
				struct gnet_dump *d)
{
	struct drr_class *cl = (struct drr_class *)arg;
	struct tc_drr_stats xstats;

	memset(&xstats, 0, sizeof(xstats));
	if (cl->qdisc->q.qlen) {
		xstats.deficit = cl->deficit;
		cl->qdisc->qstats.qlen = cl->qdisc->q.qlen;
	}

	if (gnet_stats_copy_basic(d, &cl->bstats) < 0 ||
	    gnet_stats_copy_rate_est(d, &cl->bstats, &cl->rate_est) < 0 ||
	    gnet_stats_copy_queue(d, &cl->qdisc->qstats) < 0)
		return -1;

	return gnet_stats_copy_app(d, &xstats, sizeof(xstats));
}

static void drr_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	struct hlist_node *n;
	unsigned int i;

	if (arg->stop)
		return;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, n, &q->clhash.hash[i], common.hnode) {
			if (arg->count < arg->skip) {
				arg->count++;
				continue;
			}
			if (arg->fn(sch, (unsigned long)cl, arg) < 0) {
				arg->stop = 1;
				return;
			}
			arg->count++;
		}
	}
}

static struct drr_class *drr_classify(struct sk_buff *skb, struct Qdisc *sch,
				      int *qerr)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	struct tcf_result res;
	int result;

	if (TC_H_MAJ(skb->priority ^ sch->handle) == 0) {
		cl = drr_find_class(sch, skb->priority);
		if (cl != NULL)
			return cl;
	}

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	result = tc_classify(skb, q->filter_list, &res);
	if (result >= 0) {
#ifdef CONFIG_NET_CLS_ACT
		switch (result) {
		case TC_ACT_QUEUED:
		case TC_ACT_STOLEN:
			*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
		case TC_ACT_SHOT:
			return NULL;
		}
#endif
		cl = (struct drr_class *)res.class;
		if (cl == NULL)
			cl = drr_find_class(sch, res.classid);
		return cl;
	}
	return NULL;
}

static int drr_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	int err;

	cl = drr_classify(skb, sch, &err);
	if (cl == NULL) {
		if (err & __NET_XMIT_BYPASS)
			sch->qstats.drops++;
		kfree_skb(skb);
		return err;
	}

	err = qdisc_enqueue(skb, cl->qdisc);
	if (unlikely(err != NET_XMIT_SUCCESS)) {
		if (net_xmit_drop_count(err)) {
			cl->qstats.drops++;
			sch->qstats.drops++;
		}
		return err;
	}

	if (cl->qdisc->q.qlen == 1) {
		list_add_tail(&cl->alist, &q->active);
		cl->deficit = cl->quantum;
	}

	bstats_update(&cl->bstats, skb);
	qdisc_bstats_update(sch, skb);

	sch->q.qlen++;
	return err;
}

static struct sk_buff *drr_dequeue(struct Qdisc *sch)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	struct sk_buff *skb;
	unsigned int len;

	if (list_empty(&q->active))
		goto out;
	while (1) {
		cl = list_first_entry(&q->active, struct drr_class, alist);
		skb = cl->qdisc->ops->peek(cl->qdisc);
		if (skb == NULL)
			goto out;

		len = qdisc_pkt_len(skb);
		if (len <= cl->deficit) {
			cl->deficit -= len;
			skb = qdisc_dequeue_peeked(cl->qdisc);
			if (cl->qdisc->q.qlen == 0)
				list_del(&cl->alist);
			sch->q.qlen--;
			return skb;
		}

		cl->deficit += cl->quantum;
		list_move_tail(&cl->alist, &q->active);
	}
out:
	return NULL;
}

static unsigned int drr_drop(struct Qdisc *sch)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	unsigned int len;

	list_for_each_entry(cl, &q->active, alist) {
		if (cl->qdisc->ops->drop) {
			len = cl->qdisc->ops->drop(cl->qdisc);
			if (len > 0) {
				sch->q.qlen--;
				if (cl->qdisc->q.qlen == 0)
					list_del(&cl->alist);
				return len;
			}
		}
	}
	return 0;
}

static int drr_init_qdisc(struct Qdisc *sch, struct nlattr *opt)
{
	struct drr_sched *q = qdisc_priv(sch);
	int err;

	err = qdisc_class_hash_init(&q->clhash);
	if (err < 0)
		return err;
	INIT_LIST_HEAD(&q->active);
	return 0;
}

static void drr_reset_qdisc(struct Qdisc *sch)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	struct hlist_node *n;
	unsigned int i;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, n, &q->clhash.hash[i], common.hnode) {
			if (cl->qdisc->q.qlen)
				list_del(&cl->alist);
			qdisc_reset(cl->qdisc);
		}
	}
	sch->q.qlen = 0;
}

static void drr_destroy_qdisc(struct Qdisc *sch)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	struct hlist_node *n, *next;
	unsigned int i;

	tcf_destroy_chain(&q->filter_list);

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry_safe(cl, n, next, &q->clhash.hash[i],
					  common.hnode)
			drr_destroy_class(sch, cl);
	}
	qdisc_class_hash_destroy(&q->clhash);
}

static const struct Qdisc_class_ops drr_class_ops = {
	.change		= drr_change_class,
	.delete		= drr_delete_class,
	.get		= drr_get_class,
	.put		= drr_put_class,
	.tcf_chain	= drr_tcf_chain,
	.bind_tcf	= drr_bind_tcf,
	.unbind_tcf	= drr_unbind_tcf,
	.graft		= drr_graft_class,
	.leaf		= drr_class_leaf,
	.qlen_notify	= drr_qlen_notify,
	.dump		= drr_dump_class,
	.dump_stats	= drr_dump_class_stats,
	.walk		= drr_walk,
};

static struct Qdisc_ops drr_qdisc_ops __read_mostly = {
	.cl_ops		= &drr_class_ops,
	.id		= "drr",
	.priv_size	= sizeof(struct drr_sched),
	.enqueue	= drr_enqueue,
	.dequeue	= drr_dequeue,
	.peek		= qdisc_peek_dequeued,
	.drop		= drr_drop,
	.init		= drr_init_qdisc,
	.reset		= drr_reset_qdisc,
	.destroy	= drr_destroy_qdisc,
	.owner		= THIS_MODULE,
};

static int __init drr_init(void)
{
	return register_qdisc(&drr_qdisc_ops);
}

static void __exit drr_exit(void)
{
	unregister_qdisc(&drr_qdisc_ops);
}

module_init(drr_init);
module_exit(drr_exit);
MODULE_LICENSE("GPL");
