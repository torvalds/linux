// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/export.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <net/netdev_lock.h>

#include "dev.h"

static void netdev_work_proc(struct work_struct *work);

/* @netdev_work_lock protects:
 *  - @netdev_work_list
 *  - within the list entries (struct net_device fields):
 *	- work_node
 *	- work_tracker
 *	- work_pending
 *	- work_core_pending
 */
static LIST_HEAD(netdev_work_list);
static DEFINE_SPINLOCK(netdev_work_lock);
static DECLARE_WORK(netdev_work, netdev_work_proc);

static void netdev_work_enqueue(struct net_device *dev, unsigned long events,
				unsigned long core)
{
	if (!events && !core)
		return;

	spin_lock_bh(&netdev_work_lock);
	if (list_empty(&dev->work_node)) {
		list_add_tail(&dev->work_node, &netdev_work_list);
		netdev_hold(dev, &dev->work_tracker, GFP_ATOMIC);
	}
	dev->work_pending |= events;
	dev->work_core_pending |= core;
	spin_unlock_bh(&netdev_work_lock);

	schedule_work(&netdev_work);
}

static unsigned long
netdev_work_dequeue(struct net_device *dev, unsigned long *pending,
		    unsigned long mask)
{
	unsigned long events;

	spin_lock_bh(&netdev_work_lock);
	events = *pending & mask;
	*pending &= ~events;
	if (!list_empty(&dev->work_node) &&
	    !dev->work_pending && !dev->work_core_pending) {
		list_del_init(&dev->work_node);
		netdev_put(dev, &dev->work_tracker);
	}
	spin_unlock_bh(&netdev_work_lock);

	return events;
}

void netdev_work_sched(struct net_device *dev, unsigned long events)
{
	netdev_work_enqueue(dev, events, 0);
}
EXPORT_SYMBOL(netdev_work_sched);

/**
 * netdev_work_cancel() - cancel selected work for a netdev
 * @dev: net_device
 * @mask: events to cancel
 *
 * Clear @mask from the device's work pending mask. If no work is left pending
 * the device is dequeued and its ndo_work won't be called.
 *
 * No expectations on locking, but also no guarantees provided. If the caller
 * wants to touch @dev afterwards (e.g. call the work that got canceled)
 * they have to ensure @dev does not get freed.
 *
 * Returns: the subset of @mask that was actually pending, so the caller can run
 * those events inline.
 */
unsigned long netdev_work_cancel(struct net_device *dev, unsigned long mask)
{
	return netdev_work_dequeue(dev, &dev->work_pending, mask);
}
EXPORT_SYMBOL(netdev_work_cancel);

void __netdev_work_core_sched(struct net_device *dev, unsigned long events)
{
	netdev_work_enqueue(dev, 0, events);
}

unsigned long
__netdev_work_core_cancel(struct net_device *dev, unsigned long mask)
{
	return netdev_work_dequeue(dev, &dev->work_core_pending, mask);
}

static void netdev_work_run(struct net_device *dev, unsigned long events,
			    unsigned long core)
{
	if (!netif_device_present(dev))
		return;

	if (core & NETDEV_WORK_RX_MODE)
		netif_rx_mode_run(dev);
	if (events && dev->netdev_ops->ndo_work)
		dev->netdev_ops->ndo_work(dev, events);
}

static void netdev_work_proc(struct work_struct *work)
{
	rtnl_lock();

	while (true) {
		unsigned long events = 0, core = 0;
		netdevice_tracker tracker;
		struct net_device *dev;

		spin_lock_bh(&netdev_work_lock);
		if (list_empty(&netdev_work_list)) {
			spin_unlock_bh(&netdev_work_lock);
			break;
		}
		dev = list_first_entry(&netdev_work_list, struct net_device,
				       work_node);
		/* Take a temporary reference so @dev can't be freed while we
		 * drop the lock to grab its ops lock; the work reference is
		 * only released once we claim the work below.
		 * The re-locking dance is to ensure that ops lock is enough
		 * to ensure canceling work is not racy with dequeue.
		 */
		netdev_hold(dev, &tracker, GFP_ATOMIC);
		spin_unlock_bh(&netdev_work_lock);

		netdev_lock_ops(dev);
		spin_lock_bh(&netdev_work_lock);
		if (!list_empty(&dev->work_node)) {
			list_del_init(&dev->work_node);
			core = dev->work_core_pending;
			dev->work_core_pending = 0;
			events = dev->work_pending;
			dev->work_pending = 0;
			/* We took another ref above */
			netdev_put(dev, &dev->work_tracker);

			if (!dev_isalive(dev))
				core = events = 0;
		}
		spin_unlock_bh(&netdev_work_lock);

		netdev_work_run(dev, events, core);
		netdev_unlock_ops(dev);

		netdev_put(dev, &tracker);
	}

	rtnl_unlock();
}
