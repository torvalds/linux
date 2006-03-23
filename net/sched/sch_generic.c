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

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

/* Main transmission queue. */

/* Main qdisc structure lock. 

   However, modifications
   to data, participating in scheduling must be additionally
   protected with dev->queue_lock spinlock.

   The idea is the following:
   - enqueue, dequeue are serialized via top level device
     spinlock dev->queue_lock.
   - tree walking is protected by read_lock_bh(qdisc_tree_lock)
     and this lock is used only in process context.
   - updates to tree are made under rtnl semaphore or
     from softirq context (__qdisc_destroy rcu-callback)
     hence this lock needs local bh disabling.

   qdisc_tree_lock must be grabbed BEFORE dev->queue_lock!
 */
DEFINE_RWLOCK(qdisc_tree_lock);

void qdisc_lock_tree(struct net_device *dev)
{
	write_lock_bh(&qdisc_tree_lock);
	spin_lock_bh(&dev->queue_lock);
}

void qdisc_unlock_tree(struct net_device *dev)
{
	spin_unlock_bh(&dev->queue_lock);
	write_unlock_bh(&qdisc_tree_lock);
}

/* 
   dev->queue_lock serializes queue accesses for this device
   AND dev->qdisc pointer itself.

   dev->xmit_lock serializes accesses to device driver.

   dev->queue_lock and dev->xmit_lock are mutually exclusive,
   if one is grabbed, another must be free.
 */


/* Kick device.
   Note, that this procedure can be called by a watchdog timer, so that
   we do not check dev->tbusy flag here.

   Returns:  0  - queue is empty.
            >0  - queue is not empty, but throttled.
	    <0  - queue is not empty. Device is throttled, if dev->tbusy != 0.

   NOTE: Called under dev->queue_lock with locally disabled BH.
*/

int qdisc_restart(struct net_device *dev)
{
	struct Qdisc *q = dev->qdisc;
	struct sk_buff *skb;

	/* Dequeue packet */
	if ((skb = q->dequeue(q)) != NULL) {
		unsigned nolock = (dev->features & NETIF_F_LLTX);
		/*
		 * When the driver has LLTX set it does its own locking
		 * in start_xmit. No need to add additional overhead by
		 * locking again. These checks are worth it because
		 * even uncongested locks can be quite expensive.
		 * The driver can do trylock like here too, in case
		 * of lock congestion it should return -1 and the packet
		 * will be requeued.
		 */
		if (!nolock) {
			if (!spin_trylock(&dev->xmit_lock)) {
			collision:
				/* So, someone grabbed the driver. */
				
				/* It may be transient configuration error,
				   when hard_start_xmit() recurses. We detect
				   it by checking xmit owner and drop the
				   packet when deadloop is detected.
				*/
				if (dev->xmit_lock_owner == smp_processor_id()) {
					kfree_skb(skb);
					if (net_ratelimit())
						printk(KERN_DEBUG "Dead loop on netdevice %s, fix it urgently!\n", dev->name);
					return -1;
				}
				__get_cpu_var(netdev_rx_stat).cpu_collision++;
				goto requeue;
			}
			/* Remember that the driver is grabbed by us. */
			dev->xmit_lock_owner = smp_processor_id();
		}
		
		{
			/* And release queue */
			spin_unlock(&dev->queue_lock);

			if (!netif_queue_stopped(dev)) {
				int ret;
				if (netdev_nit)
					dev_queue_xmit_nit(skb, dev);

				ret = dev->hard_start_xmit(skb, dev);
				if (ret == NETDEV_TX_OK) { 
					if (!nolock) {
						dev->xmit_lock_owner = -1;
						spin_unlock(&dev->xmit_lock);
					}
					spin_lock(&dev->queue_lock);
					return -1;
				}
				if (ret == NETDEV_TX_LOCKED && nolock) {
					spin_lock(&dev->queue_lock);
					goto collision; 
				}
			}

			/* NETDEV_TX_BUSY - we need to requeue */
			/* Release the driver */
			if (!nolock) { 
				dev->xmit_lock_owner = -1;
				spin_unlock(&dev->xmit_lock);
			} 
			spin_lock(&dev->queue_lock);
			q = dev->qdisc;
		}

		/* Device kicked us out :(
		   This is possible in three cases:

		   0. driver is locked
		   1. fastroute is enabled
		   2. device cannot determine busy state
		      before start of transmission (f.e. dialout)
		   3. device is buggy (ppp)
		 */

requeue:
		q->ops->requeue(skb, q);
		netif_schedule(dev);
		return 1;
	}
	BUG_ON((int) q->q.qlen < 0);
	return q->q.qlen;
}

static void dev_watchdog(unsigned long arg)
{
	struct net_device *dev = (struct net_device *)arg;

	spin_lock(&dev->xmit_lock);
	if (dev->qdisc != &noop_qdisc) {
		if (netif_device_present(dev) &&
		    netif_running(dev) &&
		    netif_carrier_ok(dev)) {
			if (netif_queue_stopped(dev) &&
			    (jiffies - dev->trans_start) > dev->watchdog_timeo) {
				printk(KERN_INFO "NETDEV WATCHDOG: %s: transmit timed out\n", dev->name);
				dev->tx_timeout(dev);
			}
			if (!mod_timer(&dev->watchdog_timer, jiffies + dev->watchdog_timeo))
				dev_hold(dev);
		}
	}
	spin_unlock(&dev->xmit_lock);

	dev_put(dev);
}

static void dev_watchdog_init(struct net_device *dev)
{
	init_timer(&dev->watchdog_timer);
	dev->watchdog_timer.data = (unsigned long)dev;
	dev->watchdog_timer.function = dev_watchdog;
}

void __netdev_watchdog_up(struct net_device *dev)
{
	if (dev->tx_timeout) {
		if (dev->watchdog_timeo <= 0)
			dev->watchdog_timeo = 5*HZ;
		if (!mod_timer(&dev->watchdog_timer, jiffies + dev->watchdog_timeo))
			dev_hold(dev);
	}
}

static void dev_watchdog_up(struct net_device *dev)
{
	spin_lock_bh(&dev->xmit_lock);
	__netdev_watchdog_up(dev);
	spin_unlock_bh(&dev->xmit_lock);
}

static void dev_watchdog_down(struct net_device *dev)
{
	spin_lock_bh(&dev->xmit_lock);
	if (del_timer(&dev->watchdog_timer))
		dev_put(dev);
	spin_unlock_bh(&dev->xmit_lock);
}

void netif_carrier_on(struct net_device *dev)
{
	if (test_and_clear_bit(__LINK_STATE_NOCARRIER, &dev->state))
		linkwatch_fire_event(dev);
	if (netif_running(dev))
		__netdev_watchdog_up(dev);
}

void netif_carrier_off(struct net_device *dev)
{
	if (!test_and_set_bit(__LINK_STATE_NOCARRIER, &dev->state))
		linkwatch_fire_event(dev);
}

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

struct Qdisc_ops noop_qdisc_ops = {
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

static struct Qdisc_ops noqueue_qdisc_ops = {
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

	if (skb_queue_len(list) < qdisc->dev->tx_queue_len) {
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
	RTA_PUT(skb, TCA_OPTIONS, sizeof(opt), &opt);
	return skb->len;

rtattr_failure:
	return -1;
}

static int pfifo_fast_init(struct Qdisc *qdisc, struct rtattr *opt)
{
	int prio;
	struct sk_buff_head *list = qdisc_priv(qdisc);

	for (prio = 0; prio < PFIFO_FAST_BANDS; prio++)
		skb_queue_head_init(list + prio);

	return 0;
}

static struct Qdisc_ops pfifo_fast_ops = {
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

struct Qdisc *qdisc_alloc(struct net_device *dev, struct Qdisc_ops *ops)
{
	void *p;
	struct Qdisc *sch;
	unsigned int size;
	int err = -ENOBUFS;

	/* ensure that the Qdisc and the private data are 32-byte aligned */
	size = QDISC_ALIGN(sizeof(*sch));
	size += ops->priv_size + (QDISC_ALIGNTO - 1);

	p = kmalloc(size, GFP_KERNEL);
	if (!p)
		goto errout;
	memset(p, 0, size);
	sch = (struct Qdisc *) QDISC_ALIGN((unsigned long) p);
	sch->padded = (char *) sch - (char *) p;

	INIT_LIST_HEAD(&sch->list);
	skb_queue_head_init(&sch->q);
	sch->ops = ops;
	sch->enqueue = ops->enqueue;
	sch->dequeue = ops->dequeue;
	sch->dev = dev;
	dev_hold(dev);
	sch->stats_lock = &dev->queue_lock;
	atomic_set(&sch->refcnt, 1);

	return sch;
errout:
	return ERR_PTR(-err);
}

struct Qdisc * qdisc_create_dflt(struct net_device *dev, struct Qdisc_ops *ops)
{
	struct Qdisc *sch;
	
	sch = qdisc_alloc(dev, ops);
	if (IS_ERR(sch))
		goto errout;

	if (!ops->init || ops->init(sch, NULL) == 0)
		return sch;

	qdisc_destroy(sch);
errout:
	return NULL;
}

/* Under dev->queue_lock and BH! */

void qdisc_reset(struct Qdisc *qdisc)
{
	struct Qdisc_ops *ops = qdisc->ops;

	if (ops->reset)
		ops->reset(qdisc);
}

/* this is the rcu callback function to clean up a qdisc when there 
 * are no further references to it */

static void __qdisc_destroy(struct rcu_head *head)
{
	struct Qdisc *qdisc = container_of(head, struct Qdisc, q_rcu);
	struct Qdisc_ops  *ops = qdisc->ops;

#ifdef CONFIG_NET_ESTIMATOR
	gen_kill_estimator(&qdisc->bstats, &qdisc->rate_est);
#endif
	write_lock(&qdisc_tree_lock);
	if (ops->reset)
		ops->reset(qdisc);
	if (ops->destroy)
		ops->destroy(qdisc);
	write_unlock(&qdisc_tree_lock);
	module_put(ops->owner);

	dev_put(qdisc->dev);
	kfree((char *) qdisc - qdisc->padded);
}

/* Under dev->queue_lock and BH! */

void qdisc_destroy(struct Qdisc *qdisc)
{
	struct list_head cql = LIST_HEAD_INIT(cql);
	struct Qdisc *cq, *q, *n;

	if (qdisc->flags & TCQ_F_BUILTIN ||
		!atomic_dec_and_test(&qdisc->refcnt))
		return;

	if (!list_empty(&qdisc->list)) {
		if (qdisc->ops->cl_ops == NULL)
			list_del(&qdisc->list);
		else
			list_move(&qdisc->list, &cql);
	}

	/* unlink inner qdiscs from dev->qdisc_list immediately */
	list_for_each_entry(cq, &cql, list)
		list_for_each_entry_safe(q, n, &qdisc->dev->qdisc_list, list)
			if (TC_H_MAJ(q->parent) == TC_H_MAJ(cq->handle)) {
				if (q->ops->cl_ops == NULL)
					list_del_init(&q->list);
				else
					list_move_tail(&q->list, &cql);
			}
	list_for_each_entry_safe(cq, n, &cql, list)
		list_del_init(&cq->list);

	call_rcu(&qdisc->q_rcu, __qdisc_destroy);
}

void dev_activate(struct net_device *dev)
{
	/* No queueing discipline is attached to device;
	   create default one i.e. pfifo_fast for devices,
	   which need queueing and noqueue_qdisc for
	   virtual interfaces
	 */

	if (dev->qdisc_sleeping == &noop_qdisc) {
		struct Qdisc *qdisc;
		if (dev->tx_queue_len) {
			qdisc = qdisc_create_dflt(dev, &pfifo_fast_ops);
			if (qdisc == NULL) {
				printk(KERN_INFO "%s: activation failed\n", dev->name);
				return;
			}
			write_lock_bh(&qdisc_tree_lock);
			list_add_tail(&qdisc->list, &dev->qdisc_list);
			write_unlock_bh(&qdisc_tree_lock);
		} else {
			qdisc =  &noqueue_qdisc;
		}
		write_lock_bh(&qdisc_tree_lock);
		dev->qdisc_sleeping = qdisc;
		write_unlock_bh(&qdisc_tree_lock);
	}

	if (!netif_carrier_ok(dev))
		/* Delay activation until next carrier-on event */
		return;

	spin_lock_bh(&dev->queue_lock);
	rcu_assign_pointer(dev->qdisc, dev->qdisc_sleeping);
	if (dev->qdisc != &noqueue_qdisc) {
		dev->trans_start = jiffies;
		dev_watchdog_up(dev);
	}
	spin_unlock_bh(&dev->queue_lock);
}

void dev_deactivate(struct net_device *dev)
{
	struct Qdisc *qdisc;

	spin_lock_bh(&dev->queue_lock);
	qdisc = dev->qdisc;
	dev->qdisc = &noop_qdisc;

	qdisc_reset(qdisc);

	spin_unlock_bh(&dev->queue_lock);

	dev_watchdog_down(dev);

	while (test_bit(__LINK_STATE_SCHED, &dev->state))
		yield();

	spin_unlock_wait(&dev->xmit_lock);
}

void dev_init_scheduler(struct net_device *dev)
{
	qdisc_lock_tree(dev);
	dev->qdisc = &noop_qdisc;
	dev->qdisc_sleeping = &noop_qdisc;
	INIT_LIST_HEAD(&dev->qdisc_list);
	qdisc_unlock_tree(dev);

	dev_watchdog_init(dev);
}

void dev_shutdown(struct net_device *dev)
{
	struct Qdisc *qdisc;

	qdisc_lock_tree(dev);
	qdisc = dev->qdisc_sleeping;
	dev->qdisc = &noop_qdisc;
	dev->qdisc_sleeping = &noop_qdisc;
	qdisc_destroy(qdisc);
#if defined(CONFIG_NET_SCH_INGRESS) || defined(CONFIG_NET_SCH_INGRESS_MODULE)
        if ((qdisc = dev->qdisc_ingress) != NULL) {
		dev->qdisc_ingress = NULL;
		qdisc_destroy(qdisc);
        }
#endif
	BUG_TRAP(!timer_pending(&dev->watchdog_timer));
	qdisc_unlock_tree(dev);
}

EXPORT_SYMBOL(__netdev_watchdog_up);
EXPORT_SYMBOL(netif_carrier_on);
EXPORT_SYMBOL(netif_carrier_off);
EXPORT_SYMBOL(noop_qdisc);
EXPORT_SYMBOL(noop_qdisc_ops);
EXPORT_SYMBOL(qdisc_create_dflt);
EXPORT_SYMBOL(qdisc_alloc);
EXPORT_SYMBOL(qdisc_destroy);
EXPORT_SYMBOL(qdisc_reset);
EXPORT_SYMBOL(qdisc_restart);
EXPORT_SYMBOL(qdisc_lock_tree);
EXPORT_SYMBOL(qdisc_unlock_tree);
