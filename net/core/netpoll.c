// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common framework for low-level network console, dump, and debugger code
 *
 * Sep 8 2003  Matt Mackall <mpm@selenic.com>
 *
 * based on the netconsole code from:
 *
 * Copyright (C) 2001  Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2002  Red Hat, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/string.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/inet.h>
#include <linux/interrupt.h>
#include <linux/netpoll.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/if_vlan.h>
#include <linux/udp.h>
#include <net/tcp.h>
#include <net/addrconf.h>
#include <net/ndisc.h>
#include <trace/events/napi.h>
#include <linux/kconfig.h>

#define USEC_PER_POLL	50

/*
 * carrier_timeout is netconsole-specific and only kept here to preserve the
 * netpoll.carrier_timeout module-parameter ABI. Its value is exposed to
 * netconsole through netpoll_get_carrier_timeout().
 */
static unsigned int carrier_timeout = 4;
module_param(carrier_timeout, uint, 0644);

unsigned int netpoll_get_carrier_timeout(void)
{
	return carrier_timeout;
}
EXPORT_SYMBOL_GPL(netpoll_get_carrier_timeout);

static netdev_tx_t netpoll_start_xmit(struct sk_buff *skb,
				      struct net_device *dev,
				      struct netdev_queue *txq)
{
	netdev_tx_t status = NETDEV_TX_OK;
	netdev_features_t features;

	features = netif_skb_features(skb);

	if (skb_vlan_tag_present(skb) &&
	    !vlan_hw_offload_capable(features, skb->vlan_proto)) {
		skb = __vlan_hwaccel_push_inside(skb);
		if (unlikely(!skb)) {
			/* This is actually a packet drop, but we
			 * don't want the code that calls this
			 * function to try and operate on a NULL skb.
			 */
			goto out;
		}
	}

	status = netdev_start_xmit(skb, dev, txq, false);

out:
	return status;
}

static void queue_process(struct work_struct *work)
{
	struct netpoll_info *npinfo =
		container_of(work, struct netpoll_info, tx_work.work);
	struct sk_buff *skb;
	unsigned long flags;

	while ((skb = skb_dequeue(&npinfo->txq))) {
		struct net_device *dev = skb->dev;
		struct netdev_queue *txq;
		unsigned int q_index;

		if (!netif_device_present(dev) || !netif_running(dev)) {
			kfree_skb(skb);
			continue;
		}

		local_irq_save(flags);
		/* check if skb->queue_mapping is still valid */
		q_index = skb_get_queue_mapping(skb);
		if (unlikely(q_index >= dev->real_num_tx_queues)) {
			q_index = q_index % dev->real_num_tx_queues;
			skb_set_queue_mapping(skb, q_index);
		}
		txq = netdev_get_tx_queue(dev, q_index);
		HARD_TX_LOCK(dev, txq, smp_processor_id());
		if (netif_xmit_frozen_or_stopped(txq) ||
		    !dev_xmit_complete(netpoll_start_xmit(skb, dev, txq))) {
			skb_queue_head(&npinfo->txq, skb);
			HARD_TX_UNLOCK(dev, txq);
			local_irq_restore(flags);

			schedule_delayed_work(&npinfo->tx_work, HZ/10);
			return;
		}
		HARD_TX_UNLOCK(dev, txq);
		local_irq_restore(flags);
	}
}

static int netif_local_xmit_active(struct net_device *dev)
{
	int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);

		if (netif_tx_owned(txq, smp_processor_id()))
			return 1;
	}

	return 0;
}

static void poll_one_napi(struct napi_struct *napi)
{
	int work;

	/* If we set this bit but see that it has already been set,
	 * that indicates that napi has been disabled and we need
	 * to abort this operation
	 */
	if (test_and_set_bit(NAPI_STATE_NPSVC, &napi->state))
		return;

	/* We explicitly pass the polling call a budget of 0 to
	 * indicate that we are clearing the Tx path only.
	 */
	work = napi->poll(napi, 0);
	WARN_ONCE(work, "%pS exceeded budget in poll\n", napi->poll);
	trace_napi_poll(napi, work, 0);

	clear_bit(NAPI_STATE_NPSVC, &napi->state);
}

static void poll_napi(struct net_device *dev)
{
	struct napi_struct *napi;
	int cpu = smp_processor_id();

	list_for_each_entry_rcu(napi, &dev->napi_list, dev_list) {
		if (cmpxchg(&napi->poll_owner, -1, cpu) == -1) {
			poll_one_napi(napi);
			smp_store_release(&napi->poll_owner, -1);
		}
	}
}

void netpoll_poll_dev(struct net_device *dev)
{
	struct netpoll_info *ni = rcu_dereference_bh(dev->npinfo);
	const struct net_device_ops *ops;

	/* Don't do any rx activity if the dev_lock mutex is held
	 * the dev_open/close paths use this to block netpoll activity
	 * while changing device state
	 */
	if (!ni || down_trylock(&ni->dev_lock))
		return;

	/* Some drivers will take the same locks in poll and xmit,
	 * we can't poll if local CPU is already in xmit.
	 */
	if (!netif_running(dev) || netif_local_xmit_active(dev)) {
		up(&ni->dev_lock);
		return;
	}

	ops = dev->netdev_ops;
	if (ops->ndo_poll_controller)
		ops->ndo_poll_controller(dev);

	poll_napi(dev);

	up(&ni->dev_lock);

	netpoll_zap_completion_queue();
}
EXPORT_SYMBOL(netpoll_poll_dev);

void netpoll_poll_disable(struct net_device *dev)
{
	struct netpoll_info *ni;

	might_sleep();
	ni = rtnl_dereference(dev->npinfo);
	if (ni)
		down(&ni->dev_lock);
}

void netpoll_poll_enable(struct net_device *dev)
{
	struct netpoll_info *ni;

	ni = rtnl_dereference(dev->npinfo);
	if (ni)
		up(&ni->dev_lock);
}

void netpoll_zap_completion_queue(void)
{
	unsigned long flags;
	struct softnet_data *sd = &get_cpu_var(softnet_data);

	if (sd->completion_queue) {
		struct sk_buff *clist;

		local_irq_save(flags);
		clist = sd->completion_queue;
		sd->completion_queue = NULL;
		local_irq_restore(flags);

		while (clist != NULL) {
			struct sk_buff *skb = clist;
			clist = clist->next;
			if (!skb_irq_freeable(skb)) {
				refcount_set(&skb->users, 1);
				dev_kfree_skb_any(skb); /* put this one back */
			} else {
				__kfree_skb(skb);
			}
		}
	}

	put_cpu_var(softnet_data);
}
EXPORT_SYMBOL_NS_GPL(netpoll_zap_completion_queue, "NETDEV_INTERNAL");

static int netpoll_owner_active(struct net_device *dev)
{
	struct napi_struct *napi;

	list_for_each_entry_rcu(napi, &dev->napi_list, dev_list) {
		if (READ_ONCE(napi->poll_owner) == smp_processor_id())
			return 1;
	}
	return 0;
}

/* call with IRQ disabled */
static netdev_tx_t __netpoll_send_skb(struct netpoll *np, struct sk_buff *skb)
{
	netdev_tx_t status = NETDEV_TX_BUSY;
	netdev_tx_t ret = NET_XMIT_DROP;
	struct net_device *dev;
	unsigned long tries;
	/* It is up to the caller to keep npinfo alive. */
	struct netpoll_info *npinfo;

	lockdep_assert_irqs_disabled();

	dev = np->dev;
	/* npinfo->txq belongs to np->dev, so retries must stay bound to it. */
	skb->dev = dev;
	rcu_read_lock();
	npinfo = rcu_dereference_bh(dev->npinfo);

	if (!npinfo || !netif_running(dev) || !netif_device_present(dev)) {
		dev_kfree_skb_irq(skb);
		goto out;
	}

	/* don't get messages out of order, and no recursion */
	if (skb_queue_len(&npinfo->txq) == 0 && !netpoll_owner_active(dev)) {
		struct netdev_queue *txq;

		txq = netdev_core_pick_tx(dev, skb, NULL);

		/* try until next clock tick */
		for (tries = jiffies_to_usecs(1)/USEC_PER_POLL;
		     tries > 0; --tries) {
			if (HARD_TX_TRYLOCK(dev, txq)) {
				if (!netif_xmit_stopped(txq))
					status = netpoll_start_xmit(skb, dev, txq);

				HARD_TX_UNLOCK(dev, txq);

				if (dev_xmit_complete(status))
					break;

			}

			/* tickle device maybe there is some cleanup */
			netpoll_poll_dev(np->dev);

			udelay(USEC_PER_POLL);
		}

		WARN_ONCE(!irqs_disabled(),
			"netpoll_send_skb_on_dev(): %s enabled interrupts in poll (%pS)\n",
			dev->name, dev->netdev_ops->ndo_start_xmit);

	}

	if (!dev_xmit_complete(status)) {
		skb_queue_tail(&npinfo->txq, skb);
		schedule_delayed_work(&npinfo->tx_work,0);
	}
	ret = NETDEV_TX_OK;
out:
	rcu_read_unlock();
	return ret;
}

netdev_tx_t netpoll_send_skb(struct netpoll *np, struct sk_buff *skb)
{
	unsigned long flags;
	netdev_tx_t ret;

	if (unlikely(!np)) {
		dev_kfree_skb_irq(skb);
		ret = NET_XMIT_DROP;
	} else {
		local_irq_save(flags);
		ret = __netpoll_send_skb(np, skb);
		local_irq_restore(flags);
	}
	return ret;
}
EXPORT_SYMBOL(netpoll_send_skb);

int __netpoll_setup(struct netpoll *np, struct net_device *ndev)
{
	struct netpoll_info *npinfo;
	const struct net_device_ops *ops;
	int err;

	if (ndev->priv_flags & IFF_DISABLE_NETPOLL) {
		np_err(np, "%s doesn't support polling, aborting\n",
		       ndev->name);
		err = -ENOTSUPP;
		goto out;
	}

	npinfo = rtnl_dereference(ndev->npinfo);
	if (!npinfo) {
		npinfo = kmalloc_obj(*npinfo);
		if (!npinfo) {
			err = -ENOMEM;
			goto out;
		}

		sema_init(&npinfo->dev_lock, 1);
		skb_queue_head_init(&npinfo->txq);
		INIT_DELAYED_WORK(&npinfo->tx_work, queue_process);

		refcount_set(&npinfo->refcnt, 1);

		ops = ndev->netdev_ops;
		if (ops->ndo_netpoll_setup) {
			err = ops->ndo_netpoll_setup(ndev);
			if (err)
				goto free_npinfo;
		}
	} else {
		refcount_inc(&npinfo->refcnt);
	}

	np->dev = ndev;
	strscpy(np->dev_name, ndev->name, IFNAMSIZ);

	/* last thing to do is link it to the net device structure */
	rcu_assign_pointer(ndev->npinfo, npinfo);

	return 0;

free_npinfo:
	kfree(npinfo);
out:
	return err;
}
EXPORT_SYMBOL_GPL(__netpoll_setup);

static void rcu_cleanup_netpoll_info(struct rcu_head *rcu_head)
{
	struct netpoll_info *npinfo =
			container_of(rcu_head, struct netpoll_info, rcu);

	skb_queue_purge(&npinfo->txq);
	kfree(npinfo);
}

static void __netpoll_cleanup(struct netpoll *np)
{
	struct netpoll_info *npinfo;

	npinfo = rtnl_dereference(np->dev->npinfo);
	if (!npinfo)
		return;

	/* At this point, there is a single npinfo instance per netdevice, and
	 * its refcnt tracks how many netpoll structures are linked to it. We
	 * only perform npinfo cleanup when the refcnt decrements to zero.
	 */
	if (refcount_dec_and_test(&npinfo->refcnt)) {
		const struct net_device_ops *ops;

		ops = np->dev->netdev_ops;
		if (ops->ndo_netpoll_cleanup)
			ops->ndo_netpoll_cleanup(np->dev);

		RCU_INIT_POINTER(np->dev->npinfo, NULL);
		disable_delayed_work_sync(&npinfo->tx_work);
		call_rcu(&npinfo->rcu, rcu_cleanup_netpoll_info);
	}
}

void __netpoll_free(struct netpoll *np)
{
	ASSERT_RTNL();

	/* Wait for transmitting packets to finish before freeing. */
	synchronize_net();
	__netpoll_cleanup(np);
	kfree(np);
}
EXPORT_SYMBOL_GPL(__netpoll_free);

void do_netpoll_cleanup(struct netpoll *np)
{
	__netpoll_cleanup(np);
	netdev_put(np->dev, &np->dev_tracker);
	np->dev = NULL;
}
EXPORT_SYMBOL(do_netpoll_cleanup);

void netpoll_cleanup(struct netpoll *np)
{
	rtnl_lock();
	if (!np->dev)
		goto out;
	do_netpoll_cleanup(np);
out:
	rtnl_unlock();
}
EXPORT_SYMBOL(netpoll_cleanup);
