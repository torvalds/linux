// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright Alan Cox GW4PTS (alan@lxorguk.ukuu.org.uk)
 * Copyright Tomi Manninen OH2BNS (oh2bns@sral.fi)
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
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <net/netrom.h>
#include <linux/seq_file.h>
#include <linux/export.h>

static unsigned int nr_neigh_no = 1;

static HLIST_HEAD(nr_node_list);
static DEFINE_SPINLOCK(nr_node_list_lock);
static HLIST_HEAD(nr_neigh_list);
static DEFINE_SPINLOCK(nr_neigh_list_lock);

static struct nr_node *nr_node_get(ax25_address *callsign)
{
	struct nr_node *found = NULL;
	struct nr_node *nr_node;

	spin_lock_bh(&nr_node_list_lock);
	nr_node_for_each(nr_node, &nr_node_list)
		if (ax25cmp(callsign, &nr_node->callsign) == 0) {
			nr_node_hold(nr_node);
			found = nr_node;
			break;
		}
	spin_unlock_bh(&nr_node_list_lock);
	return found;
}

static struct nr_neigh *nr_neigh_get_dev(ax25_address *callsign,
					 struct net_device *dev)
{
	struct nr_neigh *found = NULL;
	struct nr_neigh *nr_neigh;

	spin_lock_bh(&nr_neigh_list_lock);
	nr_neigh_for_each(nr_neigh, &nr_neigh_list)
		if (ax25cmp(callsign, &nr_neigh->callsign) == 0 &&
		    nr_neigh->dev == dev) {
			nr_neigh_hold(nr_neigh);
			found = nr_neigh;
			break;
		}
	spin_unlock_bh(&nr_neigh_list_lock);
	return found;
}

static void nr_remove_neigh(struct nr_neigh *);

/*      re-sort the routes in quality order.    */
static void re_sort_routes(struct nr_node *nr_node, int x, int y)
{
	if (nr_node->routes[y].quality > nr_node->routes[x].quality) {
		if (nr_node->which == x)
			nr_node->which = y;
		else if (nr_node->which == y)
			nr_node->which = x;

		swap(nr_node->routes[x], nr_node->routes[y]);
	}
}

/*
 *	Add a new route to a node, and in the process add the node and the
 *	neighbour if it is new.
 */
static int __must_check nr_add_node(ax25_address *nr, const char *mnemonic,
	ax25_address *ax25, ax25_digi *ax25_digi, struct net_device *dev,
	int quality, int obs_count)
{
	struct nr_node  *nr_node;
	struct nr_neigh *nr_neigh;
	int i, found;
	struct net_device *odev;

	if ((odev=nr_dev_get(nr)) != NULL) {	/* Can't add routes to ourself */
		dev_put(odev);
		return -EINVAL;
	}

	nr_node = nr_node_get(nr);

	nr_neigh = nr_neigh_get_dev(ax25, dev);

	/*
	 * The L2 link to a neighbour has failed in the past
	 * and now a frame comes from this neighbour. We assume
	 * it was a temporary trouble with the link and reset the
	 * routes now (and not wait for a node broadcast).
	 */
	if (nr_neigh != NULL && nr_neigh->failed != 0 && quality == 0) {
		struct nr_node *nr_nodet;

		spin_lock_bh(&nr_node_list_lock);
		nr_node_for_each(nr_nodet, &nr_node_list) {
			nr_node_lock(nr_nodet);
			for (i = 0; i < nr_nodet->count; i++)
				if (nr_nodet->routes[i].neighbour == nr_neigh)
					if (i < nr_nodet->which)
						nr_nodet->which = i;
			nr_node_unlock(nr_nodet);
		}
		spin_unlock_bh(&nr_node_list_lock);
	}

	if (nr_neigh != NULL)
		nr_neigh->failed = 0;

	if (quality == 0 && nr_neigh != NULL && nr_node != NULL) {
		nr_neigh_put(nr_neigh);
		nr_node_put(nr_node);
		return 0;
	}

	if (nr_neigh == NULL) {
		if ((nr_neigh = kmalloc(sizeof(*nr_neigh), GFP_ATOMIC)) == NULL) {
			if (nr_node)
				nr_node_put(nr_node);
			return -ENOMEM;
		}

		nr_neigh->callsign = *ax25;
		nr_neigh->digipeat = NULL;
		nr_neigh->ax25     = NULL;
		nr_neigh->dev      = dev;
		nr_neigh->quality  = READ_ONCE(sysctl_netrom_default_path_quality);
		nr_neigh->locked   = 0;
		nr_neigh->count    = 0;
		nr_neigh->number   = nr_neigh_no++;
		nr_neigh->failed   = 0;
		refcount_set(&nr_neigh->refcount, 1);

		if (ax25_digi != NULL && ax25_digi->ndigi > 0) {
			nr_neigh->digipeat = kmemdup(ax25_digi,
						     sizeof(*ax25_digi),
						     GFP_KERNEL);
			if (nr_neigh->digipeat == NULL) {
				kfree(nr_neigh);
				if (nr_node)
					nr_node_put(nr_node);
				return -ENOMEM;
			}
		}

		spin_lock_bh(&nr_neigh_list_lock);
		hlist_add_head(&nr_neigh->neigh_node, &nr_neigh_list);
		nr_neigh_hold(nr_neigh);
		spin_unlock_bh(&nr_neigh_list_lock);
	}

	if (quality != 0 && ax25cmp(nr, ax25) == 0 && !nr_neigh->locked)
		nr_neigh->quality = quality;

	if (nr_node == NULL) {
		if ((nr_node = kmalloc(sizeof(*nr_node), GFP_ATOMIC)) == NULL) {
			if (nr_neigh)
				nr_neigh_put(nr_neigh);
			return -ENOMEM;
		}

		nr_node->callsign = *nr;
		strcpy(nr_node->mnemonic, mnemonic);

		nr_node->which = 0;
		nr_node->count = 1;
		refcount_set(&nr_node->refcount, 1);
		spin_lock_init(&nr_node->node_lock);

		nr_node->routes[0].quality   = quality;
		nr_node->routes[0].obs_count = obs_count;
		nr_node->routes[0].neighbour = nr_neigh;

		nr_neigh_hold(nr_neigh);
		nr_neigh->count++;

		spin_lock_bh(&nr_node_list_lock);
		hlist_add_head(&nr_node->node_node, &nr_node_list);
		/* refcount initialized at 1 */
		spin_unlock_bh(&nr_node_list_lock);

		nr_neigh_put(nr_neigh);
		return 0;
	}
	nr_node_lock(nr_node);

	if (quality != 0)
		strcpy(nr_node->mnemonic, mnemonic);

	for (found = 0, i = 0; i < nr_node->count; i++) {
		if (nr_node->routes[i].neighbour == nr_neigh) {
			nr_node->routes[i].quality   = quality;
			nr_node->routes[i].obs_count = obs_count;
			found = 1;
			break;
		}
	}

	if (!found) {
		/* We have space at the bottom, slot it in */
		if (nr_node->count < 3) {
			nr_node->routes[2] = nr_node->routes[1];
			nr_node->routes[1] = nr_node->routes[0];

			nr_node->routes[0].quality   = quality;
			nr_node->routes[0].obs_count = obs_count;
			nr_node->routes[0].neighbour = nr_neigh;

			nr_node->which++;
			nr_node->count++;
			nr_neigh_hold(nr_neigh);
			nr_neigh->count++;
		} else {
			/* It must be better than the worst */
			if (quality > nr_node->routes[2].quality) {
				nr_node->routes[2].neighbour->count--;
				nr_neigh_put(nr_node->routes[2].neighbour);

				if (nr_node->routes[2].neighbour->count == 0 && !nr_node->routes[2].neighbour->locked)
					nr_remove_neigh(nr_node->routes[2].neighbour);

				nr_node->routes[2].quality   = quality;
				nr_node->routes[2].obs_count = obs_count;
				nr_node->routes[2].neighbour = nr_neigh;

				nr_neigh_hold(nr_neigh);
				nr_neigh->count++;
			}
		}
	}

	/* Now re-sort the routes in quality order */
	switch (nr_node->count) {
	case 3:
		re_sort_routes(nr_node, 0, 1);
		re_sort_routes(nr_node, 1, 2);
		fallthrough;
	case 2:
		re_sort_routes(nr_node, 0, 1);
		break;
	case 1:
		break;
	}

	for (i = 0; i < nr_node->count; i++) {
		if (nr_node->routes[i].neighbour == nr_neigh) {
			if (i < nr_node->which)
				nr_node->which = i;
			break;
		}
	}

	nr_neigh_put(nr_neigh);
	nr_node_unlock(nr_node);
	nr_node_put(nr_node);
	return 0;
}

static inline void __nr_remove_node(struct nr_node *nr_node)
{
	hlist_del_init(&nr_node->node_node);
	nr_node_put(nr_node);
}

#define nr_remove_node_locked(__node) \
	__nr_remove_node(__node)

static void nr_remove_node(struct nr_node *nr_node)
{
	spin_lock_bh(&nr_node_list_lock);
	__nr_remove_node(nr_node);
	spin_unlock_bh(&nr_node_list_lock);
}

static inline void __nr_remove_neigh(struct nr_neigh *nr_neigh)
{
	hlist_del_init(&nr_neigh->neigh_node);
	nr_neigh_put(nr_neigh);
}

#define nr_remove_neigh_locked(__neigh) \
	__nr_remove_neigh(__neigh)

static void nr_remove_neigh(struct nr_neigh *nr_neigh)
{
	spin_lock_bh(&nr_neigh_list_lock);
	__nr_remove_neigh(nr_neigh);
	spin_unlock_bh(&nr_neigh_list_lock);
}

/*
 *	"Delete" a node. Strictly speaking remove a route to a node. The node
 *	is only deleted if no routes are left to it.
 */
static int nr_del_node(ax25_address *callsign, ax25_address *neighbour, struct net_device *dev)
{
	struct nr_node  *nr_node;
	struct nr_neigh *nr_neigh;
	int i;

	nr_node = nr_node_get(callsign);

	if (nr_node == NULL)
		return -EINVAL;

	nr_neigh = nr_neigh_get_dev(neighbour, dev);

	if (nr_neigh == NULL) {
		nr_node_put(nr_node);
		return -EINVAL;
	}

	nr_node_lock(nr_node);
	for (i = 0; i < nr_node->count; i++) {
		if (nr_node->routes[i].neighbour == nr_neigh) {
			nr_neigh->count--;
			nr_neigh_put(nr_neigh);

			if (nr_neigh->count == 0 && !nr_neigh->locked)
				nr_remove_neigh(nr_neigh);
			nr_neigh_put(nr_neigh);

			nr_node->count--;

			if (nr_node->count == 0) {
				nr_remove_node(nr_node);
			} else {
				switch (i) {
				case 0:
					nr_node->routes[0] = nr_node->routes[1];
					fallthrough;
				case 1:
					nr_node->routes[1] = nr_node->routes[2];
					fallthrough;
				case 2:
					break;
				}
				nr_node_put(nr_node);
			}
			nr_node_unlock(nr_node);

			return 0;
		}
	}
	nr_neigh_put(nr_neigh);
	nr_node_unlock(nr_node);
	nr_node_put(nr_node);

	return -EINVAL;
}

/*
 *	Lock a neighbour with a quality.
 */
static int __must_check nr_add_neigh(ax25_address *callsign,
	ax25_digi *ax25_digi, struct net_device *dev, unsigned int quality)
{
	struct nr_neigh *nr_neigh;

	nr_neigh = nr_neigh_get_dev(callsign, dev);
	if (nr_neigh) {
		nr_neigh->quality = quality;
		nr_neigh->locked  = 1;
		nr_neigh_put(nr_neigh);
		return 0;
	}

	if ((nr_neigh = kmalloc(sizeof(*nr_neigh), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	nr_neigh->callsign = *callsign;
	nr_neigh->digipeat = NULL;
	nr_neigh->ax25     = NULL;
	nr_neigh->dev      = dev;
	nr_neigh->quality  = quality;
	nr_neigh->locked   = 1;
	nr_neigh->count    = 0;
	nr_neigh->number   = nr_neigh_no++;
	nr_neigh->failed   = 0;
	refcount_set(&nr_neigh->refcount, 1);

	if (ax25_digi != NULL && ax25_digi->ndigi > 0) {
		nr_neigh->digipeat = kmemdup(ax25_digi, sizeof(*ax25_digi),
					     GFP_KERNEL);
		if (nr_neigh->digipeat == NULL) {
			kfree(nr_neigh);
			return -ENOMEM;
		}
	}

	spin_lock_bh(&nr_neigh_list_lock);
	hlist_add_head(&nr_neigh->neigh_node, &nr_neigh_list);
	/* refcount is initialized at 1 */
	spin_unlock_bh(&nr_neigh_list_lock);

	return 0;
}

/*
 *	"Delete" a neighbour. The neighbour is only removed if the number
 *	of nodes that may use it is zero.
 */
static int nr_del_neigh(ax25_address *callsign, struct net_device *dev, unsigned int quality)
{
	struct nr_neigh *nr_neigh;

	nr_neigh = nr_neigh_get_dev(callsign, dev);

	if (nr_neigh == NULL) return -EINVAL;

	nr_neigh->quality = quality;
	nr_neigh->locked  = 0;

	if (nr_neigh->count == 0)
		nr_remove_neigh(nr_neigh);
	nr_neigh_put(nr_neigh);

	return 0;
}

/*
 *	Decrement the obsolescence count by one. If a route is reduced to a
 *	count of zero, remove it. Also remove any unlocked neighbours with
 *	zero nodes routing via it.
 */
static int nr_dec_obs(void)
{
	struct nr_neigh *nr_neigh;
	struct nr_node  *s;
	struct hlist_node *nodet;
	int i;

	spin_lock_bh(&nr_node_list_lock);
	nr_node_for_each_safe(s, nodet, &nr_node_list) {
		nr_node_lock(s);
		for (i = 0; i < s->count; i++) {
			switch (s->routes[i].obs_count) {
			case 0:		/* A locked entry */
				break;

			case 1:		/* From 1 -> 0 */
				nr_neigh = s->routes[i].neighbour;

				nr_neigh->count--;
				nr_neigh_put(nr_neigh);

				if (nr_neigh->count == 0 && !nr_neigh->locked)
					nr_remove_neigh(nr_neigh);

				s->count--;

				switch (i) {
				case 0:
					s->routes[0] = s->routes[1];
					fallthrough;
				case 1:
					s->routes[1] = s->routes[2];
					break;
				case 2:
					break;
				}
				break;

			default:
				s->routes[i].obs_count--;
				break;

			}
		}

		if (s->count <= 0)
			nr_remove_node_locked(s);
		nr_node_unlock(s);
	}
	spin_unlock_bh(&nr_node_list_lock);

	return 0;
}

/*
 *	A device has been removed. Remove its routes and neighbours.
 */
void nr_rt_device_down(struct net_device *dev)
{
	struct nr_neigh *s;
	struct hlist_node *nodet, *node2t;
	struct nr_node  *t;
	int i;

	spin_lock_bh(&nr_neigh_list_lock);
	nr_neigh_for_each_safe(s, nodet, &nr_neigh_list) {
		if (s->dev == dev) {
			spin_lock_bh(&nr_node_list_lock);
			nr_node_for_each_safe(t, node2t, &nr_node_list) {
				nr_node_lock(t);
				for (i = 0; i < t->count; i++) {
					if (t->routes[i].neighbour == s) {
						t->count--;

						switch (i) {
						case 0:
							t->routes[0] = t->routes[1];
							fallthrough;
						case 1:
							t->routes[1] = t->routes[2];
							break;
						case 2:
							break;
						}
					}
				}

				if (t->count <= 0)
					nr_remove_node_locked(t);
				nr_node_unlock(t);
			}
			spin_unlock_bh(&nr_node_list_lock);

			nr_remove_neigh_locked(s);
		}
	}
	spin_unlock_bh(&nr_neigh_list_lock);
}

/*
 *	Check that the device given is a valid AX.25 interface that is "up".
 *	Or a valid ethernet interface with an AX.25 callsign binding.
 */
static struct net_device *nr_ax25_dev_get(char *devname)
{
	struct net_device *dev;

	if ((dev = dev_get_by_name(&init_net, devname)) == NULL)
		return NULL;

	if ((dev->flags & IFF_UP) && dev->type == ARPHRD_AX25)
		return dev;

	dev_put(dev);
	return NULL;
}

/*
 *	Find the first active NET/ROM device, usually "nr0".
 */
struct net_device *nr_dev_first(void)
{
	struct net_device *dev, *first = NULL;

	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev) {
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_NETROM)
			if (first == NULL || strncmp(dev->name, first->name, 3) < 0)
				first = dev;
	}
	dev_hold(first);
	rcu_read_unlock();

	return first;
}

/*
 *	Find the NET/ROM device for the given callsign.
 */
struct net_device *nr_dev_get(ax25_address *addr)
{
	struct net_device *dev;

	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev) {
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_NETROM &&
		    ax25cmp(addr, (const ax25_address *)dev->dev_addr) == 0) {
			dev_hold(dev);
			goto out;
		}
	}
	dev = NULL;
out:
	rcu_read_unlock();
	return dev;
}

static ax25_digi *nr_call_to_digi(ax25_digi *digi, int ndigis,
	ax25_address *digipeaters)
{
	int i;

	if (ndigis == 0)
		return NULL;

	for (i = 0; i < ndigis; i++) {
		digi->calls[i]    = digipeaters[i];
		digi->repeated[i] = 0;
	}

	digi->ndigi      = ndigis;
	digi->lastrepeat = -1;

	return digi;
}

/*
 *	Handle the ioctls that control the routing functions.
 */
int nr_rt_ioctl(unsigned int cmd, void __user *arg)
{
	struct nr_route_struct nr_route;
	struct net_device *dev;
	ax25_digi digi;
	int ret;

	switch (cmd) {
	case SIOCADDRT:
		if (copy_from_user(&nr_route, arg, sizeof(struct nr_route_struct)))
			return -EFAULT;
		if (nr_route.ndigis > AX25_MAX_DIGIS)
			return -EINVAL;
		if ((dev = nr_ax25_dev_get(nr_route.device)) == NULL)
			return -EINVAL;
		switch (nr_route.type) {
		case NETROM_NODE:
			if (strnlen(nr_route.mnemonic, 7) == 7) {
				ret = -EINVAL;
				break;
			}

			ret = nr_add_node(&nr_route.callsign,
				nr_route.mnemonic,
				&nr_route.neighbour,
				nr_call_to_digi(&digi, nr_route.ndigis,
						nr_route.digipeaters),
				dev, nr_route.quality,
				nr_route.obs_count);
			break;
		case NETROM_NEIGH:
			ret = nr_add_neigh(&nr_route.callsign,
				nr_call_to_digi(&digi, nr_route.ndigis,
						nr_route.digipeaters),
				dev, nr_route.quality);
			break;
		default:
			ret = -EINVAL;
		}
		dev_put(dev);
		return ret;

	case SIOCDELRT:
		if (copy_from_user(&nr_route, arg, sizeof(struct nr_route_struct)))
			return -EFAULT;
		if ((dev = nr_ax25_dev_get(nr_route.device)) == NULL)
			return -EINVAL;
		switch (nr_route.type) {
		case NETROM_NODE:
			ret = nr_del_node(&nr_route.callsign,
				&nr_route.neighbour, dev);
			break;
		case NETROM_NEIGH:
			ret = nr_del_neigh(&nr_route.callsign,
				dev, nr_route.quality);
			break;
		default:
			ret = -EINVAL;
		}
		dev_put(dev);
		return ret;

	case SIOCNRDECOBS:
		return nr_dec_obs();

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * 	A level 2 link has timed out, therefore it appears to be a poor link,
 *	then don't use that neighbour until it is reset.
 */
void nr_link_failed(ax25_cb *ax25, int reason)
{
	struct nr_neigh *s, *nr_neigh = NULL;
	struct nr_node  *nr_node = NULL;

	spin_lock_bh(&nr_neigh_list_lock);
	nr_neigh_for_each(s, &nr_neigh_list) {
		if (s->ax25 == ax25) {
			nr_neigh_hold(s);
			nr_neigh = s;
			break;
		}
	}
	spin_unlock_bh(&nr_neigh_list_lock);

	if (nr_neigh == NULL)
		return;

	nr_neigh->ax25 = NULL;
	ax25_cb_put(ax25);

	if (++nr_neigh->failed < READ_ONCE(sysctl_netrom_link_fails_count)) {
		nr_neigh_put(nr_neigh);
		return;
	}
	spin_lock_bh(&nr_node_list_lock);
	nr_node_for_each(nr_node, &nr_node_list) {
		nr_node_lock(nr_node);
		if (nr_node->which < nr_node->count &&
		    nr_node->routes[nr_node->which].neighbour == nr_neigh)
			nr_node->which++;
		nr_node_unlock(nr_node);
	}
	spin_unlock_bh(&nr_node_list_lock);
	nr_neigh_put(nr_neigh);
}

/*
 *	Route a frame to an appropriate AX.25 connection. A NULL ax25_cb
 *	indicates an internally generated frame.
 */
int nr_route_frame(struct sk_buff *skb, ax25_cb *ax25)
{
	ax25_address *nr_src, *nr_dest;
	struct nr_neigh *nr_neigh;
	struct nr_node  *nr_node;
	struct net_device *dev;
	unsigned char *dptr;
	ax25_cb *ax25s;
	int ret;
	struct sk_buff *skbn;


	nr_src  = (ax25_address *)(skb->data + 0);
	nr_dest = (ax25_address *)(skb->data + 7);

	if (ax25 != NULL) {
		ret = nr_add_node(nr_src, "", &ax25->dest_addr, ax25->digipeat,
				  ax25->ax25_dev->dev, 0,
				  READ_ONCE(sysctl_netrom_obsolescence_count_initialiser));
		if (ret)
			return ret;
	}

	if ((dev = nr_dev_get(nr_dest)) != NULL) {	/* Its for me */
		if (ax25 == NULL)			/* Its from me */
			ret = nr_loopback_queue(skb);
		else
			ret = nr_rx_frame(skb, dev);
		dev_put(dev);
		return ret;
	}

	if (!READ_ONCE(sysctl_netrom_routing_control) && ax25 != NULL)
		return 0;

	/* Its Time-To-Live has expired */
	if (skb->data[14] == 1) {
		return 0;
	}

	nr_node = nr_node_get(nr_dest);
	if (nr_node == NULL)
		return 0;
	nr_node_lock(nr_node);

	if (nr_node->which >= nr_node->count) {
		nr_node_unlock(nr_node);
		nr_node_put(nr_node);
		return 0;
	}

	nr_neigh = nr_node->routes[nr_node->which].neighbour;

	if ((dev = nr_dev_first()) == NULL) {
		nr_node_unlock(nr_node);
		nr_node_put(nr_node);
		return 0;
	}

	/* We are going to change the netrom headers so we should get our
	   own skb, we also did not know until now how much header space
	   we had to reserve... - RXQ */
	if ((skbn=skb_copy_expand(skb, dev->hard_header_len, 0, GFP_ATOMIC)) == NULL) {
		nr_node_unlock(nr_node);
		nr_node_put(nr_node);
		dev_put(dev);
		return 0;
	}
	kfree_skb(skb);
	skb=skbn;
	skb->data[14]--;

	dptr  = skb_push(skb, 1);
	*dptr = AX25_P_NETROM;

	ax25s = nr_neigh->ax25;
	nr_neigh->ax25 = ax25_send_frame(skb, 256,
					 (const ax25_address *)dev->dev_addr,
					 &nr_neigh->callsign,
					 nr_neigh->digipeat, nr_neigh->dev);
	if (ax25s)
		ax25_cb_put(ax25s);

	dev_put(dev);
	ret = (nr_neigh->ax25 != NULL);
	nr_node_unlock(nr_node);
	nr_node_put(nr_node);

	return ret;
}

#ifdef CONFIG_PROC_FS

static void *nr_node_start(struct seq_file *seq, loff_t *pos)
	__acquires(&nr_node_list_lock)
{
	spin_lock_bh(&nr_node_list_lock);
	return seq_hlist_start_head(&nr_node_list, *pos);
}

static void *nr_node_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_hlist_next(v, &nr_node_list, pos);
}

static void nr_node_stop(struct seq_file *seq, void *v)
	__releases(&nr_node_list_lock)
{
	spin_unlock_bh(&nr_node_list_lock);
}

static int nr_node_show(struct seq_file *seq, void *v)
{
	char buf[11];
	int i;

	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
			 "callsign  mnemonic w n qual obs neigh qual obs neigh qual obs neigh\n");
	else {
		struct nr_node *nr_node = hlist_entry(v, struct nr_node,
						      node_node);

		nr_node_lock(nr_node);
		seq_printf(seq, "%-9s %-7s  %d %d",
			ax2asc(buf, &nr_node->callsign),
			(nr_node->mnemonic[0] == '\0') ? "*" : nr_node->mnemonic,
			nr_node->which + 1,
			nr_node->count);

		for (i = 0; i < nr_node->count; i++) {
			seq_printf(seq, "  %3d   %d %05d",
				nr_node->routes[i].quality,
				nr_node->routes[i].obs_count,
				nr_node->routes[i].neighbour->number);
		}
		nr_node_unlock(nr_node);

		seq_puts(seq, "\n");
	}
	return 0;
}

const struct seq_operations nr_node_seqops = {
	.start = nr_node_start,
	.next = nr_node_next,
	.stop = nr_node_stop,
	.show = nr_node_show,
};

static void *nr_neigh_start(struct seq_file *seq, loff_t *pos)
	__acquires(&nr_neigh_list_lock)
{
	spin_lock_bh(&nr_neigh_list_lock);
	return seq_hlist_start_head(&nr_neigh_list, *pos);
}

static void *nr_neigh_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_hlist_next(v, &nr_neigh_list, pos);
}

static void nr_neigh_stop(struct seq_file *seq, void *v)
	__releases(&nr_neigh_list_lock)
{
	spin_unlock_bh(&nr_neigh_list_lock);
}

static int nr_neigh_show(struct seq_file *seq, void *v)
{
	char buf[11];
	int i;

	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "addr  callsign  dev  qual lock count failed digipeaters\n");
	else {
		struct nr_neigh *nr_neigh;

		nr_neigh = hlist_entry(v, struct nr_neigh, neigh_node);
		seq_printf(seq, "%05d %-9s %-4s  %3d    %d   %3d    %3d",
			nr_neigh->number,
			ax2asc(buf, &nr_neigh->callsign),
			nr_neigh->dev ? nr_neigh->dev->name : "???",
			nr_neigh->quality,
			nr_neigh->locked,
			nr_neigh->count,
			nr_neigh->failed);

		if (nr_neigh->digipeat != NULL) {
			for (i = 0; i < nr_neigh->digipeat->ndigi; i++)
				seq_printf(seq, " %s",
					   ax2asc(buf, &nr_neigh->digipeat->calls[i]));
		}

		seq_puts(seq, "\n");
	}
	return 0;
}

const struct seq_operations nr_neigh_seqops = {
	.start = nr_neigh_start,
	.next = nr_neigh_next,
	.stop = nr_neigh_stop,
	.show = nr_neigh_show,
};
#endif

/*
 *	Free all memory associated with the nodes and routes lists.
 */
void nr_rt_free(void)
{
	struct nr_neigh *s = NULL;
	struct nr_node  *t = NULL;
	struct hlist_node *nodet;

	spin_lock_bh(&nr_neigh_list_lock);
	spin_lock_bh(&nr_node_list_lock);
	nr_node_for_each_safe(t, nodet, &nr_node_list) {
		nr_node_lock(t);
		nr_remove_node_locked(t);
		nr_node_unlock(t);
	}
	nr_neigh_for_each_safe(s, nodet, &nr_neigh_list) {
		while(s->count) {
			s->count--;
			nr_neigh_put(s);
		}
		nr_remove_neigh_locked(s);
	}
	spin_unlock_bh(&nr_node_list_lock);
	spin_unlock_bh(&nr_neigh_list_lock);
}
