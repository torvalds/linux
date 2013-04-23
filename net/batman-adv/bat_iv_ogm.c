/* Copyright (C) 2007-2013 B.A.T.M.A.N. contributors:
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
#include "translation-table.h"
#include "originator.h"
#include "routing.h"
#include "gateway_common.h"
#include "gateway_client.h"
#include "hard-interface.h"
#include "send.h"
#include "bat_algo.h"
#include "network-coding.h"


/**
 * batadv_dup_status - duplicate status
 * @BATADV_NO_DUP: the packet is a duplicate
 * @BATADV_ORIG_DUP: OGM is a duplicate in the originator (but not for the
 *  neighbor)
 * @BATADV_NEIGH_DUP: OGM is a duplicate for the neighbor
 * @BATADV_PROTECTED: originator is currently protected (after reboot)
 */
enum batadv_dup_status {
	BATADV_NO_DUP = 0,
	BATADV_ORIG_DUP,
	BATADV_NEIGH_DUP,
	BATADV_PROTECTED,
};

/**
 * batadv_ring_buffer_set - update the ring buffer with the given value
 * @lq_recv: pointer to the ring buffer
 * @lq_index: index to store the value at
 * @value: value to store in the ring buffer
 */
static void batadv_ring_buffer_set(uint8_t lq_recv[], uint8_t *lq_index,
				   uint8_t value)
{
	lq_recv[*lq_index] = value;
	*lq_index = (*lq_index + 1) % BATADV_TQ_GLOBAL_WINDOW_SIZE;
}

/**
 * batadv_ring_buffer_set - compute the average of all non-zero values stored
 * in the given ring buffer
 * @lq_recv: pointer to the ring buffer
 *
 * Returns computed average value.
 */
static uint8_t batadv_ring_buffer_avg(const uint8_t lq_recv[])
{
	const uint8_t *ptr;
	uint16_t count = 0, i = 0, sum = 0;

	ptr = lq_recv;

	while (i < BATADV_TQ_GLOBAL_WINDOW_SIZE) {
		if (*ptr != 0) {
			count++;
			sum += *ptr;
		}

		i++;
		ptr++;
	}

	if (count == 0)
		return 0;

	return (uint8_t)(sum / count);
}

static struct batadv_neigh_node *
batadv_iv_ogm_neigh_new(struct batadv_hard_iface *hard_iface,
			const uint8_t *neigh_addr,
			struct batadv_orig_node *orig_node,
			struct batadv_orig_node *orig_neigh)
{
	struct batadv_neigh_node *neigh_node;

	neigh_node = batadv_neigh_node_new(hard_iface, neigh_addr);
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

static int batadv_iv_ogm_iface_enable(struct batadv_hard_iface *hard_iface)
{
	struct batadv_ogm_packet *batadv_ogm_packet;
	unsigned char *ogm_buff;
	uint32_t random_seqno;
	int res = -ENOMEM;

	/* randomize initial seqno to avoid collision */
	get_random_bytes(&random_seqno, sizeof(random_seqno));
	atomic_set(&hard_iface->bat_iv.ogm_seqno, random_seqno);

	hard_iface->bat_iv.ogm_buff_len = BATADV_OGM_HLEN;
	ogm_buff = kmalloc(hard_iface->bat_iv.ogm_buff_len, GFP_ATOMIC);
	if (!ogm_buff)
		goto out;

	hard_iface->bat_iv.ogm_buff = ogm_buff;

	batadv_ogm_packet = (struct batadv_ogm_packet *)ogm_buff;
	batadv_ogm_packet->header.packet_type = BATADV_IV_OGM;
	batadv_ogm_packet->header.version = BATADV_COMPAT_VERSION;
	batadv_ogm_packet->header.ttl = 2;
	batadv_ogm_packet->flags = BATADV_NO_FLAGS;
	batadv_ogm_packet->reserved = 0;
	batadv_ogm_packet->tq = BATADV_TQ_MAX_VALUE;

	res = 0;

out:
	return res;
}

static void batadv_iv_ogm_iface_disable(struct batadv_hard_iface *hard_iface)
{
	kfree(hard_iface->bat_iv.ogm_buff);
	hard_iface->bat_iv.ogm_buff = NULL;
}

static void batadv_iv_ogm_iface_update_mac(struct batadv_hard_iface *hard_iface)
{
	struct batadv_ogm_packet *batadv_ogm_packet;
	unsigned char *ogm_buff = hard_iface->bat_iv.ogm_buff;

	batadv_ogm_packet = (struct batadv_ogm_packet *)ogm_buff;
	memcpy(batadv_ogm_packet->orig,
	       hard_iface->net_dev->dev_addr, ETH_ALEN);
	memcpy(batadv_ogm_packet->prev_sender,
	       hard_iface->net_dev->dev_addr, ETH_ALEN);
}

static void
batadv_iv_ogm_primary_iface_set(struct batadv_hard_iface *hard_iface)
{
	struct batadv_ogm_packet *batadv_ogm_packet;
	unsigned char *ogm_buff = hard_iface->bat_iv.ogm_buff;

	batadv_ogm_packet = (struct batadv_ogm_packet *)ogm_buff;
	batadv_ogm_packet->flags = BATADV_PRIMARIES_FIRST_HOP;
	batadv_ogm_packet->header.ttl = BATADV_TTL;
}

/* when do we schedule our own ogm to be sent */
static unsigned long
batadv_iv_ogm_emit_send_time(const struct batadv_priv *bat_priv)
{
	unsigned int msecs;

	msecs = atomic_read(&bat_priv->orig_interval) - BATADV_JITTER;
	msecs += prandom_u32() % (2 * BATADV_JITTER);

	return jiffies + msecs_to_jiffies(msecs);
}

/* when do we schedule a ogm packet to be sent */
static unsigned long batadv_iv_ogm_fwd_send_time(void)
{
	return jiffies + msecs_to_jiffies(prandom_u32() % (BATADV_JITTER / 2));
}

/* apply hop penalty for a normal link */
static uint8_t batadv_hop_penalty(uint8_t tq,
				  const struct batadv_priv *bat_priv)
{
	int hop_penalty = atomic_read(&bat_priv->hop_penalty);
	int new_tq;

	new_tq = tq * (BATADV_TQ_MAX_VALUE - hop_penalty);
	new_tq /= BATADV_TQ_MAX_VALUE;

	return new_tq;
}

/* is there another aggregated packet here? */
static int batadv_iv_ogm_aggr_packet(int buff_pos, int packet_len,
				     __be16 tvlv_len)
{
	int next_buff_pos = 0;

	next_buff_pos += buff_pos + BATADV_OGM_HLEN;
	next_buff_pos += ntohs(tvlv_len);

	return (next_buff_pos <= packet_len) &&
	       (next_buff_pos <= BATADV_MAX_AGGREGATION_BYTES);
}

/* send a batman ogm to a given interface */
static void batadv_iv_ogm_send_to_if(struct batadv_forw_packet *forw_packet,
				     struct batadv_hard_iface *hard_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	char *fwd_str;
	uint8_t packet_num;
	int16_t buff_pos;
	struct batadv_ogm_packet *batadv_ogm_packet;
	struct sk_buff *skb;
	uint8_t *packet_pos;

	if (hard_iface->if_status != BATADV_IF_ACTIVE)
		return;

	packet_num = 0;
	buff_pos = 0;
	packet_pos = forw_packet->skb->data;
	batadv_ogm_packet = (struct batadv_ogm_packet *)packet_pos;

	/* adjust all flags and log packets */
	while (batadv_iv_ogm_aggr_packet(buff_pos, forw_packet->packet_len,
					 batadv_ogm_packet->tvlv_len)) {
		/* we might have aggregated direct link packets with an
		 * ordinary base packet
		 */
		if (forw_packet->direct_link_flags & BIT(packet_num) &&
		    forw_packet->if_incoming == hard_iface)
			batadv_ogm_packet->flags |= BATADV_DIRECTLINK;
		else
			batadv_ogm_packet->flags &= ~BATADV_DIRECTLINK;

		if (packet_num > 0 || !forw_packet->own)
			fwd_str = "Forwarding";
		else
			fwd_str = "Sending own";

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "%s %spacket (originator %pM, seqno %u, TQ %d, TTL %d, IDF %s) on interface %s [%pM]\n",
			   fwd_str, (packet_num > 0 ? "aggregated " : ""),
			   batadv_ogm_packet->orig,
			   ntohl(batadv_ogm_packet->seqno),
			   batadv_ogm_packet->tq, batadv_ogm_packet->header.ttl,
			   (batadv_ogm_packet->flags & BATADV_DIRECTLINK ?
			    "on" : "off"),
			   hard_iface->net_dev->name,
			   hard_iface->net_dev->dev_addr);

		buff_pos += BATADV_OGM_HLEN;
		buff_pos += ntohs(batadv_ogm_packet->tvlv_len);
		packet_num++;
		packet_pos = forw_packet->skb->data + buff_pos;
		batadv_ogm_packet = (struct batadv_ogm_packet *)packet_pos;
	}

	/* create clone because function is called more than once */
	skb = skb_clone(forw_packet->skb, GFP_ATOMIC);
	if (skb) {
		batadv_inc_counter(bat_priv, BATADV_CNT_MGMT_TX);
		batadv_add_counter(bat_priv, BATADV_CNT_MGMT_TX_BYTES,
				   skb->len + ETH_HLEN);
		batadv_send_skb_packet(skb, hard_iface, batadv_broadcast_addr);
	}
}

/* send a batman ogm packet */
static void batadv_iv_ogm_emit(struct batadv_forw_packet *forw_packet)
{
	struct batadv_hard_iface *hard_iface;
	struct net_device *soft_iface;
	struct batadv_priv *bat_priv;
	struct batadv_hard_iface *primary_if = NULL;
	struct batadv_ogm_packet *batadv_ogm_packet;
	unsigned char directlink;
	uint8_t *packet_pos;

	packet_pos = forw_packet->skb->data;
	batadv_ogm_packet = (struct batadv_ogm_packet *)packet_pos;
	directlink = (batadv_ogm_packet->flags & BATADV_DIRECTLINK ? 1 : 0);

	if (!forw_packet->if_incoming) {
		pr_err("Error - can't forward packet: incoming iface not specified\n");
		goto out;
	}

	soft_iface = forw_packet->if_incoming->soft_iface;
	bat_priv = netdev_priv(soft_iface);

	if (forw_packet->if_incoming->if_status != BATADV_IF_ACTIVE)
		goto out;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	/* multihomed peer assumed
	 * non-primary OGMs are only broadcasted on their interface
	 */
	if ((directlink && (batadv_ogm_packet->header.ttl == 1)) ||
	    (forw_packet->own && (forw_packet->if_incoming != primary_if))) {
		/* FIXME: what about aggregated packets ? */
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "%s packet (originator %pM, seqno %u, TTL %d) on interface %s [%pM]\n",
			   (forw_packet->own ? "Sending own" : "Forwarding"),
			   batadv_ogm_packet->orig,
			   ntohl(batadv_ogm_packet->seqno),
			   batadv_ogm_packet->header.ttl,
			   forw_packet->if_incoming->net_dev->name,
			   forw_packet->if_incoming->net_dev->dev_addr);

		/* skb is only used once and than forw_packet is free'd */
		batadv_send_skb_packet(forw_packet->skb,
				       forw_packet->if_incoming,
				       batadv_broadcast_addr);
		forw_packet->skb = NULL;

		goto out;
	}

	/* broadcast on every interface */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->soft_iface != soft_iface)
			continue;

		batadv_iv_ogm_send_to_if(forw_packet, hard_iface);
	}
	rcu_read_unlock();

out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
}

/* return true if new_packet can be aggregated with forw_packet */
static bool
batadv_iv_ogm_can_aggregate(const struct batadv_ogm_packet *new_bat_ogm_packet,
			    struct batadv_priv *bat_priv,
			    int packet_len, unsigned long send_time,
			    bool directlink,
			    const struct batadv_hard_iface *if_incoming,
			    const struct batadv_forw_packet *forw_packet)
{
	struct batadv_ogm_packet *batadv_ogm_packet;
	int aggregated_bytes = forw_packet->packet_len + packet_len;
	struct batadv_hard_iface *primary_if = NULL;
	bool res = false;
	unsigned long aggregation_end_time;

	batadv_ogm_packet = (struct batadv_ogm_packet *)forw_packet->skb->data;
	aggregation_end_time = send_time;
	aggregation_end_time += msecs_to_jiffies(BATADV_MAX_AGGREGATION_MS);

	/* we can aggregate the current packet to this aggregated packet
	 * if:
	 *
	 * - the send time is within our MAX_AGGREGATION_MS time
	 * - the resulting packet wont be bigger than
	 *   MAX_AGGREGATION_BYTES
	 */
	if (time_before(send_time, forw_packet->send_time) &&
	    time_after_eq(aggregation_end_time, forw_packet->send_time) &&
	    (aggregated_bytes <= BATADV_MAX_AGGREGATION_BYTES)) {
		/* check aggregation compatibility
		 * -> direct link packets are broadcasted on
		 *    their interface only
		 * -> aggregate packet if the current packet is
		 *    a "global" packet as well as the base
		 *    packet
		 */
		primary_if = batadv_primary_if_get_selected(bat_priv);
		if (!primary_if)
			goto out;

		/* packets without direct link flag and high TTL
		 * are flooded through the net
		 */
		if ((!directlink) &&
		    (!(batadv_ogm_packet->flags & BATADV_DIRECTLINK)) &&
		    (batadv_ogm_packet->header.ttl != 1) &&

		    /* own packets originating non-primary
		     * interfaces leave only that interface
		     */
		    ((!forw_packet->own) ||
		     (forw_packet->if_incoming == primary_if))) {
			res = true;
			goto out;
		}

		/* if the incoming packet is sent via this one
		 * interface only - we still can aggregate
		 */
		if ((directlink) &&
		    (new_bat_ogm_packet->header.ttl == 1) &&
		    (forw_packet->if_incoming == if_incoming) &&

		    /* packets from direct neighbors or
		     * own secondary interface packets
		     * (= secondary interface packets in general)
		     */
		    (batadv_ogm_packet->flags & BATADV_DIRECTLINK ||
		     (forw_packet->own &&
		      forw_packet->if_incoming != primary_if))) {
			res = true;
			goto out;
		}
	}

out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	return res;
}

/* create a new aggregated packet and add this packet to it */
static void batadv_iv_ogm_aggregate_new(const unsigned char *packet_buff,
					int packet_len, unsigned long send_time,
					bool direct_link,
					struct batadv_hard_iface *if_incoming,
					int own_packet)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_forw_packet *forw_packet_aggr;
	unsigned char *skb_buff;
	unsigned int skb_size;

	if (!atomic_inc_not_zero(&if_incoming->refcount))
		return;

	/* own packet should always be scheduled */
	if (!own_packet) {
		if (!batadv_atomic_dec_not_zero(&bat_priv->batman_queue_left)) {
			batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
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
	    (packet_len < BATADV_MAX_AGGREGATION_BYTES))
		skb_size = BATADV_MAX_AGGREGATION_BYTES;
	else
		skb_size = packet_len;

	skb_size += ETH_HLEN;

	forw_packet_aggr->skb = netdev_alloc_skb_ip_align(NULL, skb_size);
	if (!forw_packet_aggr->skb) {
		if (!own_packet)
			atomic_inc(&bat_priv->batman_queue_left);
		kfree(forw_packet_aggr);
		goto out;
	}
	forw_packet_aggr->skb->priority = TC_PRIO_CONTROL;
	skb_reserve(forw_packet_aggr->skb, ETH_HLEN);

	skb_buff = skb_put(forw_packet_aggr->skb, packet_len);
	forw_packet_aggr->packet_len = packet_len;
	memcpy(skb_buff, packet_buff, packet_len);

	forw_packet_aggr->own = own_packet;
	forw_packet_aggr->if_incoming = if_incoming;
	forw_packet_aggr->num_packets = 0;
	forw_packet_aggr->direct_link_flags = BATADV_NO_FLAGS;
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
	queue_delayed_work(batadv_event_workqueue,
			   &forw_packet_aggr->delayed_work,
			   send_time - jiffies);

	return;
out:
	batadv_hardif_free_ref(if_incoming);
}

/* aggregate a new packet into the existing ogm packet */
static void batadv_iv_ogm_aggregate(struct batadv_forw_packet *forw_packet_aggr,
				    const unsigned char *packet_buff,
				    int packet_len, bool direct_link)
{
	unsigned char *skb_buff;
	unsigned long new_direct_link_flag;

	skb_buff = skb_put(forw_packet_aggr->skb, packet_len);
	memcpy(skb_buff, packet_buff, packet_len);
	forw_packet_aggr->packet_len += packet_len;
	forw_packet_aggr->num_packets++;

	/* save packet direct link flag status */
	if (direct_link) {
		new_direct_link_flag = BIT(forw_packet_aggr->num_packets);
		forw_packet_aggr->direct_link_flags |= new_direct_link_flag;
	}
}

static void batadv_iv_ogm_queue_add(struct batadv_priv *bat_priv,
				    unsigned char *packet_buff,
				    int packet_len,
				    struct batadv_hard_iface *if_incoming,
				    int own_packet, unsigned long send_time)
{
	/* _aggr -> pointer to the packet we want to aggregate with
	 * _pos -> pointer to the position in the queue
	 */
	struct batadv_forw_packet *forw_packet_aggr = NULL;
	struct batadv_forw_packet *forw_packet_pos = NULL;
	struct batadv_ogm_packet *batadv_ogm_packet;
	bool direct_link;
	unsigned long max_aggregation_jiffies;

	batadv_ogm_packet = (struct batadv_ogm_packet *)packet_buff;
	direct_link = batadv_ogm_packet->flags & BATADV_DIRECTLINK ? 1 : 0;
	max_aggregation_jiffies = msecs_to_jiffies(BATADV_MAX_AGGREGATION_MS);

	/* find position for the packet in the forward queue */
	spin_lock_bh(&bat_priv->forw_bat_list_lock);
	/* own packets are not to be aggregated */
	if ((atomic_read(&bat_priv->aggregated_ogms)) && (!own_packet)) {
		hlist_for_each_entry(forw_packet_pos,
				     &bat_priv->forw_bat_list, list) {
			if (batadv_iv_ogm_can_aggregate(batadv_ogm_packet,
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
	 * suitable aggregation packet found
	 */
	if (!forw_packet_aggr) {
		/* the following section can run without the lock */
		spin_unlock_bh(&bat_priv->forw_bat_list_lock);

		/* if we could not aggregate this packet with one of the others
		 * we hold it back for a while, so that it might be aggregated
		 * later on
		 */
		if (!own_packet && atomic_read(&bat_priv->aggregated_ogms))
			send_time += max_aggregation_jiffies;

		batadv_iv_ogm_aggregate_new(packet_buff, packet_len,
					    send_time, direct_link,
					    if_incoming, own_packet);
	} else {
		batadv_iv_ogm_aggregate(forw_packet_aggr, packet_buff,
					packet_len, direct_link);
		spin_unlock_bh(&bat_priv->forw_bat_list_lock);
	}
}

static void batadv_iv_ogm_forward(struct batadv_orig_node *orig_node,
				  const struct ethhdr *ethhdr,
				  struct batadv_ogm_packet *batadv_ogm_packet,
				  bool is_single_hop_neigh,
				  bool is_from_best_next_hop,
				  struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	uint16_t tvlv_len;

	if (batadv_ogm_packet->header.ttl <= 1) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv, "ttl exceeded\n");
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
			batadv_ogm_packet->flags |= BATADV_NOT_BEST_NEXT_HOP;
		else
			return;
	}

	tvlv_len = ntohs(batadv_ogm_packet->tvlv_len);

	batadv_ogm_packet->header.ttl--;
	memcpy(batadv_ogm_packet->prev_sender, ethhdr->h_source, ETH_ALEN);

	/* apply hop penalty */
	batadv_ogm_packet->tq = batadv_hop_penalty(batadv_ogm_packet->tq,
						   bat_priv);

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Forwarding packet: tq: %i, ttl: %i\n",
		   batadv_ogm_packet->tq, batadv_ogm_packet->header.ttl);

	/* switch of primaries first hop flag when forwarding */
	batadv_ogm_packet->flags &= ~BATADV_PRIMARIES_FIRST_HOP;
	if (is_single_hop_neigh)
		batadv_ogm_packet->flags |= BATADV_DIRECTLINK;
	else
		batadv_ogm_packet->flags &= ~BATADV_DIRECTLINK;

	batadv_iv_ogm_queue_add(bat_priv, (unsigned char *)batadv_ogm_packet,
				BATADV_OGM_HLEN + tvlv_len,
				if_incoming, 0, batadv_iv_ogm_fwd_send_time());
}

/**
 * batadv_iv_ogm_slide_own_bcast_window - bitshift own OGM broadcast windows for
 * the given interface
 * @hard_iface: the interface for which the windows have to be shifted
 */
static void
batadv_iv_ogm_slide_own_bcast_window(struct batadv_hard_iface *hard_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct hlist_head *head;
	struct batadv_orig_node *orig_node;
	unsigned long *word;
	uint32_t i;
	size_t word_index;
	uint8_t *w;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, head, hash_entry) {
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

static void batadv_iv_ogm_schedule(struct batadv_hard_iface *hard_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	unsigned char **ogm_buff = &hard_iface->bat_iv.ogm_buff;
	struct batadv_ogm_packet *batadv_ogm_packet;
	struct batadv_hard_iface *primary_if;
	int *ogm_buff_len = &hard_iface->bat_iv.ogm_buff_len;
	int vis_server;
	uint32_t seqno;
	uint16_t tvlv_len = 0;

	vis_server = atomic_read(&bat_priv->vis_mode);
	primary_if = batadv_primary_if_get_selected(bat_priv);

	if (hard_iface == primary_if) {
		/* tt changes have to be committed before the tvlv data is
		 * appended as it may alter the tt tvlv container
		 */
		batadv_tt_local_commit_changes(bat_priv);
		tvlv_len = batadv_tvlv_container_ogm_append(bat_priv, ogm_buff,
							    ogm_buff_len,
							    BATADV_OGM_HLEN);
	}

	batadv_ogm_packet = (struct batadv_ogm_packet *)(*ogm_buff);
	batadv_ogm_packet->tvlv_len = htons(tvlv_len);

	/* change sequence number to network order */
	seqno = (uint32_t)atomic_read(&hard_iface->bat_iv.ogm_seqno);
	batadv_ogm_packet->seqno = htonl(seqno);
	atomic_inc(&hard_iface->bat_iv.ogm_seqno);

	if (vis_server == BATADV_VIS_TYPE_SERVER_SYNC)
		batadv_ogm_packet->flags |= BATADV_VIS_SERVER;
	else
		batadv_ogm_packet->flags &= ~BATADV_VIS_SERVER;

	batadv_iv_ogm_slide_own_bcast_window(hard_iface);
	batadv_iv_ogm_queue_add(bat_priv, hard_iface->bat_iv.ogm_buff,
				hard_iface->bat_iv.ogm_buff_len, hard_iface, 1,
				batadv_iv_ogm_emit_send_time(bat_priv));

	if (primary_if)
		batadv_hardif_free_ref(primary_if);
}

static void
batadv_iv_ogm_orig_update(struct batadv_priv *bat_priv,
			  struct batadv_orig_node *orig_node,
			  const struct ethhdr *ethhdr,
			  const struct batadv_ogm_packet *batadv_ogm_packet,
			  struct batadv_hard_iface *if_incoming,
			  const unsigned char *tt_buff,
			  enum batadv_dup_status dup_status)
{
	struct batadv_neigh_node *neigh_node = NULL, *tmp_neigh_node = NULL;
	struct batadv_neigh_node *router = NULL;
	struct batadv_orig_node *orig_node_tmp;
	int if_num;
	uint8_t sum_orig, sum_neigh;
	uint8_t *neigh_addr;
	uint8_t tq_avg;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "update_originator(): Searching and updating originator entry of received packet\n");

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node,
				 &orig_node->neigh_list, list) {
		neigh_addr = tmp_neigh_node->addr;
		if (batadv_compare_eth(neigh_addr, ethhdr->h_source) &&
		    tmp_neigh_node->if_incoming == if_incoming &&
		    atomic_inc_not_zero(&tmp_neigh_node->refcount)) {
			if (WARN(neigh_node, "too many matching neigh_nodes"))
				batadv_neigh_node_free_ref(neigh_node);
			neigh_node = tmp_neigh_node;
			continue;
		}

		if (dup_status != BATADV_NO_DUP)
			continue;

		spin_lock_bh(&tmp_neigh_node->lq_update_lock);
		batadv_ring_buffer_set(tmp_neigh_node->tq_recv,
				       &tmp_neigh_node->tq_index, 0);
		tq_avg = batadv_ring_buffer_avg(tmp_neigh_node->tq_recv);
		tmp_neigh_node->tq_avg = tq_avg;
		spin_unlock_bh(&tmp_neigh_node->lq_update_lock);
	}

	if (!neigh_node) {
		struct batadv_orig_node *orig_tmp;

		orig_tmp = batadv_get_orig_node(bat_priv, ethhdr->h_source);
		if (!orig_tmp)
			goto unlock;

		neigh_node = batadv_iv_ogm_neigh_new(if_incoming,
						     ethhdr->h_source,
						     orig_node, orig_tmp);

		batadv_orig_node_free_ref(orig_tmp);
		if (!neigh_node)
			goto unlock;
	} else
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Updating existing last-hop neighbor of originator\n");

	rcu_read_unlock();

	orig_node->flags = batadv_ogm_packet->flags;
	neigh_node->last_seen = jiffies;

	spin_lock_bh(&neigh_node->lq_update_lock);
	batadv_ring_buffer_set(neigh_node->tq_recv,
			       &neigh_node->tq_index,
			       batadv_ogm_packet->tq);
	neigh_node->tq_avg = batadv_ring_buffer_avg(neigh_node->tq_recv);
	spin_unlock_bh(&neigh_node->lq_update_lock);

	if (dup_status == BATADV_NO_DUP) {
		orig_node->last_ttl = batadv_ogm_packet->header.ttl;
		neigh_node->last_ttl = batadv_ogm_packet->header.ttl;
	}

	batadv_bonding_candidate_add(orig_node, neigh_node);

	/* if this neighbor already is our next hop there is nothing
	 * to change
	 */
	router = batadv_orig_node_get_router(orig_node);
	if (router == neigh_node)
		goto out;

	/* if this neighbor does not offer a better TQ we won't consider it */
	if (router && (router->tq_avg > neigh_node->tq_avg))
		goto out;

	/* if the TQ is the same and the link not more symmetric we
	 * won't consider it either
	 */
	if (router && (neigh_node->tq_avg == router->tq_avg)) {
		orig_node_tmp = router->orig_node;
		spin_lock_bh(&orig_node_tmp->ogm_cnt_lock);
		if_num = router->if_incoming->if_num;
		sum_orig = orig_node_tmp->bcast_own_sum[if_num];
		spin_unlock_bh(&orig_node_tmp->ogm_cnt_lock);

		orig_node_tmp = neigh_node->orig_node;
		spin_lock_bh(&orig_node_tmp->ogm_cnt_lock);
		if_num = neigh_node->if_incoming->if_num;
		sum_neigh = orig_node_tmp->bcast_own_sum[if_num];
		spin_unlock_bh(&orig_node_tmp->ogm_cnt_lock);

		if (sum_orig >= sum_neigh)
			goto out;
	}

	batadv_update_route(bat_priv, orig_node, neigh_node);
	goto out;

unlock:
	rcu_read_unlock();
out:
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	if (router)
		batadv_neigh_node_free_ref(router);
}

static int batadv_iv_ogm_calc_tq(struct batadv_orig_node *orig_node,
				 struct batadv_orig_node *orig_neigh_node,
				 struct batadv_ogm_packet *batadv_ogm_packet,
				 struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_neigh_node *neigh_node = NULL, *tmp_neigh_node;
	uint8_t total_count;
	uint8_t orig_eq_count, neigh_rq_count, neigh_rq_inv, tq_own;
	unsigned int neigh_rq_inv_cube, neigh_rq_max_cube;
	int tq_asym_penalty, inv_asym_penalty, ret = 0;
	unsigned int combined_tq;

	/* find corresponding one hop neighbor */
	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node,
				 &orig_neigh_node->neigh_list, list) {
		if (!batadv_compare_eth(tmp_neigh_node->addr,
					orig_neigh_node->orig))
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
		neigh_node = batadv_iv_ogm_neigh_new(if_incoming,
						     orig_neigh_node->orig,
						     orig_neigh_node,
						     orig_neigh_node);

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
	if (orig_eq_count > neigh_rq_count)
		total_count = neigh_rq_count;
	else
		total_count = orig_eq_count;

	/* if we have too few packets (too less data) we set tq_own to zero
	 * if we receive too few packets it is not considered bidirectional
	 */
	if (total_count < BATADV_TQ_LOCAL_BIDRECT_SEND_MINIMUM ||
	    neigh_rq_count < BATADV_TQ_LOCAL_BIDRECT_RECV_MINIMUM)
		tq_own = 0;
	else
		/* neigh_node->real_packet_count is never zero as we
		 * only purge old information when getting new
		 * information
		 */
		tq_own = (BATADV_TQ_MAX_VALUE * total_count) /	neigh_rq_count;

	/* 1 - ((1-x) ** 3), normalized to TQ_MAX_VALUE this does
	 * affect the nearly-symmetric links only a little, but
	 * punishes asymmetric links more.  This will give a value
	 * between 0 and TQ_MAX_VALUE
	 */
	neigh_rq_inv = BATADV_TQ_LOCAL_WINDOW_SIZE - neigh_rq_count;
	neigh_rq_inv_cube = neigh_rq_inv * neigh_rq_inv * neigh_rq_inv;
	neigh_rq_max_cube = BATADV_TQ_LOCAL_WINDOW_SIZE *
			    BATADV_TQ_LOCAL_WINDOW_SIZE *
			    BATADV_TQ_LOCAL_WINDOW_SIZE;
	inv_asym_penalty = BATADV_TQ_MAX_VALUE * neigh_rq_inv_cube;
	inv_asym_penalty /= neigh_rq_max_cube;
	tq_asym_penalty = BATADV_TQ_MAX_VALUE - inv_asym_penalty;

	combined_tq = batadv_ogm_packet->tq * tq_own * tq_asym_penalty;
	combined_tq /= BATADV_TQ_MAX_VALUE * BATADV_TQ_MAX_VALUE;
	batadv_ogm_packet->tq = combined_tq;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "bidirectional: orig = %-15pM neigh = %-15pM => own_bcast = %2i, real recv = %2i, local tq: %3i, asym_penalty: %3i, total tq: %3i\n",
		   orig_node->orig, orig_neigh_node->orig, total_count,
		   neigh_rq_count, tq_own,
		   tq_asym_penalty, batadv_ogm_packet->tq);

	/* if link has the minimum required transmission quality
	 * consider it bidirectional
	 */
	if (batadv_ogm_packet->tq >= BATADV_TQ_TOTAL_BIDRECT_LIMIT)
		ret = 1;

out:
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	return ret;
}

/**
 * batadv_iv_ogm_update_seqnos -  process a batman packet for all interfaces,
 *  adjust the sequence number and find out whether it is a duplicate
 * @ethhdr: ethernet header of the packet
 * @batadv_ogm_packet: OGM packet to be considered
 * @if_incoming: interface on which the OGM packet was received
 *
 * Returns duplicate status as enum batadv_dup_status
 */
static enum batadv_dup_status
batadv_iv_ogm_update_seqnos(const struct ethhdr *ethhdr,
			    const struct batadv_ogm_packet *batadv_ogm_packet,
			    const struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_orig_node *orig_node;
	struct batadv_neigh_node *tmp_neigh_node;
	int is_dup;
	int32_t seq_diff;
	int need_update = 0;
	int set_mark;
	enum batadv_dup_status ret = BATADV_NO_DUP;
	uint32_t seqno = ntohl(batadv_ogm_packet->seqno);
	uint8_t *neigh_addr;
	uint8_t packet_count;

	orig_node = batadv_get_orig_node(bat_priv, batadv_ogm_packet->orig);
	if (!orig_node)
		return BATADV_NO_DUP;

	spin_lock_bh(&orig_node->ogm_cnt_lock);
	seq_diff = seqno - orig_node->last_real_seqno;

	/* signalize caller that the packet is to be dropped. */
	if (!hlist_empty(&orig_node->neigh_list) &&
	    batadv_window_protected(bat_priv, seq_diff,
				    &orig_node->batman_seqno_reset)) {
		ret = BATADV_PROTECTED;
		goto out;
	}

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node,
				 &orig_node->neigh_list, list) {
		neigh_addr = tmp_neigh_node->addr;
		is_dup = batadv_test_bit(tmp_neigh_node->real_bits,
					 orig_node->last_real_seqno,
					 seqno);

		if (batadv_compare_eth(neigh_addr, ethhdr->h_source) &&
		    tmp_neigh_node->if_incoming == if_incoming) {
			set_mark = 1;
			if (is_dup)
				ret = BATADV_NEIGH_DUP;
		} else {
			set_mark = 0;
			if (is_dup && (ret != BATADV_NEIGH_DUP))
				ret = BATADV_ORIG_DUP;
		}

		/* if the window moved, set the update flag. */
		need_update |= batadv_bit_get_packet(bat_priv,
						     tmp_neigh_node->real_bits,
						     seq_diff, set_mark);

		packet_count = bitmap_weight(tmp_neigh_node->real_bits,
					     BATADV_TQ_LOCAL_WINDOW_SIZE);
		tmp_neigh_node->real_packet_count = packet_count;
	}
	rcu_read_unlock();

	if (need_update) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "updating last_seqno: old %u, new %u\n",
			   orig_node->last_real_seqno, seqno);
		orig_node->last_real_seqno = seqno;
	}

out:
	spin_unlock_bh(&orig_node->ogm_cnt_lock);
	batadv_orig_node_free_ref(orig_node);
	return ret;
}

static void batadv_iv_ogm_process(const struct ethhdr *ethhdr,
				  struct batadv_ogm_packet *batadv_ogm_packet,
				  const unsigned char *tt_buff,
				  struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_hard_iface *hard_iface;
	struct batadv_orig_node *orig_neigh_node, *orig_node;
	struct batadv_neigh_node *router = NULL, *router_router = NULL;
	struct batadv_neigh_node *orig_neigh_router = NULL;
	int has_directlink_flag;
	int is_my_addr = 0, is_my_orig = 0, is_my_oldorig = 0;
	int is_bidirect;
	bool is_single_hop_neigh = false;
	bool is_from_best_next_hop = false;
	int sameseq, similar_ttl;
	enum batadv_dup_status dup_status;
	uint32_t if_incoming_seqno;
	uint8_t *prev_sender;

	/* Silently drop when the batman packet is actually not a
	 * correct packet.
	 *
	 * This might happen if a packet is padded (e.g. Ethernet has a
	 * minimum frame length of 64 byte) and the aggregation interprets
	 * it as an additional length.
	 *
	 * TODO: A more sane solution would be to have a bit in the
	 * batadv_ogm_packet to detect whether the packet is the last
	 * packet in an aggregation.  Here we expect that the padding
	 * is always zero (or not 0x01)
	 */
	if (batadv_ogm_packet->header.packet_type != BATADV_IV_OGM)
		return;

	/* could be changed by schedule_own_packet() */
	if_incoming_seqno = atomic_read(&if_incoming->bat_iv.ogm_seqno);

	if (batadv_ogm_packet->flags & BATADV_DIRECTLINK)
		has_directlink_flag = 1;
	else
		has_directlink_flag = 0;

	if (batadv_compare_eth(ethhdr->h_source, batadv_ogm_packet->orig))
		is_single_hop_neigh = true;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Received BATMAN packet via NB: %pM, IF: %s [%pM] (from OG: %pM, via prev OG: %pM, seqno %u, tq %d, TTL %d, V %d, IDF %d)\n",
		   ethhdr->h_source, if_incoming->net_dev->name,
		   if_incoming->net_dev->dev_addr, batadv_ogm_packet->orig,
		   batadv_ogm_packet->prev_sender,
		   ntohl(batadv_ogm_packet->seqno), batadv_ogm_packet->tq,
		   batadv_ogm_packet->header.ttl,
		   batadv_ogm_packet->header.version, has_directlink_flag);

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->if_status != BATADV_IF_ACTIVE)
			continue;

		if (hard_iface->soft_iface != if_incoming->soft_iface)
			continue;

		if (batadv_compare_eth(ethhdr->h_source,
				       hard_iface->net_dev->dev_addr))
			is_my_addr = 1;

		if (batadv_compare_eth(batadv_ogm_packet->orig,
				       hard_iface->net_dev->dev_addr))
			is_my_orig = 1;

		if (batadv_compare_eth(batadv_ogm_packet->prev_sender,
				       hard_iface->net_dev->dev_addr))
			is_my_oldorig = 1;
	}
	rcu_read_unlock();

	if (is_my_addr) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: received my own broadcast (sender: %pM)\n",
			   ethhdr->h_source);
		return;
	}

	if (is_my_orig) {
		unsigned long *word;
		int offset;
		int32_t bit_pos;
		int16_t if_num;
		uint8_t *weight;

		orig_neigh_node = batadv_get_orig_node(bat_priv,
						       ethhdr->h_source);
		if (!orig_neigh_node)
			return;

		/* neighbor has to indicate direct link and it has to
		 * come via the corresponding interface
		 * save packet seqno for bidirectional check
		 */
		if (has_directlink_flag &&
		    batadv_compare_eth(if_incoming->net_dev->dev_addr,
				       batadv_ogm_packet->orig)) {
			if_num = if_incoming->if_num;
			offset = if_num * BATADV_NUM_WORDS;

			spin_lock_bh(&orig_neigh_node->ogm_cnt_lock);
			word = &(orig_neigh_node->bcast_own[offset]);
			bit_pos = if_incoming_seqno - 2;
			bit_pos -= ntohl(batadv_ogm_packet->seqno);
			batadv_set_bit(word, bit_pos);
			weight = &orig_neigh_node->bcast_own_sum[if_num];
			*weight = bitmap_weight(word,
						BATADV_TQ_LOCAL_WINDOW_SIZE);
			spin_unlock_bh(&orig_neigh_node->ogm_cnt_lock);
		}

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: originator packet from myself (via neighbor)\n");
		batadv_orig_node_free_ref(orig_neigh_node);
		return;
	}

	if (is_my_oldorig) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: ignoring all rebroadcast echos (sender: %pM)\n",
			   ethhdr->h_source);
		return;
	}

	if (batadv_ogm_packet->flags & BATADV_NOT_BEST_NEXT_HOP) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: ignoring all packets not forwarded from the best next hop (sender: %pM)\n",
			   ethhdr->h_source);
		return;
	}

	orig_node = batadv_get_orig_node(bat_priv, batadv_ogm_packet->orig);
	if (!orig_node)
		return;

	dup_status = batadv_iv_ogm_update_seqnos(ethhdr, batadv_ogm_packet,
						 if_incoming);

	if (dup_status == BATADV_PROTECTED) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: packet within seqno protection time (sender: %pM)\n",
			   ethhdr->h_source);
		goto out;
	}

	if (batadv_ogm_packet->tq == 0) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: originator packet with tq equal 0\n");
		goto out;
	}

	router = batadv_orig_node_get_router(orig_node);
	if (router)
		router_router = batadv_orig_node_get_router(router->orig_node);

	if ((router && router->tq_avg != 0) &&
	    (batadv_compare_eth(router->addr, ethhdr->h_source)))
		is_from_best_next_hop = true;

	prev_sender = batadv_ogm_packet->prev_sender;
	/* avoid temporary routing loops */
	if (router && router_router &&
	    (batadv_compare_eth(router->addr, prev_sender)) &&
	    !(batadv_compare_eth(batadv_ogm_packet->orig, prev_sender)) &&
	    (batadv_compare_eth(router->addr, router_router->addr))) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: ignoring all rebroadcast packets that may make me loop (sender: %pM)\n",
			   ethhdr->h_source);
		goto out;
	}

	batadv_tvlv_ogm_receive(bat_priv, batadv_ogm_packet, orig_node);

	/* if sender is a direct neighbor the sender mac equals
	 * originator mac
	 */
	if (is_single_hop_neigh)
		orig_neigh_node = orig_node;
	else
		orig_neigh_node = batadv_get_orig_node(bat_priv,
						       ethhdr->h_source);

	if (!orig_neigh_node)
		goto out;

	/* Update nc_nodes of the originator */
	batadv_nc_update_nc_node(bat_priv, orig_node, orig_neigh_node,
				 batadv_ogm_packet, is_single_hop_neigh);

	orig_neigh_router = batadv_orig_node_get_router(orig_neigh_node);

	/* drop packet if sender is not a direct neighbor and if we
	 * don't route towards it
	 */
	if (!is_single_hop_neigh && (!orig_neigh_router)) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: OGM via unknown neighbor!\n");
		goto out_neigh;
	}

	is_bidirect = batadv_iv_ogm_calc_tq(orig_node, orig_neigh_node,
					    batadv_ogm_packet, if_incoming);

	batadv_bonding_save_primary(orig_node, orig_neigh_node,
				    batadv_ogm_packet);

	/* update ranking if it is not a duplicate or has the same
	 * seqno and similar ttl as the non-duplicate
	 */
	sameseq = orig_node->last_real_seqno == ntohl(batadv_ogm_packet->seqno);
	similar_ttl = orig_node->last_ttl - 3 <= batadv_ogm_packet->header.ttl;
	if (is_bidirect && ((dup_status == BATADV_NO_DUP) ||
			    (sameseq && similar_ttl)))
		batadv_iv_ogm_orig_update(bat_priv, orig_node, ethhdr,
					  batadv_ogm_packet, if_incoming,
					  tt_buff, dup_status);

	/* is single hop (direct) neighbor */
	if (is_single_hop_neigh) {
		/* mark direct link on incoming interface */
		batadv_iv_ogm_forward(orig_node, ethhdr, batadv_ogm_packet,
				      is_single_hop_neigh,
				      is_from_best_next_hop, if_incoming);

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Forwarding packet: rebroadcast neighbor packet with direct link flag\n");
		goto out_neigh;
	}

	/* multihop originator */
	if (!is_bidirect) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: not received via bidirectional link\n");
		goto out_neigh;
	}

	if (dup_status == BATADV_NEIGH_DUP) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: duplicate packet received\n");
		goto out_neigh;
	}

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Forwarding packet: rebroadcast originator packet\n");
	batadv_iv_ogm_forward(orig_node, ethhdr, batadv_ogm_packet,
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

static int batadv_iv_ogm_receive(struct sk_buff *skb,
				 struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_ogm_packet *batadv_ogm_packet;
	struct ethhdr *ethhdr;
	int buff_pos = 0, packet_len;
	unsigned char *tvlv_buff, *packet_buff;
	uint8_t *packet_pos;
	bool ret;

	ret = batadv_check_management_packet(skb, if_incoming, BATADV_OGM_HLEN);
	if (!ret)
		return NET_RX_DROP;

	/* did we receive a B.A.T.M.A.N. IV OGM packet on an interface
	 * that does not have B.A.T.M.A.N. IV enabled ?
	 */
	if (bat_priv->bat_algo_ops->bat_ogm_emit != batadv_iv_ogm_emit)
		return NET_RX_DROP;

	batadv_inc_counter(bat_priv, BATADV_CNT_MGMT_RX);
	batadv_add_counter(bat_priv, BATADV_CNT_MGMT_RX_BYTES,
			   skb->len + ETH_HLEN);

	packet_len = skb_headlen(skb);
	ethhdr = eth_hdr(skb);
	packet_buff = skb->data;
	batadv_ogm_packet = (struct batadv_ogm_packet *)packet_buff;

	/* unpack the aggregated packets and process them one by one */
	while (batadv_iv_ogm_aggr_packet(buff_pos, packet_len,
					 batadv_ogm_packet->tvlv_len)) {
		tvlv_buff = packet_buff + buff_pos + BATADV_OGM_HLEN;

		batadv_iv_ogm_process(ethhdr, batadv_ogm_packet,
				      tvlv_buff, if_incoming);

		buff_pos += BATADV_OGM_HLEN;
		buff_pos += ntohs(batadv_ogm_packet->tvlv_len);

		packet_pos = packet_buff + buff_pos;
		batadv_ogm_packet = (struct batadv_ogm_packet *)packet_pos;
	}

	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

static struct batadv_algo_ops batadv_batman_iv __read_mostly = {
	.name = "BATMAN_IV",
	.bat_iface_enable = batadv_iv_ogm_iface_enable,
	.bat_iface_disable = batadv_iv_ogm_iface_disable,
	.bat_iface_update_mac = batadv_iv_ogm_iface_update_mac,
	.bat_primary_iface_set = batadv_iv_ogm_primary_iface_set,
	.bat_ogm_schedule = batadv_iv_ogm_schedule,
	.bat_ogm_emit = batadv_iv_ogm_emit,
};

int __init batadv_iv_init(void)
{
	int ret;

	/* batman originator packet */
	ret = batadv_recv_handler_register(BATADV_IV_OGM,
					   batadv_iv_ogm_receive);
	if (ret < 0)
		goto out;

	ret = batadv_algo_register(&batadv_batman_iv);
	if (ret < 0)
		goto handler_unregister;

	goto out;

handler_unregister:
	batadv_recv_handler_unregister(BATADV_IV_OGM);
out:
	return ret;
}
