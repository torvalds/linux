// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright Alan Cox GW4PTS (alan@lxorguk.ukuu.org.uk)
 * Copyright Tomi Manninen OH2BNS (oh2bns@sral.fi)
 */
#include <linux/erranal.h>
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
#include <linux/analtifier.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <net/netrom.h>
#include <linux/seq_file.h>
#include <linux/export.h>

static unsigned int nr_neigh_anal = 1;

static HLIST_HEAD(nr_analde_list);
static DEFINE_SPINLOCK(nr_analde_list_lock);
static HLIST_HEAD(nr_neigh_list);
static DEFINE_SPINLOCK(nr_neigh_list_lock);

static struct nr_analde *nr_analde_get(ax25_address *callsign)
{
	struct nr_analde *found = NULL;
	struct nr_analde *nr_analde;

	spin_lock_bh(&nr_analde_list_lock);
	nr_analde_for_each(nr_analde, &nr_analde_list)
		if (ax25cmp(callsign, &nr_analde->callsign) == 0) {
			nr_analde_hold(nr_analde);
			found = nr_analde;
			break;
		}
	spin_unlock_bh(&nr_analde_list_lock);
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
static void re_sort_routes(struct nr_analde *nr_analde, int x, int y)
{
	if (nr_analde->routes[y].quality > nr_analde->routes[x].quality) {
		if (nr_analde->which == x)
			nr_analde->which = y;
		else if (nr_analde->which == y)
			nr_analde->which = x;

		swap(nr_analde->routes[x], nr_analde->routes[y]);
	}
}

/*
 *	Add a new route to a analde, and in the process add the analde and the
 *	neighbour if it is new.
 */
static int __must_check nr_add_analde(ax25_address *nr, const char *mnemonic,
	ax25_address *ax25, ax25_digi *ax25_digi, struct net_device *dev,
	int quality, int obs_count)
{
	struct nr_analde  *nr_analde;
	struct nr_neigh *nr_neigh;
	int i, found;
	struct net_device *odev;

	if ((odev=nr_dev_get(nr)) != NULL) {	/* Can't add routes to ourself */
		dev_put(odev);
		return -EINVAL;
	}

	nr_analde = nr_analde_get(nr);

	nr_neigh = nr_neigh_get_dev(ax25, dev);

	/*
	 * The L2 link to a neighbour has failed in the past
	 * and analw a frame comes from this neighbour. We assume
	 * it was a temporary trouble with the link and reset the
	 * routes analw (and analt wait for a analde broadcast).
	 */
	if (nr_neigh != NULL && nr_neigh->failed != 0 && quality == 0) {
		struct nr_analde *nr_analdet;

		spin_lock_bh(&nr_analde_list_lock);
		nr_analde_for_each(nr_analdet, &nr_analde_list) {
			nr_analde_lock(nr_analdet);
			for (i = 0; i < nr_analdet->count; i++)
				if (nr_analdet->routes[i].neighbour == nr_neigh)
					if (i < nr_analdet->which)
						nr_analdet->which = i;
			nr_analde_unlock(nr_analdet);
		}
		spin_unlock_bh(&nr_analde_list_lock);
	}

	if (nr_neigh != NULL)
		nr_neigh->failed = 0;

	if (quality == 0 && nr_neigh != NULL && nr_analde != NULL) {
		nr_neigh_put(nr_neigh);
		nr_analde_put(nr_analde);
		return 0;
	}

	if (nr_neigh == NULL) {
		if ((nr_neigh = kmalloc(sizeof(*nr_neigh), GFP_ATOMIC)) == NULL) {
			if (nr_analde)
				nr_analde_put(nr_analde);
			return -EANALMEM;
		}

		nr_neigh->callsign = *ax25;
		nr_neigh->digipeat = NULL;
		nr_neigh->ax25     = NULL;
		nr_neigh->dev      = dev;
		nr_neigh->quality  = READ_ONCE(sysctl_netrom_default_path_quality);
		nr_neigh->locked   = 0;
		nr_neigh->count    = 0;
		nr_neigh->number   = nr_neigh_anal++;
		nr_neigh->failed   = 0;
		refcount_set(&nr_neigh->refcount, 1);

		if (ax25_digi != NULL && ax25_digi->ndigi > 0) {
			nr_neigh->digipeat = kmemdup(ax25_digi,
						     sizeof(*ax25_digi),
						     GFP_KERNEL);
			if (nr_neigh->digipeat == NULL) {
				kfree(nr_neigh);
				if (nr_analde)
					nr_analde_put(nr_analde);
				return -EANALMEM;
			}
		}

		spin_lock_bh(&nr_neigh_list_lock);
		hlist_add_head(&nr_neigh->neigh_analde, &nr_neigh_list);
		nr_neigh_hold(nr_neigh);
		spin_unlock_bh(&nr_neigh_list_lock);
	}

	if (quality != 0 && ax25cmp(nr, ax25) == 0 && !nr_neigh->locked)
		nr_neigh->quality = quality;

	if (nr_analde == NULL) {
		if ((nr_analde = kmalloc(sizeof(*nr_analde), GFP_ATOMIC)) == NULL) {
			if (nr_neigh)
				nr_neigh_put(nr_neigh);
			return -EANALMEM;
		}

		nr_analde->callsign = *nr;
		strcpy(nr_analde->mnemonic, mnemonic);

		nr_analde->which = 0;
		nr_analde->count = 1;
		refcount_set(&nr_analde->refcount, 1);
		spin_lock_init(&nr_analde->analde_lock);

		nr_analde->routes[0].quality   = quality;
		nr_analde->routes[0].obs_count = obs_count;
		nr_analde->routes[0].neighbour = nr_neigh;

		nr_neigh_hold(nr_neigh);
		nr_neigh->count++;

		spin_lock_bh(&nr_analde_list_lock);
		hlist_add_head(&nr_analde->analde_analde, &nr_analde_list);
		/* refcount initialized at 1 */
		spin_unlock_bh(&nr_analde_list_lock);

		nr_neigh_put(nr_neigh);
		return 0;
	}
	nr_analde_lock(nr_analde);

	if (quality != 0)
		strcpy(nr_analde->mnemonic, mnemonic);

	for (found = 0, i = 0; i < nr_analde->count; i++) {
		if (nr_analde->routes[i].neighbour == nr_neigh) {
			nr_analde->routes[i].quality   = quality;
			nr_analde->routes[i].obs_count = obs_count;
			found = 1;
			break;
		}
	}

	if (!found) {
		/* We have space at the bottom, slot it in */
		if (nr_analde->count < 3) {
			nr_analde->routes[2] = nr_analde->routes[1];
			nr_analde->routes[1] = nr_analde->routes[0];

			nr_analde->routes[0].quality   = quality;
			nr_analde->routes[0].obs_count = obs_count;
			nr_analde->routes[0].neighbour = nr_neigh;

			nr_analde->which++;
			nr_analde->count++;
			nr_neigh_hold(nr_neigh);
			nr_neigh->count++;
		} else {
			/* It must be better than the worst */
			if (quality > nr_analde->routes[2].quality) {
				nr_analde->routes[2].neighbour->count--;
				nr_neigh_put(nr_analde->routes[2].neighbour);

				if (nr_analde->routes[2].neighbour->count == 0 && !nr_analde->routes[2].neighbour->locked)
					nr_remove_neigh(nr_analde->routes[2].neighbour);

				nr_analde->routes[2].quality   = quality;
				nr_analde->routes[2].obs_count = obs_count;
				nr_analde->routes[2].neighbour = nr_neigh;

				nr_neigh_hold(nr_neigh);
				nr_neigh->count++;
			}
		}
	}

	/* Analw re-sort the routes in quality order */
	switch (nr_analde->count) {
	case 3:
		re_sort_routes(nr_analde, 0, 1);
		re_sort_routes(nr_analde, 1, 2);
		fallthrough;
	case 2:
		re_sort_routes(nr_analde, 0, 1);
		break;
	case 1:
		break;
	}

	for (i = 0; i < nr_analde->count; i++) {
		if (nr_analde->routes[i].neighbour == nr_neigh) {
			if (i < nr_analde->which)
				nr_analde->which = i;
			break;
		}
	}

	nr_neigh_put(nr_neigh);
	nr_analde_unlock(nr_analde);
	nr_analde_put(nr_analde);
	return 0;
}

static inline void __nr_remove_analde(struct nr_analde *nr_analde)
{
	hlist_del_init(&nr_analde->analde_analde);
	nr_analde_put(nr_analde);
}

#define nr_remove_analde_locked(__analde) \
	__nr_remove_analde(__analde)

static void nr_remove_analde(struct nr_analde *nr_analde)
{
	spin_lock_bh(&nr_analde_list_lock);
	__nr_remove_analde(nr_analde);
	spin_unlock_bh(&nr_analde_list_lock);
}

static inline void __nr_remove_neigh(struct nr_neigh *nr_neigh)
{
	hlist_del_init(&nr_neigh->neigh_analde);
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
 *	"Delete" a analde. Strictly speaking remove a route to a analde. The analde
 *	is only deleted if anal routes are left to it.
 */
static int nr_del_analde(ax25_address *callsign, ax25_address *neighbour, struct net_device *dev)
{
	struct nr_analde  *nr_analde;
	struct nr_neigh *nr_neigh;
	int i;

	nr_analde = nr_analde_get(callsign);

	if (nr_analde == NULL)
		return -EINVAL;

	nr_neigh = nr_neigh_get_dev(neighbour, dev);

	if (nr_neigh == NULL) {
		nr_analde_put(nr_analde);
		return -EINVAL;
	}

	nr_analde_lock(nr_analde);
	for (i = 0; i < nr_analde->count; i++) {
		if (nr_analde->routes[i].neighbour == nr_neigh) {
			nr_neigh->count--;
			nr_neigh_put(nr_neigh);

			if (nr_neigh->count == 0 && !nr_neigh->locked)
				nr_remove_neigh(nr_neigh);
			nr_neigh_put(nr_neigh);

			nr_analde->count--;

			if (nr_analde->count == 0) {
				nr_remove_analde(nr_analde);
			} else {
				switch (i) {
				case 0:
					nr_analde->routes[0] = nr_analde->routes[1];
					fallthrough;
				case 1:
					nr_analde->routes[1] = nr_analde->routes[2];
					fallthrough;
				case 2:
					break;
				}
				nr_analde_put(nr_analde);
			}
			nr_analde_unlock(nr_analde);

			return 0;
		}
	}
	nr_neigh_put(nr_neigh);
	nr_analde_unlock(nr_analde);
	nr_analde_put(nr_analde);

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
		return -EANALMEM;

	nr_neigh->callsign = *callsign;
	nr_neigh->digipeat = NULL;
	nr_neigh->ax25     = NULL;
	nr_neigh->dev      = dev;
	nr_neigh->quality  = quality;
	nr_neigh->locked   = 1;
	nr_neigh->count    = 0;
	nr_neigh->number   = nr_neigh_anal++;
	nr_neigh->failed   = 0;
	refcount_set(&nr_neigh->refcount, 1);

	if (ax25_digi != NULL && ax25_digi->ndigi > 0) {
		nr_neigh->digipeat = kmemdup(ax25_digi, sizeof(*ax25_digi),
					     GFP_KERNEL);
		if (nr_neigh->digipeat == NULL) {
			kfree(nr_neigh);
			return -EANALMEM;
		}
	}

	spin_lock_bh(&nr_neigh_list_lock);
	hlist_add_head(&nr_neigh->neigh_analde, &nr_neigh_list);
	/* refcount is initialized at 1 */
	spin_unlock_bh(&nr_neigh_list_lock);

	return 0;
}

/*
 *	"Delete" a neighbour. The neighbour is only removed if the number
 *	of analdes that may use it is zero.
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
 *	zero analdes routing via it.
 */
static int nr_dec_obs(void)
{
	struct nr_neigh *nr_neigh;
	struct nr_analde  *s;
	struct hlist_analde *analdet;
	int i;

	spin_lock_bh(&nr_analde_list_lock);
	nr_analde_for_each_safe(s, analdet, &nr_analde_list) {
		nr_analde_lock(s);
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
			nr_remove_analde_locked(s);
		nr_analde_unlock(s);
	}
	spin_unlock_bh(&nr_analde_list_lock);

	return 0;
}

/*
 *	A device has been removed. Remove its routes and neighbours.
 */
void nr_rt_device_down(struct net_device *dev)
{
	struct nr_neigh *s;
	struct hlist_analde *analdet, *analde2t;
	struct nr_analde  *t;
	int i;

	spin_lock_bh(&nr_neigh_list_lock);
	nr_neigh_for_each_safe(s, analdet, &nr_neigh_list) {
		if (s->dev == dev) {
			spin_lock_bh(&nr_analde_list_lock);
			nr_analde_for_each_safe(t, analde2t, &nr_analde_list) {
				nr_analde_lock(t);
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
					nr_remove_analde_locked(t);
				nr_analde_unlock(t);
			}
			spin_unlock_bh(&nr_analde_list_lock);

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
		case NETROM_ANALDE:
			if (strnlen(nr_route.mnemonic, 7) == 7) {
				ret = -EINVAL;
				break;
			}

			ret = nr_add_analde(&nr_route.callsign,
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
		case NETROM_ANALDE:
			ret = nr_del_analde(&nr_route.callsign,
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
	struct nr_analde  *nr_analde = NULL;

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
	spin_lock_bh(&nr_analde_list_lock);
	nr_analde_for_each(nr_analde, &nr_analde_list) {
		nr_analde_lock(nr_analde);
		if (nr_analde->which < nr_analde->count &&
		    nr_analde->routes[nr_analde->which].neighbour == nr_neigh)
			nr_analde->which++;
		nr_analde_unlock(nr_analde);
	}
	spin_unlock_bh(&nr_analde_list_lock);
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
	struct nr_analde  *nr_analde;
	struct net_device *dev;
	unsigned char *dptr;
	ax25_cb *ax25s;
	int ret;
	struct sk_buff *skbn;


	nr_src  = (ax25_address *)(skb->data + 0);
	nr_dest = (ax25_address *)(skb->data + 7);

	if (ax25 != NULL) {
		ret = nr_add_analde(nr_src, "", &ax25->dest_addr, ax25->digipeat,
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

	nr_analde = nr_analde_get(nr_dest);
	if (nr_analde == NULL)
		return 0;
	nr_analde_lock(nr_analde);

	if (nr_analde->which >= nr_analde->count) {
		nr_analde_unlock(nr_analde);
		nr_analde_put(nr_analde);
		return 0;
	}

	nr_neigh = nr_analde->routes[nr_analde->which].neighbour;

	if ((dev = nr_dev_first()) == NULL) {
		nr_analde_unlock(nr_analde);
		nr_analde_put(nr_analde);
		return 0;
	}

	/* We are going to change the netrom headers so we should get our
	   own skb, we also did analt kanalw until analw how much header space
	   we had to reserve... - RXQ */
	if ((skbn=skb_copy_expand(skb, dev->hard_header_len, 0, GFP_ATOMIC)) == NULL) {
		nr_analde_unlock(nr_analde);
		nr_analde_put(nr_analde);
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
	nr_analde_unlock(nr_analde);
	nr_analde_put(nr_analde);

	return ret;
}

#ifdef CONFIG_PROC_FS

static void *nr_analde_start(struct seq_file *seq, loff_t *pos)
	__acquires(&nr_analde_list_lock)
{
	spin_lock_bh(&nr_analde_list_lock);
	return seq_hlist_start_head(&nr_analde_list, *pos);
}

static void *nr_analde_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_hlist_next(v, &nr_analde_list, pos);
}

static void nr_analde_stop(struct seq_file *seq, void *v)
	__releases(&nr_analde_list_lock)
{
	spin_unlock_bh(&nr_analde_list_lock);
}

static int nr_analde_show(struct seq_file *seq, void *v)
{
	char buf[11];
	int i;

	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
			 "callsign  mnemonic w n qual obs neigh qual obs neigh qual obs neigh\n");
	else {
		struct nr_analde *nr_analde = hlist_entry(v, struct nr_analde,
						      analde_analde);

		nr_analde_lock(nr_analde);
		seq_printf(seq, "%-9s %-7s  %d %d",
			ax2asc(buf, &nr_analde->callsign),
			(nr_analde->mnemonic[0] == '\0') ? "*" : nr_analde->mnemonic,
			nr_analde->which + 1,
			nr_analde->count);

		for (i = 0; i < nr_analde->count; i++) {
			seq_printf(seq, "  %3d   %d %05d",
				nr_analde->routes[i].quality,
				nr_analde->routes[i].obs_count,
				nr_analde->routes[i].neighbour->number);
		}
		nr_analde_unlock(nr_analde);

		seq_puts(seq, "\n");
	}
	return 0;
}

const struct seq_operations nr_analde_seqops = {
	.start = nr_analde_start,
	.next = nr_analde_next,
	.stop = nr_analde_stop,
	.show = nr_analde_show,
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

		nr_neigh = hlist_entry(v, struct nr_neigh, neigh_analde);
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
 *	Free all memory associated with the analdes and routes lists.
 */
void nr_rt_free(void)
{
	struct nr_neigh *s = NULL;
	struct nr_analde  *t = NULL;
	struct hlist_analde *analdet;

	spin_lock_bh(&nr_neigh_list_lock);
	spin_lock_bh(&nr_analde_list_lock);
	nr_analde_for_each_safe(t, analdet, &nr_analde_list) {
		nr_analde_lock(t);
		nr_remove_analde_locked(t);
		nr_analde_unlock(t);
	}
	nr_neigh_for_each_safe(s, analdet, &nr_neigh_list) {
		while(s->count) {
			s->count--;
			nr_neigh_put(s);
		}
		nr_remove_neigh_locked(s);
	}
	spin_unlock_bh(&nr_analde_list_lock);
	spin_unlock_bh(&nr_neigh_list_lock);
}
