// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2007-2020  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 */

#include "hard-interface.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/byteorder/generic.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <net/net_namespace.h>
#include <net/rtnetlink.h>
#include <uapi/linux/batadv_packet.h>

#include "bat_v.h"
#include "bridge_loop_avoidance.h"
#include "debugfs.h"
#include "distributed-arp-table.h"
#include "gateway_client.h"
#include "log.h"
#include "originator.h"
#include "send.h"
#include "soft-interface.h"
#include "sysfs.h"
#include "translation-table.h"

/**
 * batadv_hardif_release() - release hard interface from lists and queue for
 *  free after rcu grace period
 * @ref: kref pointer of the hard interface
 */
void batadv_hardif_release(struct kref *ref)
{
	struct batadv_hard_iface *hard_iface;

	hard_iface = container_of(ref, struct batadv_hard_iface, refcount);
	dev_put(hard_iface->net_dev);

	kfree_rcu(hard_iface, rcu);
}

/**
 * batadv_hardif_get_by_netdev() - Get hard interface object of a net_device
 * @net_dev: net_device to search for
 *
 * Return: batadv_hard_iface of net_dev (with increased refcnt), NULL on errors
 */
struct batadv_hard_iface *
batadv_hardif_get_by_netdev(const struct net_device *net_dev)
{
	struct batadv_hard_iface *hard_iface;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->net_dev == net_dev &&
		    kref_get_unless_zero(&hard_iface->refcount))
			goto out;
	}

	hard_iface = NULL;

out:
	rcu_read_unlock();
	return hard_iface;
}

/**
 * batadv_getlink_net() - return link net namespace (of use fallback)
 * @netdev: net_device to check
 * @fallback_net: return in case get_link_net is not available for @netdev
 *
 * Return: result of rtnl_link_ops->get_link_net or @fallback_net
 */
static struct net *batadv_getlink_net(const struct net_device *netdev,
				      struct net *fallback_net)
{
	if (!netdev->rtnl_link_ops)
		return fallback_net;

	if (!netdev->rtnl_link_ops->get_link_net)
		return fallback_net;

	return netdev->rtnl_link_ops->get_link_net(netdev);
}

/**
 * batadv_mutual_parents() - check if two devices are each others parent
 * @dev1: 1st net dev
 * @net1: 1st devices netns
 * @dev2: 2nd net dev
 * @net2: 2nd devices netns
 *
 * veth devices come in pairs and each is the parent of the other!
 *
 * Return: true if the devices are each others parent, otherwise false
 */
static bool batadv_mutual_parents(const struct net_device *dev1,
				  struct net *net1,
				  const struct net_device *dev2,
				  struct net *net2)
{
	int dev1_parent_iflink = dev_get_iflink(dev1);
	int dev2_parent_iflink = dev_get_iflink(dev2);
	const struct net *dev1_parent_net;
	const struct net *dev2_parent_net;

	dev1_parent_net = batadv_getlink_net(dev1, net1);
	dev2_parent_net = batadv_getlink_net(dev2, net2);

	if (!dev1_parent_iflink || !dev2_parent_iflink)
		return false;

	return (dev1_parent_iflink == dev2->ifindex) &&
	       (dev2_parent_iflink == dev1->ifindex) &&
	       net_eq(dev1_parent_net, net2) &&
	       net_eq(dev2_parent_net, net1);
}

/**
 * batadv_is_on_batman_iface() - check if a device is a batman iface descendant
 * @net_dev: the device to check
 *
 * If the user creates any virtual device on top of a batman-adv interface, it
 * is important to prevent this new interface from being used to create a new
 * mesh network (this behaviour would lead to a batman-over-batman
 * configuration). This function recursively checks all the fathers of the
 * device passed as argument looking for a batman-adv soft interface.
 *
 * Return: true if the device is descendant of a batman-adv mesh interface (or
 * if it is a batman-adv interface itself), false otherwise
 */
static bool batadv_is_on_batman_iface(const struct net_device *net_dev)
{
	struct net *net = dev_net(net_dev);
	struct net_device *parent_dev;
	struct net *parent_net;
	int iflink;
	bool ret;

	/* check if this is a batman-adv mesh interface */
	if (batadv_softif_is_valid(net_dev))
		return true;

	iflink = dev_get_iflink(net_dev);
	if (iflink == 0)
		return false;

	parent_net = batadv_getlink_net(net_dev, net);

	/* iflink to itself, most likely physical device */
	if (net == parent_net && iflink == net_dev->ifindex)
		return false;

	/* recurse over the parent device */
	parent_dev = __dev_get_by_index((struct net *)parent_net, iflink);
	/* if we got a NULL parent_dev there is something broken.. */
	if (!parent_dev) {
		pr_err("Cannot find parent device\n");
		return false;
	}

	if (batadv_mutual_parents(net_dev, net, parent_dev, parent_net))
		return false;

	ret = batadv_is_on_batman_iface(parent_dev);

	return ret;
}

static bool batadv_is_valid_iface(const struct net_device *net_dev)
{
	if (net_dev->flags & IFF_LOOPBACK)
		return false;

	if (net_dev->type != ARPHRD_ETHER)
		return false;

	if (net_dev->addr_len != ETH_ALEN)
		return false;

	/* no batman over batman */
	if (batadv_is_on_batman_iface(net_dev))
		return false;

	return true;
}

/**
 * batadv_get_real_netdevice() - check if the given netdev struct is a virtual
 *  interface on top of another 'real' interface
 * @netdev: the device to check
 *
 * Callers must hold the rtnl semaphore. You may want batadv_get_real_netdev()
 * instead of this.
 *
 * Return: the 'real' net device or the original net device and NULL in case
 *  of an error.
 */
static struct net_device *batadv_get_real_netdevice(struct net_device *netdev)
{
	struct batadv_hard_iface *hard_iface = NULL;
	struct net_device *real_netdev = NULL;
	struct net *real_net;
	struct net *net;
	int iflink;

	ASSERT_RTNL();

	if (!netdev)
		return NULL;

	iflink = dev_get_iflink(netdev);
	if (iflink == 0) {
		dev_hold(netdev);
		return netdev;
	}

	hard_iface = batadv_hardif_get_by_netdev(netdev);
	if (!hard_iface || !hard_iface->soft_iface)
		goto out;

	net = dev_net(hard_iface->soft_iface);
	real_net = batadv_getlink_net(netdev, net);

	/* iflink to itself, most likely physical device */
	if (net == real_net && netdev->ifindex == iflink) {
		real_netdev = netdev;
		dev_hold(real_netdev);
		goto out;
	}

	real_netdev = dev_get_by_index(real_net, iflink);

out:
	if (hard_iface)
		batadv_hardif_put(hard_iface);
	return real_netdev;
}

/**
 * batadv_get_real_netdev() - check if the given net_device struct is a virtual
 *  interface on top of another 'real' interface
 * @net_device: the device to check
 *
 * Return: the 'real' net device or the original net device and NULL in case
 *  of an error.
 */
struct net_device *batadv_get_real_netdev(struct net_device *net_device)
{
	struct net_device *real_netdev;

	rtnl_lock();
	real_netdev = batadv_get_real_netdevice(net_device);
	rtnl_unlock();

	return real_netdev;
}

/**
 * batadv_is_wext_netdev() - check if the given net_device struct is a
 *  wext wifi interface
 * @net_device: the device to check
 *
 * Return: true if the net device is a wext wireless device, false
 *  otherwise.
 */
static bool batadv_is_wext_netdev(struct net_device *net_device)
{
	if (!net_device)
		return false;

#ifdef CONFIG_WIRELESS_EXT
	/* pre-cfg80211 drivers have to implement WEXT, so it is possible to
	 * check for wireless_handlers != NULL
	 */
	if (net_device->wireless_handlers)
		return true;
#endif

	return false;
}

/**
 * batadv_is_cfg80211_netdev() - check if the given net_device struct is a
 *  cfg80211 wifi interface
 * @net_device: the device to check
 *
 * Return: true if the net device is a cfg80211 wireless device, false
 *  otherwise.
 */
static bool batadv_is_cfg80211_netdev(struct net_device *net_device)
{
	if (!net_device)
		return false;

	/* cfg80211 drivers have to set ieee80211_ptr */
	if (net_device->ieee80211_ptr)
		return true;

	return false;
}

/**
 * batadv_wifi_flags_evaluate() - calculate wifi flags for net_device
 * @net_device: the device to check
 *
 * Return: batadv_hard_iface_wifi_flags flags of the device
 */
static u32 batadv_wifi_flags_evaluate(struct net_device *net_device)
{
	u32 wifi_flags = 0;
	struct net_device *real_netdev;

	if (batadv_is_wext_netdev(net_device))
		wifi_flags |= BATADV_HARDIF_WIFI_WEXT_DIRECT;

	if (batadv_is_cfg80211_netdev(net_device))
		wifi_flags |= BATADV_HARDIF_WIFI_CFG80211_DIRECT;

	real_netdev = batadv_get_real_netdevice(net_device);
	if (!real_netdev)
		return wifi_flags;

	if (real_netdev == net_device)
		goto out;

	if (batadv_is_wext_netdev(real_netdev))
		wifi_flags |= BATADV_HARDIF_WIFI_WEXT_INDIRECT;

	if (batadv_is_cfg80211_netdev(real_netdev))
		wifi_flags |= BATADV_HARDIF_WIFI_CFG80211_INDIRECT;

out:
	dev_put(real_netdev);
	return wifi_flags;
}

/**
 * batadv_is_cfg80211_hardif() - check if the given hardif is a cfg80211 wifi
 *  interface
 * @hard_iface: the device to check
 *
 * Return: true if the net device is a cfg80211 wireless device, false
 *  otherwise.
 */
bool batadv_is_cfg80211_hardif(struct batadv_hard_iface *hard_iface)
{
	u32 allowed_flags = 0;

	allowed_flags |= BATADV_HARDIF_WIFI_CFG80211_DIRECT;
	allowed_flags |= BATADV_HARDIF_WIFI_CFG80211_INDIRECT;

	return !!(hard_iface->wifi_flags & allowed_flags);
}

/**
 * batadv_is_wifi_hardif() - check if the given hardif is a wifi interface
 * @hard_iface: the device to check
 *
 * Return: true if the net device is a 802.11 wireless device, false otherwise.
 */
bool batadv_is_wifi_hardif(struct batadv_hard_iface *hard_iface)
{
	if (!hard_iface)
		return false;

	return hard_iface->wifi_flags != 0;
}

/**
 * batadv_hardif_no_broadcast() - check whether (re)broadcast is necessary
 * @if_outgoing: the outgoing interface checked and considered for (re)broadcast
 * @orig_addr: the originator of this packet
 * @orig_neigh: originator address of the forwarder we just got the packet from
 *  (NULL if we originated)
 *
 * Checks whether a packet needs to be (re)broadcasted on the given interface.
 *
 * Return:
 *	BATADV_HARDIF_BCAST_NORECIPIENT: No neighbor on interface
 *	BATADV_HARDIF_BCAST_DUPFWD: Just one neighbor, but it is the forwarder
 *	BATADV_HARDIF_BCAST_DUPORIG: Just one neighbor, but it is the originator
 *	BATADV_HARDIF_BCAST_OK: Several neighbors, must broadcast
 */
int batadv_hardif_no_broadcast(struct batadv_hard_iface *if_outgoing,
			       u8 *orig_addr, u8 *orig_neigh)
{
	struct batadv_hardif_neigh_node *hardif_neigh;
	struct hlist_node *first;
	int ret = BATADV_HARDIF_BCAST_OK;

	rcu_read_lock();

	/* 0 neighbors -> no (re)broadcast */
	first = rcu_dereference(hlist_first_rcu(&if_outgoing->neigh_list));
	if (!first) {
		ret = BATADV_HARDIF_BCAST_NORECIPIENT;
		goto out;
	}

	/* >1 neighbors -> (re)brodcast */
	if (rcu_dereference(hlist_next_rcu(first)))
		goto out;

	hardif_neigh = hlist_entry(first, struct batadv_hardif_neigh_node,
				   list);

	/* 1 neighbor, is the originator -> no rebroadcast */
	if (orig_addr && batadv_compare_eth(hardif_neigh->orig, orig_addr)) {
		ret = BATADV_HARDIF_BCAST_DUPORIG;
	/* 1 neighbor, is the one we received from -> no rebroadcast */
	} else if (orig_neigh &&
		   batadv_compare_eth(hardif_neigh->orig, orig_neigh)) {
		ret = BATADV_HARDIF_BCAST_DUPFWD;
	}

out:
	rcu_read_unlock();
	return ret;
}

static struct batadv_hard_iface *
batadv_hardif_get_active(const struct net_device *soft_iface)
{
	struct batadv_hard_iface *hard_iface;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->soft_iface != soft_iface)
			continue;

		if (hard_iface->if_status == BATADV_IF_ACTIVE &&
		    kref_get_unless_zero(&hard_iface->refcount))
			goto out;
	}

	hard_iface = NULL;

out:
	rcu_read_unlock();
	return hard_iface;
}

static void batadv_primary_if_update_addr(struct batadv_priv *bat_priv,
					  struct batadv_hard_iface *oldif)
{
	struct batadv_hard_iface *primary_if;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	batadv_dat_init_own_addr(bat_priv, primary_if);
	batadv_bla_update_orig_address(bat_priv, primary_if, oldif);
out:
	if (primary_if)
		batadv_hardif_put(primary_if);
}

static void batadv_primary_if_select(struct batadv_priv *bat_priv,
				     struct batadv_hard_iface *new_hard_iface)
{
	struct batadv_hard_iface *curr_hard_iface;

	ASSERT_RTNL();

	if (new_hard_iface)
		kref_get(&new_hard_iface->refcount);

	curr_hard_iface = rcu_replace_pointer(bat_priv->primary_if,
					      new_hard_iface, 1);

	if (!new_hard_iface)
		goto out;

	bat_priv->algo_ops->iface.primary_set(new_hard_iface);
	batadv_primary_if_update_addr(bat_priv, curr_hard_iface);

out:
	if (curr_hard_iface)
		batadv_hardif_put(curr_hard_iface);
}

static bool
batadv_hardif_is_iface_up(const struct batadv_hard_iface *hard_iface)
{
	if (hard_iface->net_dev->flags & IFF_UP)
		return true;

	return false;
}

static void batadv_check_known_mac_addr(const struct net_device *net_dev)
{
	const struct batadv_hard_iface *hard_iface;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->if_status != BATADV_IF_ACTIVE &&
		    hard_iface->if_status != BATADV_IF_TO_BE_ACTIVATED)
			continue;

		if (hard_iface->net_dev == net_dev)
			continue;

		if (!batadv_compare_eth(hard_iface->net_dev->dev_addr,
					net_dev->dev_addr))
			continue;

		pr_warn("The newly added mac address (%pM) already exists on: %s\n",
			net_dev->dev_addr, hard_iface->net_dev->name);
		pr_warn("It is strongly recommended to keep mac addresses unique to avoid problems!\n");
	}
	rcu_read_unlock();
}

/**
 * batadv_hardif_recalc_extra_skbroom() - Recalculate skbuff extra head/tailroom
 * @soft_iface: netdev struct of the mesh interface
 */
static void batadv_hardif_recalc_extra_skbroom(struct net_device *soft_iface)
{
	const struct batadv_hard_iface *hard_iface;
	unsigned short lower_header_len = ETH_HLEN;
	unsigned short lower_headroom = 0;
	unsigned short lower_tailroom = 0;
	unsigned short needed_headroom;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->if_status == BATADV_IF_NOT_IN_USE)
			continue;

		if (hard_iface->soft_iface != soft_iface)
			continue;

		lower_header_len = max_t(unsigned short, lower_header_len,
					 hard_iface->net_dev->hard_header_len);

		lower_headroom = max_t(unsigned short, lower_headroom,
				       hard_iface->net_dev->needed_headroom);

		lower_tailroom = max_t(unsigned short, lower_tailroom,
				       hard_iface->net_dev->needed_tailroom);
	}
	rcu_read_unlock();

	needed_headroom = lower_headroom + (lower_header_len - ETH_HLEN);
	needed_headroom += batadv_max_header_len();

	/* fragmentation headers don't strip the unicast/... header */
	needed_headroom += sizeof(struct batadv_frag_packet);

	soft_iface->needed_headroom = needed_headroom;
	soft_iface->needed_tailroom = lower_tailroom;
}

/**
 * batadv_hardif_min_mtu() - Calculate maximum MTU for soft interface
 * @soft_iface: netdev struct of the soft interface
 *
 * Return: MTU for the soft-interface (limited by the minimal MTU of all active
 *  slave interfaces)
 */
int batadv_hardif_min_mtu(struct net_device *soft_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(soft_iface);
	const struct batadv_hard_iface *hard_iface;
	int min_mtu = INT_MAX;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->if_status != BATADV_IF_ACTIVE &&
		    hard_iface->if_status != BATADV_IF_TO_BE_ACTIVATED)
			continue;

		if (hard_iface->soft_iface != soft_iface)
			continue;

		min_mtu = min_t(int, hard_iface->net_dev->mtu, min_mtu);
	}
	rcu_read_unlock();

	if (atomic_read(&bat_priv->fragmentation) == 0)
		goto out;

	/* with fragmentation enabled the maximum size of internally generated
	 * packets such as translation table exchanges or tvlv containers, etc
	 * has to be calculated
	 */
	min_mtu = min_t(int, min_mtu, BATADV_FRAG_MAX_FRAG_SIZE);
	min_mtu -= sizeof(struct batadv_frag_packet);
	min_mtu *= BATADV_FRAG_MAX_FRAGMENTS;

out:
	/* report to the other components the maximum amount of bytes that
	 * batman-adv can send over the wire (without considering the payload
	 * overhead). For example, this value is used by TT to compute the
	 * maximum local table size
	 */
	atomic_set(&bat_priv->packet_size_max, min_mtu);

	/* the real soft-interface MTU is computed by removing the payload
	 * overhead from the maximum amount of bytes that was just computed.
	 *
	 * However batman-adv does not support MTUs bigger than ETH_DATA_LEN
	 */
	return min_t(int, min_mtu - batadv_max_header_len(), ETH_DATA_LEN);
}

/**
 * batadv_update_min_mtu() - Adjusts the MTU if a new interface with a smaller
 *  MTU appeared
 * @soft_iface: netdev struct of the soft interface
 */
void batadv_update_min_mtu(struct net_device *soft_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(soft_iface);
	int limit_mtu;
	int mtu;

	mtu = batadv_hardif_min_mtu(soft_iface);

	if (bat_priv->mtu_set_by_user)
		limit_mtu = bat_priv->mtu_set_by_user;
	else
		limit_mtu = ETH_DATA_LEN;

	mtu = min(mtu, limit_mtu);
	dev_set_mtu(soft_iface, mtu);

	/* Check if the local translate table should be cleaned up to match a
	 * new (and smaller) MTU.
	 */
	batadv_tt_local_resize_to_mtu(soft_iface);
}

static void
batadv_hardif_activate_interface(struct batadv_hard_iface *hard_iface)
{
	struct batadv_priv *bat_priv;
	struct batadv_hard_iface *primary_if = NULL;

	if (hard_iface->if_status != BATADV_IF_INACTIVE)
		goto out;

	bat_priv = netdev_priv(hard_iface->soft_iface);

	bat_priv->algo_ops->iface.update_mac(hard_iface);
	hard_iface->if_status = BATADV_IF_TO_BE_ACTIVATED;

	/* the first active interface becomes our primary interface or
	 * the next active interface after the old primary interface was removed
	 */
	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		batadv_primary_if_select(bat_priv, hard_iface);

	batadv_info(hard_iface->soft_iface, "Interface activated: %s\n",
		    hard_iface->net_dev->name);

	batadv_update_min_mtu(hard_iface->soft_iface);

	if (bat_priv->algo_ops->iface.activate)
		bat_priv->algo_ops->iface.activate(hard_iface);

out:
	if (primary_if)
		batadv_hardif_put(primary_if);
}

static void
batadv_hardif_deactivate_interface(struct batadv_hard_iface *hard_iface)
{
	if (hard_iface->if_status != BATADV_IF_ACTIVE &&
	    hard_iface->if_status != BATADV_IF_TO_BE_ACTIVATED)
		return;

	hard_iface->if_status = BATADV_IF_INACTIVE;

	batadv_info(hard_iface->soft_iface, "Interface deactivated: %s\n",
		    hard_iface->net_dev->name);

	batadv_update_min_mtu(hard_iface->soft_iface);
}

/**
 * batadv_master_del_slave() - remove hard_iface from the current master iface
 * @slave: the interface enslaved in another master
 * @master: the master from which slave has to be removed
 *
 * Invoke ndo_del_slave on master passing slave as argument. In this way the
 * slave is free'd and the master can correctly change its internal state.
 *
 * Return: 0 on success, a negative value representing the error otherwise
 */
static int batadv_master_del_slave(struct batadv_hard_iface *slave,
				   struct net_device *master)
{
	int ret;

	if (!master)
		return 0;

	ret = -EBUSY;
	if (master->netdev_ops->ndo_del_slave)
		ret = master->netdev_ops->ndo_del_slave(master, slave->net_dev);

	return ret;
}

/**
 * batadv_hardif_enable_interface() - Enslave hard interface to soft interface
 * @hard_iface: hard interface to add to soft interface
 * @net: the applicable net namespace
 * @iface_name: name of the soft interface
 *
 * Return: 0 on success or negative error number in case of failure
 */
int batadv_hardif_enable_interface(struct batadv_hard_iface *hard_iface,
				   struct net *net, const char *iface_name)
{
	struct batadv_priv *bat_priv;
	struct net_device *soft_iface, *master;
	__be16 ethertype = htons(ETH_P_BATMAN);
	int max_header_len = batadv_max_header_len();
	int ret;

	if (hard_iface->if_status != BATADV_IF_NOT_IN_USE)
		goto out;

	kref_get(&hard_iface->refcount);

	soft_iface = dev_get_by_name(net, iface_name);

	if (!soft_iface) {
		soft_iface = batadv_softif_create(net, iface_name);

		if (!soft_iface) {
			ret = -ENOMEM;
			goto err;
		}

		/* dev_get_by_name() increases the reference counter for us */
		dev_hold(soft_iface);
	}

	if (!batadv_softif_is_valid(soft_iface)) {
		pr_err("Can't create batman mesh interface %s: already exists as regular interface\n",
		       soft_iface->name);
		ret = -EINVAL;
		goto err_dev;
	}

	/* check if the interface is enslaved in another virtual one and
	 * in that case unlink it first
	 */
	master = netdev_master_upper_dev_get(hard_iface->net_dev);
	ret = batadv_master_del_slave(hard_iface, master);
	if (ret)
		goto err_dev;

	hard_iface->soft_iface = soft_iface;
	bat_priv = netdev_priv(hard_iface->soft_iface);

	ret = netdev_master_upper_dev_link(hard_iface->net_dev,
					   soft_iface, NULL, NULL, NULL);
	if (ret)
		goto err_dev;

	ret = bat_priv->algo_ops->iface.enable(hard_iface);
	if (ret < 0)
		goto err_upper;

	hard_iface->if_status = BATADV_IF_INACTIVE;

	kref_get(&hard_iface->refcount);
	hard_iface->batman_adv_ptype.type = ethertype;
	hard_iface->batman_adv_ptype.func = batadv_batman_skb_recv;
	hard_iface->batman_adv_ptype.dev = hard_iface->net_dev;
	dev_add_pack(&hard_iface->batman_adv_ptype);

	batadv_info(hard_iface->soft_iface, "Adding interface: %s\n",
		    hard_iface->net_dev->name);

	if (atomic_read(&bat_priv->fragmentation) &&
	    hard_iface->net_dev->mtu < ETH_DATA_LEN + max_header_len)
		batadv_info(hard_iface->soft_iface,
			    "The MTU of interface %s is too small (%i) to handle the transport of batman-adv packets. Packets going over this interface will be fragmented on layer2 which could impact the performance. Setting the MTU to %i would solve the problem.\n",
			    hard_iface->net_dev->name, hard_iface->net_dev->mtu,
			    ETH_DATA_LEN + max_header_len);

	if (!atomic_read(&bat_priv->fragmentation) &&
	    hard_iface->net_dev->mtu < ETH_DATA_LEN + max_header_len)
		batadv_info(hard_iface->soft_iface,
			    "The MTU of interface %s is too small (%i) to handle the transport of batman-adv packets. If you experience problems getting traffic through try increasing the MTU to %i.\n",
			    hard_iface->net_dev->name, hard_iface->net_dev->mtu,
			    ETH_DATA_LEN + max_header_len);

	if (batadv_hardif_is_iface_up(hard_iface))
		batadv_hardif_activate_interface(hard_iface);
	else
		batadv_err(hard_iface->soft_iface,
			   "Not using interface %s (retrying later): interface not active\n",
			   hard_iface->net_dev->name);

	batadv_hardif_recalc_extra_skbroom(soft_iface);

	if (bat_priv->algo_ops->iface.enabled)
		bat_priv->algo_ops->iface.enabled(hard_iface);

out:
	return 0;

err_upper:
	netdev_upper_dev_unlink(hard_iface->net_dev, soft_iface);
err_dev:
	hard_iface->soft_iface = NULL;
	dev_put(soft_iface);
err:
	batadv_hardif_put(hard_iface);
	return ret;
}

/**
 * batadv_hardif_cnt() - get number of interfaces enslaved to soft interface
 * @soft_iface: soft interface to check
 *
 * This function is only using RCU for locking - the result can therefore be
 * off when another function is modifying the list at the same time. The
 * caller can use the rtnl_lock to make sure that the count is accurate.
 *
 * Return: number of connected/enslaved hard interfaces
 */
static size_t batadv_hardif_cnt(const struct net_device *soft_iface)
{
	struct batadv_hard_iface *hard_iface;
	size_t count = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->soft_iface != soft_iface)
			continue;

		count++;
	}
	rcu_read_unlock();

	return count;
}

/**
 * batadv_hardif_disable_interface() - Remove hard interface from soft interface
 * @hard_iface: hard interface to be removed
 * @autodel: whether to delete soft interface when it doesn't contain any other
 *  slave interfaces
 */
void batadv_hardif_disable_interface(struct batadv_hard_iface *hard_iface,
				     enum batadv_hard_if_cleanup autodel)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct batadv_hard_iface *primary_if = NULL;

	batadv_hardif_deactivate_interface(hard_iface);

	if (hard_iface->if_status != BATADV_IF_INACTIVE)
		goto out;

	batadv_info(hard_iface->soft_iface, "Removing interface: %s\n",
		    hard_iface->net_dev->name);
	dev_remove_pack(&hard_iface->batman_adv_ptype);
	batadv_hardif_put(hard_iface);

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (hard_iface == primary_if) {
		struct batadv_hard_iface *new_if;

		new_if = batadv_hardif_get_active(hard_iface->soft_iface);
		batadv_primary_if_select(bat_priv, new_if);

		if (new_if)
			batadv_hardif_put(new_if);
	}

	bat_priv->algo_ops->iface.disable(hard_iface);
	hard_iface->if_status = BATADV_IF_NOT_IN_USE;

	/* delete all references to this hard_iface */
	batadv_purge_orig_ref(bat_priv);
	batadv_purge_outstanding_packets(bat_priv, hard_iface);
	dev_put(hard_iface->soft_iface);

	netdev_upper_dev_unlink(hard_iface->net_dev, hard_iface->soft_iface);
	batadv_hardif_recalc_extra_skbroom(hard_iface->soft_iface);

	/* nobody uses this interface anymore */
	if (batadv_hardif_cnt(hard_iface->soft_iface) <= 1) {
		batadv_gw_check_client_stop(bat_priv);

		if (autodel == BATADV_IF_CLEANUP_AUTO)
			batadv_softif_destroy_sysfs(hard_iface->soft_iface);
	}

	hard_iface->soft_iface = NULL;
	batadv_hardif_put(hard_iface);

out:
	if (primary_if)
		batadv_hardif_put(primary_if);
}

static struct batadv_hard_iface *
batadv_hardif_add_interface(struct net_device *net_dev)
{
	struct batadv_hard_iface *hard_iface;
	int ret;

	ASSERT_RTNL();

	if (!batadv_is_valid_iface(net_dev))
		goto out;

	dev_hold(net_dev);

	hard_iface = kzalloc(sizeof(*hard_iface), GFP_ATOMIC);
	if (!hard_iface)
		goto release_dev;

	ret = batadv_sysfs_add_hardif(&hard_iface->hardif_obj, net_dev);
	if (ret)
		goto free_if;

	hard_iface->net_dev = net_dev;
	hard_iface->soft_iface = NULL;
	hard_iface->if_status = BATADV_IF_NOT_IN_USE;

	batadv_debugfs_add_hardif(hard_iface);

	INIT_LIST_HEAD(&hard_iface->list);
	INIT_HLIST_HEAD(&hard_iface->neigh_list);

	mutex_init(&hard_iface->bat_iv.ogm_buff_mutex);
	spin_lock_init(&hard_iface->neigh_list_lock);
	kref_init(&hard_iface->refcount);

	hard_iface->num_bcasts = BATADV_NUM_BCASTS_DEFAULT;
	hard_iface->wifi_flags = batadv_wifi_flags_evaluate(net_dev);
	if (batadv_is_wifi_hardif(hard_iface))
		hard_iface->num_bcasts = BATADV_NUM_BCASTS_WIRELESS;

	atomic_set(&hard_iface->hop_penalty, 0);

	batadv_v_hardif_init(hard_iface);

	batadv_check_known_mac_addr(hard_iface->net_dev);
	kref_get(&hard_iface->refcount);
	list_add_tail_rcu(&hard_iface->list, &batadv_hardif_list);
	batadv_hardif_generation++;

	return hard_iface;

free_if:
	kfree(hard_iface);
release_dev:
	dev_put(net_dev);
out:
	return NULL;
}

static void batadv_hardif_remove_interface(struct batadv_hard_iface *hard_iface)
{
	ASSERT_RTNL();

	/* first deactivate interface */
	if (hard_iface->if_status != BATADV_IF_NOT_IN_USE)
		batadv_hardif_disable_interface(hard_iface,
						BATADV_IF_CLEANUP_KEEP);

	if (hard_iface->if_status != BATADV_IF_NOT_IN_USE)
		return;

	hard_iface->if_status = BATADV_IF_TO_BE_REMOVED;
	batadv_debugfs_del_hardif(hard_iface);
	batadv_sysfs_del_hardif(&hard_iface->hardif_obj);
	batadv_hardif_put(hard_iface);
}

/**
 * batadv_hard_if_event_softif() - Handle events for soft interfaces
 * @event: NETDEV_* event to handle
 * @net_dev: net_device which generated an event
 *
 * Return: NOTIFY_* result
 */
static int batadv_hard_if_event_softif(unsigned long event,
				       struct net_device *net_dev)
{
	struct batadv_priv *bat_priv;

	switch (event) {
	case NETDEV_REGISTER:
		batadv_sysfs_add_meshif(net_dev);
		bat_priv = netdev_priv(net_dev);
		batadv_softif_create_vlan(bat_priv, BATADV_NO_FLAGS);
		break;
	case NETDEV_CHANGENAME:
		batadv_debugfs_rename_meshif(net_dev);
		break;
	}

	return NOTIFY_DONE;
}

static int batadv_hard_if_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct net_device *net_dev = netdev_notifier_info_to_dev(ptr);
	struct batadv_hard_iface *hard_iface;
	struct batadv_hard_iface *primary_if = NULL;
	struct batadv_priv *bat_priv;

	if (batadv_softif_is_valid(net_dev))
		return batadv_hard_if_event_softif(event, net_dev);

	hard_iface = batadv_hardif_get_by_netdev(net_dev);
	if (!hard_iface && (event == NETDEV_REGISTER ||
			    event == NETDEV_POST_TYPE_CHANGE))
		hard_iface = batadv_hardif_add_interface(net_dev);

	if (!hard_iface)
		goto out;

	switch (event) {
	case NETDEV_UP:
		batadv_hardif_activate_interface(hard_iface);
		break;
	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:
		batadv_hardif_deactivate_interface(hard_iface);
		break;
	case NETDEV_UNREGISTER:
	case NETDEV_PRE_TYPE_CHANGE:
		list_del_rcu(&hard_iface->list);
		batadv_hardif_generation++;

		batadv_hardif_remove_interface(hard_iface);
		break;
	case NETDEV_CHANGEMTU:
		if (hard_iface->soft_iface)
			batadv_update_min_mtu(hard_iface->soft_iface);
		break;
	case NETDEV_CHANGEADDR:
		if (hard_iface->if_status == BATADV_IF_NOT_IN_USE)
			goto hardif_put;

		batadv_check_known_mac_addr(hard_iface->net_dev);

		bat_priv = netdev_priv(hard_iface->soft_iface);
		bat_priv->algo_ops->iface.update_mac(hard_iface);

		primary_if = batadv_primary_if_get_selected(bat_priv);
		if (!primary_if)
			goto hardif_put;

		if (hard_iface == primary_if)
			batadv_primary_if_update_addr(bat_priv, NULL);
		break;
	case NETDEV_CHANGEUPPER:
		hard_iface->wifi_flags = batadv_wifi_flags_evaluate(net_dev);
		if (batadv_is_wifi_hardif(hard_iface))
			hard_iface->num_bcasts = BATADV_NUM_BCASTS_WIRELESS;
		break;
	case NETDEV_CHANGENAME:
		batadv_debugfs_rename_hardif(hard_iface);
		break;
	default:
		break;
	}

hardif_put:
	batadv_hardif_put(hard_iface);
out:
	if (primary_if)
		batadv_hardif_put(primary_if);
	return NOTIFY_DONE;
}

struct notifier_block batadv_hard_if_notifier = {
	.notifier_call = batadv_hard_if_event,
};
