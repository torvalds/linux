/* Copyright (C) 2007-2016  B.A.T.M.A.N. contributors:
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

#include "bat_algo.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/byteorder/generic.h>
#include <linux/cache.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/pkt_sched.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "bitarray.h"
#include "hard-interface.h"
#include "hash.h"
#include "network-coding.h"
#include "originator.h"
#include "packet.h"
#include "routing.h"
#include "send.h"
#include "translation-table.h"

/**
 * enum batadv_dup_status - duplicate status
 * @BATADV_NO_DUP: the packet is no duplicate
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
static void batadv_ring_buffer_set(u8 lq_recv[], u8 *lq_index, u8 value)
{
	lq_recv[*lq_index] = value;
	*lq_index = (*lq_index + 1) % BATADV_TQ_GLOBAL_WINDOW_SIZE;
}

/**
 * batadv_ring_buffer_avg - compute the average of all non-zero values stored
 * in the given ring buffer
 * @lq_recv: pointer to the ring buffer
 *
 * Return: computed average value.
 */
static u8 batadv_ring_buffer_avg(const u8 lq_recv[])
{
	const u8 *ptr;
	u16 count = 0;
	u16 i = 0;
	u16 sum = 0;

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

	return (u8)(sum / count);
}

/**
 * batadv_iv_ogm_orig_free - free the private resources allocated for this
 *  orig_node
 * @orig_node: the orig_node for which the resources have to be free'd
 */
static void batadv_iv_ogm_orig_free(struct batadv_orig_node *orig_node)
{
	kfree(orig_node->bat_iv.bcast_own);
	kfree(orig_node->bat_iv.bcast_own_sum);
}

/**
 * batadv_iv_ogm_orig_add_if - change the private structures of the orig_node to
 *  include the new hard-interface
 * @orig_node: the orig_node that has to be changed
 * @max_if_num: the current amount of interfaces
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int batadv_iv_ogm_orig_add_if(struct batadv_orig_node *orig_node,
				     int max_if_num)
{
	void *data_ptr;
	size_t old_size;
	int ret = -ENOMEM;

	spin_lock_bh(&orig_node->bat_iv.ogm_cnt_lock);

	old_size = (max_if_num - 1) * sizeof(unsigned long) * BATADV_NUM_WORDS;
	data_ptr = kmalloc_array(max_if_num,
				 BATADV_NUM_WORDS * sizeof(unsigned long),
				 GFP_ATOMIC);
	if (!data_ptr)
		goto unlock;

	memcpy(data_ptr, orig_node->bat_iv.bcast_own, old_size);
	kfree(orig_node->bat_iv.bcast_own);
	orig_node->bat_iv.bcast_own = data_ptr;

	data_ptr = kmalloc_array(max_if_num, sizeof(u8), GFP_ATOMIC);
	if (!data_ptr)
		goto unlock;

	memcpy(data_ptr, orig_node->bat_iv.bcast_own_sum,
	       (max_if_num - 1) * sizeof(u8));
	kfree(orig_node->bat_iv.bcast_own_sum);
	orig_node->bat_iv.bcast_own_sum = data_ptr;

	ret = 0;

unlock:
	spin_unlock_bh(&orig_node->bat_iv.ogm_cnt_lock);

	return ret;
}

/**
 * batadv_iv_ogm_drop_bcast_own_entry - drop section of bcast_own
 * @orig_node: the orig_node that has to be changed
 * @max_if_num: the current amount of interfaces
 * @del_if_num: the index of the interface being removed
 */
static void
batadv_iv_ogm_drop_bcast_own_entry(struct batadv_orig_node *orig_node,
				   int max_if_num, int del_if_num)
{
	size_t chunk_size;
	size_t if_offset;
	void *data_ptr;

	lockdep_assert_held(&orig_node->bat_iv.ogm_cnt_lock);

	chunk_size = sizeof(unsigned long) * BATADV_NUM_WORDS;
	data_ptr = kmalloc_array(max_if_num, chunk_size, GFP_ATOMIC);
	if (!data_ptr)
		/* use old buffer when new one could not be allocated */
		data_ptr = orig_node->bat_iv.bcast_own;

	/* copy first part */
	memmove(data_ptr, orig_node->bat_iv.bcast_own, del_if_num * chunk_size);

	/* copy second part */
	if_offset = (del_if_num + 1) * chunk_size;
	memmove((char *)data_ptr + del_if_num * chunk_size,
		(uint8_t *)orig_node->bat_iv.bcast_own + if_offset,
		(max_if_num - del_if_num) * chunk_size);

	/* bcast_own was shrunk down in new buffer; free old one */
	if (orig_node->bat_iv.bcast_own != data_ptr) {
		kfree(orig_node->bat_iv.bcast_own);
		orig_node->bat_iv.bcast_own = data_ptr;
	}
}

/**
 * batadv_iv_ogm_drop_bcast_own_sum_entry - drop section of bcast_own_sum
 * @orig_node: the orig_node that has to be changed
 * @max_if_num: the current amount of interfaces
 * @del_if_num: the index of the interface being removed
 */
static void
batadv_iv_ogm_drop_bcast_own_sum_entry(struct batadv_orig_node *orig_node,
				       int max_if_num, int del_if_num)
{
	size_t if_offset;
	void *data_ptr;

	lockdep_assert_held(&orig_node->bat_iv.ogm_cnt_lock);

	data_ptr = kmalloc_array(max_if_num, sizeof(u8), GFP_ATOMIC);
	if (!data_ptr)
		/* use old buffer when new one could not be allocated */
		data_ptr = orig_node->bat_iv.bcast_own_sum;

	memmove(data_ptr, orig_node->bat_iv.bcast_own_sum,
		del_if_num * sizeof(u8));

	if_offset = (del_if_num + 1) * sizeof(u8);
	memmove((char *)data_ptr + del_if_num * sizeof(u8),
		orig_node->bat_iv.bcast_own_sum + if_offset,
		(max_if_num - del_if_num) * sizeof(u8));

	/* bcast_own_sum was shrunk down in new buffer; free old one */
	if (orig_node->bat_iv.bcast_own_sum != data_ptr) {
		kfree(orig_node->bat_iv.bcast_own_sum);
		orig_node->bat_iv.bcast_own_sum = data_ptr;
	}
}

/**
 * batadv_iv_ogm_orig_del_if - change the private structures of the orig_node to
 *  exclude the removed interface
 * @orig_node: the orig_node that has to be changed
 * @max_if_num: the current amount of interfaces
 * @del_if_num: the index of the interface being removed
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int batadv_iv_ogm_orig_del_if(struct batadv_orig_node *orig_node,
				     int max_if_num, int del_if_num)
{
	spin_lock_bh(&orig_node->bat_iv.ogm_cnt_lock);

	if (max_if_num == 0) {
		kfree(orig_node->bat_iv.bcast_own);
		kfree(orig_node->bat_iv.bcast_own_sum);
		orig_node->bat_iv.bcast_own = NULL;
		orig_node->bat_iv.bcast_own_sum = NULL;
	} else {
		batadv_iv_ogm_drop_bcast_own_entry(orig_node, max_if_num,
						   del_if_num);
		batadv_iv_ogm_drop_bcast_own_sum_entry(orig_node, max_if_num,
						       del_if_num);
	}

	spin_unlock_bh(&orig_node->bat_iv.ogm_cnt_lock);

	return 0;
}

/**
 * batadv_iv_ogm_orig_get - retrieve or create (if does not exist) an originator
 * @bat_priv: the bat priv with all the soft interface information
 * @addr: mac address of the originator
 *
 * Return: the originator object corresponding to the passed mac address or NULL
 * on failure.
 * If the object does not exists it is created an initialised.
 */
static struct batadv_orig_node *
batadv_iv_ogm_orig_get(struct batadv_priv *bat_priv, const u8 *addr)
{
	struct batadv_orig_node *orig_node;
	int size, hash_added;

	orig_node = batadv_orig_hash_find(bat_priv, addr);
	if (orig_node)
		return orig_node;

	orig_node = batadv_orig_node_new(bat_priv, addr);
	if (!orig_node)
		return NULL;

	spin_lock_init(&orig_node->bat_iv.ogm_cnt_lock);

	size = bat_priv->num_ifaces * sizeof(unsigned long) * BATADV_NUM_WORDS;
	orig_node->bat_iv.bcast_own = kzalloc(size, GFP_ATOMIC);
	if (!orig_node->bat_iv.bcast_own)
		goto free_orig_node;

	size = bat_priv->num_ifaces * sizeof(u8);
	orig_node->bat_iv.bcast_own_sum = kzalloc(size, GFP_ATOMIC);
	if (!orig_node->bat_iv.bcast_own_sum)
		goto free_orig_node;

	hash_added = batadv_hash_add(bat_priv->orig_hash, batadv_compare_orig,
				     batadv_choose_orig, orig_node,
				     &orig_node->hash_entry);
	if (hash_added != 0)
		goto free_orig_node;

	return orig_node;

free_orig_node:
	/* free twice, as batadv_orig_node_new sets refcount to 2 */
	batadv_orig_node_put(orig_node);
	batadv_orig_node_put(orig_node);

	return NULL;
}

static struct batadv_neigh_node *
batadv_iv_ogm_neigh_new(struct batadv_hard_iface *hard_iface,
			const u8 *neigh_addr,
			struct batadv_orig_node *orig_node,
			struct batadv_orig_node *orig_neigh)
{
	struct batadv_neigh_node *neigh_node;

	neigh_node = batadv_neigh_node_new(orig_node, hard_iface, neigh_addr);
	if (!neigh_node)
		goto out;

	neigh_node->orig_node = orig_neigh;

out:
	return neigh_node;
}

static int batadv_iv_ogm_iface_enable(struct batadv_hard_iface *hard_iface)
{
	struct batadv_ogm_packet *batadv_ogm_packet;
	unsigned char *ogm_buff;
	u32 random_seqno;

	/* randomize initial seqno to avoid collision */
	get_random_bytes(&random_seqno, sizeof(random_seqno));
	atomic_set(&hard_iface->bat_iv.ogm_seqno, random_seqno);

	hard_iface->bat_iv.ogm_buff_len = BATADV_OGM_HLEN;
	ogm_buff = kmalloc(hard_iface->bat_iv.ogm_buff_len, GFP_ATOMIC);
	if (!ogm_buff)
		return -ENOMEM;

	hard_iface->bat_iv.ogm_buff = ogm_buff;

	batadv_ogm_packet = (struct batadv_ogm_packet *)ogm_buff;
	batadv_ogm_packet->packet_type = BATADV_IV_OGM;
	batadv_ogm_packet->version = BATADV_COMPAT_VERSION;
	batadv_ogm_packet->ttl = 2;
	batadv_ogm_packet->flags = BATADV_NO_FLAGS;
	batadv_ogm_packet->reserved = 0;
	batadv_ogm_packet->tq = BATADV_TQ_MAX_VALUE;

	return 0;
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
	ether_addr_copy(batadv_ogm_packet->orig,
			hard_iface->net_dev->dev_addr);
	ether_addr_copy(batadv_ogm_packet->prev_sender,
			hard_iface->net_dev->dev_addr);
}

static void
batadv_iv_ogm_primary_iface_set(struct batadv_hard_iface *hard_iface)
{
	struct batadv_ogm_packet *batadv_ogm_packet;
	unsigned char *ogm_buff = hard_iface->bat_iv.ogm_buff;

	batadv_ogm_packet = (struct batadv_ogm_packet *)ogm_buff;
	batadv_ogm_packet->ttl = BATADV_TTL;
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
static u8 batadv_hop_penalty(u8 tq, const struct batadv_priv *bat_priv)
{
	int hop_penalty = atomic_read(&bat_priv->hop_penalty);
	int new_tq;

	new_tq = tq * (BATADV_TQ_MAX_VALUE - hop_penalty);
	new_tq /= BATADV_TQ_MAX_VALUE;

	return new_tq;
}

/**
 * batadv_iv_ogm_aggr_packet - checks if there is another OGM attached
 * @buff_pos: current position in the skb
 * @packet_len: total length of the skb
 * @tvlv_len: tvlv length of the previously considered OGM
 *
 * Return: true if there is enough space for another OGM, false otherwise.
 */
static bool batadv_iv_ogm_aggr_packet(int buff_pos, int packet_len,
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
	const char *fwd_str;
	u8 packet_num;
	s16 buff_pos;
	struct batadv_ogm_packet *batadv_ogm_packet;
	struct sk_buff *skb;
	u8 *packet_pos;

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
			   batadv_ogm_packet->tq, batadv_ogm_packet->ttl,
			   ((batadv_ogm_packet->flags & BATADV_DIRECTLINK) ?
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
		batadv_send_broadcast_skb(skb, hard_iface);
	}
}

/* send a batman ogm packet */
static void batadv_iv_ogm_emit(struct batadv_forw_packet *forw_packet)
{
	struct net_device *soft_iface;
	struct batadv_priv *bat_priv;
	struct batadv_hard_iface *primary_if = NULL;

	if (!forw_packet->if_incoming) {
		pr_err("Error - can't forward packet: incoming iface not specified\n");
		goto out;
	}

	soft_iface = forw_packet->if_incoming->soft_iface;
	bat_priv = netdev_priv(soft_iface);

	if (WARN_ON(!forw_packet->if_outgoing))
		goto out;

	if (WARN_ON(forw_packet->if_outgoing->soft_iface != soft_iface))
		goto out;

	if (forw_packet->if_incoming->if_status != BATADV_IF_ACTIVE)
		goto out;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	/* only for one specific outgoing interface */
	batadv_iv_ogm_send_to_if(forw_packet, forw_packet->if_outgoing);

out:
	if (primary_if)
		batadv_hardif_put(primary_if);
}

/**
 * batadv_iv_ogm_can_aggregate - find out if an OGM can be aggregated on an
 *  existing forward packet
 * @new_bat_ogm_packet: OGM packet to be aggregated
 * @bat_priv: the bat priv with all the soft interface information
 * @packet_len: (total) length of the OGM
 * @send_time: timestamp (jiffies) when the packet is to be sent
 * @directlink: true if this is a direct link packet
 * @if_incoming: interface where the packet was received
 * @if_outgoing: interface for which the retransmission should be considered
 * @forw_packet: the forwarded packet which should be checked
 *
 * Return: true if new_packet can be aggregated with forw_packet
 */
static bool
batadv_iv_ogm_can_aggregate(const struct batadv_ogm_packet *new_bat_ogm_packet,
			    struct batadv_priv *bat_priv,
			    int packet_len, unsigned long send_time,
			    bool directlink,
			    const struct batadv_hard_iface *if_incoming,
			    const struct batadv_hard_iface *if_outgoing,
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
	 * otherwise aggregation is not possible
	 */
	if (!time_before(send_time, forw_packet->send_time) ||
	    !time_after_eq(aggregation_end_time, forw_packet->send_time))
		return false;

	if (aggregated_bytes > BATADV_MAX_AGGREGATION_BYTES)
		return false;

	/* packet is not leaving on the same interface. */
	if (forw_packet->if_outgoing != if_outgoing)
		return false;

	/* check aggregation compatibility
	 * -> direct link packets are broadcasted on
	 *    their interface only
	 * -> aggregate packet if the current packet is
	 *    a "global" packet as well as the base
	 *    packet
	 */
	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		return false;

	/* packets without direct link flag and high TTL
	 * are flooded through the net
	 */
	if (!directlink &&
	    !(batadv_ogm_packet->flags & BATADV_DIRECTLINK) &&
	    batadv_ogm_packet->ttl != 1 &&

	    /* own packets originating non-primary
	     * interfaces leave only that interface
	     */
	    (!forw_packet->own ||
	     forw_packet->if_incoming == primary_if)) {
		res = true;
		goto out;
	}

	/* if the incoming packet is sent via this one
	 * interface only - we still can aggregate
	 */
	if (directlink &&
	    new_bat_ogm_packet->ttl == 1 &&
	    forw_packet->if_incoming == if_incoming &&

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

out:
	if (primary_if)
		batadv_hardif_put(primary_if);
	return res;
}

/**
 * batadv_iv_ogm_aggregate_new - create a new aggregated packet and add this
 *  packet to it.
 * @packet_buff: pointer to the OGM
 * @packet_len: (total) length of the OGM
 * @send_time: timestamp (jiffies) when the packet is to be sent
 * @direct_link: whether this OGM has direct link status
 * @if_incoming: interface where the packet was received
 * @if_outgoing: interface for which the retransmission should be considered
 * @own_packet: true if it is a self-generated ogm
 */
static void batadv_iv_ogm_aggregate_new(const unsigned char *packet_buff,
					int packet_len, unsigned long send_time,
					bool direct_link,
					struct batadv_hard_iface *if_incoming,
					struct batadv_hard_iface *if_outgoing,
					int own_packet)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_forw_packet *forw_packet_aggr;
	unsigned char *skb_buff;
	unsigned int skb_size;

	/* own packet should always be scheduled */
	if (!own_packet) {
		if (!batadv_atomic_dec_not_zero(&bat_priv->batman_queue_left)) {
			batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
				   "batman packet queue full\n");
			return;
		}
	}

	forw_packet_aggr = kmalloc(sizeof(*forw_packet_aggr), GFP_ATOMIC);
	if (!forw_packet_aggr)
		goto out_nomem;

	if (atomic_read(&bat_priv->aggregated_ogms) &&
	    packet_len < BATADV_MAX_AGGREGATION_BYTES)
		skb_size = BATADV_MAX_AGGREGATION_BYTES;
	else
		skb_size = packet_len;

	skb_size += ETH_HLEN;

	forw_packet_aggr->skb = netdev_alloc_skb_ip_align(NULL, skb_size);
	if (!forw_packet_aggr->skb)
		goto out_free_forw_packet;
	forw_packet_aggr->skb->priority = TC_PRIO_CONTROL;
	skb_reserve(forw_packet_aggr->skb, ETH_HLEN);

	skb_buff = skb_put(forw_packet_aggr->skb, packet_len);
	forw_packet_aggr->packet_len = packet_len;
	memcpy(skb_buff, packet_buff, packet_len);

	kref_get(&if_incoming->refcount);
	kref_get(&if_outgoing->refcount);
	forw_packet_aggr->own = own_packet;
	forw_packet_aggr->if_incoming = if_incoming;
	forw_packet_aggr->if_outgoing = if_outgoing;
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
out_free_forw_packet:
	kfree(forw_packet_aggr);
out_nomem:
	if (!own_packet)
		atomic_inc(&bat_priv->batman_queue_left);
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

/**
 * batadv_iv_ogm_queue_add - queue up an OGM for transmission
 * @bat_priv: the bat priv with all the soft interface information
 * @packet_buff: pointer to the OGM
 * @packet_len: (total) length of the OGM
 * @if_incoming: interface where the packet was received
 * @if_outgoing: interface for which the retransmission should be considered
 * @own_packet: true if it is a self-generated ogm
 * @send_time: timestamp (jiffies) when the packet is to be sent
 */
static void batadv_iv_ogm_queue_add(struct batadv_priv *bat_priv,
				    unsigned char *packet_buff,
				    int packet_len,
				    struct batadv_hard_iface *if_incoming,
				    struct batadv_hard_iface *if_outgoing,
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
	direct_link = !!(batadv_ogm_packet->flags & BATADV_DIRECTLINK);
	max_aggregation_jiffies = msecs_to_jiffies(BATADV_MAX_AGGREGATION_MS);

	/* find position for the packet in the forward queue */
	spin_lock_bh(&bat_priv->forw_bat_list_lock);
	/* own packets are not to be aggregated */
	if (atomic_read(&bat_priv->aggregated_ogms) && !own_packet) {
		hlist_for_each_entry(forw_packet_pos,
				     &bat_priv->forw_bat_list, list) {
			if (batadv_iv_ogm_can_aggregate(batadv_ogm_packet,
							bat_priv, packet_len,
							send_time, direct_link,
							if_incoming,
							if_outgoing,
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
					    if_incoming, if_outgoing,
					    own_packet);
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
				  struct batadv_hard_iface *if_incoming,
				  struct batadv_hard_iface *if_outgoing)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	u16 tvlv_len;

	if (batadv_ogm_packet->ttl <= 1) {
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

	batadv_ogm_packet->ttl--;
	ether_addr_copy(batadv_ogm_packet->prev_sender, ethhdr->h_source);

	/* apply hop penalty */
	batadv_ogm_packet->tq = batadv_hop_penalty(batadv_ogm_packet->tq,
						   bat_priv);

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Forwarding packet: tq: %i, ttl: %i\n",
		   batadv_ogm_packet->tq, batadv_ogm_packet->ttl);

	if (is_single_hop_neigh)
		batadv_ogm_packet->flags |= BATADV_DIRECTLINK;
	else
		batadv_ogm_packet->flags &= ~BATADV_DIRECTLINK;

	batadv_iv_ogm_queue_add(bat_priv, (unsigned char *)batadv_ogm_packet,
				BATADV_OGM_HLEN + tvlv_len,
				if_incoming, if_outgoing, 0,
				batadv_iv_ogm_fwd_send_time());
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
	u32 i;
	size_t word_index;
	u8 *w;
	int if_num;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, head, hash_entry) {
			spin_lock_bh(&orig_node->bat_iv.ogm_cnt_lock);
			word_index = hard_iface->if_num * BATADV_NUM_WORDS;
			word = &orig_node->bat_iv.bcast_own[word_index];

			batadv_bit_get_packet(bat_priv, word, 1, 0);
			if_num = hard_iface->if_num;
			w = &orig_node->bat_iv.bcast_own_sum[if_num];
			*w = bitmap_weight(word, BATADV_TQ_LOCAL_WINDOW_SIZE);
			spin_unlock_bh(&orig_node->bat_iv.ogm_cnt_lock);
		}
		rcu_read_unlock();
	}
}

static void batadv_iv_ogm_schedule(struct batadv_hard_iface *hard_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	unsigned char **ogm_buff = &hard_iface->bat_iv.ogm_buff;
	struct batadv_ogm_packet *batadv_ogm_packet;
	struct batadv_hard_iface *primary_if, *tmp_hard_iface;
	int *ogm_buff_len = &hard_iface->bat_iv.ogm_buff_len;
	u32 seqno;
	u16 tvlv_len = 0;
	unsigned long send_time;

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
	seqno = (u32)atomic_read(&hard_iface->bat_iv.ogm_seqno);
	batadv_ogm_packet->seqno = htonl(seqno);
	atomic_inc(&hard_iface->bat_iv.ogm_seqno);

	batadv_iv_ogm_slide_own_bcast_window(hard_iface);

	send_time = batadv_iv_ogm_emit_send_time(bat_priv);

	if (hard_iface != primary_if) {
		/* OGMs from secondary interfaces are only scheduled on their
		 * respective interfaces.
		 */
		batadv_iv_ogm_queue_add(bat_priv, *ogm_buff, *ogm_buff_len,
					hard_iface, hard_iface, 1, send_time);
		goto out;
	}

	/* OGMs from primary interfaces are scheduled on all
	 * interfaces.
	 */
	rcu_read_lock();
	list_for_each_entry_rcu(tmp_hard_iface, &batadv_hardif_list, list) {
		if (tmp_hard_iface->soft_iface != hard_iface->soft_iface)
			continue;

		if (!kref_get_unless_zero(&tmp_hard_iface->refcount))
			continue;

		batadv_iv_ogm_queue_add(bat_priv, *ogm_buff,
					*ogm_buff_len, hard_iface,
					tmp_hard_iface, 1, send_time);

		batadv_hardif_put(tmp_hard_iface);
	}
	rcu_read_unlock();

out:
	if (primary_if)
		batadv_hardif_put(primary_if);
}

/**
 * batadv_iv_ogm_orig_update - use OGM to update corresponding data in an
 *  originator
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: the orig node who originally emitted the ogm packet
 * @orig_ifinfo: ifinfo for the outgoing interface of the orig_node
 * @ethhdr: Ethernet header of the OGM
 * @batadv_ogm_packet: the ogm packet
 * @if_incoming: interface where the packet was received
 * @if_outgoing: interface for which the retransmission should be considered
 * @dup_status: the duplicate status of this ogm packet.
 */
static void
batadv_iv_ogm_orig_update(struct batadv_priv *bat_priv,
			  struct batadv_orig_node *orig_node,
			  struct batadv_orig_ifinfo *orig_ifinfo,
			  const struct ethhdr *ethhdr,
			  const struct batadv_ogm_packet *batadv_ogm_packet,
			  struct batadv_hard_iface *if_incoming,
			  struct batadv_hard_iface *if_outgoing,
			  enum batadv_dup_status dup_status)
{
	struct batadv_neigh_ifinfo *neigh_ifinfo = NULL;
	struct batadv_neigh_ifinfo *router_ifinfo = NULL;
	struct batadv_neigh_node *neigh_node = NULL;
	struct batadv_neigh_node *tmp_neigh_node = NULL;
	struct batadv_neigh_node *router = NULL;
	struct batadv_orig_node *orig_node_tmp;
	int if_num;
	u8 sum_orig, sum_neigh;
	u8 *neigh_addr;
	u8 tq_avg;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "update_originator(): Searching and updating originator entry of received packet\n");

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node,
				 &orig_node->neigh_list, list) {
		neigh_addr = tmp_neigh_node->addr;
		if (batadv_compare_eth(neigh_addr, ethhdr->h_source) &&
		    tmp_neigh_node->if_incoming == if_incoming &&
		    kref_get_unless_zero(&tmp_neigh_node->refcount)) {
			if (WARN(neigh_node, "too many matching neigh_nodes"))
				batadv_neigh_node_put(neigh_node);
			neigh_node = tmp_neigh_node;
			continue;
		}

		if (dup_status != BATADV_NO_DUP)
			continue;

		/* only update the entry for this outgoing interface */
		neigh_ifinfo = batadv_neigh_ifinfo_get(tmp_neigh_node,
						       if_outgoing);
		if (!neigh_ifinfo)
			continue;

		spin_lock_bh(&tmp_neigh_node->ifinfo_lock);
		batadv_ring_buffer_set(neigh_ifinfo->bat_iv.tq_recv,
				       &neigh_ifinfo->bat_iv.tq_index, 0);
		tq_avg = batadv_ring_buffer_avg(neigh_ifinfo->bat_iv.tq_recv);
		neigh_ifinfo->bat_iv.tq_avg = tq_avg;
		spin_unlock_bh(&tmp_neigh_node->ifinfo_lock);

		batadv_neigh_ifinfo_put(neigh_ifinfo);
		neigh_ifinfo = NULL;
	}

	if (!neigh_node) {
		struct batadv_orig_node *orig_tmp;

		orig_tmp = batadv_iv_ogm_orig_get(bat_priv, ethhdr->h_source);
		if (!orig_tmp)
			goto unlock;

		neigh_node = batadv_iv_ogm_neigh_new(if_incoming,
						     ethhdr->h_source,
						     orig_node, orig_tmp);

		batadv_orig_node_put(orig_tmp);
		if (!neigh_node)
			goto unlock;
	} else {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Updating existing last-hop neighbor of originator\n");
	}

	rcu_read_unlock();
	neigh_ifinfo = batadv_neigh_ifinfo_new(neigh_node, if_outgoing);
	if (!neigh_ifinfo)
		goto out;

	neigh_node->last_seen = jiffies;

	spin_lock_bh(&neigh_node->ifinfo_lock);
	batadv_ring_buffer_set(neigh_ifinfo->bat_iv.tq_recv,
			       &neigh_ifinfo->bat_iv.tq_index,
			       batadv_ogm_packet->tq);
	tq_avg = batadv_ring_buffer_avg(neigh_ifinfo->bat_iv.tq_recv);
	neigh_ifinfo->bat_iv.tq_avg = tq_avg;
	spin_unlock_bh(&neigh_node->ifinfo_lock);

	if (dup_status == BATADV_NO_DUP) {
		orig_ifinfo->last_ttl = batadv_ogm_packet->ttl;
		neigh_ifinfo->last_ttl = batadv_ogm_packet->ttl;
	}

	/* if this neighbor already is our next hop there is nothing
	 * to change
	 */
	router = batadv_orig_router_get(orig_node, if_outgoing);
	if (router == neigh_node)
		goto out;

	if (router) {
		router_ifinfo = batadv_neigh_ifinfo_get(router, if_outgoing);
		if (!router_ifinfo)
			goto out;

		/* if this neighbor does not offer a better TQ we won't
		 * consider it
		 */
		if (router_ifinfo->bat_iv.tq_avg > neigh_ifinfo->bat_iv.tq_avg)
			goto out;
	}

	/* if the TQ is the same and the link not more symmetric we
	 * won't consider it either
	 */
	if (router_ifinfo &&
	    neigh_ifinfo->bat_iv.tq_avg == router_ifinfo->bat_iv.tq_avg) {
		orig_node_tmp = router->orig_node;
		spin_lock_bh(&orig_node_tmp->bat_iv.ogm_cnt_lock);
		if_num = router->if_incoming->if_num;
		sum_orig = orig_node_tmp->bat_iv.bcast_own_sum[if_num];
		spin_unlock_bh(&orig_node_tmp->bat_iv.ogm_cnt_lock);

		orig_node_tmp = neigh_node->orig_node;
		spin_lock_bh(&orig_node_tmp->bat_iv.ogm_cnt_lock);
		if_num = neigh_node->if_incoming->if_num;
		sum_neigh = orig_node_tmp->bat_iv.bcast_own_sum[if_num];
		spin_unlock_bh(&orig_node_tmp->bat_iv.ogm_cnt_lock);

		if (sum_orig >= sum_neigh)
			goto out;
	}

	batadv_update_route(bat_priv, orig_node, if_outgoing, neigh_node);
	goto out;

unlock:
	rcu_read_unlock();
out:
	if (neigh_node)
		batadv_neigh_node_put(neigh_node);
	if (router)
		batadv_neigh_node_put(router);
	if (neigh_ifinfo)
		batadv_neigh_ifinfo_put(neigh_ifinfo);
	if (router_ifinfo)
		batadv_neigh_ifinfo_put(router_ifinfo);
}

/**
 * batadv_iv_ogm_calc_tq - calculate tq for current received ogm packet
 * @orig_node: the orig node who originally emitted the ogm packet
 * @orig_neigh_node: the orig node struct of the neighbor who sent the packet
 * @batadv_ogm_packet: the ogm packet
 * @if_incoming: interface where the packet was received
 * @if_outgoing: interface for which the retransmission should be considered
 *
 * Return: true if the link can be considered bidirectional, false otherwise
 */
static bool batadv_iv_ogm_calc_tq(struct batadv_orig_node *orig_node,
				  struct batadv_orig_node *orig_neigh_node,
				  struct batadv_ogm_packet *batadv_ogm_packet,
				  struct batadv_hard_iface *if_incoming,
				  struct batadv_hard_iface *if_outgoing)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_neigh_node *neigh_node = NULL, *tmp_neigh_node;
	struct batadv_neigh_ifinfo *neigh_ifinfo;
	u8 total_count;
	u8 orig_eq_count, neigh_rq_count, neigh_rq_inv, tq_own;
	unsigned int neigh_rq_inv_cube, neigh_rq_max_cube;
	int tq_asym_penalty, inv_asym_penalty, if_num;
	unsigned int combined_tq;
	int tq_iface_penalty;
	bool ret = false;

	/* find corresponding one hop neighbor */
	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node,
				 &orig_neigh_node->neigh_list, list) {
		if (!batadv_compare_eth(tmp_neigh_node->addr,
					orig_neigh_node->orig))
			continue;

		if (tmp_neigh_node->if_incoming != if_incoming)
			continue;

		if (!kref_get_unless_zero(&tmp_neigh_node->refcount))
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
	spin_lock_bh(&orig_node->bat_iv.ogm_cnt_lock);
	if_num = if_incoming->if_num;
	orig_eq_count = orig_neigh_node->bat_iv.bcast_own_sum[if_num];
	neigh_ifinfo = batadv_neigh_ifinfo_new(neigh_node, if_outgoing);
	if (neigh_ifinfo) {
		neigh_rq_count = neigh_ifinfo->bat_iv.real_packet_count;
		batadv_neigh_ifinfo_put(neigh_ifinfo);
	} else {
		neigh_rq_count = 0;
	}
	spin_unlock_bh(&orig_node->bat_iv.ogm_cnt_lock);

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

	/* penalize if the OGM is forwarded on the same interface. WiFi
	 * interfaces and other half duplex devices suffer from throughput
	 * drops as they can't send and receive at the same time.
	 */
	tq_iface_penalty = BATADV_TQ_MAX_VALUE;
	if (if_outgoing && (if_incoming == if_outgoing) &&
	    batadv_is_wifi_netdev(if_outgoing->net_dev))
		tq_iface_penalty = batadv_hop_penalty(BATADV_TQ_MAX_VALUE,
						      bat_priv);

	combined_tq = batadv_ogm_packet->tq *
		      tq_own *
		      tq_asym_penalty *
		      tq_iface_penalty;
	combined_tq /= BATADV_TQ_MAX_VALUE *
		       BATADV_TQ_MAX_VALUE *
		       BATADV_TQ_MAX_VALUE;
	batadv_ogm_packet->tq = combined_tq;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "bidirectional: orig = %-15pM neigh = %-15pM => own_bcast = %2i, real recv = %2i, local tq: %3i, asym_penalty: %3i, iface_penalty: %3i, total tq: %3i, if_incoming = %s, if_outgoing = %s\n",
		   orig_node->orig, orig_neigh_node->orig, total_count,
		   neigh_rq_count, tq_own, tq_asym_penalty, tq_iface_penalty,
		   batadv_ogm_packet->tq, if_incoming->net_dev->name,
		   if_outgoing ? if_outgoing->net_dev->name : "DEFAULT");

	/* if link has the minimum required transmission quality
	 * consider it bidirectional
	 */
	if (batadv_ogm_packet->tq >= BATADV_TQ_TOTAL_BIDRECT_LIMIT)
		ret = true;

out:
	if (neigh_node)
		batadv_neigh_node_put(neigh_node);
	return ret;
}

/**
 * batadv_iv_ogm_update_seqnos -  process a batman packet for all interfaces,
 *  adjust the sequence number and find out whether it is a duplicate
 * @ethhdr: ethernet header of the packet
 * @batadv_ogm_packet: OGM packet to be considered
 * @if_incoming: interface on which the OGM packet was received
 * @if_outgoing: interface for which the retransmission should be considered
 *
 * Return: duplicate status as enum batadv_dup_status
 */
static enum batadv_dup_status
batadv_iv_ogm_update_seqnos(const struct ethhdr *ethhdr,
			    const struct batadv_ogm_packet *batadv_ogm_packet,
			    const struct batadv_hard_iface *if_incoming,
			    struct batadv_hard_iface *if_outgoing)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_orig_node *orig_node;
	struct batadv_orig_ifinfo *orig_ifinfo = NULL;
	struct batadv_neigh_node *neigh_node;
	struct batadv_neigh_ifinfo *neigh_ifinfo;
	bool is_dup;
	s32 seq_diff;
	bool need_update = false;
	int set_mark;
	enum batadv_dup_status ret = BATADV_NO_DUP;
	u32 seqno = ntohl(batadv_ogm_packet->seqno);
	u8 *neigh_addr;
	u8 packet_count;
	unsigned long *bitmap;

	orig_node = batadv_iv_ogm_orig_get(bat_priv, batadv_ogm_packet->orig);
	if (!orig_node)
		return BATADV_NO_DUP;

	orig_ifinfo = batadv_orig_ifinfo_new(orig_node, if_outgoing);
	if (WARN_ON(!orig_ifinfo)) {
		batadv_orig_node_put(orig_node);
		return 0;
	}

	spin_lock_bh(&orig_node->bat_iv.ogm_cnt_lock);
	seq_diff = seqno - orig_ifinfo->last_real_seqno;

	/* signalize caller that the packet is to be dropped. */
	if (!hlist_empty(&orig_node->neigh_list) &&
	    batadv_window_protected(bat_priv, seq_diff,
				    BATADV_TQ_LOCAL_WINDOW_SIZE,
				    &orig_ifinfo->batman_seqno_reset, NULL)) {
		ret = BATADV_PROTECTED;
		goto out;
	}

	rcu_read_lock();
	hlist_for_each_entry_rcu(neigh_node, &orig_node->neigh_list, list) {
		neigh_ifinfo = batadv_neigh_ifinfo_new(neigh_node,
						       if_outgoing);
		if (!neigh_ifinfo)
			continue;

		neigh_addr = neigh_node->addr;
		is_dup = batadv_test_bit(neigh_ifinfo->bat_iv.real_bits,
					 orig_ifinfo->last_real_seqno,
					 seqno);

		if (batadv_compare_eth(neigh_addr, ethhdr->h_source) &&
		    neigh_node->if_incoming == if_incoming) {
			set_mark = 1;
			if (is_dup)
				ret = BATADV_NEIGH_DUP;
		} else {
			set_mark = 0;
			if (is_dup && (ret != BATADV_NEIGH_DUP))
				ret = BATADV_ORIG_DUP;
		}

		/* if the window moved, set the update flag. */
		bitmap = neigh_ifinfo->bat_iv.real_bits;
		need_update |= batadv_bit_get_packet(bat_priv, bitmap,
						     seq_diff, set_mark);

		packet_count = bitmap_weight(bitmap,
					     BATADV_TQ_LOCAL_WINDOW_SIZE);
		neigh_ifinfo->bat_iv.real_packet_count = packet_count;
		batadv_neigh_ifinfo_put(neigh_ifinfo);
	}
	rcu_read_unlock();

	if (need_update) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "%s updating last_seqno: old %u, new %u\n",
			   if_outgoing ? if_outgoing->net_dev->name : "DEFAULT",
			   orig_ifinfo->last_real_seqno, seqno);
		orig_ifinfo->last_real_seqno = seqno;
	}

out:
	spin_unlock_bh(&orig_node->bat_iv.ogm_cnt_lock);
	batadv_orig_node_put(orig_node);
	batadv_orig_ifinfo_put(orig_ifinfo);
	return ret;
}

/**
 * batadv_iv_ogm_process_per_outif - process a batman iv OGM for an outgoing if
 * @skb: the skb containing the OGM
 * @ogm_offset: offset from skb->data to start of ogm header
 * @orig_node: the (cached) orig node for the originator of this OGM
 * @if_incoming: the interface where this packet was received
 * @if_outgoing: the interface for which the packet should be considered
 */
static void
batadv_iv_ogm_process_per_outif(const struct sk_buff *skb, int ogm_offset,
				struct batadv_orig_node *orig_node,
				struct batadv_hard_iface *if_incoming,
				struct batadv_hard_iface *if_outgoing)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_hardif_neigh_node *hardif_neigh = NULL;
	struct batadv_neigh_node *router = NULL;
	struct batadv_neigh_node *router_router = NULL;
	struct batadv_orig_node *orig_neigh_node;
	struct batadv_orig_ifinfo *orig_ifinfo;
	struct batadv_neigh_node *orig_neigh_router = NULL;
	struct batadv_neigh_ifinfo *router_ifinfo = NULL;
	struct batadv_ogm_packet *ogm_packet;
	enum batadv_dup_status dup_status;
	bool is_from_best_next_hop = false;
	bool is_single_hop_neigh = false;
	bool sameseq, similar_ttl;
	struct sk_buff *skb_priv;
	struct ethhdr *ethhdr;
	u8 *prev_sender;
	bool is_bidirect;

	/* create a private copy of the skb, as some functions change tq value
	 * and/or flags.
	 */
	skb_priv = skb_copy(skb, GFP_ATOMIC);
	if (!skb_priv)
		return;

	ethhdr = eth_hdr(skb_priv);
	ogm_packet = (struct batadv_ogm_packet *)(skb_priv->data + ogm_offset);

	dup_status = batadv_iv_ogm_update_seqnos(ethhdr, ogm_packet,
						 if_incoming, if_outgoing);
	if (batadv_compare_eth(ethhdr->h_source, ogm_packet->orig))
		is_single_hop_neigh = true;

	if (dup_status == BATADV_PROTECTED) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: packet within seqno protection time (sender: %pM)\n",
			   ethhdr->h_source);
		goto out;
	}

	if (ogm_packet->tq == 0) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: originator packet with tq equal 0\n");
		goto out;
	}

	if (is_single_hop_neigh) {
		hardif_neigh = batadv_hardif_neigh_get(if_incoming,
						       ethhdr->h_source);
		if (hardif_neigh)
			hardif_neigh->last_seen = jiffies;
	}

	router = batadv_orig_router_get(orig_node, if_outgoing);
	if (router) {
		router_router = batadv_orig_router_get(router->orig_node,
						       if_outgoing);
		router_ifinfo = batadv_neigh_ifinfo_get(router, if_outgoing);
	}

	if ((router_ifinfo && router_ifinfo->bat_iv.tq_avg != 0) &&
	    (batadv_compare_eth(router->addr, ethhdr->h_source)))
		is_from_best_next_hop = true;

	prev_sender = ogm_packet->prev_sender;
	/* avoid temporary routing loops */
	if (router && router_router &&
	    (batadv_compare_eth(router->addr, prev_sender)) &&
	    !(batadv_compare_eth(ogm_packet->orig, prev_sender)) &&
	    (batadv_compare_eth(router->addr, router_router->addr))) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: ignoring all rebroadcast packets that may make me loop (sender: %pM)\n",
			   ethhdr->h_source);
		goto out;
	}

	if (if_outgoing == BATADV_IF_DEFAULT)
		batadv_tvlv_ogm_receive(bat_priv, ogm_packet, orig_node);

	/* if sender is a direct neighbor the sender mac equals
	 * originator mac
	 */
	if (is_single_hop_neigh)
		orig_neigh_node = orig_node;
	else
		orig_neigh_node = batadv_iv_ogm_orig_get(bat_priv,
							 ethhdr->h_source);

	if (!orig_neigh_node)
		goto out;

	/* Update nc_nodes of the originator */
	batadv_nc_update_nc_node(bat_priv, orig_node, orig_neigh_node,
				 ogm_packet, is_single_hop_neigh);

	orig_neigh_router = batadv_orig_router_get(orig_neigh_node,
						   if_outgoing);

	/* drop packet if sender is not a direct neighbor and if we
	 * don't route towards it
	 */
	if (!is_single_hop_neigh && (!orig_neigh_router)) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: OGM via unknown neighbor!\n");
		goto out_neigh;
	}

	is_bidirect = batadv_iv_ogm_calc_tq(orig_node, orig_neigh_node,
					    ogm_packet, if_incoming,
					    if_outgoing);

	/* update ranking if it is not a duplicate or has the same
	 * seqno and similar ttl as the non-duplicate
	 */
	orig_ifinfo = batadv_orig_ifinfo_new(orig_node, if_outgoing);
	if (!orig_ifinfo)
		goto out_neigh;

	sameseq = orig_ifinfo->last_real_seqno == ntohl(ogm_packet->seqno);
	similar_ttl = (orig_ifinfo->last_ttl - 3) <= ogm_packet->ttl;

	if (is_bidirect && ((dup_status == BATADV_NO_DUP) ||
			    (sameseq && similar_ttl))) {
		batadv_iv_ogm_orig_update(bat_priv, orig_node,
					  orig_ifinfo, ethhdr,
					  ogm_packet, if_incoming,
					  if_outgoing, dup_status);
	}
	batadv_orig_ifinfo_put(orig_ifinfo);

	/* only forward for specific interface, not for the default one. */
	if (if_outgoing == BATADV_IF_DEFAULT)
		goto out_neigh;

	/* is single hop (direct) neighbor */
	if (is_single_hop_neigh) {
		/* OGMs from secondary interfaces should only scheduled once
		 * per interface where it has been received, not multiple times
		 */
		if ((ogm_packet->ttl <= 2) &&
		    (if_incoming != if_outgoing)) {
			batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
				   "Drop packet: OGM from secondary interface and wrong outgoing interface\n");
			goto out_neigh;
		}
		/* mark direct link on incoming interface */
		batadv_iv_ogm_forward(orig_node, ethhdr, ogm_packet,
				      is_single_hop_neigh,
				      is_from_best_next_hop, if_incoming,
				      if_outgoing);

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
	batadv_iv_ogm_forward(orig_node, ethhdr, ogm_packet,
			      is_single_hop_neigh, is_from_best_next_hop,
			      if_incoming, if_outgoing);

out_neigh:
	if ((orig_neigh_node) && (!is_single_hop_neigh))
		batadv_orig_node_put(orig_neigh_node);
out:
	if (router_ifinfo)
		batadv_neigh_ifinfo_put(router_ifinfo);
	if (router)
		batadv_neigh_node_put(router);
	if (router_router)
		batadv_neigh_node_put(router_router);
	if (orig_neigh_router)
		batadv_neigh_node_put(orig_neigh_router);
	if (hardif_neigh)
		batadv_hardif_neigh_put(hardif_neigh);

	kfree_skb(skb_priv);
}

/**
 * batadv_iv_ogm_process - process an incoming batman iv OGM
 * @skb: the skb containing the OGM
 * @ogm_offset: offset to the OGM which should be processed (for aggregates)
 * @if_incoming: the interface where this packet was receved
 */
static void batadv_iv_ogm_process(const struct sk_buff *skb, int ogm_offset,
				  struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_orig_node *orig_neigh_node, *orig_node;
	struct batadv_hard_iface *hard_iface;
	struct batadv_ogm_packet *ogm_packet;
	u32 if_incoming_seqno;
	bool has_directlink_flag;
	struct ethhdr *ethhdr;
	bool is_my_oldorig = false;
	bool is_my_addr = false;
	bool is_my_orig = false;

	ogm_packet = (struct batadv_ogm_packet *)(skb->data + ogm_offset);
	ethhdr = eth_hdr(skb);

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
	if (ogm_packet->packet_type != BATADV_IV_OGM)
		return;

	/* could be changed by schedule_own_packet() */
	if_incoming_seqno = atomic_read(&if_incoming->bat_iv.ogm_seqno);

	if (ogm_packet->flags & BATADV_DIRECTLINK)
		has_directlink_flag = true;
	else
		has_directlink_flag = false;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Received BATMAN packet via NB: %pM, IF: %s [%pM] (from OG: %pM, via prev OG: %pM, seqno %u, tq %d, TTL %d, V %d, IDF %d)\n",
		   ethhdr->h_source, if_incoming->net_dev->name,
		   if_incoming->net_dev->dev_addr, ogm_packet->orig,
		   ogm_packet->prev_sender, ntohl(ogm_packet->seqno),
		   ogm_packet->tq, ogm_packet->ttl,
		   ogm_packet->version, has_directlink_flag);

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->if_status != BATADV_IF_ACTIVE)
			continue;

		if (hard_iface->soft_iface != if_incoming->soft_iface)
			continue;

		if (batadv_compare_eth(ethhdr->h_source,
				       hard_iface->net_dev->dev_addr))
			is_my_addr = true;

		if (batadv_compare_eth(ogm_packet->orig,
				       hard_iface->net_dev->dev_addr))
			is_my_orig = true;

		if (batadv_compare_eth(ogm_packet->prev_sender,
				       hard_iface->net_dev->dev_addr))
			is_my_oldorig = true;
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
		s32 bit_pos;
		s16 if_num;
		u8 *weight;

		orig_neigh_node = batadv_iv_ogm_orig_get(bat_priv,
							 ethhdr->h_source);
		if (!orig_neigh_node)
			return;

		/* neighbor has to indicate direct link and it has to
		 * come via the corresponding interface
		 * save packet seqno for bidirectional check
		 */
		if (has_directlink_flag &&
		    batadv_compare_eth(if_incoming->net_dev->dev_addr,
				       ogm_packet->orig)) {
			if_num = if_incoming->if_num;
			offset = if_num * BATADV_NUM_WORDS;

			spin_lock_bh(&orig_neigh_node->bat_iv.ogm_cnt_lock);
			word = &orig_neigh_node->bat_iv.bcast_own[offset];
			bit_pos = if_incoming_seqno - 2;
			bit_pos -= ntohl(ogm_packet->seqno);
			batadv_set_bit(word, bit_pos);
			weight = &orig_neigh_node->bat_iv.bcast_own_sum[if_num];
			*weight = bitmap_weight(word,
						BATADV_TQ_LOCAL_WINDOW_SIZE);
			spin_unlock_bh(&orig_neigh_node->bat_iv.ogm_cnt_lock);
		}

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: originator packet from myself (via neighbor)\n");
		batadv_orig_node_put(orig_neigh_node);
		return;
	}

	if (is_my_oldorig) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: ignoring all rebroadcast echos (sender: %pM)\n",
			   ethhdr->h_source);
		return;
	}

	if (ogm_packet->flags & BATADV_NOT_BEST_NEXT_HOP) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: ignoring all packets not forwarded from the best next hop (sender: %pM)\n",
			   ethhdr->h_source);
		return;
	}

	orig_node = batadv_iv_ogm_orig_get(bat_priv, ogm_packet->orig);
	if (!orig_node)
		return;

	batadv_iv_ogm_process_per_outif(skb, ogm_offset, orig_node,
					if_incoming, BATADV_IF_DEFAULT);

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->if_status != BATADV_IF_ACTIVE)
			continue;

		if (hard_iface->soft_iface != bat_priv->soft_iface)
			continue;

		if (!kref_get_unless_zero(&hard_iface->refcount))
			continue;

		batadv_iv_ogm_process_per_outif(skb, ogm_offset, orig_node,
						if_incoming, hard_iface);

		batadv_hardif_put(hard_iface);
	}
	rcu_read_unlock();

	batadv_orig_node_put(orig_node);
}

static int batadv_iv_ogm_receive(struct sk_buff *skb,
				 struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_ogm_packet *ogm_packet;
	u8 *packet_pos;
	int ogm_offset;
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

	ogm_offset = 0;
	ogm_packet = (struct batadv_ogm_packet *)skb->data;

	/* unpack the aggregated packets and process them one by one */
	while (batadv_iv_ogm_aggr_packet(ogm_offset, skb_headlen(skb),
					 ogm_packet->tvlv_len)) {
		batadv_iv_ogm_process(skb, ogm_offset, if_incoming);

		ogm_offset += BATADV_OGM_HLEN;
		ogm_offset += ntohs(ogm_packet->tvlv_len);

		packet_pos = skb->data + ogm_offset;
		ogm_packet = (struct batadv_ogm_packet *)packet_pos;
	}

	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

/**
 * batadv_iv_ogm_orig_print_neigh - print neighbors for the originator table
 * @orig_node: the orig_node for which the neighbors are printed
 * @if_outgoing: outgoing interface for these entries
 * @seq: debugfs table seq_file struct
 *
 * Must be called while holding an rcu lock.
 */
static void
batadv_iv_ogm_orig_print_neigh(struct batadv_orig_node *orig_node,
			       struct batadv_hard_iface *if_outgoing,
			       struct seq_file *seq)
{
	struct batadv_neigh_node *neigh_node;
	struct batadv_neigh_ifinfo *n_ifinfo;

	hlist_for_each_entry_rcu(neigh_node, &orig_node->neigh_list, list) {
		n_ifinfo = batadv_neigh_ifinfo_get(neigh_node, if_outgoing);
		if (!n_ifinfo)
			continue;

		seq_printf(seq, " %pM (%3i)",
			   neigh_node->addr,
			   n_ifinfo->bat_iv.tq_avg);

		batadv_neigh_ifinfo_put(n_ifinfo);
	}
}

/**
 * batadv_iv_ogm_orig_print - print the originator table
 * @bat_priv: the bat priv with all the soft interface information
 * @seq: debugfs table seq_file struct
 * @if_outgoing: the outgoing interface for which this should be printed
 */
static void batadv_iv_ogm_orig_print(struct batadv_priv *bat_priv,
				     struct seq_file *seq,
				     struct batadv_hard_iface *if_outgoing)
{
	struct batadv_neigh_node *neigh_node;
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	int last_seen_msecs, last_seen_secs;
	struct batadv_orig_node *orig_node;
	struct batadv_neigh_ifinfo *n_ifinfo;
	unsigned long last_seen_jiffies;
	struct hlist_head *head;
	int batman_count = 0;
	u32 i;

	seq_puts(seq,
		 "  Originator      last-seen (#/255)           Nexthop [outgoingIF]:   Potential nexthops ...\n");

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, head, hash_entry) {
			neigh_node = batadv_orig_router_get(orig_node,
							    if_outgoing);
			if (!neigh_node)
				continue;

			n_ifinfo = batadv_neigh_ifinfo_get(neigh_node,
							   if_outgoing);
			if (!n_ifinfo)
				goto next;

			if (n_ifinfo->bat_iv.tq_avg == 0)
				goto next;

			last_seen_jiffies = jiffies - orig_node->last_seen;
			last_seen_msecs = jiffies_to_msecs(last_seen_jiffies);
			last_seen_secs = last_seen_msecs / 1000;
			last_seen_msecs = last_seen_msecs % 1000;

			seq_printf(seq, "%pM %4i.%03is   (%3i) %pM [%10s]:",
				   orig_node->orig, last_seen_secs,
				   last_seen_msecs, n_ifinfo->bat_iv.tq_avg,
				   neigh_node->addr,
				   neigh_node->if_incoming->net_dev->name);

			batadv_iv_ogm_orig_print_neigh(orig_node, if_outgoing,
						       seq);
			seq_puts(seq, "\n");
			batman_count++;

next:
			batadv_neigh_node_put(neigh_node);
			if (n_ifinfo)
				batadv_neigh_ifinfo_put(n_ifinfo);
		}
		rcu_read_unlock();
	}

	if (batman_count == 0)
		seq_puts(seq, "No batman nodes in range ...\n");
}

/**
 * batadv_iv_hardif_neigh_print - print a single hop neighbour node
 * @seq: neighbour table seq_file struct
 * @hardif_neigh: hardif neighbour information
 */
static void
batadv_iv_hardif_neigh_print(struct seq_file *seq,
			     struct batadv_hardif_neigh_node *hardif_neigh)
{
	int last_secs, last_msecs;

	last_secs = jiffies_to_msecs(jiffies - hardif_neigh->last_seen) / 1000;
	last_msecs = jiffies_to_msecs(jiffies - hardif_neigh->last_seen) % 1000;

	seq_printf(seq, "   %10s   %pM %4i.%03is\n",
		   hardif_neigh->if_incoming->net_dev->name,
		   hardif_neigh->addr, last_secs, last_msecs);
}

/**
 * batadv_iv_ogm_neigh_print - print the single hop neighbour list
 * @bat_priv: the bat priv with all the soft interface information
 * @seq: neighbour table seq_file struct
 */
static void batadv_iv_neigh_print(struct batadv_priv *bat_priv,
				  struct seq_file *seq)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_hardif_neigh_node *hardif_neigh;
	struct batadv_hard_iface *hard_iface;
	int batman_count = 0;

	seq_puts(seq, "           IF        Neighbor      last-seen\n");

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->soft_iface != net_dev)
			continue;

		hlist_for_each_entry_rcu(hardif_neigh,
					 &hard_iface->neigh_list, list) {
			batadv_iv_hardif_neigh_print(seq, hardif_neigh);
			batman_count++;
		}
	}
	rcu_read_unlock();

	if (batman_count == 0)
		seq_puts(seq, "No batman nodes in range ...\n");
}

/**
 * batadv_iv_ogm_neigh_cmp - compare the metrics of two neighbors
 * @neigh1: the first neighbor object of the comparison
 * @if_outgoing1: outgoing interface for the first neighbor
 * @neigh2: the second neighbor object of the comparison
 * @if_outgoing2: outgoing interface for the second neighbor
 *
 * Return: a value less, equal to or greater than 0 if the metric via neigh1 is
 * lower, the same as or higher than the metric via neigh2
 */
static int batadv_iv_ogm_neigh_cmp(struct batadv_neigh_node *neigh1,
				   struct batadv_hard_iface *if_outgoing1,
				   struct batadv_neigh_node *neigh2,
				   struct batadv_hard_iface *if_outgoing2)
{
	struct batadv_neigh_ifinfo *neigh1_ifinfo, *neigh2_ifinfo;
	u8 tq1, tq2;
	int diff;

	neigh1_ifinfo = batadv_neigh_ifinfo_get(neigh1, if_outgoing1);
	neigh2_ifinfo = batadv_neigh_ifinfo_get(neigh2, if_outgoing2);

	if (!neigh1_ifinfo || !neigh2_ifinfo) {
		diff = 0;
		goto out;
	}

	tq1 = neigh1_ifinfo->bat_iv.tq_avg;
	tq2 = neigh2_ifinfo->bat_iv.tq_avg;
	diff = tq1 - tq2;

out:
	if (neigh1_ifinfo)
		batadv_neigh_ifinfo_put(neigh1_ifinfo);
	if (neigh2_ifinfo)
		batadv_neigh_ifinfo_put(neigh2_ifinfo);

	return diff;
}

/**
 * batadv_iv_ogm_neigh_is_sob - check if neigh1 is similarly good or better
 *  than neigh2 from the metric prospective
 * @neigh1: the first neighbor object of the comparison
 * @if_outgoing1: outgoing interface for the first neighbor
 * @neigh2: the second neighbor object of the comparison
 * @if_outgoing2: outgoing interface for the second neighbor
 *
 * Return: true if the metric via neigh1 is equally good or better than
 * the metric via neigh2, false otherwise.
 */
static bool
batadv_iv_ogm_neigh_is_sob(struct batadv_neigh_node *neigh1,
			   struct batadv_hard_iface *if_outgoing1,
			   struct batadv_neigh_node *neigh2,
			   struct batadv_hard_iface *if_outgoing2)
{
	struct batadv_neigh_ifinfo *neigh1_ifinfo, *neigh2_ifinfo;
	u8 tq1, tq2;
	bool ret;

	neigh1_ifinfo = batadv_neigh_ifinfo_get(neigh1, if_outgoing1);
	neigh2_ifinfo = batadv_neigh_ifinfo_get(neigh2, if_outgoing2);

	/* we can't say that the metric is better */
	if (!neigh1_ifinfo || !neigh2_ifinfo) {
		ret = false;
		goto out;
	}

	tq1 = neigh1_ifinfo->bat_iv.tq_avg;
	tq2 = neigh2_ifinfo->bat_iv.tq_avg;
	ret = (tq1 - tq2) > -BATADV_TQ_SIMILARITY_THRESHOLD;

out:
	if (neigh1_ifinfo)
		batadv_neigh_ifinfo_put(neigh1_ifinfo);
	if (neigh2_ifinfo)
		batadv_neigh_ifinfo_put(neigh2_ifinfo);

	return ret;
}

static struct batadv_algo_ops batadv_batman_iv __read_mostly = {
	.name = "BATMAN_IV",
	.bat_iface_enable = batadv_iv_ogm_iface_enable,
	.bat_iface_disable = batadv_iv_ogm_iface_disable,
	.bat_iface_update_mac = batadv_iv_ogm_iface_update_mac,
	.bat_primary_iface_set = batadv_iv_ogm_primary_iface_set,
	.bat_ogm_schedule = batadv_iv_ogm_schedule,
	.bat_ogm_emit = batadv_iv_ogm_emit,
	.bat_neigh_cmp = batadv_iv_ogm_neigh_cmp,
	.bat_neigh_is_similar_or_better = batadv_iv_ogm_neigh_is_sob,
	.bat_neigh_print = batadv_iv_neigh_print,
	.bat_orig_print = batadv_iv_ogm_orig_print,
	.bat_orig_free = batadv_iv_ogm_orig_free,
	.bat_orig_add_if = batadv_iv_ogm_orig_add_if,
	.bat_orig_del_if = batadv_iv_ogm_orig_del_if,
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
