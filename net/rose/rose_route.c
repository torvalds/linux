// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright (C) Terry Dawson VK2KTJ (terry@animats.net)
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/arp.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <net/rose.h>
#include <linux/seq_file.h>
#include <linux/export.h>

static unsigned int rose_neigh_no = 1;

static struct rose_node  *rose_node_list;
static DEFINE_SPINLOCK(rose_node_list_lock);
static struct rose_neigh *rose_neigh_list;
static DEFINE_SPINLOCK(rose_neigh_list_lock);
static struct rose_route *rose_route_list;
static DEFINE_SPINLOCK(rose_route_list_lock);

struct rose_neigh *rose_loopback_neigh;

/*
 *	Add a new route to a node, and in the process add the node and the
 *	neighbour if it is new.
 */
static int __must_check rose_add_node(struct rose_route_struct *rose_route,
	struct net_device *dev)
{
	struct rose_node  *rose_node, *rose_tmpn, *rose_tmpp;
	struct rose_neigh *rose_neigh;
	int i, res = 0;

	spin_lock_bh(&rose_node_list_lock);
	spin_lock_bh(&rose_neigh_list_lock);

	rose_node = rose_node_list;
	while (rose_node != NULL) {
		if ((rose_node->mask == rose_route->mask) &&
		    (rosecmpm(&rose_route->address, &rose_node->address,
			      rose_route->mask) == 0))
			break;
		rose_node = rose_node->next;
	}

	if (rose_node != NULL && rose_node->loopback) {
		res = -EINVAL;
		goto out;
	}

	rose_neigh = rose_neigh_list;
	while (rose_neigh != NULL) {
		if (ax25cmp(&rose_route->neighbour,
			    &rose_neigh->callsign) == 0 &&
		    rose_neigh->dev == dev)
			break;
		rose_neigh = rose_neigh->next;
	}

	if (rose_neigh == NULL) {
		rose_neigh = kmalloc(sizeof(*rose_neigh), GFP_ATOMIC);
		if (rose_neigh == NULL) {
			res = -ENOMEM;
			goto out;
		}

		rose_neigh->callsign  = rose_route->neighbour;
		rose_neigh->digipeat  = NULL;
		rose_neigh->ax25      = NULL;
		rose_neigh->dev       = dev;
		rose_neigh->count     = 0;
		rose_neigh->use       = 0;
		rose_neigh->dce_mode  = 0;
		rose_neigh->loopback  = 0;
		rose_neigh->number    = rose_neigh_no++;
		rose_neigh->restarted = 0;

		skb_queue_head_init(&rose_neigh->queue);

		timer_setup(&rose_neigh->ftimer, NULL, 0);
		timer_setup(&rose_neigh->t0timer, NULL, 0);

		if (rose_route->ndigis != 0) {
			rose_neigh->digipeat =
				kmalloc(sizeof(ax25_digi), GFP_ATOMIC);
			if (rose_neigh->digipeat == NULL) {
				kfree(rose_neigh);
				res = -ENOMEM;
				goto out;
			}

			rose_neigh->digipeat->ndigi      = rose_route->ndigis;
			rose_neigh->digipeat->lastrepeat = -1;

			for (i = 0; i < rose_route->ndigis; i++) {
				rose_neigh->digipeat->calls[i]    =
					rose_route->digipeaters[i];
				rose_neigh->digipeat->repeated[i] = 0;
			}
		}

		rose_neigh->next = rose_neigh_list;
		rose_neigh_list  = rose_neigh;
	}

	/*
	 * This is a new node to be inserted into the list. Find where it needs
	 * to be inserted into the list, and insert it. We want to be sure
	 * to order the list in descending order of mask size to ensure that
	 * later when we are searching this list the first match will be the
	 * best match.
	 */
	if (rose_node == NULL) {
		rose_tmpn = rose_node_list;
		rose_tmpp = NULL;

		while (rose_tmpn != NULL) {
			if (rose_tmpn->mask > rose_route->mask) {
				rose_tmpp = rose_tmpn;
				rose_tmpn = rose_tmpn->next;
			} else {
				break;
			}
		}

		/* create new node */
		rose_node = kmalloc(sizeof(*rose_node), GFP_ATOMIC);
		if (rose_node == NULL) {
			res = -ENOMEM;
			goto out;
		}

		rose_node->address      = rose_route->address;
		rose_node->mask         = rose_route->mask;
		rose_node->count        = 1;
		rose_node->loopback     = 0;
		rose_node->neighbour[0] = rose_neigh;

		if (rose_tmpn == NULL) {
			if (rose_tmpp == NULL) {	/* Empty list */
				rose_node_list  = rose_node;
				rose_node->next = NULL;
			} else {
				rose_tmpp->next = rose_node;
				rose_node->next = NULL;
			}
		} else {
			if (rose_tmpp == NULL) {	/* 1st node */
				rose_node->next = rose_node_list;
				rose_node_list  = rose_node;
			} else {
				rose_tmpp->next = rose_node;
				rose_node->next = rose_tmpn;
			}
		}
		rose_neigh->count++;

		goto out;
	}

	/* We have space, slot it in */
	if (rose_node->count < 3) {
		rose_node->neighbour[rose_node->count] = rose_neigh;
		rose_node->count++;
		rose_neigh->count++;
	}

out:
	spin_unlock_bh(&rose_neigh_list_lock);
	spin_unlock_bh(&rose_node_list_lock);

	return res;
}

/*
 * Caller is holding rose_node_list_lock.
 */
static void rose_remove_node(struct rose_node *rose_node)
{
	struct rose_node *s;

	if ((s = rose_node_list) == rose_node) {
		rose_node_list = rose_node->next;
		kfree(rose_node);
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == rose_node) {
			s->next = rose_node->next;
			kfree(rose_node);
			return;
		}

		s = s->next;
	}
}

/*
 * Caller is holding rose_neigh_list_lock.
 */
static void rose_remove_neigh(struct rose_neigh *rose_neigh)
{
	struct rose_neigh *s;

	rose_stop_ftimer(rose_neigh);
	rose_stop_t0timer(rose_neigh);

	skb_queue_purge(&rose_neigh->queue);

	if ((s = rose_neigh_list) == rose_neigh) {
		rose_neigh_list = rose_neigh->next;
		if (rose_neigh->ax25)
			ax25_cb_put(rose_neigh->ax25);
		kfree(rose_neigh->digipeat);
		kfree(rose_neigh);
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == rose_neigh) {
			s->next = rose_neigh->next;
			if (rose_neigh->ax25)
				ax25_cb_put(rose_neigh->ax25);
			kfree(rose_neigh->digipeat);
			kfree(rose_neigh);
			return;
		}

		s = s->next;
	}
}

/*
 * Caller is holding rose_route_list_lock.
 */
static void rose_remove_route(struct rose_route *rose_route)
{
	struct rose_route *s;

	if (rose_route->neigh1 != NULL)
		rose_route->neigh1->use--;

	if (rose_route->neigh2 != NULL)
		rose_route->neigh2->use--;

	if ((s = rose_route_list) == rose_route) {
		rose_route_list = rose_route->next;
		kfree(rose_route);
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == rose_route) {
			s->next = rose_route->next;
			kfree(rose_route);
			return;
		}

		s = s->next;
	}
}

/*
 *	"Delete" a node. Strictly speaking remove a route to a node. The node
 *	is only deleted if no routes are left to it.
 */
static int rose_del_node(struct rose_route_struct *rose_route,
	struct net_device *dev)
{
	struct rose_node  *rose_node;
	struct rose_neigh *rose_neigh;
	int i, err = 0;

	spin_lock_bh(&rose_node_list_lock);
	spin_lock_bh(&rose_neigh_list_lock);

	rose_node = rose_node_list;
	while (rose_node != NULL) {
		if ((rose_node->mask == rose_route->mask) &&
		    (rosecmpm(&rose_route->address, &rose_node->address,
			      rose_route->mask) == 0))
			break;
		rose_node = rose_node->next;
	}

	if (rose_node == NULL || rose_node->loopback) {
		err = -EINVAL;
		goto out;
	}

	rose_neigh = rose_neigh_list;
	while (rose_neigh != NULL) {
		if (ax25cmp(&rose_route->neighbour,
			    &rose_neigh->callsign) == 0 &&
		    rose_neigh->dev == dev)
			break;
		rose_neigh = rose_neigh->next;
	}

	if (rose_neigh == NULL) {
		err = -EINVAL;
		goto out;
	}

	for (i = 0; i < rose_node->count; i++) {
		if (rose_node->neighbour[i] == rose_neigh) {
			rose_neigh->count--;

			if (rose_neigh->count == 0 && rose_neigh->use == 0)
				rose_remove_neigh(rose_neigh);

			rose_node->count--;

			if (rose_node->count == 0) {
				rose_remove_node(rose_node);
			} else {
				switch (i) {
				case 0:
					rose_node->neighbour[0] =
						rose_node->neighbour[1];
					fallthrough;
				case 1:
					rose_node->neighbour[1] =
						rose_node->neighbour[2];
					break;
				case 2:
					break;
				}
			}
			goto out;
		}
	}
	err = -EINVAL;

out:
	spin_unlock_bh(&rose_neigh_list_lock);
	spin_unlock_bh(&rose_node_list_lock);

	return err;
}

/*
 *	Add the loopback neighbour.
 */
void rose_add_loopback_neigh(void)
{
	struct rose_neigh *sn;

	rose_loopback_neigh = kmalloc(sizeof(struct rose_neigh), GFP_KERNEL);
	if (!rose_loopback_neigh)
		return;
	sn = rose_loopback_neigh;

	sn->callsign  = null_ax25_address;
	sn->digipeat  = NULL;
	sn->ax25      = NULL;
	sn->dev       = NULL;
	sn->count     = 0;
	sn->use       = 0;
	sn->dce_mode  = 1;
	sn->loopback  = 1;
	sn->number    = rose_neigh_no++;
	sn->restarted = 1;

	skb_queue_head_init(&sn->queue);

	timer_setup(&sn->ftimer, NULL, 0);
	timer_setup(&sn->t0timer, NULL, 0);

	spin_lock_bh(&rose_neigh_list_lock);
	sn->next = rose_neigh_list;
	rose_neigh_list           = sn;
	spin_unlock_bh(&rose_neigh_list_lock);
}

/*
 *	Add a loopback node.
 */
int rose_add_loopback_node(rose_address *address)
{
	struct rose_node *rose_node;
	int err = 0;

	spin_lock_bh(&rose_node_list_lock);

	rose_node = rose_node_list;
	while (rose_node != NULL) {
		if ((rose_node->mask == 10) &&
		     (rosecmpm(address, &rose_node->address, 10) == 0) &&
		     rose_node->loopback)
			break;
		rose_node = rose_node->next;
	}

	if (rose_node != NULL)
		goto out;

	if ((rose_node = kmalloc(sizeof(*rose_node), GFP_ATOMIC)) == NULL) {
		err = -ENOMEM;
		goto out;
	}

	rose_node->address      = *address;
	rose_node->mask         = 10;
	rose_node->count        = 1;
	rose_node->loopback     = 1;
	rose_node->neighbour[0] = rose_loopback_neigh;

	/* Insert at the head of list. Address is always mask=10 */
	rose_node->next = rose_node_list;
	rose_node_list  = rose_node;

	rose_loopback_neigh->count++;

out:
	spin_unlock_bh(&rose_node_list_lock);

	return err;
}

/*
 *	Delete a loopback node.
 */
void rose_del_loopback_node(rose_address *address)
{
	struct rose_node *rose_node;

	spin_lock_bh(&rose_node_list_lock);

	rose_node = rose_node_list;
	while (rose_node != NULL) {
		if ((rose_node->mask == 10) &&
		    (rosecmpm(address, &rose_node->address, 10) == 0) &&
		    rose_node->loopback)
			break;
		rose_node = rose_node->next;
	}

	if (rose_node == NULL)
		goto out;

	rose_remove_node(rose_node);

	rose_loopback_neigh->count--;

out:
	spin_unlock_bh(&rose_node_list_lock);
}

/*
 *	A device has been removed. Remove its routes and neighbours.
 */
void rose_rt_device_down(struct net_device *dev)
{
	struct rose_neigh *s, *rose_neigh;
	struct rose_node  *t, *rose_node;
	int i;

	spin_lock_bh(&rose_node_list_lock);
	spin_lock_bh(&rose_neigh_list_lock);
	rose_neigh = rose_neigh_list;
	while (rose_neigh != NULL) {
		s          = rose_neigh;
		rose_neigh = rose_neigh->next;

		if (s->dev != dev)
			continue;

		rose_node = rose_node_list;

		while (rose_node != NULL) {
			t         = rose_node;
			rose_node = rose_node->next;

			for (i = 0; i < t->count; i++) {
				if (t->neighbour[i] != s)
					continue;

				t->count--;

				switch (i) {
				case 0:
					t->neighbour[0] = t->neighbour[1];
					fallthrough;
				case 1:
					t->neighbour[1] = t->neighbour[2];
					break;
				case 2:
					break;
				}
			}

			if (t->count <= 0)
				rose_remove_node(t);
		}

		rose_remove_neigh(s);
	}
	spin_unlock_bh(&rose_neigh_list_lock);
	spin_unlock_bh(&rose_node_list_lock);
}

#if 0 /* Currently unused */
/*
 *	A device has been removed. Remove its links.
 */
void rose_route_device_down(struct net_device *dev)
{
	struct rose_route *s, *rose_route;

	spin_lock_bh(&rose_route_list_lock);
	rose_route = rose_route_list;
	while (rose_route != NULL) {
		s          = rose_route;
		rose_route = rose_route->next;

		if (s->neigh1->dev == dev || s->neigh2->dev == dev)
			rose_remove_route(s);
	}
	spin_unlock_bh(&rose_route_list_lock);
}
#endif

/*
 *	Clear all nodes and neighbours out, except for neighbours with
 *	active connections going through them.
 *  Do not clear loopback neighbour and nodes.
 */
static int rose_clear_routes(void)
{
	struct rose_neigh *s, *rose_neigh;
	struct rose_node  *t, *rose_node;

	spin_lock_bh(&rose_node_list_lock);
	spin_lock_bh(&rose_neigh_list_lock);

	rose_neigh = rose_neigh_list;
	rose_node  = rose_node_list;

	while (rose_node != NULL) {
		t         = rose_node;
		rose_node = rose_node->next;
		if (!t->loopback)
			rose_remove_node(t);
	}

	while (rose_neigh != NULL) {
		s          = rose_neigh;
		rose_neigh = rose_neigh->next;

		if (s->use == 0 && !s->loopback) {
			s->count = 0;
			rose_remove_neigh(s);
		}
	}

	spin_unlock_bh(&rose_neigh_list_lock);
	spin_unlock_bh(&rose_node_list_lock);

	return 0;
}

/*
 *	Check that the device given is a valid AX.25 interface that is "up".
 * 	called with RTNL
 */
static struct net_device *rose_ax25_dev_find(char *devname)
{
	struct net_device *dev;

	if ((dev = __dev_get_by_name(&init_net, devname)) == NULL)
		return NULL;

	if ((dev->flags & IFF_UP) && dev->type == ARPHRD_AX25)
		return dev;

	return NULL;
}

/*
 *	Find the first active ROSE device, usually "rose0".
 */
struct net_device *rose_dev_first(void)
{
	struct net_device *dev, *first = NULL;

	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev) {
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_ROSE)
			if (first == NULL || strncmp(dev->name, first->name, 3) < 0)
				first = dev;
	}
	rcu_read_unlock();

	return first;
}

/*
 *	Find the ROSE device for the given address.
 */
struct net_device *rose_dev_get(rose_address *addr)
{
	struct net_device *dev;

	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev) {
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_ROSE && rosecmp(addr, (rose_address *)dev->dev_addr) == 0) {
			dev_hold(dev);
			goto out;
		}
	}
	dev = NULL;
out:
	rcu_read_unlock();
	return dev;
}

static int rose_dev_exists(rose_address *addr)
{
	struct net_device *dev;

	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev) {
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_ROSE && rosecmp(addr, (rose_address *)dev->dev_addr) == 0)
			goto out;
	}
	dev = NULL;
out:
	rcu_read_unlock();
	return dev != NULL;
}




struct rose_route *rose_route_free_lci(unsigned int lci, struct rose_neigh *neigh)
{
	struct rose_route *rose_route;

	for (rose_route = rose_route_list; rose_route != NULL; rose_route = rose_route->next)
		if ((rose_route->neigh1 == neigh && rose_route->lci1 == lci) ||
		    (rose_route->neigh2 == neigh && rose_route->lci2 == lci))
			return rose_route;

	return NULL;
}

/*
 *	Find a neighbour or a route given a ROSE address.
 */
struct rose_neigh *rose_get_neigh(rose_address *addr, unsigned char *cause,
	unsigned char *diagnostic, int route_frame)
{
	struct rose_neigh *res = NULL;
	struct rose_node *node;
	int failed = 0;
	int i;

	if (!route_frame) spin_lock_bh(&rose_node_list_lock);
	for (node = rose_node_list; node != NULL; node = node->next) {
		if (rosecmpm(addr, &node->address, node->mask) == 0) {
			for (i = 0; i < node->count; i++) {
				if (node->neighbour[i]->restarted) {
					res = node->neighbour[i];
					goto out;
				}
			}
		}
	}
	if (!route_frame) { /* connect request */
		for (node = rose_node_list; node != NULL; node = node->next) {
			if (rosecmpm(addr, &node->address, node->mask) == 0) {
				for (i = 0; i < node->count; i++) {
					if (!rose_ftimer_running(node->neighbour[i])) {
						res = node->neighbour[i];
						goto out;
					}
					failed = 1;
				}
			}
		}
	}

	if (failed) {
		*cause      = ROSE_OUT_OF_ORDER;
		*diagnostic = 0;
	} else {
		*cause      = ROSE_NOT_OBTAINABLE;
		*diagnostic = 0;
	}

out:
	if (!route_frame) spin_unlock_bh(&rose_node_list_lock);
	return res;
}

/*
 *	Handle the ioctls that control the routing functions.
 */
int rose_rt_ioctl(unsigned int cmd, void __user *arg)
{
	struct rose_route_struct rose_route;
	struct net_device *dev;
	int err;

	switch (cmd) {
	case SIOCADDRT:
		if (copy_from_user(&rose_route, arg, sizeof(struct rose_route_struct)))
			return -EFAULT;
		if ((dev = rose_ax25_dev_find(rose_route.device)) == NULL)
			return -EINVAL;
		if (rose_dev_exists(&rose_route.address)) /* Can't add routes to ourself */
			return -EINVAL;
		if (rose_route.mask > 10) /* Mask can't be more than 10 digits */
			return -EINVAL;
		if (rose_route.ndigis > AX25_MAX_DIGIS)
			return -EINVAL;
		err = rose_add_node(&rose_route, dev);
		return err;

	case SIOCDELRT:
		if (copy_from_user(&rose_route, arg, sizeof(struct rose_route_struct)))
			return -EFAULT;
		if ((dev = rose_ax25_dev_find(rose_route.device)) == NULL)
			return -EINVAL;
		err = rose_del_node(&rose_route, dev);
		return err;

	case SIOCRSCLRRT:
		return rose_clear_routes();

	default:
		return -EINVAL;
	}

	return 0;
}

static void rose_del_route_by_neigh(struct rose_neigh *rose_neigh)
{
	struct rose_route *rose_route, *s;

	rose_neigh->restarted = 0;

	rose_stop_t0timer(rose_neigh);
	rose_start_ftimer(rose_neigh);

	skb_queue_purge(&rose_neigh->queue);

	spin_lock_bh(&rose_route_list_lock);

	rose_route = rose_route_list;

	while (rose_route != NULL) {
		if ((rose_route->neigh1 == rose_neigh && rose_route->neigh2 == rose_neigh) ||
		    (rose_route->neigh1 == rose_neigh && rose_route->neigh2 == NULL)       ||
		    (rose_route->neigh2 == rose_neigh && rose_route->neigh1 == NULL)) {
			s = rose_route->next;
			rose_remove_route(rose_route);
			rose_route = s;
			continue;
		}

		if (rose_route->neigh1 == rose_neigh) {
			rose_route->neigh1->use--;
			rose_route->neigh1 = NULL;
			rose_transmit_clear_request(rose_route->neigh2, rose_route->lci2, ROSE_OUT_OF_ORDER, 0);
		}

		if (rose_route->neigh2 == rose_neigh) {
			rose_route->neigh2->use--;
			rose_route->neigh2 = NULL;
			rose_transmit_clear_request(rose_route->neigh1, rose_route->lci1, ROSE_OUT_OF_ORDER, 0);
		}

		rose_route = rose_route->next;
	}
	spin_unlock_bh(&rose_route_list_lock);
}

/*
 * 	A level 2 link has timed out, therefore it appears to be a poor link,
 *	then don't use that neighbour until it is reset. Blow away all through
 *	routes and connections using this route.
 */
void rose_link_failed(ax25_cb *ax25, int reason)
{
	struct rose_neigh *rose_neigh;

	spin_lock_bh(&rose_neigh_list_lock);
	rose_neigh = rose_neigh_list;
	while (rose_neigh != NULL) {
		if (rose_neigh->ax25 == ax25)
			break;
		rose_neigh = rose_neigh->next;
	}

	if (rose_neigh != NULL) {
		rose_neigh->ax25 = NULL;
		ax25_cb_put(ax25);

		rose_del_route_by_neigh(rose_neigh);
		rose_kill_by_neigh(rose_neigh);
	}
	spin_unlock_bh(&rose_neigh_list_lock);
}

/*
 * 	A device has been "downed" remove its link status. Blow away all
 *	through routes and connections that use this device.
 */
void rose_link_device_down(struct net_device *dev)
{
	struct rose_neigh *rose_neigh;

	for (rose_neigh = rose_neigh_list; rose_neigh != NULL; rose_neigh = rose_neigh->next) {
		if (rose_neigh->dev == dev) {
			rose_del_route_by_neigh(rose_neigh);
			rose_kill_by_neigh(rose_neigh);
		}
	}
}

/*
 *	Route a frame to an appropriate AX.25 connection.
 *	A NULL ax25_cb indicates an internally generated frame.
 */
int rose_route_frame(struct sk_buff *skb, ax25_cb *ax25)
{
	struct rose_neigh *rose_neigh, *new_neigh;
	struct rose_route *rose_route;
	struct rose_facilities_struct facilities;
	rose_address *src_addr, *dest_addr;
	struct sock *sk;
	unsigned short frametype;
	unsigned int lci, new_lci;
	unsigned char cause, diagnostic;
	struct net_device *dev;
	int res = 0;
	char buf[11];

	if (skb->len < ROSE_MIN_LEN)
		return res;

	if (!ax25)
		return rose_loopback_queue(skb, NULL);

	frametype = skb->data[2];
	lci = ((skb->data[0] << 8) & 0xF00) + ((skb->data[1] << 0) & 0x0FF);
	if (frametype == ROSE_CALL_REQUEST &&
	    (skb->len <= ROSE_CALL_REQ_FACILITIES_OFF ||
	     skb->data[ROSE_CALL_REQ_ADDR_LEN_OFF] !=
	     ROSE_CALL_REQ_ADDR_LEN_VAL))
		return res;
	src_addr  = (rose_address *)(skb->data + ROSE_CALL_REQ_SRC_ADDR_OFF);
	dest_addr = (rose_address *)(skb->data + ROSE_CALL_REQ_DEST_ADDR_OFF);

	spin_lock_bh(&rose_neigh_list_lock);
	spin_lock_bh(&rose_route_list_lock);

	rose_neigh = rose_neigh_list;
	while (rose_neigh != NULL) {
		if (ax25cmp(&ax25->dest_addr, &rose_neigh->callsign) == 0 &&
		    ax25->ax25_dev->dev == rose_neigh->dev)
			break;
		rose_neigh = rose_neigh->next;
	}

	if (rose_neigh == NULL) {
		printk("rose_route : unknown neighbour or device %s\n",
		       ax2asc(buf, &ax25->dest_addr));
		goto out;
	}

	/*
	 *	Obviously the link is working, halt the ftimer.
	 */
	rose_stop_ftimer(rose_neigh);

	/*
	 *	LCI of zero is always for us, and its always a restart
	 * 	frame.
	 */
	if (lci == 0) {
		rose_link_rx_restart(skb, rose_neigh, frametype);
		goto out;
	}

	/*
	 *	Find an existing socket.
	 */
	if ((sk = rose_find_socket(lci, rose_neigh)) != NULL) {
		if (frametype == ROSE_CALL_REQUEST) {
			struct rose_sock *rose = rose_sk(sk);

			/* Remove an existing unused socket */
			rose_clear_queues(sk);
			rose->cause	 = ROSE_NETWORK_CONGESTION;
			rose->diagnostic = 0;
			rose->neighbour->use--;
			rose->neighbour	 = NULL;
			rose->lci	 = 0;
			rose->state	 = ROSE_STATE_0;
			sk->sk_state	 = TCP_CLOSE;
			sk->sk_err	 = 0;
			sk->sk_shutdown	 |= SEND_SHUTDOWN;
			if (!sock_flag(sk, SOCK_DEAD)) {
				sk->sk_state_change(sk);
				sock_set_flag(sk, SOCK_DEAD);
			}
		}
		else {
			skb_reset_transport_header(skb);
			res = rose_process_rx_frame(sk, skb);
			goto out;
		}
	}

	/*
	 *	Is is a Call Request and is it for us ?
	 */
	if (frametype == ROSE_CALL_REQUEST)
		if ((dev = rose_dev_get(dest_addr)) != NULL) {
			res = rose_rx_call_request(skb, dev, rose_neigh, lci);
			dev_put(dev);
			goto out;
		}

	if (!sysctl_rose_routing_control) {
		rose_transmit_clear_request(rose_neigh, lci, ROSE_NOT_OBTAINABLE, 0);
		goto out;
	}

	/*
	 *	Route it to the next in line if we have an entry for it.
	 */
	rose_route = rose_route_list;
	while (rose_route != NULL) {
		if (rose_route->lci1 == lci &&
		    rose_route->neigh1 == rose_neigh) {
			if (frametype == ROSE_CALL_REQUEST) {
				/* F6FBB - Remove an existing unused route */
				rose_remove_route(rose_route);
				break;
			} else if (rose_route->neigh2 != NULL) {
				skb->data[0] &= 0xF0;
				skb->data[0] |= (rose_route->lci2 >> 8) & 0x0F;
				skb->data[1]  = (rose_route->lci2 >> 0) & 0xFF;
				rose_transmit_link(skb, rose_route->neigh2);
				if (frametype == ROSE_CLEAR_CONFIRMATION)
					rose_remove_route(rose_route);
				res = 1;
				goto out;
			} else {
				if (frametype == ROSE_CLEAR_CONFIRMATION)
					rose_remove_route(rose_route);
				goto out;
			}
		}
		if (rose_route->lci2 == lci &&
		    rose_route->neigh2 == rose_neigh) {
			if (frametype == ROSE_CALL_REQUEST) {
				/* F6FBB - Remove an existing unused route */
				rose_remove_route(rose_route);
				break;
			} else if (rose_route->neigh1 != NULL) {
				skb->data[0] &= 0xF0;
				skb->data[0] |= (rose_route->lci1 >> 8) & 0x0F;
				skb->data[1]  = (rose_route->lci1 >> 0) & 0xFF;
				rose_transmit_link(skb, rose_route->neigh1);
				if (frametype == ROSE_CLEAR_CONFIRMATION)
					rose_remove_route(rose_route);
				res = 1;
				goto out;
			} else {
				if (frametype == ROSE_CLEAR_CONFIRMATION)
					rose_remove_route(rose_route);
				goto out;
			}
		}
		rose_route = rose_route->next;
	}

	/*
	 *	We know that:
	 *	1. The frame isn't for us,
	 *	2. It isn't "owned" by any existing route.
	 */
	if (frametype != ROSE_CALL_REQUEST) {	/* XXX */
		res = 0;
		goto out;
	}

	memset(&facilities, 0x00, sizeof(struct rose_facilities_struct));

	if (!rose_parse_facilities(skb->data + ROSE_CALL_REQ_FACILITIES_OFF,
				   skb->len - ROSE_CALL_REQ_FACILITIES_OFF,
				   &facilities)) {
		rose_transmit_clear_request(rose_neigh, lci, ROSE_INVALID_FACILITY, 76);
		goto out;
	}

	/*
	 *	Check for routing loops.
	 */
	rose_route = rose_route_list;
	while (rose_route != NULL) {
		if (rose_route->rand == facilities.rand &&
		    rosecmp(src_addr, &rose_route->src_addr) == 0 &&
		    ax25cmp(&facilities.dest_call, &rose_route->src_call) == 0 &&
		    ax25cmp(&facilities.source_call, &rose_route->dest_call) == 0) {
			rose_transmit_clear_request(rose_neigh, lci, ROSE_NOT_OBTAINABLE, 120);
			goto out;
		}
		rose_route = rose_route->next;
	}

	if ((new_neigh = rose_get_neigh(dest_addr, &cause, &diagnostic, 1)) == NULL) {
		rose_transmit_clear_request(rose_neigh, lci, cause, diagnostic);
		goto out;
	}

	if ((new_lci = rose_new_lci(new_neigh)) == 0) {
		rose_transmit_clear_request(rose_neigh, lci, ROSE_NETWORK_CONGESTION, 71);
		goto out;
	}

	if ((rose_route = kmalloc(sizeof(*rose_route), GFP_ATOMIC)) == NULL) {
		rose_transmit_clear_request(rose_neigh, lci, ROSE_NETWORK_CONGESTION, 120);
		goto out;
	}

	rose_route->lci1      = lci;
	rose_route->src_addr  = *src_addr;
	rose_route->dest_addr = *dest_addr;
	rose_route->src_call  = facilities.dest_call;
	rose_route->dest_call = facilities.source_call;
	rose_route->rand      = facilities.rand;
	rose_route->neigh1    = rose_neigh;
	rose_route->lci2      = new_lci;
	rose_route->neigh2    = new_neigh;

	rose_route->neigh1->use++;
	rose_route->neigh2->use++;

	rose_route->next = rose_route_list;
	rose_route_list  = rose_route;

	skb->data[0] &= 0xF0;
	skb->data[0] |= (rose_route->lci2 >> 8) & 0x0F;
	skb->data[1]  = (rose_route->lci2 >> 0) & 0xFF;

	rose_transmit_link(skb, rose_route->neigh2);
	res = 1;

out:
	spin_unlock_bh(&rose_route_list_lock);
	spin_unlock_bh(&rose_neigh_list_lock);

	return res;
}

#ifdef CONFIG_PROC_FS

static void *rose_node_start(struct seq_file *seq, loff_t *pos)
	__acquires(rose_node_list_lock)
{
	struct rose_node *rose_node;
	int i = 1;

	spin_lock_bh(&rose_node_list_lock);
	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (rose_node = rose_node_list; rose_node && i < *pos;
	     rose_node = rose_node->next, ++i);

	return (i == *pos) ? rose_node : NULL;
}

static void *rose_node_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;

	return (v == SEQ_START_TOKEN) ? rose_node_list
		: ((struct rose_node *)v)->next;
}

static void rose_node_stop(struct seq_file *seq, void *v)
	__releases(rose_node_list_lock)
{
	spin_unlock_bh(&rose_node_list_lock);
}

static int rose_node_show(struct seq_file *seq, void *v)
{
	char rsbuf[11];
	int i;

	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "address    mask n neigh neigh neigh\n");
	else {
		const struct rose_node *rose_node = v;
		/* if (rose_node->loopback) {
			seq_printf(seq, "%-10s %04d 1 loopback\n",
				   rose2asc(rsbuf, &rose_node->address),
				   rose_node->mask);
		} else { */
			seq_printf(seq, "%-10s %04d %d",
				   rose2asc(rsbuf, &rose_node->address),
				   rose_node->mask,
				   rose_node->count);

			for (i = 0; i < rose_node->count; i++)
				seq_printf(seq, " %05d",
					rose_node->neighbour[i]->number);

			seq_puts(seq, "\n");
		/* } */
	}
	return 0;
}

const struct seq_operations rose_node_seqops = {
	.start = rose_node_start,
	.next = rose_node_next,
	.stop = rose_node_stop,
	.show = rose_node_show,
};

static void *rose_neigh_start(struct seq_file *seq, loff_t *pos)
	__acquires(rose_neigh_list_lock)
{
	struct rose_neigh *rose_neigh;
	int i = 1;

	spin_lock_bh(&rose_neigh_list_lock);
	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (rose_neigh = rose_neigh_list; rose_neigh && i < *pos;
	     rose_neigh = rose_neigh->next, ++i);

	return (i == *pos) ? rose_neigh : NULL;
}

static void *rose_neigh_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;

	return (v == SEQ_START_TOKEN) ? rose_neigh_list
		: ((struct rose_neigh *)v)->next;
}

static void rose_neigh_stop(struct seq_file *seq, void *v)
	__releases(rose_neigh_list_lock)
{
	spin_unlock_bh(&rose_neigh_list_lock);
}

static int rose_neigh_show(struct seq_file *seq, void *v)
{
	char buf[11];
	int i;

	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
			 "addr  callsign  dev  count use mode restart  t0  tf digipeaters\n");
	else {
		struct rose_neigh *rose_neigh = v;

		/* if (!rose_neigh->loopback) { */
		seq_printf(seq, "%05d %-9s %-4s   %3d %3d  %3s     %3s %3lu %3lu",
			   rose_neigh->number,
			   (rose_neigh->loopback) ? "RSLOOP-0" : ax2asc(buf, &rose_neigh->callsign),
			   rose_neigh->dev ? rose_neigh->dev->name : "???",
			   rose_neigh->count,
			   rose_neigh->use,
			   (rose_neigh->dce_mode) ? "DCE" : "DTE",
			   (rose_neigh->restarted) ? "yes" : "no",
			   ax25_display_timer(&rose_neigh->t0timer) / HZ,
			   ax25_display_timer(&rose_neigh->ftimer)  / HZ);

		if (rose_neigh->digipeat != NULL) {
			for (i = 0; i < rose_neigh->digipeat->ndigi; i++)
				seq_printf(seq, " %s", ax2asc(buf, &rose_neigh->digipeat->calls[i]));
		}

		seq_puts(seq, "\n");
	}
	return 0;
}


const struct seq_operations rose_neigh_seqops = {
	.start = rose_neigh_start,
	.next = rose_neigh_next,
	.stop = rose_neigh_stop,
	.show = rose_neigh_show,
};

static void *rose_route_start(struct seq_file *seq, loff_t *pos)
	__acquires(rose_route_list_lock)
{
	struct rose_route *rose_route;
	int i = 1;

	spin_lock_bh(&rose_route_list_lock);
	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (rose_route = rose_route_list; rose_route && i < *pos;
	     rose_route = rose_route->next, ++i);

	return (i == *pos) ? rose_route : NULL;
}

static void *rose_route_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;

	return (v == SEQ_START_TOKEN) ? rose_route_list
		: ((struct rose_route *)v)->next;
}

static void rose_route_stop(struct seq_file *seq, void *v)
	__releases(rose_route_list_lock)
{
	spin_unlock_bh(&rose_route_list_lock);
}

static int rose_route_show(struct seq_file *seq, void *v)
{
	char buf[11], rsbuf[11];

	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
			 "lci  address     callsign   neigh  <-> lci  address     callsign   neigh\n");
	else {
		struct rose_route *rose_route = v;

		if (rose_route->neigh1)
			seq_printf(seq,
				   "%3.3X  %-10s  %-9s  %05d      ",
				   rose_route->lci1,
				   rose2asc(rsbuf, &rose_route->src_addr),
				   ax2asc(buf, &rose_route->src_call),
				   rose_route->neigh1->number);
		else
			seq_puts(seq,
				 "000  *           *          00000      ");

		if (rose_route->neigh2)
			seq_printf(seq,
				   "%3.3X  %-10s  %-9s  %05d\n",
				   rose_route->lci2,
				   rose2asc(rsbuf, &rose_route->dest_addr),
				   ax2asc(buf, &rose_route->dest_call),
				   rose_route->neigh2->number);
		 else
			 seq_puts(seq,
				  "000  *           *          00000\n");
		}
	return 0;
}

struct seq_operations rose_route_seqops = {
	.start = rose_route_start,
	.next = rose_route_next,
	.stop = rose_route_stop,
	.show = rose_route_show,
};
#endif /* CONFIG_PROC_FS */

/*
 *	Release all memory associated with ROSE routing structures.
 */
void __exit rose_rt_free(void)
{
	struct rose_neigh *s, *rose_neigh = rose_neigh_list;
	struct rose_node  *t, *rose_node  = rose_node_list;
	struct rose_route *u, *rose_route = rose_route_list;

	while (rose_neigh != NULL) {
		s          = rose_neigh;
		rose_neigh = rose_neigh->next;

		rose_remove_neigh(s);
	}

	while (rose_node != NULL) {
		t         = rose_node;
		rose_node = rose_node->next;

		rose_remove_node(t);
	}

	while (rose_route != NULL) {
		u          = rose_route;
		rose_route = rose_route->next;

		rose_remove_route(u);
	}
}
