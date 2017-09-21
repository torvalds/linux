/*
 *	LAPB release 002
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
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
EXPORT_SYMBOL(lapb_register);

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

	init_timer(&lapb->t1timer);
	init_timer(&lapb->t2timer);

	lapb->t1      = LAPB_DEFAULT_T1;
	lapb->t2      = LAPB_DEFAULT_T2;
	lapb->n2      = LAPB_DEFAULT_N2;
	lapb->mode    = LAPB_DEFAULT_MODE;
	lapb->window  = LAPB_DEFAULT_WINDOW;
	lapb->state   = LAPB_STATE_0;
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

int lapb_unregister(struct net_device *dev)
{
	struct lapb_cb *lapb;
	int rc = LAPB_BADTOKEN;

	write_lock_bh(&lapb_list_lock);
	lapb = __lapb_devtostruct(dev);
	if (!lapb)
		goto out;

	lapb_stop_t1timer(lapb);
	lapb_stop_t2timer(lapb);

	lapb_clear_queues(lapb);

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
	lapb_put(lapb);
out:
	return rc;
}
EXPORT_SYMBOL(lapb_connect_request);

int lapb_disconnect_request(struct net_device *dev)
{
	struct lapb_cb *lapb = lapb_devtostruct(dev);
	int rc = LAPB_BADTOKEN;

	if (!lapb)
		goto out;

	switch (lapb->state) {
	case LAPB_STATE_0:
		rc = LAPB_NOTCONNECTED;
		goto out_put;

	case LAPB_STATE_1:
		lapb_dbg(1, "(%p) S1 TX DISC(1)\n", lapb->dev);
		lapb_dbg(0, "(%p) S1 -> S0\n", lapb->dev);
		lapb_send_control(lapb, LAPB_DISC, LAPB_POLLON, LAPB_COMMAND);
		lapb->state = LAPB_STATE_0;
		lapb_start_t1timer(lapb);
		rc = LAPB_NOTCONNECTED;
		goto out_put;

	case LAPB_STATE_2:
		rc = LAPB_OK;
		goto out_put;
	}

	lapb_clear_queues(lapb);
	lapb->n2count = 0;
	lapb_send_control(lapb, LAPB_DISC, LAPB_POLLON, LAPB_COMMAND);
	lapb_start_t1timer(lapb);
	lapb_stop_t2timer(lapb);
	lapb->state = LAPB_STATE_2;

	lapb_dbg(1, "(%p) S3 DISC(1)\n", lapb->dev);
	lapb_dbg(0, "(%p) S3 -> S2\n", lapb->dev);

	rc = LAPB_OK;
out_put:
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

	rc = LAPB_NOTCONNECTED;
	if (lapb->state != LAPB_STATE_3 && lapb->state != LAPB_STATE_4)
		goto out_put;

	skb_queue_tail(&lapb->write_queue, skb);
	lapb_kick(lapb);
	rc = LAPB_OK;
out_put:
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
		lapb_data_input(lapb, skb);
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

static int __init lapb_init(void)
{
	return 0;
}

static void __exit lapb_exit(void)
{
	WARN_ON(!list_empty(&lapb_list));
}

MODULE_AUTHOR("Jonathan Naylor <g4klx@g4klx.demon.co.uk>");
MODULE_DESCRIPTION("The X.25 Link Access Procedure B link layer protocol");
MODULE_LICENSE("GPL");

module_init(lapb_init);
module_exit(lapb_exit);
