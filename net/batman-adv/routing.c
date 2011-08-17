/*
 * Copyright (C) 2007-2011 B.A.T.M.A.N. contributors:
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
#include "routing.h"
#include "send.h"
#include "hash.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "icmp_socket.h"
#include "translation-table.h"
#include "originator.h"
#include "ring_buffer.h"
#include "vis.h"
#include "aggregation.h"
#include "gateway_common.h"
#include "gateway_client.h"
#include "unicast.h"

void slide_own_bcast_window(struct hard_iface *hard_iface)
{
	struct bat_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct hashtable_t *hash = bat_priv->orig_hash;
	struct hlist_node *node;
	struct hlist_head *head;
	struct orig_node *orig_node;
	unsigned long *word;
	int i;
	size_t word_index;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, node, head, hash_entry) {
			spin_lock_bh(&orig_node->ogm_cnt_lock);
			word_index = hard_iface->if_num * NUM_WORDS;
			word = &(orig_node->bcast_own[word_index]);

			bit_get_packet(bat_priv, word, 1, 0);
			orig_node->bcast_own_sum[hard_iface->if_num] =
				bit_packet_count(word);
			spin_unlock_bh(&orig_node->ogm_cnt_lock);
		}
		rcu_read_unlock();
	}
}

static void update_transtable(struct bat_priv *bat_priv,
			      struct orig_node *orig_node,
			      const unsigned char *tt_buff,
			      uint8_t tt_num_changes, uint8_t ttvn,
			      uint16_t tt_crc)
{
	uint8_t orig_ttvn = (uint8_t)atomic_read(&orig_node->last_ttvn);
	bool full_table = true;

	/* the ttvn increased by one -> we can apply the attached changes */
	if (ttvn - orig_ttvn == 1) {
		/* the OGM could not contain the changes because they were too
		 * many to fit in one frame or because they have already been
		 * sent TT_OGM_APPEND_MAX times. In this case send a tt
		 * request */
		if (!tt_num_changes) {
			full_table = false;
			goto request_table;
		}

		tt_update_changes(bat_priv, orig_node, tt_num_changes, ttvn,
				  (struct tt_change *)tt_buff);

		/* Even if we received the crc into the OGM, we prefer
		 * to recompute it to spot any possible inconsistency
		 * in the global table */
		orig_node->tt_crc = tt_global_crc(bat_priv, orig_node);

		/* The ttvn alone is not enough to guarantee consistency
		 * because a single value could repesent different states
		 * (due to the wrap around). Thus a node has to check whether
		 * the resulting table (after applying the changes) is still
		 * consistent or not. E.g. a node could disconnect while its
		 * ttvn is X and reconnect on ttvn = X + TTVN_MAX: in this case
		 * checking the CRC value is mandatory to detect the
		 * inconsistency */
		if (orig_node->tt_crc != tt_crc)
			goto request_table;

		/* Roaming phase is over: tables are in sync again. I can
		 * unset the flag */
		orig_node->tt_poss_change = false;
	} else {
		/* if we missed more than one change or our tables are not
		 * in sync anymore -> request fresh tt data */
		if (ttvn != orig_ttvn || orig_node->tt_crc != tt_crc) {
request_table:
			bat_dbg(DBG_TT, bat_priv, "TT inconsistency for %pM. "
				"Need to retrieve the correct information "
				"(ttvn: %u last_ttvn: %u crc: %u last_crc: "
				"%u num_changes: %u)\n", orig_node->orig, ttvn,
				orig_ttvn, tt_crc, orig_node->tt_crc,
				tt_num_changes);
			send_tt_request(bat_priv, orig_node, ttvn, tt_crc,
					full_table);
			return;
		}
	}
}

static void update_route(struct bat_priv *bat_priv,
			 struct orig_node *orig_node,
			 struct neigh_node *neigh_node)
{
	struct neigh_node *curr_router;

	curr_router = orig_node_get_router(orig_node);

	/* route deleted */
	if ((curr_router) && (!neigh_node)) {
		bat_dbg(DBG_ROUTES, bat_priv, "Deleting route towards: %pM\n",
			orig_node->orig);
		tt_global_del_orig(bat_priv, orig_node,
				    "Deleted route towards originator");

	/* route added */
	} else if ((!curr_router) && (neigh_node)) {

		bat_dbg(DBG_ROUTES, bat_priv,
			"Adding route towards: %pM (via %pM)\n",
			orig_node->orig, neigh_node->addr);
	/* route changed */
	} else if (neigh_node && curr_router) {
		bat_dbg(DBG_ROUTES, bat_priv,
			"Changing route towards: %pM "
			"(now via %pM - was via %pM)\n",
			orig_node->orig, neigh_node->addr,
			curr_router->addr);
	}

	if (curr_router)
		neigh_node_free_ref(curr_router);

	/* increase refcount of new best neighbor */
	if (neigh_node && !atomic_inc_not_zero(&neigh_node->refcount))
		neigh_node = NULL;

	spin_lock_bh(&orig_node->neigh_list_lock);
	rcu_assign_pointer(orig_node->router, neigh_node);
	spin_unlock_bh(&orig_node->neigh_list_lock);

	/* decrease refcount of previous best neighbor */
	if (curr_router)
		neigh_node_free_ref(curr_router);
}

void update_routes(struct bat_priv *bat_priv, struct orig_node *orig_node,
		   struct neigh_node *neigh_node)
{
	struct neigh_node *router = NULL;

	if (!orig_node)
		goto out;

	router = orig_node_get_router(orig_node);

	if (router != neigh_node)
		update_route(bat_priv, orig_node, neigh_node);

out:
	if (router)
		neigh_node_free_ref(router);
}

static int is_bidirectional_neigh(struct orig_node *orig_node,
				struct orig_node *orig_neigh_node,
				struct batman_packet *batman_packet,
				struct hard_iface *if_incoming)
{
	struct bat_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct neigh_node *neigh_node = NULL, *tmp_neigh_node;
	struct hlist_node *node;
	uint8_t total_count;
	uint8_t orig_eq_count, neigh_rq_count, tq_own;
	int tq_asym_penalty, ret = 0;

	/* find corresponding one hop neighbor */
	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node, node,
				 &orig_neigh_node->neigh_list, list) {

		if (!compare_eth(tmp_neigh_node->addr, orig_neigh_node->orig))
			continue;

		if (tmp_neigh_node->if_incoming != if_incoming)
			continue;

		if (!atomic_inc_not_zero(&tmp_neigh_node->refcount))
			continue;

		neigh_node = tmp_neigh_node;
		break;
	}
	rcu_read_unlock();

	if (!neigh_node)
		neigh_node = create_neighbor(orig_neigh_node,
					     orig_neigh_node,
					     orig_neigh_node->orig,
					     if_incoming);

	if (!neigh_node)
		goto out;

	/* if orig_node is direct neighbour update neigh_node last_valid */
	if (orig_node == orig_neigh_node)
		neigh_node->last_valid = jiffies;

	orig_node->last_valid = jiffies;

	/* find packet count of corresponding one hop neighbor */
	spin_lock_bh(&orig_node->ogm_cnt_lock);
	orig_eq_count = orig_neigh_node->bcast_own_sum[if_incoming->if_num];
	neigh_rq_count = neigh_node->real_packet_count;
	spin_unlock_bh(&orig_node->ogm_cnt_lock);

	/* pay attention to not get a value bigger than 100 % */
	total_count = (orig_eq_count > neigh_rq_count ?
		       neigh_rq_count : orig_eq_count);

	/* if we have too few packets (too less data) we set tq_own to zero */
	/* if we receive too few packets it is not considered bidirectional */
	if ((total_count < TQ_LOCAL_BIDRECT_SEND_MINIMUM) ||
	    (neigh_rq_count < TQ_LOCAL_BIDRECT_RECV_MINIMUM))
		tq_own = 0;
	else
		/* neigh_node->real_packet_count is never zero as we
		 * only purge old information when getting new
		 * information */
		tq_own = (TQ_MAX_VALUE * total_count) /	neigh_rq_count;

	/*
	 * 1 - ((1-x) ** 3), normalized to TQ_MAX_VALUE this does
	 * affect the nearly-symmetric links only a little, but
	 * punishes asymmetric links more.  This will give a value
	 * between 0 and TQ_MAX_VALUE
	 */
	tq_asym_penalty = TQ_MAX_VALUE - (TQ_MAX_VALUE *
				(TQ_LOCAL_WINDOW_SIZE - neigh_rq_count) *
				(TQ_LOCAL_WINDOW_SIZE - neigh_rq_count) *
				(TQ_LOCAL_WINDOW_SIZE - neigh_rq_count)) /
					(TQ_LOCAL_WINDOW_SIZE *
					 TQ_LOCAL_WINDOW_SIZE *
					 TQ_LOCAL_WINDOW_SIZE);

	batman_packet->tq = ((batman_packet->tq * tq_own * tq_asym_penalty) /
						(TQ_MAX_VALUE * TQ_MAX_VALUE));

	bat_dbg(DBG_BATMAN, bat_priv,
		"bidirectional: "
		"orig = %-15pM neigh = %-15pM => own_bcast = %2i, "
		"real recv = %2i, local tq: %3i, asym_penalty: %3i, "
		"total tq: %3i\n",
		orig_node->orig, orig_neigh_node->orig, total_count,
		neigh_rq_count, tq_own,	tq_asym_penalty, batman_packet->tq);

	/* if link has the minimum required transmission quality
	 * consider it bidirectional */
	if (batman_packet->tq >= TQ_TOTAL_BIDRECT_LIMIT)
		ret = 1;

out:
	if (neigh_node)
		neigh_node_free_ref(neigh_node);
	return ret;
}

/* caller must hold the neigh_list_lock */
void bonding_candidate_del(struct orig_node *orig_node,
			   struct neigh_node *neigh_node)
{
	/* this neighbor is not part of our candidate list */
	if (list_empty(&neigh_node->bonding_list))
		goto out;

	list_del_rcu(&neigh_node->bonding_list);
	INIT_LIST_HEAD(&neigh_node->bonding_list);
	neigh_node_free_ref(neigh_node);
	atomic_dec(&orig_node->bond_candidates);

out:
	return;
}

static void bonding_candidate_add(struct orig_node *orig_node,
				  struct neigh_node *neigh_node)
{
	struct hlist_node *node;
	struct neigh_node *tmp_neigh_node, *router = NULL;
	uint8_t interference_candidate = 0;

	spin_lock_bh(&orig_node->neigh_list_lock);

	/* only consider if it has the same primary address ...  */
	if (!compare_eth(orig_node->orig,
			 neigh_node->orig_node->primary_addr))
		goto candidate_del;

	router = orig_node_get_router(orig_node);
	if (!router)
		goto candidate_del;

	/* ... and is good enough to be considered */
	if (neigh_node->tq_avg < router->tq_avg - BONDING_TQ_THRESHOLD)
		goto candidate_del;

	/**
	 * check if we have another candidate with the same mac address or
	 * interface. If we do, we won't select this candidate because of
	 * possible interference.
	 */
	hlist_for_each_entry_rcu(tmp_neigh_node, node,
				 &orig_node->neigh_list, list) {

		if (tmp_neigh_node == neigh_node)
			continue;

		/* we only care if the other candidate is even
		* considered as candidate. */
		if (list_empty(&tmp_neigh_node->bonding_list))
			continue;

		if ((neigh_node->if_incoming == tmp_neigh_node->if_incoming) ||
		    (compare_eth(neigh_node->addr, tmp_neigh_node->addr))) {
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
	bonding_candidate_del(orig_node, neigh_node);

out:
	spin_unlock_bh(&orig_node->neigh_list_lock);

	if (router)
		neigh_node_free_ref(router);
}

/* copy primary address for bonding */
static void bonding_save_primary(const struct orig_node *orig_node,
				 struct orig_node *orig_neigh_node,
				 const struct batman_packet *batman_packet)
{
	if (!(batman_packet->flags & PRIMARIES_FIRST_HOP))
		return;

	memcpy(orig_neigh_node->primary_addr, orig_node->orig, ETH_ALEN);
}

static void update_orig(struct bat_priv *bat_priv, struct orig_node *orig_node,
			const struct ethhdr *ethhdr,
			const struct batman_packet *batman_packet,
			struct hard_iface *if_incoming,
			const unsigned char *tt_buff, int is_duplicate)
{
	struct neigh_node *neigh_node = NULL, *tmp_neigh_node = NULL;
	struct neigh_node *router = NULL;
	struct orig_node *orig_node_tmp;
	struct hlist_node *node;
	uint8_t bcast_own_sum_orig, bcast_own_sum_neigh;

	bat_dbg(DBG_BATMAN, bat_priv, "update_originator(): "
		"Searching and updating originator entry of received packet\n");

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node, node,
				 &orig_node->neigh_list, list) {
		if (compare_eth(tmp_neigh_node->addr, ethhdr->h_source) &&
		    (tmp_neigh_node->if_incoming == if_incoming) &&
		     atomic_inc_not_zero(&tmp_neigh_node->refcount)) {
			if (neigh_node)
				neigh_node_free_ref(neigh_node);
			neigh_node = tmp_neigh_node;
			continue;
		}

		if (is_duplicate)
			continue;

		spin_lock_bh(&tmp_neigh_node->tq_lock);
		ring_buffer_set(tmp_neigh_node->tq_recv,
				&tmp_neigh_node->tq_index, 0);
		tmp_neigh_node->tq_avg =
			ring_buffer_avg(tmp_neigh_node->tq_recv);
		spin_unlock_bh(&tmp_neigh_node->tq_lock);
	}

	if (!neigh_node) {
		struct orig_node *orig_tmp;

		orig_tmp = get_orig_node(bat_priv, ethhdr->h_source);
		if (!orig_tmp)
			goto unlock;

		neigh_node = create_neighbor(orig_node, orig_tmp,
					     ethhdr->h_source, if_incoming);

		orig_node_free_ref(orig_tmp);
		if (!neigh_node)
			goto unlock;
	} else
		bat_dbg(DBG_BATMAN, bat_priv,
			"Updating existing last-hop neighbor of originator\n");

	rcu_read_unlock();

	orig_node->flags = batman_packet->flags;
	neigh_node->last_valid = jiffies;

	spin_lock_bh(&neigh_node->tq_lock);
	ring_buffer_set(neigh_node->tq_recv,
			&neigh_node->tq_index,
			batman_packet->tq);
	neigh_node->tq_avg = ring_buffer_avg(neigh_node->tq_recv);
	spin_unlock_bh(&neigh_node->tq_lock);

	if (!is_duplicate) {
		orig_node->last_ttl = batman_packet->ttl;
		neigh_node->last_ttl = batman_packet->ttl;
	}

	bonding_candidate_add(orig_node, neigh_node);

	/* if this neighbor already is our next hop there is nothing
	 * to change */
	router = orig_node_get_router(orig_node);
	if (router == neigh_node)
		goto update_tt;

	/* if this neighbor does not offer a better TQ we won't consider it */
	if (router && (router->tq_avg > neigh_node->tq_avg))
		goto update_tt;

	/* if the TQ is the same and the link not more symetric we
	 * won't consider it either */
	if (router && (neigh_node->tq_avg == router->tq_avg)) {
		orig_node_tmp = router->orig_node;
		spin_lock_bh(&orig_node_tmp->ogm_cnt_lock);
		bcast_own_sum_orig =
			orig_node_tmp->bcast_own_sum[if_incoming->if_num];
		spin_unlock_bh(&orig_node_tmp->ogm_cnt_lock);

		orig_node_tmp = neigh_node->orig_node;
		spin_lock_bh(&orig_node_tmp->ogm_cnt_lock);
		bcast_own_sum_neigh =
			orig_node_tmp->bcast_own_sum[if_incoming->if_num];
		spin_unlock_bh(&orig_node_tmp->ogm_cnt_lock);

		if (bcast_own_sum_orig >= bcast_own_sum_neigh)
			goto update_tt;
	}

	update_routes(bat_priv, orig_node, neigh_node);

update_tt:
	/* I have to check for transtable changes only if the OGM has been
	 * sent through a primary interface */
	if (((batman_packet->orig != ethhdr->h_source) &&
				(batman_packet->ttl > 2)) ||
				(batman_packet->flags & PRIMARIES_FIRST_HOP))
		update_transtable(bat_priv, orig_node, tt_buff,
				  batman_packet->tt_num_changes,
				  batman_packet->ttvn,
				  batman_packet->tt_crc);

	if (orig_node->gw_flags != batman_packet->gw_flags)
		gw_node_update(bat_priv, orig_node, batman_packet->gw_flags);

	orig_node->gw_flags = batman_packet->gw_flags;

	/* restart gateway selection if fast or late switching was enabled */
	if ((orig_node->gw_flags) &&
	    (atomic_read(&bat_priv->gw_mode) == GW_MODE_CLIENT) &&
	    (atomic_read(&bat_priv->gw_sel_class) > 2))
		gw_check_election(bat_priv, orig_node);

	goto out;

unlock:
	rcu_read_unlock();
out:
	if (neigh_node)
		neigh_node_free_ref(neigh_node);
	if (router)
		neigh_node_free_ref(router);
}

/* checks whether the host restarted and is in the protection time.
 * returns:
 *  0 if the packet is to be accepted
 *  1 if the packet is to be ignored.
 */
static int window_protected(struct bat_priv *bat_priv,
			    int32_t seq_num_diff,
			    unsigned long *last_reset)
{
	if ((seq_num_diff <= -TQ_LOCAL_WINDOW_SIZE)
		|| (seq_num_diff >= EXPECTED_SEQNO_RANGE)) {
		if (time_after(jiffies, *last_reset +
			msecs_to_jiffies(RESET_PROTECTION_MS))) {

			*last_reset = jiffies;
			bat_dbg(DBG_BATMAN, bat_priv,
				"old packet received, start protection\n");

			return 0;
		} else
			return 1;
	}
	return 0;
}

/* processes a batman packet for all interfaces, adjusts the sequence number and
 * finds out whether it is a duplicate.
 * returns:
 *   1 the packet is a duplicate
 *   0 the packet has not yet been received
 *  -1 the packet is old and has been received while the seqno window
 *     was protected. Caller should drop it.
 */
static int count_real_packets(const struct ethhdr *ethhdr,
			       const struct batman_packet *batman_packet,
			       const struct hard_iface *if_incoming)
{
	struct bat_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct orig_node *orig_node;
	struct neigh_node *tmp_neigh_node;
	struct hlist_node *node;
	int is_duplicate = 0;
	int32_t seq_diff;
	int need_update = 0;
	int set_mark, ret = -1;

	orig_node = get_orig_node(bat_priv, batman_packet->orig);
	if (!orig_node)
		return 0;

	spin_lock_bh(&orig_node->ogm_cnt_lock);
	seq_diff = batman_packet->seqno - orig_node->last_real_seqno;

	/* signalize caller that the packet is to be dropped. */
	if (window_protected(bat_priv, seq_diff,
			     &orig_node->batman_seqno_reset))
		goto out;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node, node,
				 &orig_node->neigh_list, list) {

		is_duplicate |= get_bit_status(tmp_neigh_node->real_bits,
					       orig_node->last_real_seqno,
					       batman_packet->seqno);

		if (compare_eth(tmp_neigh_node->addr, ethhdr->h_source) &&
		    (tmp_neigh_node->if_incoming == if_incoming))
			set_mark = 1;
		else
			set_mark = 0;

		/* if the window moved, set the update flag. */
		need_update |= bit_get_packet(bat_priv,
					      tmp_neigh_node->real_bits,
					      seq_diff, set_mark);

		tmp_neigh_node->real_packet_count =
			bit_packet_count(tmp_neigh_node->real_bits);
	}
	rcu_read_unlock();

	if (need_update) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"updating last_seqno: old %d, new %d\n",
			orig_node->last_real_seqno, batman_packet->seqno);
		orig_node->last_real_seqno = batman_packet->seqno;
	}

	ret = is_duplicate;

out:
	spin_unlock_bh(&orig_node->ogm_cnt_lock);
	orig_node_free_ref(orig_node);
	return ret;
}

void receive_bat_packet(const struct ethhdr *ethhdr,
			struct batman_packet *batman_packet,
			const unsigned char *tt_buff,
			struct hard_iface *if_incoming)
{
	struct bat_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct hard_iface *hard_iface;
	struct orig_node *orig_neigh_node, *orig_node;
	struct neigh_node *router = NULL, *router_router = NULL;
	struct neigh_node *orig_neigh_router = NULL;
	int has_directlink_flag;
	int is_my_addr = 0, is_my_orig = 0, is_my_oldorig = 0;
	int is_broadcast = 0, is_bidirectional, is_single_hop_neigh;
	int is_duplicate;
	uint32_t if_incoming_seqno;

	/* Silently drop when the batman packet is actually not a
	 * correct packet.
	 *
	 * This might happen if a packet is padded (e.g. Ethernet has a
	 * minimum frame length of 64 byte) and the aggregation interprets
	 * it as an additional length.
	 *
	 * TODO: A more sane solution would be to have a bit in the
	 * batman_packet to detect whether the packet is the last
	 * packet in an aggregation.  Here we expect that the padding
	 * is always zero (or not 0x01)
	 */
	if (batman_packet->packet_type != BAT_PACKET)
		return;

	/* could be changed by schedule_own_packet() */
	if_incoming_seqno = atomic_read(&if_incoming->seqno);

	has_directlink_flag = (batman_packet->flags & DIRECTLINK ? 1 : 0);

	is_single_hop_neigh = (compare_eth(ethhdr->h_source,
					   batman_packet->orig) ? 1 : 0);

	bat_dbg(DBG_BATMAN, bat_priv,
		"Received BATMAN packet via NB: %pM, IF: %s [%pM] "
		"(from OG: %pM, via prev OG: %pM, seqno %d, ttvn %u, "
		"crc %u, changes %u, td %d, TTL %d, V %d, IDF %d)\n",
		ethhdr->h_source, if_incoming->net_dev->name,
		if_incoming->net_dev->dev_addr, batman_packet->orig,
		batman_packet->prev_sender, batman_packet->seqno,
		batman_packet->ttvn, batman_packet->tt_crc,
		batman_packet->tt_num_changes, batman_packet->tq,
		batman_packet->ttl, batman_packet->version,
		has_directlink_flag);

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &hardif_list, list) {
		if (hard_iface->if_status != IF_ACTIVE)
			continue;

		if (hard_iface->soft_iface != if_incoming->soft_iface)
			continue;

		if (compare_eth(ethhdr->h_source,
				hard_iface->net_dev->dev_addr))
			is_my_addr = 1;

		if (compare_eth(batman_packet->orig,
				hard_iface->net_dev->dev_addr))
			is_my_orig = 1;

		if (compare_eth(batman_packet->prev_sender,
				hard_iface->net_dev->dev_addr))
			is_my_oldorig = 1;

		if (is_broadcast_ether_addr(ethhdr->h_source))
			is_broadcast = 1;
	}
	rcu_read_unlock();

	if (batman_packet->version != COMPAT_VERSION) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: incompatible batman version (%i)\n",
			batman_packet->version);
		return;
	}

	if (is_my_addr) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: received my own broadcast (sender: %pM"
			")\n",
			ethhdr->h_source);
		return;
	}

	if (is_broadcast) {
		bat_dbg(DBG_BATMAN, bat_priv, "Drop packet: "
		"ignoring all packets with broadcast source addr (sender: %pM"
		")\n", ethhdr->h_source);
		return;
	}

	if (is_my_orig) {
		unsigned long *word;
		int offset;

		orig_neigh_node = get_orig_node(bat_priv, ethhdr->h_source);
		if (!orig_neigh_node)
			return;

		/* neighbor has to indicate direct link and it has to
		 * come via the corresponding interface */
		/* save packet seqno for bidirectional check */
		if (has_directlink_flag &&
		    compare_eth(if_incoming->net_dev->dev_addr,
				batman_packet->orig)) {
			offset = if_incoming->if_num * NUM_WORDS;

			spin_lock_bh(&orig_neigh_node->ogm_cnt_lock);
			word = &(orig_neigh_node->bcast_own[offset]);
			bit_mark(word,
				 if_incoming_seqno - batman_packet->seqno - 2);
			orig_neigh_node->bcast_own_sum[if_incoming->if_num] =
				bit_packet_count(word);
			spin_unlock_bh(&orig_neigh_node->ogm_cnt_lock);
		}

		bat_dbg(DBG_BATMAN, bat_priv, "Drop packet: "
			"originator packet from myself (via neighbor)\n");
		orig_node_free_ref(orig_neigh_node);
		return;
	}

	if (is_my_oldorig) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: ignoring all rebroadcast echos (sender: "
			"%pM)\n", ethhdr->h_source);
		return;
	}

	orig_node = get_orig_node(bat_priv, batman_packet->orig);
	if (!orig_node)
		return;

	is_duplicate = count_real_packets(ethhdr, batman_packet, if_incoming);

	if (is_duplicate == -1) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: packet within seqno protection time "
			"(sender: %pM)\n", ethhdr->h_source);
		goto out;
	}

	if (batman_packet->tq == 0) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: originator packet with tq equal 0\n");
		goto out;
	}

	router = orig_node_get_router(orig_node);
	if (router)
		router_router = orig_node_get_router(router->orig_node);

	/* avoid temporary routing loops */
	if (router && router_router &&
	    (compare_eth(router->addr, batman_packet->prev_sender)) &&
	    !(compare_eth(batman_packet->orig, batman_packet->prev_sender)) &&
	    (compare_eth(router->addr, router_router->addr))) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: ignoring all rebroadcast packets that "
			"may make me loop (sender: %pM)\n", ethhdr->h_source);
		goto out;
	}

	/* if sender is a direct neighbor the sender mac equals
	 * originator mac */
	orig_neigh_node = (is_single_hop_neigh ?
			   orig_node :
			   get_orig_node(bat_priv, ethhdr->h_source));
	if (!orig_neigh_node)
		goto out;

	orig_neigh_router = orig_node_get_router(orig_neigh_node);

	/* drop packet if sender is not a direct neighbor and if we
	 * don't route towards it */
	if (!is_single_hop_neigh && (!orig_neigh_router)) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: OGM via unknown neighbor!\n");
		goto out_neigh;
	}

	is_bidirectional = is_bidirectional_neigh(orig_node, orig_neigh_node,
						batman_packet, if_incoming);

	bonding_save_primary(orig_node, orig_neigh_node, batman_packet);

	/* update ranking if it is not a duplicate or has the same
	 * seqno and similar ttl as the non-duplicate */
	if (is_bidirectional &&
	    (!is_duplicate ||
	     ((orig_node->last_real_seqno == batman_packet->seqno) &&
	      (orig_node->last_ttl - 3 <= batman_packet->ttl))))
		update_orig(bat_priv, orig_node, ethhdr, batman_packet,
			    if_incoming, tt_buff, is_duplicate);

	/* is single hop (direct) neighbor */
	if (is_single_hop_neigh) {

		/* mark direct link on incoming interface */
		schedule_forward_packet(orig_node, ethhdr, batman_packet,
					1, if_incoming);

		bat_dbg(DBG_BATMAN, bat_priv, "Forwarding packet: "
			"rebroadcast neighbor packet with direct link flag\n");
		goto out_neigh;
	}

	/* multihop originator */
	if (!is_bidirectional) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: not received via bidirectional link\n");
		goto out_neigh;
	}

	if (is_duplicate) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: duplicate packet received\n");
		goto out_neigh;
	}

	bat_dbg(DBG_BATMAN, bat_priv,
		"Forwarding packet: rebroadcast originator packet\n");
	schedule_forward_packet(orig_node, ethhdr, batman_packet,
				0, if_incoming);

out_neigh:
	if ((orig_neigh_node) && (!is_single_hop_neigh))
		orig_node_free_ref(orig_neigh_node);
out:
	if (router)
		neigh_node_free_ref(router);
	if (router_router)
		neigh_node_free_ref(router_router);
	if (orig_neigh_router)
		neigh_node_free_ref(orig_neigh_router);

	orig_node_free_ref(orig_node);
}

int recv_bat_packet(struct sk_buff *skb, struct hard_iface *hard_iface)
{
	struct ethhdr *ethhdr;

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb, sizeof(struct batman_packet))))
		return NET_RX_DROP;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with broadcast indication but unicast recipient */
	if (!is_broadcast_ether_addr(ethhdr->h_dest))
		return NET_RX_DROP;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		return NET_RX_DROP;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, 0) < 0)
		return NET_RX_DROP;

	/* keep skb linear */
	if (skb_linearize(skb) < 0)
		return NET_RX_DROP;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	receive_aggr_bat_packet(ethhdr,
				skb->data,
				skb_headlen(skb),
				hard_iface);

	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

static int recv_my_icmp_packet(struct bat_priv *bat_priv,
			       struct sk_buff *skb, size_t icmp_len)
{
	struct hard_iface *primary_if = NULL;
	struct orig_node *orig_node = NULL;
	struct neigh_node *router = NULL;
	struct icmp_packet_rr *icmp_packet;
	int ret = NET_RX_DROP;

	icmp_packet = (struct icmp_packet_rr *)skb->data;

	/* add data to device queue */
	if (icmp_packet->msg_type != ECHO_REQUEST) {
		bat_socket_receive_packet(icmp_packet, icmp_len);
		goto out;
	}

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	/* answer echo request (ping) */
	/* get routing information */
	orig_node = orig_hash_find(bat_priv, icmp_packet->orig);
	if (!orig_node)
		goto out;

	router = orig_node_get_router(orig_node);
	if (!router)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, sizeof(struct ethhdr)) < 0)
		goto out;

	icmp_packet = (struct icmp_packet_rr *)skb->data;

	memcpy(icmp_packet->dst, icmp_packet->orig, ETH_ALEN);
	memcpy(icmp_packet->orig, primary_if->net_dev->dev_addr, ETH_ALEN);
	icmp_packet->msg_type = ECHO_REPLY;
	icmp_packet->ttl = TTL;

	send_skb_packet(skb, router->if_incoming, router->addr);
	ret = NET_RX_SUCCESS;

out:
	if (primary_if)
		hardif_free_ref(primary_if);
	if (router)
		neigh_node_free_ref(router);
	if (orig_node)
		orig_node_free_ref(orig_node);
	return ret;
}

static int recv_icmp_ttl_exceeded(struct bat_priv *bat_priv,
				  struct sk_buff *skb)
{
	struct hard_iface *primary_if = NULL;
	struct orig_node *orig_node = NULL;
	struct neigh_node *router = NULL;
	struct icmp_packet *icmp_packet;
	int ret = NET_RX_DROP;

	icmp_packet = (struct icmp_packet *)skb->data;

	/* send TTL exceeded if packet is an echo request (traceroute) */
	if (icmp_packet->msg_type != ECHO_REQUEST) {
		pr_debug("Warning - can't forward icmp packet from %pM to "
			 "%pM: ttl exceeded\n", icmp_packet->orig,
			 icmp_packet->dst);
		goto out;
	}

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	/* get routing information */
	orig_node = orig_hash_find(bat_priv, icmp_packet->orig);
	if (!orig_node)
		goto out;

	router = orig_node_get_router(orig_node);
	if (!router)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, sizeof(struct ethhdr)) < 0)
		goto out;

	icmp_packet = (struct icmp_packet *)skb->data;

	memcpy(icmp_packet->dst, icmp_packet->orig, ETH_ALEN);
	memcpy(icmp_packet->orig, primary_if->net_dev->dev_addr, ETH_ALEN);
	icmp_packet->msg_type = TTL_EXCEEDED;
	icmp_packet->ttl = TTL;

	send_skb_packet(skb, router->if_incoming, router->addr);
	ret = NET_RX_SUCCESS;

out:
	if (primary_if)
		hardif_free_ref(primary_if);
	if (router)
		neigh_node_free_ref(router);
	if (orig_node)
		orig_node_free_ref(orig_node);
	return ret;
}


int recv_icmp_packet(struct sk_buff *skb, struct hard_iface *recv_if)
{
	struct bat_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct icmp_packet_rr *icmp_packet;
	struct ethhdr *ethhdr;
	struct orig_node *orig_node = NULL;
	struct neigh_node *router = NULL;
	int hdr_size = sizeof(struct icmp_packet);
	int ret = NET_RX_DROP;

	/**
	 * we truncate all incoming icmp packets if they don't match our size
	 */
	if (skb->len >= sizeof(struct icmp_packet_rr))
		hdr_size = sizeof(struct icmp_packet_rr);

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
	if (!is_my_mac(ethhdr->h_dest))
		goto out;

	icmp_packet = (struct icmp_packet_rr *)skb->data;

	/* add record route information if not full */
	if ((hdr_size == sizeof(struct icmp_packet_rr)) &&
	    (icmp_packet->rr_cur < BAT_RR_LEN)) {
		memcpy(&(icmp_packet->rr[icmp_packet->rr_cur]),
			ethhdr->h_dest, ETH_ALEN);
		icmp_packet->rr_cur++;
	}

	/* packet for me */
	if (is_my_mac(icmp_packet->dst))
		return recv_my_icmp_packet(bat_priv, skb, hdr_size);

	/* TTL exceeded */
	if (icmp_packet->ttl < 2)
		return recv_icmp_ttl_exceeded(bat_priv, skb);

	/* get routing information */
	orig_node = orig_hash_find(bat_priv, icmp_packet->dst);
	if (!orig_node)
		goto out;

	router = orig_node_get_router(orig_node);
	if (!router)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, sizeof(struct ethhdr)) < 0)
		goto out;

	icmp_packet = (struct icmp_packet_rr *)skb->data;

	/* decrement ttl */
	icmp_packet->ttl--;

	/* route it */
	send_skb_packet(skb, router->if_incoming, router->addr);
	ret = NET_RX_SUCCESS;

out:
	if (router)
		neigh_node_free_ref(router);
	if (orig_node)
		orig_node_free_ref(orig_node);
	return ret;
}

/* In the bonding case, send the packets in a round
 * robin fashion over the remaining interfaces.
 *
 * This method rotates the bonding list and increases the
 * returned router's refcount. */
static struct neigh_node *find_bond_router(struct orig_node *primary_orig,
					   const struct hard_iface *recv_if)
{
	struct neigh_node *tmp_neigh_node;
	struct neigh_node *router = NULL, *first_candidate = NULL;

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
	 * after the current router */
	spin_lock_bh(&primary_orig->neigh_list_lock);
	/* this is a list_move(), which unfortunately
	 * does not exist as rcu version */
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
 * Increases the returned router's refcount */
static struct neigh_node *find_ifalter_router(struct orig_node *primary_orig,
					      const struct hard_iface *recv_if)
{
	struct neigh_node *tmp_neigh_node;
	struct neigh_node *router = NULL, *first_candidate = NULL;

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
		 * or this one is better, choose it. */
		if ((!router) ||
		    (tmp_neigh_node->tq_avg > router->tq_avg)) {
			/* decrement refcount of
			 * previously selected router */
			if (router)
				neigh_node_free_ref(router);

			router = tmp_neigh_node;
			atomic_inc_not_zero(&router->refcount);
		}

		neigh_node_free_ref(tmp_neigh_node);
	}

	/* use the first candidate if nothing was found. */
	if (!router && first_candidate &&
	    atomic_inc_not_zero(&first_candidate->refcount))
		router = first_candidate;

	rcu_read_unlock();
	return router;
}

int recv_tt_query(struct sk_buff *skb, struct hard_iface *recv_if)
{
	struct bat_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct tt_query_packet *tt_query;
	struct ethhdr *ethhdr;

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb, sizeof(struct tt_query_packet))))
		goto out;

	/* I could need to modify it */
	if (skb_cow(skb, sizeof(struct tt_query_packet)) < 0)
		goto out;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with unicast indication but broadcast recipient */
	if (is_broadcast_ether_addr(ethhdr->h_dest))
		goto out;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		goto out;

	tt_query = (struct tt_query_packet *)skb->data;

	tt_query->tt_data = ntohs(tt_query->tt_data);

	switch (tt_query->flags & TT_QUERY_TYPE_MASK) {
	case TT_REQUEST:
		/* If we cannot provide an answer the tt_request is
		 * forwarded */
		if (!send_tt_response(bat_priv, tt_query)) {
			bat_dbg(DBG_TT, bat_priv,
				"Routing TT_REQUEST to %pM [%c]\n",
				tt_query->dst,
				(tt_query->flags & TT_FULL_TABLE ? 'F' : '.'));
			tt_query->tt_data = htons(tt_query->tt_data);
			return route_unicast_packet(skb, recv_if);
		}
		break;
	case TT_RESPONSE:
		/* packet needs to be linearised to access the TT changes */
		if (skb_linearize(skb) < 0)
			goto out;

		if (is_my_mac(tt_query->dst))
			handle_tt_response(bat_priv, tt_query);
		else {
			bat_dbg(DBG_TT, bat_priv,
				"Routing TT_RESPONSE to %pM [%c]\n",
				tt_query->dst,
				(tt_query->flags & TT_FULL_TABLE ? 'F' : '.'));
			tt_query->tt_data = htons(tt_query->tt_data);
			return route_unicast_packet(skb, recv_if);
		}
		break;
	}

out:
	/* returning NET_RX_DROP will make the caller function kfree the skb */
	return NET_RX_DROP;
}

int recv_roam_adv(struct sk_buff *skb, struct hard_iface *recv_if)
{
	struct bat_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct roam_adv_packet *roam_adv_packet;
	struct orig_node *orig_node;
	struct ethhdr *ethhdr;

	/* drop packet if it has not necessary minimum size */
	if (unlikely(!pskb_may_pull(skb, sizeof(struct roam_adv_packet))))
		goto out;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with unicast indication but broadcast recipient */
	if (is_broadcast_ether_addr(ethhdr->h_dest))
		goto out;

	/* packet with broadcast sender address */
	if (is_broadcast_ether_addr(ethhdr->h_source))
		goto out;

	roam_adv_packet = (struct roam_adv_packet *)skb->data;

	if (!is_my_mac(roam_adv_packet->dst))
		return route_unicast_packet(skb, recv_if);

	orig_node = orig_hash_find(bat_priv, roam_adv_packet->src);
	if (!orig_node)
		goto out;

	bat_dbg(DBG_TT, bat_priv, "Received ROAMING_ADV from %pM "
		"(client %pM)\n", roam_adv_packet->src,
		roam_adv_packet->client);

	tt_global_add(bat_priv, orig_node, roam_adv_packet->client,
		      atomic_read(&orig_node->last_ttvn) + 1, true);

	/* Roaming phase starts: I have new information but the ttvn has not
	 * been incremented yet. This flag will make me check all the incoming
	 * packets for the correct destination. */
	bat_priv->tt_poss_change = true;

	orig_node_free_ref(orig_node);
out:
	/* returning NET_RX_DROP will make the caller function kfree the skb */
	return NET_RX_DROP;
}

/* find a suitable router for this originator, and use
 * bonding if possible. increases the found neighbors
 * refcount.*/
struct neigh_node *find_router(struct bat_priv *bat_priv,
			       struct orig_node *orig_node,
			       const struct hard_iface *recv_if)
{
	struct orig_node *primary_orig_node;
	struct orig_node *router_orig;
	struct neigh_node *router;
	static uint8_t zero_mac[ETH_ALEN] = {0, 0, 0, 0, 0, 0};
	int bonding_enabled;

	if (!orig_node)
		return NULL;

	router = orig_node_get_router(orig_node);
	if (!router)
		goto err;

	/* without bonding, the first node should
	 * always choose the default router. */
	bonding_enabled = atomic_read(&bat_priv->bonding);

	rcu_read_lock();
	/* select default router to output */
	router_orig = router->orig_node;
	if (!router_orig)
		goto err_unlock;

	if ((!recv_if) && (!bonding_enabled))
		goto return_router;

	/* if we have something in the primary_addr, we can search
	 * for a potential bonding candidate. */
	if (compare_eth(router_orig->primary_addr, zero_mac))
		goto return_router;

	/* find the orig_node which has the primary interface. might
	 * even be the same as our router_orig in many cases */

	if (compare_eth(router_orig->primary_addr, router_orig->orig)) {
		primary_orig_node = router_orig;
	} else {
		primary_orig_node = orig_hash_find(bat_priv,
						   router_orig->primary_addr);
		if (!primary_orig_node)
			goto return_router;

		orig_node_free_ref(primary_orig_node);
	}

	/* with less than 2 candidates, we can't do any
	 * bonding and prefer the original router. */
	if (atomic_read(&primary_orig_node->bond_candidates) < 2)
		goto return_router;

	/* all nodes between should choose a candidate which
	 * is is not on the interface where the packet came
	 * in. */

	neigh_node_free_ref(router);

	if (bonding_enabled)
		router = find_bond_router(primary_orig_node, recv_if);
	else
		router = find_ifalter_router(primary_orig_node, recv_if);

return_router:
	if (router && router->if_incoming->if_status != IF_ACTIVE)
		goto err_unlock;

	rcu_read_unlock();
	return router;
err_unlock:
	rcu_read_unlock();
err:
	if (router)
		neigh_node_free_ref(router);
	return NULL;
}

static int check_unicast_packet(struct sk_buff *skb, int hdr_size)
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
	if (!is_my_mac(ethhdr->h_dest))
		return -1;

	return 0;
}

int route_unicast_packet(struct sk_buff *skb, struct hard_iface *recv_if)
{
	struct bat_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct orig_node *orig_node = NULL;
	struct neigh_node *neigh_node = NULL;
	struct unicast_packet *unicast_packet;
	struct ethhdr *ethhdr = (struct ethhdr *)skb_mac_header(skb);
	int ret = NET_RX_DROP;
	struct sk_buff *new_skb;

	unicast_packet = (struct unicast_packet *)skb->data;

	/* TTL exceeded */
	if (unicast_packet->ttl < 2) {
		pr_debug("Warning - can't forward unicast packet from %pM to "
			 "%pM: ttl exceeded\n", ethhdr->h_source,
			 unicast_packet->dest);
		goto out;
	}

	/* get routing information */
	orig_node = orig_hash_find(bat_priv, unicast_packet->dest);

	if (!orig_node)
		goto out;

	/* find_router() increases neigh_nodes refcount if found. */
	neigh_node = find_router(bat_priv, orig_node, recv_if);

	if (!neigh_node)
		goto out;

	/* create a copy of the skb, if needed, to modify it. */
	if (skb_cow(skb, sizeof(struct ethhdr)) < 0)
		goto out;

	unicast_packet = (struct unicast_packet *)skb->data;

	if (unicast_packet->packet_type == BAT_UNICAST &&
	    atomic_read(&bat_priv->fragmentation) &&
	    skb->len > neigh_node->if_incoming->net_dev->mtu) {
		ret = frag_send_skb(skb, bat_priv,
				    neigh_node->if_incoming, neigh_node->addr);
		goto out;
	}

	if (unicast_packet->packet_type == BAT_UNICAST_FRAG &&
	    frag_can_reassemble(skb, neigh_node->if_incoming->net_dev->mtu)) {

		ret = frag_reassemble_skb(skb, bat_priv, &new_skb);

		if (ret == NET_RX_DROP)
			goto out;

		/* packet was buffered for late merge */
		if (!new_skb) {
			ret = NET_RX_SUCCESS;
			goto out;
		}

		skb = new_skb;
		unicast_packet = (struct unicast_packet *)skb->data;
	}

	/* decrement ttl */
	unicast_packet->ttl--;

	/* route it */
	send_skb_packet(skb, neigh_node->if_incoming, neigh_node->addr);
	ret = NET_RX_SUCCESS;

out:
	if (neigh_node)
		neigh_node_free_ref(neigh_node);
	if (orig_node)
		orig_node_free_ref(orig_node);
	return ret;
}

static int check_unicast_ttvn(struct bat_priv *bat_priv,
			       struct sk_buff *skb) {
	uint8_t curr_ttvn;
	struct orig_node *orig_node;
	struct ethhdr *ethhdr;
	struct hard_iface *primary_if;
	struct unicast_packet *unicast_packet;
	bool tt_poss_change;

	/* I could need to modify it */
	if (skb_cow(skb, sizeof(struct unicast_packet)) < 0)
		return 0;

	unicast_packet = (struct unicast_packet *)skb->data;

	if (is_my_mac(unicast_packet->dest)) {
		tt_poss_change = bat_priv->tt_poss_change;
		curr_ttvn = (uint8_t)atomic_read(&bat_priv->ttvn);
	} else {
		orig_node = orig_hash_find(bat_priv, unicast_packet->dest);

		if (!orig_node)
			return 0;

		curr_ttvn = (uint8_t)atomic_read(&orig_node->last_ttvn);
		tt_poss_change = orig_node->tt_poss_change;
		orig_node_free_ref(orig_node);
	}

	/* Check whether I have to reroute the packet */
	if (seq_before(unicast_packet->ttvn, curr_ttvn) || tt_poss_change) {
		/* Linearize the skb before accessing it */
		if (skb_linearize(skb) < 0)
			return 0;

		ethhdr = (struct ethhdr *)(skb->data +
			sizeof(struct unicast_packet));
		orig_node = transtable_search(bat_priv, ethhdr->h_dest);

		if (!orig_node) {
			if (!is_my_client(bat_priv, ethhdr->h_dest))
				return 0;
			primary_if = primary_if_get_selected(bat_priv);
			if (!primary_if)
				return 0;
			memcpy(unicast_packet->dest,
			       primary_if->net_dev->dev_addr, ETH_ALEN);
			hardif_free_ref(primary_if);
		} else {
			memcpy(unicast_packet->dest, orig_node->orig,
			       ETH_ALEN);
			curr_ttvn = (uint8_t)
				atomic_read(&orig_node->last_ttvn);
			orig_node_free_ref(orig_node);
		}

		bat_dbg(DBG_ROUTES, bat_priv, "TTVN mismatch (old_ttvn %u "
			"new_ttvn %u)! Rerouting unicast packet (for %pM) to "
			"%pM\n", unicast_packet->ttvn, curr_ttvn,
			ethhdr->h_dest, unicast_packet->dest);

		unicast_packet->ttvn = curr_ttvn;
	}
	return 1;
}

int recv_unicast_packet(struct sk_buff *skb, struct hard_iface *recv_if)
{
	struct bat_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct unicast_packet *unicast_packet;
	int hdr_size = sizeof(*unicast_packet);

	if (check_unicast_packet(skb, hdr_size) < 0)
		return NET_RX_DROP;

	if (!check_unicast_ttvn(bat_priv, skb))
		return NET_RX_DROP;

	unicast_packet = (struct unicast_packet *)skb->data;

	/* packet for me */
	if (is_my_mac(unicast_packet->dest)) {
		interface_rx(recv_if->soft_iface, skb, recv_if, hdr_size);
		return NET_RX_SUCCESS;
	}

	return route_unicast_packet(skb, recv_if);
}

int recv_ucast_frag_packet(struct sk_buff *skb, struct hard_iface *recv_if)
{
	struct bat_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct unicast_frag_packet *unicast_packet;
	int hdr_size = sizeof(*unicast_packet);
	struct sk_buff *new_skb = NULL;
	int ret;

	if (check_unicast_packet(skb, hdr_size) < 0)
		return NET_RX_DROP;

	if (!check_unicast_ttvn(bat_priv, skb))
		return NET_RX_DROP;

	unicast_packet = (struct unicast_frag_packet *)skb->data;

	/* packet for me */
	if (is_my_mac(unicast_packet->dest)) {

		ret = frag_reassemble_skb(skb, bat_priv, &new_skb);

		if (ret == NET_RX_DROP)
			return NET_RX_DROP;

		/* packet was buffered for late merge */
		if (!new_skb)
			return NET_RX_SUCCESS;

		interface_rx(recv_if->soft_iface, new_skb, recv_if,
			     sizeof(struct unicast_packet));
		return NET_RX_SUCCESS;
	}

	return route_unicast_packet(skb, recv_if);
}


int recv_bcast_packet(struct sk_buff *skb, struct hard_iface *recv_if)
{
	struct bat_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct orig_node *orig_node = NULL;
	struct bcast_packet *bcast_packet;
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
	if (is_my_mac(ethhdr->h_source))
		goto out;

	bcast_packet = (struct bcast_packet *)skb->data;

	/* ignore broadcasts originated by myself */
	if (is_my_mac(bcast_packet->orig))
		goto out;

	if (bcast_packet->ttl < 2)
		goto out;

	orig_node = orig_hash_find(bat_priv, bcast_packet->orig);

	if (!orig_node)
		goto out;

	spin_lock_bh(&orig_node->bcast_seqno_lock);

	/* check whether the packet is a duplicate */
	if (get_bit_status(orig_node->bcast_bits, orig_node->last_bcast_seqno,
			   ntohl(bcast_packet->seqno)))
		goto spin_unlock;

	seq_diff = ntohl(bcast_packet->seqno) - orig_node->last_bcast_seqno;

	/* check whether the packet is old and the host just restarted. */
	if (window_protected(bat_priv, seq_diff,
			     &orig_node->bcast_seqno_reset))
		goto spin_unlock;

	/* mark broadcast in flood history, update window position
	 * if required. */
	if (bit_get_packet(bat_priv, orig_node->bcast_bits, seq_diff, 1))
		orig_node->last_bcast_seqno = ntohl(bcast_packet->seqno);

	spin_unlock_bh(&orig_node->bcast_seqno_lock);

	/* rebroadcast packet */
	add_bcast_packet_to_list(bat_priv, skb, 1);

	/* broadcast for me */
	interface_rx(recv_if->soft_iface, skb, recv_if, hdr_size);
	ret = NET_RX_SUCCESS;
	goto out;

spin_unlock:
	spin_unlock_bh(&orig_node->bcast_seqno_lock);
out:
	if (orig_node)
		orig_node_free_ref(orig_node);
	return ret;
}

int recv_vis_packet(struct sk_buff *skb, struct hard_iface *recv_if)
{
	struct vis_packet *vis_packet;
	struct ethhdr *ethhdr;
	struct bat_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	int hdr_size = sizeof(*vis_packet);

	/* keep skb linear */
	if (skb_linearize(skb) < 0)
		return NET_RX_DROP;

	if (unlikely(!pskb_may_pull(skb, hdr_size)))
		return NET_RX_DROP;

	vis_packet = (struct vis_packet *)skb->data;
	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* not for me */
	if (!is_my_mac(ethhdr->h_dest))
		return NET_RX_DROP;

	/* ignore own packets */
	if (is_my_mac(vis_packet->vis_orig))
		return NET_RX_DROP;

	if (is_my_mac(vis_packet->sender_orig))
		return NET_RX_DROP;

	switch (vis_packet->vis_type) {
	case VIS_TYPE_SERVER_SYNC:
		receive_server_sync_packet(bat_priv, vis_packet,
					   skb_headlen(skb));
		break;

	case VIS_TYPE_CLIENT_UPDATE:
		receive_client_update_packet(bat_priv, vis_packet,
					     skb_headlen(skb));
		break;

	default:	/* ignore unknown packet */
		break;
	}

	/* We take a copy of the data in the packet, so we should
	   always free the skbuf. */
	return NET_RX_DROP;
}
