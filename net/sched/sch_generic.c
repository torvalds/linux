// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/sch_generic.c	Generic packet scheduler routines.
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
#include <linux/skb_array.h>
#include <linux/if_macvlan.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/dst.h>
#include <trace/events/qdisc.h>
#include <trace/events/net.h>
#include <net/xfrm.h>

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

#define SKB_XOFF_MAGIC ((struct sk_buff *)1UL)

static inline struct sk_buff *__skb_dequeue_bad_txq(struct Qdisc *q)
{
	const struct netdev_queue *txq = q->dev_queue;
	spinlock_t *lock = NULL;
	struct sk_buff *skb;

	if (q->flags & TCQ_F_NOLOCK) {
		lock = qdisc_lock(q);
		spin_lock(lock);
	}

	skb = skb_peek(&q->skb_bad_txq);
	if (skb) {
		/* check the reason of requeuing without tx lock first */
		txq = skb_get_tx_queue(txq->dev, skb);
		if (!netif_xmit_frozen_or_stopped(txq)) {
			skb = __skb_dequeue(&q->skb_bad_txq);
			if (qdisc_is_percpu_stats(q)) {
				qdisc_qstats_cpu_backlog_dec(q, skb);
				qdisc_qstats_cpu_qlen_dec(q);
			} else {
				qdisc_qstats_backlog_dec(q, skb);
				q->q.qlen--;
			}
		} else {
			skb = SKB_XOFF_MAGIC;
		}
	}

	if (lock)
		spin_unlock(lock);

	return skb;
}

static inline struct sk_buff *qdisc_dequeue_skb_bad_txq(struct Qdisc *q)
{
	struct sk_buff *skb = skb_peek(&q->skb_bad_txq);

	if (unlikely(skb))
		skb = __skb_dequeue_bad_txq(q);

	return skb;
}

static inline void qdisc_enqueue_skb_bad_txq(struct Qdisc *q,
					     struct sk_buff *skb)
{
	spinlock_t *lock = NULL;

	if (q->flags & TCQ_F_NOLOCK) {
		lock = qdisc_lock(q);
		spin_lock(lock);
	}

	__skb_queue_tail(&q->skb_bad_txq, skb);

	if (qdisc_is_percpu_stats(q)) {
		qdisc_qstats_cpu_backlog_inc(q, skb);
		qdisc_qstats_cpu_qlen_inc(q);
	} else {
		qdisc_qstats_backlog_inc(q, skb);
		q->q.qlen++;
	}

	if (lock)
		spin_unlock(lock);
}

static inline void dev_requeue_skb(struct sk_buff *skb, struct Qdisc *q)
{
	spinlock_t *lock = NULL;

	if (q->flags & TCQ_F_NOLOCK) {
		lock = qdisc_lock(q);
		spin_lock(lock);
	}

	while (skb) {
		struct sk_buff *next = skb->next;

		__skb_queue_tail(&q->gso_skb, skb);

		/* it's still part of the queue */
		if (qdisc_is_percpu_stats(q)) {
			qdisc_qstats_cpu_requeues_inc(q);
			qdisc_qstats_cpu_backlog_inc(q, skb);
			qdisc_qstats_cpu_qlen_inc(q);
		} else {
			q->qstats.requeues++;
			qdisc_qstats_backlog_inc(q, skb);
			q->q.qlen++;
		}

		skb = next;
	}
	if (lock)
		spin_unlock(lock);
	__netif_schedule(q);
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
	skb_mark_not_on_list(skb);
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
			qdisc_enqueue_skb_bad_txq(q, nskb);
			break;
		}
		skb->next = nskb;
		skb = nskb;
	} while (++cnt < 8);
	(*packets) += cnt;
	skb_mark_not_on_list(skb);
}

/* Note that dequeue_skb can possibly return a SKB list (via skb->next).
 * A requeued skb (via q->gso_skb) can also be a SKB list.
 */
static struct sk_buff *dequeue_skb(struct Qdisc *q, bool *validate,
				   int *packets)
{
	const struct netdev_queue *txq = q->dev_queue;
	struct sk_buff *skb = NULL;

	*packets = 1;
	if (unlikely(!skb_queue_empty(&q->gso_skb))) {
		spinlock_t *lock = NULL;

		if (q->flags & TCQ_F_NOLOCK) {
			lock = qdisc_lock(q);
			spin_lock(lock);
		}

		skb = skb_peek(&q->gso_skb);

		/* skb may be null if another cpu pulls gso_skb off in between
		 * empty check and lock.
		 */
		if (!skb) {
			if (lock)
				spin_unlock(lock);
			goto validate;
		}

		/* skb in gso_skb were already validated */
		*validate = false;
		if (xfrm_offload(skb))
			*validate = true;
		/* check the reason of requeuing without tx lock first */
		txq = skb_get_tx_queue(txq->dev, skb);
		if (!netif_xmit_frozen_or_stopped(txq)) {
			skb = __skb_dequeue(&q->gso_skb);
			if (qdisc_is_percpu_stats(q)) {
				qdisc_qstats_cpu_backlog_dec(q, skb);
				qdisc_qstats_cpu_qlen_dec(q);
			} else {
				qdisc_qstats_backlog_dec(q, skb);
				q->q.qlen--;
			}
		} else {
			skb = NULL;
		}
		if (lock)
			spin_unlock(lock);
		goto trace;
	}
validate:
	*validate = true;

	if ((q->flags & TCQ_F_ONETXQUEUE) &&
	    netif_xmit_frozen_or_stopped(txq))
		return skb;

	skb = qdisc_dequeue_skb_bad_txq(q);
	if (unlikely(skb)) {
		if (skb == SKB_XOFF_MAGIC)
			return NULL;
		goto bulk;
	}
	skb = q->dequeue(q);
	if (skb) {
bulk:
		if (qdisc_may_bulk(q))
			try_bulk_dequeue_skb(q, skb, txq, packets);
		else
			try_bulk_dequeue_skb_slow(q, skb, packets);
	}
trace:
	trace_qdisc_dequeue(q, txq, *packets, skb);
	return skb;
}

/*
 * Transmit possibly several skbs, and handle the return status as
 * required. Owning running seqcount bit guarantees that
 * only one CPU can execute this function.
 *
 * Returns to the caller:
 *				false  - hardware queue frozen backoff
 *				true   - feel free to send more pkts
 */
bool sch_direct_xmit(struct sk_buff *skb, struct Qdisc *q,
		     struct net_device *dev, struct netdev_queue *txq,
		     spinlock_t *root_lock, bool validate)
{
	int ret = NETDEV_TX_BUSY;
	bool again = false;

	/* And release qdisc */
	if (root_lock)
		spin_unlock(root_lock);

	/* Note that we validate skb (GSO, checksum, ...) outside of locks */
	if (validate)
		skb = validate_xmit_skb_list(skb, dev, &again);

#ifdef CONFIG_XFRM_OFFLOAD
	if (unlikely(again)) {
		if (root_lock)
			spin_lock(root_lock);

		dev_requeue_skb(skb, q);
		return false;
	}
#endif

	if (likely(skb)) {
		HARD_TX_LOCK(dev, txq, smp_processor_id());
		if (!netif_xmit_frozen_or_stopped(txq))
			skb = dev_hard_start_xmit(skb, dev, txq, &ret);

		HARD_TX_UNLOCK(dev, txq);
	} else {
		if (root_lock)
			spin_lock(root_lock);
		return true;
	}

	if (root_lock)
		spin_lock(root_lock);

	if (!dev_xmit_complete(ret)) {
		/* Driver returned NETDEV_TX_BUSY - requeue skb */
		if (unlikely(ret != NETDEV_TX_BUSY))
			net_warn_ratelimited("BUG %s code %d qlen %d\n",
					     dev->name, ret, q->q.qlen);

		dev_requeue_skb(skb, q);
		return false;
	}

	return true;
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
static inline bool qdisc_restart(struct Qdisc *q, int *packets)
{
	spinlock_t *root_lock = NULL;
	struct netdev_queue *txq;
	struct net_device *dev;
	struct sk_buff *skb;
	bool validate;

	/* Dequeue packet */
	skb = dequeue_skb(q, &validate, packets);
	if (unlikely(!skb))
		return false;

	if (!(q->flags & TCQ_F_NOLOCK))
		root_lock = qdisc_lock(q);

	dev = qdisc_dev(q);
	txq = skb_get_tx_queue(dev, skb);

	return sch_direct_xmit(skb, q, dev, txq, root_lock, validate);
}

void __qdisc_run(struct Qdisc *q)
{
	int quota = dev_tx_weight;
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
}

unsigned long dev_trans_start(struct net_device *dev)
{
	unsigned long val, res;
	unsigned int i;

	if (is_vlan_dev(dev))
		dev = vlan_dev_real_dev(dev);
	else if (netif_is_macvlan(dev))
		dev = macvlan_dev_real_dev(dev);
	res = netdev_get_tx_queue(dev, 0)->trans_start;
	for (i = 1; i < dev->num_tx_queues; i++) {
		val = netdev_get_tx_queue(dev, i)->trans_start;
		if (val && time_after(val, res))
			res = val;
	}

	return res;
}
EXPORT_SYMBOL(dev_trans_start);

static void dev_watchdog(struct timer_list *t)
{
	struct net_device *dev = from_timer(dev, t, watchdog_timer);

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
				trace_net_dev_xmit_timeout(dev, i);
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
 * Device has detected acquisition of carrier.
 */
void netif_carrier_on(struct net_device *dev)
{
	if (test_and_clear_bit(__LINK_STATE_NOCARRIER, &dev->state)) {
		if (dev->reg_state == NETREG_UNINITIALIZED)
			return;
		atomic_inc(&dev->carrier_up_count);
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
		atomic_inc(&dev->carrier_down_count);
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
	RCU_POINTER_INITIALIZER(qdisc, &noop_qdisc),
	.qdisc_sleeping	=	&noop_qdisc,
};

struct Qdisc noop_qdisc = {
	.enqueue	=	noop_enqueue,
	.dequeue	=	noop_dequeue,
	.flags		=	TCQ_F_BUILTIN,
	.ops		=	&noop_qdisc_ops,
	.q.lock		=	__SPIN_LOCK_UNLOCKED(noop_qdisc.q.lock),
	.dev_queue	=	&noop_netdev_queue,
	.running	=	SEQCNT_ZERO(noop_qdisc.running),
	.busylock	=	__SPIN_LOCK_UNLOCKED(noop_qdisc.busylock),
	.gso_skb = {
		.next = (struct sk_buff *)&noop_qdisc.gso_skb,
		.prev = (struct sk_buff *)&noop_qdisc.gso_skb,
		.qlen = 0,
		.lock = __SPIN_LOCK_UNLOCKED(noop_qdisc.gso_skb.lock),
	},
	.skb_bad_txq = {
		.next = (struct sk_buff *)&noop_qdisc.skb_bad_txq,
		.prev = (struct sk_buff *)&noop_qdisc.skb_bad_txq,
		.qlen = 0,
		.lock = __SPIN_LOCK_UNLOCKED(noop_qdisc.skb_bad_txq.lock),
	},
};
EXPORT_SYMBOL(noop_qdisc);

static int noqueue_init(struct Qdisc *qdisc, struct nlattr *opt,
			struct netlink_ext_ack *extack)
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
 *	- rings for priority bands
 */
struct pfifo_fast_priv {
	struct skb_array q[PFIFO_FAST_BANDS];
};

static inline struct skb_array *band2list(struct pfifo_fast_priv *priv,
					  int band)
{
	return &priv->q[band];
}

static int pfifo_fast_enqueue(struct sk_buff *skb, struct Qdisc *qdisc,
			      struct sk_buff **to_free)
{
	int band = prio2band[skb->priority & TC_PRIO_MAX];
	struct pfifo_fast_priv *priv = qdisc_priv(qdisc);
	struct skb_array *q = band2list(priv, band);
	unsigned int pkt_len = qdisc_pkt_len(skb);
	int err;

	err = skb_array_produce(q, skb);

	if (unlikely(err)) {
		if (qdisc_is_percpu_stats(qdisc))
			return qdisc_drop_cpu(skb, qdisc, to_free);
		else
			return qdisc_drop(skb, qdisc, to_free);
	}

	qdisc_update_stats_at_enqueue(qdisc, pkt_len);
	return NET_XMIT_SUCCESS;
}

static struct sk_buff *pfifo_fast_dequeue(struct Qdisc *qdisc)
{
	struct pfifo_fast_priv *priv = qdisc_priv(qdisc);
	struct sk_buff *skb = NULL;
	int band;

	for (band = 0; band < PFIFO_FAST_BANDS && !skb; band++) {
		struct skb_array *q = band2list(priv, band);

		if (__skb_array_empty(q))
			continue;

		skb = __skb_array_consume(q);
	}
	if (likely(skb)) {
		qdisc_update_stats_at_dequeue(qdisc, skb);
	} else {
		qdisc->empty = true;
	}

	return skb;
}

static struct sk_buff *pfifo_fast_peek(struct Qdisc *qdisc)
{
	struct pfifo_fast_priv *priv = qdisc_priv(qdisc);
	struct sk_buff *skb = NULL;
	int band;

	for (band = 0; band < PFIFO_FAST_BANDS && !skb; band++) {
		struct skb_array *q = band2list(priv, band);

		skb = __skb_array_peek(q);
	}

	return skb;
}

static void pfifo_fast_reset(struct Qdisc *qdisc)
{
	int i, band;
	struct pfifo_fast_priv *priv = qdisc_priv(qdisc);

	for (band = 0; band < PFIFO_FAST_BANDS; band++) {
		struct skb_array *q = band2list(priv, band);
		struct sk_buff *skb;

		/* NULL ring is possible if destroy path is due to a failed
		 * skb_array_init() in pfifo_fast_init() case.
		 */
		if (!q->ring.queue)
			continue;

		while ((skb = __skb_array_consume(q)) != NULL)
			kfree_skb(skb);
	}

	if (qdisc_is_percpu_stats(qdisc)) {
		for_each_possible_cpu(i) {
			struct gnet_stats_queue *q;

			q = per_cpu_ptr(qdisc->cpu_qstats, i);
			q->backlog = 0;
			q->qlen = 0;
		}
	}
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

static int pfifo_fast_init(struct Qdisc *qdisc, struct nlattr *opt,
			   struct netlink_ext_ack *extack)
{
	unsigned int qlen = qdisc_dev(qdisc)->tx_queue_len;
	struct pfifo_fast_priv *priv = qdisc_priv(qdisc);
	int prio;

	/* guard against zero length rings */
	if (!qlen)
		return -EINVAL;

	for (prio = 0; prio < PFIFO_FAST_BANDS; prio++) {
		struct skb_array *q = band2list(priv, prio);
		int err;

		err = skb_array_init(q, qlen, GFP_KERNEL);
		if (err)
			return -ENOMEM;
	}

	/* Can by-pass the queue discipline */
	qdisc->flags |= TCQ_F_CAN_BYPASS;
	return 0;
}

static void pfifo_fast_destroy(struct Qdisc *sch)
{
	struct pfifo_fast_priv *priv = qdisc_priv(sch);
	int prio;

	for (prio = 0; prio < PFIFO_FAST_BANDS; prio++) {
		struct skb_array *q = band2list(priv, prio);

		/* NULL ring is possible if destroy path is due to a failed
		 * skb_array_init() in pfifo_fast_init() case.
		 */
		if (!q->ring.queue)
			continue;
		/* Destroy ring but no need to kfree_skb because a call to
		 * pfifo_fast_reset() has already done that work.
		 */
		ptr_ring_cleanup(&q->ring, NULL);
	}
}

static int pfifo_fast_change_tx_queue_len(struct Qdisc *sch,
					  unsigned int new_len)
{
	struct pfifo_fast_priv *priv = qdisc_priv(sch);
	struct skb_array *bands[PFIFO_FAST_BANDS];
	int prio;

	for (prio = 0; prio < PFIFO_FAST_BANDS; prio++) {
		struct skb_array *q = band2list(priv, prio);

		bands[prio] = q;
	}

	return skb_array_resize_multiple(bands, PFIFO_FAST_BANDS, new_len,
					 GFP_KERNEL);
}

struct Qdisc_ops pfifo_fast_ops __read_mostly = {
	.id		=	"pfifo_fast",
	.priv_size	=	sizeof(struct pfifo_fast_priv),
	.enqueue	=	pfifo_fast_enqueue,
	.dequeue	=	pfifo_fast_dequeue,
	.peek		=	pfifo_fast_peek,
	.init		=	pfifo_fast_init,
	.destroy	=	pfifo_fast_destroy,
	.reset		=	pfifo_fast_reset,
	.dump		=	pfifo_fast_dump,
	.change_tx_queue_len =  pfifo_fast_change_tx_queue_len,
	.owner		=	THIS_MODULE,
	.static_flags	=	TCQ_F_NOLOCK | TCQ_F_CPUSTATS,
};
EXPORT_SYMBOL(pfifo_fast_ops);

static struct lock_class_key qdisc_tx_busylock;
static struct lock_class_key qdisc_running_key;

struct Qdisc *qdisc_alloc(struct netdev_queue *dev_queue,
			  const struct Qdisc_ops *ops,
			  struct netlink_ext_ack *extack)
{
	void *p;
	struct Qdisc *sch;
	unsigned int size = QDISC_ALIGN(sizeof(*sch)) + ops->priv_size;
	int err = -ENOBUFS;
	struct net_device *dev;

	if (!dev_queue) {
		NL_SET_ERR_MSG(extack, "No device queue given");
		err = -EINVAL;
		goto errout;
	}

	dev = dev_queue->dev;
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
	__skb_queue_head_init(&sch->gso_skb);
	__skb_queue_head_init(&sch->skb_bad_txq);
	qdisc_skb_head_init(&sch->q);
	spin_lock_init(&sch->q.lock);

	if (ops->static_flags & TCQ_F_CPUSTATS) {
		sch->cpu_bstats =
			netdev_alloc_pcpu_stats(struct gnet_stats_basic_cpu);
		if (!sch->cpu_bstats)
			goto errout1;

		sch->cpu_qstats = alloc_percpu(struct gnet_stats_queue);
		if (!sch->cpu_qstats) {
			free_percpu(sch->cpu_bstats);
			goto errout1;
		}
	}

	spin_lock_init(&sch->busylock);
	lockdep_set_class(&sch->busylock,
			  dev->qdisc_tx_busylock ?: &qdisc_tx_busylock);

	/* seqlock has the same scope of busylock, for NOLOCK qdisc */
	spin_lock_init(&sch->seqlock);
	lockdep_set_class(&sch->busylock,
			  dev->qdisc_tx_busylock ?: &qdisc_tx_busylock);

	seqcount_init(&sch->running);
	lockdep_set_class(&sch->running,
			  dev->qdisc_running_key ?: &qdisc_running_key);

	sch->ops = ops;
	sch->flags = ops->static_flags;
	sch->enqueue = ops->enqueue;
	sch->dequeue = ops->dequeue;
	sch->dev_queue = dev_queue;
	sch->empty = true;
	dev_hold(dev);
	refcount_set(&sch->refcnt, 1);

	return sch;
errout1:
	kfree(p);
errout:
	return ERR_PTR(err);
}

struct Qdisc *qdisc_create_dflt(struct netdev_queue *dev_queue,
				const struct Qdisc_ops *ops,
				unsigned int parentid,
				struct netlink_ext_ack *extack)
{
	struct Qdisc *sch;

	if (!try_module_get(ops->owner)) {
		NL_SET_ERR_MSG(extack, "Failed to increase module reference counter");
		return NULL;
	}

	sch = qdisc_alloc(dev_queue, ops, extack);
	if (IS_ERR(sch)) {
		module_put(ops->owner);
		return NULL;
	}
	sch->parent = parentid;

	if (!ops->init || ops->init(sch, NULL, extack) == 0)
		return sch;

	qdisc_put(sch);
	return NULL;
}
EXPORT_SYMBOL(qdisc_create_dflt);

/* Under qdisc_lock(qdisc) and BH! */

void qdisc_reset(struct Qdisc *qdisc)
{
	const struct Qdisc_ops *ops = qdisc->ops;
	struct sk_buff *skb, *tmp;

	if (ops->reset)
		ops->reset(qdisc);

	skb_queue_walk_safe(&qdisc->gso_skb, skb, tmp) {
		__skb_unlink(skb, &qdisc->gso_skb);
		kfree_skb_list(skb);
	}

	skb_queue_walk_safe(&qdisc->skb_bad_txq, skb, tmp) {
		__skb_unlink(skb, &qdisc->skb_bad_txq);
		kfree_skb_list(skb);
	}

	qdisc->q.qlen = 0;
	qdisc->qstats.backlog = 0;
}
EXPORT_SYMBOL(qdisc_reset);

void qdisc_free(struct Qdisc *qdisc)
{
	if (qdisc_is_percpu_stats(qdisc)) {
		free_percpu(qdisc->cpu_bstats);
		free_percpu(qdisc->cpu_qstats);
	}

	kfree((char *) qdisc - qdisc->padded);
}

static void qdisc_free_cb(struct rcu_head *head)
{
	struct Qdisc *q = container_of(head, struct Qdisc, rcu);

	qdisc_free(q);
}

static void qdisc_destroy(struct Qdisc *qdisc)
{
	const struct Qdisc_ops  *ops = qdisc->ops;
	struct sk_buff *skb, *tmp;

#ifdef CONFIG_NET_SCHED
	qdisc_hash_del(qdisc);

	qdisc_put_stab(rtnl_dereference(qdisc->stab));
#endif
	gen_kill_estimator(&qdisc->rate_est);
	if (ops->reset)
		ops->reset(qdisc);
	if (ops->destroy)
		ops->destroy(qdisc);

	module_put(ops->owner);
	dev_put(qdisc_dev(qdisc));

	skb_queue_walk_safe(&qdisc->gso_skb, skb, tmp) {
		__skb_unlink(skb, &qdisc->gso_skb);
		kfree_skb_list(skb);
	}

	skb_queue_walk_safe(&qdisc->skb_bad_txq, skb, tmp) {
		__skb_unlink(skb, &qdisc->skb_bad_txq);
		kfree_skb_list(skb);
	}

	call_rcu(&qdisc->rcu, qdisc_free_cb);
}

void qdisc_put(struct Qdisc *qdisc)
{
	if (!qdisc)
		return;

	if (qdisc->flags & TCQ_F_BUILTIN ||
	    !refcount_dec_and_test(&qdisc->refcnt))
		return;

	qdisc_destroy(qdisc);
}
EXPORT_SYMBOL(qdisc_put);

/* Version of qdisc_put() that is called with rtnl mutex unlocked.
 * Intended to be used as optimization, this function only takes rtnl lock if
 * qdisc reference counter reached zero.
 */

void qdisc_put_unlocked(struct Qdisc *qdisc)
{
	if (qdisc->flags & TCQ_F_BUILTIN ||
	    !refcount_dec_and_rtnl_lock(&qdisc->refcnt))
		return;

	qdisc_destroy(qdisc);
	rtnl_unlock();
}
EXPORT_SYMBOL(qdisc_put_unlocked);

/* Attach toplevel qdisc to device queue. */
struct Qdisc *dev_graft_qdisc(struct netdev_queue *dev_queue,
			      struct Qdisc *qdisc)
{
	struct Qdisc *oqdisc = dev_queue->qdisc_sleeping;
	spinlock_t *root_lock;

	root_lock = qdisc_lock(oqdisc);
	spin_lock_bh(root_lock);

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

	qdisc = qdisc_create_dflt(dev_queue, ops, TC_H_ROOT, NULL);
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
		qdisc_refcount_inc(dev->qdisc);
	} else {
		qdisc = qdisc_create_dflt(txq, &mq_qdisc_ops, TC_H_ROOT, NULL);
		if (qdisc) {
			dev->qdisc = qdisc;
			qdisc->ops->attach(qdisc);
		}
	}
#ifdef CONFIG_NET_SCHED
	if (dev->qdisc != &noop_qdisc)
		qdisc_hash_add(dev->qdisc, false);
#endif
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
		bool nolock = qdisc->flags & TCQ_F_NOLOCK;

		if (nolock)
			spin_lock_bh(&qdisc->seqlock);
		spin_lock_bh(qdisc_lock(qdisc));

		if (!(qdisc->flags & TCQ_F_BUILTIN))
			set_bit(__QDISC_STATE_DEACTIVATED, &qdisc->state);

		rcu_assign_pointer(dev_queue->qdisc, qdisc_default);
		qdisc_reset(qdisc);

		spin_unlock_bh(qdisc_lock(qdisc));
		if (nolock)
			spin_unlock_bh(&qdisc->seqlock);
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

static void dev_qdisc_reset(struct net_device *dev,
			    struct netdev_queue *dev_queue,
			    void *none)
{
	struct Qdisc *qdisc = dev_queue->qdisc_sleeping;

	if (qdisc)
		qdisc_reset(qdisc);
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

	list_for_each_entry(dev, head, close_list) {
		netdev_for_each_tx_queue(dev, dev_deactivate_queue,
					 &noop_qdisc);
		if (dev_ingress_queue(dev))
			dev_deactivate_queue(dev, dev_ingress_queue(dev),
					     &noop_qdisc);

		dev_watchdog_down(dev);
	}

	/* Wait for outstanding qdisc-less dev_queue_xmit calls.
	 * This is avoided if all devices are in dismantle phase :
	 * Caller will call synchronize_net() for us
	 */
	synchronize_net();

	/* Wait for outstanding qdisc_run calls. */
	list_for_each_entry(dev, head, close_list) {
		while (some_qdisc_is_busy(dev))
			yield();
		/* The new qdisc is assigned at this point so we can safely
		 * unwind stale skb lists and qdisc statistics
		 */
		netdev_for_each_tx_queue(dev, dev_qdisc_reset, NULL);
		if (dev_ingress_queue(dev))
			dev_qdisc_reset(dev, dev_ingress_queue(dev), NULL);
	}
}

void dev_deactivate(struct net_device *dev)
{
	LIST_HEAD(single);

	list_add(&dev->close_list, &single);
	dev_deactivate_many(&single);
	list_del(&single);
}
EXPORT_SYMBOL(dev_deactivate);

static int qdisc_change_tx_queue_len(struct net_device *dev,
				     struct netdev_queue *dev_queue)
{
	struct Qdisc *qdisc = dev_queue->qdisc_sleeping;
	const struct Qdisc_ops *ops = qdisc->ops;

	if (ops->change_tx_queue_len)
		return ops->change_tx_queue_len(qdisc, dev->tx_queue_len);
	return 0;
}

int dev_qdisc_change_tx_queue_len(struct net_device *dev)
{
	bool up = dev->flags & IFF_UP;
	unsigned int i;
	int ret = 0;

	if (up)
		dev_deactivate(dev);

	for (i = 0; i < dev->num_tx_queues; i++) {
		ret = qdisc_change_tx_queue_len(dev, &dev->_tx[i]);

		/* TODO: revert changes on a partial failure */
		if (ret)
			break;
	}

	if (up)
		dev_activate(dev);
	return ret;
}

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

	timer_setup(&dev->watchdog_timer, dev_watchdog, 0);
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

		qdisc_put(qdisc);
	}
}

void dev_shutdown(struct net_device *dev)
{
	netdev_for_each_tx_queue(dev, shutdown_scheduler_queue, &noop_qdisc);
	if (dev_ingress_queue(dev))
		shutdown_scheduler_queue(dev, dev_ingress_queue(dev), &noop_qdisc);
	qdisc_put(dev->qdisc);
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

static void mini_qdisc_rcu_func(struct rcu_head *head)
{
}

void mini_qdisc_pair_swap(struct mini_Qdisc_pair *miniqp,
			  struct tcf_proto *tp_head)
{
	/* Protected with chain0->filter_chain_lock.
	 * Can't access chain directly because tp_head can be NULL.
	 */
	struct mini_Qdisc *miniq_old =
		rcu_dereference_protected(*miniqp->p_miniq, 1);
	struct mini_Qdisc *miniq;

	if (!tp_head) {
		RCU_INIT_POINTER(*miniqp->p_miniq, NULL);
		/* Wait for flying RCU callback before it is freed. */
		rcu_barrier();
		return;
	}

	miniq = !miniq_old || miniq_old == &miniqp->miniq2 ?
		&miniqp->miniq1 : &miniqp->miniq2;

	/* We need to make sure that readers won't see the miniq
	 * we are about to modify. So wait until previous call_rcu callback
	 * is done.
	 */
	rcu_barrier();
	miniq->filter_list = tp_head;
	rcu_assign_pointer(*miniqp->p_miniq, miniq);

	if (miniq_old)
		/* This is counterpart of the rcu barriers above. We need to
		 * block potential new user of miniq_old until all readers
		 * are not seeing it.
		 */
		call_rcu(&miniq_old->rcu, mini_qdisc_rcu_func);
}
EXPORT_SYMBOL(mini_qdisc_pair_swap);

void mini_qdisc_pair_init(struct mini_Qdisc_pair *miniqp, struct Qdisc *qdisc,
			  struct mini_Qdisc __rcu **p_miniq)
{
	miniqp->miniq1.cpu_bstats = qdisc->cpu_bstats;
	miniqp->miniq1.cpu_qstats = qdisc->cpu_qstats;
	miniqp->miniq2.cpu_bstats = qdisc->cpu_bstats;
	miniqp->miniq2.cpu_qstats = qdisc->cpu_qstats;
	miniqp->p_miniq = p_miniq;
}
EXPORT_SYMBOL(mini_qdisc_pair_init);
