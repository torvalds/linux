// SPDX-License-Identifier: GPL-2.0-only
/*
 * net/sched/sch_drr.c         Deficit Round Robin scheduler
 *
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
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

	struct gnet_stats_basic_sync		bstats;
	struct gnet_stats_queue		qstats;
	struct net_rate_estimator __rcu *rate_est;
	struct list_head		alist;
	struct Qdisc			*qdisc;

	u32				quantum;
	u32				deficit;
};

struct drr_sched {
	struct list_head		active;
	struct tcf_proto __rcu		*filter_list;
	struct tcf_block		*block;
	struct Qdisc_class_hash		clhash;
};

static bool cl_is_active(struct drr_class *cl)
{
	return !list_empty(&cl->alist);
}

static struct drr_class *drr_find_class(struct Qdisc *sch, u32 classid)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct Qdisc_class_common *clc;

	clc = qdisc_class_find(&q->clhash, classid);
	if (clc == NULL)
		return NULL;
	return container_of(clc, struct drr_class, common);
}

static const struct nla_policy drr_policy[TCA_DRR_MAX + 1] = {
	[TCA_DRR_QUANTUM]	= { .type = NLA_U32 },
};

static int drr_change_class(struct Qdisc *sch, u32 classid, u32 parentid,
			    struct nlattr **tca, unsigned long *arg,
			    struct netlink_ext_ack *extack)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl = (struct drr_class *)*arg;
	struct nlattr *opt = tca[TCA_OPTIONS];
	struct nlattr *tb[TCA_DRR_MAX + 1];
	u32 quantum;
	int err;

	if (!opt) {
		NL_SET_ERR_MSG(extack, "DRR options are required for this operation");
		return -EINVAL;
	}

	err = nla_parse_nested_deprecated(tb, TCA_DRR_MAX, opt, drr_policy,
					  extack);
	if (err < 0)
		return err;

	if (tb[TCA_DRR_QUANTUM]) {
		quantum = nla_get_u32(tb[TCA_DRR_QUANTUM]);
		if (quantum == 0) {
			NL_SET_ERR_MSG(extack, "Specified DRR quantum cannot be zero");
			return -EINVAL;
		}
	} else
		quantum = psched_mtu(qdisc_dev(sch));

	if (cl != NULL) {
		if (tca[TCA_RATE]) {
			err = gen_replace_estimator(&cl->bstats, NULL,
						    &cl->rate_est,
						    NULL, true,
						    tca[TCA_RATE]);
			if (err) {
				NL_SET_ERR_MSG(extack, "Failed to replace estimator");
				return err;
			}
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

	gnet_stats_basic_sync_init(&cl->bstats);
	INIT_LIST_HEAD(&cl->alist);
	cl->common.classid = classid;
	cl->quantum	   = quantum;
	cl->qdisc	   = qdisc_create_dflt(sch->dev_queue,
					       &pfifo_qdisc_ops, classid,
					       NULL);
	if (cl->qdisc == NULL)
		cl->qdisc = &noop_qdisc;
	else
		qdisc_hash_add(cl->qdisc, true);

	if (tca[TCA_RATE]) {
		err = gen_replace_estimator(&cl->bstats, NULL, &cl->rate_est,
					    NULL, true, tca[TCA_RATE]);
		if (err) {
			NL_SET_ERR_MSG(extack, "Failed to replace estimator");
			qdisc_put(cl->qdisc);
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
	gen_kill_estimator(&cl->rate_est);
	qdisc_put(cl->qdisc);
	kfree(cl);
}

static int drr_delete_class(struct Qdisc *sch, unsigned long arg,
			    struct netlink_ext_ack *extack)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl = (struct drr_class *)arg;

	if (qdisc_class_in_use(&cl->common)) {
		NL_SET_ERR_MSG(extack, "DRR class is in use");
		return -EBUSY;
	}

	sch_tree_lock(sch);

	qdisc_purge_queue(cl->qdisc);
	qdisc_class_hash_remove(&q->clhash, &cl->common);

	sch_tree_unlock(sch);

	drr_destroy_class(sch, cl);
	return 0;
}

static unsigned long drr_search_class(struct Qdisc *sch, u32 classid)
{
	return (unsigned long)drr_find_class(sch, classid);
}

static struct tcf_block *drr_tcf_block(struct Qdisc *sch, unsigned long cl,
				       struct netlink_ext_ack *extack)
{
	struct drr_sched *q = qdisc_priv(sch);

	if (cl) {
		NL_SET_ERR_MSG(extack, "DRR classid must be zero");
		return NULL;
	}

	return q->block;
}

static unsigned long drr_bind_tcf(struct Qdisc *sch, unsigned long parent,
				  u32 classid)
{
	struct drr_class *cl = drr_find_class(sch, classid);

	if (cl)
		qdisc_class_get(&cl->common);

	return (unsigned long)cl;
}

static void drr_unbind_tcf(struct Qdisc *sch, unsigned long arg)
{
	struct drr_class *cl = (struct drr_class *)arg;

	qdisc_class_put(&cl->common);
}

static int drr_graft_class(struct Qdisc *sch, unsigned long arg,
			   struct Qdisc *new, struct Qdisc **old,
			   struct netlink_ext_ack *extack)
{
	struct drr_class *cl = (struct drr_class *)arg;

	if (new == NULL) {
		new = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
					cl->common.classid, NULL);
		if (new == NULL)
			new = &noop_qdisc;
	}

	*old = qdisc_replace(sch, new, &cl->qdisc);
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

	list_del_init(&cl->alist);
}

static int drr_dump_class(struct Qdisc *sch, unsigned long arg,
			  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct drr_class *cl = (struct drr_class *)arg;
	struct nlattr *nest;

	tcm->tcm_parent	= TC_H_ROOT;
	tcm->tcm_handle	= cl->common.classid;
	tcm->tcm_info	= cl->qdisc->handle;

	nest = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;
	if (nla_put_u32(skb, TCA_DRR_QUANTUM, cl->quantum))
		goto nla_put_failure;
	return nla_nest_end(skb, nest);

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int drr_dump_class_stats(struct Qdisc *sch, unsigned long arg,
				struct gnet_dump *d)
{
	struct drr_class *cl = (struct drr_class *)arg;
	__u32 qlen = qdisc_qlen_sum(cl->qdisc);
	struct Qdisc *cl_q = cl->qdisc;
	struct tc_drr_stats xstats;

	memset(&xstats, 0, sizeof(xstats));
	if (qlen)
		xstats.deficit = cl->deficit;

	if (gnet_stats_copy_basic(d, NULL, &cl->bstats, true) < 0 ||
	    gnet_stats_copy_rate_est(d, &cl->rate_est) < 0 ||
	    gnet_stats_copy_queue(d, cl_q->cpu_qstats, &cl_q->qstats, qlen) < 0)
		return -1;

	return gnet_stats_copy_app(d, &xstats, sizeof(xstats));
}

static void drr_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	unsigned int i;

	if (arg->stop)
		return;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, &q->clhash.hash[i], common.hnode) {
			if (!tc_qdisc_stats_dump(sch, (unsigned long)cl, arg))
				return;
		}
	}
}

static struct drr_class *drr_classify(struct sk_buff *skb, struct Qdisc *sch,
				      int *qerr)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	struct tcf_result res;
	struct tcf_proto *fl;
	int result;

	if (TC_H_MAJ(skb->priority ^ sch->handle) == 0) {
		cl = drr_find_class(sch, skb->priority);
		if (cl != NULL)
			return cl;
	}

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	fl = rcu_dereference_bh(q->filter_list);
	result = tcf_classify(skb, NULL, fl, &res, false);
	if (result >= 0) {
#ifdef CONFIG_NET_CLS_ACT
		switch (result) {
		case TC_ACT_QUEUED:
		case TC_ACT_STOLEN:
		case TC_ACT_TRAP:
			*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
			fallthrough;
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

static int drr_enqueue(struct sk_buff *skb, struct Qdisc *sch,
		       struct sk_buff **to_free)
{
	unsigned int len = qdisc_pkt_len(skb);
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	int err = 0;

	cl = drr_classify(skb, sch, &err);
	if (cl == NULL) {
		if (err & __NET_XMIT_BYPASS)
			qdisc_qstats_drop(sch);
		__qdisc_drop(skb, to_free);
		return err;
	}

	err = qdisc_enqueue(skb, cl->qdisc, to_free);
	if (unlikely(err != NET_XMIT_SUCCESS)) {
		if (net_xmit_drop_count(err)) {
			cl->qstats.drops++;
			qdisc_qstats_drop(sch);
		}
		return err;
	}

	if (!cl_is_active(cl)) {
		list_add_tail(&cl->alist, &q->active);
		cl->deficit = cl->quantum;
	}

	sch->qstats.backlog += len;
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
		if (skb == NULL) {
			qdisc_warn_nonwc(__func__, cl->qdisc);
			goto out;
		}

		len = qdisc_pkt_len(skb);
		if (len <= cl->deficit) {
			cl->deficit -= len;
			skb = qdisc_dequeue_peeked(cl->qdisc);
			if (unlikely(skb == NULL))
				goto out;
			if (cl->qdisc->q.qlen == 0)
				list_del_init(&cl->alist);

			bstats_update(&cl->bstats, skb);
			qdisc_bstats_update(sch, skb);
			qdisc_qstats_backlog_dec(sch, skb);
			sch->q.qlen--;
			return skb;
		}

		cl->deficit += cl->quantum;
		list_move_tail(&cl->alist, &q->active);
	}
out:
	return NULL;
}

static int drr_init_qdisc(struct Qdisc *sch, struct nlattr *opt,
			  struct netlink_ext_ack *extack)
{
	struct drr_sched *q = qdisc_priv(sch);
	int err;

	err = tcf_block_get(&q->block, &q->filter_list, sch, extack);
	if (err)
		return err;
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
	unsigned int i;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, &q->clhash.hash[i], common.hnode) {
			if (cl->qdisc->q.qlen)
				list_del_init(&cl->alist);
			qdisc_reset(cl->qdisc);
		}
	}
}

static void drr_destroy_qdisc(struct Qdisc *sch)
{
	struct drr_sched *q = qdisc_priv(sch);
	struct drr_class *cl;
	struct hlist_node *next;
	unsigned int i;

	tcf_block_put(q->block);

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry_safe(cl, next, &q->clhash.hash[i],
					  common.hnode)
			drr_destroy_class(sch, cl);
	}
	qdisc_class_hash_destroy(&q->clhash);
}

static const struct Qdisc_class_ops drr_class_ops = {
	.change		= drr_change_class,
	.delete		= drr_delete_class,
	.find		= drr_search_class,
	.tcf_block	= drr_tcf_block,
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
	.init		= drr_init_qdisc,
	.reset		= drr_reset_qdisc,
	.destroy	= drr_destroy_qdisc,
	.owner		= THIS_MODULE,
};
MODULE_ALIAS_NET_SCH("drr");

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
MODULE_DESCRIPTION("Deficit Round Robin scheduler");
