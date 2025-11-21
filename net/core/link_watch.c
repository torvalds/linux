// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux network device link state notification
 *
 * Author:
 *     Stefan Rompf <sux@loplof.de>
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/if.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/types.h>

#include "dev.h"

enum lw_bits {
	LW_URGENT = 0,
};

static unsigned long linkwatch_flags;
static unsigned long linkwatch_nextevent;

static void linkwatch_event(struct work_struct *dummy);
static DECLARE_DELAYED_WORK(linkwatch_work, linkwatch_event);

static LIST_HEAD(lweventlist);
static DEFINE_SPINLOCK(lweventlist_lock);

static unsigned int default_operstate(const struct net_device *dev)
{
	if (netif_testing(dev))
		return IF_OPER_TESTING;

	/* Some uppers (DSA) have additional sources for being down, so
	 * first check whether lower is indeed the source of its down state.
	 */
	if (!netif_carrier_ok(dev)) {
		struct net_device *peer;
		int iflink;

		/* If called from netdev_run_todo()/linkwatch_sync_dev(),
		 * dev_net(dev) can be already freed, and RTNL is not held.
		 */
		if (dev->reg_state <= NETREG_REGISTERED)
			iflink = dev_get_iflink(dev);
		else
			iflink = dev->ifindex;

		if (iflink == dev->ifindex)
			return IF_OPER_DOWN;

		ASSERT_RTNL();
		peer = __dev_get_by_index(dev_net(dev), iflink);
		if (!peer)
			return IF_OPER_DOWN;

		return netif_carrier_ok(peer) ? IF_OPER_DOWN :
						IF_OPER_LOWERLAYERDOWN;
	}

	if (netif_dormant(dev))
		return IF_OPER_DORMANT;

	return IF_OPER_UP;
}

static void rfc2863_policy(struct net_device *dev)
{
	unsigned int operstate = default_operstate(dev);

	if (operstate == READ_ONCE(dev->operstate))
		return;

	switch(dev->link_mode) {
	case IF_LINK_MODE_TESTING:
		if (operstate == IF_OPER_UP)
			operstate = IF_OPER_TESTING;
		break;

	case IF_LINK_MODE_DORMANT:
		if (operstate == IF_OPER_UP)
			operstate = IF_OPER_DORMANT;
		break;
	case IF_LINK_MODE_DEFAULT:
	default:
		break;
	}

	WRITE_ONCE(dev->operstate, operstate);
}


void linkwatch_init_dev(struct net_device *dev)
{
	/* Handle pre-registration link state changes */
	if (!netif_carrier_ok(dev) || netif_dormant(dev) ||
	    netif_testing(dev))
		rfc2863_policy(dev);
}


static bool linkwatch_urgent_event(struct net_device *dev)
{
	if (!netif_running(dev))
		return false;

	if (dev->ifindex != dev_get_iflink(dev))
		return true;

	if (netif_is_lag_port(dev) || netif_is_lag_master(dev))
		return true;

	return netif_carrier_ok(dev) &&	qdisc_tx_changing(dev);
}


static void linkwatch_add_event(struct net_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&lweventlist_lock, flags);
	if (list_empty(&dev->link_watch_list)) {
		list_add_tail(&dev->link_watch_list, &lweventlist);
		netdev_hold(dev, &dev->linkwatch_dev_tracker, GFP_ATOMIC);
	}
	spin_unlock_irqrestore(&lweventlist_lock, flags);
}


static void linkwatch_schedule_work(int urgent)
{
	unsigned long delay = linkwatch_nextevent - jiffies;

	if (test_bit(LW_URGENT, &linkwatch_flags))
		return;

	/* Minimise down-time: drop delay for up event. */
	if (urgent) {
		if (test_and_set_bit(LW_URGENT, &linkwatch_flags))
			return;
		delay = 0;
	}

	/* If we wrap around we'll delay it by at most HZ. */
	if (delay > HZ)
		delay = 0;

	/*
	 * If urgent, schedule immediate execution; otherwise, don't
	 * override the existing timer.
	 */
	if (test_bit(LW_URGENT, &linkwatch_flags))
		mod_delayed_work(system_dfl_wq, &linkwatch_work, 0);
	else
		queue_delayed_work(system_dfl_wq, &linkwatch_work, delay);
}


static void linkwatch_do_dev(struct net_device *dev)
{
	/*
	 * Make sure the above read is complete since it can be
	 * rewritten as soon as we clear the bit below.
	 */
	smp_mb__before_atomic();

	/* We are about to handle this device,
	 * so new events can be accepted
	 */
	clear_bit(__LINK_STATE_LINKWATCH_PENDING, &dev->state);

	rfc2863_policy(dev);
	if (dev->flags & IFF_UP) {
		if (netif_carrier_ok(dev))
			dev_activate(dev);
		else
			dev_deactivate(dev);

		netif_state_change(dev);
	}
	/* Note: our callers are responsible for calling netdev_tracker_free().
	 * This is the reason we use __dev_put() instead of dev_put().
	 */
	__dev_put(dev);
}

static void __linkwatch_run_queue(int urgent_only)
{
#define MAX_DO_DEV_PER_LOOP	100

	int do_dev = MAX_DO_DEV_PER_LOOP;
	/* Use a local list here since we add non-urgent
	 * events back to the global one when called with
	 * urgent_only=1.
	 */
	LIST_HEAD(wrk);

	/* Give urgent case more budget */
	if (urgent_only)
		do_dev += MAX_DO_DEV_PER_LOOP;

	/*
	 * Limit the number of linkwatch events to one
	 * per second so that a runaway driver does not
	 * cause a storm of messages on the netlink
	 * socket.  This limit does not apply to up events
	 * while the device qdisc is down.
	 */
	if (!urgent_only)
		linkwatch_nextevent = jiffies + HZ;
	/* Limit wrap-around effect on delay. */
	else if (time_after(linkwatch_nextevent, jiffies + HZ))
		linkwatch_nextevent = jiffies;

	clear_bit(LW_URGENT, &linkwatch_flags);

	spin_lock_irq(&lweventlist_lock);
	list_splice_init(&lweventlist, &wrk);

	while (!list_empty(&wrk) && do_dev > 0) {
		struct net_device *dev;

		dev = list_first_entry(&wrk, struct net_device, link_watch_list);
		list_del_init(&dev->link_watch_list);

		if (!netif_device_present(dev) ||
		    (urgent_only && !linkwatch_urgent_event(dev))) {
			list_add_tail(&dev->link_watch_list, &lweventlist);
			continue;
		}
		/* We must free netdev tracker under
		 * the spinlock protection.
		 */
		netdev_tracker_free(dev, &dev->linkwatch_dev_tracker);
		spin_unlock_irq(&lweventlist_lock);
		netdev_lock_ops(dev);
		linkwatch_do_dev(dev);
		netdev_unlock_ops(dev);
		do_dev--;
		spin_lock_irq(&lweventlist_lock);
	}

	/* Add the remaining work back to lweventlist */
	list_splice_init(&wrk, &lweventlist);

	if (!list_empty(&lweventlist))
		linkwatch_schedule_work(0);
	spin_unlock_irq(&lweventlist_lock);
}

static bool linkwatch_clean_dev(struct net_device *dev)
{
	unsigned long flags;
	bool clean = false;

	spin_lock_irqsave(&lweventlist_lock, flags);
	if (!list_empty(&dev->link_watch_list)) {
		list_del_init(&dev->link_watch_list);
		clean = true;
		/* We must release netdev tracker under
		 * the spinlock protection.
		 */
		netdev_tracker_free(dev, &dev->linkwatch_dev_tracker);
	}
	spin_unlock_irqrestore(&lweventlist_lock, flags);

	return clean;
}

void __linkwatch_sync_dev(struct net_device *dev)
{
	netdev_ops_assert_locked(dev);

	if (linkwatch_clean_dev(dev))
		linkwatch_do_dev(dev);
}

void linkwatch_sync_dev(struct net_device *dev)
{
	if (linkwatch_clean_dev(dev)) {
		netdev_lock_ops(dev);
		linkwatch_do_dev(dev);
		netdev_unlock_ops(dev);
	}
}

/* Must be called with the rtnl semaphore held */
void linkwatch_run_queue(void)
{
	__linkwatch_run_queue(0);
}


static void linkwatch_event(struct work_struct *dummy)
{
	rtnl_lock();
	__linkwatch_run_queue(time_after(linkwatch_nextevent, jiffies));
	rtnl_unlock();
}


void linkwatch_fire_event(struct net_device *dev)
{
	bool urgent = linkwatch_urgent_event(dev);

	if (!test_and_set_bit(__LINK_STATE_LINKWATCH_PENDING, &dev->state)) {
		linkwatch_add_event(dev);
	} else if (!urgent)
		return;

	linkwatch_schedule_work(urgent);
}
EXPORT_SYMBOL(linkwatch_fire_event);
