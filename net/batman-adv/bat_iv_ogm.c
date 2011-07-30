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
#include "bat_ogm.h"
#include "translation-table.h"
#include "ring_buffer.h"
#include "originator.h"
#include "routing.h"
#include "gateway_common.h"
#include "gateway_client.h"
#include "hard-interface.h"
#include "send.h"

void bat_ogm_init(struct hard_iface *hard_iface)
{
	struct batman_ogm_packet *batman_ogm_packet;

	hard_iface->packet_len = BATMAN_OGM_LEN;
	hard_iface->packet_buff = kmalloc(hard_iface->packet_len, GFP_ATOMIC);

	batman_ogm_packet = (struct batman_ogm_packet *)hard_iface->packet_buff;
	batman_ogm_packet->packet_type = BAT_OGM;
	batman_ogm_packet->version = COMPAT_VERSION;
	batman_ogm_packet->flags = NO_FLAGS;
	batman_ogm_packet->ttl = 2;
	batman_ogm_packet->tq = TQ_MAX_VALUE;
	batman_ogm_packet->tt_num_changes = 0;
	batman_ogm_packet->ttvn = 0;
}

void bat_ogm_init_primary(struct hard_iface *hard_iface)
{
	struct batman_ogm_packet *batman_ogm_packet;

	batman_ogm_packet = (struct batman_ogm_packet *)hard_iface->packet_buff;
	batman_ogm_packet->flags = PRIMARIES_FIRST_HOP;
	batman_ogm_packet->ttl = TTL;
}

void bat_ogm_update_mac(struct hard_iface *hard_iface)
{
	struct batman_ogm_packet *batman_ogm_packet;

	batman_ogm_packet = (struct batman_ogm_packet *)hard_iface->packet_buff;
	memcpy(batman_ogm_packet->orig,
	       hard_iface->net_dev->dev_addr, ETH_ALEN);
	memcpy(batman_ogm_packet->prev_sender,
	       hard_iface->net_dev->dev_addr, ETH_ALEN);
}

/* is there another aggregated packet here? */
static int bat_ogm_aggr_packet(int buff_pos, int packet_len,
			       int tt_num_changes)
{
	int next_buff_pos = buff_pos + BATMAN_OGM_LEN + tt_len(tt_num_changes);

	return (next_buff_pos <= packet_len) &&
		(next_buff_pos <= MAX_AGGREGATION_BYTES);
}

static void bat_ogm_orig_update(struct bat_priv *bat_priv,
				struct orig_node *orig_node,
				const struct ethhdr *ethhdr,
				const struct batman_ogm_packet
							*batman_ogm_packet,
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

	orig_node->flags = batman_ogm_packet->flags;
	neigh_node->last_valid = jiffies;

	spin_lock_bh(&neigh_node->tq_lock);
	ring_buffer_set(neigh_node->tq_recv,
			&neigh_node->tq_index,
			batman_ogm_packet->tq);
	neigh_node->tq_avg = ring_buffer_avg(neigh_node->tq_recv);
	spin_unlock_bh(&neigh_node->tq_lock);

	if (!is_duplicate) {
		orig_node->last_ttl = batman_ogm_packet->ttl;
		neigh_node->last_ttl = batman_ogm_packet->ttl;
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

	/* if the TQ is the same and the link not more symmetric we
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

	update_route(bat_priv, orig_node, neigh_node);

update_tt:
	/* I have to check for transtable changes only if the OGM has been
	 * sent through a primary interface */
	if (((batman_ogm_packet->orig != ethhdr->h_source) &&
	     (batman_ogm_packet->ttl > 2)) ||
	    (batman_ogm_packet->flags & PRIMARIES_FIRST_HOP))
		tt_update_orig(bat_priv, orig_node, tt_buff,
			       batman_ogm_packet->tt_num_changes,
			       batman_ogm_packet->ttvn,
			       batman_ogm_packet->tt_crc);

	if (orig_node->gw_flags != batman_ogm_packet->gw_flags)
		gw_node_update(bat_priv, orig_node,
			       batman_ogm_packet->gw_flags);

	orig_node->gw_flags = batman_ogm_packet->gw_flags;

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

static int bat_ogm_calc_tq(struct orig_node *orig_node,
			   struct orig_node *orig_neigh_node,
			   struct batman_ogm_packet *batman_ogm_packet,
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

	/* if orig_node is direct neighbor update neigh_node last_valid */
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

	batman_ogm_packet->tq = ((batman_ogm_packet->tq * tq_own
							* tq_asym_penalty) /
						(TQ_MAX_VALUE * TQ_MAX_VALUE));

	bat_dbg(DBG_BATMAN, bat_priv,
		"bidirectional: "
		"orig = %-15pM neigh = %-15pM => own_bcast = %2i, "
		"real recv = %2i, local tq: %3i, asym_penalty: %3i, "
		"total tq: %3i\n",
		orig_node->orig, orig_neigh_node->orig, total_count,
		neigh_rq_count, tq_own,	tq_asym_penalty, batman_ogm_packet->tq);

	/* if link has the minimum required transmission quality
	 * consider it bidirectional */
	if (batman_ogm_packet->tq >= TQ_TOTAL_BIDRECT_LIMIT)
		ret = 1;

out:
	if (neigh_node)
		neigh_node_free_ref(neigh_node);
	return ret;
}

/* processes a batman packet for all interfaces, adjusts the sequence number and
 * finds out whether it is a duplicate.
 * returns:
 *   1 the packet is a duplicate
 *   0 the packet has not yet been received
 *  -1 the packet is old and has been received while the seqno window
 *     was protected. Caller should drop it.
 */
static int bat_ogm_update_seqnos(const struct ethhdr *ethhdr,
				 const struct batman_ogm_packet
							*batman_ogm_packet,
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

	orig_node = get_orig_node(bat_priv, batman_ogm_packet->orig);
	if (!orig_node)
		return 0;

	spin_lock_bh(&orig_node->ogm_cnt_lock);
	seq_diff = batman_ogm_packet->seqno - orig_node->last_real_seqno;

	/* signalize caller that the packet is to be dropped. */
	if (window_protected(bat_priv, seq_diff,
			     &orig_node->batman_seqno_reset))
		goto out;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node, node,
				 &orig_node->neigh_list, list) {

		is_duplicate |= get_bit_status(tmp_neigh_node->real_bits,
					       orig_node->last_real_seqno,
					       batman_ogm_packet->seqno);

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
			orig_node->last_real_seqno, batman_ogm_packet->seqno);
		orig_node->last_real_seqno = batman_ogm_packet->seqno;
	}

	ret = is_duplicate;

out:
	spin_unlock_bh(&orig_node->ogm_cnt_lock);
	orig_node_free_ref(orig_node);
	return ret;
}

static void bat_ogm_process(const struct ethhdr *ethhdr,
			    struct batman_ogm_packet *batman_ogm_packet,
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
	 * batman_ogm_packet to detect whether the packet is the last
	 * packet in an aggregation.  Here we expect that the padding
	 * is always zero (or not 0x01)
	 */
	if (batman_ogm_packet->packet_type != BAT_OGM)
		return;

	/* could be changed by schedule_own_packet() */
	if_incoming_seqno = atomic_read(&if_incoming->seqno);

	has_directlink_flag = (batman_ogm_packet->flags & DIRECTLINK ? 1 : 0);

	is_single_hop_neigh = (compare_eth(ethhdr->h_source,
					   batman_ogm_packet->orig) ? 1 : 0);

	bat_dbg(DBG_BATMAN, bat_priv,
		"Received BATMAN packet via NB: %pM, IF: %s [%pM] "
		"(from OG: %pM, via prev OG: %pM, seqno %d, ttvn %u, "
		"crc %u, changes %u, td %d, TTL %d, V %d, IDF %d)\n",
		ethhdr->h_source, if_incoming->net_dev->name,
		if_incoming->net_dev->dev_addr, batman_ogm_packet->orig,
		batman_ogm_packet->prev_sender, batman_ogm_packet->seqno,
		batman_ogm_packet->ttvn, batman_ogm_packet->tt_crc,
		batman_ogm_packet->tt_num_changes, batman_ogm_packet->tq,
		batman_ogm_packet->ttl, batman_ogm_packet->version,
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

		if (compare_eth(batman_ogm_packet->orig,
				hard_iface->net_dev->dev_addr))
			is_my_orig = 1;

		if (compare_eth(batman_ogm_packet->prev_sender,
				hard_iface->net_dev->dev_addr))
			is_my_oldorig = 1;

		if (is_broadcast_ether_addr(ethhdr->h_source))
			is_broadcast = 1;
	}
	rcu_read_unlock();

	if (batman_ogm_packet->version != COMPAT_VERSION) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: incompatible batman version (%i)\n",
			batman_ogm_packet->version);
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
				batman_ogm_packet->orig)) {
			offset = if_incoming->if_num * NUM_WORDS;

			spin_lock_bh(&orig_neigh_node->ogm_cnt_lock);
			word = &(orig_neigh_node->bcast_own[offset]);
			bit_mark(word,
				 if_incoming_seqno -
						batman_ogm_packet->seqno - 2);
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

	orig_node = get_orig_node(bat_priv, batman_ogm_packet->orig);
	if (!orig_node)
		return;

	is_duplicate = bat_ogm_update_seqnos(ethhdr, batman_ogm_packet,
					     if_incoming);

	if (is_duplicate == -1) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: packet within seqno protection time "
			"(sender: %pM)\n", ethhdr->h_source);
		goto out;
	}

	if (batman_ogm_packet->tq == 0) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: originator packet with tq equal 0\n");
		goto out;
	}

	router = orig_node_get_router(orig_node);
	if (router)
		router_router = orig_node_get_router(router->orig_node);

	/* avoid temporary routing loops */
	if (router && router_router &&
	    (compare_eth(router->addr, batman_ogm_packet->prev_sender)) &&
	    !(compare_eth(batman_ogm_packet->orig,
			  batman_ogm_packet->prev_sender)) &&
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

	is_bidirectional = bat_ogm_calc_tq(orig_node, orig_neigh_node,
					   batman_ogm_packet, if_incoming);

	bonding_save_primary(orig_node, orig_neigh_node, batman_ogm_packet);

	/* update ranking if it is not a duplicate or has the same
	 * seqno and similar ttl as the non-duplicate */
	if (is_bidirectional &&
	    (!is_duplicate ||
	     ((orig_node->last_real_seqno == batman_ogm_packet->seqno) &&
	      (orig_node->last_ttl - 3 <= batman_ogm_packet->ttl))))
		bat_ogm_orig_update(bat_priv, orig_node, ethhdr,
				    batman_ogm_packet, if_incoming,
				    tt_buff, is_duplicate);

	/* is single hop (direct) neighbor */
	if (is_single_hop_neigh) {

		/* mark direct link on incoming interface */
		schedule_forward_packet(orig_node, ethhdr, batman_ogm_packet,
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
	schedule_forward_packet(orig_node, ethhdr, batman_ogm_packet,
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

void bat_ogm_receive(const struct ethhdr *ethhdr, unsigned char *packet_buff,
		     int packet_len, struct hard_iface *if_incoming)
{
	struct batman_ogm_packet *batman_ogm_packet;
	int buff_pos = 0;
	unsigned char *tt_buff;

	batman_ogm_packet = (struct batman_ogm_packet *)packet_buff;

	/* unpack the aggregated packets and process them one by one */
	do {
		/* network to host order for our 32bit seqno and the
		   orig_interval */
		batman_ogm_packet->seqno = ntohl(batman_ogm_packet->seqno);
		batman_ogm_packet->tt_crc = ntohs(batman_ogm_packet->tt_crc);

		tt_buff = packet_buff + buff_pos + BATMAN_OGM_LEN;

		bat_ogm_process(ethhdr, batman_ogm_packet,
				tt_buff, if_incoming);

		buff_pos += BATMAN_OGM_LEN +
				tt_len(batman_ogm_packet->tt_num_changes);

		batman_ogm_packet = (struct batman_ogm_packet *)
						(packet_buff + buff_pos);
	} while (bat_ogm_aggr_packet(buff_pos, packet_len,
				     batman_ogm_packet->tt_num_changes));
}
