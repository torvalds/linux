/* Copyright (C) 2007-2014 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "main.h"
#include "distributed-arp-table.h"
#include "hard-interface.h"
#include "soft-interface.h"
#include "send.h"
#include "translation-table.h"
#include "routing.h"
#include "sysfs.h"
#include "debugfs.h"
#include "originator.h"
#include "hash.h"
#include "bridge_loop_avoidance.h"
#include "gateway_client.h"

#include <linux/if_arp.h>
#include <linux/if_ether.h>

void batadv_hardif_free_rcu(struct rcu_head *rcu)
{
	struct batadv_hard_iface *hard_iface;

	hard_iface = container_of(rcu, struct batadv_hard_iface, rcu);
	dev_put(hard_iface->net_dev);
	kfree(hard_iface);
}

struct batadv_hard_iface *
batadv_hardif_get_by_netdev(const struct net_device *net_dev)
{
	struct batadv_hard_iface *hard_iface;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->net_dev == net_dev &&
		    atomic_inc_not_zero(&hard_iface->refcount))
			goto out;
	}

	hard_iface = NULL;

out:
	rcu_read_unlock();
	return hard_iface;
}

/**
 * batadv_is_on_batman_iface - check if a device is a batman iface descendant
 * @net_dev: the device to check
 *
 * If the user creates any virtual device on top of a batman-adv interface, it
 * is important to prevent this new interface to be used to create a new mesh
 * network (this behaviour would lead to a batman-over-batman configuration).
 * This function recursively checks all the fathers of the device passed as
 * argument looking for a batman-adv soft interface.
 *
 * Returns true if the device is descendant of a batman-adv mesh interface (or
 * if it is a batman-adv interface itself), false otherwise
 */
static bool batadv_is_on_batman_iface(const struct net_device *net_dev)
{
	struct net_device *parent_dev;
	bool ret;

	/* check if this is a batman-adv mesh interface */
	if (batadv_softif_is_valid(net_dev))
		return true;

	/* no more parents..stop recursion */
	if (net_dev->iflink == 0 || net_dev->iflink == net_dev->ifindex)
		return false;

	/* recurse over the parent device */
	parent_dev = __dev_get_by_index(&init_net, net_dev->iflink);
	/* if we got a NULL parent_dev there is something broken.. */
	if (WARN(!parent_dev, "Cannot find parent device"))
		return false;

	ret = batadv_is_on_batman_iface(parent_dev);

	return ret;
}

static int batadv_is_valid_iface(const struct net_device *net_dev)
{
	if (net_dev->flags & IFF_LOOPBACK)
		return 0;

	if (net_dev->type != ARPHRD_ETHER)
		return 0;

	if (net_dev->addr_len != ETH_ALEN)
		return 0;

	/* no batman over batman */
	if (batadv_is_on_batman_iface(net_dev))
		return 0;

	return 1;
}

/**
 * batadv_is_wifi_netdev - check if the given net_device struct is a wifi
 *  interface
 * @net_device: the device to check
 *
 * Returns true if the net device is a 802.11 wireless device, false otherwise.
 */
bool batadv_is_wifi_netdev(struct net_device *net_device)
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

	/* cfg80211 drivers have to set ieee80211_ptr */
	if (net_device->ieee80211_ptr)
		return true;

	return false;
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
		    atomic_inc_not_zero(&hard_iface->refcount))
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
		batadv_hardif_free_ref(primary_if);
}

static void batadv_primary_if_select(struct batadv_priv *bat_priv,
				     struct batadv_hard_iface *new_hard_iface)
{
	struct batadv_hard_iface *curr_hard_iface;

	ASSERT_RTNL();

	if (new_hard_iface && !atomic_inc_not_zero(&new_hard_iface->refcount))
		new_hard_iface = NULL;

	curr_hard_iface = rcu_dereference_protected(bat_priv->primary_if, 1);
	rcu_assign_pointer(bat_priv->primary_if, new_hard_iface);

	if (!new_hard_iface)
		goto out;

	bat_priv->bat_algo_ops->bat_primary_iface_set(new_hard_iface);
	batadv_primary_if_update_addr(bat_priv, curr_hard_iface);

out:
	if (curr_hard_iface)
		batadv_hardif_free_ref(curr_hard_iface);
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
		if ((hard_iface->if_status != BATADV_IF_ACTIVE) &&
		    (hard_iface->if_status != BATADV_IF_TO_BE_ACTIVATED))
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

int batadv_hardif_min_mtu(struct net_device *soft_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(soft_iface);
	const struct batadv_hard_iface *hard_iface;
	int min_mtu = INT_MAX;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if ((hard_iface->if_status != BATADV_IF_ACTIVE) &&
		    (hard_iface->if_status != BATADV_IF_TO_BE_ACTIVATED))
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
	 * maximum local table table size
	 */
	atomic_set(&bat_priv->packet_size_max, min_mtu);

	/* the real soft-interface MTU is computed by removing the payload
	 * overhead from the maximum amount of bytes that was just computed.
	 *
	 * However batman-adv does not support MTUs bigger than ETH_DATA_LEN
	 */
	return min_t(int, min_mtu - batadv_max_header_len(), ETH_DATA_LEN);
}

/* adjusts the MTU if a new interface with a smaller MTU appeared. */
void batadv_update_min_mtu(struct net_device *soft_iface)
{
	soft_iface->mtu = batadv_hardif_min_mtu(soft_iface);

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

	bat_priv->bat_algo_ops->bat_iface_update_mac(hard_iface);
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

out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
}

static void
batadv_hardif_deactivate_interface(struct batadv_hard_iface *hard_iface)
{
	if ((hard_iface->if_status != BATADV_IF_ACTIVE) &&
	    (hard_iface->if_status != BATADV_IF_TO_BE_ACTIVATED))
		return;

	hard_iface->if_status = BATADV_IF_INACTIVE;

	batadv_info(hard_iface->soft_iface, "Interface deactivated: %s\n",
		    hard_iface->net_dev->name);

	batadv_update_min_mtu(hard_iface->soft_iface);
}

/**
 * batadv_master_del_slave - remove hard_iface from the current master interface
 * @slave: the interface enslaved in another master
 * @master: the master from which slave has to be removed
 *
 * Invoke ndo_del_slave on master passing slave as argument. In this way slave
 * is free'd and master can correctly change its internal state.
 * Return 0 on success, a negative value representing the error otherwise
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

int batadv_hardif_enable_interface(struct batadv_hard_iface *hard_iface,
				   const char *iface_name)
{
	struct batadv_priv *bat_priv;
	struct net_device *soft_iface, *master;
	__be16 ethertype = htons(ETH_P_BATMAN);
	int max_header_len = batadv_max_header_len();
	int ret;

	if (hard_iface->if_status != BATADV_IF_NOT_IN_USE)
		goto out;

	if (!atomic_inc_not_zero(&hard_iface->refcount))
		goto out;

	soft_iface = dev_get_by_name(&init_net, iface_name);

	if (!soft_iface) {
		soft_iface = batadv_softif_create(iface_name);

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

	ret = netdev_master_upper_dev_link(hard_iface->net_dev, soft_iface);
	if (ret)
		goto err_dev;

	ret = bat_priv->bat_algo_ops->bat_iface_enable(hard_iface);
	if (ret < 0)
		goto err_upper;

	hard_iface->if_num = bat_priv->num_ifaces;
	bat_priv->num_ifaces++;
	hard_iface->if_status = BATADV_IF_INACTIVE;
	ret = batadv_orig_hash_add_if(hard_iface, bat_priv->num_ifaces);
	if (ret < 0) {
		bat_priv->bat_algo_ops->bat_iface_disable(hard_iface);
		bat_priv->num_ifaces--;
		hard_iface->if_status = BATADV_IF_NOT_IN_USE;
		goto err_upper;
	}

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

	/* begin scheduling originator messages on that interface */
	batadv_schedule_bat_ogm(hard_iface);

out:
	return 0;

err_upper:
	netdev_upper_dev_unlink(hard_iface->net_dev, soft_iface);
err_dev:
	hard_iface->soft_iface = NULL;
	dev_put(soft_iface);
err:
	batadv_hardif_free_ref(hard_iface);
	return ret;
}

void batadv_hardif_disable_interface(struct batadv_hard_iface *hard_iface,
				     enum batadv_hard_if_cleanup autodel)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct batadv_hard_iface *primary_if = NULL;

	if (hard_iface->if_status == BATADV_IF_ACTIVE)
		batadv_hardif_deactivate_interface(hard_iface);

	if (hard_iface->if_status != BATADV_IF_INACTIVE)
		goto out;

	batadv_info(hard_iface->soft_iface, "Removing interface: %s\n",
		    hard_iface->net_dev->name);
	dev_remove_pack(&hard_iface->batman_adv_ptype);

	bat_priv->num_ifaces--;
	batadv_orig_hash_del_if(hard_iface, bat_priv->num_ifaces);

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (hard_iface == primary_if) {
		struct batadv_hard_iface *new_if;

		new_if = batadv_hardif_get_active(hard_iface->soft_iface);
		batadv_primary_if_select(bat_priv, new_if);

		if (new_if)
			batadv_hardif_free_ref(new_if);
	}

	bat_priv->bat_algo_ops->bat_iface_disable(hard_iface);
	hard_iface->if_status = BATADV_IF_NOT_IN_USE;

	/* delete all references to this hard_iface */
	batadv_purge_orig_ref(bat_priv);
	batadv_purge_outstanding_packets(bat_priv, hard_iface);
	dev_put(hard_iface->soft_iface);

	/* nobody uses this interface anymore */
	if (!bat_priv->num_ifaces) {
		batadv_gw_check_client_stop(bat_priv);

		if (autodel == BATADV_IF_CLEANUP_AUTO)
			batadv_softif_destroy_sysfs(hard_iface->soft_iface);
	}

	netdev_upper_dev_unlink(hard_iface->net_dev, hard_iface->soft_iface);
	hard_iface->soft_iface = NULL;
	batadv_hardif_free_ref(hard_iface);

out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
}

/**
 * batadv_hardif_remove_interface_finish - cleans up the remains of a hardif
 * @work: work queue item
 *
 * Free the parts of the hard interface which can not be removed under
 * rtnl lock (to prevent deadlock situations).
 */
static void batadv_hardif_remove_interface_finish(struct work_struct *work)
{
	struct batadv_hard_iface *hard_iface;

	hard_iface = container_of(work, struct batadv_hard_iface,
				  cleanup_work);

	batadv_debugfs_del_hardif(hard_iface);
	batadv_sysfs_del_hardif(&hard_iface->hardif_obj);
	batadv_hardif_free_ref(hard_iface);
}

static struct batadv_hard_iface *
batadv_hardif_add_interface(struct net_device *net_dev)
{
	struct batadv_hard_iface *hard_iface;
	int ret;

	ASSERT_RTNL();

	ret = batadv_is_valid_iface(net_dev);
	if (ret != 1)
		goto out;

	dev_hold(net_dev);

	hard_iface = kzalloc(sizeof(*hard_iface), GFP_ATOMIC);
	if (!hard_iface)
		goto release_dev;

	ret = batadv_sysfs_add_hardif(&hard_iface->hardif_obj, net_dev);
	if (ret)
		goto free_if;

	hard_iface->if_num = -1;
	hard_iface->net_dev = net_dev;
	hard_iface->soft_iface = NULL;
	hard_iface->if_status = BATADV_IF_NOT_IN_USE;

	ret = batadv_debugfs_add_hardif(hard_iface);
	if (ret)
		goto free_sysfs;

	INIT_LIST_HEAD(&hard_iface->list);
	INIT_WORK(&hard_iface->cleanup_work,
		  batadv_hardif_remove_interface_finish);

	hard_iface->num_bcasts = BATADV_NUM_BCASTS_DEFAULT;
	if (batadv_is_wifi_netdev(net_dev))
		hard_iface->num_bcasts = BATADV_NUM_BCASTS_WIRELESS;

	/* extra reference for return */
	atomic_set(&hard_iface->refcount, 2);

	batadv_check_known_mac_addr(hard_iface->net_dev);
	list_add_tail_rcu(&hard_iface->list, &batadv_hardif_list);

	return hard_iface;

free_sysfs:
	batadv_sysfs_del_hardif(&hard_iface->hardif_obj);
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
						BATADV_IF_CLEANUP_AUTO);

	if (hard_iface->if_status != BATADV_IF_NOT_IN_USE)
		return;

	hard_iface->if_status = BATADV_IF_TO_BE_REMOVED;
	queue_work(batadv_event_workqueue, &hard_iface->cleanup_work);
}

void batadv_hardif_remove_interfaces(void)
{
	struct batadv_hard_iface *hard_iface, *hard_iface_tmp;

	rtnl_lock();
	list_for_each_entry_safe(hard_iface, hard_iface_tmp,
				 &batadv_hardif_list, list) {
		list_del_rcu(&hard_iface->list);
		batadv_hardif_remove_interface(hard_iface);
	}
	rtnl_unlock();
}

static int batadv_hard_if_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct net_device *net_dev = netdev_notifier_info_to_dev(ptr);
	struct batadv_hard_iface *hard_iface;
	struct batadv_hard_iface *primary_if = NULL;
	struct batadv_priv *bat_priv;

	if (batadv_softif_is_valid(net_dev) && event == NETDEV_REGISTER) {
		batadv_sysfs_add_meshif(net_dev);
		bat_priv = netdev_priv(net_dev);
		batadv_softif_create_vlan(bat_priv, BATADV_NO_FLAGS);
		return NOTIFY_DONE;
	}

	hard_iface = batadv_hardif_get_by_netdev(net_dev);
	if (!hard_iface && event == NETDEV_REGISTER)
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
		list_del_rcu(&hard_iface->list);

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
		bat_priv->bat_algo_ops->bat_iface_update_mac(hard_iface);

		primary_if = batadv_primary_if_get_selected(bat_priv);
		if (!primary_if)
			goto hardif_put;

		if (hard_iface == primary_if)
			batadv_primary_if_update_addr(bat_priv, NULL);
		break;
	default:
		break;
	}

hardif_put:
	batadv_hardif_free_ref(hard_iface);
out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	return NOTIFY_DONE;
}

struct notifier_block batadv_hard_if_notifier = {
	.notifier_call = batadv_hard_if_event,
};
