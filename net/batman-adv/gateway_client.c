// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2009-2018  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
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

#include "gateway_client.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/byteorder/generic.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/gfp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/udp.h>
#include <net/sock.h>
#include <uapi/linux/batadv_packet.h>
#include <uapi/linux/batman_adv.h>

#include "gateway_common.h"
#include "hard-interface.h"
#include "log.h"
#include "netlink.h"
#include "originator.h"
#include "routing.h"
#include "soft-interface.h"
#include "sysfs.h"
#include "translation-table.h"

/* These are the offsets of the "hw type" and "hw address length" in the dhcp
 * packet starting at the beginning of the dhcp header
 */
#define BATADV_DHCP_HTYPE_OFFSET	1
#define BATADV_DHCP_HLEN_OFFSET		2
/* Value of htype representing Ethernet */
#define BATADV_DHCP_HTYPE_ETHERNET	0x01
/* This is the offset of the "chaddr" field in the dhcp packet starting at the
 * beginning of the dhcp header
 */
#define BATADV_DHCP_CHADDR_OFFSET	28

/**
 * batadv_gw_node_release() - release gw_node from lists and queue for free
 *  after rcu grace period
 * @ref: kref pointer of the gw_node
 */
static void batadv_gw_node_release(struct kref *ref)
{
	struct batadv_gw_node *gw_node;

	gw_node = container_of(ref, struct batadv_gw_node, refcount);

	batadv_orig_node_put(gw_node->orig_node);
	kfree_rcu(gw_node, rcu);
}

/**
 * batadv_gw_node_put() - decrement the gw_node refcounter and possibly release
 *  it
 * @gw_node: gateway node to free
 */
void batadv_gw_node_put(struct batadv_gw_node *gw_node)
{
	kref_put(&gw_node->refcount, batadv_gw_node_release);
}

/**
 * batadv_gw_get_selected_gw_node() - Get currently selected gateway
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: selected gateway (with increased refcnt), NULL on errors
 */
struct batadv_gw_node *
batadv_gw_get_selected_gw_node(struct batadv_priv *bat_priv)
{
	struct batadv_gw_node *gw_node;

	rcu_read_lock();
	gw_node = rcu_dereference(bat_priv->gw.curr_gw);
	if (!gw_node)
		goto out;

	if (!kref_get_unless_zero(&gw_node->refcount))
		gw_node = NULL;

out:
	rcu_read_unlock();
	return gw_node;
}

/**
 * batadv_gw_get_selected_orig() - Get originator of currently selected gateway
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: orig_node of selected gateway (with increased refcnt), NULL on errors
 */
struct batadv_orig_node *
batadv_gw_get_selected_orig(struct batadv_priv *bat_priv)
{
	struct batadv_gw_node *gw_node;
	struct batadv_orig_node *orig_node = NULL;

	gw_node = batadv_gw_get_selected_gw_node(bat_priv);
	if (!gw_node)
		goto out;

	rcu_read_lock();
	orig_node = gw_node->orig_node;
	if (!orig_node)
		goto unlock;

	if (!kref_get_unless_zero(&orig_node->refcount))
		orig_node = NULL;

unlock:
	rcu_read_unlock();
out:
	if (gw_node)
		batadv_gw_node_put(gw_node);
	return orig_node;
}

static void batadv_gw_select(struct batadv_priv *bat_priv,
			     struct batadv_gw_node *new_gw_node)
{
	struct batadv_gw_node *curr_gw_node;

	spin_lock_bh(&bat_priv->gw.list_lock);

	if (new_gw_node)
		kref_get(&new_gw_node->refcount);

	curr_gw_node = rcu_dereference_protected(bat_priv->gw.curr_gw, 1);
	rcu_assign_pointer(bat_priv->gw.curr_gw, new_gw_node);

	if (curr_gw_node)
		batadv_gw_node_put(curr_gw_node);

	spin_unlock_bh(&bat_priv->gw.list_lock);
}

/**
 * batadv_gw_reselect() - force a gateway reselection
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Set a flag to remind the GW component to perform a new gateway reselection.
 * However this function does not ensure that the current gateway is going to be
 * deselected. The reselection mechanism may elect the same gateway once again.
 *
 * This means that invoking batadv_gw_reselect() does not guarantee a gateway
 * change and therefore a uevent is not necessarily expected.
 */
void batadv_gw_reselect(struct batadv_priv *bat_priv)
{
	atomic_set(&bat_priv->gw.reselect, 1);
}

/**
 * batadv_gw_check_client_stop() - check if client mode has been switched off
 * @bat_priv: the bat priv with all the soft interface information
 *
 * This function assumes the caller has checked that the gw state *is actually
 * changing*. This function is not supposed to be called when there is no state
 * change.
 */
void batadv_gw_check_client_stop(struct batadv_priv *bat_priv)
{
	struct batadv_gw_node *curr_gw;

	if (atomic_read(&bat_priv->gw.mode) != BATADV_GW_MODE_CLIENT)
		return;

	curr_gw = batadv_gw_get_selected_gw_node(bat_priv);
	if (!curr_gw)
		return;

	/* deselect the current gateway so that next time that client mode is
	 * enabled a proper GW_ADD event can be sent
	 */
	batadv_gw_select(bat_priv, NULL);

	/* if batman-adv is switching the gw client mode off and a gateway was
	 * already selected, send a DEL uevent
	 */
	batadv_throw_uevent(bat_priv, BATADV_UEV_GW, BATADV_UEV_DEL, NULL);

	batadv_gw_node_put(curr_gw);
}

/**
 * batadv_gw_election() - Elect the best gateway
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_gw_election(struct batadv_priv *bat_priv)
{
	struct batadv_gw_node *curr_gw = NULL;
	struct batadv_gw_node *next_gw = NULL;
	struct batadv_neigh_node *router = NULL;
	struct batadv_neigh_ifinfo *router_ifinfo = NULL;
	char gw_addr[18] = { '\0' };

	if (atomic_read(&bat_priv->gw.mode) != BATADV_GW_MODE_CLIENT)
		goto out;

	if (!bat_priv->algo_ops->gw.get_best_gw_node)
		goto out;

	curr_gw = batadv_gw_get_selected_gw_node(bat_priv);

	if (!batadv_atomic_dec_not_zero(&bat_priv->gw.reselect) && curr_gw)
		goto out;

	/* if gw.reselect is set to 1 it means that a previous call to
	 * gw.is_eligible() said that we have a new best GW, therefore it can
	 * now be picked from the list and selected
	 */
	next_gw = bat_priv->algo_ops->gw.get_best_gw_node(bat_priv);

	if (curr_gw == next_gw)
		goto out;

	if (next_gw) {
		sprintf(gw_addr, "%pM", next_gw->orig_node->orig);

		router = batadv_orig_router_get(next_gw->orig_node,
						BATADV_IF_DEFAULT);
		if (!router) {
			batadv_gw_reselect(bat_priv);
			goto out;
		}

		router_ifinfo = batadv_neigh_ifinfo_get(router,
							BATADV_IF_DEFAULT);
		if (!router_ifinfo) {
			batadv_gw_reselect(bat_priv);
			goto out;
		}
	}

	if (curr_gw && !next_gw) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Removing selected gateway - no gateway in range\n");
		batadv_throw_uevent(bat_priv, BATADV_UEV_GW, BATADV_UEV_DEL,
				    NULL);
	} else if (!curr_gw && next_gw) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Adding route to gateway %pM (bandwidth: %u.%u/%u.%u MBit, tq: %i)\n",
			   next_gw->orig_node->orig,
			   next_gw->bandwidth_down / 10,
			   next_gw->bandwidth_down % 10,
			   next_gw->bandwidth_up / 10,
			   next_gw->bandwidth_up % 10,
			   router_ifinfo->bat_iv.tq_avg);
		batadv_throw_uevent(bat_priv, BATADV_UEV_GW, BATADV_UEV_ADD,
				    gw_addr);
	} else {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Changing route to gateway %pM (bandwidth: %u.%u/%u.%u MBit, tq: %i)\n",
			   next_gw->orig_node->orig,
			   next_gw->bandwidth_down / 10,
			   next_gw->bandwidth_down % 10,
			   next_gw->bandwidth_up / 10,
			   next_gw->bandwidth_up % 10,
			   router_ifinfo->bat_iv.tq_avg);
		batadv_throw_uevent(bat_priv, BATADV_UEV_GW, BATADV_UEV_CHANGE,
				    gw_addr);
	}

	batadv_gw_select(bat_priv, next_gw);

out:
	if (curr_gw)
		batadv_gw_node_put(curr_gw);
	if (next_gw)
		batadv_gw_node_put(next_gw);
	if (router)
		batadv_neigh_node_put(router);
	if (router_ifinfo)
		batadv_neigh_ifinfo_put(router_ifinfo);
}

/**
 * batadv_gw_check_election() - Elect orig node as best gateway when eligible
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig node which is to be checked
 */
void batadv_gw_check_election(struct batadv_priv *bat_priv,
			      struct batadv_orig_node *orig_node)
{
	struct batadv_orig_node *curr_gw_orig;

	/* abort immediately if the routing algorithm does not support gateway
	 * election
	 */
	if (!bat_priv->algo_ops->gw.is_eligible)
		return;

	curr_gw_orig = batadv_gw_get_selected_orig(bat_priv);
	if (!curr_gw_orig)
		goto reselect;

	/* this node already is the gateway */
	if (curr_gw_orig == orig_node)
		goto out;

	if (!bat_priv->algo_ops->gw.is_eligible(bat_priv, curr_gw_orig,
						orig_node))
		goto out;

reselect:
	batadv_gw_reselect(bat_priv);
out:
	if (curr_gw_orig)
		batadv_orig_node_put(curr_gw_orig);
}

/**
 * batadv_gw_node_add() - add gateway node to list of available gateways
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: originator announcing gateway capabilities
 * @gateway: announced bandwidth information
 *
 * Has to be called with the appropriate locks being acquired
 * (gw.list_lock).
 */
static void batadv_gw_node_add(struct batadv_priv *bat_priv,
			       struct batadv_orig_node *orig_node,
			       struct batadv_tvlv_gateway_data *gateway)
{
	struct batadv_gw_node *gw_node;

	lockdep_assert_held(&bat_priv->gw.list_lock);

	if (gateway->bandwidth_down == 0)
		return;

	gw_node = kzalloc(sizeof(*gw_node), GFP_ATOMIC);
	if (!gw_node)
		return;

	kref_init(&gw_node->refcount);
	INIT_HLIST_NODE(&gw_node->list);
	kref_get(&orig_node->refcount);
	gw_node->orig_node = orig_node;
	gw_node->bandwidth_down = ntohl(gateway->bandwidth_down);
	gw_node->bandwidth_up = ntohl(gateway->bandwidth_up);

	kref_get(&gw_node->refcount);
	hlist_add_head_rcu(&gw_node->list, &bat_priv->gw.gateway_list);

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Found new gateway %pM -> gw bandwidth: %u.%u/%u.%u MBit\n",
		   orig_node->orig,
		   ntohl(gateway->bandwidth_down) / 10,
		   ntohl(gateway->bandwidth_down) % 10,
		   ntohl(gateway->bandwidth_up) / 10,
		   ntohl(gateway->bandwidth_up) % 10);

	/* don't return reference to new gw_node */
	batadv_gw_node_put(gw_node);
}

/**
 * batadv_gw_node_get() - retrieve gateway node from list of available gateways
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: originator announcing gateway capabilities
 *
 * Return: gateway node if found or NULL otherwise.
 */
struct batadv_gw_node *batadv_gw_node_get(struct batadv_priv *bat_priv,
					  struct batadv_orig_node *orig_node)
{
	struct batadv_gw_node *gw_node_tmp, *gw_node = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(gw_node_tmp, &bat_priv->gw.gateway_list,
				 list) {
		if (gw_node_tmp->orig_node != orig_node)
			continue;

		if (!kref_get_unless_zero(&gw_node_tmp->refcount))
			continue;

		gw_node = gw_node_tmp;
		break;
	}
	rcu_read_unlock();

	return gw_node;
}

/**
 * batadv_gw_node_update() - update list of available gateways with changed
 *  bandwidth information
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: originator announcing gateway capabilities
 * @gateway: announced bandwidth information
 */
void batadv_gw_node_update(struct batadv_priv *bat_priv,
			   struct batadv_orig_node *orig_node,
			   struct batadv_tvlv_gateway_data *gateway)
{
	struct batadv_gw_node *gw_node, *curr_gw = NULL;

	spin_lock_bh(&bat_priv->gw.list_lock);
	gw_node = batadv_gw_node_get(bat_priv, orig_node);
	if (!gw_node) {
		batadv_gw_node_add(bat_priv, orig_node, gateway);
		spin_unlock_bh(&bat_priv->gw.list_lock);
		goto out;
	}
	spin_unlock_bh(&bat_priv->gw.list_lock);

	if (gw_node->bandwidth_down == ntohl(gateway->bandwidth_down) &&
	    gw_node->bandwidth_up == ntohl(gateway->bandwidth_up))
		goto out;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Gateway bandwidth of originator %pM changed from %u.%u/%u.%u MBit to %u.%u/%u.%u MBit\n",
		   orig_node->orig,
		   gw_node->bandwidth_down / 10,
		   gw_node->bandwidth_down % 10,
		   gw_node->bandwidth_up / 10,
		   gw_node->bandwidth_up % 10,
		   ntohl(gateway->bandwidth_down) / 10,
		   ntohl(gateway->bandwidth_down) % 10,
		   ntohl(gateway->bandwidth_up) / 10,
		   ntohl(gateway->bandwidth_up) % 10);

	gw_node->bandwidth_down = ntohl(gateway->bandwidth_down);
	gw_node->bandwidth_up = ntohl(gateway->bandwidth_up);

	if (ntohl(gateway->bandwidth_down) == 0) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Gateway %pM removed from gateway list\n",
			   orig_node->orig);

		/* Note: We don't need a NULL check here, since curr_gw never
		 * gets dereferenced.
		 */
		spin_lock_bh(&bat_priv->gw.list_lock);
		if (!hlist_unhashed(&gw_node->list)) {
			hlist_del_init_rcu(&gw_node->list);
			batadv_gw_node_put(gw_node);
		}
		spin_unlock_bh(&bat_priv->gw.list_lock);

		curr_gw = batadv_gw_get_selected_gw_node(bat_priv);
		if (gw_node == curr_gw)
			batadv_gw_reselect(bat_priv);

		if (curr_gw)
			batadv_gw_node_put(curr_gw);
	}

out:
	if (gw_node)
		batadv_gw_node_put(gw_node);
}

/**
 * batadv_gw_node_delete() - Remove orig_node from gateway list
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig node which is currently in process of being removed
 */
void batadv_gw_node_delete(struct batadv_priv *bat_priv,
			   struct batadv_orig_node *orig_node)
{
	struct batadv_tvlv_gateway_data gateway;

	gateway.bandwidth_down = 0;
	gateway.bandwidth_up = 0;

	batadv_gw_node_update(bat_priv, orig_node, &gateway);
}

/**
 * batadv_gw_node_free() - Free gateway information from soft interface
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_gw_node_free(struct batadv_priv *bat_priv)
{
	struct batadv_gw_node *gw_node;
	struct hlist_node *node_tmp;

	spin_lock_bh(&bat_priv->gw.list_lock);
	hlist_for_each_entry_safe(gw_node, node_tmp,
				  &bat_priv->gw.gateway_list, list) {
		hlist_del_init_rcu(&gw_node->list);
		batadv_gw_node_put(gw_node);
	}
	spin_unlock_bh(&bat_priv->gw.list_lock);
}

#ifdef CONFIG_BATMAN_ADV_DEBUGFS

/**
 * batadv_gw_client_seq_print_text() - Print the gateway table in a seq file
 * @seq: seq file to print on
 * @offset: not used
 *
 * Return: always 0
 */
int batadv_gw_client_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	struct batadv_hard_iface *primary_if;

	primary_if = batadv_seq_print_text_primary_if_get(seq);
	if (!primary_if)
		return 0;

	seq_printf(seq, "[B.A.T.M.A.N. adv %s, MainIF/MAC: %s/%pM (%s %s)]\n",
		   BATADV_SOURCE_VERSION, primary_if->net_dev->name,
		   primary_if->net_dev->dev_addr, net_dev->name,
		   bat_priv->algo_ops->name);

	batadv_hardif_put(primary_if);

	if (!bat_priv->algo_ops->gw.print) {
		seq_puts(seq,
			 "No printing function for this routing protocol\n");
		return 0;
	}

	bat_priv->algo_ops->gw.print(bat_priv, seq);

	return 0;
}
#endif

/**
 * batadv_gw_dump() - Dump gateways into a message
 * @msg: Netlink message to dump into
 * @cb: Control block containing additional options
 *
 * Return: Error code, or length of message
 */
int batadv_gw_dump(struct sk_buff *msg, struct netlink_callback *cb)
{
	struct batadv_hard_iface *primary_if = NULL;
	struct net *net = sock_net(cb->skb->sk);
	struct net_device *soft_iface;
	struct batadv_priv *bat_priv;
	int ifindex;
	int ret;

	ifindex = batadv_netlink_get_ifindex(cb->nlh,
					     BATADV_ATTR_MESH_IFINDEX);
	if (!ifindex)
		return -EINVAL;

	soft_iface = dev_get_by_index(net, ifindex);
	if (!soft_iface || !batadv_softif_is_valid(soft_iface)) {
		ret = -ENODEV;
		goto out;
	}

	bat_priv = netdev_priv(soft_iface);

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if || primary_if->if_status != BATADV_IF_ACTIVE) {
		ret = -ENOENT;
		goto out;
	}

	if (!bat_priv->algo_ops->gw.dump) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	bat_priv->algo_ops->gw.dump(msg, cb, bat_priv);

	ret = msg->len;

out:
	if (primary_if)
		batadv_hardif_put(primary_if);
	if (soft_iface)
		dev_put(soft_iface);

	return ret;
}

/**
 * batadv_gw_dhcp_recipient_get() - check if a packet is a DHCP message
 * @skb: the packet to check
 * @header_len: a pointer to the batman-adv header size
 * @chaddr: buffer where the client address will be stored. Valid
 *  only if the function returns BATADV_DHCP_TO_CLIENT
 *
 * This function may re-allocate the data buffer of the skb passed as argument.
 *
 * Return:
 * - BATADV_DHCP_NO if the packet is not a dhcp message or if there was an error
 *   while parsing it
 * - BATADV_DHCP_TO_SERVER if this is a message going to the DHCP server
 * - BATADV_DHCP_TO_CLIENT if this is a message going to a DHCP client
 */
enum batadv_dhcp_recipient
batadv_gw_dhcp_recipient_get(struct sk_buff *skb, unsigned int *header_len,
			     u8 *chaddr)
{
	enum batadv_dhcp_recipient ret = BATADV_DHCP_NO;
	struct ethhdr *ethhdr;
	struct iphdr *iphdr;
	struct ipv6hdr *ipv6hdr;
	struct udphdr *udphdr;
	struct vlan_ethhdr *vhdr;
	int chaddr_offset;
	__be16 proto;
	u8 *p;

	/* check for ethernet header */
	if (!pskb_may_pull(skb, *header_len + ETH_HLEN))
		return BATADV_DHCP_NO;

	ethhdr = eth_hdr(skb);
	proto = ethhdr->h_proto;
	*header_len += ETH_HLEN;

	/* check for initial vlan header */
	if (proto == htons(ETH_P_8021Q)) {
		if (!pskb_may_pull(skb, *header_len + VLAN_HLEN))
			return BATADV_DHCP_NO;

		vhdr = vlan_eth_hdr(skb);
		proto = vhdr->h_vlan_encapsulated_proto;
		*header_len += VLAN_HLEN;
	}

	/* check for ip header */
	switch (proto) {
	case htons(ETH_P_IP):
		if (!pskb_may_pull(skb, *header_len + sizeof(*iphdr)))
			return BATADV_DHCP_NO;

		iphdr = (struct iphdr *)(skb->data + *header_len);
		*header_len += iphdr->ihl * 4;

		/* check for udp header */
		if (iphdr->protocol != IPPROTO_UDP)
			return BATADV_DHCP_NO;

		break;
	case htons(ETH_P_IPV6):
		if (!pskb_may_pull(skb, *header_len + sizeof(*ipv6hdr)))
			return BATADV_DHCP_NO;

		ipv6hdr = (struct ipv6hdr *)(skb->data + *header_len);
		*header_len += sizeof(*ipv6hdr);

		/* check for udp header */
		if (ipv6hdr->nexthdr != IPPROTO_UDP)
			return BATADV_DHCP_NO;

		break;
	default:
		return BATADV_DHCP_NO;
	}

	if (!pskb_may_pull(skb, *header_len + sizeof(*udphdr)))
		return BATADV_DHCP_NO;

	udphdr = (struct udphdr *)(skb->data + *header_len);
	*header_len += sizeof(*udphdr);

	/* check for bootp port */
	switch (proto) {
	case htons(ETH_P_IP):
		if (udphdr->dest == htons(67))
			ret = BATADV_DHCP_TO_SERVER;
		else if (udphdr->source == htons(67))
			ret = BATADV_DHCP_TO_CLIENT;
		break;
	case htons(ETH_P_IPV6):
		if (udphdr->dest == htons(547))
			ret = BATADV_DHCP_TO_SERVER;
		else if (udphdr->source == htons(547))
			ret = BATADV_DHCP_TO_CLIENT;
		break;
	}

	chaddr_offset = *header_len + BATADV_DHCP_CHADDR_OFFSET;
	/* store the client address if the message is going to a client */
	if (ret == BATADV_DHCP_TO_CLIENT) {
		if (!pskb_may_pull(skb, chaddr_offset + ETH_ALEN))
			return BATADV_DHCP_NO;

		/* check if the DHCP packet carries an Ethernet DHCP */
		p = skb->data + *header_len + BATADV_DHCP_HTYPE_OFFSET;
		if (*p != BATADV_DHCP_HTYPE_ETHERNET)
			return BATADV_DHCP_NO;

		/* check if the DHCP packet carries a valid Ethernet address */
		p = skb->data + *header_len + BATADV_DHCP_HLEN_OFFSET;
		if (*p != ETH_ALEN)
			return BATADV_DHCP_NO;

		ether_addr_copy(chaddr, skb->data + chaddr_offset);
	}

	return ret;
}

/**
 * batadv_gw_out_of_range() - check if the dhcp request destination is the best
 *  gateway
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the outgoing packet
 *
 * Check if the skb is a DHCP request and if it is sent to the current best GW
 * server. Due to topology changes it may be the case that the GW server
 * previously selected is not the best one anymore.
 *
 * This call might reallocate skb data.
 * Must be invoked only when the DHCP packet is going TO a DHCP SERVER.
 *
 * Return: true if the packet destination is unicast and it is not the best gw,
 * false otherwise.
 */
bool batadv_gw_out_of_range(struct batadv_priv *bat_priv,
			    struct sk_buff *skb)
{
	struct batadv_neigh_node *neigh_curr = NULL;
	struct batadv_neigh_node *neigh_old = NULL;
	struct batadv_orig_node *orig_dst_node = NULL;
	struct batadv_gw_node *gw_node = NULL;
	struct batadv_gw_node *curr_gw = NULL;
	struct batadv_neigh_ifinfo *curr_ifinfo, *old_ifinfo;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	bool out_of_range = false;
	u8 curr_tq_avg;
	unsigned short vid;

	vid = batadv_get_vid(skb, 0);

	if (is_multicast_ether_addr(ethhdr->h_dest))
		goto out;

	orig_dst_node = batadv_transtable_search(bat_priv, ethhdr->h_source,
						 ethhdr->h_dest, vid);
	if (!orig_dst_node)
		goto out;

	gw_node = batadv_gw_node_get(bat_priv, orig_dst_node);
	if (!gw_node)
		goto out;

	switch (atomic_read(&bat_priv->gw.mode)) {
	case BATADV_GW_MODE_SERVER:
		/* If we are a GW then we are our best GW. We can artificially
		 * set the tq towards ourself as the maximum value
		 */
		curr_tq_avg = BATADV_TQ_MAX_VALUE;
		break;
	case BATADV_GW_MODE_CLIENT:
		curr_gw = batadv_gw_get_selected_gw_node(bat_priv);
		if (!curr_gw)
			goto out;

		/* packet is going to our gateway */
		if (curr_gw->orig_node == orig_dst_node)
			goto out;

		/* If the dhcp packet has been sent to a different gw,
		 * we have to evaluate whether the old gw is still
		 * reliable enough
		 */
		neigh_curr = batadv_find_router(bat_priv, curr_gw->orig_node,
						NULL);
		if (!neigh_curr)
			goto out;

		curr_ifinfo = batadv_neigh_ifinfo_get(neigh_curr,
						      BATADV_IF_DEFAULT);
		if (!curr_ifinfo)
			goto out;

		curr_tq_avg = curr_ifinfo->bat_iv.tq_avg;
		batadv_neigh_ifinfo_put(curr_ifinfo);

		break;
	case BATADV_GW_MODE_OFF:
	default:
		goto out;
	}

	neigh_old = batadv_find_router(bat_priv, orig_dst_node, NULL);
	if (!neigh_old)
		goto out;

	old_ifinfo = batadv_neigh_ifinfo_get(neigh_old, BATADV_IF_DEFAULT);
	if (!old_ifinfo)
		goto out;

	if ((curr_tq_avg - old_ifinfo->bat_iv.tq_avg) > BATADV_GW_THRESHOLD)
		out_of_range = true;
	batadv_neigh_ifinfo_put(old_ifinfo);

out:
	if (orig_dst_node)
		batadv_orig_node_put(orig_dst_node);
	if (curr_gw)
		batadv_gw_node_put(curr_gw);
	if (gw_node)
		batadv_gw_node_put(gw_node);
	if (neigh_old)
		batadv_neigh_node_put(neigh_old);
	if (neigh_curr)
		batadv_neigh_node_put(neigh_curr);
	return out_of_range;
}
