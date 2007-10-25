/*
 * net/sched/sch_prio.c	Simple 3-band priority "scheduler".
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 * Fixes:       19990609: J Hadi Salim <hadi@nortelnetworks.com>:
 *              Init --  EINVAL when opt undefined
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>


struct prio_sched_data
{
	int bands;
	int curband; /* for round-robin */
	struct tcf_proto *filter_list;
	u8  prio2band[TC_PRIO_MAX+1];
	struct Qdisc *queues[TCQ_PRIO_BANDS];
	int mq;
};


static struct Qdisc *
prio_classify(struct sk_buff *skb, struct Qdisc *sch, int *qerr)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	u32 band = skb->priority;
	struct tcf_result res;
	int err;

	*qerr = NET_XMIT_BYPASS;
	if (TC_H_MAJ(skb->priority) != sch->handle) {
		err = tc_classify(skb, q->filter_list, &res);
#ifdef CONFIG_NET_CLS_ACT
		switch (err) {
		case TC_ACT_STOLEN:
		case TC_ACT_QUEUED:
			*qerr = NET_XMIT_SUCCESS;
		case TC_ACT_SHOT:
			return NULL;
		}
#endif
		if (!q->filter_list || err < 0) {
			if (TC_H_MAJ(band))
				band = 0;
			band = q->prio2band[band&TC_PRIO_MAX];
			goto out;
		}
		band = res.classid;
	}
	band = TC_H_MIN(band) - 1;
	if (band >= q->bands)
		band = q->prio2band[0];
out:
	if (q->mq)
		skb_set_queue_mapping(skb, band);
	return q->queues[band];
}

static int
prio_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct Qdisc *qdisc;
	int ret;

	qdisc = prio_classify(skb, sch, &ret);
#ifdef CONFIG_NET_CLS_ACT
	if (qdisc == NULL) {

		if (ret == NET_XMIT_BYPASS)
			sch->qstats.drops++;
		kfree_skb(skb);
		return ret;
	}
#endif

	if ((ret = qdisc->enqueue(skb, qdisc)) == NET_XMIT_SUCCESS) {
		sch->bstats.bytes += skb->len;
		sch->bstats.packets++;
		sch->q.qlen++;
		return NET_XMIT_SUCCESS;
	}
	sch->qstats.drops++;
	return ret;
}


static int
prio_requeue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct Qdisc *qdisc;
	int ret;

	qdisc = prio_classify(skb, sch, &ret);
#ifdef CONFIG_NET_CLS_ACT
	if (qdisc == NULL) {
		if (ret == NET_XMIT_BYPASS)
			sch->qstats.drops++;
		kfree_skb(skb);
		return ret;
	}
#endif

	if ((ret = qdisc->ops->requeue(skb, qdisc)) == NET_XMIT_SUCCESS) {
		sch->q.qlen++;
		sch->qstats.requeues++;
		return 0;
	}
	sch->qstats.drops++;
	return NET_XMIT_DROP;
}


static struct sk_buff *
prio_dequeue(struct Qdisc* sch)
{
	struct sk_buff *skb;
	struct prio_sched_data *q = qdisc_priv(sch);
	int prio;
	struct Qdisc *qdisc;

	for (prio = 0; prio < q->bands; prio++) {
		/* Check if the target subqueue is available before
		 * pulling an skb.  This way we avoid excessive requeues
		 * for slower queues.
		 */
		if (!__netif_subqueue_stopped(sch->dev, (q->mq ? prio : 0))) {
			qdisc = q->queues[prio];
			skb = qdisc->dequeue(qdisc);
			if (skb) {
				sch->q.qlen--;
				return skb;
			}
		}
	}
	return NULL;

}

static struct sk_buff *rr_dequeue(struct Qdisc* sch)
{
	struct sk_buff *skb;
	struct prio_sched_data *q = qdisc_priv(sch);
	struct Qdisc *qdisc;
	int bandcount;

	/* Only take one pass through the queues.  If nothing is available,
	 * return nothing.
	 */
	for (bandcount = 0; bandcount < q->bands; bandcount++) {
		/* Check if the target subqueue is available before
		 * pulling an skb.  This way we avoid excessive requeues
		 * for slower queues.  If the queue is stopped, try the
		 * next queue.
		 */
		if (!__netif_subqueue_stopped(sch->dev,
					    (q->mq ? q->curband : 0))) {
			qdisc = q->queues[q->curband];
			skb = qdisc->dequeue(qdisc);
			if (skb) {
				sch->q.qlen--;
				q->curband++;
				if (q->curband >= q->bands)
					q->curband = 0;
				return skb;
			}
		}
		q->curband++;
		if (q->curband >= q->bands)
			q->curband = 0;
	}
	return NULL;
}

static unsigned int prio_drop(struct Qdisc* sch)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	int prio;
	unsigned int len;
	struct Qdisc *qdisc;

	for (prio = q->bands-1; prio >= 0; prio--) {
		qdisc = q->queues[prio];
		if (qdisc->ops->drop && (len = qdisc->ops->drop(qdisc)) != 0) {
			sch->q.qlen--;
			return len;
		}
	}
	return 0;
}


static void
prio_reset(struct Qdisc* sch)
{
	int prio;
	struct prio_sched_data *q = qdisc_priv(sch);

	for (prio=0; prio<q->bands; prio++)
		qdisc_reset(q->queues[prio]);
	sch->q.qlen = 0;
}

static void
prio_destroy(struct Qdisc* sch)
{
	int prio;
	struct prio_sched_data *q = qdisc_priv(sch);

	tcf_destroy_chain(q->filter_list);
	for (prio=0; prio<q->bands; prio++)
		qdisc_destroy(q->queues[prio]);
}

static int prio_tune(struct Qdisc *sch, struct rtattr *opt)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	struct tc_prio_qopt *qopt;
	struct rtattr *tb[TCA_PRIO_MAX];
	int i;

	if (rtattr_parse_nested_compat(tb, TCA_PRIO_MAX, opt, qopt,
				       sizeof(*qopt)))
		return -EINVAL;
	q->bands = qopt->bands;
	/* If we're multiqueue, make sure the number of incoming bands
	 * matches the number of queues on the device we're associating with.
	 * If the number of bands requested is zero, then set q->bands to
	 * dev->egress_subqueue_count.  Also, the root qdisc must be the
	 * only one that is enabled for multiqueue, since it's the only one
	 * that interacts with the underlying device.
	 */
	q->mq = RTA_GET_FLAG(tb[TCA_PRIO_MQ - 1]);
	if (q->mq) {
		if (sch->parent != TC_H_ROOT)
			return -EINVAL;
		if (netif_is_multiqueue(sch->dev)) {
			if (q->bands == 0)
				q->bands = sch->dev->egress_subqueue_count;
			else if (q->bands != sch->dev->egress_subqueue_count)
				return -EINVAL;
		} else
			return -EOPNOTSUPP;
	}

	if (q->bands > TCQ_PRIO_BANDS || q->bands < 2)
		return -EINVAL;

	for (i=0; i<=TC_PRIO_MAX; i++) {
		if (qopt->priomap[i] >= q->bands)
			return -EINVAL;
	}

	sch_tree_lock(sch);
	memcpy(q->prio2band, qopt->priomap, TC_PRIO_MAX+1);

	for (i=q->bands; i<TCQ_PRIO_BANDS; i++) {
		struct Qdisc *child = xchg(&q->queues[i], &noop_qdisc);
		if (child != &noop_qdisc) {
			qdisc_tree_decrease_qlen(child, child->q.qlen);
			qdisc_destroy(child);
		}
	}
	sch_tree_unlock(sch);

	for (i=0; i<q->bands; i++) {
		if (q->queues[i] == &noop_qdisc) {
			struct Qdisc *child;
			child = qdisc_create_dflt(sch->dev, &pfifo_qdisc_ops,
						  TC_H_MAKE(sch->handle, i + 1));
			if (child) {
				sch_tree_lock(sch);
				child = xchg(&q->queues[i], child);

				if (child != &noop_qdisc) {
					qdisc_tree_decrease_qlen(child,
								 child->q.qlen);
					qdisc_destroy(child);
				}
				sch_tree_unlock(sch);
			}
		}
	}
	return 0;
}

static int prio_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	int i;

	for (i=0; i<TCQ_PRIO_BANDS; i++)
		q->queues[i] = &noop_qdisc;

	if (opt == NULL) {
		return -EINVAL;
	} else {
		int err;

		if ((err= prio_tune(sch, opt)) != 0)
			return err;
	}
	return 0;
}

static int prio_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	unsigned char *b = skb_tail_pointer(skb);
	struct rtattr *nest;
	struct tc_prio_qopt opt;

	opt.bands = q->bands;
	memcpy(&opt.priomap, q->prio2band, TC_PRIO_MAX+1);

	nest = RTA_NEST_COMPAT(skb, TCA_OPTIONS, sizeof(opt), &opt);
	if (q->mq)
		RTA_PUT_FLAG(skb, TCA_PRIO_MQ);
	RTA_NEST_COMPAT_END(skb, nest);

	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int prio_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		      struct Qdisc **old)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	unsigned long band = arg - 1;

	if (band >= q->bands)
		return -EINVAL;

	if (new == NULL)
		new = &noop_qdisc;

	sch_tree_lock(sch);
	*old = q->queues[band];
	q->queues[band] = new;
	qdisc_tree_decrease_qlen(*old, (*old)->q.qlen);
	qdisc_reset(*old);
	sch_tree_unlock(sch);

	return 0;
}

static struct Qdisc *
prio_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	unsigned long band = arg - 1;

	if (band >= q->bands)
		return NULL;

	return q->queues[band];
}

static unsigned long prio_get(struct Qdisc *sch, u32 classid)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	unsigned long band = TC_H_MIN(classid);

	if (band - 1 >= q->bands)
		return 0;
	return band;
}

static unsigned long prio_bind(struct Qdisc *sch, unsigned long parent, u32 classid)
{
	return prio_get(sch, classid);
}


static void prio_put(struct Qdisc *q, unsigned long cl)
{
	return;
}

static int prio_change(struct Qdisc *sch, u32 handle, u32 parent, struct rtattr **tca, unsigned long *arg)
{
	unsigned long cl = *arg;
	struct prio_sched_data *q = qdisc_priv(sch);

	if (cl - 1 > q->bands)
		return -ENOENT;
	return 0;
}

static int prio_delete(struct Qdisc *sch, unsigned long cl)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	if (cl - 1 > q->bands)
		return -ENOENT;
	return 0;
}


static int prio_dump_class(struct Qdisc *sch, unsigned long cl, struct sk_buff *skb,
			   struct tcmsg *tcm)
{
	struct prio_sched_data *q = qdisc_priv(sch);

	if (cl - 1 > q->bands)
		return -ENOENT;
	tcm->tcm_handle |= TC_H_MIN(cl);
	if (q->queues[cl-1])
		tcm->tcm_info = q->queues[cl-1]->handle;
	return 0;
}

static int prio_dump_class_stats(struct Qdisc *sch, unsigned long cl,
				 struct gnet_dump *d)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	struct Qdisc *cl_q;

	cl_q = q->queues[cl - 1];
	if (gnet_stats_copy_basic(d, &cl_q->bstats) < 0 ||
	    gnet_stats_copy_queue(d, &cl_q->qstats) < 0)
		return -1;

	return 0;
}

static void prio_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	int prio;

	if (arg->stop)
		return;

	for (prio = 0; prio < q->bands; prio++) {
		if (arg->count < arg->skip) {
			arg->count++;
			continue;
		}
		if (arg->fn(sch, prio+1, arg) < 0) {
			arg->stop = 1;
			break;
		}
		arg->count++;
	}
}

static struct tcf_proto ** prio_find_tcf(struct Qdisc *sch, unsigned long cl)
{
	struct prio_sched_data *q = qdisc_priv(sch);

	if (cl)
		return NULL;
	return &q->filter_list;
}

static struct Qdisc_class_ops prio_class_ops = {
	.graft		=	prio_graft,
	.leaf		=	prio_leaf,
	.get		=	prio_get,
	.put		=	prio_put,
	.change		=	prio_change,
	.delete		=	prio_delete,
	.walk		=	prio_walk,
	.tcf_chain	=	prio_find_tcf,
	.bind_tcf	=	prio_bind,
	.unbind_tcf	=	prio_put,
	.dump		=	prio_dump_class,
	.dump_stats	=	prio_dump_class_stats,
};

static struct Qdisc_ops prio_qdisc_ops = {
	.next		=	NULL,
	.cl_ops		=	&prio_class_ops,
	.id		=	"prio",
	.priv_size	=	sizeof(struct prio_sched_data),
	.enqueue	=	prio_enqueue,
	.dequeue	=	prio_dequeue,
	.requeue	=	prio_requeue,
	.drop		=	prio_drop,
	.init		=	prio_init,
	.reset		=	prio_reset,
	.destroy	=	prio_destroy,
	.change		=	prio_tune,
	.dump		=	prio_dump,
	.owner		=	THIS_MODULE,
};

static struct Qdisc_ops rr_qdisc_ops = {
	.next		=	NULL,
	.cl_ops		=	&prio_class_ops,
	.id		=	"rr",
	.priv_size	=	sizeof(struct prio_sched_data),
	.enqueue	=	prio_enqueue,
	.dequeue	=	rr_dequeue,
	.requeue	=	prio_requeue,
	.drop		=	prio_drop,
	.init		=	prio_init,
	.reset		=	prio_reset,
	.destroy	=	prio_destroy,
	.change		=	prio_tune,
	.dump		=	prio_dump,
	.owner		=	THIS_MODULE,
};

static int __init prio_module_init(void)
{
	int err;

	err = register_qdisc(&prio_qdisc_ops);
	if (err < 0)
		return err;
	err = register_qdisc(&rr_qdisc_ops);
	if (err < 0)
		unregister_qdisc(&prio_qdisc_ops);
	return err;
}

static void __exit prio_module_exit(void)
{
	unregister_qdisc(&prio_qdisc_ops);
	unregister_qdisc(&rr_qdisc_ops);
}

module_init(prio_module_init)
module_exit(prio_module_exit)

MODULE_LICENSE("GPL");
MODULE_ALIAS("sch_rr");
