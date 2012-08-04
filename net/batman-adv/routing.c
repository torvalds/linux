/* Copyright (C) 2007-2012 B.A.T.M.A.N. contributors:
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
 */

#include "main.h"
#include "routing.h"
#include "send.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "icmp_socket.h"
#include "translation-table.h"
#include "originator.h"
#include "vis.h"
#include "unicast.h"
#include "bridge_loop_avoidance.h"

static int batadv_route_unicast_packet(struct sk_buff *skb,
				       struct batadv_hard_iface *recv_if);

void batadv_slide_own_bcast_window(struct batadv_hard_iface *hard_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct hlist_node *node;
	struct hlist_head *head;
	struct batadv_orig_node *orig_node;
	unsigned long *word;
	uint32_t i;
	size_t word_index;
	uint8_t *w;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, node, head, hash_entry) {
			spin_lock_bh(&orig_node->ogm_cnt_lock);
			word_index = hard_iface->if_num * BATADV_NUM_WORDS;
			word = &(orig_node->bcast_own[word_index]);

			batadv_bit_get_packet(bat_priv, word, 1, 0);
			w = &orig_node->bcast_own_sum[hard_iface->if_num];
			*w = bitmap_weight(word, BATADV_TQ_LOCAL_WINDOW_SIZE);
			spin_unlock_bh(&orig_node->ogm_cnt_lock);
		}
		rcu_read_unlock();
	}
}

static void _batadv_update_route(struct batadv_priv *bat_priv,
				 struct batadv_orig_node *orig_node,
				 struct batadv_neigh_node *neigh_node)
{
	struct batadv_neigh_node *curr_router;

	curr_router = batadv_orig_node_get_router(orig_node);

	/* route deleted */
	if ((curr_router) && (!neigh_node)) {
		batadv_dbg(BATADV_DBG_ROUTES, bat_priv,
			   "Deleting route towards: %pM\n", orig_node->orig);
		batadv_tt_global_del_orig(bat_priv, orig_node,
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
	rcu_assign_pointer(orig_node->router, neigh_node);
	spin_unlock_bh(&orig_node->neigh_list_lock);

	/* decrease refcount of previous best neighbor */
	if (curr_router)
		batadv_neigh_node_free_ref(curr_router);
}

void batadv_update_route(struct batadv_priv *bat_priv,
			 struct batadv_orig_node *orig_node,
			 struct batadv_neigh_node *neigh_node)
{
	struct batadv_neigh_node *router = NULL;

	if (!orig_node)
		goto out;

	router = batadv_orig_node_get_router(orig_node);

	if (router != neigh_node)
		_batadv_update_route(bat_priv, orig_node, neigh_node);

out:
	if (router)
		batadv_neigh_node_free_ref(router);
}

/* caller must hold the neigh_list_lock */
void batadv_bonding_candidate_del(struct batadv_orig_node *orig_node,
				  struct batadv_neigh_node *neigh_node)
{
	/* this neighbor is not part of our candidate list */
	if (list_empty(&neigh_node->bonding_list))
		goto out;

	list_del_rcu(&neigh_node->bonding_list);
	INIT_LIST_HEAD(&neigh_node->bonding_list);
	batadv_neigh_node_free_ref(neigh_node);
	atomic_dec(&orig_node->bond_candidates);

out:
	return;
}

void batadv_bonding_candidate_add(struct batadv_orig_node *orig_node,
				  struct batadv_neigh_node *neigh_node)
{
	struct hlist_node *node;
	struct batadv_neigh_node *tmp_neigh_node, *router = NULL;
	uint8_t interference_candidate = 0;

	spin_lock_bh(&orig_node->neigh_list_lock);

	/* only consider if it has the same primary address ...  */
	if (!batadv_compare_eth(orig_node->orig,
				neigh_node->orig_node->primary_addr))
		goto candidate_del;

	router = batadv_orig_node_get_router(orig_node);
	if (!router)
		goto candidate_del;

	/* ... and is good enough to be considered */
	if (neigh_node->tq_avg < router->tq_avg - BATADV_BONDING_TQ_THRESHOLD)
		goto candidate_del;

	/* check if we have another candidate with the same mac address or
	 * interface. If we do, we won't select this candidate because of
	 * possible interference.
	 */
	hlist_for_each_entry_rcu(tmp_neigh_node, node,
				 &orig_node->neigh_list, list) {

		if (tmp_neigh_node == neigh_node)
			continue;

		/* we only care if the other candidate is even
		 * considered as candidate.
		 */
		if (list_empty(&tmp_neigh_node->bonding_list))
			continue;

		if ((neigh_node->if_incoming == tmp_neigh_node->if_incoming) ||
		    (batadv_compare_eth(neigh_node->addr,
					tmp_neigh_node->addr))) {
			interference_candidate = 1;
			break;
		}
	}

	/* don't care further if it is an interference candidate */
	if (interference_candidate)
		goto candidate_del;

	/* this neighbor already is part of our candidate list */
	if (!list_empty(&neigh_node->bonding_list))
		goto out;

	if (!atomic_inc_not_zero(&neigh_node->refcount))
		goto out;

	list_add_rcu(&neigh_node->bonding_list, &orig_node->bond_list);
	atomic_inc(&orig_node->bond_candidates);
	goto out;

candidate_del:
	batadv_bonding_candidate_del(orig_node, neigh_node);

out:
	spin_unlock_bh(&orig_node->neigh_list_lock);

	if (router)
		batadv_neigh_node_free_ref(router);
}

/* copy primary address for bonding */
void
batadv_bonding_save_primary(const struct batadv_orig_node *orig_node,
			    struct batadv_orig_node *orig_neigh_node,
			    const struct batadv_ogm_packet *batman_ogm_packet)
{
	if (!(batman_ogm_packet->flags & BATADV_PRIMARIES_FIRST_HOP))
		return;

	memcpy(orig_neigh_node->primary_addr, orig_node->orig, ETH_ALEN);
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

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

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

static int batadv_recv_my_icmp_packet(struct batadv_priv *bat_priv,
				      struct sk_buff *skb, size_t icmp_len)
{
	struct batadv_hard_iface *primary_if = NULL;
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_neigh_node *router = NULL;
	struct batadv_icmp_packet_rr *icmp_packet;
	int ret = NET_RX_DROP;

	icmp_packet = (struct batadv_icmp_packet_rr *)skb->data;

	/* add data to device queue */
	if (icmp_packet->msg_type != BATADV_ECHO_REQUEST) {
		batadv_socket_receive_packet(icmp_packet, icmp_len);
		goto out;
	}

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	/* answer echo request (ping) */
	/* get routing information */
	orig_node = batadv_orig_hash_find(bat_priv, icmp_packet->orig);
	if (!orig_node)
		goto out;

	router = batadv_orig_node_get_router(orig_node);
	if (!router)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, ETH_HLEN) < 0)
		goto out;

	icmp_packet = (struct batadv_icmp_packet_rr *)skb->data;

	memcpy(icmp_packet->dst, icmp_packet->orig, ETH_ALEN);
	memcpy(icmp_packet->orig, primary_if->net_dev->dev_addr, ETH_ALEN);
	icmp_packet->msg_type = BATADV_ECHO_REPLY;
	icmp_packet->header.ttl = BATADV_TTL;

	batadv_send_skb_packet(skb, router->if_incoming, router->addr);
	ret = NET_RX_SUCCESS;

out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	if (router)
		batadv_neigh_node_free_ref(router);
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}

static int batadv_recv_icmp_ttl_exceeded(struct batadv_priv *bat_priv,
					 struct sk_buff *skb)
{
	struct batadv_hard_iface *primary_if = NULL;
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_neigh_node *router = NULL;
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

	router = batadv_orig_node_get_router(orig_node);
	if (!router)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, ETH_HLEN) < 0)
		goto out;

	icmp_packet = (struct batadv_icmp_packet *)skb->data;

	memcpy(icmp_packet->dst, icmp_packet->orig, ETH_ALEN);
	memcpy(icmp_packet->orig, primary_if->net_dev->dev_addr, ETH_ALEN);
	icmp_packet->msg_type = BATADV_TTL_EXCEEDED;
	icmp_packet->header.ttl = BATADV_TTL;

	batadv_send_skb_packet(skb, router->if_incoming, router->addr);
	ret = NET_RX_SUCCESS;

out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	if (router)
		batadv_neigh_node_free_ref(router);
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}


int batadv_recv_icmp_packet(struct sk_buff *skb,
			    struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_icmp_packet_rr *icmp_packet;
	struct ethhdr *ethhdr;
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_neigh_node *router = NULL;
	int hdr_size = sizeof(struct batadv_icmp_packet);
	int ret = NET_RX_DROP;

	/* we truncate all incoming icmp packets if they don't match our size */
	if (skb->len >= sizeof(struct batadv_icmp_packet_rr))
		hdr_size = sizeof(struct batadv_icmp_packet_rr);

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb, hdr_size)))
		goto out;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with unicast indication but broadcast recipient */
	if (is_broadcast_ether_addr(ethhdr->h_dest))
		goto out;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		goto out;

	/* not for me */
	if (!batadv_is_my_mac(ethhdr->h_dest))
		goto out;

	icmp_packet = (struct batadv_icmp_packet_rr *)skb->data;

	/* add record route information if not full */
	if ((hdr_size == sizeof(struct batadv_icmp_packet_rr)) &&
	    (icmp_packet->rr_cur < BATADV_RR_LEN)) {
		memcpy(&(icmp_packet->rr[icmp_packet->rr_cur]),
		       ethhdr->h_dest, ETH_ALEN);
		icmp_packet->rr_cur++;
	}

	/* packet for me */
	if (batadv_is_my_mac(icmp_packet->dst))
		return batadv_recv_my_icmp_packet(bat_priv, skb, hdr_size);

	/* TTL exceeded */
	if (icmp_packet->header.ttl < 2)
		return batadv_recv_icmp_ttl_exceeded(bat_priv, skb);

	/* get routing information */
	orig_node = batadv_orig_hash_find(bat_priv, icmp_packet->dst);
	if (!orig_node)
		goto out;

	router = batadv_orig_node_get_router(orig_node);
	if (!router)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, ETH_HLEN) < 0)
		goto out;

	icmp_packet = (struct batadv_icmp_packet_rr *)skb->data;

	/* decrement ttl */
	icmp_packet->header.ttl--;

	/* route it */
	batadv_send_skb_packet(skb, router->if_incoming, router->addr);
	ret = NET_RX_SUCCESS;

out:
	if (router)
		batadv_neigh_node_free_ref(router);
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}

/* In the bonding case, send the packets in a round
 * robin fashion over the remaining interfaces.
 *
 * This method rotates the bonding list and increases the
 * returned router's refcount.
 */
static struct batadv_neigh_node *
batadv_find_bond_router(struct batadv_orig_node *primary_orig,
			const struct batadv_hard_iface *recv_if)
{
	struct batadv_neigh_node *tmp_neigh_node;
	struct batadv_neigh_node *router = NULL, *first_candidate = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(tmp_neigh_node, &primary_orig->bond_list,
				bonding_list) {
		if (!first_candidate)
			first_candidate = tmp_neigh_node;

		/* recv_if == NULL on the first node. */
		if (tmp_neigh_node->if_incoming == recv_if)
			continue;

		if (!atomic_inc_not_zero(&tmp_neigh_node->refcount))
			continue;

		router = tmp_neigh_node;
		break;
	}

	/* use the first candidate if nothing was found. */
	if (!router && first_candidate &&
	    atomic_inc_not_zero(&first_candidate->refcount))
		router = first_candidate;

	if (!router)
		goto out;

	/* selected should point to the next element
	 * after the current router
	 */
	spin_lock_bh(&primary_orig->neigh_list_lock);
	/* this is a list_move(), which unfortunately
	 * does not exist as rcu version
	 */
	list_del_rcu(&primary_orig->bond_list);
	list_add_rcu(&primary_orig->bond_list,
		     &router->bonding_list);
	spin_unlock_bh(&primary_orig->neigh_list_lock);

out:
	rcu_read_unlock();
	return router;
}

/* Interface Alternating: Use the best of the
 * remaining candidates which are not using
 * this interface.
 *
 * Increases the returned router's refcount
 */
static struct batadv_neigh_node *
batadv_find_ifalter_router(struct batadv_orig_node *primary_orig,
			   const struct batadv_hard_iface *recv_if)
{
	struct batadv_neigh_node *tmp_neigh_node;
	struct batadv_neigh_node *router = NULL, *first_candidate = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(tmp_neigh_node, &primary_orig->bond_list,
				bonding_list) {
		if (!first_candidate)
			first_candidate = tmp_neigh_node;

		/* recv_if == NULL on the first node. */
		if (tmp_neigh_node->if_incoming == recv_if)
			continue;

		if (!atomic_inc_not_zero(&tmp_neigh_node->refcount))
			continue;

		/* if we don't have a router yet
		 * or this one is better, choose it.
		 */
		if ((!router) ||
		    (tmp_neigh_node->tq_avg > router->tq_avg)) {
			/* decrement refcount of
			 * previously selected router
			 */
			if (router)
				batadv_neigh_node_free_ref(router);

			router = tmp_neigh_node;
			atomic_inc_not_zero(&router->refcount);
		}

		batadv_neigh_node_free_ref(tmp_neigh_node);
	}

	/* use the first candidate if nothing was found. */
	if (!router && first_candidate &&
	    atomic_inc_not_zero(&first_candidate->refcount))
		router = first_candidate;

	rcu_read_unlock();
	return router;
}

int batadv_recv_tt_query(struct sk_buff *skb, struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_tt_query_packet *tt_query;
	uint16_t tt_size;
	struct ethhdr *ethhdr;
	char tt_flag;
	size_t packet_size;

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb,
				    sizeof(struct batadv_tt_query_packet))))
		goto out;

	/* I could need to modify it */
	if (skb_cow(skb, sizeof(struct batadv_tt_query_packet)) < 0)
		goto out;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with unicast indication but broadcast recipient */
	if (is_broadcast_ether_addr(ethhdr->h_dest))
		goto out;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		goto out;

	tt_query = (struct batadv_tt_query_packet *)skb->data;

	switch (tt_query->flags & BATADV_TT_QUERY_TYPE_MASK) {
	case BATADV_TT_REQUEST:
		batadv_inc_counter(bat_priv, BATADV_CNT_TT_REQUEST_RX);

		/* If we cannot provide an answer the tt_request is
		 * forwarded
		 */
		if (!batadv_send_tt_response(bat_priv, tt_query)) {
			if (tt_query->flags & BATADV_TT_FULL_TABLE)
				tt_flag = 'F';
			else
				tt_flag = '.';

			batadv_dbg(BATADV_DBG_TT, bat_priv,
				   "Routing TT_REQUEST to %pM [%c]\n",
				   tt_query->dst,
				   tt_flag);
			return batadv_route_unicast_packet(skb, recv_if);
		}
		break;
	case BATADV_TT_RESPONSE:
		batadv_inc_counter(bat_priv, BATADV_CNT_TT_RESPONSE_RX);

		if (batadv_is_my_mac(tt_query->dst)) {
			/* packet needs to be linearized to access the TT
			 * changes
			 */
			if (skb_linearize(skb) < 0)
				goto out;
			/* skb_linearize() possibly changed skb->data */
			tt_query = (struct batadv_tt_query_packet *)skb->data;

			tt_size = batadv_tt_len(ntohs(tt_query->tt_data));

			/* Ensure we have all the claimed data */
			packet_size = sizeof(struct batadv_tt_query_packet);
			packet_size += tt_size;
			if (unlikely(skb_headlen(skb) < packet_size))
				goto out;

			batadv_handle_tt_response(bat_priv, tt_query);
		} else {
			if (tt_query->flags & BATADV_TT_FULL_TABLE)
				tt_flag =  'F';
			else
				tt_flag = '.';
			batadv_dbg(BATADV_DBG_TT, bat_priv,
				   "Routing TT_RESPONSE to %pM [%c]\n",
				   tt_query->dst,
				   tt_flag);
			return batadv_route_unicast_packet(skb, recv_if);
		}
		break;
	}

out:
	/* returning NET_RX_DROP will make the caller function kfree the skb */
	return NET_RX_DROP;
}

int batadv_recv_roam_adv(struct sk_buff *skb, struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_roam_adv_packet *roam_adv_packet;
	struct batadv_orig_node *orig_node;
	struct ethhdr *ethhdr;

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb,
				    sizeof(struct batadv_roam_adv_packet))))
		goto out;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with unicast indication but broadcast recipient */
	if (is_broadcast_ether_addr(ethhdr->h_dest))
		goto out;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		goto out;

	batadv_inc_counter(bat_priv, BATADV_CNT_TT_ROAM_ADV_RX);

	roam_adv_packet = (struct batadv_roam_adv_packet *)skb->data;

	if (!batadv_is_my_mac(roam_adv_packet->dst))
		return batadv_route_unicast_packet(skb, recv_if);

	/* check if it is a backbone gateway. we don't accept
	 * roaming advertisement from it, as it has the same
	 * entries as we have.
	 */
	if (batadv_bla_is_backbone_gw_orig(bat_priv, roam_adv_packet->src))
		goto out;

	orig_node = batadv_orig_hash_find(bat_priv, roam_adv_packet->src);
	if (!orig_node)
		goto out;

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Received ROAMING_ADV from %pM (client %pM)\n",
		   roam_adv_packet->src, roam_adv_packet->client);

	batadv_tt_global_add(bat_priv, orig_node, roam_adv_packet->client,
			     BATADV_TT_CLIENT_ROAM,
			     atomic_read(&orig_node->last_ttvn) + 1);

	/* Roaming phase starts: I have new information but the ttvn has not
	 * been incremented yet. This flag will make me check all the incoming
	 * packets for the correct destination.
	 */
	bat_priv->tt_poss_change = true;

	batadv_orig_node_free_ref(orig_node);
out:
	/* returning NET_RX_DROP will make the caller function kfree the skb */
	return NET_RX_DROP;
}

/* find a suitable router for this originator, and use
 * bonding if possible. increases the found neighbors
 * refcount.
 */
struct batadv_neigh_node *
batadv_find_router(struct batadv_priv *bat_priv,
		   struct batadv_orig_node *orig_node,
		   const struct batadv_hard_iface *recv_if)
{
	struct batadv_orig_node *primary_orig_node;
	struct batadv_orig_node *router_orig;
	struct batadv_neigh_node *router;
	static uint8_t zero_mac[ETH_ALEN] = {0, 0, 0, 0, 0, 0};
	int bonding_enabled;
	uint8_t *primary_addr;

	if (!orig_node)
		return NULL;

	router = batadv_orig_node_get_router(orig_node);
	if (!router)
		goto err;

	/* without bonding, the first node should
	 * always choose the default router.
	 */
	bonding_enabled = atomic_read(&bat_priv->bonding);

	rcu_read_lock();
	/* select default router to output */
	router_orig = router->orig_node;
	if (!router_orig)
		goto err_unlock;

	if ((!recv_if) && (!bonding_enabled))
		goto return_router;

	primary_addr = router_orig->primary_addr;

	/* if we have something in the primary_addr, we can search
	 * for a potential bonding candidate.
	 */
	if (batadv_compare_eth(primary_addr, zero_mac))
		goto return_router;

	/* find the orig_node which has the primary interface. might
	 * even be the same as our router_orig in many cases
	 */
	if (batadv_compare_eth(primary_addr, router_orig->orig)) {
		primary_orig_node = router_orig;
	} else {
		primary_orig_node = batadv_orig_hash_find(bat_priv,
							  primary_addr);
		if (!primary_orig_node)
			goto return_router;

		batadv_orig_node_free_ref(primary_orig_node);
	}

	/* with less than 2 candidates, we can't do any
	 * bonding and prefer the original router.
	 */
	if (atomic_read(&primary_orig_node->bond_candidates) < 2)
		goto return_router;

	/* all nodes between should choose a candidate which
	 * is is not on the interface where the packet came
	 * in.
	 */
	batadv_neigh_node_free_ref(router);

	if (bonding_enabled)
		router = batadv_find_bond_router(primary_orig_node, recv_if);
	else
		router = batadv_find_ifalter_router(primary_orig_node, recv_if);

return_router:
	if (router && router->if_incoming->if_status != BATADV_IF_ACTIVE)
		goto err_unlock;

	rcu_read_unlock();
	return router;
err_unlock:
	rcu_read_unlock();
err:
	if (router)
		batadv_neigh_node_free_ref(router);
	return NULL;
}

static int batadv_check_unicast_packet(struct sk_buff *skb, int hdr_size)
{
	struct ethhdr *ethhdr;

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb, hdr_size)))
		return -1;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with unicast indication but broadcast recipient */
	if (is_broadcast_ether_addr(ethhdr->h_dest))
		return -1;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		return -1;

	/* not for me */
	if (!batadv_is_my_mac(ethhdr->h_dest))
		return -1;

	return 0;
}

static int batadv_route_unicast_packet(struct sk_buff *skb,
				       struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_neigh_node *neigh_node = NULL;
	struct batadv_unicast_packet *unicast_packet;
	struct ethhdr *ethhdr = (struct ethhdr *)skb_mac_header(skb);
	int ret = NET_RX_DROP;
	struct sk_buff *new_skb;

	unicast_packet = (struct batadv_unicast_packet *)skb->data;

	/* TTL exceeded */
	if (unicast_packet->header.ttl < 2) {
		pr_debug("Warning - can't forward unicast packet from %pM to %pM: ttl exceeded\n",
			 ethhdr->h_source, unicast_packet->dest);
		goto out;
	}

	/* get routing information */
	orig_node = batadv_orig_hash_find(bat_priv, unicast_packet->dest);

	if (!orig_node)
		goto out;

	/* find_router() increases neigh_nodes refcount if found. */
	neigh_node = batadv_find_router(bat_priv, orig_node, recv_if);

	if (!neigh_node)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, ETH_HLEN) < 0)
		goto out;

	unicast_packet = (struct batadv_unicast_packet *)skb->data;

	if (unicast_packet->header.packet_type == BATADV_UNICAST &&
	    atomic_read(&bat_priv->fragmentation) &&
	    skb->len > neigh_node->if_incoming->net_dev->mtu) {
		ret = batadv_frag_send_skb(skb, bat_priv,
					   neigh_node->if_incoming,
					   neigh_node->addr);
		goto out;
	}

	if (unicast_packet->header.packet_type == BATADV_UNICAST_FRAG &&
	    batadv_frag_can_reassemble(skb,
				       neigh_node->if_incoming->net_dev->mtu)) {

		ret = batadv_frag_reassemble_skb(skb, bat_priv, &new_skb);

		if (ret == NET_RX_DROP)
			goto out;

		/* packet was buffered for late merge */
		if (!new_skb) {
			ret = NET_RX_SUCCESS;
			goto out;
		}

		skb = new_skb;
		unicast_packet = (struct batadv_unicast_packet *)skb->data;
	}

	/* decrement ttl */
	unicast_packet->header.ttl--;

	/* Update stats counter */
	batadv_inc_counter(bat_priv, BATADV_CNT_FORWARD);
	batadv_add_counter(bat_priv, BATADV_CNT_FORWARD_BYTES,
			   skb->len + ETH_HLEN);

	/* route it */
	batadv_send_skb_packet(skb, neigh_node->if_incoming, neigh_node->addr);
	ret = NET_RX_SUCCESS;

out:
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}

static int batadv_check_unicast_ttvn(struct batadv_priv *bat_priv,
				     struct sk_buff *skb) {
	uint8_t curr_ttvn;
	struct batadv_orig_node *orig_node;
	struct ethhdr *ethhdr;
	struct batadv_hard_iface *primary_if;
	struct batadv_unicast_packet *unicast_packet;
	bool tt_poss_change;
	int is_old_ttvn;

	/* I could need to modify it */
	if (skb_cow(skb, sizeof(struct batadv_unicast_packet)) < 0)
		return 0;

	unicast_packet = (struct batadv_unicast_packet *)skb->data;

	if (batadv_is_my_mac(unicast_packet->dest)) {
		tt_poss_change = bat_priv->tt_poss_change;
		curr_ttvn = (uint8_t)atomic_read(&bat_priv->ttvn);
	} else {
		orig_node = batadv_orig_hash_find(bat_priv,
						  unicast_packet->dest);

		if (!orig_node)
			return 0;

		curr_ttvn = (uint8_t)atomic_read(&orig_node->last_ttvn);
		tt_poss_change = orig_node->tt_poss_change;
		batadv_orig_node_free_ref(orig_node);
	}

	/* Check whether I have to reroute the packet */
	is_old_ttvn = batadv_seq_before(unicast_packet->ttvn, curr_ttvn);
	if (is_old_ttvn || tt_poss_change) {
		/* check if there is enough data before accessing it */
		if (pskb_may_pull(skb, sizeof(struct batadv_unicast_packet) +
				  ETH_HLEN) < 0)
			return 0;

		ethhdr = (struct ethhdr *)(skb->data + sizeof(*unicast_packet));

		/* we don't have an updated route for this client, so we should
		 * not try to reroute the packet!!
		 */
		if (batadv_tt_global_client_is_roaming(bat_priv,
						       ethhdr->h_dest))
			return 1;

		orig_node = batadv_transtable_search(bat_priv, NULL,
						     ethhdr->h_dest);

		if (!orig_node) {
			if (!batadv_is_my_client(bat_priv, ethhdr->h_dest))
				return 0;
			primary_if = batadv_primary_if_get_selected(bat_priv);
			if (!primary_if)
				return 0;
			memcpy(unicast_packet->dest,
			       primary_if->net_dev->dev_addr, ETH_ALEN);
			batadv_hardif_free_ref(primary_if);
		} else {
			memcpy(unicast_packet->dest, orig_node->orig,
			       ETH_ALEN);
			curr_ttvn = (uint8_t)
				atomic_read(&orig_node->last_ttvn);
			batadv_orig_node_free_ref(orig_node);
		}

		batadv_dbg(BATADV_DBG_ROUTES, bat_priv,
			   "TTVN mismatch (old_ttvn %u new_ttvn %u)! Rerouting unicast packet (for %pM) to %pM\n",
			   unicast_packet->ttvn, curr_ttvn, ethhdr->h_dest,
			   unicast_packet->dest);

		unicast_packet->ttvn = curr_ttvn;
	}
	return 1;
}

int batadv_recv_unicast_packet(struct sk_buff *skb,
			       struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_unicast_packet *unicast_packet;
	int hdr_size = sizeof(*unicast_packet);

	if (batadv_check_unicast_packet(skb, hdr_size) < 0)
		return NET_RX_DROP;

	if (!batadv_check_unicast_ttvn(bat_priv, skb))
		return NET_RX_DROP;

	unicast_packet = (struct batadv_unicast_packet *)skb->data;

	/* packet for me */
	if (batadv_is_my_mac(unicast_packet->dest)) {
		batadv_interface_rx(recv_if->soft_iface, skb, recv_if,
				    hdr_size);
		return NET_RX_SUCCESS;
	}

	return batadv_route_unicast_packet(skb, recv_if);
}

int batadv_recv_ucast_frag_packet(struct sk_buff *skb,
				  struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_unicast_frag_packet *unicast_packet;
	int hdr_size = sizeof(*unicast_packet);
	struct sk_buff *new_skb = NULL;
	int ret;

	if (batadv_check_unicast_packet(skb, hdr_size) < 0)
		return NET_RX_DROP;

	if (!batadv_check_unicast_ttvn(bat_priv, skb))
		return NET_RX_DROP;

	unicast_packet = (struct batadv_unicast_frag_packet *)skb->data;

	/* packet for me */
	if (batadv_is_my_mac(unicast_packet->dest)) {

		ret = batadv_frag_reassemble_skb(skb, bat_priv, &new_skb);

		if (ret == NET_RX_DROP)
			return NET_RX_DROP;

		/* packet was buffered for late merge */
		if (!new_skb)
			return NET_RX_SUCCESS;

		batadv_interface_rx(recv_if->soft_iface, new_skb, recv_if,
				    sizeof(struct batadv_unicast_packet));
		return NET_RX_SUCCESS;
	}

	return batadv_route_unicast_packet(skb, recv_if);
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

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb, hdr_size)))
		goto out;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with broadcast indication but unicast recipient */
	if (!is_broadcast_ether_addr(ethhdr->h_dest))
		goto out;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		goto out;

	/* ignore broadcasts sent by myself */
	if (batadv_is_my_mac(ethhdr->h_source))
		goto out;

	bcast_packet = (struct batadv_bcast_packet *)skb->data;

	/* ignore broadcasts originated by myself */
	if (batadv_is_my_mac(bcast_packet->orig))
		goto out;

	if (bcast_packet->header.ttl < 2)
		goto out;

	orig_node = batadv_orig_hash_find(bat_priv, bcast_packet->orig);

	if (!orig_node)
		goto out;

	spin_lock_bh(&orig_node->bcast_seqno_lock);

	/* check whether the packet is a duplicate */
	if (batadv_test_bit(orig_node->bcast_bits, orig_node->last_bcast_seqno,
			    ntohl(bcast_packet->seqno)))
		goto spin_unlock;

	seq_diff = ntohl(bcast_packet->seqno) - orig_node->last_bcast_seqno;

	/* check whether the packet is old and the host just restarted. */
	if (batadv_window_protected(bat_priv, seq_diff,
				    &orig_node->bcast_seqno_reset))
		goto spin_unlock;

	/* mark broadcast in flood history, update window position
	 * if required.
	 */
	if (batadv_bit_get_packet(bat_priv, orig_node->bcast_bits, seq_diff, 1))
		orig_node->last_bcast_seqno = ntohl(bcast_packet->seqno);

	spin_unlock_bh(&orig_node->bcast_seqno_lock);

	/* check whether this has been sent by another originator before */
	if (batadv_bla_check_bcast_duplist(bat_priv, bcast_packet, hdr_size))
		goto out;

	/* rebroadcast packet */
	batadv_add_bcast_packet_to_list(bat_priv, skb, 1);

	/* don't hand the broadcast up if it is from an originator
	 * from the same backbone.
	 */
	if (batadv_bla_is_backbone_gw(skb, orig_node, hdr_size))
		goto out;

	/* broadcast for me */
	batadv_interface_rx(recv_if->soft_iface, skb, recv_if, hdr_size);
	ret = NET_RX_SUCCESS;
	goto out;

spin_unlock:
	spin_unlock_bh(&orig_node->bcast_seqno_lock);
out:
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}

int batadv_recv_vis_packet(struct sk_buff *skb,
			   struct batadv_hard_iface *recv_if)
{
	struct batadv_vis_packet *vis_packet;
	struct ethhdr *ethhdr;
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	int hdr_size = sizeof(*vis_packet);

	/* keep skb linear */
	if (skb_linearize(skb) < 0)
		return NET_RX_DROP;

	if (unlikely(!pskb_may_pull(skb, hdr_size)))
		return NET_RX_DROP;

	vis_packet = (struct batadv_vis_packet *)skb->data;
	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* not for me */
	if (!batadv_is_my_mac(ethhdr->h_dest))
		return NET_RX_DROP;

	/* ignore own packets */
	if (batadv_is_my_mac(vis_packet->vis_orig))
		return NET_RX_DROP;

	if (batadv_is_my_mac(vis_packet->sender_orig))
		return NET_RX_DROP;

	switch (vis_packet->vis_type) {
	case BATADV_VIS_TYPE_SERVER_SYNC:
		batadv_receive_server_sync_packet(bat_priv, vis_packet,
						  skb_headlen(skb));
		break;

	case BATADV_VIS_TYPE_CLIENT_UPDATE:
		batadv_receive_client_update_packet(bat_priv, vis_packet,
						    skb_headlen(skb));
		break;

	default:	/* ignore unknown packet */
		break;
	}

	/* We take a copy of the data in the packet, so we should
	 * always free the skbuf.
	 */
	return NET_RX_DROP;
}
