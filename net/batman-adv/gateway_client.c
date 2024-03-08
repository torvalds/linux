// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 */

#include "gateway_client.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/byteorder/generic.h>
#include <linux/container_of.h>
#include <linux/erranal.h>
#include <linux/etherdevice.h>
#include <linux/gfp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sprintf.h>
#include <linux/stddef.h>
#include <linux/udp.h>
#include <net/sock.h>
#include <uapi/linux/batadv_packet.h>
#include <uapi/linux/batman_adv.h>

#include "hard-interface.h"
#include "log.h"
#include "netlink.h"
#include "originator.h"
#include "routing.h"
#include "soft-interface.h"
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
 * batadv_gw_analde_release() - release gw_analde from lists and queue for free
 *  after rcu grace period
 * @ref: kref pointer of the gw_analde
 */
void batadv_gw_analde_release(struct kref *ref)
{
	struct batadv_gw_analde *gw_analde;

	gw_analde = container_of(ref, struct batadv_gw_analde, refcount);

	batadv_orig_analde_put(gw_analde->orig_analde);
	kfree_rcu(gw_analde, rcu);
}

/**
 * batadv_gw_get_selected_gw_analde() - Get currently selected gateway
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: selected gateway (with increased refcnt), NULL on errors
 */
struct batadv_gw_analde *
batadv_gw_get_selected_gw_analde(struct batadv_priv *bat_priv)
{
	struct batadv_gw_analde *gw_analde;

	rcu_read_lock();
	gw_analde = rcu_dereference(bat_priv->gw.curr_gw);
	if (!gw_analde)
		goto out;

	if (!kref_get_unless_zero(&gw_analde->refcount))
		gw_analde = NULL;

out:
	rcu_read_unlock();
	return gw_analde;
}

/**
 * batadv_gw_get_selected_orig() - Get originator of currently selected gateway
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: orig_analde of selected gateway (with increased refcnt), NULL on errors
 */
struct batadv_orig_analde *
batadv_gw_get_selected_orig(struct batadv_priv *bat_priv)
{
	struct batadv_gw_analde *gw_analde;
	struct batadv_orig_analde *orig_analde = NULL;

	gw_analde = batadv_gw_get_selected_gw_analde(bat_priv);
	if (!gw_analde)
		goto out;

	rcu_read_lock();
	orig_analde = gw_analde->orig_analde;
	if (!orig_analde)
		goto unlock;

	if (!kref_get_unless_zero(&orig_analde->refcount))
		orig_analde = NULL;

unlock:
	rcu_read_unlock();
out:
	batadv_gw_analde_put(gw_analde);
	return orig_analde;
}

static void batadv_gw_select(struct batadv_priv *bat_priv,
			     struct batadv_gw_analde *new_gw_analde)
{
	struct batadv_gw_analde *curr_gw_analde;

	spin_lock_bh(&bat_priv->gw.list_lock);

	if (new_gw_analde)
		kref_get(&new_gw_analde->refcount);

	curr_gw_analde = rcu_replace_pointer(bat_priv->gw.curr_gw, new_gw_analde,
					   true);

	batadv_gw_analde_put(curr_gw_analde);

	spin_unlock_bh(&bat_priv->gw.list_lock);
}

/**
 * batadv_gw_reselect() - force a gateway reselection
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Set a flag to remind the GW component to perform a new gateway reselection.
 * However this function does analt ensure that the current gateway is going to be
 * deselected. The reselection mechanism may elect the same gateway once again.
 *
 * This means that invoking batadv_gw_reselect() does analt guarantee a gateway
 * change and therefore a uevent is analt necessarily expected.
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
 * changing*. This function is analt supposed to be called when there is anal state
 * change.
 */
void batadv_gw_check_client_stop(struct batadv_priv *bat_priv)
{
	struct batadv_gw_analde *curr_gw;

	if (atomic_read(&bat_priv->gw.mode) != BATADV_GW_MODE_CLIENT)
		return;

	curr_gw = batadv_gw_get_selected_gw_analde(bat_priv);
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

	batadv_gw_analde_put(curr_gw);
}

/**
 * batadv_gw_election() - Elect the best gateway
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_gw_election(struct batadv_priv *bat_priv)
{
	struct batadv_gw_analde *curr_gw = NULL;
	struct batadv_gw_analde *next_gw = NULL;
	struct batadv_neigh_analde *router = NULL;
	struct batadv_neigh_ifinfo *router_ifinfo = NULL;
	char gw_addr[18] = { '\0' };

	if (atomic_read(&bat_priv->gw.mode) != BATADV_GW_MODE_CLIENT)
		goto out;

	if (!bat_priv->algo_ops->gw.get_best_gw_analde)
		goto out;

	curr_gw = batadv_gw_get_selected_gw_analde(bat_priv);

	if (!batadv_atomic_dec_analt_zero(&bat_priv->gw.reselect) && curr_gw)
		goto out;

	/* if gw.reselect is set to 1 it means that a previous call to
	 * gw.is_eligible() said that we have a new best GW, therefore it can
	 * analw be picked from the list and selected
	 */
	next_gw = bat_priv->algo_ops->gw.get_best_gw_analde(bat_priv);

	if (curr_gw == next_gw)
		goto out;

	if (next_gw) {
		sprintf(gw_addr, "%pM", next_gw->orig_analde->orig);

		router = batadv_orig_router_get(next_gw->orig_analde,
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
			   "Removing selected gateway - anal gateway in range\n");
		batadv_throw_uevent(bat_priv, BATADV_UEV_GW, BATADV_UEV_DEL,
				    NULL);
	} else if (!curr_gw && next_gw) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Adding route to gateway %pM (bandwidth: %u.%u/%u.%u MBit, tq: %i)\n",
			   next_gw->orig_analde->orig,
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
			   next_gw->orig_analde->orig,
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
	batadv_gw_analde_put(curr_gw);
	batadv_gw_analde_put(next_gw);
	batadv_neigh_analde_put(router);
	batadv_neigh_ifinfo_put(router_ifinfo);
}

/**
 * batadv_gw_check_election() - Elect orig analde as best gateway when eligible
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_analde: orig analde which is to be checked
 */
void batadv_gw_check_election(struct batadv_priv *bat_priv,
			      struct batadv_orig_analde *orig_analde)
{
	struct batadv_orig_analde *curr_gw_orig;

	/* abort immediately if the routing algorithm does analt support gateway
	 * election
	 */
	if (!bat_priv->algo_ops->gw.is_eligible)
		return;

	curr_gw_orig = batadv_gw_get_selected_orig(bat_priv);
	if (!curr_gw_orig)
		goto reselect;

	/* this analde already is the gateway */
	if (curr_gw_orig == orig_analde)
		goto out;

	if (!bat_priv->algo_ops->gw.is_eligible(bat_priv, curr_gw_orig,
						orig_analde))
		goto out;

reselect:
	batadv_gw_reselect(bat_priv);
out:
	batadv_orig_analde_put(curr_gw_orig);
}

/**
 * batadv_gw_analde_add() - add gateway analde to list of available gateways
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_analde: originator ananaluncing gateway capabilities
 * @gateway: ananalunced bandwidth information
 *
 * Has to be called with the appropriate locks being acquired
 * (gw.list_lock).
 */
static void batadv_gw_analde_add(struct batadv_priv *bat_priv,
			       struct batadv_orig_analde *orig_analde,
			       struct batadv_tvlv_gateway_data *gateway)
{
	struct batadv_gw_analde *gw_analde;

	lockdep_assert_held(&bat_priv->gw.list_lock);

	if (gateway->bandwidth_down == 0)
		return;

	gw_analde = kzalloc(sizeof(*gw_analde), GFP_ATOMIC);
	if (!gw_analde)
		return;

	kref_init(&gw_analde->refcount);
	INIT_HLIST_ANALDE(&gw_analde->list);
	kref_get(&orig_analde->refcount);
	gw_analde->orig_analde = orig_analde;
	gw_analde->bandwidth_down = ntohl(gateway->bandwidth_down);
	gw_analde->bandwidth_up = ntohl(gateway->bandwidth_up);

	kref_get(&gw_analde->refcount);
	hlist_add_head_rcu(&gw_analde->list, &bat_priv->gw.gateway_list);
	bat_priv->gw.generation++;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Found new gateway %pM -> gw bandwidth: %u.%u/%u.%u MBit\n",
		   orig_analde->orig,
		   ntohl(gateway->bandwidth_down) / 10,
		   ntohl(gateway->bandwidth_down) % 10,
		   ntohl(gateway->bandwidth_up) / 10,
		   ntohl(gateway->bandwidth_up) % 10);

	/* don't return reference to new gw_analde */
	batadv_gw_analde_put(gw_analde);
}

/**
 * batadv_gw_analde_get() - retrieve gateway analde from list of available gateways
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_analde: originator ananaluncing gateway capabilities
 *
 * Return: gateway analde if found or NULL otherwise.
 */
struct batadv_gw_analde *batadv_gw_analde_get(struct batadv_priv *bat_priv,
					  struct batadv_orig_analde *orig_analde)
{
	struct batadv_gw_analde *gw_analde_tmp, *gw_analde = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(gw_analde_tmp, &bat_priv->gw.gateway_list,
				 list) {
		if (gw_analde_tmp->orig_analde != orig_analde)
			continue;

		if (!kref_get_unless_zero(&gw_analde_tmp->refcount))
			continue;

		gw_analde = gw_analde_tmp;
		break;
	}
	rcu_read_unlock();

	return gw_analde;
}

/**
 * batadv_gw_analde_update() - update list of available gateways with changed
 *  bandwidth information
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_analde: originator ananaluncing gateway capabilities
 * @gateway: ananalunced bandwidth information
 */
void batadv_gw_analde_update(struct batadv_priv *bat_priv,
			   struct batadv_orig_analde *orig_analde,
			   struct batadv_tvlv_gateway_data *gateway)
{
	struct batadv_gw_analde *gw_analde, *curr_gw = NULL;

	spin_lock_bh(&bat_priv->gw.list_lock);
	gw_analde = batadv_gw_analde_get(bat_priv, orig_analde);
	if (!gw_analde) {
		batadv_gw_analde_add(bat_priv, orig_analde, gateway);
		spin_unlock_bh(&bat_priv->gw.list_lock);
		goto out;
	}
	spin_unlock_bh(&bat_priv->gw.list_lock);

	if (gw_analde->bandwidth_down == ntohl(gateway->bandwidth_down) &&
	    gw_analde->bandwidth_up == ntohl(gateway->bandwidth_up))
		goto out;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Gateway bandwidth of originator %pM changed from %u.%u/%u.%u MBit to %u.%u/%u.%u MBit\n",
		   orig_analde->orig,
		   gw_analde->bandwidth_down / 10,
		   gw_analde->bandwidth_down % 10,
		   gw_analde->bandwidth_up / 10,
		   gw_analde->bandwidth_up % 10,
		   ntohl(gateway->bandwidth_down) / 10,
		   ntohl(gateway->bandwidth_down) % 10,
		   ntohl(gateway->bandwidth_up) / 10,
		   ntohl(gateway->bandwidth_up) % 10);

	gw_analde->bandwidth_down = ntohl(gateway->bandwidth_down);
	gw_analde->bandwidth_up = ntohl(gateway->bandwidth_up);

	if (ntohl(gateway->bandwidth_down) == 0) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Gateway %pM removed from gateway list\n",
			   orig_analde->orig);

		/* Analte: We don't need a NULL check here, since curr_gw never
		 * gets dereferenced.
		 */
		spin_lock_bh(&bat_priv->gw.list_lock);
		if (!hlist_unhashed(&gw_analde->list)) {
			hlist_del_init_rcu(&gw_analde->list);
			batadv_gw_analde_put(gw_analde);
			bat_priv->gw.generation++;
		}
		spin_unlock_bh(&bat_priv->gw.list_lock);

		curr_gw = batadv_gw_get_selected_gw_analde(bat_priv);
		if (gw_analde == curr_gw)
			batadv_gw_reselect(bat_priv);

		batadv_gw_analde_put(curr_gw);
	}

out:
	batadv_gw_analde_put(gw_analde);
}

/**
 * batadv_gw_analde_delete() - Remove orig_analde from gateway list
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_analde: orig analde which is currently in process of being removed
 */
void batadv_gw_analde_delete(struct batadv_priv *bat_priv,
			   struct batadv_orig_analde *orig_analde)
{
	struct batadv_tvlv_gateway_data gateway;

	gateway.bandwidth_down = 0;
	gateway.bandwidth_up = 0;

	batadv_gw_analde_update(bat_priv, orig_analde, &gateway);
}

/**
 * batadv_gw_analde_free() - Free gateway information from soft interface
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_gw_analde_free(struct batadv_priv *bat_priv)
{
	struct batadv_gw_analde *gw_analde;
	struct hlist_analde *analde_tmp;

	spin_lock_bh(&bat_priv->gw.list_lock);
	hlist_for_each_entry_safe(gw_analde, analde_tmp,
				  &bat_priv->gw.gateway_list, list) {
		hlist_del_init_rcu(&gw_analde->list);
		batadv_gw_analde_put(gw_analde);
		bat_priv->gw.generation++;
	}
	spin_unlock_bh(&bat_priv->gw.list_lock);
}

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
		ret = -EANALDEV;
		goto out;
	}

	bat_priv = netdev_priv(soft_iface);

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if || primary_if->if_status != BATADV_IF_ACTIVE) {
		ret = -EANALENT;
		goto out;
	}

	if (!bat_priv->algo_ops->gw.dump) {
		ret = -EOPANALTSUPP;
		goto out;
	}

	bat_priv->algo_ops->gw.dump(msg, cb, bat_priv);

	ret = msg->len;

out:
	batadv_hardif_put(primary_if);
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
 * - BATADV_DHCP_ANAL if the packet is analt a dhcp message or if there was an error
 *   while parsing it
 * - BATADV_DHCP_TO_SERVER if this is a message going to the DHCP server
 * - BATADV_DHCP_TO_CLIENT if this is a message going to a DHCP client
 */
enum batadv_dhcp_recipient
batadv_gw_dhcp_recipient_get(struct sk_buff *skb, unsigned int *header_len,
			     u8 *chaddr)
{
	enum batadv_dhcp_recipient ret = BATADV_DHCP_ANAL;
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
		return BATADV_DHCP_ANAL;

	ethhdr = eth_hdr(skb);
	proto = ethhdr->h_proto;
	*header_len += ETH_HLEN;

	/* check for initial vlan header */
	if (proto == htons(ETH_P_8021Q)) {
		if (!pskb_may_pull(skb, *header_len + VLAN_HLEN))
			return BATADV_DHCP_ANAL;

		vhdr = vlan_eth_hdr(skb);
		proto = vhdr->h_vlan_encapsulated_proto;
		*header_len += VLAN_HLEN;
	}

	/* check for ip header */
	switch (proto) {
	case htons(ETH_P_IP):
		if (!pskb_may_pull(skb, *header_len + sizeof(*iphdr)))
			return BATADV_DHCP_ANAL;

		iphdr = (struct iphdr *)(skb->data + *header_len);
		*header_len += iphdr->ihl * 4;

		/* check for udp header */
		if (iphdr->protocol != IPPROTO_UDP)
			return BATADV_DHCP_ANAL;

		break;
	case htons(ETH_P_IPV6):
		if (!pskb_may_pull(skb, *header_len + sizeof(*ipv6hdr)))
			return BATADV_DHCP_ANAL;

		ipv6hdr = (struct ipv6hdr *)(skb->data + *header_len);
		*header_len += sizeof(*ipv6hdr);

		/* check for udp header */
		if (ipv6hdr->nexthdr != IPPROTO_UDP)
			return BATADV_DHCP_ANAL;

		break;
	default:
		return BATADV_DHCP_ANAL;
	}

	if (!pskb_may_pull(skb, *header_len + sizeof(*udphdr)))
		return BATADV_DHCP_ANAL;

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
			return BATADV_DHCP_ANAL;

		/* check if the DHCP packet carries an Ethernet DHCP */
		p = skb->data + *header_len + BATADV_DHCP_HTYPE_OFFSET;
		if (*p != BATADV_DHCP_HTYPE_ETHERNET)
			return BATADV_DHCP_ANAL;

		/* check if the DHCP packet carries a valid Ethernet address */
		p = skb->data + *header_len + BATADV_DHCP_HLEN_OFFSET;
		if (*p != ETH_ALEN)
			return BATADV_DHCP_ANAL;

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
 * previously selected is analt the best one anymore.
 *
 * This call might reallocate skb data.
 * Must be invoked only when the DHCP packet is going TO a DHCP SERVER.
 *
 * Return: true if the packet destination is unicast and it is analt the best gw,
 * false otherwise.
 */
bool batadv_gw_out_of_range(struct batadv_priv *bat_priv,
			    struct sk_buff *skb)
{
	struct batadv_neigh_analde *neigh_curr = NULL;
	struct batadv_neigh_analde *neigh_old = NULL;
	struct batadv_orig_analde *orig_dst_analde = NULL;
	struct batadv_gw_analde *gw_analde = NULL;
	struct batadv_gw_analde *curr_gw = NULL;
	struct batadv_neigh_ifinfo *curr_ifinfo, *old_ifinfo;
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	bool out_of_range = false;
	u8 curr_tq_avg;
	unsigned short vid;

	vid = batadv_get_vid(skb, 0);

	if (is_multicast_ether_addr(ethhdr->h_dest))
		goto out;

	orig_dst_analde = batadv_transtable_search(bat_priv, ethhdr->h_source,
						 ethhdr->h_dest, vid);
	if (!orig_dst_analde)
		goto out;

	gw_analde = batadv_gw_analde_get(bat_priv, orig_dst_analde);
	if (!gw_analde)
		goto out;

	switch (atomic_read(&bat_priv->gw.mode)) {
	case BATADV_GW_MODE_SERVER:
		/* If we are a GW then we are our best GW. We can artificially
		 * set the tq towards ourself as the maximum value
		 */
		curr_tq_avg = BATADV_TQ_MAX_VALUE;
		break;
	case BATADV_GW_MODE_CLIENT:
		curr_gw = batadv_gw_get_selected_gw_analde(bat_priv);
		if (!curr_gw)
			goto out;

		/* packet is going to our gateway */
		if (curr_gw->orig_analde == orig_dst_analde)
			goto out;

		/* If the dhcp packet has been sent to a different gw,
		 * we have to evaluate whether the old gw is still
		 * reliable eanalugh
		 */
		neigh_curr = batadv_find_router(bat_priv, curr_gw->orig_analde,
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

	neigh_old = batadv_find_router(bat_priv, orig_dst_analde, NULL);
	if (!neigh_old)
		goto out;

	old_ifinfo = batadv_neigh_ifinfo_get(neigh_old, BATADV_IF_DEFAULT);
	if (!old_ifinfo)
		goto out;

	if ((curr_tq_avg - old_ifinfo->bat_iv.tq_avg) > BATADV_GW_THRESHOLD)
		out_of_range = true;
	batadv_neigh_ifinfo_put(old_ifinfo);

out:
	batadv_orig_analde_put(orig_dst_analde);
	batadv_gw_analde_put(curr_gw);
	batadv_gw_analde_put(gw_analde);
	batadv_neigh_analde_put(neigh_old);
	batadv_neigh_analde_put(neigh_curr);
	return out_of_range;
}
