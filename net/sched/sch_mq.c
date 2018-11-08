/*
 * net/sched/sch_mq.c		Classful multiqueue dummy scheduler
 *
 * Copyright (c) 2009 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>
#include <net/sch_generic.h>

struct mq_sched {
	struct Qdisc		**qdiscs;
};

static int mq_offload(struct Qdisc *sch, enum tc_mq_command cmd)
{
	struct net_device *dev = qdisc_dev(sch);
	struct tc_mq_qopt_offload opt = {
		.command = cmd,
		.handle = sch->handle,
	};

	if (!tc_can_offload(dev) || !dev->netdev_ops->ndo_setup_tc)
		return -EOPNOTSUPP;

	return dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_QDISC_MQ, &opt);
}

static int mq_offload_stats(struct Qdisc *sch)
{
	struct tc_mq_qopt_offload opt = {
		.command = TC_MQ_STATS,
		.handle = sch->handle,
		.stats = {
			.bstats = &sch->bstats,
			.qstats = &sch->qstats,
		},
	};

	return qdisc_offload_dump_helper(sch, TC_SETUP_QDISC_MQ, &opt);
}

static void mq_destroy(struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);
	struct mq_sched *priv = qdisc_priv(sch);
	unsigned int ntx;

	mq_offload(sch, TC_MQ_DESTROY);

	if (!priv->qdiscs)
		return;
	for (ntx = 0; ntx < dev->num_tx_queues && priv->qdiscs[ntx]; ntx++)
		qdisc_put(priv->qdiscs[ntx]);
	kfree(priv->qdiscs);
}

static int mq_init(struct Qdisc *sch, struct nlattr *opt,
		   struct netlink_ext_ack *extack)
{
	struct net_device *dev = qdisc_dev(sch);
	struct mq_sched *priv = qdisc_priv(sch);
	struct netdev_queue *dev_queue;
	struct Qdisc *qdisc;
	unsigned int ntx;

	if (sch->parent != TC_H_ROOT)
		return -EOPNOTSUPP;

	if (!netif_is_multiqueue(dev))
		return -EOPNOTSUPP;

	/* pre-allocate qdiscs, attachment can't fail */
	priv->qdiscs = kcalloc(dev->num_tx_queues, sizeof(priv->qdiscs[0]),
			       GFP_KERNEL);
	if (!priv->qdiscs)
		return -ENOMEM;

	for (ntx = 0; ntx < dev->num_tx_queues; ntx++) {
		dev_queue = netdev_get_tx_queue(dev, ntx);
		qdisc = qdisc_create_dflt(dev_queue, get_default_qdisc_ops(dev, ntx),
					  TC_H_MAKE(TC_H_MAJ(sch->handle),
						    TC_H_MIN(ntx + 1)),
					  extack);
		if (!qdisc)
			return -ENOMEM;
		priv->qdiscs[ntx] = qdisc;
		qdisc->flags |= TCQ_F_ONETXQUEUE | TCQ_F_NOPARENT;
	}

	sch->flags |= TCQ_F_MQROOT;

	mq_offload(sch, TC_MQ_CREATE);
	return 0;
}

static void mq_attach(struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);
	struct mq_sched *priv = qdisc_priv(sch);
	struct Qdisc *qdisc, *old;
	unsigned int ntx;

	for (ntx = 0; ntx < dev->num_tx_queues; ntx++) {
		qdisc = priv->qdiscs[ntx];
		old = dev_graft_qdisc(qdisc->dev_queue, qdisc);
		if (old)
			qdisc_put(old);
#ifdef CONFIG_NET_SCHED
		if (ntx < dev->real_num_tx_queues)
			qdisc_hash_add(qdisc, false);
#endif

	}
	kfree(priv->qdiscs);
	priv->qdiscs = NULL;
}

static int mq_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct net_device *dev = qdisc_dev(sch);
	struct Qdisc *qdisc;
	unsigned int ntx;
	__u32 qlen = 0;

	sch->q.qlen = 0;
	memset(&sch->bstats, 0, sizeof(sch->bstats));
	memset(&sch->qstats, 0, sizeof(sch->qstats));

	/* MQ supports lockless qdiscs. However, statistics accounting needs
	 * to account for all, none, or a mix of locked and unlocked child
	 * qdiscs. Percpu stats are added to counters in-band and locking
	 * qdisc totals are added at end.
	 */
	for (ntx = 0; ntx < dev->num_tx_queues; ntx++) {
		qdisc = netdev_get_tx_queue(dev, ntx)->qdisc_sleeping;
		spin_lock_bh(qdisc_lock(qdisc));

		if (qdisc_is_percpu_stats(qdisc)) {
			qlen = qdisc_qlen_sum(qdisc);
			__gnet_stats_copy_basic(NULL, &sch->bstats,
						qdisc->cpu_bstats,
						&qdisc->bstats);
			__gnet_stats_copy_queue(&sch->qstats,
						qdisc->cpu_qstats,
						&qdisc->qstats, qlen);
		} else {
			sch->q.qlen		+= qdisc->q.qlen;
			sch->bstats.bytes	+= qdisc->bstats.bytes;
			sch->bstats.packets	+= qdisc->bstats.packets;
			sch->qstats.qlen	+= qdisc->qstats.qlen;
			sch->qstats.backlog	+= qdisc->qstats.backlog;
			sch->qstats.drops	+= qdisc->qstats.drops;
			sch->qstats.requeues	+= qdisc->qstats.requeues;
			sch->qstats.overlimits	+= qdisc->qstats.overlimits;
		}

		spin_unlock_bh(qdisc_lock(qdisc));
	}

	return mq_offload_stats(sch);
}

static struct netdev_queue *mq_queue_get(struct Qdisc *sch, unsigned long cl)
{
	struct net_device *dev = qdisc_dev(sch);
	unsigned long ntx = cl - 1;

	if (ntx >= dev->num_tx_queues)
		return NULL;
	return netdev_get_tx_queue(dev, ntx);
}

static struct netdev_queue *mq_select_queue(struct Qdisc *sch,
					    struct tcmsg *tcm)
{
	return mq_queue_get(sch, TC_H_MIN(tcm->tcm_parent));
}

static int mq_graft(struct Qdisc *sch, unsigned long cl, struct Qdisc *new,
		    struct Qdisc **old, struct netlink_ext_ack *extack)
{
	struct netdev_queue *dev_queue = mq_queue_get(sch, cl);
	struct net_device *dev = qdisc_dev(sch);

	if (dev->flags & IFF_UP)
		dev_deactivate(dev);

	*old = dev_graft_qdisc(dev_queue, new);
	if (new)
		new->flags |= TCQ_F_ONETXQUEUE | TCQ_F_NOPARENT;
	if (dev->flags & IFF_UP)
		dev_activate(dev);
	return 0;
}

static struct Qdisc *mq_leaf(struct Qdisc *sch, unsigned long cl)
{
	struct netdev_queue *dev_queue = mq_queue_get(sch, cl);

	return dev_queue->qdisc_sleeping;
}

static unsigned long mq_find(struct Qdisc *sch, u32 classid)
{
	unsigned int ntx = TC_H_MIN(classid);

	if (!mq_queue_get(sch, ntx))
		return 0;
	return ntx;
}

static int mq_dump_class(struct Qdisc *sch, unsigned long cl,
			 struct sk_buff *skb, struct tcmsg *tcm)
{
	struct netdev_queue *dev_queue = mq_queue_get(sch, cl);

	tcm->tcm_parent = TC_H_ROOT;
	tcm->tcm_handle |= TC_H_MIN(cl);
	tcm->tcm_info = dev_queue->qdisc_sleeping->handle;
	return 0;
}

static int mq_dump_class_stats(struct Qdisc *sch, unsigned long cl,
			       struct gnet_dump *d)
{
	struct netdev_queue *dev_queue = mq_queue_get(sch, cl);

	sch = dev_queue->qdisc_sleeping;
	if (gnet_stats_copy_basic(&sch->running, d, NULL, &sch->bstats) < 0 ||
	    gnet_stats_copy_queue(d, NULL, &sch->qstats, sch->q.qlen) < 0)
		return -1;
	return 0;
}

static void mq_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct net_device *dev = qdisc_dev(sch);
	unsigned int ntx;

	if (arg->stop)
		return;

	arg->count = arg->skip;
	for (ntx = arg->skip; ntx < dev->num_tx_queues; ntx++) {
		if (arg->fn(sch, ntx + 1, arg) < 0) {
			arg->stop = 1;
			break;
		}
		arg->count++;
	}
}

static const struct Qdisc_class_ops mq_class_ops = {
	.select_queue	= mq_select_queue,
	.graft		= mq_graft,
	.leaf		= mq_leaf,
	.find		= mq_find,
	.walk		= mq_walk,
	.dump		= mq_dump_class,
	.dump_stats	= mq_dump_class_stats,
};

struct Qdisc_ops mq_qdisc_ops __read_mostly = {
	.cl_ops		= &mq_class_ops,
	.id		= "mq",
	.priv_size	= sizeof(struct mq_sched),
	.init		= mq_init,
	.destroy	= mq_destroy,
	.attach		= mq_attach,
	.dump		= mq_dump,
	.owner		= THIS_MODULE,
};
