/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine,
 *	randomly fail to work with new releases, misbehave and/or generally
 *	screw up. It might even work.
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	X.25 001	Jonathan Naylor	  Started coding.
 *	X.25 002	Jonathan Naylor	  New timer architecture.
 *	mar/20/00	Daniela Squassoni Disabling/enabling of facilities
 *					  negotiation.
 *	2000-09-04	Henner Eisen	  dev_hold() / dev_put() for x25_neigh.
 */

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <net/x25.h>

LIST_HEAD(x25_neigh_list);
DEFINE_RWLOCK(x25_neigh_list_lock);

static void x25_t20timer_expiry(unsigned long);

static void x25_transmit_restart_confirmation(struct x25_neigh *nb);
static void x25_transmit_restart_request(struct x25_neigh *nb);

/*
 *	Linux set/reset timer routines
 */
static inline void x25_start_t20timer(struct x25_neigh *nb)
{
	mod_timer(&nb->t20timer, jiffies + nb->t20);
}

static void x25_t20timer_expiry(unsigned long param)
{
	struct x25_neigh *nb = (struct x25_neigh *)param;

	x25_transmit_restart_request(nb);

	x25_start_t20timer(nb);
}

static inline void x25_stop_t20timer(struct x25_neigh *nb)
{
	del_timer(&nb->t20timer);
}

static inline int x25_t20timer_pending(struct x25_neigh *nb)
{
	return timer_pending(&nb->t20timer);
}

/*
 *	This handles all restart and diagnostic frames.
 */
void x25_link_control(struct sk_buff *skb, struct x25_neigh *nb,
		      unsigned short frametype)
{
	struct sk_buff *skbn;
	int confirm;

	switch (frametype) {
		case X25_RESTART_REQUEST:
			confirm = !x25_t20timer_pending(nb);
			x25_stop_t20timer(nb);
			nb->state = X25_LINK_STATE_3;
			if (confirm)
				x25_transmit_restart_confirmation(nb);
			break;

		case X25_RESTART_CONFIRMATION:
			x25_stop_t20timer(nb);
			nb->state = X25_LINK_STATE_3;
			break;

		case X25_DIAGNOSTIC:
			printk(KERN_WARNING "x25: diagnostic #%d - "
			       "%02X %02X %02X\n",
			       skb->data[3], skb->data[4],
			       skb->data[5], skb->data[6]);
			break;

		default:
			printk(KERN_WARNING "x25: received unknown %02X "
			       "with LCI 000\n", frametype);
			break;
	}

	if (nb->state == X25_LINK_STATE_3)
		while ((skbn = skb_dequeue(&nb->queue)) != NULL)
			x25_send_frame(skbn, nb);
}

/*
 *	This routine is called when a Restart Request is needed
 */
static void x25_transmit_restart_request(struct x25_neigh *nb)
{
	unsigned char *dptr;
	int len = X25_MAX_L2_LEN + X25_STD_MIN_LEN + 2;
	struct sk_buff *skb = alloc_skb(len, GFP_ATOMIC);

	if (!skb)
		return;

	skb_reserve(skb, X25_MAX_L2_LEN);

	dptr = skb_put(skb, X25_STD_MIN_LEN + 2);

	*dptr++ = nb->extended ? X25_GFI_EXTSEQ : X25_GFI_STDSEQ;
	*dptr++ = 0x00;
	*dptr++ = X25_RESTART_REQUEST;
	*dptr++ = 0x00;
	*dptr++ = 0;

	skb->sk = NULL;

	x25_send_frame(skb, nb);
}

/*
 * This routine is called when a Restart Confirmation is needed
 */
static void x25_transmit_restart_confirmation(struct x25_neigh *nb)
{
	unsigned char *dptr;
	int len = X25_MAX_L2_LEN + X25_STD_MIN_LEN;
	struct sk_buff *skb = alloc_skb(len, GFP_ATOMIC);

	if (!skb)
		return;

	skb_reserve(skb, X25_MAX_L2_LEN);

	dptr = skb_put(skb, X25_STD_MIN_LEN);

	*dptr++ = nb->extended ? X25_GFI_EXTSEQ : X25_GFI_STDSEQ;
	*dptr++ = 0x00;
	*dptr++ = X25_RESTART_CONFIRMATION;

	skb->sk = NULL;

	x25_send_frame(skb, nb);
}

/*
 *	This routine is called when a Clear Request is needed outside of the context
 *	of a connected socket.
 */
void x25_transmit_clear_request(struct x25_neigh *nb, unsigned int lci,
				unsigned char cause)
{
	unsigned char *dptr;
	int len = X25_MAX_L2_LEN + X25_STD_MIN_LEN + 2;
	struct sk_buff *skb = alloc_skb(len, GFP_ATOMIC);

	if (!skb)
		return;

	skb_reserve(skb, X25_MAX_L2_LEN);

	dptr = skb_put(skb, X25_STD_MIN_LEN + 2);

	*dptr++ = ((lci >> 8) & 0x0F) | (nb->extended ?
					 X25_GFI_EXTSEQ :
					 X25_GFI_STDSEQ);
	*dptr++ = (lci >> 0) & 0xFF;
	*dptr++ = X25_CLEAR_REQUEST;
	*dptr++ = cause;
	*dptr++ = 0x00;

	skb->sk = NULL;

	x25_send_frame(skb, nb);
}

void x25_transmit_link(struct sk_buff *skb, struct x25_neigh *nb)
{
	switch (nb->state) {
		case X25_LINK_STATE_0:
			skb_queue_tail(&nb->queue, skb);
			nb->state = X25_LINK_STATE_1;
			x25_establish_link(nb);
			break;
		case X25_LINK_STATE_1:
		case X25_LINK_STATE_2:
			skb_queue_tail(&nb->queue, skb);
			break;
		case X25_LINK_STATE_3:
			x25_send_frame(skb, nb);
			break;
	}
}

/*
 *	Called when the link layer has become established.
 */
void x25_link_established(struct x25_neigh *nb)
{
	switch (nb->state) {
		case X25_LINK_STATE_0:
			nb->state = X25_LINK_STATE_2;
			break;
		case X25_LINK_STATE_1:
			x25_transmit_restart_request(nb);
			nb->state = X25_LINK_STATE_2;
			x25_start_t20timer(nb);
			break;
	}
}

/*
 *	Called when the link layer has terminated, or an establishment
 *	request has failed.
 */

void x25_link_terminated(struct x25_neigh *nb)
{
	nb->state = X25_LINK_STATE_0;
	/* Out of order: clear existing virtual calls (X.25 03/93 4.6.3) */
	x25_kill_by_neigh(nb);
}

/*
 *	Add a new device.
 */
void x25_link_device_up(struct net_device *dev)
{
	struct x25_neigh *nb = kmalloc(sizeof(*nb), GFP_ATOMIC);

	if (!nb)
		return;

	skb_queue_head_init(&nb->queue);
	setup_timer(&nb->t20timer, x25_t20timer_expiry, (unsigned long)nb);

	dev_hold(dev);
	nb->dev      = dev;
	nb->state    = X25_LINK_STATE_0;
	nb->extended = 0;
	/*
	 * Enables negotiation
	 */
	nb->global_facil_mask = X25_MASK_REVERSE |
				       X25_MASK_THROUGHPUT |
				       X25_MASK_PACKET_SIZE |
				       X25_MASK_WINDOW_SIZE;
	nb->t20      = sysctl_x25_restart_request_timeout;
	atomic_set(&nb->refcnt, 1);

	write_lock_bh(&x25_neigh_list_lock);
	list_add(&nb->node, &x25_neigh_list);
	write_unlock_bh(&x25_neigh_list_lock);
}

/**
 *	__x25_remove_neigh - remove neighbour from x25_neigh_list
 *	@nb - neigh to remove
 *
 *	Remove neighbour from x25_neigh_list. If it was there.
 *	Caller must hold x25_neigh_list_lock.
 */
static void __x25_remove_neigh(struct x25_neigh *nb)
{
	skb_queue_purge(&nb->queue);
	x25_stop_t20timer(nb);

	if (nb->node.next) {
		list_del(&nb->node);
		x25_neigh_put(nb);
	}
}

/*
 *	A device has been removed, remove its links.
 */
void x25_link_device_down(struct net_device *dev)
{
	struct x25_neigh *nb;
	struct list_head *entry, *tmp;

	write_lock_bh(&x25_neigh_list_lock);

	list_for_each_safe(entry, tmp, &x25_neigh_list) {
		nb = list_entry(entry, struct x25_neigh, node);

		if (nb->dev == dev) {
			__x25_remove_neigh(nb);
			dev_put(dev);
		}
	}

	write_unlock_bh(&x25_neigh_list_lock);
}

/*
 *	Given a device, return the neighbour address.
 */
struct x25_neigh *x25_get_neigh(struct net_device *dev)
{
	struct x25_neigh *nb, *use = NULL;
	struct list_head *entry;

	read_lock_bh(&x25_neigh_list_lock);
	list_for_each(entry, &x25_neigh_list) {
		nb = list_entry(entry, struct x25_neigh, node);

		if (nb->dev == dev) {
			use = nb;
			break;
		}
	}

	if (use)
		x25_neigh_hold(use);
	read_unlock_bh(&x25_neigh_list_lock);
	return use;
}

/*
 *	Handle the ioctls that control the subscription functions.
 */
int x25_subscr_ioctl(unsigned int cmd, void __user *arg)
{
	struct x25_subscrip_struct x25_subscr;
	struct x25_neigh *nb;
	struct net_device *dev;
	int rc = -EINVAL;

	if (cmd != SIOCX25GSUBSCRIP && cmd != SIOCX25SSUBSCRIP)
		goto out;

	rc = -EFAULT;
	if (copy_from_user(&x25_subscr, arg, sizeof(x25_subscr)))
		goto out;

	rc = -EINVAL;
	if ((dev = x25_dev_get(x25_subscr.device)) == NULL)
		goto out;

	if ((nb = x25_get_neigh(dev)) == NULL)
		goto out_dev_put;

	dev_put(dev);

	if (cmd == SIOCX25GSUBSCRIP) {
		read_lock_bh(&x25_neigh_list_lock);
		x25_subscr.extended	     = nb->extended;
		x25_subscr.global_facil_mask = nb->global_facil_mask;
		read_unlock_bh(&x25_neigh_list_lock);
		rc = copy_to_user(arg, &x25_subscr,
				  sizeof(x25_subscr)) ? -EFAULT : 0;
	} else {
		rc = -EINVAL;
		if (!(x25_subscr.extended && x25_subscr.extended != 1)) {
			rc = 0;
			write_lock_bh(&x25_neigh_list_lock);
			nb->extended	     = x25_subscr.extended;
			nb->global_facil_mask = x25_subscr.global_facil_mask;
			write_unlock_bh(&x25_neigh_list_lock);
		}
	}
	x25_neigh_put(nb);
out:
	return rc;
out_dev_put:
	dev_put(dev);
	goto out;
}


/*
 *	Release all memory associated with X.25 neighbour structures.
 */
void __exit x25_link_free(void)
{
	struct x25_neigh *nb;
	struct list_head *entry, *tmp;

	write_lock_bh(&x25_neigh_list_lock);

	list_for_each_safe(entry, tmp, &x25_neigh_list) {
		struct net_device *dev;

		nb = list_entry(entry, struct x25_neigh, node);
		dev = nb->dev;
		__x25_remove_neigh(nb);
		dev_put(dev);
	}
	write_unlock_bh(&x25_neigh_list_lock);
}
