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
#include "routing.h"
#include "send.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "icmp_socket.h"
#include "translation-table.h"
#include "originator.h"
#include "bridge_loop_avoidance.h"
#include "distributed-arp-table.h"
#include "network-coding.h"
#include "fragmentation.h"

#include <linux/if_vlan.h>

static int batadv_route_unicast_packet(struct sk_buff *skb,
				       struct batadv_hard_iface *recv_if);

/**
 * _batadv_update_route - set the router for this originator
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig node which is to be configured
 * @recv_if: the receive interface for which this route is set
 * @neigh_node: neighbor which should be the next router
 *
 * This function does not perform any error checks
 */
static void _batadv_update_route(struct batadv_priv *bat_priv,
				 struct batadv_orig_node *orig_node,
				 struct batadv_hard_iface *recv_if,
				 struct batadv_neigh_node *neigh_node)
{
	struct batadv_orig_ifinfo *orig_ifinfo;
	struct batadv_neigh_node *curr_router;

	orig_ifinfo = batadv_orig_ifinfo_get(orig_node, recv_if);
	if (!orig_ifinfo)
		return;

	rcu_read_lock();
	curr_router = rcu_dereference(orig_ifinfo->router);
	if (curr_router && !atomic_inc_not_zero(&curr_router->refcount))
		curr_router = NULL;
	rcu_read_unlock();

	/* route deleted */
	if ((curr_router) && (!neigh_node)) {
		batadv_dbg(BATADV_DBG_ROUTES, bat_priv,
			   "Deleting route towards: %pM\n", orig_node->orig);
		batadv_tt_global_del_orig(bat_priv, orig_node, -1,
					  "Deleted route towards originator");

	/* route added */
	} else if ((!curr_router) && (neigh_node)) {
		batadv_dbg(BATADV_DBG_ROUTES, bat_priv,
			   "Adding route towards: %pM (via %pM)\n",
			   orig_node->orig, neigh_node->addr);
	/* route changed */
	} else if (neigh_node && curr_router) {
		batadv_dbg(BATADV_DBG_ROUTES, bat_priv,
			   "Changing route towards: %pM (now via %pM - was via %pM)\n",
			   orig_node->orig, neigh_node->addr,
			   curr_router->addr);
	}

	if (curr_router)
		batadv_neigh_node_free_ref(curr_router);

	/* increase refcount of new best neighbor */
	if (neigh_node && !atomic_inc_not_zero(&neigh_node->refcount))
		neigh_node = NULL;

	spin_lock_bh(&orig_node->neigh_list_lock);
	rcu_assign_pointer(orig_ifinfo->router, neigh_node);
	spin_unlock_bh(&orig_node->neigh_list_lock);
	batadv_orig_ifinfo_free_ref(orig_ifinfo);

	/* decrease refcount of previous best neighbor */
	if (curr_router)
		batadv_neigh_node_free_ref(curr_router);
}

/**
 * batadv_update_route - set the router for this originator
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig node which is to be configured
 * @recv_if: the receive interface for which this route is set
 * @neigh_node: neighbor which should be the next router
 */
void batadv_update_route(struct batadv_priv *bat_priv,
			 struct batadv_orig_node *orig_node,
			 struct batadv_hard_iface *recv_if,
			 struct batadv_neigh_node *neigh_node)
{
	struct batadv_neigh_node *router = NULL;

	if (!orig_node)
		goto out;

	router = batadv_orig_router_get(orig_node, recv_if);

	if (router != neigh_node)
		_batadv_update_route(bat_priv, orig_node, recv_if, neigh_node);

out:
	if (router)
		batadv_neigh_node_free_ref(router);
}

/* checks whether the host restarted and is in the protection time.
 * returns:
 *  0 if the packet is to be accepted
 *  1 if the packet is to be ignored.
 */
int batadv_window_protected(struct batadv_priv *bat_priv, int32_t seq_num_diff,
			    unsigned long *last_reset)
{
	if (seq_num_diff <= -BATADV_TQ_LOCAL_WINDOW_SIZE ||
	    seq_num_diff >= BATADV_EXPECTED_SEQNO_RANGE) {
		if (!batadv_has_timed_out(*last_reset,
					  BATADV_RESET_PROTECTION_MS))
			return 1;

		*last_reset = jiffies;
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "old packet received, start protection\n");
	}

	return 0;
}

bool batadv_check_management_packet(struct sk_buff *skb,
				    struct batadv_hard_iface *hard_iface,
				    int header_len)
{
	struct ethhdr *ethhdr;

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb, header_len)))
		return false;

	ethhdr = eth_hdr(skb);

	/* packet with broadcast indication but unicast recipient */
	if (!is_broadcast_ether_addr(ethhdr->h_dest))
		return false;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		return false;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, 0) < 0)
		return false;

	/* keep skb linear */
	if (skb_linearize(skb) < 0)
		return false;

	return true;
}

/**
 * batadv_recv_my_icmp_packet - receive an icmp packet locally
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: icmp packet to process
 *
 * Returns NET_RX_SUCCESS if the packet has been consumed or NET_RX_DROP
 * otherwise.
 */
static int batadv_recv_my_icmp_packet(struct batadv_priv *bat_priv,
				      struct sk_buff *skb)
{
	struct batadv_hard_iface *primary_if = NULL;
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_icmp_header *icmph;
	int res, ret = NET_RX_DROP;

	icmph = (struct batadv_icmp_header *)skb->data;

	switch (icmph->msg_type) {
	case BATADV_ECHO_REPLY:
	case BATADV_DESTINATION_UNREACHABLE:
	case BATADV_TTL_EXCEEDED:
		/* receive the packet */
		if (skb_linearize(skb) < 0)
			break;

		batadv_socket_receive_packet(icmph, skb->len);
		break;
	case BATADV_ECHO_REQUEST:
		/* answer echo request (ping) */
		primary_if = batadv_primary_if_get_selected(bat_priv);
		if (!primary_if)
			goto out;

		/* get routing information */
		orig_node = batadv_orig_hash_find(bat_priv, icmph->orig);
		if (!orig_node)
			goto out;

		/* create a copy of the skb, if needed, to modify it. */
		if (skb_cow(skb, ETH_HLEN) < 0)
			goto out;

		icmph = (struct batadv_icmp_header *)skb->data;

		ether_addr_copy(icmph->dst, icmph->orig);
		ether_addr_copy(icmph->orig, primary_if->net_dev->dev_addr);
		icmph->msg_type = BATADV_ECHO_REPLY;
		icmph->ttl = BATADV_TTL;

		res = batadv_send_skb_to_orig(skb, orig_node, NULL);
		if (res != NET_XMIT_DROP)
			ret = NET_RX_SUCCESS;

		break;
	default:
		/* drop unknown type */
		goto out;
	}
out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}

static int batadv_recv_icmp_ttl_exceeded(struct batadv_priv *bat_priv,
					 struct sk_buff *skb)
{
	struct batadv_hard_iface *primary_if = NULL;
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_icmp_packet *icmp_packet;
	int ret = NET_RX_DROP;

	icmp_packet = (struct batadv_icmp_packet *)skb->data;

	/* send TTL exceeded if packet is an echo request (traceroute) */
	if (icmp_packet->msg_type != BATADV_ECHO_REQUEST) {
		pr_debug("Warning - can't forward icmp packet from %pM to %pM: ttl exceeded\n",
			 icmp_packet->orig, icmp_packet->dst);
		goto out;
	}

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	/* get routing information */
	orig_node = batadv_orig_hash_find(bat_priv, icmp_packet->orig);
	if (!orig_node)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, ETH_HLEN) < 0)
		goto out;

	icmp_packet = (struct batadv_icmp_packet *)skb->data;

	ether_addr_copy(icmp_packet->dst, icmp_packet->orig);
	ether_addr_copy(icmp_packet->orig, primary_if->net_dev->dev_addr);
	icmp_packet->msg_type = BATADV_TTL_EXCEEDED;
	icmp_packet->ttl = BATADV_TTL;

	if (batadv_send_skb_to_orig(skb, orig_node, NULL) != NET_XMIT_DROP)
		ret = NET_RX_SUCCESS;

out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}

int batadv_recv_icmp_packet(struct sk_buff *skb,
			    struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_icmp_header *icmph;
	struct batadv_icmp_packet_rr *icmp_packet_rr;
	struct ethhdr *ethhdr;
	struct batadv_orig_node *orig_node = NULL;
	int hdr_size = sizeof(struct batadv_icmp_header);
	int ret = NET_RX_DROP;

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb, hdr_size)))
		goto out;

	ethhdr = eth_hdr(skb);

	/* packet with unicast indication but broadcast recipient */
	if (is_broadcast_ether_addr(ethhdr->h_dest))
		goto out;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		goto out;

	/* not for me */
	if (!batadv_is_my_mac(bat_priv, ethhdr->h_dest))
		goto out;

	icmph = (struct batadv_icmp_header *)skb->data;

	/* add record route information if not full */
	if ((icmph->msg_type == BATADV_ECHO_REPLY ||
	     icmph->msg_type == BATADV_ECHO_REQUEST) &&
	    (skb->len >= sizeof(struct batadv_icmp_packet_rr))) {
		if (skb_linearize(skb) < 0)
			goto out;

		/* create a copy of the skb, if needed, to modify it. */
		if (skb_cow(skb, ETH_HLEN) < 0)
			goto out;

		icmph = (struct batadv_icmp_header *)skb->data;
		icmp_packet_rr = (struct batadv_icmp_packet_rr *)icmph;
		if (icmp_packet_rr->rr_cur >= BATADV_RR_LEN)
			goto out;

		ether_addr_copy(icmp_packet_rr->rr[icmp_packet_rr->rr_cur],
				ethhdr->h_dest);
		icmp_packet_rr->rr_cur++;
	}

	/* packet for me */
	if (batadv_is_my_mac(bat_priv, icmph->dst))
		return batadv_recv_my_icmp_packet(bat_priv, skb);

	/* TTL exceeded */
	if (icmph->ttl < 2)
		return batadv_recv_icmp_ttl_exceeded(bat_priv, skb);

	/* get routing information */
	orig_node = batadv_orig_hash_find(bat_priv, icmph->dst);
	if (!orig_node)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, ETH_HLEN) < 0)
		goto out;

	icmph = (struct batadv_icmp_header *)skb->data;

	/* decrement ttl */
	icmph->ttl--;

	/* route it */
	if (batadv_send_skb_to_orig(skb, orig_node, recv_if) != NET_XMIT_DROP)
		ret = NET_RX_SUCCESS;

out:
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}

/**
 * batadv_check_unicast_packet - Check for malformed unicast packets
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: packet to check
 * @hdr_size: size of header to pull
 *
 * Check for short header and bad addresses in given packet. Returns negative
 * value when check fails and 0 otherwise. The negative value depends on the
 * reason: -ENODATA for bad header, -EBADR for broadcast destination or source,
 * and -EREMOTE for non-local (other host) destination.
 */
static int batadv_check_unicast_packet(struct batadv_priv *bat_priv,
				       struct sk_buff *skb, int hdr_size)
{
	struct ethhdr *ethhdr;

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb, hdr_size)))
		return -ENODATA;

	ethhdr = eth_hdr(skb);

	/* packet with unicast indication but broadcast recipient */
	if (is_broadcast_ether_addr(ethhdr->h_dest))
		return -EBADR;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		return -EBADR;

	/* not for me */
	if (!batadv_is_my_mac(bat_priv, ethhdr->h_dest))
		return -EREMOTE;

	return 0;
}

/**
 * batadv_find_router - find a suitable router for this originator
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: the destination node
 * @recv_if: pointer to interface this packet was received on
 *
 * Returns the router which should be used for this orig_node on
 * this interface, or NULL if not available.
 */
struct batadv_neigh_node *
batadv_find_router(struct batadv_priv *bat_priv,
		   struct batadv_orig_node *orig_node,
		   struct batadv_hard_iface *recv_if)
{
	struct batadv_algo_ops *bao = bat_priv->bat_algo_ops;
	struct batadv_neigh_node *first_candidate_router = NULL;
	struct batadv_neigh_node *next_candidate_router = NULL;
	struct batadv_neigh_node *router, *cand_router = NULL;
	struct batadv_neigh_node *last_cand_router = NULL;
	struct batadv_orig_ifinfo *cand, *first_candidate = NULL;
	struct batadv_orig_ifinfo *next_candidate = NULL;
	struct batadv_orig_ifinfo *last_candidate;
	bool last_candidate_found = false;

	if (!orig_node)
		return NULL;

	router = batadv_orig_router_get(orig_node, recv_if);

	if (!router)
		return router;

	/* only consider bonding for recv_if == BATADV_IF_DEFAULT (first hop)
	 * and if activated.
	 */
	if (!(recv_if == BATADV_IF_DEFAULT && atomic_read(&bat_priv->bonding)))
		return router;

	/* bonding: loop through the list of possible routers found
	 * for the various outgoing interfaces and find a candidate after
	 * the last chosen bonding candidate (next_candidate). If no such
	 * router is found, use the first candidate found (the previously
	 * chosen bonding candidate might have been the last one in the list).
	 * If this can't be found either, return the previously chosen
	 * router - obviously there are no other candidates.
	 */
	rcu_read_lock();
	last_candidate = orig_node->last_bonding_candidate;
	if (last_candidate)
		last_cand_router = rcu_dereference(last_candidate->router);

	hlist_for_each_entry_rcu(cand, &orig_node->ifinfo_list, list) {
		/* acquire some structures and references ... */
		if (!atomic_inc_not_zero(&cand->refcount))
			continue;

		cand_router = rcu_dereference(cand->router);
		if (!cand_router)
			goto next;

		if (!atomic_inc_not_zero(&cand_router->refcount)) {
			cand_router = NULL;
			goto next;
		}

		/* alternative candidate should be good enough to be
		 * considered
		 */
		if (!bao->bat_neigh_is_equiv_or_better(cand_router,
						       cand->if_outgoing,
						       router, recv_if))
			goto next;

		/* don't use the same router twice */
		if (last_cand_router == cand_router)
			goto next;

		/* mark the first possible candidate */
		if (!first_candidate) {
			atomic_inc(&cand_router->refcount);
			atomic_inc(&cand->refcount);
			first_candidate = cand;
			first_candidate_router = cand_router;
		}

		/* check if the loop has already passed the previously selected
		 * candidate ... this function should select the next candidate
		 * AFTER the previously used bonding candidate.
		 */
		if (!last_candidate || last_candidate_found) {
			next_candidate = cand;
			next_candidate_router = cand_router;
			break;
		}

		if (last_candidate == cand)
			last_candidate_found = true;
next:
		/* free references */
		if (cand_router) {
			batadv_neigh_node_free_ref(cand_router);
			cand_router = NULL;
		}
		batadv_orig_ifinfo_free_ref(cand);
	}
	rcu_read_unlock();

	/* last_bonding_candidate is reset below, remove the old reference. */
	if (orig_node->last_bonding_candidate)
		batadv_orig_ifinfo_free_ref(orig_node->last_bonding_candidate);

	/* After finding candidates, handle the three cases:
	 * 1) there is a next candidate, use that
	 * 2) there is no next candidate, use the first of the list
	 * 3) there is no candidate at all, return the default router
	 */
	if (next_candidate) {
		batadv_neigh_node_free_ref(router);

		/* remove references to first candidate, we don't need it. */
		if (first_candidate) {
			batadv_neigh_node_free_ref(first_candidate_router);
			batadv_orig_ifinfo_free_ref(first_candidate);
		}
		router = next_candidate_router;
		orig_node->last_bonding_candidate = next_candidate;
	} else if (first_candidate) {
		batadv_neigh_node_free_ref(router);

		/* refcounting has already been done in the loop above. */
		router = first_candidate_router;
		orig_node->last_bonding_candidate = first_candidate;
	} else {
		orig_node->last_bonding_candidate = NULL;
	}

	return router;
}

static int batadv_route_unicast_packet(struct sk_buff *skb,
				       struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_unicast_packet *unicast_packet;
	struct ethhdr *ethhdr = eth_hdr(skb);
	int res, hdr_len, ret = NET_RX_DROP;

	unicast_packet = (struct batadv_unicast_packet *)skb->data;

	/* TTL exceeded */
	if (unicast_packet->ttl < 2) {
		pr_debug("Warning - can't forward unicast packet from %pM to %pM: ttl exceeded\n",
			 ethhdr->h_source, unicast_packet->dest);
		goto out;
	}

	/* get routing information */
	orig_node = batadv_orig_hash_find(bat_priv, unicast_packet->dest);

	if (!orig_node)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, ETH_HLEN) < 0)
		goto out;

	/* decrement ttl */
	unicast_packet = (struct batadv_unicast_packet *)skb->data;
	unicast_packet->ttl--;

	switch (unicast_packet->packet_type) {
	case BATADV_UNICAST_4ADDR:
		hdr_len = sizeof(struct batadv_unicast_4addr_packet);
		break;
	case BATADV_UNICAST:
		hdr_len = sizeof(struct batadv_unicast_packet);
		break;
	default:
		/* other packet types not supported - yet */
		hdr_len = -1;
		break;
	}

	if (hdr_len > 0)
		batadv_skb_set_priority(skb, hdr_len);

	res = batadv_send_skb_to_orig(skb, orig_node, recv_if);

	/* translate transmit result into receive result */
	if (res == NET_XMIT_SUCCESS) {
		/* skb was transmitted and consumed */
		batadv_inc_counter(bat_priv, BATADV_CNT_FORWARD);
		batadv_add_counter(bat_priv, BATADV_CNT_FORWARD_BYTES,
				   skb->len + ETH_HLEN);

		ret = NET_RX_SUCCESS;
	} else if (res == NET_XMIT_POLICED) {
		/* skb was buffered and consumed */
		ret = NET_RX_SUCCESS;
	}

out:
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}

/**
 * batadv_reroute_unicast_packet - update the unicast header for re-routing
 * @bat_priv: the bat priv with all the soft interface information
 * @unicast_packet: the unicast header to be updated
 * @dst_addr: the payload destination
 * @vid: VLAN identifier
 *
 * Search the translation table for dst_addr and update the unicast header with
 * the new corresponding information (originator address where the destination
 * client currently is and its known TTVN)
 *
 * Returns true if the packet header has been updated, false otherwise
 */
static bool
batadv_reroute_unicast_packet(struct batadv_priv *bat_priv,
			      struct batadv_unicast_packet *unicast_packet,
			      uint8_t *dst_addr, unsigned short vid)
{
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_hard_iface *primary_if = NULL;
	bool ret = false;
	uint8_t *orig_addr, orig_ttvn;

	if (batadv_is_my_client(bat_priv, dst_addr, vid)) {
		primary_if = batadv_primary_if_get_selected(bat_priv);
		if (!primary_if)
			goto out;
		orig_addr = primary_if->net_dev->dev_addr;
		orig_ttvn = (uint8_t)atomic_read(&bat_priv->tt.vn);
	} else {
		orig_node = batadv_transtable_search(bat_priv, NULL, dst_addr,
						     vid);
		if (!orig_node)
			goto out;

		if (batadv_compare_eth(orig_node->orig, unicast_packet->dest))
			goto out;

		orig_addr = orig_node->orig;
		orig_ttvn = (uint8_t)atomic_read(&orig_node->last_ttvn);
	}

	/* update the packet header */
	ether_addr_copy(unicast_packet->dest, orig_addr);
	unicast_packet->ttvn = orig_ttvn;

	ret = true;
out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);

	return ret;
}

static int batadv_check_unicast_ttvn(struct batadv_priv *bat_priv,
				     struct sk_buff *skb, int hdr_len) {
	struct batadv_unicast_packet *unicast_packet;
	struct batadv_hard_iface *primary_if;
	struct batadv_orig_node *orig_node;
	uint8_t curr_ttvn, old_ttvn;
	struct ethhdr *ethhdr;
	unsigned short vid;
	int is_old_ttvn;

	/* check if there is enough data before accessing it */
	if (!pskb_may_pull(skb, hdr_len + ETH_HLEN))
		return 0;

	/* create a copy of the skb (in case of for re-routing) to modify it. */
	if (skb_cow(skb, sizeof(*unicast_packet)) < 0)
		return 0;

	unicast_packet = (struct batadv_unicast_packet *)skb->data;
	vid = batadv_get_vid(skb, hdr_len);
	ethhdr = (struct ethhdr *)(skb->data + hdr_len);

	/* check if the destination client was served by this node and it is now
	 * roaming. In this case, it means that the node has got a ROAM_ADV
	 * message and that it knows the new destination in the mesh to re-route
	 * the packet to
	 */
	if (batadv_tt_local_client_is_roaming(bat_priv, ethhdr->h_dest, vid)) {
		if (batadv_reroute_unicast_packet(bat_priv, unicast_packet,
						  ethhdr->h_dest, vid))
			batadv_dbg_ratelimited(BATADV_DBG_TT,
					       bat_priv,
					       "Rerouting unicast packet to %pM (dst=%pM): Local Roaming\n",
					       unicast_packet->dest,
					       ethhdr->h_dest);
		/* at this point the mesh destination should have been
		 * substituted with the originator address found in the global
		 * table. If not, let the packet go untouched anyway because
		 * there is nothing the node can do
		 */
		return 1;
	}

	/* retrieve the TTVN known by this node for the packet destination. This
	 * value is used later to check if the node which sent (or re-routed
	 * last time) the packet had an updated information or not
	 */
	curr_ttvn = (uint8_t)atomic_read(&bat_priv->tt.vn);
	if (!batadv_is_my_mac(bat_priv, unicast_packet->dest)) {
		orig_node = batadv_orig_hash_find(bat_priv,
						  unicast_packet->dest);
		/* if it is not possible to find the orig_node representing the
		 * destination, the packet can immediately be dropped as it will
		 * not be possible to deliver it
		 */
		if (!orig_node)
			return 0;

		curr_ttvn = (uint8_t)atomic_read(&orig_node->last_ttvn);
		batadv_orig_node_free_ref(orig_node);
	}

	/* check if the TTVN contained in the packet is fresher than what the
	 * node knows
	 */
	is_old_ttvn = batadv_seq_before(unicast_packet->ttvn, curr_ttvn);
	if (!is_old_ttvn)
		return 1;

	old_ttvn = unicast_packet->ttvn;
	/* the packet was forged based on outdated network information. Its
	 * destination can possibly be updated and forwarded towards the new
	 * target host
	 */
	if (batadv_reroute_unicast_packet(bat_priv, unicast_packet,
					  ethhdr->h_dest, vid)) {
		batadv_dbg_ratelimited(BATADV_DBG_TT, bat_priv,
				       "Rerouting unicast packet to %pM (dst=%pM): TTVN mismatch old_ttvn=%u new_ttvn=%u\n",
				       unicast_packet->dest, ethhdr->h_dest,
				       old_ttvn, curr_ttvn);
		return 1;
	}

	/* the packet has not been re-routed: either the destination is
	 * currently served by this node or there is no destination at all and
	 * it is possible to drop the packet
	 */
	if (!batadv_is_my_client(bat_priv, ethhdr->h_dest, vid))
		return 0;

	/* update the header in order to let the packet be delivered to this
	 * node's soft interface
	 */
	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		return 0;

	ether_addr_copy(unicast_packet->dest, primary_if->net_dev->dev_addr);

	batadv_hardif_free_ref(primary_if);

	unicast_packet->ttvn = curr_ttvn;

	return 1;
}

/**
 * batadv_recv_unhandled_unicast_packet - receive and process packets which
 *	are in the unicast number space but not yet known to the implementation
 * @skb: unicast tvlv packet to process
 * @recv_if: pointer to interface this packet was received on
 *
 * Returns NET_RX_SUCCESS if the packet has been consumed or NET_RX_DROP
 * otherwise.
 */
int batadv_recv_unhandled_unicast_packet(struct sk_buff *skb,
					 struct batadv_hard_iface *recv_if)
{
	struct batadv_unicast_packet *unicast_packet;
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	int check, hdr_size = sizeof(*unicast_packet);

	check = batadv_check_unicast_packet(bat_priv, skb, hdr_size);
	if (check < 0)
		return NET_RX_DROP;

	/* we don't know about this type, drop it. */
	unicast_packet = (struct batadv_unicast_packet *)skb->data;
	if (batadv_is_my_mac(bat_priv, unicast_packet->dest))
		return NET_RX_DROP;

	return batadv_route_unicast_packet(skb, recv_if);
}

int batadv_recv_unicast_packet(struct sk_buff *skb,
			       struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_unicast_packet *unicast_packet;
	struct batadv_unicast_4addr_packet *unicast_4addr_packet;
	uint8_t *orig_addr;
	struct batadv_orig_node *orig_node = NULL;
	int check, hdr_size = sizeof(*unicast_packet);
	bool is4addr;

	unicast_packet = (struct batadv_unicast_packet *)skb->data;
	unicast_4addr_packet = (struct batadv_unicast_4addr_packet *)skb->data;

	is4addr = unicast_packet->packet_type == BATADV_UNICAST_4ADDR;
	/* the caller function should have already pulled 2 bytes */
	if (is4addr)
		hdr_size = sizeof(*unicast_4addr_packet);

	/* function returns -EREMOTE for promiscuous packets */
	check = batadv_check_unicast_packet(bat_priv, skb, hdr_size);

	/* Even though the packet is not for us, we might save it to use for
	 * decoding a later received coded packet
	 */
	if (check == -EREMOTE)
		batadv_nc_skb_store_sniffed_unicast(bat_priv, skb);

	if (check < 0)
		return NET_RX_DROP;
	if (!batadv_check_unicast_ttvn(bat_priv, skb, hdr_size))
		return NET_RX_DROP;

	/* packet for me */
	if (batadv_is_my_mac(bat_priv, unicast_packet->dest)) {
		if (is4addr) {
			batadv_dat_inc_counter(bat_priv,
					       unicast_4addr_packet->subtype);
			orig_addr = unicast_4addr_packet->src;
			orig_node = batadv_orig_hash_find(bat_priv, orig_addr);
		}

		if (batadv_dat_snoop_incoming_arp_request(bat_priv, skb,
							  hdr_size))
			goto rx_success;
		if (batadv_dat_snoop_incoming_arp_reply(bat_priv, skb,
							hdr_size))
			goto rx_success;

		batadv_interface_rx(recv_if->soft_iface, skb, recv_if, hdr_size,
				    orig_node);

rx_success:
		if (orig_node)
			batadv_orig_node_free_ref(orig_node);

		return NET_RX_SUCCESS;
	}

	return batadv_route_unicast_packet(skb, recv_if);
}

/**
 * batadv_recv_unicast_tvlv - receive and process unicast tvlv packets
 * @skb: unicast tvlv packet to process
 * @recv_if: pointer to interface this packet was received on
 * @dst_addr: the payload destination
 *
 * Returns NET_RX_SUCCESS if the packet has been consumed or NET_RX_DROP
 * otherwise.
 */
int batadv_recv_unicast_tvlv(struct sk_buff *skb,
			     struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_unicast_tvlv_packet *unicast_tvlv_packet;
	unsigned char *tvlv_buff;
	uint16_t tvlv_buff_len;
	int hdr_size = sizeof(*unicast_tvlv_packet);
	int ret = NET_RX_DROP;

	if (batadv_check_unicast_packet(bat_priv, skb, hdr_size) < 0)
		return NET_RX_DROP;

	/* the header is likely to be modified while forwarding */
	if (skb_cow(skb, hdr_size) < 0)
		return NET_RX_DROP;

	/* packet needs to be linearized to access the tvlv content */
	if (skb_linearize(skb) < 0)
		return NET_RX_DROP;

	unicast_tvlv_packet = (struct batadv_unicast_tvlv_packet *)skb->data;

	tvlv_buff = (unsigned char *)(skb->data + hdr_size);
	tvlv_buff_len = ntohs(unicast_tvlv_packet->tvlv_len);

	if (tvlv_buff_len > skb->len - hdr_size)
		return NET_RX_DROP;

	ret = batadv_tvlv_containers_process(bat_priv, false, NULL,
					     unicast_tvlv_packet->src,
					     unicast_tvlv_packet->dst,
					     tvlv_buff, tvlv_buff_len);

	if (ret != NET_RX_SUCCESS)
		ret = batadv_route_unicast_packet(skb, recv_if);
	else
		consume_skb(skb);

	return ret;
}

/**
 * batadv_recv_frag_packet - process received fragment
 * @skb: the received fragment
 * @recv_if: interface that the skb is received on
 *
 * This function does one of the three following things: 1) Forward fragment, if
 * the assembled packet will exceed our MTU; 2) Buffer fragment, if we till
 * lack further fragments; 3) Merge fragments, if we have all needed parts.
 *
 * Return NET_RX_DROP if the skb is not consumed, NET_RX_SUCCESS otherwise.
 */
int batadv_recv_frag_packet(struct sk_buff *skb,
			    struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_orig_node *orig_node_src = NULL;
	struct batadv_frag_packet *frag_packet;
	int ret = NET_RX_DROP;

	if (batadv_check_unicast_packet(bat_priv, skb,
					sizeof(*frag_packet)) < 0)
		goto out;

	frag_packet = (struct batadv_frag_packet *)skb->data;
	orig_node_src = batadv_orig_hash_find(bat_priv, frag_packet->orig);
	if (!orig_node_src)
		goto out;

	/* Route the fragment if it is not for us and too big to be merged. */
	if (!batadv_is_my_mac(bat_priv, frag_packet->dest) &&
	    batadv_frag_skb_fwd(skb, recv_if, orig_node_src)) {
		ret = NET_RX_SUCCESS;
		goto out;
	}

	batadv_inc_counter(bat_priv, BATADV_CNT_FRAG_RX);
	batadv_add_counter(bat_priv, BATADV_CNT_FRAG_RX_BYTES, skb->len);

	/* Add fragment to buffer and merge if possible. */
	if (!batadv_frag_skb_buffer(&skb, orig_node_src))
		goto out;

	/* Deliver merged packet to the appropriate handler, if it was
	 * merged
	 */
	if (skb)
		batadv_batman_skb_recv(skb, recv_if->net_dev,
				       &recv_if->batman_adv_ptype, NULL);

	ret = NET_RX_SUCCESS;

out:
	if (orig_node_src)
		batadv_orig_node_free_ref(orig_node_src);

	return ret;
}

int batadv_recv_bcast_packet(struct sk_buff *skb,
			     struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_bcast_packet *bcast_packet;
	struct ethhdr *ethhdr;
	int hdr_size = sizeof(*bcast_packet);
	int ret = NET_RX_DROP;
	int32_t seq_diff;
	uint32_t seqno;

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb, hdr_size)))
		goto out;

	ethhdr = eth_hdr(skb);

	/* packet with broadcast indication but unicast recipient */
	if (!is_broadcast_ether_addr(ethhdr->h_dest))
		goto out;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		goto out;

	/* ignore broadcasts sent by myself */
	if (batadv_is_my_mac(bat_priv, ethhdr->h_source))
		goto out;

	bcast_packet = (struct batadv_bcast_packet *)skb->data;

	/* ignore broadcasts originated by myself */
	if (batadv_is_my_mac(bat_priv, bcast_packet->orig))
		goto out;

	if (bcast_packet->ttl < 2)
		goto out;

	orig_node = batadv_orig_hash_find(bat_priv, bcast_packet->orig);

	if (!orig_node)
		goto out;

	spin_lock_bh(&orig_node->bcast_seqno_lock);

	seqno = ntohl(bcast_packet->seqno);
	/* check whether the packet is a duplicate */
	if (batadv_test_bit(orig_node->bcast_bits, orig_node->last_bcast_seqno,
			    seqno))
		goto spin_unlock;

	seq_diff = seqno - orig_node->last_bcast_seqno;

	/* check whether the packet is old and the host just restarted. */
	if (batadv_window_protected(bat_priv, seq_diff,
				    &orig_node->bcast_seqno_reset))
		goto spin_unlock;

	/* mark broadcast in flood history, update window position
	 * if required.
	 */
	if (batadv_bit_get_packet(bat_priv, orig_node->bcast_bits, seq_diff, 1))
		orig_node->last_bcast_seqno = seqno;

	spin_unlock_bh(&orig_node->bcast_seqno_lock);

	/* check whether this has been sent by another originator before */
	if (batadv_bla_check_bcast_duplist(bat_priv, skb))
		goto out;

	batadv_skb_set_priority(skb, sizeof(struct batadv_bcast_packet));

	/* rebroadcast packet */
	batadv_add_bcast_packet_to_list(bat_priv, skb, 1);

	/* don't hand the broadcast up if it is from an originator
	 * from the same backbone.
	 */
	if (batadv_bla_is_backbone_gw(skb, orig_node, hdr_size))
		goto out;

	if (batadv_dat_snoop_incoming_arp_request(bat_priv, skb, hdr_size))
		goto rx_success;
	if (batadv_dat_snoop_incoming_arp_reply(bat_priv, skb, hdr_size))
		goto rx_success;

	/* broadcast for me */
	batadv_interface_rx(recv_if->soft_iface, skb, recv_if, hdr_size,
			    orig_node);

rx_success:
	ret = NET_RX_SUCCESS;
	goto out;

spin_unlock:
	spin_unlock_bh(&orig_node->bcast_seqno_lock);
out:
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}
