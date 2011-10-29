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
#include "send.h"
#include "routing.h"
#include "translation-table.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "vis.h"
#include "aggregation.h"
#include "gateway_common.h"
#include "originator.h"

static void send_outstanding_bcast_packet(struct work_struct *work);

/* apply hop penalty for a normal link */
static uint8_t hop_penalty(uint8_t tq, const struct bat_priv *bat_priv)
{
	int hop_penalty = atomic_read(&bat_priv->hop_penalty);
	return (tq * (TQ_MAX_VALUE - hop_penalty)) / (TQ_MAX_VALUE);
}

/* when do we schedule our own packet to be sent */
static unsigned long own_send_time(const struct bat_priv *bat_priv)
{
	return jiffies + msecs_to_jiffies(
		   atomic_read(&bat_priv->orig_interval) -
		   JITTER + (random32() % 2*JITTER));
}

/* when do we schedule a forwarded packet to be sent */
static unsigned long forward_send_time(void)
{
	return jiffies + msecs_to_jiffies(random32() % (JITTER/2));
}

/* send out an already prepared packet to the given address via the
 * specified batman interface */
int send_skb_packet(struct sk_buff *skb, struct hard_iface *hard_iface,
		    const uint8_t *dst_addr)
{
	struct ethhdr *ethhdr;

	if (hard_iface->if_status != IF_ACTIVE)
		goto send_skb_err;

	if (unlikely(!hard_iface->net_dev))
		goto send_skb_err;

	if (!(hard_iface->net_dev->flags & IFF_UP)) {
		pr_warning("Interface %s is not up - can't send packet via "
			   "that interface!\n", hard_iface->net_dev->name);
		goto send_skb_err;
	}

	/* push to the ethernet header. */
	if (my_skb_head_push(skb, sizeof(*ethhdr)) < 0)
		goto send_skb_err;

	skb_reset_mac_header(skb);

	ethhdr = (struct ethhdr *) skb_mac_header(skb);
	memcpy(ethhdr->h_source, hard_iface->net_dev->dev_addr, ETH_ALEN);
	memcpy(ethhdr->h_dest, dst_addr, ETH_ALEN);
	ethhdr->h_proto = __constant_htons(ETH_P_BATMAN);

	skb_set_network_header(skb, ETH_HLEN);
	skb->priority = TC_PRIO_CONTROL;
	skb->protocol = __constant_htons(ETH_P_BATMAN);

	skb->dev = hard_iface->net_dev;

	/* dev_queue_xmit() returns a negative result on error.	 However on
	 * congestion and traffic shaping, it drops and returns NET_XMIT_DROP
	 * (which is > 0). This will not be treated as an error. */

	return dev_queue_xmit(skb);
send_skb_err:
	kfree_skb(skb);
	return NET_XMIT_DROP;
}

/* Send a packet to a given interface */
static void send_packet_to_if(struct forw_packet *forw_packet,
			      struct hard_iface *hard_iface)
{
	struct bat_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	char *fwd_str;
	uint8_t packet_num;
	int16_t buff_pos;
	struct batman_packet *batman_packet;
	struct sk_buff *skb;

	if (hard_iface->if_status != IF_ACTIVE)
		return;

	packet_num = 0;
	buff_pos = 0;
	batman_packet = (struct batman_packet *)forw_packet->skb->data;

	/* adjust all flags and log packets */
	while (aggregated_packet(buff_pos,
				 forw_packet->packet_len,
				 batman_packet->tt_num_changes)) {

		/* we might have aggregated direct link packets with an
		 * ordinary base packet */
		if ((forw_packet->direct_link_flags & (1 << packet_num)) &&
		    (forw_packet->if_incoming == hard_iface))
			batman_packet->flags |= DIRECTLINK;
		else
			batman_packet->flags &= ~DIRECTLINK;

		fwd_str = (packet_num > 0 ? "Forwarding" : (forw_packet->own ?
							    "Sending own" :
							    "Forwarding"));
		bat_dbg(DBG_BATMAN, bat_priv,
			"%s %spacket (originator %pM, seqno %d, TQ %d, TTL %d,"
			" IDF %s, hvn %d) on interface %s [%pM]\n",
			fwd_str, (packet_num > 0 ? "aggregated " : ""),
			batman_packet->orig, ntohl(batman_packet->seqno),
			batman_packet->tq, batman_packet->ttl,
			(batman_packet->flags & DIRECTLINK ?
			 "on" : "off"),
			batman_packet->ttvn, hard_iface->net_dev->name,
			hard_iface->net_dev->dev_addr);

		buff_pos += sizeof(*batman_packet) +
			tt_len(batman_packet->tt_num_changes);
		packet_num++;
		batman_packet = (struct batman_packet *)
			(forw_packet->skb->data + buff_pos);
	}

	/* create clone because function is called more than once */
	skb = skb_clone(forw_packet->skb, GFP_ATOMIC);
	if (skb)
		send_skb_packet(skb, hard_iface, broadcast_addr);
}

/* send a batman packet */
static void send_packet(struct forw_packet *forw_packet)
{
	struct hard_iface *hard_iface;
	struct net_device *soft_iface;
	struct bat_priv *bat_priv;
	struct hard_iface *primary_if = NULL;
	struct batman_packet *batman_packet =
		(struct batman_packet *)(forw_packet->skb->data);
	int directlink = (batman_packet->flags & DIRECTLINK ? 1 : 0);

	if (!forw_packet->if_incoming) {
		pr_err("Error - can't forward packet: incoming iface not "
		       "specified\n");
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
	if ((directlink && (batman_packet->ttl == 1)) ||
	    (forw_packet->own && (forw_packet->if_incoming != primary_if))) {

		/* FIXME: what about aggregated packets ? */
		bat_dbg(DBG_BATMAN, bat_priv,
			"%s packet (originator %pM, seqno %d, TTL %d) "
			"on interface %s [%pM]\n",
			(forw_packet->own ? "Sending own" : "Forwarding"),
			batman_packet->orig, ntohl(batman_packet->seqno),
			batman_packet->ttl,
			forw_packet->if_incoming->net_dev->name,
			forw_packet->if_incoming->net_dev->dev_addr);

		/* skb is only used once and than forw_packet is free'd */
		send_skb_packet(forw_packet->skb, forw_packet->if_incoming,
				broadcast_addr);
		forw_packet->skb = NULL;

		goto out;
	}

	/* broadcast on every interface */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &hardif_list, list) {
		if (hard_iface->soft_iface != soft_iface)
			continue;

		send_packet_to_if(forw_packet, hard_iface);
	}
	rcu_read_unlock();

out:
	if (primary_if)
		hardif_free_ref(primary_if);
}

static void realloc_packet_buffer(struct hard_iface *hard_iface,
				int new_len)
{
	unsigned char *new_buff;
	struct batman_packet *batman_packet;

	new_buff = kmalloc(new_len, GFP_ATOMIC);

	/* keep old buffer if kmalloc should fail */
	if (new_buff) {
		memcpy(new_buff, hard_iface->packet_buff,
		       sizeof(*batman_packet));

		kfree(hard_iface->packet_buff);
		hard_iface->packet_buff = new_buff;
		hard_iface->packet_len = new_len;
	}
}

/* when calling this function (hard_iface == primary_if) has to be true */
static void prepare_packet_buffer(struct bat_priv *bat_priv,
				  struct hard_iface *hard_iface)
{
	int new_len;
	struct batman_packet *batman_packet;

	new_len = BAT_PACKET_LEN +
		  tt_len((uint8_t)atomic_read(&bat_priv->tt_local_changes));

	/* if we have too many changes for one packet don't send any
	 * and wait for the tt table request which will be fragmented */
	if (new_len > hard_iface->soft_iface->mtu)
		new_len = BAT_PACKET_LEN;

	realloc_packet_buffer(hard_iface, new_len);
	batman_packet = (struct batman_packet *)hard_iface->packet_buff;

	atomic_set(&bat_priv->tt_crc, tt_local_crc(bat_priv));

	/* reset the sending counter */
	atomic_set(&bat_priv->tt_ogm_append_cnt, TT_OGM_APPEND_MAX);

	batman_packet->tt_num_changes = tt_changes_fill_buffer(bat_priv,
				hard_iface->packet_buff + BAT_PACKET_LEN,
				hard_iface->packet_len - BAT_PACKET_LEN);

}

static void reset_packet_buffer(struct bat_priv *bat_priv,
	struct hard_iface *hard_iface)
{
	struct batman_packet *batman_packet;

	realloc_packet_buffer(hard_iface, BAT_PACKET_LEN);

	batman_packet = (struct batman_packet *)hard_iface->packet_buff;
	batman_packet->tt_num_changes = 0;
}

void schedule_own_packet(struct hard_iface *hard_iface)
{
	struct bat_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct hard_iface *primary_if;
	unsigned long send_time;
	struct batman_packet *batman_packet;
	int vis_server;

	if ((hard_iface->if_status == IF_NOT_IN_USE) ||
	    (hard_iface->if_status == IF_TO_BE_REMOVED))
		return;

	vis_server = atomic_read(&bat_priv->vis_mode);
	primary_if = primary_if_get_selected(bat_priv);

	/**
	 * the interface gets activated here to avoid race conditions between
	 * the moment of activating the interface in
	 * hardif_activate_interface() where the originator mac is set and
	 * outdated packets (especially uninitialized mac addresses) in the
	 * packet queue
	 */
	if (hard_iface->if_status == IF_TO_BE_ACTIVATED)
		hard_iface->if_status = IF_ACTIVE;

	if (hard_iface == primary_if) {
		/* if at least one change happened */
		if (atomic_read(&bat_priv->tt_local_changes) > 0) {
			tt_commit_changes(bat_priv);
			prepare_packet_buffer(bat_priv, hard_iface);
		}

		/* if the changes have been sent enough times */
		if (!atomic_dec_not_zero(&bat_priv->tt_ogm_append_cnt))
			reset_packet_buffer(bat_priv, hard_iface);
	}

	/**
	 * NOTE: packet_buff might just have been re-allocated in
	 * prepare_packet_buffer() or in reset_packet_buffer()
	 */
	batman_packet = (struct batman_packet *)hard_iface->packet_buff;

	/* change sequence number to network order */
	batman_packet->seqno =
		htonl((uint32_t)atomic_read(&hard_iface->seqno));

	batman_packet->ttvn = atomic_read(&bat_priv->ttvn);
	batman_packet->tt_crc = htons((uint16_t)atomic_read(&bat_priv->tt_crc));

	if (vis_server == VIS_TYPE_SERVER_SYNC)
		batman_packet->flags |= VIS_SERVER;
	else
		batman_packet->flags &= ~VIS_SERVER;

	if ((hard_iface == primary_if) &&
	    (atomic_read(&bat_priv->gw_mode) == GW_MODE_SERVER))
		batman_packet->gw_flags =
				(uint8_t)atomic_read(&bat_priv->gw_bandwidth);
	else
		batman_packet->gw_flags = NO_FLAGS;

	atomic_inc(&hard_iface->seqno);

	slide_own_bcast_window(hard_iface);
	send_time = own_send_time(bat_priv);
	add_bat_packet_to_list(bat_priv,
			       hard_iface->packet_buff,
			       hard_iface->packet_len,
			       hard_iface, 1, send_time);

	if (primary_if)
		hardif_free_ref(primary_if);
}

void schedule_forward_packet(struct orig_node *orig_node,
			     const struct ethhdr *ethhdr,
			     struct batman_packet *batman_packet,
			     int directlink,
			     struct hard_iface *if_incoming)
{
	struct bat_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct neigh_node *router;
	uint8_t in_tq, in_ttl, tq_avg = 0;
	unsigned long send_time;
	uint8_t tt_num_changes;

	if (batman_packet->ttl <= 1) {
		bat_dbg(DBG_BATMAN, bat_priv, "ttl exceeded\n");
		return;
	}

	router = orig_node_get_router(orig_node);

	in_tq = batman_packet->tq;
	in_ttl = batman_packet->ttl;
	tt_num_changes = batman_packet->tt_num_changes;

	batman_packet->ttl--;
	memcpy(batman_packet->prev_sender, ethhdr->h_source, ETH_ALEN);

	/* rebroadcast tq of our best ranking neighbor to ensure the rebroadcast
	 * of our best tq value */
	if (router && router->tq_avg != 0) {

		/* rebroadcast ogm of best ranking neighbor as is */
		if (!compare_eth(router->addr, ethhdr->h_source)) {
			batman_packet->tq = router->tq_avg;

			if (router->last_ttl)
				batman_packet->ttl = router->last_ttl - 1;
		}

		tq_avg = router->tq_avg;
	}

	if (router)
		neigh_node_free_ref(router);

	/* apply hop penalty */
	batman_packet->tq = hop_penalty(batman_packet->tq, bat_priv);

	bat_dbg(DBG_BATMAN, bat_priv,
		"Forwarding packet: tq_orig: %i, tq_avg: %i, "
		"tq_forw: %i, ttl_orig: %i, ttl_forw: %i\n",
		in_tq, tq_avg, batman_packet->tq, in_ttl - 1,
		batman_packet->ttl);

	batman_packet->seqno = htonl(batman_packet->seqno);
	batman_packet->tt_crc = htons(batman_packet->tt_crc);

	/* switch of primaries first hop flag when forwarding */
	batman_packet->flags &= ~PRIMARIES_FIRST_HOP;
	if (directlink)
		batman_packet->flags |= DIRECTLINK;
	else
		batman_packet->flags &= ~DIRECTLINK;

	send_time = forward_send_time();
	add_bat_packet_to_list(bat_priv,
			       (unsigned char *)batman_packet,
			       sizeof(*batman_packet) + tt_len(tt_num_changes),
			       if_incoming, 0, send_time);
}

static void forw_packet_free(struct forw_packet *forw_packet)
{
	if (forw_packet->skb)
		kfree_skb(forw_packet->skb);
	if (forw_packet->if_incoming)
		hardif_free_ref(forw_packet->if_incoming);
	kfree(forw_packet);
}

static void _add_bcast_packet_to_list(struct bat_priv *bat_priv,
				      struct forw_packet *forw_packet,
				      unsigned long send_time)
{
	INIT_HLIST_NODE(&forw_packet->list);

	/* add new packet to packet list */
	spin_lock_bh(&bat_priv->forw_bcast_list_lock);
	hlist_add_head(&forw_packet->list, &bat_priv->forw_bcast_list);
	spin_unlock_bh(&bat_priv->forw_bcast_list_lock);

	/* start timer for this packet */
	INIT_DELAYED_WORK(&forw_packet->delayed_work,
			  send_outstanding_bcast_packet);
	queue_delayed_work(bat_event_workqueue, &forw_packet->delayed_work,
			   send_time);
}

/* add a broadcast packet to the queue and setup timers. broadcast packets
 * are sent multiple times to increase probability for beeing received.
 *
 * This function returns NETDEV_TX_OK on success and NETDEV_TX_BUSY on
 * errors.
 *
 * The skb is not consumed, so the caller should make sure that the
 * skb is freed. */
int add_bcast_packet_to_list(struct bat_priv *bat_priv,
			     const struct sk_buff *skb, unsigned long delay)
{
	struct hard_iface *primary_if = NULL;
	struct forw_packet *forw_packet;
	struct bcast_packet *bcast_packet;
	struct sk_buff *newskb;

	if (!atomic_dec_not_zero(&bat_priv->bcast_queue_left)) {
		bat_dbg(DBG_BATMAN, bat_priv, "bcast packet queue full\n");
		goto out;
	}

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out_and_inc;

	forw_packet = kmalloc(sizeof(*forw_packet), GFP_ATOMIC);

	if (!forw_packet)
		goto out_and_inc;

	newskb = skb_copy(skb, GFP_ATOMIC);
	if (!newskb)
		goto packet_free;

	/* as we have a copy now, it is safe to decrease the TTL */
	bcast_packet = (struct bcast_packet *)newskb->data;
	bcast_packet->ttl--;

	skb_reset_mac_header(newskb);

	forw_packet->skb = newskb;
	forw_packet->if_incoming = primary_if;

	/* how often did we send the bcast packet ? */
	forw_packet->num_packets = 0;

	_add_bcast_packet_to_list(bat_priv, forw_packet, delay);
	return NETDEV_TX_OK;

packet_free:
	kfree(forw_packet);
out_and_inc:
	atomic_inc(&bat_priv->bcast_queue_left);
out:
	if (primary_if)
		hardif_free_ref(primary_if);
	return NETDEV_TX_BUSY;
}

static void send_outstanding_bcast_packet(struct work_struct *work)
{
	struct hard_iface *hard_iface;
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct forw_packet *forw_packet =
		container_of(delayed_work, struct forw_packet, delayed_work);
	struct sk_buff *skb1;
	struct net_device *soft_iface = forw_packet->if_incoming->soft_iface;
	struct bat_priv *bat_priv = netdev_priv(soft_iface);

	spin_lock_bh(&bat_priv->forw_bcast_list_lock);
	hlist_del(&forw_packet->list);
	spin_unlock_bh(&bat_priv->forw_bcast_list_lock);

	if (atomic_read(&bat_priv->mesh_state) == MESH_DEACTIVATING)
		goto out;

	/* rebroadcast packet */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &hardif_list, list) {
		if (hard_iface->soft_iface != soft_iface)
			continue;

		/* send a copy of the saved skb */
		skb1 = skb_clone(forw_packet->skb, GFP_ATOMIC);
		if (skb1)
			send_skb_packet(skb1, hard_iface, broadcast_addr);
	}
	rcu_read_unlock();

	forw_packet->num_packets++;

	/* if we still have some more bcasts to send */
	if (forw_packet->num_packets < 3) {
		_add_bcast_packet_to_list(bat_priv, forw_packet,
					  ((5 * HZ) / 1000));
		return;
	}

out:
	forw_packet_free(forw_packet);
	atomic_inc(&bat_priv->bcast_queue_left);
}

void send_outstanding_bat_packet(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct forw_packet *forw_packet =
		container_of(delayed_work, struct forw_packet, delayed_work);
	struct bat_priv *bat_priv;

	bat_priv = netdev_priv(forw_packet->if_incoming->soft_iface);
	spin_lock_bh(&bat_priv->forw_bat_list_lock);
	hlist_del(&forw_packet->list);
	spin_unlock_bh(&bat_priv->forw_bat_list_lock);

	if (atomic_read(&bat_priv->mesh_state) == MESH_DEACTIVATING)
		goto out;

	send_packet(forw_packet);

	/**
	 * we have to have at least one packet in the queue
	 * to determine the queues wake up time unless we are
	 * shutting down
	 */
	if (forw_packet->own)
		schedule_own_packet(forw_packet->if_incoming);

out:
	/* don't count own packet */
	if (!forw_packet->own)
		atomic_inc(&bat_priv->batman_queue_left);

	forw_packet_free(forw_packet);
}

void purge_outstanding_packets(struct bat_priv *bat_priv,
			       const struct hard_iface *hard_iface)
{
	struct forw_packet *forw_packet;
	struct hlist_node *tmp_node, *safe_tmp_node;
	bool pending;

	if (hard_iface)
		bat_dbg(DBG_BATMAN, bat_priv,
			"purge_outstanding_packets(): %s\n",
			hard_iface->net_dev->name);
	else
		bat_dbg(DBG_BATMAN, bat_priv,
			"purge_outstanding_packets()\n");

	/* free bcast list */
	spin_lock_bh(&bat_priv->forw_bcast_list_lock);
	hlist_for_each_entry_safe(forw_packet, tmp_node, safe_tmp_node,
				  &bat_priv->forw_bcast_list, list) {

		/**
		 * if purge_outstanding_packets() was called with an argmument
		 * we delete only packets belonging to the given interface
		 */
		if ((hard_iface) &&
		    (forw_packet->if_incoming != hard_iface))
			continue;

		spin_unlock_bh(&bat_priv->forw_bcast_list_lock);

		/**
		 * send_outstanding_bcast_packet() will lock the list to
		 * delete the item from the list
		 */
		pending = cancel_delayed_work_sync(&forw_packet->delayed_work);
		spin_lock_bh(&bat_priv->forw_bcast_list_lock);

		if (pending) {
			hlist_del(&forw_packet->list);
			forw_packet_free(forw_packet);
		}
	}
	spin_unlock_bh(&bat_priv->forw_bcast_list_lock);

	/* free batman packet list */
	spin_lock_bh(&bat_priv->forw_bat_list_lock);
	hlist_for_each_entry_safe(forw_packet, tmp_node, safe_tmp_node,
				  &bat_priv->forw_bat_list, list) {

		/**
		 * if purge_outstanding_packets() was called with an argmument
		 * we delete only packets belonging to the given interface
		 */
		if ((hard_iface) &&
		    (forw_packet->if_incoming != hard_iface))
			continue;

		spin_unlock_bh(&bat_priv->forw_bat_list_lock);

		/**
		 * send_outstanding_bat_packet() will lock the list to
		 * delete the item from the list
		 */
		pending = cancel_delayed_work_sync(&forw_packet->delayed_work);
		spin_lock_bh(&bat_priv->forw_bat_list_lock);

		if (pending) {
			hlist_del(&forw_packet->list);
			forw_packet_free(forw_packet);
		}
	}
	spin_unlock_bh(&bat_priv->forw_bat_list_lock);
}
