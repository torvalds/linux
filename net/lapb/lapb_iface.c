// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	LAPB release 002
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	History
 *	LAPB 001	Jonathan Naylor	Started Coding
 *	LAPB 002	Jonathan Naylor	New timer architecture.
 *	2000-10-29	Henner Eisen	lapb_data_indication() return status.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <net/lapb.h>

static LIST_HEAD(lapb_list);
static DEFINE_RWLOCK(lapb_list_lock);

/*
 *	Free an allocated lapb control block.
 */
static void lapb_free_cb(struct lapb_cb *lapb)
{
	kfree(lapb);
}

static __inline__ void lapb_hold(struct lapb_cb *lapb)
{
	refcount_inc(&lapb->refcnt);
}

static __inline__ void lapb_put(struct lapb_cb *lapb)
{
	if (refcount_dec_and_test(&lapb->refcnt))
		lapb_free_cb(lapb);
}

/*
 *	Socket removal during an interrupt is now safe.
 */
static void __lapb_remove_cb(struct lapb_cb *lapb)
{
	if (lapb->node.next) {
		list_del(&lapb->node);
		lapb_put(lapb);
	}
}

/*
 *	Add a socket to the bound sockets list.
 */
static void __lapb_insert_cb(struct lapb_cb *lapb)
{
	list_add(&lapb->node, &lapb_list);
	lapb_hold(lapb);
}

static struct lapb_cb *__lapb_devtostruct(struct net_device *dev)
{
	struct list_head *entry;
	struct lapb_cb *lapb, *use = NULL;

	list_for_each(entry, &lapb_list) {
		lapb = list_entry(entry, struct lapb_cb, node);
		if (lapb->dev == dev) {
			use = lapb;
			break;
		}
	}

	if (use)
		lapb_hold(use);

	return use;
}

static struct lapb_cb *lapb_devtostruct(struct net_device *dev)
{
	struct lapb_cb *rc;

	read_lock_bh(&lapb_list_lock);
	rc = __lapb_devtostruct(dev);
	read_unlock_bh(&lapb_list_lock);

	return rc;
}
/*
 *	Create an empty LAPB control block.
 */
static struct lapb_cb *lapb_create_cb(void)
{
	struct lapb_cb *lapb = kzalloc(sizeof(*lapb), GFP_ATOMIC);

	if (!lapb)
		goto out;

	skb_queue_head_init(&lapb->write_queue);
	skb_queue_head_init(&lapb->ack_queue);

	timer_setup(&lapb->t1timer, NULL, 0);
	timer_setup(&lapb->t2timer, NULL, 0);
	lapb->t1timer_stop = true;
	lapb->t2timer_stop = true;

	lapb->t1      = LAPB_DEFAULT_T1;
	lapb->t2      = LAPB_DEFAULT_T2;
	lapb->n2      = LAPB_DEFAULT_N2;
	lapb->mode    = LAPB_DEFAULT_MODE;
	lapb->window  = LAPB_DEFAULT_WINDOW;
	lapb->state   = LAPB_STATE_0;

	spin_lock_init(&lapb->lock);
	refcount_set(&lapb->refcnt, 1);
out:
	return lapb;
}

int lapb_register(struct net_device *dev,
		  const struct lapb_register_struct *callbacks)
{
	struct lapb_cb *lapb;
	int rc = LAPB_BADTOKEN;

	write_lock_bh(&lapb_list_lock);

	lapb = __lapb_devtostruct(dev);
	if (lapb) {
		lapb_put(lapb);
		goto out;
	}

	lapb = lapb_create_cb();
	rc = LAPB_NOMEM;
	if (!lapb)
		goto out;

	lapb->dev       = dev;
	lapb->callbacks = callbacks;

	__lapb_insert_cb(lapb);

	lapb_start_t1timer(lapb);

	rc = LAPB_OK;
out:
	write_unlock_bh(&lapb_list_lock);
	return rc;
}
EXPORT_SYMBOL(lapb_register);

int lapb_unregister(struct net_device *dev)
{
	struct lapb_cb *lapb;
	int rc = LAPB_BADTOKEN;

	write_lock_bh(&lapb_list_lock);
	lapb = __lapb_devtostruct(dev);
	if (!lapb)
		goto out;
	lapb_put(lapb);

	/* Wait for other refs to "lapb" to drop */
	while (refcount_read(&lapb->refcnt) > 2)
		usleep_range(1, 10);

	spin_lock_bh(&lapb->lock);

	lapb_stop_t1timer(lapb);
	lapb_stop_t2timer(lapb);

	lapb_clear_queues(lapb);

	spin_unlock_bh(&lapb->lock);

	/* Wait for running timers to stop */
	del_timer_sync(&lapb->t1timer);
	del_timer_sync(&lapb->t2timer);

	__lapb_remove_cb(lapb);

	lapb_put(lapb);
	rc = LAPB_OK;
out:
	write_unlock_bh(&lapb_list_lock);
	return rc;
}
EXPORT_SYMBOL(lapb_unregister);

int lapb_getparms(struct net_device *dev, struct lapb_parms_struct *parms)
{
	int rc = LAPB_BADTOKEN;
	struct lapb_cb *lapb = lapb_devtostruct(dev);

	if (!lapb)
		goto out;

	spin_lock_bh(&lapb->lock);

	parms->t1      = lapb->t1 / HZ;
	parms->t2      = lapb->t2 / HZ;
	parms->n2      = lapb->n2;
	parms->n2count = lapb->n2count;
	parms->state   = lapb->state;
	parms->window  = lapb->window;
	parms->mode    = lapb->mode;

	if (!timer_pending(&lapb->t1timer))
		parms->t1timer = 0;
	else
		parms->t1timer = (lapb->t1timer.expires - jiffies) / HZ;

	if (!timer_pending(&lapb->t2timer))
		parms->t2timer = 0;
	else
		parms->t2timer = (lapb->t2timer.expires - jiffies) / HZ;

	spin_unlock_bh(&lapb->lock);
	lapb_put(lapb);
	rc = LAPB_OK;
out:
	return rc;
}
EXPORT_SYMBOL(lapb_getparms);

int lapb_setparms(struct net_device *dev, struct lapb_parms_struct *parms)
{
	int rc = LAPB_BADTOKEN;
	struct lapb_cb *lapb = lapb_devtostruct(dev);

	if (!lapb)
		goto out;

	spin_lock_bh(&lapb->lock);

	rc = LAPB_INVALUE;
	if (parms->t1 < 1 || parms->t2 < 1 || parms->n2 < 1)
		goto out_put;

	if (lapb->state == LAPB_STATE_0) {
		if (parms->mode & LAPB_EXTENDED) {
			if (parms->window < 1 || parms->window > 127)
				goto out_put;
		} else {
			if (parms->window < 1 || parms->window > 7)
				goto out_put;
		}
		lapb->mode    = parms->mode;
		lapb->window  = parms->window;
	}

	lapb->t1    = parms->t1 * HZ;
	lapb->t2    = parms->t2 * HZ;
	lapb->n2    = parms->n2;

	rc = LAPB_OK;
out_put:
	spin_unlock_bh(&lapb->lock);
	lapb_put(lapb);
out:
	return rc;
}
EXPORT_SYMBOL(lapb_setparms);

int lapb_connect_request(struct net_device *dev)
{
	struct lapb_cb *lapb = lapb_devtostruct(dev);
	int rc = LAPB_BADTOKEN;

	if (!lapb)
		goto out;

	spin_lock_bh(&lapb->lock);

	rc = LAPB_OK;
	if (lapb->state == LAPB_STATE_1)
		goto out_put;

	rc = LAPB_CONNECTED;
	if (lapb->state == LAPB_STATE_3 || lapb->state == LAPB_STATE_4)
		goto out_put;

	lapb_establish_data_link(lapb);

	lapb_dbg(0, "(%p) S0 -> S1\n", lapb->dev);
	lapb->state = LAPB_STATE_1;

	rc = LAPB_OK;
out_put:
	spin_unlock_bh(&lapb->lock);
	lapb_put(lapb);
out:
	return rc;
}
EXPORT_SYMBOL(lapb_connect_request);

static int __lapb_disconnect_request(struct lapb_cb *lapb)
{
	switch (lapb->state) {
	case LAPB_STATE_0:
		return LAPB_NOTCONNECTED;

	case LAPB_STATE_1:
		lapb_dbg(1, "(%p) S1 TX DISC(1)\n", lapb->dev);
		lapb_dbg(0, "(%p) S1 -> S0\n", lapb->dev);
		lapb_send_control(lapb, LAPB_DISC, LAPB_POLLON, LAPB_COMMAND);
		lapb->state = LAPB_STATE_0;
		lapb_start_t1timer(lapb);
		return LAPB_NOTCONNECTED;

	case LAPB_STATE_2:
		return LAPB_OK;
	}

	lapb_clear_queues(lapb);
	lapb->n2count = 0;
	lapb_send_control(lapb, LAPB_DISC, LAPB_POLLON, LAPB_COMMAND);
	lapb_start_t1timer(lapb);
	lapb_stop_t2timer(lapb);
	lapb->state = LAPB_STATE_2;

	lapb_dbg(1, "(%p) S3 DISC(1)\n", lapb->dev);
	lapb_dbg(0, "(%p) S3 -> S2\n", lapb->dev);

	return LAPB_OK;
}

int lapb_disconnect_request(struct net_device *dev)
{
	struct lapb_cb *lapb = lapb_devtostruct(dev);
	int rc = LAPB_BADTOKEN;

	if (!lapb)
		goto out;

	spin_lock_bh(&lapb->lock);

	rc = __lapb_disconnect_request(lapb);

	spin_unlock_bh(&lapb->lock);
	lapb_put(lapb);
out:
	return rc;
}
EXPORT_SYMBOL(lapb_disconnect_request);

int lapb_data_request(struct net_device *dev, struct sk_buff *skb)
{
	struct lapb_cb *lapb = lapb_devtostruct(dev);
	int rc = LAPB_BADTOKEN;

	if (!lapb)
		goto out;

	spin_lock_bh(&lapb->lock);

	rc = LAPB_NOTCONNECTED;
	if (lapb->state != LAPB_STATE_3 && lapb->state != LAPB_STATE_4)
		goto out_put;

	skb_queue_tail(&lapb->write_queue, skb);
	lapb_kick(lapb);
	rc = LAPB_OK;
out_put:
	spin_unlock_bh(&lapb->lock);
	lapb_put(lapb);
out:
	return rc;
}
EXPORT_SYMBOL(lapb_data_request);

int lapb_data_received(struct net_device *dev, struct sk_buff *skb)
{
	struct lapb_cb *lapb = lapb_devtostruct(dev);
	int rc = LAPB_BADTOKEN;

	if (lapb) {
		spin_lock_bh(&lapb->lock);
		lapb_data_input(lapb, skb);
		spin_unlock_bh(&lapb->lock);
		lapb_put(lapb);
		rc = LAPB_OK;
	}

	return rc;
}
EXPORT_SYMBOL(lapb_data_received);

void lapb_connect_confirmation(struct lapb_cb *lapb, int reason)
{
	if (lapb->callbacks->connect_confirmation)
		lapb->callbacks->connect_confirmation(lapb->dev, reason);
}

void lapb_connect_indication(struct lapb_cb *lapb, int reason)
{
	if (lapb->callbacks->connect_indication)
		lapb->callbacks->connect_indication(lapb->dev, reason);
}

void lapb_disconnect_confirmation(struct lapb_cb *lapb, int reason)
{
	if (lapb->callbacks->disconnect_confirmation)
		lapb->callbacks->disconnect_confirmation(lapb->dev, reason);
}

void lapb_disconnect_indication(struct lapb_cb *lapb, int reason)
{
	if (lapb->callbacks->disconnect_indication)
		lapb->callbacks->disconnect_indication(lapb->dev, reason);
}

int lapb_data_indication(struct lapb_cb *lapb, struct sk_buff *skb)
{
	if (lapb->callbacks->data_indication)
		return lapb->callbacks->data_indication(lapb->dev, skb);

	kfree_skb(skb);
	return NET_RX_SUCCESS; /* For now; must be != NET_RX_DROP */
}

int lapb_data_transmit(struct lapb_cb *lapb, struct sk_buff *skb)
{
	int used = 0;

	if (lapb->callbacks->data_transmit) {
		lapb->callbacks->data_transmit(lapb->dev, skb);
		used = 1;
	}

	return used;
}

/* Handle device status changes. */
static int lapb_device_event(struct notifier_block *this, unsigned long event,
			     void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct lapb_cb *lapb;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	if (dev->type != ARPHRD_X25)
		return NOTIFY_DONE;

	lapb = lapb_devtostruct(dev);
	if (!lapb)
		return NOTIFY_DONE;

	spin_lock_bh(&lapb->lock);

	switch (event) {
	case NETDEV_UP:
		lapb_dbg(0, "(%p) Interface up: %s\n", dev, dev->name);

		if (netif_carrier_ok(dev)) {
			lapb_dbg(0, "(%p): Carrier is already up: %s\n", dev,
				 dev->name);
			if (lapb->mode & LAPB_DCE) {
				lapb_start_t1timer(lapb);
			} else {
				if (lapb->state == LAPB_STATE_0) {
					lapb->state = LAPB_STATE_1;
					lapb_establish_data_link(lapb);
				}
			}
		}
		break;
	case NETDEV_GOING_DOWN:
		if (netif_carrier_ok(dev))
			__lapb_disconnect_request(lapb);
		break;
	case NETDEV_DOWN:
		lapb_dbg(0, "(%p) Interface down: %s\n", dev, dev->name);
		lapb_dbg(0, "(%p) S%d -> S0\n", dev, lapb->state);
		lapb_clear_queues(lapb);
		lapb->state = LAPB_STATE_0;
		lapb->n2count   = 0;
		lapb_stop_t1timer(lapb);
		lapb_stop_t2timer(lapb);
		break;
	case NETDEV_CHANGE:
		if (netif_carrier_ok(dev)) {
			lapb_dbg(0, "(%p): Carrier detected: %s\n", dev,
				 dev->name);
			if (lapb->mode & LAPB_DCE) {
				lapb_start_t1timer(lapb);
			} else {
				if (lapb->state == LAPB_STATE_0) {
					lapb->state = LAPB_STATE_1;
					lapb_establish_data_link(lapb);
				}
			}
		} else {
			lapb_dbg(0, "(%p) Carrier lost: %s\n", dev, dev->name);
			lapb_dbg(0, "(%p) S%d -> S0\n", dev, lapb->state);
			lapb_clear_queues(lapb);
			lapb->state = LAPB_STATE_0;
			lapb->n2count   = 0;
			lapb_stop_t1timer(lapb);
			lapb_stop_t2timer(lapb);
		}
		break;
	}

	spin_unlock_bh(&lapb->lock);
	lapb_put(lapb);
	return NOTIFY_DONE;
}

static struct notifier_block lapb_dev_notifier = {
	.notifier_call = lapb_device_event,
};

static int __init lapb_init(void)
{
	return register_netdevice_notifier(&lapb_dev_notifier);
}

static void __exit lapb_exit(void)
{
	WARN_ON(!list_empty(&lapb_list));

	unregister_netdevice_notifier(&lapb_dev_notifier);
}

MODULE_AUTHOR("Jonathan Naylor <g4klx@g4klx.demon.co.uk>");
MODULE_DESCRIPTION("The X.25 Link Access Procedure B link layer protocol");
MODULE_LICENSE("GPL");

module_init(lapb_init);
module_exit(lapb_exit);
