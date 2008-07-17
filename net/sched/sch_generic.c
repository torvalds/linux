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
#include <net/pkt_sched.h>

/* Main transmission queue. */

/* Modifications to data participating in scheduling must be protected with
 * queue->lock spinlock.
 *
 * The idea is the following:
 * - enqueue, dequeue are serialized via top level device
 *   spinlock queue->lock.
 * - ingress filtering is serialized via top level device
 *   spinlock dev->rx_queue.lock.
 * - updates to tree and tree walking are only done under the rtnl mutex.
 */

void qdisc_lock_tree(struct net_device *dev)
	__acquires(dev->rx_queue.lock)
{
	unsigned int i;

	local_bh_disable();
	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);
		spin_lock(&txq->lock);
	}
	spin_lock(&dev->rx_queue.lock);
}
EXPORT_SYMBOL(qdisc_lock_tree);

void qdisc_unlock_tree(struct net_device *dev)
	__releases(dev->rx_queue.lock)
{
	unsigned int i;

	spin_unlock(&dev->rx_queue.lock);
	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);
		spin_unlock(&txq->lock);
	}
	local_bh_enable();
}
EXPORT_SYMBOL(qdisc_unlock_tree);

static inline int qdisc_qlen(struct Qdisc *q)
{
	return q->q.qlen;
}

static inline int dev_requeue_skb(struct sk_buff *skb,
				  struct netdev_queue *dev_queue,
				  struct Qdisc *q)
{
	if (unlikely(skb->next))
		dev_queue->gso_skb = skb;
	else
		q->ops->requeue(skb, q);

	netif_schedule_queue(dev_queue);
	return 0;
}

static inline struct sk_buff *dequeue_skb(struct netdev_queue *dev_queue,
					  struct Qdisc *q)
{
	struct sk_buff *skb;

	if ((skb = dev_queue->gso_skb))
		dev_queue->gso_skb = NULL;
	else
		skb = q->dequeue(q);

	return skb;
}

static inline int handle_dev_cpu_collision(struct sk_buff *skb,
					   struct netdev_queue *dev_queue,
					   struct Qdisc *q)
{
	int ret;

	if (unlikely(dev_queue->xmit_lock_owner == smp_processor_id())) {
		/*
		 * Same CPU holding the lock. It may be a transient
		 * configuration error, when hard_start_xmit() recurses. We
		 * detect it by checking xmit owner and drop the packet when
		 * deadloop is detected. Return OK to try the next skb.
		 */
		kfree_skb(skb);
		if (net_ratelimit())
			printk(KERN_WARNING "Dead loop on netdevice %s, "
			       "fix it urgently!\n", dev_queue->dev->name);
		ret = qdisc_qlen(q);
	} else {
		/*
		 * Another cpu is holding lock, requeue & delay xmits for
		 * some time.
		 */
		__get_cpu_var(netdev_rx_stat).cpu_collision++;
		ret = dev_requeue_skb(skb, dev_queue, q);
	}

	return ret;
}

/*
 * NOTE: Called under queue->lock with locally disabled BH.
 *
 * __QUEUE_STATE_QDISC_RUNNING guarantees only one CPU can process
 * this queue at a time. queue->lock serializes queue accesses for
 * this queue AND txq->qdisc pointer itself.
 *
 *  netif_tx_lock serializes accesses to device driver.
 *
 *  queue->lock and netif_tx_lock are mutually exclusive,
 *  if one is grabbed, another must be free.
 *
 * Note, that this procedure can be called by a watchdog timer
 *
 * Returns to the caller:
 *				0  - queue is empty or throttled.
 *				>0 - queue is not empty.
 *
 */
static inline int qdisc_restart(struct netdev_queue *txq)
{
	struct Qdisc *q = txq->qdisc;
	int ret = NETDEV_TX_BUSY;
	struct net_device *dev;
	struct sk_buff *skb;

	/* Dequeue packet */
	if (unlikely((skb = dequeue_skb(txq, q)) == NULL))
		return 0;


	/* And release queue */
	spin_unlock(&txq->lock);

	dev = txq->dev;

	HARD_TX_LOCK(dev, txq, smp_processor_id());
	if (!netif_subqueue_stopped(dev, skb))
		ret = dev_hard_start_xmit(skb, dev, txq);
	HARD_TX_UNLOCK(dev, txq);

	spin_lock(&txq->lock);
	q = txq->qdisc;

	switch (ret) {
	case NETDEV_TX_OK:
		/* Driver sent out skb successfully */
		ret = qdisc_qlen(q);
		break;

	case NETDEV_TX_LOCKED:
		/* Driver try lock failed */
		ret = handle_dev_cpu_collision(skb, txq, q);
		break;

	default:
		/* Driver returned NETDEV_TX_BUSY - requeue skb */
		if (unlikely (ret != NETDEV_TX_BUSY && net_ratelimit()))
			printk(KERN_WARNING "BUG %s code %d qlen %d\n",
			       dev->name, ret, q->q.qlen);

		ret = dev_requeue_skb(skb, txq, q);
		break;
	}

	return ret;
}

void __qdisc_run(struct netdev_queue *txq)
{
	unsigned long start_time = jiffies;

	while (qdisc_restart(txq)) {
		if (netif_tx_queue_stopped(txq))
			break;

		/*
		 * Postpone processing if
		 * 1. another process needs the CPU;
		 * 2. we've been doing it for too long.
		 */
		if (need_resched() || jiffies != start_time) {
			netif_schedule_queue(txq);
			break;
		}
	}

	clear_bit(__QUEUE_STATE_QDISC_RUNNING, &txq->state);
}

static void dev_watchdog(unsigned long arg)
{
	struct net_device *dev = (struct net_device *)arg;

	netif_tx_lock(dev);
	if (!qdisc_tx_is_noop(dev)) {
		if (netif_device_present(dev) &&
		    netif_running(dev) &&
		    netif_carrier_ok(dev)) {
			int some_queue_stopped = 0;
			unsigned int i;

			for (i = 0; i < dev->num_tx_queues; i++) {
				struct netdev_queue *txq;

				txq = netdev_get_tx_queue(dev, i);
				if (netif_tx_queue_stopped(txq)) {
					some_queue_stopped = 1;
					break;
				}
			}

			if (some_queue_stopped &&
			    time_after(jiffies, (dev->trans_start +
						 dev->watchdog_timeo))) {
				printk(KERN_INFO "NETDEV WATCHDOG: %s: "
				       "transmit timed out\n",
				       dev->name);
				dev->tx_timeout(dev);
				WARN_ON_ONCE(1);
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
	if (dev->tx_timeout) {
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
	if (!test_and_set_bit(__LINK_STATE_NOCARRIER, &dev->state))
		linkwatch_fire_event(dev);
}
EXPORT_SYMBOL(netif_carrier_off);

/* "NOOP" scheduler: the best scheduler, recommended for all interfaces
   under all circumstances. It is difficult to invent anything faster or
   cheaper.
 */

static int noop_enqueue(struct sk_buff *skb, struct Qdisc * qdisc)
{
	kfree_skb(skb);
	return NET_XMIT_CN;
}

static struct sk_buff *noop_dequeue(struct Qdisc * qdisc)
{
	return NULL;
}

static int noop_requeue(struct sk_buff *skb, struct Qdisc* qdisc)
{
	if (net_ratelimit())
		printk(KERN_DEBUG "%s deferred output. It is buggy.\n",
		       skb->dev->name);
	kfree_skb(skb);
	return NET_XMIT_CN;
}

struct Qdisc_ops noop_qdisc_ops __read_mostly = {
	.id		=	"noop",
	.priv_size	=	0,
	.enqueue	=	noop_enqueue,
	.dequeue	=	noop_dequeue,
	.requeue	=	noop_requeue,
	.owner		=	THIS_MODULE,
};

struct Qdisc noop_qdisc = {
	.enqueue	=	noop_enqueue,
	.dequeue	=	noop_dequeue,
	.flags		=	TCQ_F_BUILTIN,
	.ops		=	&noop_qdisc_ops,
	.list		=	LIST_HEAD_INIT(noop_qdisc.list),
};
EXPORT_SYMBOL(noop_qdisc);

static struct Qdisc_ops noqueue_qdisc_ops __read_mostly = {
	.id		=	"noqueue",
	.priv_size	=	0,
	.enqueue	=	noop_enqueue,
	.dequeue	=	noop_dequeue,
	.requeue	=	noop_requeue,
	.owner		=	THIS_MODULE,
};

static struct Qdisc noqueue_qdisc = {
	.enqueue	=	NULL,
	.dequeue	=	noop_dequeue,
	.flags		=	TCQ_F_BUILTIN,
	.ops		=	&noqueue_qdisc_ops,
	.list		=	LIST_HEAD_INIT(noqueue_qdisc.list),
};


static const u8 prio2band[TC_PRIO_MAX+1] =
	{ 1, 2, 2, 2, 1, 2, 0, 0 , 1, 1, 1, 1, 1, 1, 1, 1 };

/* 3-band FIFO queue: old style, but should be a bit faster than
   generic prio+fifo combination.
 */

#define PFIFO_FAST_BANDS 3

static inline struct sk_buff_head *prio2list(struct sk_buff *skb,
					     struct Qdisc *qdisc)
{
	struct sk_buff_head *list = qdisc_priv(qdisc);
	return list + prio2band[skb->priority & TC_PRIO_MAX];
}

static int pfifo_fast_enqueue(struct sk_buff *skb, struct Qdisc* qdisc)
{
	struct sk_buff_head *list = prio2list(skb, qdisc);

	if (skb_queue_len(list) < qdisc_dev(qdisc)->tx_queue_len) {
		qdisc->q.qlen++;
		return __qdisc_enqueue_tail(skb, qdisc, list);
	}

	return qdisc_drop(skb, qdisc);
}

static struct sk_buff *pfifo_fast_dequeue(struct Qdisc* qdisc)
{
	int prio;
	struct sk_buff_head *list = qdisc_priv(qdisc);

	for (prio = 0; prio < PFIFO_FAST_BANDS; prio++) {
		if (!skb_queue_empty(list + prio)) {
			qdisc->q.qlen--;
			return __qdisc_dequeue_head(qdisc, list + prio);
		}
	}

	return NULL;
}

static int pfifo_fast_requeue(struct sk_buff *skb, struct Qdisc* qdisc)
{
	qdisc->q.qlen++;
	return __qdisc_requeue(skb, qdisc, prio2list(skb, qdisc));
}

static void pfifo_fast_reset(struct Qdisc* qdisc)
{
	int prio;
	struct sk_buff_head *list = qdisc_priv(qdisc);

	for (prio = 0; prio < PFIFO_FAST_BANDS; prio++)
		__qdisc_reset_queue(qdisc, list + prio);

	qdisc->qstats.backlog = 0;
	qdisc->q.qlen = 0;
}

static int pfifo_fast_dump(struct Qdisc *qdisc, struct sk_buff *skb)
{
	struct tc_prio_qopt opt = { .bands = PFIFO_FAST_BANDS };

	memcpy(&opt.priomap, prio2band, TC_PRIO_MAX+1);
	NLA_PUT(skb, TCA_OPTIONS, sizeof(opt), &opt);
	return skb->len;

nla_put_failure:
	return -1;
}

static int pfifo_fast_init(struct Qdisc *qdisc, struct nlattr *opt)
{
	int prio;
	struct sk_buff_head *list = qdisc_priv(qdisc);

	for (prio = 0; prio < PFIFO_FAST_BANDS; prio++)
		skb_queue_head_init(list + prio);

	return 0;
}

static struct Qdisc_ops pfifo_fast_ops __read_mostly = {
	.id		=	"pfifo_fast",
	.priv_size	=	PFIFO_FAST_BANDS * sizeof(struct sk_buff_head),
	.enqueue	=	pfifo_fast_enqueue,
	.dequeue	=	pfifo_fast_dequeue,
	.requeue	=	pfifo_fast_requeue,
	.init		=	pfifo_fast_init,
	.reset		=	pfifo_fast_reset,
	.dump		=	pfifo_fast_dump,
	.owner		=	THIS_MODULE,
};

struct Qdisc *qdisc_alloc(struct netdev_queue *dev_queue,
			  struct Qdisc_ops *ops)
{
	void *p;
	struct Qdisc *sch;
	unsigned int size;
	int err = -ENOBUFS;

	/* ensure that the Qdisc and the private data are 32-byte aligned */
	size = QDISC_ALIGN(sizeof(*sch));
	size += ops->priv_size + (QDISC_ALIGNTO - 1);

	p = kzalloc(size, GFP_KERNEL);
	if (!p)
		goto errout;
	sch = (struct Qdisc *) QDISC_ALIGN((unsigned long) p);
	sch->padded = (char *) sch - (char *) p;

	INIT_LIST_HEAD(&sch->list);
	skb_queue_head_init(&sch->q);
	sch->ops = ops;
	sch->enqueue = ops->enqueue;
	sch->dequeue = ops->dequeue;
	sch->dev_queue = dev_queue;
	dev_hold(qdisc_dev(sch));
	atomic_set(&sch->refcnt, 1);

	return sch;
errout:
	return ERR_PTR(err);
}

struct Qdisc * qdisc_create_dflt(struct net_device *dev,
				 struct netdev_queue *dev_queue,
				 struct Qdisc_ops *ops,
				 unsigned int parentid)
{
	struct Qdisc *sch;

	sch = qdisc_alloc(dev_queue, ops);
	if (IS_ERR(sch))
		goto errout;
	sch->parent = parentid;

	if (!ops->init || ops->init(sch, NULL) == 0)
		return sch;

	qdisc_destroy(sch);
errout:
	return NULL;
}
EXPORT_SYMBOL(qdisc_create_dflt);

/* Under queue->lock and BH! */

void qdisc_reset(struct Qdisc *qdisc)
{
	const struct Qdisc_ops *ops = qdisc->ops;

	if (ops->reset)
		ops->reset(qdisc);
}
EXPORT_SYMBOL(qdisc_reset);

/* this is the rcu callback function to clean up a qdisc when there
 * are no further references to it */

static void __qdisc_destroy(struct rcu_head *head)
{
	struct Qdisc *qdisc = container_of(head, struct Qdisc, q_rcu);
	kfree((char *) qdisc - qdisc->padded);
}

/* Under queue->lock and BH! */

void qdisc_destroy(struct Qdisc *qdisc)
{
	const struct Qdisc_ops  *ops = qdisc->ops;

	if (qdisc->flags & TCQ_F_BUILTIN ||
	    !atomic_dec_and_test(&qdisc->refcnt))
		return;

	list_del(&qdisc->list);
	gen_kill_estimator(&qdisc->bstats, &qdisc->rate_est);
	if (ops->reset)
		ops->reset(qdisc);
	if (ops->destroy)
		ops->destroy(qdisc);

	module_put(ops->owner);
	dev_put(qdisc_dev(qdisc));
	call_rcu(&qdisc->q_rcu, __qdisc_destroy);
}
EXPORT_SYMBOL(qdisc_destroy);

static bool dev_all_qdisc_sleeping_noop(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);

		if (txq->qdisc_sleeping != &noop_qdisc)
			return false;
	}
	return true;
}

static void attach_one_default_qdisc(struct net_device *dev,
				     struct netdev_queue *dev_queue,
				     void *_unused)
{
	struct Qdisc *qdisc;

	if (dev->tx_queue_len) {
		qdisc = qdisc_create_dflt(dev, dev_queue,
					  &pfifo_fast_ops, TC_H_ROOT);
		if (!qdisc) {
			printk(KERN_INFO "%s: activation failed\n", dev->name);
			return;
		}
		list_add_tail(&qdisc->list, &dev_queue->qdisc_list);
	} else {
		qdisc =  &noqueue_qdisc;
	}
	dev_queue->qdisc_sleeping = qdisc;
}

static void transition_one_qdisc(struct net_device *dev,
				 struct netdev_queue *dev_queue,
				 void *_need_watchdog)
{
	int *need_watchdog_p = _need_watchdog;

	spin_lock_bh(&dev_queue->lock);
	rcu_assign_pointer(dev_queue->qdisc, dev_queue->qdisc_sleeping);
	if (dev_queue->qdisc != &noqueue_qdisc)
		*need_watchdog_p = 1;
	spin_unlock_bh(&dev_queue->lock);
}

void dev_activate(struct net_device *dev)
{
	int need_watchdog;

	/* No queueing discipline is attached to device;
	   create default one i.e. pfifo_fast for devices,
	   which need queueing and noqueue_qdisc for
	   virtual interfaces
	 */

	if (dev_all_qdisc_sleeping_noop(dev))
		netdev_for_each_tx_queue(dev, attach_one_default_qdisc, NULL);

	if (!netif_carrier_ok(dev))
		/* Delay activation until next carrier-on event */
		return;

	need_watchdog = 0;
	netdev_for_each_tx_queue(dev, transition_one_qdisc, &need_watchdog);

	if (need_watchdog) {
		dev->trans_start = jiffies;
		dev_watchdog_up(dev);
	}
}

static void dev_deactivate_queue(struct net_device *dev,
				 struct netdev_queue *dev_queue,
				 void *_qdisc_default)
{
	struct Qdisc *qdisc_default = _qdisc_default;
	struct Qdisc *qdisc;
	struct sk_buff *skb;

	spin_lock_bh(&dev_queue->lock);

	qdisc = dev_queue->qdisc;
	if (qdisc) {
		dev_queue->qdisc = qdisc_default;
		qdisc_reset(qdisc);
	}
	skb = dev_queue->gso_skb;
	dev_queue->gso_skb = NULL;

	spin_unlock_bh(&dev_queue->lock);

	kfree_skb(skb);
}

static bool some_qdisc_is_running(struct net_device *dev, int lock)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *dev_queue;
		int val;

		dev_queue = netdev_get_tx_queue(dev, i);

		if (lock)
			spin_lock_bh(&dev_queue->lock);

		val = test_bit(__QUEUE_STATE_QDISC_RUNNING, &dev_queue->state);

		if (lock)
			spin_unlock_bh(&dev_queue->lock);

		if (val)
			return true;
	}
	return false;
}

void dev_deactivate(struct net_device *dev)
{
	bool running;

	netdev_for_each_tx_queue(dev, dev_deactivate_queue, &noop_qdisc);

	dev_watchdog_down(dev);

	/* Wait for outstanding qdisc-less dev_queue_xmit calls. */
	synchronize_rcu();

	/* Wait for outstanding qdisc_run calls. */
	do {
		while (some_qdisc_is_running(dev, 0))
			yield();

		/*
		 * Double-check inside queue lock to ensure that all effects
		 * of the queue run are visible when we return.
		 */
		running = some_qdisc_is_running(dev, 1);

		/*
		 * The running flag should never be set at this point because
		 * we've already set dev->qdisc to noop_qdisc *inside* the same
		 * pair of spin locks.  That is, if any qdisc_run starts after
		 * our initial test it should see the noop_qdisc and then
		 * clear the RUNNING bit before dropping the queue lock.  So
		 * if it is set here then we've found a bug.
		 */
	} while (WARN_ON_ONCE(running));
}

static void dev_init_scheduler_queue(struct net_device *dev,
				     struct netdev_queue *dev_queue,
				     void *_qdisc)
{
	struct Qdisc *qdisc = _qdisc;

	dev_queue->qdisc = qdisc;
	dev_queue->qdisc_sleeping = qdisc;
	INIT_LIST_HEAD(&dev_queue->qdisc_list);
}

void dev_init_scheduler(struct net_device *dev)
{
	qdisc_lock_tree(dev);
	netdev_for_each_tx_queue(dev, dev_init_scheduler_queue, &noop_qdisc);
	dev_init_scheduler_queue(dev, &dev->rx_queue, NULL);
	qdisc_unlock_tree(dev);

	setup_timer(&dev->watchdog_timer, dev_watchdog, (unsigned long)dev);
}

static void shutdown_scheduler_queue(struct net_device *dev,
				     struct netdev_queue *dev_queue,
				     void *_qdisc_default)
{
	struct Qdisc *qdisc = dev_queue->qdisc_sleeping;
	struct Qdisc *qdisc_default = _qdisc_default;

	if (qdisc) {
		dev_queue->qdisc = qdisc_default;
		dev_queue->qdisc_sleeping = qdisc_default;

		qdisc_destroy(qdisc);
	}
}

void dev_shutdown(struct net_device *dev)
{
	qdisc_lock_tree(dev);
	netdev_for_each_tx_queue(dev, shutdown_scheduler_queue, &noop_qdisc);
	shutdown_scheduler_queue(dev, &dev->rx_queue, NULL);
	BUG_TRAP(!timer_pending(&dev->watchdog_timer));
	qdisc_unlock_tree(dev);
}
