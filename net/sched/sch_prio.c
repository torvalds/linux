// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/sch_prio.c	Simple 3-band priority "scheduler".
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 * Fixes:       19990609: J Hadi Salim <hadi@nortelnetworks.com>:
 *              Init --  EINVAL when opt undefined
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>

struct prio_sched_data {
	int bands;
	struct tcf_proto __rcu *filter_list;
	struct tcf_block *block;
	u8  prio2band[TC_PRIO_MAX+1];
	struct Qdisc *queues[TCQ_PRIO_BANDS];
};


static struct Qdisc *
prio_classify(struct sk_buff *skb, struct Qdisc *sch, int *qerr)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	u32 band = skb->priority;
	struct tcf_result res;
	struct tcf_proto *fl;
	int err;

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	if (TC_H_MAJ(skb->priority) != sch->handle) {
		fl = rcu_dereference_bh(q->filter_list);
		err = tcf_classify(skb, NULL, fl, &res, false);
#ifdef CONFIG_NET_CLS_ACT
		switch (err) {
		case TC_ACT_STOLEN:
		case TC_ACT_QUEUED:
		case TC_ACT_TRAP:
			*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
			fallthrough;
		case TC_ACT_SHOT:
			return NULL;
		}
#endif
		if (!fl || err < 0) {
			if (TC_H_MAJ(band))
				band = 0;
			return q->queues[q->prio2band[band & TC_PRIO_MAX]];
		}
		band = res.classid;
	}
	band = TC_H_MIN(band) - 1;
	if (band >= q->bands)
		return q->queues[q->prio2band[0]];

	return q->queues[band];
}

static int
prio_enqueue(struct sk_buff *skb, struct Qdisc *sch, struct sk_buff **to_free)
{
	unsigned int len = qdisc_pkt_len(skb);
	struct Qdisc *qdisc;
	int ret;

	qdisc = prio_classify(skb, sch, &ret);
#ifdef CONFIG_NET_CLS_ACT
	if (qdisc == NULL) {

		if (ret & __NET_XMIT_BYPASS)
			qdisc_qstats_drop(sch);
		__qdisc_drop(skb, to_free);
		return ret;
	}
#endif

	ret = qdisc_enqueue(skb, qdisc, to_free);
	if (ret == NET_XMIT_SUCCESS) {
		sch->qstats.backlog += len;
		sch->q.qlen++;
		return NET_XMIT_SUCCESS;
	}
	if (net_xmit_drop_count(ret))
		qdisc_qstats_drop(sch);
	return ret;
}

static struct sk_buff *prio_peek(struct Qdisc *sch)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	int prio;

	for (prio = 0; prio < q->bands; prio++) {
		struct Qdisc *qdisc = q->queues[prio];
		struct sk_buff *skb = qdisc->ops->peek(qdisc);
		if (skb)
			return skb;
	}
	return NULL;
}

static struct sk_buff *prio_dequeue(struct Qdisc *sch)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	int prio;

	for (prio = 0; prio < q->bands; prio++) {
		struct Qdisc *qdisc = q->queues[prio];
		struct sk_buff *skb = qdisc_dequeue_peeked(qdisc);
		if (skb) {
			qdisc_bstats_update(sch, skb);
			qdisc_qstats_backlog_dec(sch, skb);
			sch->q.qlen--;
			return skb;
		}
	}
	return NULL;

}

static void
prio_reset(struct Qdisc *sch)
{
	int prio;
	struct prio_sched_data *q = qdisc_priv(sch);

	for (prio = 0; prio < q->bands; prio++)
		qdisc_reset(q->queues[prio]);
}

static int prio_offload(struct Qdisc *sch, struct tc_prio_qopt *qopt)
{
	struct net_device *dev = qdisc_dev(sch);
	struct tc_prio_qopt_offload opt = {
		.handle = sch->handle,
		.parent = sch->parent,
	};

	if (!tc_can_offload(dev) || !dev->netdev_ops->ndo_setup_tc)
		return -EOPNOTSUPP;

	if (qopt) {
		opt.command = TC_PRIO_REPLACE;
		opt.replace_params.bands = qopt->bands;
		memcpy(&opt.replace_params.priomap, qopt->priomap,
		       TC_PRIO_MAX + 1);
		opt.replace_params.qstats = &sch->qstats;
	} else {
		opt.command = TC_PRIO_DESTROY;
	}

	return dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_QDISC_PRIO, &opt);
}

static void
prio_destroy(struct Qdisc *sch)
{
	int prio;
	struct prio_sched_data *q = qdisc_priv(sch);

	tcf_block_put(q->block);
	prio_offload(sch, NULL);
	for (prio = 0; prio < q->bands; prio++)
		qdisc_put(q->queues[prio]);
}

static int prio_tune(struct Qdisc *sch, struct nlattr *opt,
		     struct netlink_ext_ack *extack)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	struct Qdisc *queues[TCQ_PRIO_BANDS];
	int oldbands = q->bands, i;
	struct tc_prio_qopt *qopt;

	if (nla_len(opt) < sizeof(*qopt))
		return -EINVAL;
	qopt = nla_data(opt);

	if (qopt->bands > TCQ_PRIO_BANDS || qopt->bands < TCQ_MIN_PRIO_BANDS)
		return -EINVAL;

	for (i = 0; i <= TC_PRIO_MAX; i++) {
		if (qopt->priomap[i] >= qopt->bands)
			return -EINVAL;
	}

	/* Before commit, make sure we can allocate all new qdiscs */
	for (i = oldbands; i < qopt->bands; i++) {
		queues[i] = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
					      TC_H_MAKE(sch->handle, i + 1),
					      extack);
		if (!queues[i]) {
			while (i > oldbands)
				qdisc_put(queues[--i]);
			return -ENOMEM;
		}
	}

	prio_offload(sch, qopt);
	sch_tree_lock(sch);
	q->bands = qopt->bands;
	memcpy(q->prio2band, qopt->priomap, TC_PRIO_MAX+1);

	for (i = q->bands; i < oldbands; i++)
		qdisc_tree_flush_backlog(q->queues[i]);

	for (i = oldbands; i < q->bands; i++) {
		q->queues[i] = queues[i];
		if (q->queues[i] != &noop_qdisc)
			qdisc_hash_add(q->queues[i], true);
	}

	sch_tree_unlock(sch);

	for (i = q->bands; i < oldbands; i++)
		qdisc_put(q->queues[i]);
	return 0;
}

static int prio_init(struct Qdisc *sch, struct nlattr *opt,
		     struct netlink_ext_ack *extack)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	int err;

	if (!opt)
		return -EINVAL;

	err = tcf_block_get(&q->block, &q->filter_list, sch, extack);
	if (err)
		return err;

	return prio_tune(sch, opt, extack);
}

static int prio_dump_offload(struct Qdisc *sch)
{
	struct tc_prio_qopt_offload hw_stats = {
		.command = TC_PRIO_STATS,
		.handle = sch->handle,
		.parent = sch->parent,
		{
			.stats = {
				.bstats = &sch->bstats,
				.qstats = &sch->qstats,
			},
		},
	};

	return qdisc_offload_dump_helper(sch, TC_SETUP_QDISC_PRIO, &hw_stats);
}

static int prio_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	unsigned char *b = skb_tail_pointer(skb);
	struct tc_prio_qopt opt;
	int err;

	opt.bands = q->bands;
	memcpy(&opt.priomap, q->prio2band, TC_PRIO_MAX + 1);

	err = prio_dump_offload(sch);
	if (err)
		goto nla_put_failure;

	if (nla_put(skb, TCA_OPTIONS, sizeof(opt), &opt))
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int prio_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		      struct Qdisc **old, struct netlink_ext_ack *extack)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	struct tc_prio_qopt_offload graft_offload;
	unsigned long band = arg - 1;

	if (!new) {
		new = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
					TC_H_MAKE(sch->handle, arg), extack);
		if (!new)
			new = &noop_qdisc;
		else
			qdisc_hash_add(new, true);
	}

	*old = qdisc_replace(sch, new, &q->queues[band]);

	graft_offload.handle = sch->handle;
	graft_offload.parent = sch->parent;
	graft_offload.graft_params.band = band;
	graft_offload.graft_params.child_handle = new->handle;
	graft_offload.command = TC_PRIO_GRAFT;

	qdisc_offload_graft_helper(qdisc_dev(sch), sch, new, *old,
				   TC_SETUP_QDISC_PRIO, &graft_offload,
				   extack);
	return 0;
}

static struct Qdisc *
prio_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	unsigned long band = arg - 1;

	return q->queues[band];
}

static unsigned long prio_find(struct Qdisc *sch, u32 classid)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	unsigned long band = TC_H_MIN(classid);

	if (band - 1 >= q->bands)
		return 0;
	return band;
}

static unsigned long prio_bind(struct Qdisc *sch, unsigned long parent, u32 classid)
{
	return prio_find(sch, classid);
}


static void prio_unbind(struct Qdisc *q, unsigned long cl)
{
}

static int prio_dump_class(struct Qdisc *sch, unsigned long cl, struct sk_buff *skb,
			   struct tcmsg *tcm)
{
	struct prio_sched_data *q = qdisc_priv(sch);

	tcm->tcm_handle |= TC_H_MIN(cl);
	tcm->tcm_info = q->queues[cl-1]->handle;
	return 0;
}

static int prio_dump_class_stats(struct Qdisc *sch, unsigned long cl,
				 struct gnet_dump *d)
{
	struct prio_sched_data *q = qdisc_priv(sch);
	struct Qdisc *cl_q;

	cl_q = q->queues[cl - 1];
	if (gnet_stats_copy_basic(d, cl_q->cpu_bstats,
				  &cl_q->bstats, true) < 0 ||
	    qdisc_qstats_copy(d, cl_q) < 0)
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
		if (!tc_qdisc_stats_dump(sch, prio + 1, arg))
			break;
	}
}

static struct tcf_block *prio_tcf_block(struct Qdisc *sch, unsigned long cl,
					struct netlink_ext_ack *extack)
{
	struct prio_sched_data *q = qdisc_priv(sch);

	if (cl)
		return NULL;
	return q->block;
}

static const struct Qdisc_class_ops prio_class_ops = {
	.graft		=	prio_graft,
	.leaf		=	prio_leaf,
	.find		=	prio_find,
	.walk		=	prio_walk,
	.tcf_block	=	prio_tcf_block,
	.bind_tcf	=	prio_bind,
	.unbind_tcf	=	prio_unbind,
	.dump		=	prio_dump_class,
	.dump_stats	=	prio_dump_class_stats,
};

static struct Qdisc_ops prio_qdisc_ops __read_mostly = {
	.next		=	NULL,
	.cl_ops		=	&prio_class_ops,
	.id		=	"prio",
	.priv_size	=	sizeof(struct prio_sched_data),
	.enqueue	=	prio_enqueue,
	.dequeue	=	prio_dequeue,
	.peek		=	prio_peek,
	.init		=	prio_init,
	.reset		=	prio_reset,
	.destroy	=	prio_destroy,
	.change		=	prio_tune,
	.dump		=	prio_dump,
	.owner		=	THIS_MODULE,
};
MODULE_ALIAS_NET_SCH("prio");

static int __init prio_module_init(void)
{
	return register_qdisc(&prio_qdisc_ops);
}

static void __exit prio_module_exit(void)
{
	unregister_qdisc(&prio_qdisc_ops);
}

module_init(prio_module_init)
module_exit(prio_module_exit)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple 3-band priority qdisc");
