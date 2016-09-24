/*
 * net/sched/sch_generic.c	Generic packet scheduler routines.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *              Jamal Hadi Salim, <hadi@cyberus.ca> 990601
 *              - Ingress support
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/if_vlan.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/dst.h>

/* Qdisc to use by default */
const struct Qdisc_ops *default_qdisc_ops = &pfifo_fast_ops;
EXPORT_SYMBOL(default_qdisc_ops);

/* Main transmission queue. */

/* Modifications to data participating in scheduling must be protected with
 * qdisc_lock(qdisc) spinlock.
 *
 * The idea is the following:
 * - enqueue, dequeue are serialized via qdisc root lock
 * - ingress filtering is also serialized via qdisc root lock
 * - updates to tree and tree walking are only done under the rtnl mutex.
 */

static inline int dev_requeue_skb(struct sk_buff *skb, struct Qdisc *q)
{
	q->gso_skb = skb;
	q->qstats.requeues++;
	qdisc_qstats_backlog_inc(q, skb);
	q->q.qlen++;	/* it's still part of the queue */
	__netif_schedule(q);

	return 0;
}

static void try_bulk_dequeue_skb(struct Qdisc *q,
				 struct sk_buff *skb,
				 const struct netdev_queue *txq,
				 int *packets)
{
	int bytelimit = qdisc_avail_bulklimit(txq) - skb->len;

	while (bytelimit > 0) {
		struct sk_buff *nskb = q->dequeue(q);

		if (!nskb)
			break;

		bytelimit -= nskb->len; /* covers GSO len */
		skb->next = nskb;
		skb = nskb;
		(*packets)++; /* GSO counts as one pkt */
	}
	skb->next = NULL;
}

/* This variant of try_bulk_dequeue_skb() makes sure
 * all skbs in the chain are for the same txq
 */
static void try_bulk_dequeue_skb_slow(struct Qdisc *q,
				      struct sk_buff *skb,
				      int *packets)
{
	int mapping = skb_get_queue_mapping(skb);
	struct sk_buff *nskb;
	int cnt = 0;

	do {
		nskb = q->dequeue(q);
		if (!nskb)
			break;
		if (unlikely(skb_get_queue_mapping(nskb) != mapping)) {
			q->skb_bad_txq = nskb;
			qdisc_qstats_backlog_inc(q, nskb);
			q->q.qlen++;
			break;
		}
		skb->next = nskb;
		skb = nskb;
	} while (++cnt < 8);
	(*packets) += cnt;
	skb->next = NULL;
}

/* Note that dequeue_skb can possibly return a SKB list (via skb->next).
 * A requeued skb (via q->gso_skb) can also be a SKB list.
 */
static struct sk_buff *dequeue_skb(struct Qdisc *q, bool *validate,
				   int *packets)
{
	struct sk_buff *skb = q->gso_skb;
	const struct netdev_queue *txq = q->dev_queue;

	*packets = 1;
	if (unlikely(skb)) {
		/* skb in gso_skb were already validated */
		*validate = false;
		/* check the reason of requeuing without tx lock first */
		txq = skb_get_tx_queue(txq->dev, skb);
		if (!netif_xmit_frozen_or_stopped(txq)) {
			q->gso_skb = NULL;
			qdisc_qstats_backlog_dec(q, skb);
			q->q.qlen--;
		} else
			skb = NULL;
		return skb;
	}
	*validate = true;
	skb = q->skb_bad_txq;
	if (unlikely(skb)) {
		/* check the reason of requeuing without tx lock first */
		txq = skb_get_tx_queue(txq->dev, skb);
		if (!netif_xmit_frozen_or_stopped(txq)) {
			q->skb_bad_txq = NULL;
			qdisc_qstats_backlog_dec(q, skb);
			q->q.qlen--;
			goto bulk;
		}
		return NULL;
	}
	if (!(q->flags & TCQ_F_ONETXQUEUE) ||
	    !netif_xmit_frozen_or_stopped(txq))
		skb = q->dequeue(q);
	if (skb) {
bulk:
		if (qdisc_may_bulk(q))
			try_bulk_dequeue_skb(q, skb, txq, packets);
		else
			try_bulk_dequeue_skb_slow(q, skb, packets);
	}
	return skb;
}

/*
 * Transmit possibly several skbs, and handle the return status as
 * required. Owning running seqcount bit guarantees that
 * only one CPU can execute this function.
 *
 * Returns to the caller:
 *				0  - queue is empty or throttled.
 *				>0 - queue is not empty.
 */
int sch_direct_xmit(struct sk_buff *skb, struct Qdisc *q,
		    struct net_device *dev, struct netdev_queue *txq,
		    spinlock_t *root_lock, bool validate)
{
	int ret = NETDEV_TX_BUSY;

	/* And release qdisc */
	spin_unlock(root_lock);

	/* Note that we validate skb (GSO, checksum, ...) outside of locks */
	if (validate)
		skb = validate_xmit_skb_list(skb, dev);

	if (likely(skb)) {
		HARD_TX_LOCK(dev, txq, smp_processor_id());
		if (!netif_xmit_frozen_or_stopped(txq))
			skb = dev_hard_start_xmit(skb, dev, txq, &ret);

		HARD_TX_UNLOCK(dev, txq);
	} else {
		spin_lock(root_lock);
		return qdisc_qlen(q);
	}
	spin_lock(root_lock);

	if (dev_xmit_complete(ret)) {
		/* Driver sent out skb successfully or skb was consumed */
		ret = qdisc_qlen(q);
	} else {
		/* Driver returned NETDEV_TX_BUSY - requeue skb */
		if (unlikely(ret != NETDEV_TX_BUSY))
			net_warn_ratelimited("BUG %s code %d qlen %d\n",
					     dev->name, ret, q->q.qlen);

		ret = dev_requeue_skb(skb, q);
	}

	if (ret && netif_xmit_frozen_or_stopped(txq))
		ret = 0;

	return ret;
}

/*
 * NOTE: Called under qdisc_lock(q) with locally disabled BH.
 *
 * running seqcount guarantees only one CPU can process
 * this qdisc at a time. qdisc_lock(q) serializes queue accesses for
 * this queue.
 *
 *  netif_tx_lock serializes accesses to device driver.
 *
 *  qdisc_lock(q) and netif_tx_lock are mutually exclusive,
 *  if one is grabbed, another must be free.
 *
 * Note, that this procedure can be called by a watchdog timer
 *
 * Returns to the caller:
 *				0  - queue is empty or throttled.
 *				>0 - queue is not empty.
 *
 */
static inline int qdisc_restart(struct Qdisc *q, int *packets)
{
	struct netdev_queue *txq;
	struct net_device *dev;
	spinlock_t *root_lock;
	struct sk_buff *skb;
	bool validate;

	/* Dequeue packet */
	skb = dequeue_skb(q, &validate, packets);
	if (unlikely(!skb))
		return 0;

	root_lock = qdisc_lock(q);
	dev = qdisc_dev(q);
	txq = skb_get_tx_queue(dev, skb);

	return sch_direct_xmit(skb, q, dev, txq, root_lock, validate);
}

void __qdisc_run(struct Qdisc *q)
{
	int quota = weight_p;
	int packets;

	while (qdisc_restart(q, &packets)) {
		/*
		 * Ordered by possible occurrence: Postpone processing if
		 * 1. we've exceeded packet quota
		 * 2. another process needs the CPU;
		 */
		quota -= packets;
		if (quota <= 0 || need_resched()) {
			__netif_schedule(q);
			break;
		}
	}

	qdisc_run_end(q);
}

unsigned long dev_trans_start(struct net_device *dev)
{
	unsigned long val, res;
	unsigned int i;

	if (is_vlan_dev(dev))
		dev = vlan_dev_real_dev(dev);
	res = netdev_get_tx_queue(dev, 0)->trans_start;
	for (i = 1; i < dev->num_tx_queues; i++) {
		val = netdev_get_tx_queue(dev, i)->trans_start;
		if (val && time_after(val, res))
			res = val;
	}

	return res;
}
EXPORT_SYMBOL(dev_trans_start);

static void dev_watchdog(unsigned long arg)
{
	struct net_device *dev = (struct net_device *)arg;

	netif_tx_lock(dev);
	if (!qdisc_tx_is_noop(dev)) {
		if (netif_device_present(dev) &&
		    netif_running(dev) &&
		    netif_carrier_ok(dev)) {
			int some_queue_timedout = 0;
			unsigned int i;
			unsigned long trans_start;

			for (i = 0; i < dev->num_tx_queues; i++) {
				struct netdev_queue *txq;

				txq = netdev_get_tx_queue(dev, i);
				trans_start = txq->trans_start;
				if (netif_xmit_stopped(txq) &&
				    time_after(jiffies, (trans_start +
							 dev->watchdog_timeo))) {
					some_queue_timedout = 1;
					txq->trans_timeout++;
					break;
				}
			}

			if (some_queue_timedout) {
				WARN_ONCE(1, KERN_INFO "NETDEV WATCHDOG: %s (%s): transmit queue %u timed out\n",
				       dev->name, netdev_drivername(dev), i);
				dev->netdev_ops->ndo_tx_timeout(dev);
			}
			if (!mod_timer(&dev->watchdog_timer,
				       round_jiffies(jiffies +
						     dev->watchdog_timeo)))
				dev_hold(dev);
		}
	}
	netif_tx_unlock(dev);

	dev_put(dev);
}

void __netdev_watchdog_up(struct net_device *dev)
{
	if (dev->netdev_ops->ndo_tx_timeout) {
		if (dev->watchdog_timeo <= 0)
			dev->watchdog_timeo = 5*HZ;
		if (!mod_timer(&dev->watchdog_timer,
			       round_jiffies(jiffies + dev->watchdog_timeo)))
			dev_hold(dev);
	}
}

static void dev_watchdog_up(struct net_device *dev)
{
	__netdev_watchdog_up(dev);
}

static void dev_watchdog_down(struct net_device *dev)
{
	netif_tx_lock_bh(dev);
	if (del_timer(&dev->watchdog_timer))
		dev_put(dev);
	netif_tx_unlock_bh(dev);
}

/**
 *	netif_carrier_on - set carrier
 *	@dev: network device
 *
 * Device has detected that carrier.
 */
void netif_carrier_on(struct net_device *dev)
{
	if (test_and_clear_bit(__LINK_STATE_NOCARRIER, &dev->state)) {
		if (dev->reg_state == NETREG_UNINITIALIZED)
			return;
		atomic_inc(&dev->carrier_changes);
		linkwatch_fire_event(dev);
		if (netif_running(dev))
			__netdev_watchdog_up(dev);
	}
}
EXPORT_SYMBOL(netif_carrier_on);

/**
 *	netif_carrier_off - clear carrier
 *	@dev: network device
 *
 * Device has detected loss of carrier.
 */
void netif_carrier_off(struct net_device *dev)
{
	if (!test_and_set_bit(__LINK_STATE_NOCARRIER, &dev->state)) {
		if (dev->reg_state == NETREG_UNINITIALIZED)
			return;
		atomic_inc(&dev->carrier_changes);
		linkwatch_fire_event(dev);
	}
}
EXPORT_SYMBOL(netif_carrier_off);

/* "NOOP" scheduler: the best scheduler, recommended for all interfaces
   under all circumstances. It is difficult to invent anything faster or
   cheaper.
 */

static int noop_enqueue(struct sk_buff *skb, struct Qdisc *qdisc,
			struct sk_buff **to_free)
{
	__qdisc_drop(skb, to_free);
	return NET_XMIT_CN;
}

static struct sk_buff *noop_dequeue(struct Qdisc *qdisc)
{
	return NULL;
}

struct Qdisc_ops noop_qdisc_ops __read_mostly = {
	.id		=	"noop",
	.priv_size	=	0,
	.enqueue	=	noop_enqueue,
	.dequeue	=	noop_dequeue,
	.peek		=	noop_dequeue,
	.owner		=	THIS_MODULE,
};

static struct netdev_queue noop_netdev_queue = {
	.qdisc		=	&noop_qdisc,
	.qdisc_sleeping	=	&noop_qdisc,
};

struct Qdisc noop_qdisc = {
	.enqueue	=	noop_enqueue,
	.dequeue	=	noop_dequeue,
	.flags		=	TCQ_F_BUILTIN,
	.ops		=	&noop_qdisc_ops,
	.list		=	LIST_HEAD_INIT(noop_qdisc.list),
	.q.lock		=	__SPIN_LOCK_UNLOCKED(noop_qdisc.q.lock),
	.dev_queue	=	&noop_netdev_queue,
	.running	=	SEQCNT_ZERO(noop_qdisc.running),
	.busylock	=	__SPIN_LOCK_UNLOCKED(noop_qdisc.busylock),
};
EXPORT_SYMBOL(noop_qdisc);

static int noqueue_init(struct Qdisc *qdisc, struct nlattr *opt)
{
	/* register_qdisc() assigns a default of noop_enqueue if unset,
	 * but __dev_queue_xmit() treats noqueue only as such
	 * if this is NULL - so clear it here. */
	qdisc->enqueue = NULL;
	return 0;
}

struct Qdisc_ops noqueue_qdisc_ops __read_mostly = {
	.id		=	"noqueue",
	.priv_size	=	0,
	.init		=	noqueue_init,
	.enqueue	=	noop_enqueue,
	.dequeue	=	noop_dequeue,
	.peek		=	noop_dequeue,
	.owner		=	THIS_MODULE,
};

static const u8 prio2band[TC_PRIO_MAX + 1] = {
	1, 2, 2, 2, 1, 2, 0, 0 , 1, 1, 1, 1, 1, 1, 1, 1
};

/* 3-band FIFO queue: old style, but should be a bit faster than
   generic prio+fifo combination.
 */

#define PFIFO_FAST_BANDS 3

/*
 * Private data for a pfifo_fast scheduler containing:
 * 	- queues for the three band
 * 	- bitmap indicating which of the bands contain skbs
 */
struct pfifo_fast_priv {
	u32 bitmap;
	struct sk_buff_head q[PFIFO_FAST_BANDS];
};

/*
 * Convert a bitmap to the first band number where an skb is queued, where:
 * 	bitmap=0 means there are no skbs on any band.
 * 	bitmap=1 means there is an skb on band 0.
 *	bitmap=7 means there are skbs on all 3 bands, etc.
 */
static const int bitmap2band[] = {-1, 0, 1, 0, 2, 0, 1, 0};

static inline struct sk_buff_head *band2list(struct pfifo_fast_priv *priv,
					     int band)
{
	return priv->q + band;
}

static int pfifo_fast_enqueue(struct sk_buff *skb, struct Qdisc *qdisc,
			      struct sk_buff **to_free)
{
	if (skb_queue_len(&qdisc->q) < qdisc_dev(qdisc)->tx_queue_len) {
		int band = prio2band[skb->priority & TC_PRIO_MAX];
		struct pfifo_fast_priv *priv = qdisc_priv(qdisc);
		struct sk_buff_head *list = band2list(priv, band);

		priv->bitmap |= (1 << band);
		qdisc->q.qlen++;
		return __qdisc_enqueue_tail(skb, qdisc, list);
	}

	return qdisc_drop(skb, qdisc, to_free);
}

static struct sk_buff *pfifo_fast_dequeue(struct Qdisc *qdisc)
{
	struct pfifo_fast_priv *priv = qdisc_priv(qdisc);
	int band = bitmap2band[priv->bitmap];

	if (likely(band >= 0)) {
		struct sk_buff_head *list = band2list(priv, band);
		struct sk_buff *skb = __qdisc_dequeue_head(qdisc, list);

		qdisc->q.qlen--;
		if (skb_queue_empty(list))
			priv->bitmap &= ~(1 << band);

		return skb;
	}

	return NULL;
}

static struct sk_buff *pfifo_fast_peek(struct Qdisc *qdisc)
{
	struct pfifo_fast_priv *priv = qdisc_priv(qdisc);
	int band = bitmap2band[priv->bitmap];

	if (band >= 0) {
		struct sk_buff_head *list = band2list(priv, band);

		return skb_peek(list);
	}

	return NULL;
}

static void pfifo_fast_reset(struct Qdisc *qdisc)
{
	int prio;
	struct pfifo_fast_priv *priv = qdisc_priv(qdisc);

	for (prio = 0; prio < PFIFO_FAST_BANDS; prio++)
		__qdisc_reset_queue(band2list(priv, prio));

	priv->bitmap = 0;
	qdisc->qstats.backlog = 0;
	qdisc->q.qlen = 0;
}

static int pfifo_fast_dump(struct Qdisc *qdisc, struct sk_buff *skb)
{
	struct tc_prio_qopt opt = { .bands = PFIFO_FAST_BANDS };

	memcpy(&opt.priomap, prio2band, TC_PRIO_MAX + 1);
	if (nla_put(skb, TCA_OPTIONS, sizeof(opt), &opt))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	return -1;
}

static int pfifo_fast_init(struct Qdisc *qdisc, struct nlattr *opt)
{
	int prio;
	struct pfifo_fast_priv *priv = qdisc_priv(qdisc);

	for (prio = 0; prio < PFIFO_FAST_BANDS; prio++)
		__skb_queue_head_init(band2list(priv, prio));

	/* Can by-pass the queue discipline */
	qdisc->flags |= TCQ_F_CAN_BYPASS;
	return 0;
}

struct Qdisc_ops pfifo_fast_ops __read_mostly = {
	.id		=	"pfifo_fast",
	.priv_size	=	sizeof(struct pfifo_fast_priv),
	.enqueue	=	pfifo_fast_enqueue,
	.dequeue	=	pfifo_fast_dequeue,
	.peek		=	pfifo_fast_peek,
	.init		=	pfifo_fast_init,
	.reset		=	pfifo_fast_reset,
	.dump		=	pfifo_fast_dump,
	.owner		=	THIS_MODULE,
};
EXPORT_SYMBOL(pfifo_fast_ops);

static struct lock_class_key qdisc_tx_busylock;
static struct lock_class_key qdisc_running_key;

struct Qdisc *qdisc_alloc(struct netdev_queue *dev_queue,
			  const struct Qdisc_ops *ops)
{
	void *p;
	struct Qdisc *sch;
	unsigned int size = QDISC_ALIGN(sizeof(*sch)) + ops->priv_size;
	int err = -ENOBUFS;
	struct net_device *dev = dev_queue->dev;

	p = kzalloc_node(size, GFP_KERNEL,
			 netdev_queue_numa_node_read(dev_queue));

	if (!p)
		goto errout;
	sch = (struct Qdisc *) QDISC_ALIGN((unsigned long) p);
	/* if we got non aligned memory, ask more and do alignment ourself */
	if (sch != p) {
		kfree(p);
		p = kzalloc_node(size + QDISC_ALIGNTO - 1, GFP_KERNEL,
				 netdev_queue_numa_node_read(dev_queue));
		if (!p)
			goto errout;
		sch = (struct Qdisc *) QDISC_ALIGN((unsigned long) p);
		sch->padded = (char *) sch - (char *) p;
	}
	INIT_LIST_HEAD(&sch->list);
	skb_queue_head_init(&sch->q);

	spin_lock_init(&sch->busylock);
	lockdep_set_class(&sch->busylock,
			  dev->qdisc_tx_busylock ?: &qdisc_tx_busylock);

	seqcount_init(&sch->running);
	lockdep_set_class(&sch->running,
			  dev->qdisc_running_key ?: &qdisc_running_key);

	sch->ops = ops;
	sch->enqueue = ops->enqueue;
	sch->dequeue = ops->dequeue;
	sch->dev_queue = dev_queue;
	dev_hold(dev);
	atomic_set(&sch->refcnt, 1);

	return sch;
errout:
	return ERR_PTR(err);
}

struct Qdisc *qdisc_create_dflt(struct netdev_queue *dev_queue,
				const struct Qdisc_ops *ops,
				unsigned int parentid)
{
	struct Qdisc *sch;

	if (!try_module_get(ops->owner))
		return NULL;

	sch = qdisc_alloc(dev_queue, ops);
	if (IS_ERR(sch)) {
		module_put(ops->owner);
		return NULL;
	}
	sch->parent = parentid;

	if (!ops->init || ops->init(sch, NULL) == 0)
		return sch;

	qdisc_destroy(sch);
	return NULL;
}
EXPORT_SYMBOL(qdisc_create_dflt);

/* Under qdisc_lock(qdisc) and BH! */

void qdisc_reset(struct Qdisc *qdisc)
{
	const struct Qdisc_ops *ops = qdisc->ops;

	if (ops->reset)
		ops->reset(qdisc);

	kfree_skb(qdisc->skb_bad_txq);
	qdisc->skb_bad_txq = NULL;

	if (qdisc->gso_skb) {
		kfree_skb_list(qdisc->gso_skb);
		qdisc->gso_skb = NULL;
	}
	qdisc->q.qlen = 0;
}
EXPORT_SYMBOL(qdisc_reset);

static void qdisc_rcu_free(struct rcu_head *head)
{
	struct Qdisc *qdisc = container_of(head, struct Qdisc, rcu_head);

	if (qdisc_is_percpu_stats(qdisc)) {
		free_percpu(qdisc->cpu_bstats);
		free_percpu(qdisc->cpu_qstats);
	}

	kfree((char *) qdisc - qdisc->padded);
}

void qdisc_destroy(struct Qdisc *qdisc)
{
	const struct Qdisc_ops  *ops = qdisc->ops;

	if (qdisc->flags & TCQ_F_BUILTIN ||
	    !atomic_dec_and_test(&qdisc->refcnt))
		return;

#ifdef CONFIG_NET_SCHED
	qdisc_list_del(qdisc);

	qdisc_put_stab(rtnl_dereference(qdisc->stab));
#endif
	gen_kill_estimator(&qdisc->bstats, &qdisc->rate_est);
	if (ops->reset)
		ops->reset(qdisc);
	if (ops->destroy)
		ops->destroy(qdisc);

	module_put(ops->owner);
	dev_put(qdisc_dev(qdisc));

	kfree_skb_list(qdisc->gso_skb);
	kfree_skb(qdisc->skb_bad_txq);
	/*
	 * gen_estimator est_timer() might access qdisc->q.lock,
	 * wait a RCU grace period before freeing qdisc.
	 */
	call_rcu(&qdisc->rcu_head, qdisc_rcu_free);
}
EXPORT_SYMBOL(qdisc_destroy);

/* Attach toplevel qdisc to device queue. */
struct Qdisc *dev_graft_qdisc(struct netdev_queue *dev_queue,
			      struct Qdisc *qdisc)
{
	struct Qdisc *oqdisc = dev_queue->qdisc_sleeping;
	spinlock_t *root_lock;

	root_lock = qdisc_lock(oqdisc);
	spin_lock_bh(root_lock);

	/* Prune old scheduler */
	if (oqdisc && atomic_read(&oqdisc->refcnt) <= 1)
		qdisc_reset(oqdisc);

	/* ... and graft new one */
	if (qdisc == NULL)
		qdisc = &noop_qdisc;
	dev_queue->qdisc_sleeping = qdisc;
	rcu_assign_pointer(dev_queue->qdisc, &noop_qdisc);

	spin_unlock_bh(root_lock);

	return oqdisc;
}
EXPORT_SYMBOL(dev_graft_qdisc);

static void attach_one_default_qdisc(struct net_device *dev,
				     struct netdev_queue *dev_queue,
				     void *_unused)
{
	struct Qdisc *qdisc;
	const struct Qdisc_ops *ops = default_qdisc_ops;

	if (dev->priv_flags & IFF_NO_QUEUE)
		ops = &noqueue_qdisc_ops;

	qdisc = qdisc_create_dflt(dev_queue, ops, TC_H_ROOT);
	if (!qdisc) {
		netdev_info(dev, "activation failed\n");
		return;
	}
	if (!netif_is_multiqueue(dev))
		qdisc->flags |= TCQ_F_ONETXQUEUE | TCQ_F_NOPARENT;
	dev_queue->qdisc_sleeping = qdisc;
}

static void attach_default_qdiscs(struct net_device *dev)
{
	struct netdev_queue *txq;
	struct Qdisc *qdisc;

	txq = netdev_get_tx_queue(dev, 0);

	if (!netif_is_multiqueue(dev) ||
	    dev->priv_flags & IFF_NO_QUEUE) {
		netdev_for_each_tx_queue(dev, attach_one_default_qdisc, NULL);
		dev->qdisc = txq->qdisc_sleeping;
		atomic_inc(&dev->qdisc->refcnt);
	} else {
		qdisc = qdisc_create_dflt(txq, &mq_qdisc_ops, TC_H_ROOT);
		if (qdisc) {
			dev->qdisc = qdisc;
			qdisc->ops->attach(qdisc);
		}
	}
}

static void transition_one_qdisc(struct net_device *dev,
				 struct netdev_queue *dev_queue,
				 void *_need_watchdog)
{
	struct Qdisc *new_qdisc = dev_queue->qdisc_sleeping;
	int *need_watchdog_p = _need_watchdog;

	if (!(new_qdisc->flags & TCQ_F_BUILTIN))
		clear_bit(__QDISC_STATE_DEACTIVATED, &new_qdisc->state);

	rcu_assign_pointer(dev_queue->qdisc, new_qdisc);
	if (need_watchdog_p) {
		dev_queue->trans_start = 0;
		*need_watchdog_p = 1;
	}
}

void dev_activate(struct net_device *dev)
{
	int need_watchdog;

	/* No queueing discipline is attached to device;
	 * create default one for devices, which need queueing
	 * and noqueue_qdisc for virtual interfaces
	 */

	if (dev->qdisc == &noop_qdisc)
		attach_default_qdiscs(dev);

	if (!netif_carrier_ok(dev))
		/* Delay activation until next carrier-on event */
		return;

	need_watchdog = 0;
	netdev_for_each_tx_queue(dev, transition_one_qdisc, &need_watchdog);
	if (dev_ingress_queue(dev))
		transition_one_qdisc(dev, dev_ingress_queue(dev), NULL);

	if (need_watchdog) {
		netif_trans_update(dev);
		dev_watchdog_up(dev);
	}
}
EXPORT_SYMBOL(dev_activate);

static void dev_deactivate_queue(struct net_device *dev,
				 struct netdev_queue *dev_queue,
				 void *_qdisc_default)
{
	struct Qdisc *qdisc_default = _qdisc_default;
	struct Qdisc *qdisc;

	qdisc = rtnl_dereference(dev_queue->qdisc);
	if (qdisc) {
		spin_lock_bh(qdisc_lock(qdisc));

		if (!(qdisc->flags & TCQ_F_BUILTIN))
			set_bit(__QDISC_STATE_DEACTIVATED, &qdisc->state);

		rcu_assign_pointer(dev_queue->qdisc, qdisc_default);
		qdisc_reset(qdisc);

		spin_unlock_bh(qdisc_lock(qdisc));
	}
}

static bool some_qdisc_is_busy(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *dev_queue;
		spinlock_t *root_lock;
		struct Qdisc *q;
		int val;

		dev_queue = netdev_get_tx_queue(dev, i);
		q = dev_queue->qdisc_sleeping;
		root_lock = qdisc_lock(q);

		spin_lock_bh(root_lock);

		val = (qdisc_is_running(q) ||
		       test_bit(__QDISC_STATE_SCHED, &q->state));

		spin_unlock_bh(root_lock);

		if (val)
			return true;
	}
	return false;
}

/**
 * 	dev_deactivate_many - deactivate transmissions on several devices
 * 	@head: list of devices to deactivate
 *
 *	This function returns only when all outstanding transmissions
 *	have completed, unless all devices are in dismantle phase.
 */
void dev_deactivate_many(struct list_head *head)
{
	struct net_device *dev;
	bool sync_needed = false;

	list_for_each_entry(dev, head, close_list) {
		netdev_for_each_tx_queue(dev, dev_deactivate_queue,
					 &noop_qdisc);
		if (dev_ingress_queue(dev))
			dev_deactivate_queue(dev, dev_ingress_queue(dev),
					     &noop_qdisc);

		dev_watchdog_down(dev);
		sync_needed |= !dev->dismantle;
	}

	/* Wait for outstanding qdisc-less dev_queue_xmit calls.
	 * This is avoided if all devices are in dismantle phase :
	 * Caller will call synchronize_net() for us
	 */
	if (sync_needed)
		synchronize_net();

	/* Wait for outstanding qdisc_run calls. */
	list_for_each_entry(dev, head, close_list)
		while (some_qdisc_is_busy(dev))
			yield();
}

void dev_deactivate(struct net_device *dev)
{
	LIST_HEAD(single);

	list_add(&dev->close_list, &single);
	dev_deactivate_many(&single);
	list_del(&single);
}
EXPORT_SYMBOL(dev_deactivate);

static void dev_init_scheduler_queue(struct net_device *dev,
				     struct netdev_queue *dev_queue,
				     void *_qdisc)
{
	struct Qdisc *qdisc = _qdisc;

	rcu_assign_pointer(dev_queue->qdisc, qdisc);
	dev_queue->qdisc_sleeping = qdisc;
}

void dev_init_scheduler(struct net_device *dev)
{
	dev->qdisc = &noop_qdisc;
	netdev_for_each_tx_queue(dev, dev_init_scheduler_queue, &noop_qdisc);
	if (dev_ingress_queue(dev))
		dev_init_scheduler_queue(dev, dev_ingress_queue(dev), &noop_qdisc);

	setup_timer(&dev->watchdog_timer, dev_watchdog, (unsigned long)dev);
}

static void shutdown_scheduler_queue(struct net_device *dev,
				     struct netdev_queue *dev_queue,
				     void *_qdisc_default)
{
	struct Qdisc *qdisc = dev_queue->qdisc_sleeping;
	struct Qdisc *qdisc_default = _qdisc_default;

	if (qdisc) {
		rcu_assign_pointer(dev_queue->qdisc, qdisc_default);
		dev_queue->qdisc_sleeping = qdisc_default;

		qdisc_destroy(qdisc);
	}
}

void dev_shutdown(struct net_device *dev)
{
	netdev_for_each_tx_queue(dev, shutdown_scheduler_queue, &noop_qdisc);
	if (dev_ingress_queue(dev))
		shutdown_scheduler_queue(dev, dev_ingress_queue(dev), &noop_qdisc);
	qdisc_destroy(dev->qdisc);
	dev->qdisc = &noop_qdisc;

	WARN_ON(timer_pending(&dev->watchdog_timer));
}

void psched_ratecfg_precompute(struct psched_ratecfg *r,
			       const struct tc_ratespec *conf,
			       u64 rate64)
{
	memset(r, 0, sizeof(*r));
	r->overhead = conf->overhead;
	r->rate_bytes_ps = max_t(u64, conf->rate, rate64);
	r->linklayer = (conf->linklayer & TC_LINKLAYER_MASK);
	r->mult = 1;
	/*
	 * The deal here is to replace a divide by a reciprocal one
	 * in fast path (a reciprocal divide is a multiply and a shift)
	 *
	 * Normal formula would be :
	 *  time_in_ns = (NSEC_PER_SEC * len) / rate_bps
	 *
	 * We compute mult/shift to use instead :
	 *  time_in_ns = (len * mult) >> shift;
	 *
	 * We try to get the highest possible mult value for accuracy,
	 * but have to make sure no overflows will ever happen.
	 */
	if (r->rate_bytes_ps > 0) {
		u64 factor = NSEC_PER_SEC;

		for (;;) {
			r->mult = div64_u64(factor, r->rate_bytes_ps);
			if (r->mult & (1U << 31) || factor & (1ULL << 63))
				break;
			factor <<= 1;
			r->shift++;
		}
	}
}
EXPORT_SYMBOL(psched_ratecfg_precompute);
