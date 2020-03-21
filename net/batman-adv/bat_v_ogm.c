// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2013-2018  B.A.T.M.A.N. contributors:
 *
 * Antonio Quartulli
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

#include "bat_v_ogm.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/byteorder/generic.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/gfp.h>
#include <linux/if_ether.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/random.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <uapi/linux/batadv_packet.h>

#include "bat_algo.h"
#include "hard-interface.h"
#include "hash.h"
#include "log.h"
#include "originator.h"
#include "routing.h"
#include "send.h"
#include "translation-table.h"
#include "tvlv.h"

/**
 * batadv_v_ogm_orig_get() - retrieve and possibly create an originator node
 * @bat_priv: the bat priv with all the soft interface information
 * @addr: the address of the originator
 *
 * Return: the orig_node corresponding to the specified address. If such object
 * does not exist it is allocated here. In case of allocation failure returns
 * NULL.
 */
struct batadv_orig_node *batadv_v_ogm_orig_get(struct batadv_priv *bat_priv,
					       const u8 *addr)
{
	struct batadv_orig_node *orig_node;
	int hash_added;

	orig_node = batadv_orig_hash_find(bat_priv, addr);
	if (orig_node)
		return orig_node;

	orig_node = batadv_orig_node_new(bat_priv, addr);
	if (!orig_node)
		return NULL;

	kref_get(&orig_node->refcount);
	hash_added = batadv_hash_add(bat_priv->orig_hash, batadv_compare_orig,
				     batadv_choose_orig, orig_node,
				     &orig_node->hash_entry);
	if (hash_added != 0) {
		/* remove refcnt for newly created orig_node and hash entry */
		batadv_orig_node_put(orig_node);
		batadv_orig_node_put(orig_node);
		orig_node = NULL;
	}

	return orig_node;
}

/**
 * batadv_v_ogm_start_timer() - restart the OGM sending timer
 * @bat_priv: the bat priv with all the soft interface information
 */
static void batadv_v_ogm_start_timer(struct batadv_priv *bat_priv)
{
	unsigned long msecs;
	/* this function may be invoked in different contexts (ogm rescheduling
	 * or hard_iface activation), but the work timer should not be reset
	 */
	if (delayed_work_pending(&bat_priv->bat_v.ogm_wq))
		return;

	msecs = atomic_read(&bat_priv->orig_interval) - BATADV_JITTER;
	msecs += prandom_u32() % (2 * BATADV_JITTER);
	queue_delayed_work(batadv_event_workqueue, &bat_priv->bat_v.ogm_wq,
			   msecs_to_jiffies(msecs));
}

/**
 * batadv_v_ogm_send_to_if() - send a batman ogm using a given interface
 * @skb: the OGM to send
 * @hard_iface: the interface to use to send the OGM
 */
static void batadv_v_ogm_send_to_if(struct sk_buff *skb,
				    struct batadv_hard_iface *hard_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);

	if (hard_iface->if_status != BATADV_IF_ACTIVE)
		return;

	batadv_inc_counter(bat_priv, BATADV_CNT_MGMT_TX);
	batadv_add_counter(bat_priv, BATADV_CNT_MGMT_TX_BYTES,
			   skb->len + ETH_HLEN);

	batadv_send_broadcast_skb(skb, hard_iface);
}

/**
 * batadv_v_ogm_send_softif() - periodic worker broadcasting the own OGM
 * @bat_priv: the bat priv with all the soft interface information
 */
static void batadv_v_ogm_send_softif(struct batadv_priv *bat_priv)
{
	struct batadv_hard_iface *hard_iface;
	struct batadv_ogm2_packet *ogm_packet;
	struct sk_buff *skb, *skb_tmp;
	unsigned char *ogm_buff;
	int ogm_buff_len;
	u16 tvlv_len = 0;
	int ret;

	lockdep_assert_held(&bat_priv->bat_v.ogm_buff_mutex);

	if (atomic_read(&bat_priv->mesh_state) == BATADV_MESH_DEACTIVATING)
		goto out;

	ogm_buff = bat_priv->bat_v.ogm_buff;
	ogm_buff_len = bat_priv->bat_v.ogm_buff_len;
	/* tt changes have to be committed before the tvlv data is
	 * appended as it may alter the tt tvlv container
	 */
	batadv_tt_local_commit_changes(bat_priv);
	tvlv_len = batadv_tvlv_container_ogm_append(bat_priv, &ogm_buff,
						    &ogm_buff_len,
						    BATADV_OGM2_HLEN);

	bat_priv->bat_v.ogm_buff = ogm_buff;
	bat_priv->bat_v.ogm_buff_len = ogm_buff_len;

	skb = netdev_alloc_skb_ip_align(NULL, ETH_HLEN + ogm_buff_len);
	if (!skb)
		goto reschedule;

	skb_reserve(skb, ETH_HLEN);
	skb_put_data(skb, ogm_buff, ogm_buff_len);

	ogm_packet = (struct batadv_ogm2_packet *)skb->data;
	ogm_packet->seqno = htonl(atomic_read(&bat_priv->bat_v.ogm_seqno));
	atomic_inc(&bat_priv->bat_v.ogm_seqno);
	ogm_packet->tvlv_len = htons(tvlv_len);

	/* broadcast on every interface */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->soft_iface != bat_priv->soft_iface)
			continue;

		if (!kref_get_unless_zero(&hard_iface->refcount))
			continue;

		ret = batadv_hardif_no_broadcast(hard_iface, NULL, NULL);
		if (ret) {
			char *type;

			switch (ret) {
			case BATADV_HARDIF_BCAST_NORECIPIENT:
				type = "no neighbor";
				break;
			case BATADV_HARDIF_BCAST_DUPFWD:
				type = "single neighbor is source";
				break;
			case BATADV_HARDIF_BCAST_DUPORIG:
				type = "single neighbor is originator";
				break;
			default:
				type = "unknown";
			}

			batadv_dbg(BATADV_DBG_BATMAN, bat_priv, "OGM2 from ourselves on %s suppressed: %s\n",
				   hard_iface->net_dev->name, type);

			batadv_hardif_put(hard_iface);
			continue;
		}

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Sending own OGM2 packet (originator %pM, seqno %u, throughput %u, TTL %d) on interface %s [%pM]\n",
			   ogm_packet->orig, ntohl(ogm_packet->seqno),
			   ntohl(ogm_packet->throughput), ogm_packet->ttl,
			   hard_iface->net_dev->name,
			   hard_iface->net_dev->dev_addr);

		/* this skb gets consumed by batadv_v_ogm_send_to_if() */
		skb_tmp = skb_clone(skb, GFP_ATOMIC);
		if (!skb_tmp) {
			batadv_hardif_put(hard_iface);
			break;
		}

		batadv_v_ogm_send_to_if(skb_tmp, hard_iface);
		batadv_hardif_put(hard_iface);
	}
	rcu_read_unlock();

	consume_skb(skb);

reschedule:
	batadv_v_ogm_start_timer(bat_priv);
out:
	return;
}

/**
 * batadv_v_ogm_send() - periodic worker broadcasting the own OGM
 * @work: work queue item
 */
static void batadv_v_ogm_send(struct work_struct *work)
{
	struct batadv_priv_bat_v *bat_v;
	struct batadv_priv *bat_priv;

	bat_v = container_of(work, struct batadv_priv_bat_v, ogm_wq.work);
	bat_priv = container_of(bat_v, struct batadv_priv, bat_v);

	mutex_lock(&bat_priv->bat_v.ogm_buff_mutex);
	batadv_v_ogm_send_softif(bat_priv);
	mutex_unlock(&bat_priv->bat_v.ogm_buff_mutex);
}

/**
 * batadv_v_ogm_iface_enable() - prepare an interface for B.A.T.M.A.N. V
 * @hard_iface: the interface to prepare
 *
 * Takes care of scheduling own OGM sending routine for this interface.
 *
 * Return: 0 on success or a negative error code otherwise
 */
int batadv_v_ogm_iface_enable(struct batadv_hard_iface *hard_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);

	batadv_v_ogm_start_timer(bat_priv);

	return 0;
}

/**
 * batadv_v_ogm_primary_iface_set() - set a new primary interface
 * @primary_iface: the new primary interface
 */
void batadv_v_ogm_primary_iface_set(struct batadv_hard_iface *primary_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(primary_iface->soft_iface);
	struct batadv_ogm2_packet *ogm_packet;

	mutex_lock(&bat_priv->bat_v.ogm_buff_mutex);
	if (!bat_priv->bat_v.ogm_buff)
		goto unlock;

	ogm_packet = (struct batadv_ogm2_packet *)bat_priv->bat_v.ogm_buff;
	ether_addr_copy(ogm_packet->orig, primary_iface->net_dev->dev_addr);

unlock:
	mutex_unlock(&bat_priv->bat_v.ogm_buff_mutex);
}

/**
 * batadv_v_forward_penalty() - apply a penalty to the throughput metric
 *  forwarded with B.A.T.M.A.N. V OGMs
 * @bat_priv: the bat priv with all the soft interface information
 * @if_incoming: the interface where the OGM has been received
 * @if_outgoing: the interface where the OGM has to be forwarded to
 * @throughput: the current throughput
 *
 * Apply a penalty on the current throughput metric value based on the
 * characteristic of the interface where the OGM has been received. The return
 * value is computed as follows:
 * - throughput * 50%          if the incoming and outgoing interface are the
 *                             same WiFi interface and the throughput is above
 *                             1MBit/s
 * - throughput                if the outgoing interface is the default
 *                             interface (i.e. this OGM is processed for the
 *                             internal table and not forwarded)
 * - throughput * hop penalty  otherwise
 *
 * Return: the penalised throughput metric.
 */
static u32 batadv_v_forward_penalty(struct batadv_priv *bat_priv,
				    struct batadv_hard_iface *if_incoming,
				    struct batadv_hard_iface *if_outgoing,
				    u32 throughput)
{
	int hop_penalty = atomic_read(&bat_priv->hop_penalty);
	int hop_penalty_max = BATADV_TQ_MAX_VALUE;

	/* Don't apply hop penalty in default originator table. */
	if (if_outgoing == BATADV_IF_DEFAULT)
		return throughput;

	/* Forwarding on the same WiFi interface cuts the throughput in half
	 * due to the store & forward characteristics of WIFI.
	 * Very low throughput values are the exception.
	 */
	if (throughput > 10 &&
	    if_incoming == if_outgoing &&
	    !(if_incoming->bat_v.flags & BATADV_FULL_DUPLEX))
		return throughput / 2;

	/* hop penalty of 255 equals 100% */
	return throughput * (hop_penalty_max - hop_penalty) / hop_penalty_max;
}

/**
 * batadv_v_ogm_forward() - check conditions and forward an OGM to the given
 *  outgoing interface
 * @bat_priv: the bat priv with all the soft interface information
 * @ogm_received: previously received OGM to be forwarded
 * @orig_node: the originator which has been updated
 * @neigh_node: the neigh_node through with the OGM has been received
 * @if_incoming: the interface on which this OGM was received on
 * @if_outgoing: the interface to which the OGM has to be forwarded to
 *
 * Forward an OGM to an interface after having altered the throughput metric and
 * the TTL value contained in it. The original OGM isn't modified.
 */
static void batadv_v_ogm_forward(struct batadv_priv *bat_priv,
				 const struct batadv_ogm2_packet *ogm_received,
				 struct batadv_orig_node *orig_node,
				 struct batadv_neigh_node *neigh_node,
				 struct batadv_hard_iface *if_incoming,
				 struct batadv_hard_iface *if_outgoing)
{
	struct batadv_neigh_ifinfo *neigh_ifinfo = NULL;
	struct batadv_orig_ifinfo *orig_ifinfo = NULL;
	struct batadv_neigh_node *router = NULL;
	struct batadv_ogm2_packet *ogm_forward;
	unsigned char *skb_buff;
	struct sk_buff *skb;
	size_t packet_len;
	u16 tvlv_len;

	/* only forward for specific interfaces, not for the default one. */
	if (if_outgoing == BATADV_IF_DEFAULT)
		goto out;

	orig_ifinfo = batadv_orig_ifinfo_new(orig_node, if_outgoing);
	if (!orig_ifinfo)
		goto out;

	/* acquire possibly updated router */
	router = batadv_orig_router_get(orig_node, if_outgoing);

	/* strict rule: forward packets coming from the best next hop only */
	if (neigh_node != router)
		goto out;

	/* don't forward the same seqno twice on one interface */
	if (orig_ifinfo->last_seqno_forwarded == ntohl(ogm_received->seqno))
		goto out;

	orig_ifinfo->last_seqno_forwarded = ntohl(ogm_received->seqno);

	if (ogm_received->ttl <= 1) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv, "ttl exceeded\n");
		goto out;
	}

	neigh_ifinfo = batadv_neigh_ifinfo_get(neigh_node, if_outgoing);
	if (!neigh_ifinfo)
		goto out;

	tvlv_len = ntohs(ogm_received->tvlv_len);

	packet_len = BATADV_OGM2_HLEN + tvlv_len;
	skb = netdev_alloc_skb_ip_align(if_outgoing->net_dev,
					ETH_HLEN + packet_len);
	if (!skb)
		goto out;

	skb_reserve(skb, ETH_HLEN);
	skb_buff = skb_put_data(skb, ogm_received, packet_len);

	/* apply forward penalty */
	ogm_forward = (struct batadv_ogm2_packet *)skb_buff;
	ogm_forward->throughput = htonl(neigh_ifinfo->bat_v.throughput);
	ogm_forward->ttl--;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Forwarding OGM2 packet on %s: throughput %u, ttl %u, received via %s\n",
		   if_outgoing->net_dev->name, ntohl(ogm_forward->throughput),
		   ogm_forward->ttl, if_incoming->net_dev->name);

	batadv_v_ogm_send_to_if(skb, if_outgoing);

out:
	if (orig_ifinfo)
		batadv_orig_ifinfo_put(orig_ifinfo);
	if (router)
		batadv_neigh_node_put(router);
	if (neigh_ifinfo)
		batadv_neigh_ifinfo_put(neigh_ifinfo);
}

/**
 * batadv_v_ogm_metric_update() - update route metric based on OGM
 * @bat_priv: the bat priv with all the soft interface information
 * @ogm2: OGM2 structure
 * @orig_node: Originator structure for which the OGM has been received
 * @neigh_node: the neigh_node through with the OGM has been received
 * @if_incoming: the interface where this packet was received
 * @if_outgoing: the interface for which the packet should be considered
 *
 * Return:
 *  1  if the OGM is new,
 *  0  if it is not new but valid,
 *  <0 on error (e.g. old OGM)
 */
static int batadv_v_ogm_metric_update(struct batadv_priv *bat_priv,
				      const struct batadv_ogm2_packet *ogm2,
				      struct batadv_orig_node *orig_node,
				      struct batadv_neigh_node *neigh_node,
				      struct batadv_hard_iface *if_incoming,
				      struct batadv_hard_iface *if_outgoing)
{
	struct batadv_orig_ifinfo *orig_ifinfo;
	struct batadv_neigh_ifinfo *neigh_ifinfo = NULL;
	bool protection_started = false;
	int ret = -EINVAL;
	u32 path_throughput;
	s32 seq_diff;

	orig_ifinfo = batadv_orig_ifinfo_new(orig_node, if_outgoing);
	if (!orig_ifinfo)
		goto out;

	seq_diff = ntohl(ogm2->seqno) - orig_ifinfo->last_real_seqno;

	if (!hlist_empty(&orig_node->neigh_list) &&
	    batadv_window_protected(bat_priv, seq_diff,
				    BATADV_OGM_MAX_AGE,
				    &orig_ifinfo->batman_seqno_reset,
				    &protection_started)) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: packet within window protection time from %pM\n",
			   ogm2->orig);
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Last reset: %ld, %ld\n",
			   orig_ifinfo->batman_seqno_reset, jiffies);
		goto out;
	}

	/* drop packets with old seqnos, however accept the first packet after
	 * a host has been rebooted.
	 */
	if (seq_diff < 0 && !protection_started)
		goto out;

	neigh_node->last_seen = jiffies;

	orig_node->last_seen = jiffies;

	orig_ifinfo->last_real_seqno = ntohl(ogm2->seqno);
	orig_ifinfo->last_ttl = ogm2->ttl;

	neigh_ifinfo = batadv_neigh_ifinfo_new(neigh_node, if_outgoing);
	if (!neigh_ifinfo)
		goto out;

	path_throughput = batadv_v_forward_penalty(bat_priv, if_incoming,
						   if_outgoing,
						   ntohl(ogm2->throughput));
	neigh_ifinfo->bat_v.throughput = path_throughput;
	neigh_ifinfo->bat_v.last_seqno = ntohl(ogm2->seqno);
	neigh_ifinfo->last_ttl = ogm2->ttl;

	if (seq_diff > 0 || protection_started)
		ret = 1;
	else
		ret = 0;
out:
	if (orig_ifinfo)
		batadv_orig_ifinfo_put(orig_ifinfo);
	if (neigh_ifinfo)
		batadv_neigh_ifinfo_put(neigh_ifinfo);

	return ret;
}

/**
 * batadv_v_ogm_route_update() - update routes based on OGM
 * @bat_priv: the bat priv with all the soft interface information
 * @ethhdr: the Ethernet header of the OGM2
 * @ogm2: OGM2 structure
 * @orig_node: Originator structure for which the OGM has been received
 * @neigh_node: the neigh_node through with the OGM has been received
 * @if_incoming: the interface where this packet was received
 * @if_outgoing: the interface for which the packet should be considered
 *
 * Return: true if the packet should be forwarded, false otherwise
 */
static bool batadv_v_ogm_route_update(struct batadv_priv *bat_priv,
				      const struct ethhdr *ethhdr,
				      const struct batadv_ogm2_packet *ogm2,
				      struct batadv_orig_node *orig_node,
				      struct batadv_neigh_node *neigh_node,
				      struct batadv_hard_iface *if_incoming,
				      struct batadv_hard_iface *if_outgoing)
{
	struct batadv_neigh_node *router = NULL;
	struct batadv_orig_node *orig_neigh_node;
	struct batadv_neigh_node *orig_neigh_router = NULL;
	struct batadv_neigh_ifinfo *router_ifinfo = NULL, *neigh_ifinfo = NULL;
	u32 router_throughput, neigh_throughput;
	u32 router_last_seqno;
	u32 neigh_last_seqno;
	s32 neigh_seq_diff;
	bool forward = false;

	orig_neigh_node = batadv_v_ogm_orig_get(bat_priv, ethhdr->h_source);
	if (!orig_neigh_node)
		goto out;

	orig_neigh_router = batadv_orig_router_get(orig_neigh_node,
						   if_outgoing);

	/* drop packet if sender is not a direct neighbor and if we
	 * don't route towards it
	 */
	router = batadv_orig_router_get(orig_node, if_outgoing);
	if (router && router->orig_node != orig_node && !orig_neigh_router) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: OGM via unknown neighbor!\n");
		goto out;
	}

	/* Mark the OGM to be considered for forwarding, and update routes
	 * if needed.
	 */
	forward = true;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Searching and updating originator entry of received packet\n");

	/* if this neighbor already is our next hop there is nothing
	 * to change
	 */
	if (router == neigh_node)
		goto out;

	/* don't consider neighbours with worse throughput.
	 * also switch route if this seqno is BATADV_V_MAX_ORIGDIFF newer than
	 * the last received seqno from our best next hop.
	 */
	if (router) {
		router_ifinfo = batadv_neigh_ifinfo_get(router, if_outgoing);
		neigh_ifinfo = batadv_neigh_ifinfo_get(neigh_node, if_outgoing);

		/* if these are not allocated, something is wrong. */
		if (!router_ifinfo || !neigh_ifinfo)
			goto out;

		neigh_last_seqno = neigh_ifinfo->bat_v.last_seqno;
		router_last_seqno = router_ifinfo->bat_v.last_seqno;
		neigh_seq_diff = neigh_last_seqno - router_last_seqno;
		router_throughput = router_ifinfo->bat_v.throughput;
		neigh_throughput = neigh_ifinfo->bat_v.throughput;

		if (neigh_seq_diff < BATADV_OGM_MAX_ORIGDIFF &&
		    router_throughput >= neigh_throughput)
			goto out;
	}

	batadv_update_route(bat_priv, orig_node, if_outgoing, neigh_node);
out:
	if (router)
		batadv_neigh_node_put(router);
	if (orig_neigh_router)
		batadv_neigh_node_put(orig_neigh_router);
	if (orig_neigh_node)
		batadv_orig_node_put(orig_neigh_node);
	if (router_ifinfo)
		batadv_neigh_ifinfo_put(router_ifinfo);
	if (neigh_ifinfo)
		batadv_neigh_ifinfo_put(neigh_ifinfo);

	return forward;
}

/**
 * batadv_v_ogm_process_per_outif() - process a batman v OGM for an outgoing if
 * @bat_priv: the bat priv with all the soft interface information
 * @ethhdr: the Ethernet header of the OGM2
 * @ogm2: OGM2 structure
 * @orig_node: Originator structure for which the OGM has been received
 * @neigh_node: the neigh_node through with the OGM has been received
 * @if_incoming: the interface where this packet was received
 * @if_outgoing: the interface for which the packet should be considered
 */
static void
batadv_v_ogm_process_per_outif(struct batadv_priv *bat_priv,
			       const struct ethhdr *ethhdr,
			       const struct batadv_ogm2_packet *ogm2,
			       struct batadv_orig_node *orig_node,
			       struct batadv_neigh_node *neigh_node,
			       struct batadv_hard_iface *if_incoming,
			       struct batadv_hard_iface *if_outgoing)
{
	int seqno_age;
	bool forward;

	/* first, update the metric with according sanity checks */
	seqno_age = batadv_v_ogm_metric_update(bat_priv, ogm2, orig_node,
					       neigh_node, if_incoming,
					       if_outgoing);

	/* outdated sequence numbers are to be discarded */
	if (seqno_age < 0)
		return;

	/* only unknown & newer OGMs contain TVLVs we are interested in */
	if (seqno_age > 0 && if_outgoing == BATADV_IF_DEFAULT)
		batadv_tvlv_containers_process(bat_priv, true, orig_node,
					       NULL, NULL,
					       (unsigned char *)(ogm2 + 1),
					       ntohs(ogm2->tvlv_len));

	/* if the metric update went through, update routes if needed */
	forward = batadv_v_ogm_route_update(bat_priv, ethhdr, ogm2, orig_node,
					    neigh_node, if_incoming,
					    if_outgoing);

	/* if the routes have been processed correctly, check and forward */
	if (forward)
		batadv_v_ogm_forward(bat_priv, ogm2, orig_node, neigh_node,
				     if_incoming, if_outgoing);
}

/**
 * batadv_v_ogm_aggr_packet() - checks if there is another OGM aggregated
 * @buff_pos: current position in the skb
 * @packet_len: total length of the skb
 * @ogm2_packet: potential OGM2 in buffer
 *
 * Return: true if there is enough space for another OGM, false otherwise.
 */
static bool
batadv_v_ogm_aggr_packet(int buff_pos, int packet_len,
			 const struct batadv_ogm2_packet *ogm2_packet)
{
	int next_buff_pos = 0;

	/* check if there is enough space for the header */
	next_buff_pos += buff_pos + sizeof(*ogm2_packet);
	if (next_buff_pos > packet_len)
		return false;

	/* check if there is enough space for the optional TVLV */
	next_buff_pos += ntohs(ogm2_packet->tvlv_len);

	return (next_buff_pos <= packet_len) &&
	       (next_buff_pos <= BATADV_MAX_AGGREGATION_BYTES);
}

/**
 * batadv_v_ogm_process() - process an incoming batman v OGM
 * @skb: the skb containing the OGM
 * @ogm_offset: offset to the OGM which should be processed (for aggregates)
 * @if_incoming: the interface where this packet was receved
 */
static void batadv_v_ogm_process(const struct sk_buff *skb, int ogm_offset,
				 struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct ethhdr *ethhdr;
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_hardif_neigh_node *hardif_neigh = NULL;
	struct batadv_neigh_node *neigh_node = NULL;
	struct batadv_hard_iface *hard_iface;
	struct batadv_ogm2_packet *ogm_packet;
	u32 ogm_throughput, link_throughput, path_throughput;
	int ret;

	ethhdr = eth_hdr(skb);
	ogm_packet = (struct batadv_ogm2_packet *)(skb->data + ogm_offset);

	ogm_throughput = ntohl(ogm_packet->throughput);

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Received OGM2 packet via NB: %pM, IF: %s [%pM] (from OG: %pM, seqno %u, throughput %u, TTL %u, V %u, tvlv_len %u)\n",
		   ethhdr->h_source, if_incoming->net_dev->name,
		   if_incoming->net_dev->dev_addr, ogm_packet->orig,
		   ntohl(ogm_packet->seqno), ogm_throughput, ogm_packet->ttl,
		   ogm_packet->version, ntohs(ogm_packet->tvlv_len));

	/* If the throughput metric is 0, immediately drop the packet. No need
	 * to create orig_node / neigh_node for an unusable route.
	 */
	if (ogm_throughput == 0) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: originator packet with throughput metric of 0\n");
		return;
	}

	/* require ELP packets be to received from this neighbor first */
	hardif_neigh = batadv_hardif_neigh_get(if_incoming, ethhdr->h_source);
	if (!hardif_neigh) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: OGM via unknown neighbor!\n");
		goto out;
	}

	orig_node = batadv_v_ogm_orig_get(bat_priv, ogm_packet->orig);
	if (!orig_node)
		return;

	neigh_node = batadv_neigh_node_get_or_create(orig_node, if_incoming,
						     ethhdr->h_source);
	if (!neigh_node)
		goto out;

	/* Update the received throughput metric to match the link
	 * characteristic:
	 *  - If this OGM traveled one hop so far (emitted by single hop
	 *    neighbor) the path throughput metric equals the link throughput.
	 *  - For OGMs traversing more than hop the path throughput metric is
	 *    the smaller of the path throughput and the link throughput.
	 */
	link_throughput = ewma_throughput_read(&hardif_neigh->bat_v.throughput);
	path_throughput = min_t(u32, link_throughput, ogm_throughput);
	ogm_packet->throughput = htonl(path_throughput);

	batadv_v_ogm_process_per_outif(bat_priv, ethhdr, ogm_packet, orig_node,
				       neigh_node, if_incoming,
				       BATADV_IF_DEFAULT);

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->if_status != BATADV_IF_ACTIVE)
			continue;

		if (hard_iface->soft_iface != bat_priv->soft_iface)
			continue;

		if (!kref_get_unless_zero(&hard_iface->refcount))
			continue;

		ret = batadv_hardif_no_broadcast(hard_iface,
						 ogm_packet->orig,
						 hardif_neigh->orig);

		if (ret) {
			char *type;

			switch (ret) {
			case BATADV_HARDIF_BCAST_NORECIPIENT:
				type = "no neighbor";
				break;
			case BATADV_HARDIF_BCAST_DUPFWD:
				type = "single neighbor is source";
				break;
			case BATADV_HARDIF_BCAST_DUPORIG:
				type = "single neighbor is originator";
				break;
			default:
				type = "unknown";
			}

			batadv_dbg(BATADV_DBG_BATMAN, bat_priv, "OGM2 packet from %pM on %s suppressed: %s\n",
				   ogm_packet->orig, hard_iface->net_dev->name,
				   type);

			batadv_hardif_put(hard_iface);
			continue;
		}

		batadv_v_ogm_process_per_outif(bat_priv, ethhdr, ogm_packet,
					       orig_node, neigh_node,
					       if_incoming, hard_iface);

		batadv_hardif_put(hard_iface);
	}
	rcu_read_unlock();
out:
	if (orig_node)
		batadv_orig_node_put(orig_node);
	if (neigh_node)
		batadv_neigh_node_put(neigh_node);
	if (hardif_neigh)
		batadv_hardif_neigh_put(hardif_neigh);
}

/**
 * batadv_v_ogm_packet_recv() - OGM2 receiving handler
 * @skb: the received OGM
 * @if_incoming: the interface where this OGM has been received
 *
 * Return: NET_RX_SUCCESS and consume the skb on success or returns NET_RX_DROP
 * (without freeing the skb) on failure
 */
int batadv_v_ogm_packet_recv(struct sk_buff *skb,
			     struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_ogm2_packet *ogm_packet;
	struct ethhdr *ethhdr = eth_hdr(skb);
	int ogm_offset;
	u8 *packet_pos;
	int ret = NET_RX_DROP;

	/* did we receive a OGM2 packet on an interface that does not have
	 * B.A.T.M.A.N. V enabled ?
	 */
	if (strcmp(bat_priv->algo_ops->name, "BATMAN_V") != 0)
		goto free_skb;

	if (!batadv_check_management_packet(skb, if_incoming, BATADV_OGM2_HLEN))
		goto free_skb;

	if (batadv_is_my_mac(bat_priv, ethhdr->h_source))
		goto free_skb;

	ogm_packet = (struct batadv_ogm2_packet *)skb->data;

	if (batadv_is_my_mac(bat_priv, ogm_packet->orig))
		goto free_skb;

	batadv_inc_counter(bat_priv, BATADV_CNT_MGMT_RX);
	batadv_add_counter(bat_priv, BATADV_CNT_MGMT_RX_BYTES,
			   skb->len + ETH_HLEN);

	ogm_offset = 0;
	ogm_packet = (struct batadv_ogm2_packet *)skb->data;

	while (batadv_v_ogm_aggr_packet(ogm_offset, skb_headlen(skb),
					ogm_packet)) {
		batadv_v_ogm_process(skb, ogm_offset, if_incoming);

		ogm_offset += BATADV_OGM2_HLEN;
		ogm_offset += ntohs(ogm_packet->tvlv_len);

		packet_pos = skb->data + ogm_offset;
		ogm_packet = (struct batadv_ogm2_packet *)packet_pos;
	}

	ret = NET_RX_SUCCESS;

free_skb:
	if (ret == NET_RX_SUCCESS)
		consume_skb(skb);
	else
		kfree_skb(skb);

	return ret;
}

/**
 * batadv_v_ogm_init() - initialise the OGM2 engine
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: 0 on success or a negative error code in case of failure
 */
int batadv_v_ogm_init(struct batadv_priv *bat_priv)
{
	struct batadv_ogm2_packet *ogm_packet;
	unsigned char *ogm_buff;
	u32 random_seqno;

	bat_priv->bat_v.ogm_buff_len = BATADV_OGM2_HLEN;
	ogm_buff = kzalloc(bat_priv->bat_v.ogm_buff_len, GFP_ATOMIC);
	if (!ogm_buff)
		return -ENOMEM;

	bat_priv->bat_v.ogm_buff = ogm_buff;
	ogm_packet = (struct batadv_ogm2_packet *)ogm_buff;
	ogm_packet->packet_type = BATADV_OGM2;
	ogm_packet->version = BATADV_COMPAT_VERSION;
	ogm_packet->ttl = BATADV_TTL;
	ogm_packet->flags = BATADV_NO_FLAGS;
	ogm_packet->throughput = htonl(BATADV_THROUGHPUT_MAX_VALUE);

	/* randomize initial seqno to avoid collision */
	get_random_bytes(&random_seqno, sizeof(random_seqno));
	atomic_set(&bat_priv->bat_v.ogm_seqno, random_seqno);
	INIT_DELAYED_WORK(&bat_priv->bat_v.ogm_wq, batadv_v_ogm_send);

	mutex_init(&bat_priv->bat_v.ogm_buff_mutex);

	return 0;
}

/**
 * batadv_v_ogm_free() - free OGM private resources
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_v_ogm_free(struct batadv_priv *bat_priv)
{
	cancel_delayed_work_sync(&bat_priv->bat_v.ogm_wq);

	mutex_lock(&bat_priv->bat_v.ogm_buff_mutex);

	kfree(bat_priv->bat_v.ogm_buff);
	bat_priv->bat_v.ogm_buff = NULL;
	bat_priv->bat_v.ogm_buff_len = 0;

	mutex_unlock(&bat_priv->bat_v.ogm_buff_mutex);
}
