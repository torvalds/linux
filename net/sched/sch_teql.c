/* net/sched/sch_teql.c	"True" (or "trivial") link equalizer.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/moduleparam.h>
#include <net/dst.h>
#include <net/neighbour.h>
#include <net/pkt_sched.h>

/*
   How to setup it.
   ----------------

   After loading this module you will find a new device teqlN
   and new qdisc with the same name. To join a slave to the equalizer
   you should just set this qdisc on a device f.e.

   # tc qdisc add dev eth0 root teql0
   # tc qdisc add dev eth1 root teql0

   That's all. Full PnP 8)

   Applicability.
   --------------

   1. Slave devices MUST be active devices, i.e., they must raise the tbusy
      signal and generate EOI events. If you want to equalize virtual devices
      like tunnels, use a normal eql device.
   2. This device puts no limitations on physical slave characteristics
      f.e. it will equalize 9600baud line and 100Mb ethernet perfectly :-)
      Certainly, large difference in link speeds will make the resulting
      eqalized link unusable, because of huge packet reordering.
      I estimate an upper useful difference as ~10 times.
   3. If the slave requires address resolution, only protocols using
      neighbour cache (IPv4/IPv6) will work over the equalized link.
      Other protocols are still allowed to use the slave device directly,
      which will not break load balancing, though native slave
      traffic will have the highest priority.  */

struct teql_master {
	struct Qdisc_ops qops;
	struct net_device *dev;
	struct Qdisc *slaves;
	struct list_head master_list;
	unsigned long	tx_bytes;
	unsigned long	tx_packets;
	unsigned long	tx_errors;
	unsigned long	tx_dropped;
};

struct teql_sched_data {
	struct Qdisc *next;
	struct teql_master *m;
	struct neighbour *ncache;
	struct sk_buff_head q;
};

#define NEXT_SLAVE(q) (((struct teql_sched_data *)qdisc_priv(q))->next)

#define FMASK (IFF_BROADCAST | IFF_POINTOPOINT)

/* "teql*" qdisc routines */

static int
teql_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);
	struct teql_sched_data *q = qdisc_priv(sch);

	if (q->q.qlen < dev->tx_queue_len) {
		__skb_queue_tail(&q->q, skb);
		return NET_XMIT_SUCCESS;
	}

	return qdisc_drop(skb, sch);
}

static struct sk_buff *
teql_dequeue(struct Qdisc *sch)
{
	struct teql_sched_data *dat = qdisc_priv(sch);
	struct netdev_queue *dat_queue;
	struct sk_buff *skb;

	skb = __skb_dequeue(&dat->q);
	dat_queue = netdev_get_tx_queue(dat->m->dev, 0);
	if (skb == NULL) {
		struct net_device *m = qdisc_dev(dat_queue->qdisc);
		if (m) {
			dat->m->slaves = sch;
			netif_wake_queue(m);
		}
	} else {
		qdisc_bstats_update(sch, skb);
	}
	sch->q.qlen = dat->q.qlen + dat_queue->qdisc->q.qlen;
	return skb;
}

static struct sk_buff *
teql_peek(struct Qdisc *sch)
{
	/* teql is meant to be used as root qdisc */
	return NULL;
}

static inline void
teql_neigh_release(struct neighbour *n)
{
	if (n)
		neigh_release(n);
}

static void
teql_reset(struct Qdisc *sch)
{
	struct teql_sched_data *dat = qdisc_priv(sch);

	skb_queue_purge(&dat->q);
	sch->q.qlen = 0;
	teql_neigh_release(xchg(&dat->ncache, NULL));
}

static void
teql_destroy(struct Qdisc *sch)
{
	struct Qdisc *q, *prev;
	struct teql_sched_data *dat = qdisc_priv(sch);
	struct teql_master *master = dat->m;

	prev = master->slaves;
	if (prev) {
		do {
			q = NEXT_SLAVE(prev);
			if (q == sch) {
				NEXT_SLAVE(prev) = NEXT_SLAVE(q);
				if (q == master->slaves) {
					master->slaves = NEXT_SLAVE(q);
					if (q == master->slaves) {
						struct netdev_queue *txq;
						spinlock_t *root_lock;

						txq = netdev_get_tx_queue(master->dev, 0);
						master->slaves = NULL;

						root_lock = qdisc_root_sleeping_lock(txq->qdisc);
						spin_lock_bh(root_lock);
						qdisc_reset(txq->qdisc);
						spin_unlock_bh(root_lock);
					}
				}
				skb_queue_purge(&dat->q);
				teql_neigh_release(xchg(&dat->ncache, NULL));
				break;
			}

		} while ((prev = q) != master->slaves);
	}
}

static int teql_qdisc_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct net_device *dev = qdisc_dev(sch);
	struct teql_master *m = (struct teql_master *)sch->ops;
	struct teql_sched_data *q = qdisc_priv(sch);

	if (dev->hard_header_len > m->dev->hard_header_len)
		return -EINVAL;

	if (m->dev == dev)
		return -ELOOP;

	q->m = m;

	skb_queue_head_init(&q->q);

	if (m->slaves) {
		if (m->dev->flags & IFF_UP) {
			if ((m->dev->flags & IFF_POINTOPOINT &&
			     !(dev->flags & IFF_POINTOPOINT)) ||
			    (m->dev->flags & IFF_BROADCAST &&
			     !(dev->flags & IFF_BROADCAST)) ||
			    (m->dev->flags & IFF_MULTICAST &&
			     !(dev->flags & IFF_MULTICAST)) ||
			    dev->mtu < m->dev->mtu)
				return -EINVAL;
		} else {
			if (!(dev->flags&IFF_POINTOPOINT))
				m->dev->flags &= ~IFF_POINTOPOINT;
			if (!(dev->flags&IFF_BROADCAST))
				m->dev->flags &= ~IFF_BROADCAST;
			if (!(dev->flags&IFF_MULTICAST))
				m->dev->flags &= ~IFF_MULTICAST;
			if (dev->mtu < m->dev->mtu)
				m->dev->mtu = dev->mtu;
		}
		q->next = NEXT_SLAVE(m->slaves);
		NEXT_SLAVE(m->slaves) = sch;
	} else {
		q->next = sch;
		m->slaves = sch;
		m->dev->mtu = dev->mtu;
		m->dev->flags = (m->dev->flags&~FMASK)|(dev->flags&FMASK);
	}
	return 0;
}


static int
__teql_resolve(struct sk_buff *skb, struct sk_buff *skb_res,
	       struct net_device *dev, struct netdev_queue *txq,
	       struct neighbour *mn)
{
	struct teql_sched_data *q = qdisc_priv(txq->qdisc);
	struct neighbour *n = q->ncache;

	if (mn->tbl == NULL)
		return -EINVAL;
	if (n && n->tbl == mn->tbl &&
	    memcmp(n->primary_key, mn->primary_key, mn->tbl->key_len) == 0) {
		atomic_inc(&n->refcnt);
	} else {
		n = __neigh_lookup_errno(mn->tbl, mn->primary_key, dev);
		if (IS_ERR(n))
			return PTR_ERR(n);
	}
	if (neigh_event_send(n, skb_res) == 0) {
		int err;
		char haddr[MAX_ADDR_LEN];

		neigh_ha_snapshot(haddr, n, dev);
		err = dev_hard_header(skb, dev, ntohs(skb->protocol), haddr,
				      NULL, skb->len);

		if (err < 0) {
			neigh_release(n);
			return -EINVAL;
		}
		teql_neigh_release(xchg(&q->ncache, n));
		return 0;
	}
	neigh_release(n);
	return (skb_res == NULL) ? -EAGAIN : 1;
}

static inline int teql_resolve(struct sk_buff *skb,
			       struct sk_buff *skb_res,
			       struct net_device *dev,
			       struct netdev_queue *txq)
{
	struct dst_entry *dst = skb_dst(skb);
	struct neighbour *mn;
	int res;

	if (txq->qdisc == &noop_qdisc)
		return -ENODEV;

	if (!dev->header_ops || !dst)
		return 0;

	rcu_read_lock();
	mn = dst_get_neighbour_noref(dst);
	res = mn ? __teql_resolve(skb, skb_res, dev, txq, mn) : 0;
	rcu_read_unlock();

	return res;
}

static netdev_tx_t teql_master_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct teql_master *master = netdev_priv(dev);
	struct Qdisc *start, *q;
	int busy;
	int nores;
	int subq = skb_get_queue_mapping(skb);
	struct sk_buff *skb_res = NULL;

	start = master->slaves;

restart:
	nores = 0;
	busy = 0;

	q = start;
	if (!q)
		goto drop;

	do {
		struct net_device *slave = qdisc_dev(q);
		struct netdev_queue *slave_txq = netdev_get_tx_queue(slave, 0);
		const struct net_device_ops *slave_ops = slave->netdev_ops;

		if (slave_txq->qdisc_sleeping != q)
			continue;
		if (netif_xmit_stopped(netdev_get_tx_queue(slave, subq)) ||
		    !netif_running(slave)) {
			busy = 1;
			continue;
		}

		switch (teql_resolve(skb, skb_res, slave, slave_txq)) {
		case 0:
			if (__netif_tx_trylock(slave_txq)) {
				unsigned int length = qdisc_pkt_len(skb);

				if (!netif_xmit_frozen_or_stopped(slave_txq) &&
				    slave_ops->ndo_start_xmit(skb, slave) == NETDEV_TX_OK) {
					txq_trans_update(slave_txq);
					__netif_tx_unlock(slave_txq);
					master->slaves = NEXT_SLAVE(q);
					netif_wake_queue(dev);
					master->tx_packets++;
					master->tx_bytes += length;
					return NETDEV_TX_OK;
				}
				__netif_tx_unlock(slave_txq);
			}
			if (netif_xmit_stopped(netdev_get_tx_queue(dev, 0)))
				busy = 1;
			break;
		case 1:
			master->slaves = NEXT_SLAVE(q);
			return NETDEV_TX_OK;
		default:
			nores = 1;
			break;
		}
		__skb_pull(skb, skb_network_offset(skb));
	} while ((q = NEXT_SLAVE(q)) != start);

	if (nores && skb_res == NULL) {
		skb_res = skb;
		goto restart;
	}

	if (busy) {
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}
	master->tx_errors++;

drop:
	master->tx_dropped++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int teql_master_open(struct net_device *dev)
{
	struct Qdisc *q;
	struct teql_master *m = netdev_priv(dev);
	int mtu = 0xFFFE;
	unsigned int flags = IFF_NOARP | IFF_MULTICAST;

	if (m->slaves == NULL)
		return -EUNATCH;

	flags = FMASK;

	q = m->slaves;
	do {
		struct net_device *slave = qdisc_dev(q);

		if (slave == NULL)
			return -EUNATCH;

		if (slave->mtu < mtu)
			mtu = slave->mtu;
		if (slave->hard_header_len > LL_MAX_HEADER)
			return -EINVAL;

		/* If all the slaves are BROADCAST, master is BROADCAST
		   If all the slaves are PtP, master is PtP
		   Otherwise, master is NBMA.
		 */
		if (!(slave->flags&IFF_POINTOPOINT))
			flags &= ~IFF_POINTOPOINT;
		if (!(slave->flags&IFF_BROADCAST))
			flags &= ~IFF_BROADCAST;
		if (!(slave->flags&IFF_MULTICAST))
			flags &= ~IFF_MULTICAST;
	} while ((q = NEXT_SLAVE(q)) != m->slaves);

	m->dev->mtu = mtu;
	m->dev->flags = (m->dev->flags&~FMASK) | flags;
	netif_start_queue(m->dev);
	return 0;
}

static int teql_master_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static struct rtnl_link_stats64 *teql_master_stats64(struct net_device *dev,
						     struct rtnl_link_stats64 *stats)
{
	struct teql_master *m = netdev_priv(dev);

	stats->tx_packets	= m->tx_packets;
	stats->tx_bytes		= m->tx_bytes;
	stats->tx_errors	= m->tx_errors;
	stats->tx_dropped	= m->tx_dropped;
	return stats;
}

static int teql_master_mtu(struct net_device *dev, int new_mtu)
{
	struct teql_master *m = netdev_priv(dev);
	struct Qdisc *q;

	if (new_mtu < 68)
		return -EINVAL;

	q = m->slaves;
	if (q) {
		do {
			if (new_mtu > qdisc_dev(q)->mtu)
				return -EINVAL;
		} while ((q = NEXT_SLAVE(q)) != m->slaves);
	}

	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops teql_netdev_ops = {
	.ndo_open	= teql_master_open,
	.ndo_stop	= teql_master_close,
	.ndo_start_xmit	= teql_master_xmit,
	.ndo_get_stats64 = teql_master_stats64,
	.ndo_change_mtu	= teql_master_mtu,
};

static __init void teql_master_setup(struct net_device *dev)
{
	struct teql_master *master = netdev_priv(dev);
	struct Qdisc_ops *ops = &master->qops;

	master->dev	= dev;
	ops->priv_size  = sizeof(struct teql_sched_data);

	ops->enqueue	=	teql_enqueue;
	ops->dequeue	=	teql_dequeue;
	ops->peek	=	teql_peek;
	ops->init	=	teql_qdisc_init;
	ops->reset	=	teql_reset;
	ops->destroy	=	teql_destroy;
	ops->owner	=	THIS_MODULE;

	dev->netdev_ops =       &teql_netdev_ops;
	dev->type		= ARPHRD_VOID;
	dev->mtu		= 1500;
	dev->tx_queue_len	= 100;
	dev->flags		= IFF_NOARP;
	dev->hard_header_len	= LL_MAX_HEADER;
	dev->priv_flags		&= ~IFF_XMIT_DST_RELEASE;
}

static LIST_HEAD(master_dev_list);
static int max_equalizers = 1;
module_param(max_equalizers, int, 0);
MODULE_PARM_DESC(max_equalizers, "Max number of link equalizers");

static int __init teql_init(void)
{
	int i;
	int err = -ENODEV;

	for (i = 0; i < max_equalizers; i++) {
		struct net_device *dev;
		struct teql_master *master;

		dev = alloc_netdev(sizeof(struct teql_master),
				  "teql%d", teql_master_setup);
		if (!dev) {
			err = -ENOMEM;
			break;
		}

		if ((err = register_netdev(dev))) {
			free_netdev(dev);
			break;
		}

		master = netdev_priv(dev);

		strlcpy(master->qops.id, dev->name, IFNAMSIZ);
		err = register_qdisc(&master->qops);

		if (err) {
			unregister_netdev(dev);
			free_netdev(dev);
			break;
		}

		list_add_tail(&master->master_list, &master_dev_list);
	}
	return i ? 0 : err;
}

static void __exit teql_exit(void)
{
	struct teql_master *master, *nxt;

	list_for_each_entry_safe(master, nxt, &master_dev_list, master_list) {

		list_del(&master->master_list);

		unregister_qdisc(&master->qops);
		unregister_netdev(master->dev);
		free_netdev(master->dev);
	}
}

module_init(teql_init);
module_exit(teql_exit);

MODULE_LICENSE("GPL");
