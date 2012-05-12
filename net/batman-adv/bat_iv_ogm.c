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
#include "translation-table.h"
#include "ring_buffer.h"
#include "originator.h"
#include "routing.h"
#include "gateway_common.h"
#include "gateway_client.h"
#include "hard-interface.h"
#include "send.h"
#include "bat_algo.h"

static struct neigh_node *bat_iv_ogm_neigh_new(struct hard_iface *hard_iface,
					       const uint8_t *neigh_addr,
					       struct orig_node *orig_node,
					       struct orig_node *orig_neigh,
					       __be32 seqno)
{
	struct neigh_node *neigh_node;

	neigh_node = batadv_neigh_node_new(hard_iface, neigh_addr,
					   ntohl(seqno));
	if (!neigh_node)
		goto out;

	INIT_LIST_HEAD(&neigh_node->bonding_list);

	neigh_node->orig_node = orig_neigh;
	neigh_node->if_incoming = hard_iface;

	spin_lock_bh(&orig_node->neigh_list_lock);
	hlist_add_head_rcu(&neigh_node->list, &orig_node->neigh_list);
	spin_unlock_bh(&orig_node->neigh_list_lock);

out:
	return neigh_node;
}

static int bat_iv_ogm_iface_enable(struct hard_iface *hard_iface)
{
	struct batman_ogm_packet *batman_ogm_packet;
	uint32_t random_seqno;
	int res = -ENOMEM;

	/* randomize initial seqno to avoid collision */
	get_random_bytes(&random_seqno, sizeof(random_seqno));
	atomic_set(&hard_iface->seqno, random_seqno);

	hard_iface->packet_len = BATMAN_OGM_HLEN;
	hard_iface->packet_buff = kmalloc(hard_iface->packet_len, GFP_ATOMIC);

	if (!hard_iface->packet_buff)
		goto out;

	batman_ogm_packet = (struct batman_ogm_packet *)hard_iface->packet_buff;
	batman_ogm_packet->header.packet_type = BAT_IV_OGM;
	batman_ogm_packet->header.version = COMPAT_VERSION;
	batman_ogm_packet->header.ttl = 2;
	batman_ogm_packet->flags = NO_FLAGS;
	batman_ogm_packet->tq = TQ_MAX_VALUE;
	batman_ogm_packet->tt_num_changes = 0;
	batman_ogm_packet->ttvn = 0;

	res = 0;

out:
	return res;
}

static void bat_iv_ogm_iface_disable(struct hard_iface *hard_iface)
{
	kfree(hard_iface->packet_buff);
	hard_iface->packet_buff = NULL;
}

static void bat_iv_ogm_iface_update_mac(struct hard_iface *hard_iface)
{
	struct batman_ogm_packet *batman_ogm_packet;

	batman_ogm_packet = (struct batman_ogm_packet *)hard_iface->packet_buff;
	memcpy(batman_ogm_packet->orig,
	       hard_iface->net_dev->dev_addr, ETH_ALEN);
	memcpy(batman_ogm_packet->prev_sender,
	       hard_iface->net_dev->dev_addr, ETH_ALEN);
}

static void bat_iv_ogm_primary_iface_set(struct hard_iface *hard_iface)
{
	struct batman_ogm_packet *batman_ogm_packet;

	batman_ogm_packet = (struct batman_ogm_packet *)hard_iface->packet_buff;
	batman_ogm_packet->flags = PRIMARIES_FIRST_HOP;
	batman_ogm_packet->header.ttl = TTL;
}

/* when do we schedule our own ogm to be sent */
static unsigned long bat_iv_ogm_emit_send_time(const struct bat_priv *bat_priv)
{
	return jiffies + msecs_to_jiffies(
		   atomic_read(&bat_priv->orig_interval) -
		   JITTER + (random32() % 2*JITTER));
}

/* when do we schedule a ogm packet to be sent */
static unsigned long bat_iv_ogm_fwd_send_time(void)
{
	return jiffies + msecs_to_jiffies(random32() % (JITTER/2));
}

/* apply hop penalty for a normal link */
static uint8_t hop_penalty(uint8_t tq, const struct bat_priv *bat_priv)
{
	int hop_penalty = atomic_read(&bat_priv->hop_penalty);
	return (tq * (TQ_MAX_VALUE - hop_penalty)) / (TQ_MAX_VALUE);
}

/* is there another aggregated packet here? */
static int bat_iv_ogm_aggr_packet(int buff_pos, int packet_len,
				  int tt_num_changes)
{
	int next_buff_pos = buff_pos + BATMAN_OGM_HLEN + tt_len(tt_num_changes);

	return (next_buff_pos <= packet_len) &&
		(next_buff_pos <= MAX_AGGREGATION_BYTES);
}

/* send a batman ogm to a given interface */
static void bat_iv_ogm_send_to_if(struct forw_packet *forw_packet,
				  struct hard_iface *hard_iface)
{
	struct bat_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	char *fwd_str;
	uint8_t packet_num;
	int16_t buff_pos;
	struct batman_ogm_packet *batman_ogm_packet;
	struct sk_buff *skb;

	if (hard_iface->if_status != IF_ACTIVE)
		return;

	packet_num = 0;
	buff_pos = 0;
	batman_ogm_packet = (struct batman_ogm_packet *)forw_packet->skb->data;

	/* adjust all flags and log packets */
	while (bat_iv_ogm_aggr_packet(buff_pos, forw_packet->packet_len,
				      batman_ogm_packet->tt_num_changes)) {

		/* we might have aggregated direct link packets with an
		 * ordinary base packet */
		if ((forw_packet->direct_link_flags & (1 << packet_num)) &&
		    (forw_packet->if_incoming == hard_iface))
			batman_ogm_packet->flags |= DIRECTLINK;
		else
			batman_ogm_packet->flags &= ~DIRECTLINK;

		fwd_str = (packet_num > 0 ? "Forwarding" : (forw_packet->own ?
							    "Sending own" :
							    "Forwarding"));
		bat_dbg(DBG_BATMAN, bat_priv,
			"%s %spacket (originator %pM, seqno %u, TQ %d, TTL %d, IDF %s, ttvn %d) on interface %s [%pM]\n",
			fwd_str, (packet_num > 0 ? "aggregated " : ""),
			batman_ogm_packet->orig,
			ntohl(batman_ogm_packet->seqno),
			batman_ogm_packet->tq, batman_ogm_packet->header.ttl,
			(batman_ogm_packet->flags & DIRECTLINK ?
			 "on" : "off"),
			batman_ogm_packet->ttvn, hard_iface->net_dev->name,
			hard_iface->net_dev->dev_addr);

		buff_pos += BATMAN_OGM_HLEN +
				tt_len(batman_ogm_packet->tt_num_changes);
		packet_num++;
		batman_ogm_packet = (struct batman_ogm_packet *)
					(forw_packet->skb->data + buff_pos);
	}

	/* create clone because function is called more than once */
	skb = skb_clone(forw_packet->skb, GFP_ATOMIC);
	if (skb) {
		batadv_inc_counter(bat_priv, BAT_CNT_MGMT_TX);
		batadv_add_counter(bat_priv, BAT_CNT_MGMT_TX_BYTES,
				   skb->len + ETH_HLEN);
		batadv_send_skb_packet(skb, hard_iface, broadcast_addr);
	}
}

/* send a batman ogm packet */
static void bat_iv_ogm_emit(struct forw_packet *forw_packet)
{
	struct hard_iface *hard_iface;
	struct net_device *soft_iface;
	struct bat_priv *bat_priv;
	struct hard_iface *primary_if = NULL;
	struct batman_ogm_packet *batman_ogm_packet;
	unsigned char directlink;

	batman_ogm_packet = (struct batman_ogm_packet *)
						(forw_packet->skb->data);
	directlink = (batman_ogm_packet->flags & DIRECTLINK ? 1 : 0);

	if (!forw_packet->if_incoming) {
		pr_err("Error - can't forward packet: incoming iface not specified\n");
		goto out;
	}

	soft_iface = forw_packet->if_incoming->soft_iface;
	bat_priv = netdev_priv(soft_iface);

	if (forw_packet->if_incoming->if_status != IF_ACTIVE)
		goto out;

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	/* multihomed peer assumed */
	/* non-primary OGMs are only broadcasted on their interface */
	if ((directlink && (batman_ogm_packet->header.ttl == 1)) ||
	    (forw_packet->own && (forw_packet->if_incoming != primary_if))) {

		/* FIXME: what about aggregated packets ? */
		bat_dbg(DBG_BATMAN, bat_priv,
			"%s packet (originator %pM, seqno %u, TTL %d) on interface %s [%pM]\n",
			(forw_packet->own ? "Sending own" : "Forwarding"),
			batman_ogm_packet->orig,
			ntohl(batman_ogm_packet->seqno),
			batman_ogm_packet->header.ttl,
			forw_packet->if_incoming->net_dev->name,
			forw_packet->if_incoming->net_dev->dev_addr);

		/* skb is only used once and than forw_packet is free'd */
		batadv_send_skb_packet(forw_packet->skb,
				       forw_packet->if_incoming,
				       broadcast_addr);
		forw_packet->skb = NULL;

		goto out;
	}

	/* broadcast on every interface */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &hardif_list, list) {
		if (hard_iface->soft_iface != soft_iface)
			continue;

		bat_iv_ogm_send_to_if(forw_packet, hard_iface);
	}
	rcu_read_unlock();

out:
	if (primary_if)
		hardif_free_ref(primary_if);
}

/* return true if new_packet can be aggregated with forw_packet */
static bool bat_iv_ogm_can_aggregate(const struct batman_ogm_packet
							*new_batman_ogm_packet,
				     struct bat_priv *bat_priv,
				     int packet_len, unsigned long send_time,
				     bool directlink,
				     const struct hard_iface *if_incoming,
				     const struct forw_packet *forw_packet)
{
	struct batman_ogm_packet *batman_ogm_packet;
	int aggregated_bytes = forw_packet->packet_len + packet_len;
	struct hard_iface *primary_if = NULL;
	bool res = false;

	batman_ogm_packet = (struct batman_ogm_packet *)forw_packet->skb->data;

	/**
	 * we can aggregate the current packet to this aggregated packet
	 * if:
	 *
	 * - the send time is within our MAX_AGGREGATION_MS time
	 * - the resulting packet wont be bigger than
	 *   MAX_AGGREGATION_BYTES
	 */

	if (time_before(send_time, forw_packet->send_time) &&
	    time_after_eq(send_time + msecs_to_jiffies(MAX_AGGREGATION_MS),
					forw_packet->send_time) &&
	    (aggregated_bytes <= MAX_AGGREGATION_BYTES)) {

		/**
		 * check aggregation compatibility
		 * -> direct link packets are broadcasted on
		 *    their interface only
		 * -> aggregate packet if the current packet is
		 *    a "global" packet as well as the base
		 *    packet
		 */

		primary_if = primary_if_get_selected(bat_priv);
		if (!primary_if)
			goto out;

		/* packets without direct link flag and high TTL
		 * are flooded through the net  */
		if ((!directlink) &&
		    (!(batman_ogm_packet->flags & DIRECTLINK)) &&
		    (batman_ogm_packet->header.ttl != 1) &&

		    /* own packets originating non-primary
		     * interfaces leave only that interface */
		    ((!forw_packet->own) ||
		     (forw_packet->if_incoming == primary_if))) {
			res = true;
			goto out;
		}

		/* if the incoming packet is sent via this one
		 * interface only - we still can aggregate */
		if ((directlink) &&
		    (new_batman_ogm_packet->header.ttl == 1) &&
		    (forw_packet->if_incoming == if_incoming) &&

		    /* packets from direct neighbors or
		     * own secondary interface packets
		     * (= secondary interface packets in general) */
		    (batman_ogm_packet->flags & DIRECTLINK ||
		     (forw_packet->own &&
		      forw_packet->if_incoming != primary_if))) {
			res = true;
			goto out;
		}
	}

out:
	if (primary_if)
		hardif_free_ref(primary_if);
	return res;
}

/* create a new aggregated packet and add this packet to it */
static void bat_iv_ogm_aggregate_new(const unsigned char *packet_buff,
				     int packet_len, unsigned long send_time,
				     bool direct_link,
				     struct hard_iface *if_incoming,
				     int own_packet)
{
	struct bat_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct forw_packet *forw_packet_aggr;
	unsigned char *skb_buff;

	if (!atomic_inc_not_zero(&if_incoming->refcount))
		return;

	/* own packet should always be scheduled */
	if (!own_packet) {
		if (!atomic_dec_not_zero(&bat_priv->batman_queue_left)) {
			bat_dbg(DBG_BATMAN, bat_priv,
				"batman packet queue full\n");
			goto out;
		}
	}

	forw_packet_aggr = kmalloc(sizeof(*forw_packet_aggr), GFP_ATOMIC);
	if (!forw_packet_aggr) {
		if (!own_packet)
			atomic_inc(&bat_priv->batman_queue_left);
		goto out;
	}

	if ((atomic_read(&bat_priv->aggregated_ogms)) &&
	    (packet_len < MAX_AGGREGATION_BYTES))
		forw_packet_aggr->skb = dev_alloc_skb(MAX_AGGREGATION_BYTES +
						      ETH_HLEN);
	else
		forw_packet_aggr->skb = dev_alloc_skb(packet_len + ETH_HLEN);

	if (!forw_packet_aggr->skb) {
		if (!own_packet)
			atomic_inc(&bat_priv->batman_queue_left);
		kfree(forw_packet_aggr);
		goto out;
	}
	skb_reserve(forw_packet_aggr->skb, ETH_HLEN);

	INIT_HLIST_NODE(&forw_packet_aggr->list);

	skb_buff = skb_put(forw_packet_aggr->skb, packet_len);
	forw_packet_aggr->packet_len = packet_len;
	memcpy(skb_buff, packet_buff, packet_len);

	forw_packet_aggr->own = own_packet;
	forw_packet_aggr->if_incoming = if_incoming;
	forw_packet_aggr->num_packets = 0;
	forw_packet_aggr->direct_link_flags = NO_FLAGS;
	forw_packet_aggr->send_time = send_time;

	/* save packet direct link flag status */
	if (direct_link)
		forw_packet_aggr->direct_link_flags |= 1;

	/* add new packet to packet list */
	spin_lock_bh(&bat_priv->forw_bat_list_lock);
	hlist_add_head(&forw_packet_aggr->list, &bat_priv->forw_bat_list);
	spin_unlock_bh(&bat_priv->forw_bat_list_lock);

	/* start timer for this packet */
	INIT_DELAYED_WORK(&forw_packet_aggr->delayed_work,
			  batadv_send_outstanding_bat_ogm_packet);
	queue_delayed_work(bat_event_workqueue,
			   &forw_packet_aggr->delayed_work,
			   send_time - jiffies);

	return;
out:
	hardif_free_ref(if_incoming);
}

/* aggregate a new packet into the existing ogm packet */
static void bat_iv_ogm_aggregate(struct forw_packet *forw_packet_aggr,
				 const unsigned char *packet_buff,
				 int packet_len, bool direct_link)
{
	unsigned char *skb_buff;

	skb_buff = skb_put(forw_packet_aggr->skb, packet_len);
	memcpy(skb_buff, packet_buff, packet_len);
	forw_packet_aggr->packet_len += packet_len;
	forw_packet_aggr->num_packets++;

	/* save packet direct link flag status */
	if (direct_link)
		forw_packet_aggr->direct_link_flags |=
			(1 << forw_packet_aggr->num_packets);
}

static void bat_iv_ogm_queue_add(struct bat_priv *bat_priv,
				 unsigned char *packet_buff,
				 int packet_len, struct hard_iface *if_incoming,
				 int own_packet, unsigned long send_time)
{
	/**
	 * _aggr -> pointer to the packet we want to aggregate with
	 * _pos -> pointer to the position in the queue
	 */
	struct forw_packet *forw_packet_aggr = NULL, *forw_packet_pos = NULL;
	struct hlist_node *tmp_node;
	struct batman_ogm_packet *batman_ogm_packet;
	bool direct_link;

	batman_ogm_packet = (struct batman_ogm_packet *)packet_buff;
	direct_link = batman_ogm_packet->flags & DIRECTLINK ? 1 : 0;

	/* find position for the packet in the forward queue */
	spin_lock_bh(&bat_priv->forw_bat_list_lock);
	/* own packets are not to be aggregated */
	if ((atomic_read(&bat_priv->aggregated_ogms)) && (!own_packet)) {
		hlist_for_each_entry(forw_packet_pos, tmp_node,
				     &bat_priv->forw_bat_list, list) {
			if (bat_iv_ogm_can_aggregate(batman_ogm_packet,
						     bat_priv, packet_len,
						     send_time, direct_link,
						     if_incoming,
						     forw_packet_pos)) {
				forw_packet_aggr = forw_packet_pos;
				break;
			}
		}
	}

	/* nothing to aggregate with - either aggregation disabled or no
	 * suitable aggregation packet found */
	if (!forw_packet_aggr) {
		/* the following section can run without the lock */
		spin_unlock_bh(&bat_priv->forw_bat_list_lock);

		/**
		 * if we could not aggregate this packet with one of the others
		 * we hold it back for a while, so that it might be aggregated
		 * later on
		 */
		if ((!own_packet) &&
		    (atomic_read(&bat_priv->aggregated_ogms)))
			send_time += msecs_to_jiffies(MAX_AGGREGATION_MS);

		bat_iv_ogm_aggregate_new(packet_buff, packet_len,
					 send_time, direct_link,
					 if_incoming, own_packet);
	} else {
		bat_iv_ogm_aggregate(forw_packet_aggr, packet_buff,
				     packet_len, direct_link);
		spin_unlock_bh(&bat_priv->forw_bat_list_lock);
	}
}

static void bat_iv_ogm_forward(struct orig_node *orig_node,
			       const struct ethhdr *ethhdr,
			       struct batman_ogm_packet *batman_ogm_packet,
			       bool is_single_hop_neigh,
			       bool is_from_best_next_hop,
			       struct hard_iface *if_incoming)
{
	struct bat_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	uint8_t tt_num_changes;

	if (batman_ogm_packet->header.ttl <= 1) {
		bat_dbg(DBG_BATMAN, bat_priv, "ttl exceeded\n");
		return;
	}

	if (!is_from_best_next_hop) {
		/* Mark the forwarded packet when it is not coming from our
		 * best next hop. We still need to forward the packet for our
		 * neighbor link quality detection to work in case the packet
		 * originated from a single hop neighbor. Otherwise we can
		 * simply drop the ogm.
		 */
		if (is_single_hop_neigh)
			batman_ogm_packet->flags |= NOT_BEST_NEXT_HOP;
		else
			return;
	}

	tt_num_changes = batman_ogm_packet->tt_num_changes;

	batman_ogm_packet->header.ttl--;
	memcpy(batman_ogm_packet->prev_sender, ethhdr->h_source, ETH_ALEN);

	/* apply hop penalty */
	batman_ogm_packet->tq = hop_penalty(batman_ogm_packet->tq, bat_priv);

	bat_dbg(DBG_BATMAN, bat_priv,
		"Forwarding packet: tq: %i, ttl: %i\n",
		batman_ogm_packet->tq, batman_ogm_packet->header.ttl);

	/* switch of primaries first hop flag when forwarding */
	batman_ogm_packet->flags &= ~PRIMARIES_FIRST_HOP;
	if (is_single_hop_neigh)
		batman_ogm_packet->flags |= DIRECTLINK;
	else
		batman_ogm_packet->flags &= ~DIRECTLINK;

	bat_iv_ogm_queue_add(bat_priv, (unsigned char *)batman_ogm_packet,
			     BATMAN_OGM_HLEN + tt_len(tt_num_changes),
			     if_incoming, 0, bat_iv_ogm_fwd_send_time());
}

static void bat_iv_ogm_schedule(struct hard_iface *hard_iface)
{
	struct bat_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct batman_ogm_packet *batman_ogm_packet;
	struct hard_iface *primary_if;
	int vis_server, tt_num_changes = 0;

	vis_server = atomic_read(&bat_priv->vis_mode);
	primary_if = primary_if_get_selected(bat_priv);

	if (hard_iface == primary_if)
		tt_num_changes = batadv_tt_append_diff(bat_priv,
						       &hard_iface->packet_buff,
						       &hard_iface->packet_len,
						       BATMAN_OGM_HLEN);

	batman_ogm_packet = (struct batman_ogm_packet *)hard_iface->packet_buff;

	/* change sequence number to network order */
	batman_ogm_packet->seqno =
			htonl((uint32_t)atomic_read(&hard_iface->seqno));
	atomic_inc(&hard_iface->seqno);

	batman_ogm_packet->ttvn = atomic_read(&bat_priv->ttvn);
	batman_ogm_packet->tt_crc = htons(bat_priv->tt_crc);
	if (tt_num_changes >= 0)
		batman_ogm_packet->tt_num_changes = tt_num_changes;

	if (vis_server == VIS_TYPE_SERVER_SYNC)
		batman_ogm_packet->flags |= VIS_SERVER;
	else
		batman_ogm_packet->flags &= ~VIS_SERVER;

	if ((hard_iface == primary_if) &&
	    (atomic_read(&bat_priv->gw_mode) == GW_MODE_SERVER))
		batman_ogm_packet->gw_flags =
				(uint8_t)atomic_read(&bat_priv->gw_bandwidth);
	else
		batman_ogm_packet->gw_flags = NO_FLAGS;

	batadv_slide_own_bcast_window(hard_iface);
	bat_iv_ogm_queue_add(bat_priv, hard_iface->packet_buff,
			     hard_iface->packet_len, hard_iface, 1,
			     bat_iv_ogm_emit_send_time(bat_priv));

	if (primary_if)
		hardif_free_ref(primary_if);
}

static void bat_iv_ogm_orig_update(struct bat_priv *bat_priv,
				   struct orig_node *orig_node,
				   const struct ethhdr *ethhdr,
				   const struct batman_ogm_packet
							*batman_ogm_packet,
				   struct hard_iface *if_incoming,
				   const unsigned char *tt_buff,
				   int is_duplicate)
{
	struct neigh_node *neigh_node = NULL, *tmp_neigh_node = NULL;
	struct neigh_node *router = NULL;
	struct orig_node *orig_node_tmp;
	struct hlist_node *node;
	uint8_t bcast_own_sum_orig, bcast_own_sum_neigh;

	bat_dbg(DBG_BATMAN, bat_priv,
		"update_originator(): Searching and updating originator entry of received packet\n");

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node, node,
				 &orig_node->neigh_list, list) {
		if (compare_eth(tmp_neigh_node->addr, ethhdr->h_source) &&
		    (tmp_neigh_node->if_incoming == if_incoming) &&
		     atomic_inc_not_zero(&tmp_neigh_node->refcount)) {
			if (neigh_node)
				batadv_neigh_node_free_ref(neigh_node);
			neigh_node = tmp_neigh_node;
			continue;
		}

		if (is_duplicate)
			continue;

		spin_lock_bh(&tmp_neigh_node->lq_update_lock);
		batadv_ring_buffer_set(tmp_neigh_node->tq_recv,
				       &tmp_neigh_node->tq_index, 0);
		tmp_neigh_node->tq_avg =
			batadv_ring_buffer_avg(tmp_neigh_node->tq_recv);
		spin_unlock_bh(&tmp_neigh_node->lq_update_lock);
	}

	if (!neigh_node) {
		struct orig_node *orig_tmp;

		orig_tmp = batadv_get_orig_node(bat_priv, ethhdr->h_source);
		if (!orig_tmp)
			goto unlock;

		neigh_node = bat_iv_ogm_neigh_new(if_incoming, ethhdr->h_source,
						  orig_node, orig_tmp,
						  batman_ogm_packet->seqno);

		batadv_orig_node_free_ref(orig_tmp);
		if (!neigh_node)
			goto unlock;
	} else
		bat_dbg(DBG_BATMAN, bat_priv,
			"Updating existing last-hop neighbor of originator\n");

	rcu_read_unlock();

	orig_node->flags = batman_ogm_packet->flags;
	neigh_node->last_seen = jiffies;

	spin_lock_bh(&neigh_node->lq_update_lock);
	batadv_ring_buffer_set(neigh_node->tq_recv,
			       &neigh_node->tq_index,
			       batman_ogm_packet->tq);
	neigh_node->tq_avg = batadv_ring_buffer_avg(neigh_node->tq_recv);
	spin_unlock_bh(&neigh_node->lq_update_lock);

	if (!is_duplicate) {
		orig_node->last_ttl = batman_ogm_packet->header.ttl;
		neigh_node->last_ttl = batman_ogm_packet->header.ttl;
	}

	batadv_bonding_candidate_add(orig_node, neigh_node);

	/* if this neighbor already is our next hop there is nothing
	 * to change */
	router = batadv_orig_node_get_router(orig_node);
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

	batadv_update_route(bat_priv, orig_node, neigh_node);

update_tt:
	/* I have to check for transtable changes only if the OGM has been
	 * sent through a primary interface */
	if (((batman_ogm_packet->orig != ethhdr->h_source) &&
	     (batman_ogm_packet->header.ttl > 2)) ||
	    (batman_ogm_packet->flags & PRIMARIES_FIRST_HOP))
		tt_update_orig(bat_priv, orig_node, tt_buff,
			       batman_ogm_packet->tt_num_changes,
			       batman_ogm_packet->ttvn,
			       ntohs(batman_ogm_packet->tt_crc));

	if (orig_node->gw_flags != batman_ogm_packet->gw_flags)
		batadv_gw_node_update(bat_priv, orig_node,
				      batman_ogm_packet->gw_flags);

	orig_node->gw_flags = batman_ogm_packet->gw_flags;

	/* restart gateway selection if fast or late switching was enabled */
	if ((orig_node->gw_flags) &&
	    (atomic_read(&bat_priv->gw_mode) == GW_MODE_CLIENT) &&
	    (atomic_read(&bat_priv->gw_sel_class) > 2))
		batadv_gw_check_election(bat_priv, orig_node);

	goto out;

unlock:
	rcu_read_unlock();
out:
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	if (router)
		batadv_neigh_node_free_ref(router);
}

static int bat_iv_ogm_calc_tq(struct orig_node *orig_node,
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
		neigh_node = bat_iv_ogm_neigh_new(if_incoming,
						  orig_neigh_node->orig,
						  orig_neigh_node,
						  orig_neigh_node,
						  batman_ogm_packet->seqno);

	if (!neigh_node)
		goto out;

	/* if orig_node is direct neighbor update neigh_node last_seen */
	if (orig_node == orig_neigh_node)
		neigh_node->last_seen = jiffies;

	orig_node->last_seen = jiffies;

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

	/* 1 - ((1-x) ** 3), normalized to TQ_MAX_VALUE this does
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
		"bidirectional: orig = %-15pM neigh = %-15pM => own_bcast = %2i, real recv = %2i, local tq: %3i, asym_penalty: %3i, total tq: %3i\n",
		orig_node->orig, orig_neigh_node->orig, total_count,
		neigh_rq_count, tq_own,	tq_asym_penalty, batman_ogm_packet->tq);

	/* if link has the minimum required transmission quality
	 * consider it bidirectional */
	if (batman_ogm_packet->tq >= TQ_TOTAL_BIDRECT_LIMIT)
		ret = 1;

out:
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
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
static int bat_iv_ogm_update_seqnos(const struct ethhdr *ethhdr,
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
	uint32_t seqno = ntohl(batman_ogm_packet->seqno);

	orig_node = batadv_get_orig_node(bat_priv, batman_ogm_packet->orig);
	if (!orig_node)
		return 0;

	spin_lock_bh(&orig_node->ogm_cnt_lock);
	seq_diff = seqno - orig_node->last_real_seqno;

	/* signalize caller that the packet is to be dropped. */
	if (!hlist_empty(&orig_node->neigh_list) &&
	    batadv_window_protected(bat_priv, seq_diff,
				    &orig_node->batman_seqno_reset))
		goto out;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node, node,
				 &orig_node->neigh_list, list) {

		is_duplicate |= bat_test_bit(tmp_neigh_node->real_bits,
					     orig_node->last_real_seqno,
					     seqno);

		if (compare_eth(tmp_neigh_node->addr, ethhdr->h_source) &&
		    (tmp_neigh_node->if_incoming == if_incoming))
			set_mark = 1;
		else
			set_mark = 0;

		/* if the window moved, set the update flag. */
		need_update |= batadv_bit_get_packet(bat_priv,
						     tmp_neigh_node->real_bits,
						     seq_diff, set_mark);

		tmp_neigh_node->real_packet_count =
			bitmap_weight(tmp_neigh_node->real_bits,
				      TQ_LOCAL_WINDOW_SIZE);
	}
	rcu_read_unlock();

	if (need_update) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"updating last_seqno: old %u, new %u\n",
			orig_node->last_real_seqno, seqno);
		orig_node->last_real_seqno = seqno;
	}

	ret = is_duplicate;

out:
	spin_unlock_bh(&orig_node->ogm_cnt_lock);
	batadv_orig_node_free_ref(orig_node);
	return ret;
}

static void bat_iv_ogm_process(const struct ethhdr *ethhdr,
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
	int is_broadcast = 0, is_bidirectional;
	bool is_single_hop_neigh = false;
	bool is_from_best_next_hop = false;
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
	if (batman_ogm_packet->header.packet_type != BAT_IV_OGM)
		return;

	/* could be changed by schedule_own_packet() */
	if_incoming_seqno = atomic_read(&if_incoming->seqno);

	has_directlink_flag = (batman_ogm_packet->flags & DIRECTLINK ? 1 : 0);

	if (compare_eth(ethhdr->h_source, batman_ogm_packet->orig))
		is_single_hop_neigh = true;

	bat_dbg(DBG_BATMAN, bat_priv,
		"Received BATMAN packet via NB: %pM, IF: %s [%pM] (from OG: %pM, via prev OG: %pM, seqno %u, ttvn %u, crc %u, changes %u, td %d, TTL %d, V %d, IDF %d)\n",
		ethhdr->h_source, if_incoming->net_dev->name,
		if_incoming->net_dev->dev_addr, batman_ogm_packet->orig,
		batman_ogm_packet->prev_sender, ntohl(batman_ogm_packet->seqno),
		batman_ogm_packet->ttvn, ntohs(batman_ogm_packet->tt_crc),
		batman_ogm_packet->tt_num_changes, batman_ogm_packet->tq,
		batman_ogm_packet->header.ttl,
		batman_ogm_packet->header.version, has_directlink_flag);

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

	if (batman_ogm_packet->header.version != COMPAT_VERSION) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: incompatible batman version (%i)\n",
			batman_ogm_packet->header.version);
		return;
	}

	if (is_my_addr) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: received my own broadcast (sender: %pM)\n",
			ethhdr->h_source);
		return;
	}

	if (is_broadcast) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: ignoring all packets with broadcast source addr (sender: %pM)\n",
			ethhdr->h_source);
		return;
	}

	if (is_my_orig) {
		unsigned long *word;
		int offset;

		orig_neigh_node = batadv_get_orig_node(bat_priv,
						       ethhdr->h_source);
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
			bat_set_bit(word,
				    if_incoming_seqno -
					ntohl(batman_ogm_packet->seqno) - 2);
			orig_neigh_node->bcast_own_sum[if_incoming->if_num] =
				bitmap_weight(word, TQ_LOCAL_WINDOW_SIZE);
			spin_unlock_bh(&orig_neigh_node->ogm_cnt_lock);
		}

		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: originator packet from myself (via neighbor)\n");
		batadv_orig_node_free_ref(orig_neigh_node);
		return;
	}

	if (is_my_oldorig) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: ignoring all rebroadcast echos (sender: %pM)\n",
			ethhdr->h_source);
		return;
	}

	if (batman_ogm_packet->flags & NOT_BEST_NEXT_HOP) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: ignoring all packets not forwarded from the best next hop (sender: %pM)\n",
			ethhdr->h_source);
		return;
	}

	orig_node = batadv_get_orig_node(bat_priv, batman_ogm_packet->orig);
	if (!orig_node)
		return;

	is_duplicate = bat_iv_ogm_update_seqnos(ethhdr, batman_ogm_packet,
						if_incoming);

	if (is_duplicate == -1) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: packet within seqno protection time (sender: %pM)\n",
			ethhdr->h_source);
		goto out;
	}

	if (batman_ogm_packet->tq == 0) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: originator packet with tq equal 0\n");
		goto out;
	}

	router = batadv_orig_node_get_router(orig_node);
	if (router)
		router_router = batadv_orig_node_get_router(router->orig_node);

	if ((router && router->tq_avg != 0) &&
	    (compare_eth(router->addr, ethhdr->h_source)))
		is_from_best_next_hop = true;

	/* avoid temporary routing loops */
	if (router && router_router &&
	    (compare_eth(router->addr, batman_ogm_packet->prev_sender)) &&
	    !(compare_eth(batman_ogm_packet->orig,
			  batman_ogm_packet->prev_sender)) &&
	    (compare_eth(router->addr, router_router->addr))) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: ignoring all rebroadcast packets that may make me loop (sender: %pM)\n",
			ethhdr->h_source);
		goto out;
	}

	/* if sender is a direct neighbor the sender mac equals
	 * originator mac */
	orig_neigh_node = (is_single_hop_neigh ?
			   orig_node :
			   batadv_get_orig_node(bat_priv, ethhdr->h_source));
	if (!orig_neigh_node)
		goto out;

	orig_neigh_router = batadv_orig_node_get_router(orig_neigh_node);

	/* drop packet if sender is not a direct neighbor and if we
	 * don't route towards it */
	if (!is_single_hop_neigh && (!orig_neigh_router)) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: OGM via unknown neighbor!\n");
		goto out_neigh;
	}

	is_bidirectional = bat_iv_ogm_calc_tq(orig_node, orig_neigh_node,
					      batman_ogm_packet, if_incoming);

	batadv_bonding_save_primary(orig_node, orig_neigh_node,
				    batman_ogm_packet);

	/* update ranking if it is not a duplicate or has the same
	 * seqno and similar ttl as the non-duplicate */
	if (is_bidirectional &&
	    (!is_duplicate ||
	     ((orig_node->last_real_seqno == ntohl(batman_ogm_packet->seqno)) &&
	      (orig_node->last_ttl - 3 <= batman_ogm_packet->header.ttl))))
		bat_iv_ogm_orig_update(bat_priv, orig_node, ethhdr,
				       batman_ogm_packet, if_incoming,
				       tt_buff, is_duplicate);

	/* is single hop (direct) neighbor */
	if (is_single_hop_neigh) {

		/* mark direct link on incoming interface */
		bat_iv_ogm_forward(orig_node, ethhdr, batman_ogm_packet,
				   is_single_hop_neigh, is_from_best_next_hop,
				   if_incoming);

		bat_dbg(DBG_BATMAN, bat_priv,
			"Forwarding packet: rebroadcast neighbor packet with direct link flag\n");
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
	bat_iv_ogm_forward(orig_node, ethhdr, batman_ogm_packet,
			   is_single_hop_neigh, is_from_best_next_hop,
			   if_incoming);

out_neigh:
	if ((orig_neigh_node) && (!is_single_hop_neigh))
		batadv_orig_node_free_ref(orig_neigh_node);
out:
	if (router)
		batadv_neigh_node_free_ref(router);
	if (router_router)
		batadv_neigh_node_free_ref(router_router);
	if (orig_neigh_router)
		batadv_neigh_node_free_ref(orig_neigh_router);

	batadv_orig_node_free_ref(orig_node);
}

static int bat_iv_ogm_receive(struct sk_buff *skb,
			      struct hard_iface *if_incoming)
{
	struct bat_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batman_ogm_packet *batman_ogm_packet;
	struct ethhdr *ethhdr;
	int buff_pos = 0, packet_len;
	unsigned char *tt_buff, *packet_buff;
	bool ret;

	ret = batadv_check_management_packet(skb, if_incoming, BATMAN_OGM_HLEN);
	if (!ret)
		return NET_RX_DROP;

	/* did we receive a B.A.T.M.A.N. IV OGM packet on an interface
	 * that does not have B.A.T.M.A.N. IV enabled ?
	 */
	if (bat_priv->bat_algo_ops->bat_ogm_emit != bat_iv_ogm_emit)
		return NET_RX_DROP;

	batadv_inc_counter(bat_priv, BAT_CNT_MGMT_RX);
	batadv_add_counter(bat_priv, BAT_CNT_MGMT_RX_BYTES,
			   skb->len + ETH_HLEN);

	packet_len = skb_headlen(skb);
	ethhdr = (struct ethhdr *)skb_mac_header(skb);
	packet_buff = skb->data;
	batman_ogm_packet = (struct batman_ogm_packet *)packet_buff;

	/* unpack the aggregated packets and process them one by one */
	do {
		tt_buff = packet_buff + buff_pos + BATMAN_OGM_HLEN;

		bat_iv_ogm_process(ethhdr, batman_ogm_packet,
				   tt_buff, if_incoming);

		buff_pos += BATMAN_OGM_HLEN +
				tt_len(batman_ogm_packet->tt_num_changes);

		batman_ogm_packet = (struct batman_ogm_packet *)
						(packet_buff + buff_pos);
	} while (bat_iv_ogm_aggr_packet(buff_pos, packet_len,
					batman_ogm_packet->tt_num_changes));

	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

static struct bat_algo_ops batman_iv __read_mostly = {
	.name = "BATMAN_IV",
	.bat_iface_enable = bat_iv_ogm_iface_enable,
	.bat_iface_disable = bat_iv_ogm_iface_disable,
	.bat_iface_update_mac = bat_iv_ogm_iface_update_mac,
	.bat_primary_iface_set = bat_iv_ogm_primary_iface_set,
	.bat_ogm_schedule = bat_iv_ogm_schedule,
	.bat_ogm_emit = bat_iv_ogm_emit,
};

int __init batadv_iv_init(void)
{
	int ret;

	/* batman originator packet */
	ret = recv_handler_register(BAT_IV_OGM, bat_iv_ogm_receive);
	if (ret < 0)
		goto out;

	ret = bat_algo_register(&batman_iv);
	if (ret < 0)
		goto handler_unregister;

	goto out;

handler_unregister:
	recv_handler_unregister(BAT_IV_OGM);
out:
	return ret;
}
