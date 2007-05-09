/*
 * Linux network device link state notification
 *
 * Author:
 *     Stefan Rompf <sux@loplof.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/if.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <asm/types.h>


enum lw_bits {
	LW_RUNNING = 0,
};

static unsigned long linkwatch_flags;
static unsigned long linkwatch_nextevent;

static void linkwatch_event(struct work_struct *dummy);
static DECLARE_DELAYED_WORK(linkwatch_work, linkwatch_event);

static struct net_device *lweventlist;
static DEFINE_SPINLOCK(lweventlist_lock);

static unsigned char default_operstate(const struct net_device *dev)
{
	if (!netif_carrier_ok(dev))
		return (dev->ifindex != dev->iflink ?
			IF_OPER_LOWERLAYERDOWN : IF_OPER_DOWN);

	if (netif_dormant(dev))
		return IF_OPER_DORMANT;

	return IF_OPER_UP;
}


static void rfc2863_policy(struct net_device *dev)
{
	unsigned char operstate = default_operstate(dev);

	if (operstate == dev->operstate)
		return;

	write_lock_bh(&dev_base_lock);

	switch(dev->link_mode) {
	case IF_LINK_MODE_DORMANT:
		if (operstate == IF_OPER_UP)
			operstate = IF_OPER_DORMANT;
		break;

	case IF_LINK_MODE_DEFAULT:
	default:
		break;
	}

	dev->operstate = operstate;

	write_unlock_bh(&dev_base_lock);
}


static int linkwatch_urgent_event(struct net_device *dev)
{
	return netif_running(dev) && netif_carrier_ok(dev) &&
	       dev->qdisc != dev->qdisc_sleeping;
}


static void linkwatch_add_event(struct net_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&lweventlist_lock, flags);
	dev->link_watch_next = lweventlist;
	lweventlist = dev;
	spin_unlock_irqrestore(&lweventlist_lock, flags);
}


static void linkwatch_schedule_work(unsigned long delay)
{
	if (test_and_set_bit(LW_RUNNING, &linkwatch_flags))
		return;

	/* If we wrap around we'll delay it by at most HZ. */
	if (delay > HZ)
		delay = 0;

	schedule_delayed_work(&linkwatch_work, delay);
}


static void __linkwatch_run_queue(int urgent_only)
{
	struct net_device *next;

	/*
	 * Limit the number of linkwatch events to one
	 * per second so that a runaway driver does not
	 * cause a storm of messages on the netlink
	 * socket.  This limit does not apply to up events
	 * while the device qdisc is down.
	 */
	if (!urgent_only)
		linkwatch_nextevent = jiffies + HZ;
	clear_bit(LW_RUNNING, &linkwatch_flags);

	spin_lock_irq(&lweventlist_lock);
	next = lweventlist;
	lweventlist = NULL;
	spin_unlock_irq(&lweventlist_lock);

	while (next) {
		struct net_device *dev = next;

		next = dev->link_watch_next;

		if (urgent_only && !linkwatch_urgent_event(dev)) {
			linkwatch_add_event(dev);
			continue;
		}

		/*
		 * Make sure the above read is complete since it can be
		 * rewritten as soon as we clear the bit below.
		 */
		smp_mb__before_clear_bit();

		/* We are about to handle this device,
		 * so new events can be accepted
		 */
		clear_bit(__LINK_STATE_LINKWATCH_PENDING, &dev->state);

		rfc2863_policy(dev);
		if (dev->flags & IFF_UP) {
			if (netif_carrier_ok(dev)) {
				WARN_ON(dev->qdisc_sleeping == &noop_qdisc);
				dev_activate(dev);
			} else
				dev_deactivate(dev);

			netdev_state_change(dev);
		}

		dev_put(dev);
	}

	if (lweventlist)
		linkwatch_schedule_work(linkwatch_nextevent - jiffies);
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
	if (!test_and_set_bit(__LINK_STATE_LINKWATCH_PENDING, &dev->state)) {
		unsigned long delay;

		dev_hold(dev);

		linkwatch_add_event(dev);

		delay = linkwatch_nextevent - jiffies;

		/* Minimise down-time: drop delay for up event. */
		if (linkwatch_urgent_event(dev))
			delay = 0;

		linkwatch_schedule_work(delay);
	}
}

EXPORT_SYMBOL(linkwatch_fire_event);
