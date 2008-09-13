/*
 * Copyright (c) 2008, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Author: Alexander Duyck <alexander.h.duyck@intel.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>


struct multiq_sched_data {
	u16 bands;
	u16 max_bands;
	u16 curband;
	struct tcf_proto *filter_list;
	struct Qdisc **queues;
};


static struct Qdisc *
multiq_classify(struct sk_buff *skb, struct Qdisc *sch, int *qerr)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	u32 band;
	struct tcf_result res;
	int err;

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	err = tc_classify(skb, q->filter_list, &res);
#ifdef CONFIG_NET_CLS_ACT
	switch (err) {
	case TC_ACT_STOLEN:
	case TC_ACT_QUEUED:
		*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
	case TC_ACT_SHOT:
		return NULL;
	}
#endif
	band = skb_get_queue_mapping(skb);

	if (band >= q->bands)
		return q->queues[0];

	return q->queues[band];
}

static int
multiq_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct Qdisc *qdisc;
	int ret;

	qdisc = multiq_classify(skb, sch, &ret);
#ifdef CONFIG_NET_CLS_ACT
	if (qdisc == NULL) {

		if (ret & __NET_XMIT_BYPASS)
			sch->qstats.drops++;
		kfree_skb(skb);
		return ret;
	}
#endif

	ret = qdisc_enqueue(skb, qdisc);
	if (ret == NET_XMIT_SUCCESS) {
		sch->bstats.bytes += qdisc_pkt_len(skb);
		sch->bstats.packets++;
		sch->q.qlen++;
		return NET_XMIT_SUCCESS;
	}
	if (net_xmit_drop_count(ret))
		sch->qstats.drops++;
	return ret;
}


static int
multiq_requeue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct Qdisc *qdisc;
	int ret;

	qdisc = multiq_classify(skb, sch, &ret);
#ifdef CONFIG_NET_CLS_ACT
	if (qdisc == NULL) {
		if (ret & __NET_XMIT_BYPASS)
			sch->qstats.drops++;
		kfree_skb(skb);
		return ret;
	}
#endif

	ret = qdisc->ops->requeue(skb, qdisc);
	if (ret == NET_XMIT_SUCCESS) {
		sch->q.qlen++;
		sch->qstats.requeues++;
		return NET_XMIT_SUCCESS;
	}
	if (net_xmit_drop_count(ret))
		sch->qstats.drops++;
	return ret;
}


static struct sk_buff *multiq_dequeue(struct Qdisc *sch)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	struct Qdisc *qdisc;
	struct sk_buff *skb;
	int band;

	for (band = 0; band < q->bands; band++) {
		/* cycle through bands to ensure fairness */
		q->curband++;
		if (q->curband >= q->bands)
			q->curband = 0;

		/* Check that target subqueue is available before
		 * pulling an skb to avoid excessive requeues
		 */
		if (!__netif_subqueue_stopped(qdisc_dev(sch), q->curband)) {
			qdisc = q->queues[q->curband];
			skb = qdisc->dequeue(qdisc);
			if (skb) {
				sch->q.qlen--;
				return skb;
			}
		}
	}
	return NULL;

}

static unsigned int multiq_drop(struct Qdisc *sch)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	int band;
	unsigned int len;
	struct Qdisc *qdisc;

	for (band = q->bands-1; band >= 0; band--) {
		qdisc = q->queues[band];
		if (qdisc->ops->drop) {
			len = qdisc->ops->drop(qdisc);
			if (len != 0) {
				sch->q.qlen--;
				return len;
			}
		}
	}
	return 0;
}


static void
multiq_reset(struct Qdisc *sch)
{
	u16 band;
	struct multiq_sched_data *q = qdisc_priv(sch);

	for (band = 0; band < q->bands; band++)
		qdisc_reset(q->queues[band]);
	sch->q.qlen = 0;
	q->curband = 0;
}

static void
multiq_destroy(struct Qdisc *sch)
{
	int band;
	struct multiq_sched_data *q = qdisc_priv(sch);

	tcf_destroy_chain(&q->filter_list);
	for (band = 0; band < q->bands; band++)
		qdisc_destroy(q->queues[band]);

	kfree(q->queues);
}

static int multiq_tune(struct Qdisc *sch, struct nlattr *opt)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	struct tc_multiq_qopt *qopt;
	int i;

	if (!netif_is_multiqueue(qdisc_dev(sch)))
		return -EINVAL;
	if (nla_len(opt) < sizeof(*qopt))
		return -EINVAL;

	qopt = nla_data(opt);

	qopt->bands = qdisc_dev(sch)->real_num_tx_queues;

	sch_tree_lock(sch);
	q->bands = qopt->bands;
	for (i = q->bands; i < q->max_bands; i++) {
		if (q->queues[i] != &noop_qdisc) {
			struct Qdisc *child = xchg(&q->queues[i], &noop_qdisc);
			qdisc_tree_decrease_qlen(child, child->q.qlen);
			qdisc_destroy(child);
		}
	}

	sch_tree_unlock(sch);

	for (i = 0; i < q->bands; i++) {
		if (q->queues[i] == &noop_qdisc) {
			struct Qdisc *child;
			child = qdisc_create_dflt(qdisc_dev(sch),
						  sch->dev_queue,
						  &pfifo_qdisc_ops,
						  TC_H_MAKE(sch->handle,
							    i + 1));
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

static int multiq_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	int i, err;

	q->queues = NULL;

	if (opt == NULL)
		return -EINVAL;

	q->max_bands = qdisc_dev(sch)->num_tx_queues;

	q->queues = kcalloc(q->max_bands, sizeof(struct Qdisc *), GFP_KERNEL);
	if (!q->queues)
		return -ENOBUFS;
	for (i = 0; i < q->max_bands; i++)
		q->queues[i] = &noop_qdisc;

	err = multiq_tune(sch,opt);

	if (err)
		kfree(q->queues);

	return err;
}

static int multiq_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	unsigned char *b = skb_tail_pointer(skb);
	struct tc_multiq_qopt opt;

	opt.bands = q->bands;
	opt.max_bands = q->max_bands;

	NLA_PUT(skb, TCA_OPTIONS, sizeof(opt), &opt);

	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int multiq_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		      struct Qdisc **old)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
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
multiq_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	unsigned long band = arg - 1;

	if (band >= q->bands)
		return NULL;

	return q->queues[band];
}

static unsigned long multiq_get(struct Qdisc *sch, u32 classid)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	unsigned long band = TC_H_MIN(classid);

	if (band - 1 >= q->bands)
		return 0;
	return band;
}

static unsigned long multiq_bind(struct Qdisc *sch, unsigned long parent,
				 u32 classid)
{
	return multiq_get(sch, classid);
}


static void multiq_put(struct Qdisc *q, unsigned long cl)
{
	return;
}

static int multiq_change(struct Qdisc *sch, u32 handle, u32 parent,
			 struct nlattr **tca, unsigned long *arg)
{
	unsigned long cl = *arg;
	struct multiq_sched_data *q = qdisc_priv(sch);

	if (cl - 1 > q->bands)
		return -ENOENT;
	return 0;
}

static int multiq_delete(struct Qdisc *sch, unsigned long cl)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	if (cl - 1 > q->bands)
		return -ENOENT;
	return 0;
}


static int multiq_dump_class(struct Qdisc *sch, unsigned long cl,
			     struct sk_buff *skb, struct tcmsg *tcm)
{
	struct multiq_sched_data *q = qdisc_priv(sch);

	if (cl - 1 > q->bands)
		return -ENOENT;
	tcm->tcm_handle |= TC_H_MIN(cl);
	if (q->queues[cl-1])
		tcm->tcm_info = q->queues[cl-1]->handle;
	return 0;
}

static int multiq_dump_class_stats(struct Qdisc *sch, unsigned long cl,
				 struct gnet_dump *d)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	struct Qdisc *cl_q;

	cl_q = q->queues[cl - 1];
	if (gnet_stats_copy_basic(d, &cl_q->bstats) < 0 ||
	    gnet_stats_copy_queue(d, &cl_q->qstats) < 0)
		return -1;

	return 0;
}

static void multiq_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct multiq_sched_data *q = qdisc_priv(sch);
	int band;

	if (arg->stop)
		return;

	for (band = 0; band < q->bands; band++) {
		if (arg->count < arg->skip) {
			arg->count++;
			continue;
		}
		if (arg->fn(sch, band+1, arg) < 0) {
			arg->stop = 1;
			break;
		}
		arg->count++;
	}
}

static struct tcf_proto **multiq_find_tcf(struct Qdisc *sch, unsigned long cl)
{
	struct multiq_sched_data *q = qdisc_priv(sch);

	if (cl)
		return NULL;
	return &q->filter_list;
}

static const struct Qdisc_class_ops multiq_class_ops = {
	.graft		=	multiq_graft,
	.leaf		=	multiq_leaf,
	.get		=	multiq_get,
	.put		=	multiq_put,
	.change		=	multiq_change,
	.delete		=	multiq_delete,
	.walk		=	multiq_walk,
	.tcf_chain	=	multiq_find_tcf,
	.bind_tcf	=	multiq_bind,
	.unbind_tcf	=	multiq_put,
	.dump		=	multiq_dump_class,
	.dump_stats	=	multiq_dump_class_stats,
};

static struct Qdisc_ops multiq_qdisc_ops __read_mostly = {
	.next		=	NULL,
	.cl_ops		=	&multiq_class_ops,
	.id		=	"multiq",
	.priv_size	=	sizeof(struct multiq_sched_data),
	.enqueue	=	multiq_enqueue,
	.dequeue	=	multiq_dequeue,
	.requeue	=	multiq_requeue,
	.drop		=	multiq_drop,
	.init		=	multiq_init,
	.reset		=	multiq_reset,
	.destroy	=	multiq_destroy,
	.change		=	multiq_tune,
	.dump		=	multiq_dump,
	.owner		=	THIS_MODULE,
};

static int __init multiq_module_init(void)
{
	return register_qdisc(&multiq_qdisc_ops);
}

static void __exit multiq_module_exit(void)
{
	unregister_qdisc(&multiq_qdisc_ops);
}

module_init(multiq_module_init)
module_exit(multiq_module_exit)

MODULE_LICENSE("GPL");
