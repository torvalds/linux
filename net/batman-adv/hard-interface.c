/*
 * Copyright (C) 2007-2012 B.A.T.M.A.N. contributors:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "hard-interface.h"
#include "soft-interface.h"
#include "send.h"
#include "translation-table.h"
#include "routing.h"
#include "bat_sysfs.h"
#include "originator.h"
#include "hash.h"
#include "bridge_loop_avoidance.h"

#include <linux/if_arp.h>


static int batman_skb_recv(struct sk_buff *skb,
			   struct net_device *dev,
			   struct packet_type *ptype,
			   struct net_device *orig_dev);

void hardif_free_rcu(struct rcu_head *rcu)
{
	struct hard_iface *hard_iface;

	hard_iface = container_of(rcu, struct hard_iface, rcu);
	dev_put(hard_iface->net_dev);
	kfree(hard_iface);
}

struct hard_iface *hardif_get_by_netdev(const struct net_device *net_dev)
{
	struct hard_iface *hard_iface;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &hardif_list, list) {
		if (hard_iface->net_dev == net_dev &&
		    atomic_inc_not_zero(&hard_iface->refcount))
			goto out;
	}

	hard_iface = NULL;

out:
	rcu_read_unlock();
	return hard_iface;
}

static int is_valid_iface(const struct net_device *net_dev)
{
	if (net_dev->flags & IFF_LOOPBACK)
		return 0;

	if (net_dev->type != ARPHRD_ETHER)
		return 0;

	if (net_dev->addr_len != ETH_ALEN)
		return 0;

	/* no batman over batman */
	if (softif_is_valid(net_dev))
		return 0;

	/* Device is being bridged */
	/* if (net_dev->priv_flags & IFF_BRIDGE_PORT)
		return 0; */

	return 1;
}

static struct hard_iface *hardif_get_active(const struct net_device *soft_iface)
{
	struct hard_iface *hard_iface;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &hardif_list, list) {
		if (hard_iface->soft_iface != soft_iface)
			continue;

		if (hard_iface->if_status == IF_ACTIVE &&
		    atomic_inc_not_zero(&hard_iface->refcount))
			goto out;
	}

	hard_iface = NULL;

out:
	rcu_read_unlock();
	return hard_iface;
}

static void primary_if_update_addr(struct bat_priv *bat_priv,
				   struct hard_iface *oldif)
{
	struct vis_packet *vis_packet;
	struct hard_iface *primary_if;

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	vis_packet = (struct vis_packet *)
				bat_priv->my_vis_info->skb_packet->data;
	memcpy(vis_packet->vis_orig, primary_if->net_dev->dev_addr, ETH_ALEN);
	memcpy(vis_packet->sender_orig,
	       primary_if->net_dev->dev_addr, ETH_ALEN);

	bla_update_orig_address(bat_priv, primary_if, oldif);
out:
	if (primary_if)
		hardif_free_ref(primary_if);
}

static void primary_if_select(struct bat_priv *bat_priv,
			      struct hard_iface *new_hard_iface)
{
	struct hard_iface *curr_hard_iface;

	ASSERT_RTNL();

	if (new_hard_iface && !atomic_inc_not_zero(&new_hard_iface->refcount))
		new_hard_iface = NULL;

	curr_hard_iface = rcu_dereference_protected(bat_priv->primary_if, 1);
	rcu_assign_pointer(bat_priv->primary_if, new_hard_iface);

	if (!new_hard_iface)
		goto out;

	bat_priv->bat_algo_ops->bat_ogm_init_primary(new_hard_iface);
	primary_if_update_addr(bat_priv, curr_hard_iface);

out:
	if (curr_hard_iface)
		hardif_free_ref(curr_hard_iface);
}

static bool hardif_is_iface_up(const struct hard_iface *hard_iface)
{
	if (hard_iface->net_dev->flags & IFF_UP)
		return true;

	return false;
}

static void check_known_mac_addr(const struct net_device *net_dev)
{
	const struct hard_iface *hard_iface;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &hardif_list, list) {
		if ((hard_iface->if_status != IF_ACTIVE) &&
		    (hard_iface->if_status != IF_TO_BE_ACTIVATED))
			continue;

		if (hard_iface->net_dev == net_dev)
			continue;

		if (!compare_eth(hard_iface->net_dev->dev_addr,
				 net_dev->dev_addr))
			continue;

		pr_warning("The newly added mac address (%pM) already exists on: %s\n",
			   net_dev->dev_addr, hard_iface->net_dev->name);
		pr_warning("It is strongly recommended to keep mac addresses unique to avoid problems!\n");
	}
	rcu_read_unlock();
}

int hardif_min_mtu(struct net_device *soft_iface)
{
	const struct bat_priv *bat_priv = netdev_priv(soft_iface);
	const struct hard_iface *hard_iface;
	/* allow big frames if all devices are capable to do so
	 * (have MTU > 1500 + BAT_HEADER_LEN) */
	int min_mtu = ETH_DATA_LEN;

	if (atomic_read(&bat_priv->fragmentation))
		goto out;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &hardif_list, list) {
		if ((hard_iface->if_status != IF_ACTIVE) &&
		    (hard_iface->if_status != IF_TO_BE_ACTIVATED))
			continue;

		if (hard_iface->soft_iface != soft_iface)
			continue;

		min_mtu = min_t(int, hard_iface->net_dev->mtu - BAT_HEADER_LEN,
				min_mtu);
	}
	rcu_read_unlock();
out:
	return min_mtu;
}

/* adjusts the MTU if a new interface with a smaller MTU appeared. */
void update_min_mtu(struct net_device *soft_iface)
{
	int min_mtu;

	min_mtu = hardif_min_mtu(soft_iface);
	if (soft_iface->mtu != min_mtu)
		soft_iface->mtu = min_mtu;
}

static void hardif_activate_interface(struct hard_iface *hard_iface)
{
	struct bat_priv *bat_priv;
	struct hard_iface *primary_if = NULL;

	if (hard_iface->if_status != IF_INACTIVE)
		goto out;

	bat_priv = netdev_priv(hard_iface->soft_iface);

	bat_priv->bat_algo_ops->bat_ogm_update_mac(hard_iface);
	hard_iface->if_status = IF_TO_BE_ACTIVATED;

	/**
	 * the first active interface becomes our primary interface or
	 * the next active interface after the old primary interface was removed
	 */
	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if)
		primary_if_select(bat_priv, hard_iface);

	bat_info(hard_iface->soft_iface, "Interface activated: %s\n",
		 hard_iface->net_dev->name);

	update_min_mtu(hard_iface->soft_iface);

out:
	if (primary_if)
		hardif_free_ref(primary_if);
}

static void hardif_deactivate_interface(struct hard_iface *hard_iface)
{
	if ((hard_iface->if_status != IF_ACTIVE) &&
	    (hard_iface->if_status != IF_TO_BE_ACTIVATED))
		return;

	hard_iface->if_status = IF_INACTIVE;

	bat_info(hard_iface->soft_iface, "Interface deactivated: %s\n",
		 hard_iface->net_dev->name);

	update_min_mtu(hard_iface->soft_iface);
}

int hardif_enable_interface(struct hard_iface *hard_iface,
			    const char *iface_name)
{
	struct bat_priv *bat_priv;
	struct net_device *soft_iface;
	int ret;

	if (hard_iface->if_status != IF_NOT_IN_USE)
		goto out;

	if (!atomic_inc_not_zero(&hard_iface->refcount))
		goto out;

	/* hard-interface is part of a bridge */
	if (hard_iface->net_dev->priv_flags & IFF_BRIDGE_PORT)
		pr_err("You are about to enable batman-adv on '%s' which already is part of a bridge. Unless you know exactly what you are doing this is probably wrong and won't work the way you think it would.\n",
		       hard_iface->net_dev->name);

	soft_iface = dev_get_by_name(&init_net, iface_name);

	if (!soft_iface) {
		soft_iface = softif_create(iface_name);

		if (!soft_iface) {
			ret = -ENOMEM;
			goto err;
		}

		/* dev_get_by_name() increases the reference counter for us */
		dev_hold(soft_iface);
	}

	if (!softif_is_valid(soft_iface)) {
		pr_err("Can't create batman mesh interface %s: already exists as regular interface\n",
		       soft_iface->name);
		dev_put(soft_iface);
		ret = -EINVAL;
		goto err;
	}

	hard_iface->soft_iface = soft_iface;
	bat_priv = netdev_priv(hard_iface->soft_iface);

	bat_priv->bat_algo_ops->bat_ogm_init(hard_iface);

	if (!hard_iface->packet_buff) {
		bat_err(hard_iface->soft_iface,
			"Can't add interface packet (%s): out of memory\n",
			hard_iface->net_dev->name);
		ret = -ENOMEM;
		goto err;
	}

	hard_iface->if_num = bat_priv->num_ifaces;
	bat_priv->num_ifaces++;
	hard_iface->if_status = IF_INACTIVE;
	orig_hash_add_if(hard_iface, bat_priv->num_ifaces);

	hard_iface->batman_adv_ptype.type = __constant_htons(ETH_P_BATMAN);
	hard_iface->batman_adv_ptype.func = batman_skb_recv;
	hard_iface->batman_adv_ptype.dev = hard_iface->net_dev;
	dev_add_pack(&hard_iface->batman_adv_ptype);

	atomic_set(&hard_iface->frag_seqno, 1);
	bat_info(hard_iface->soft_iface, "Adding interface: %s\n",
		 hard_iface->net_dev->name);

	if (atomic_read(&bat_priv->fragmentation) && hard_iface->net_dev->mtu <
		ETH_DATA_LEN + BAT_HEADER_LEN)
		bat_info(hard_iface->soft_iface,
			 "The MTU of interface %s is too small (%i) to handle the transport of batman-adv packets. Packets going over this interface will be fragmented on layer2 which could impact the performance. Setting the MTU to %zi would solve the problem.\n",
			 hard_iface->net_dev->name, hard_iface->net_dev->mtu,
			 ETH_DATA_LEN + BAT_HEADER_LEN);

	if (!atomic_read(&bat_priv->fragmentation) && hard_iface->net_dev->mtu <
		ETH_DATA_LEN + BAT_HEADER_LEN)
		bat_info(hard_iface->soft_iface,
			 "The MTU of interface %s is too small (%i) to handle the transport of batman-adv packets. If you experience problems getting traffic through try increasing the MTU to %zi.\n",
			 hard_iface->net_dev->name, hard_iface->net_dev->mtu,
			 ETH_DATA_LEN + BAT_HEADER_LEN);

	if (hardif_is_iface_up(hard_iface))
		hardif_activate_interface(hard_iface);
	else
		bat_err(hard_iface->soft_iface,
			"Not using interface %s (retrying later): interface not active\n",
			hard_iface->net_dev->name);

	/* begin scheduling originator messages on that interface */
	schedule_bat_ogm(hard_iface);

out:
	return 0;

err:
	hardif_free_ref(hard_iface);
	return ret;
}

void hardif_disable_interface(struct hard_iface *hard_iface)
{
	struct bat_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct hard_iface *primary_if = NULL;

	if (hard_iface->if_status == IF_ACTIVE)
		hardif_deactivate_interface(hard_iface);

	if (hard_iface->if_status != IF_INACTIVE)
		goto out;

	bat_info(hard_iface->soft_iface, "Removing interface: %s\n",
		 hard_iface->net_dev->name);
	dev_remove_pack(&hard_iface->batman_adv_ptype);

	bat_priv->num_ifaces--;
	orig_hash_del_if(hard_iface, bat_priv->num_ifaces);

	primary_if = primary_if_get_selected(bat_priv);
	if (hard_iface == primary_if) {
		struct hard_iface *new_if;

		new_if = hardif_get_active(hard_iface->soft_iface);
		primary_if_select(bat_priv, new_if);

		if (new_if)
			hardif_free_ref(new_if);
	}

	kfree(hard_iface->packet_buff);
	hard_iface->packet_buff = NULL;
	hard_iface->if_status = IF_NOT_IN_USE;

	/* delete all references to this hard_iface */
	purge_orig_ref(bat_priv);
	purge_outstanding_packets(bat_priv, hard_iface);
	dev_put(hard_iface->soft_iface);

	/* nobody uses this interface anymore */
	if (!bat_priv->num_ifaces)
		softif_destroy(hard_iface->soft_iface);

	hard_iface->soft_iface = NULL;
	hardif_free_ref(hard_iface);

out:
	if (primary_if)
		hardif_free_ref(primary_if);
}

static struct hard_iface *hardif_add_interface(struct net_device *net_dev)
{
	struct hard_iface *hard_iface;
	int ret;

	ASSERT_RTNL();

	ret = is_valid_iface(net_dev);
	if (ret != 1)
		goto out;

	dev_hold(net_dev);

	hard_iface = kmalloc(sizeof(*hard_iface), GFP_ATOMIC);
	if (!hard_iface)
		goto release_dev;

	ret = sysfs_add_hardif(&hard_iface->hardif_obj, net_dev);
	if (ret)
		goto free_if;

	hard_iface->if_num = -1;
	hard_iface->net_dev = net_dev;
	hard_iface->soft_iface = NULL;
	hard_iface->if_status = IF_NOT_IN_USE;
	INIT_LIST_HEAD(&hard_iface->list);
	/* extra reference for return */
	atomic_set(&hard_iface->refcount, 2);

	check_known_mac_addr(hard_iface->net_dev);
	list_add_tail_rcu(&hard_iface->list, &hardif_list);

	/**
	 * This can't be called via a bat_priv callback because
	 * we have no bat_priv yet.
	 */
	atomic_set(&hard_iface->seqno, 1);
	hard_iface->packet_buff = NULL;

	return hard_iface;

free_if:
	kfree(hard_iface);
release_dev:
	dev_put(net_dev);
out:
	return NULL;
}

static void hardif_remove_interface(struct hard_iface *hard_iface)
{
	ASSERT_RTNL();

	/* first deactivate interface */
	if (hard_iface->if_status != IF_NOT_IN_USE)
		hardif_disable_interface(hard_iface);

	if (hard_iface->if_status != IF_NOT_IN_USE)
		return;

	hard_iface->if_status = IF_TO_BE_REMOVED;
	sysfs_del_hardif(&hard_iface->hardif_obj);
	hardif_free_ref(hard_iface);
}

void hardif_remove_interfaces(void)
{
	struct hard_iface *hard_iface, *hard_iface_tmp;

	rtnl_lock();
	list_for_each_entry_safe(hard_iface, hard_iface_tmp,
				 &hardif_list, list) {
		list_del_rcu(&hard_iface->list);
		hardif_remove_interface(hard_iface);
	}
	rtnl_unlock();
}

static int hard_if_event(struct notifier_block *this,
			 unsigned long event, void *ptr)
{
	struct net_device *net_dev = ptr;
	struct hard_iface *hard_iface = hardif_get_by_netdev(net_dev);
	struct hard_iface *primary_if = NULL;
	struct bat_priv *bat_priv;

	if (!hard_iface && event == NETDEV_REGISTER)
		hard_iface = hardif_add_interface(net_dev);

	if (!hard_iface)
		goto out;

	switch (event) {
	case NETDEV_UP:
		hardif_activate_interface(hard_iface);
		break;
	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:
		hardif_deactivate_interface(hard_iface);
		break;
	case NETDEV_UNREGISTER:
		list_del_rcu(&hard_iface->list);

		hardif_remove_interface(hard_iface);
		break;
	case NETDEV_CHANGEMTU:
		if (hard_iface->soft_iface)
			update_min_mtu(hard_iface->soft_iface);
		break;
	case NETDEV_CHANGEADDR:
		if (hard_iface->if_status == IF_NOT_IN_USE)
			goto hardif_put;

		check_known_mac_addr(hard_iface->net_dev);

		bat_priv = netdev_priv(hard_iface->soft_iface);
		bat_priv->bat_algo_ops->bat_ogm_update_mac(hard_iface);

		primary_if = primary_if_get_selected(bat_priv);
		if (!primary_if)
			goto hardif_put;

		if (hard_iface == primary_if)
			primary_if_update_addr(bat_priv, NULL);
		break;
	default:
		break;
	}

hardif_put:
	hardif_free_ref(hard_iface);
out:
	if (primary_if)
		hardif_free_ref(primary_if);
	return NOTIFY_DONE;
}

/* incoming packets with the batman ethertype received on any active hard
 * interface */
static int batman_skb_recv(struct sk_buff *skb, struct net_device *dev,
			   struct packet_type *ptype,
			   struct net_device *orig_dev)
{
	struct bat_priv *bat_priv;
	struct batman_ogm_packet *batman_ogm_packet;
	struct hard_iface *hard_iface;
	int ret;

	hard_iface = container_of(ptype, struct hard_iface, batman_adv_ptype);
	skb = skb_share_check(skb, GFP_ATOMIC);

	/* skb was released by skb_share_check() */
	if (!skb)
		goto err_out;

	/* packet should hold at least type and version */
	if (unlikely(!pskb_may_pull(skb, 2)))
		goto err_free;

	/* expect a valid ethernet header here. */
	if (unlikely(skb->mac_len != sizeof(struct ethhdr) ||
		     !skb_mac_header(skb)))
		goto err_free;

	if (!hard_iface->soft_iface)
		goto err_free;

	bat_priv = netdev_priv(hard_iface->soft_iface);

	if (atomic_read(&bat_priv->mesh_state) != MESH_ACTIVE)
		goto err_free;

	/* discard frames on not active interfaces */
	if (hard_iface->if_status != IF_ACTIVE)
		goto err_free;

	batman_ogm_packet = (struct batman_ogm_packet *)skb->data;

	if (batman_ogm_packet->header.version != COMPAT_VERSION) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: incompatible batman version (%i)\n",
			batman_ogm_packet->header.version);
		goto err_free;
	}

	/* all receive handlers return whether they received or reused
	 * the supplied skb. if not, we have to free the skb. */

	switch (batman_ogm_packet->header.packet_type) {
		/* batman originator packet */
	case BAT_OGM:
		ret = recv_bat_ogm_packet(skb, hard_iface);
		break;

		/* batman icmp packet */
	case BAT_ICMP:
		ret = recv_icmp_packet(skb, hard_iface);
		break;

		/* unicast packet */
	case BAT_UNICAST:
		ret = recv_unicast_packet(skb, hard_iface);
		break;

		/* fragmented unicast packet */
	case BAT_UNICAST_FRAG:
		ret = recv_ucast_frag_packet(skb, hard_iface);
		break;

		/* broadcast packet */
	case BAT_BCAST:
		ret = recv_bcast_packet(skb, hard_iface);
		break;

		/* vis packet */
	case BAT_VIS:
		ret = recv_vis_packet(skb, hard_iface);
		break;
		/* Translation table query (request or response) */
	case BAT_TT_QUERY:
		ret = recv_tt_query(skb, hard_iface);
		break;
		/* Roaming advertisement */
	case BAT_ROAM_ADV:
		ret = recv_roam_adv(skb, hard_iface);
		break;
	default:
		ret = NET_RX_DROP;
	}

	if (ret == NET_RX_DROP)
		kfree_skb(skb);

	/* return NET_RX_SUCCESS in any case as we
	 * most probably dropped the packet for
	 * routing-logical reasons. */

	return NET_RX_SUCCESS;

err_free:
	kfree_skb(skb);
err_out:
	return NET_RX_DROP;
}

/* This function returns true if the interface represented by ifindex is a
 * 802.11 wireless device */
bool is_wifi_iface(int ifindex)
{
	struct net_device *net_device = NULL;
	bool ret = false;

	if (ifindex == NULL_IFINDEX)
		goto out;

	net_device = dev_get_by_index(&init_net, ifindex);
	if (!net_device)
		goto out;

#ifdef CONFIG_WIRELESS_EXT
	/* pre-cfg80211 drivers have to implement WEXT, so it is possible to
	 * check for wireless_handlers != NULL */
	if (net_device->wireless_handlers)
		ret = true;
	else
#endif
		/* cfg80211 drivers have to set ieee80211_ptr */
		if (net_device->ieee80211_ptr)
			ret = true;
out:
	if (net_device)
		dev_put(net_device);
	return ret;
}

struct notifier_block hard_if_notifier = {
	.notifier_call = hard_if_event,
};
