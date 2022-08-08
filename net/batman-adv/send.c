// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 */

#include "send.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/byteorder/generic.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/gfp.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/workqueue.h>

#include "distributed-arp-table.h"
#include "fragmentation.h"
#include "gateway_client.h"
#include "hard-interface.h"
#include "log.h"
#include "network-coding.h"
#include "originator.h"
#include "routing.h"
#include "soft-interface.h"
#include "translation-table.h"

static void batadv_send_outstanding_bcast_packet(struct work_struct *work);

/**
 * batadv_send_skb_packet() - send an already prepared packet
 * @skb: the packet to send
 * @hard_iface: the interface to use to send the broadcast packet
 * @dst_addr: the payload destination
 *
 * Send out an already prepared packet to the given neighbor or broadcast it
 * using the specified interface. Either hard_iface or neigh_node must be not
 * NULL.
 * If neigh_node is NULL, then the packet is broadcasted using hard_iface,
 * otherwise it is sent as unicast to the given neighbor.
 *
 * Regardless of the return value, the skb is consumed.
 *
 * Return: A negative errno code is returned on a failure. A success does not
 * guarantee the frame will be transmitted as it may be dropped due
 * to congestion or traffic shaping.
 */
int batadv_send_skb_packet(struct sk_buff *skb,
			   struct batadv_hard_iface *hard_iface,
			   const u8 *dst_addr)
{
	struct batadv_priv *bat_priv;
	struct ethhdr *ethhdr;
	int ret;

	bat_priv = netdev_priv(hard_iface->soft_iface);

	if (hard_iface->if_status != BATADV_IF_ACTIVE)
		goto send_skb_err;

	if (unlikely(!hard_iface->net_dev))
		goto send_skb_err;

	if (!(hard_iface->net_dev->flags & IFF_UP)) {
		pr_warn("Interface %s is not up - can't send packet via that interface!\n",
			hard_iface->net_dev->name);
		goto send_skb_err;
	}

	/* push to the ethernet header. */
	if (batadv_skb_head_push(skb, ETH_HLEN) < 0)
		goto send_skb_err;

	skb_reset_mac_header(skb);

	ethhdr = eth_hdr(skb);
	ether_addr_copy(ethhdr->h_source, hard_iface->net_dev->dev_addr);
	ether_addr_copy(ethhdr->h_dest, dst_addr);
	ethhdr->h_proto = htons(ETH_P_BATMAN);

	skb_set_network_header(skb, ETH_HLEN);
	skb->protocol = htons(ETH_P_BATMAN);

	skb->dev = hard_iface->net_dev;

	/* Save a clone of the skb to use when decoding coded packets */
	batadv_nc_skb_store_for_decoding(bat_priv, skb);

	/* dev_queue_xmit() returns a negative result on error.	 However on
	 * congestion and traffic shaping, it drops and returns NET_XMIT_DROP
	 * (which is > 0). This will not be treated as an error.
	 */
	ret = dev_queue_xmit(skb);
	return net_xmit_eval(ret);
send_skb_err:
	kfree_skb(skb);
	return NET_XMIT_DROP;
}

/**
 * batadv_send_broadcast_skb() - Send broadcast packet via hard interface
 * @skb: packet to be transmitted (with batadv header and no outer eth header)
 * @hard_iface: outgoing interface
 *
 * Return: A negative errno code is returned on a failure. A success does not
 * guarantee the frame will be transmitted as it may be dropped due
 * to congestion or traffic shaping.
 */
int batadv_send_broadcast_skb(struct sk_buff *skb,
			      struct batadv_hard_iface *hard_iface)
{
	return batadv_send_skb_packet(skb, hard_iface, batadv_broadcast_addr);
}

/**
 * batadv_send_unicast_skb() - Send unicast packet to neighbor
 * @skb: packet to be transmitted (with batadv header and no outer eth header)
 * @neigh: neighbor which is used as next hop to destination
 *
 * Return: A negative errno code is returned on a failure. A success does not
 * guarantee the frame will be transmitted as it may be dropped due
 * to congestion or traffic shaping.
 */
int batadv_send_unicast_skb(struct sk_buff *skb,
			    struct batadv_neigh_node *neigh)
{
#ifdef CONFIG_BATMAN_ADV_BATMAN_V
	struct batadv_hardif_neigh_node *hardif_neigh;
#endif
	int ret;

	ret = batadv_send_skb_packet(skb, neigh->if_incoming, neigh->addr);

#ifdef CONFIG_BATMAN_ADV_BATMAN_V
	hardif_neigh = batadv_hardif_neigh_get(neigh->if_incoming, neigh->addr);

	if (hardif_neigh && ret != NET_XMIT_DROP)
		hardif_neigh->bat_v.last_unicast_tx = jiffies;

	if (hardif_neigh)
		batadv_hardif_neigh_put(hardif_neigh);
#endif

	return ret;
}

/**
 * batadv_send_skb_to_orig() - Lookup next-hop and transmit skb.
 * @skb: Packet to be transmitted.
 * @orig_node: Final destination of the packet.
 * @recv_if: Interface used when receiving the packet (can be NULL).
 *
 * Looks up the best next-hop towards the passed originator and passes the
 * skb on for preparation of MAC header. If the packet originated from this
 * host, NULL can be passed as recv_if and no interface alternating is
 * attempted.
 *
 * Return: negative errno code on a failure, -EINPROGRESS if the skb is
 * buffered for later transmit or the NET_XMIT status returned by the
 * lower routine if the packet has been passed down.
 */
int batadv_send_skb_to_orig(struct sk_buff *skb,
			    struct batadv_orig_node *orig_node,
			    struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = orig_node->bat_priv;
	struct batadv_neigh_node *neigh_node;
	int ret;

	/* batadv_find_router() increases neigh_nodes refcount if found. */
	neigh_node = batadv_find_router(bat_priv, orig_node, recv_if);
	if (!neigh_node) {
		ret = -EINVAL;
		goto free_skb;
	}

	/* Check if the skb is too large to send in one piece and fragment
	 * it if needed.
	 */
	if (atomic_read(&bat_priv->fragmentation) &&
	    skb->len > neigh_node->if_incoming->net_dev->mtu) {
		/* Fragment and send packet. */
		ret = batadv_frag_send_packet(skb, orig_node, neigh_node);
		/* skb was consumed */
		skb = NULL;

		goto put_neigh_node;
	}

	/* try to network code the packet, if it is received on an interface
	 * (i.e. being forwarded). If the packet originates from this node or if
	 * network coding fails, then send the packet as usual.
	 */
	if (recv_if && batadv_nc_skb_forward(skb, neigh_node))
		ret = -EINPROGRESS;
	else
		ret = batadv_send_unicast_skb(skb, neigh_node);

	/* skb was consumed */
	skb = NULL;

put_neigh_node:
	batadv_neigh_node_put(neigh_node);
free_skb:
	kfree_skb(skb);

	return ret;
}

/**
 * batadv_send_skb_push_fill_unicast() - extend the buffer and initialize the
 *  common fields for unicast packets
 * @skb: the skb carrying the unicast header to initialize
 * @hdr_size: amount of bytes to push at the beginning of the skb
 * @orig_node: the destination node
 *
 * Return: false if the buffer extension was not possible or true otherwise.
 */
static bool
batadv_send_skb_push_fill_unicast(struct sk_buff *skb, int hdr_size,
				  struct batadv_orig_node *orig_node)
{
	struct batadv_unicast_packet *unicast_packet;
	u8 ttvn = (u8)atomic_read(&orig_node->last_ttvn);

	if (batadv_skb_head_push(skb, hdr_size) < 0)
		return false;

	unicast_packet = (struct batadv_unicast_packet *)skb->data;
	unicast_packet->version = BATADV_COMPAT_VERSION;
	/* batman packet type: unicast */
	unicast_packet->packet_type = BATADV_UNICAST;
	/* set unicast ttl */
	unicast_packet->ttl = BATADV_TTL;
	/* copy the destination for faster routing */
	ether_addr_copy(unicast_packet->dest, orig_node->orig);
	/* set the destination tt version number */
	unicast_packet->ttvn = ttvn;

	return true;
}

/**
 * batadv_send_skb_prepare_unicast() - encapsulate an skb with a unicast header
 * @skb: the skb containing the payload to encapsulate
 * @orig_node: the destination node
 *
 * Return: false if the payload could not be encapsulated or true otherwise.
 */
static bool batadv_send_skb_prepare_unicast(struct sk_buff *skb,
					    struct batadv_orig_node *orig_node)
{
	size_t uni_size = sizeof(struct batadv_unicast_packet);

	return batadv_send_skb_push_fill_unicast(skb, uni_size, orig_node);
}

/**
 * batadv_send_skb_prepare_unicast_4addr() - encapsulate an skb with a
 *  unicast 4addr header
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the skb containing the payload to encapsulate
 * @orig: the destination node
 * @packet_subtype: the unicast 4addr packet subtype to use
 *
 * Return: false if the payload could not be encapsulated or true otherwise.
 */
bool batadv_send_skb_prepare_unicast_4addr(struct batadv_priv *bat_priv,
					   struct sk_buff *skb,
					   struct batadv_orig_node *orig,
					   int packet_subtype)
{
	struct batadv_hard_iface *primary_if;
	struct batadv_unicast_4addr_packet *uc_4addr_packet;
	bool ret = false;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	/* Pull the header space and fill the unicast_packet substructure.
	 * We can do that because the first member of the uc_4addr_packet
	 * is of type struct unicast_packet
	 */
	if (!batadv_send_skb_push_fill_unicast(skb, sizeof(*uc_4addr_packet),
					       orig))
		goto out;

	uc_4addr_packet = (struct batadv_unicast_4addr_packet *)skb->data;
	uc_4addr_packet->u.packet_type = BATADV_UNICAST_4ADDR;
	ether_addr_copy(uc_4addr_packet->src, primary_if->net_dev->dev_addr);
	uc_4addr_packet->subtype = packet_subtype;
	uc_4addr_packet->reserved = 0;

	ret = true;
out:
	if (primary_if)
		batadv_hardif_put(primary_if);
	return ret;
}

/**
 * batadv_send_skb_unicast() - encapsulate and send an skb via unicast
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: payload to send
 * @packet_type: the batman unicast packet type to use
 * @packet_subtype: the unicast 4addr packet subtype (only relevant for unicast
 *  4addr packets)
 * @orig_node: the originator to send the packet to
 * @vid: the vid to be used to search the translation table
 *
 * Wrap the given skb into a batman-adv unicast or unicast-4addr header
 * depending on whether BATADV_UNICAST or BATADV_UNICAST_4ADDR was supplied
 * as packet_type. Then send this frame to the given orig_node.
 *
 * Return: NET_XMIT_DROP in case of error or NET_XMIT_SUCCESS otherwise.
 */
int batadv_send_skb_unicast(struct batadv_priv *bat_priv,
			    struct sk_buff *skb, int packet_type,
			    int packet_subtype,
			    struct batadv_orig_node *orig_node,
			    unsigned short vid)
{
	struct batadv_unicast_packet *unicast_packet;
	struct ethhdr *ethhdr;
	int ret = NET_XMIT_DROP;

	if (!orig_node)
		goto out;

	switch (packet_type) {
	case BATADV_UNICAST:
		if (!batadv_send_skb_prepare_unicast(skb, orig_node))
			goto out;
		break;
	case BATADV_UNICAST_4ADDR:
		if (!batadv_send_skb_prepare_unicast_4addr(bat_priv, skb,
							   orig_node,
							   packet_subtype))
			goto out;
		break;
	default:
		/* this function supports UNICAST and UNICAST_4ADDR only. It
		 * should never be invoked with any other packet type
		 */
		goto out;
	}

	/* skb->data might have been reallocated by
	 * batadv_send_skb_prepare_unicast{,_4addr}()
	 */
	ethhdr = eth_hdr(skb);
	unicast_packet = (struct batadv_unicast_packet *)skb->data;

	/* inform the destination node that we are still missing a correct route
	 * for this client. The destination will receive this packet and will
	 * try to reroute it because the ttvn contained in the header is less
	 * than the current one
	 */
	if (batadv_tt_global_client_is_roaming(bat_priv, ethhdr->h_dest, vid))
		unicast_packet->ttvn = unicast_packet->ttvn - 1;

	ret = batadv_send_skb_to_orig(skb, orig_node, NULL);
	 /* skb was consumed */
	skb = NULL;

out:
	kfree_skb(skb);
	return ret;
}

/**
 * batadv_send_skb_via_tt_generic() - send an skb via TT lookup
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: payload to send
 * @packet_type: the batman unicast packet type to use
 * @packet_subtype: the unicast 4addr packet subtype (only relevant for unicast
 *  4addr packets)
 * @dst_hint: can be used to override the destination contained in the skb
 * @vid: the vid to be used to search the translation table
 *
 * Look up the recipient node for the destination address in the ethernet
 * header via the translation table. Wrap the given skb into a batman-adv
 * unicast or unicast-4addr header depending on whether BATADV_UNICAST or
 * BATADV_UNICAST_4ADDR was supplied as packet_type. Then send this frame
 * to the according destination node.
 *
 * Return: NET_XMIT_DROP in case of error or NET_XMIT_SUCCESS otherwise.
 */
int batadv_send_skb_via_tt_generic(struct batadv_priv *bat_priv,
				   struct sk_buff *skb, int packet_type,
				   int packet_subtype, u8 *dst_hint,
				   unsigned short vid)
{
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	struct batadv_orig_node *orig_node;
	u8 *src, *dst;
	int ret;

	src = ethhdr->h_source;
	dst = ethhdr->h_dest;

	/* if we got an hint! let's send the packet to this client (if any) */
	if (dst_hint) {
		src = NULL;
		dst = dst_hint;
	}
	orig_node = batadv_transtable_search(bat_priv, src, dst, vid);

	ret = batadv_send_skb_unicast(bat_priv, skb, packet_type,
				      packet_subtype, orig_node, vid);

	if (orig_node)
		batadv_orig_node_put(orig_node);

	return ret;
}

/**
 * batadv_send_skb_via_gw() - send an skb via gateway lookup
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: payload to send
 * @vid: the vid to be used to search the translation table
 *
 * Look up the currently selected gateway. Wrap the given skb into a batman-adv
 * unicast header and send this frame to this gateway node.
 *
 * Return: NET_XMIT_DROP in case of error or NET_XMIT_SUCCESS otherwise.
 */
int batadv_send_skb_via_gw(struct batadv_priv *bat_priv, struct sk_buff *skb,
			   unsigned short vid)
{
	struct batadv_orig_node *orig_node;
	int ret;

	orig_node = batadv_gw_get_selected_orig(bat_priv);
	ret = batadv_send_skb_unicast(bat_priv, skb, BATADV_UNICAST_4ADDR,
				      BATADV_P_DATA, orig_node, vid);

	if (orig_node)
		batadv_orig_node_put(orig_node);

	return ret;
}

/**
 * batadv_forw_packet_free() - free a forwarding packet
 * @forw_packet: The packet to free
 * @dropped: whether the packet is freed because is dropped
 *
 * This frees a forwarding packet and releases any resources it might
 * have claimed.
 */
void batadv_forw_packet_free(struct batadv_forw_packet *forw_packet,
			     bool dropped)
{
	if (dropped)
		kfree_skb(forw_packet->skb);
	else
		consume_skb(forw_packet->skb);

	if (forw_packet->if_incoming)
		batadv_hardif_put(forw_packet->if_incoming);
	if (forw_packet->if_outgoing)
		batadv_hardif_put(forw_packet->if_outgoing);
	if (forw_packet->queue_left)
		atomic_inc(forw_packet->queue_left);
	kfree(forw_packet);
}

/**
 * batadv_forw_packet_alloc() - allocate a forwarding packet
 * @if_incoming: The (optional) if_incoming to be grabbed
 * @if_outgoing: The (optional) if_outgoing to be grabbed
 * @queue_left: The (optional) queue counter to decrease
 * @bat_priv: The bat_priv for the mesh of this forw_packet
 * @skb: The raw packet this forwarding packet shall contain
 *
 * Allocates a forwarding packet and tries to get a reference to the
 * (optional) if_incoming, if_outgoing and queue_left. If queue_left
 * is NULL then bat_priv is optional, too.
 *
 * Return: An allocated forwarding packet on success, NULL otherwise.
 */
struct batadv_forw_packet *
batadv_forw_packet_alloc(struct batadv_hard_iface *if_incoming,
			 struct batadv_hard_iface *if_outgoing,
			 atomic_t *queue_left,
			 struct batadv_priv *bat_priv,
			 struct sk_buff *skb)
{
	struct batadv_forw_packet *forw_packet;
	const char *qname;

	if (queue_left && !batadv_atomic_dec_not_zero(queue_left)) {
		qname = "unknown";

		if (queue_left == &bat_priv->bcast_queue_left)
			qname = "bcast";

		if (queue_left == &bat_priv->batman_queue_left)
			qname = "batman";

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "%s queue is full\n", qname);

		return NULL;
	}

	forw_packet = kmalloc(sizeof(*forw_packet), GFP_ATOMIC);
	if (!forw_packet)
		goto err;

	if (if_incoming)
		kref_get(&if_incoming->refcount);

	if (if_outgoing)
		kref_get(&if_outgoing->refcount);

	INIT_HLIST_NODE(&forw_packet->list);
	INIT_HLIST_NODE(&forw_packet->cleanup_list);
	forw_packet->skb = skb;
	forw_packet->queue_left = queue_left;
	forw_packet->if_incoming = if_incoming;
	forw_packet->if_outgoing = if_outgoing;
	forw_packet->num_packets = 0;

	return forw_packet;

err:
	if (queue_left)
		atomic_inc(queue_left);

	return NULL;
}

/**
 * batadv_forw_packet_was_stolen() - check whether someone stole this packet
 * @forw_packet: the forwarding packet to check
 *
 * This function checks whether the given forwarding packet was claimed by
 * someone else for free().
 *
 * Return: True if someone stole it, false otherwise.
 */
static bool
batadv_forw_packet_was_stolen(struct batadv_forw_packet *forw_packet)
{
	return !hlist_unhashed(&forw_packet->cleanup_list);
}

/**
 * batadv_forw_packet_steal() - claim a forw_packet for free()
 * @forw_packet: the forwarding packet to steal
 * @lock: a key to the store to steal from (e.g. forw_{bat,bcast}_list_lock)
 *
 * This function tries to steal a specific forw_packet from global
 * visibility for the purpose of getting it for free(). That means
 * the caller is *not* allowed to requeue it afterwards.
 *
 * Return: True if stealing was successful. False if someone else stole it
 * before us.
 */
bool batadv_forw_packet_steal(struct batadv_forw_packet *forw_packet,
			      spinlock_t *lock)
{
	/* did purging routine steal it earlier? */
	spin_lock_bh(lock);
	if (batadv_forw_packet_was_stolen(forw_packet)) {
		spin_unlock_bh(lock);
		return false;
	}

	hlist_del_init(&forw_packet->list);

	/* Just to spot misuse of this function */
	hlist_add_fake(&forw_packet->cleanup_list);

	spin_unlock_bh(lock);
	return true;
}

/**
 * batadv_forw_packet_list_steal() - claim a list of forward packets for free()
 * @forw_list: the to be stolen forward packets
 * @cleanup_list: a backup pointer, to be able to dispose the packet later
 * @hard_iface: the interface to steal forward packets from
 *
 * This function claims responsibility to free any forw_packet queued on the
 * given hard_iface. If hard_iface is NULL forwarding packets on all hard
 * interfaces will be claimed.
 *
 * The packets are being moved from the forw_list to the cleanup_list. This
 * makes it possible for already running threads to notice the claim.
 */
static void
batadv_forw_packet_list_steal(struct hlist_head *forw_list,
			      struct hlist_head *cleanup_list,
			      const struct batadv_hard_iface *hard_iface)
{
	struct batadv_forw_packet *forw_packet;
	struct hlist_node *safe_tmp_node;

	hlist_for_each_entry_safe(forw_packet, safe_tmp_node,
				  forw_list, list) {
		/* if purge_outstanding_packets() was called with an argument
		 * we delete only packets belonging to the given interface
		 */
		if (hard_iface &&
		    forw_packet->if_incoming != hard_iface &&
		    forw_packet->if_outgoing != hard_iface)
			continue;

		hlist_del(&forw_packet->list);
		hlist_add_head(&forw_packet->cleanup_list, cleanup_list);
	}
}

/**
 * batadv_forw_packet_list_free() - free a list of forward packets
 * @head: a list of to be freed forw_packets
 *
 * This function cancels the scheduling of any packet in the provided list,
 * waits for any possibly running packet forwarding thread to finish and
 * finally, safely frees this forward packet.
 *
 * This function might sleep.
 */
static void batadv_forw_packet_list_free(struct hlist_head *head)
{
	struct batadv_forw_packet *forw_packet;
	struct hlist_node *safe_tmp_node;

	hlist_for_each_entry_safe(forw_packet, safe_tmp_node, head,
				  cleanup_list) {
		cancel_delayed_work_sync(&forw_packet->delayed_work);

		hlist_del(&forw_packet->cleanup_list);
		batadv_forw_packet_free(forw_packet, true);
	}
}

/**
 * batadv_forw_packet_queue() - try to queue a forwarding packet
 * @forw_packet: the forwarding packet to queue
 * @lock: a key to the store (e.g. forw_{bat,bcast}_list_lock)
 * @head: the shelve to queue it on (e.g. forw_{bat,bcast}_list)
 * @send_time: timestamp (jiffies) when the packet is to be sent
 *
 * This function tries to (re)queue a forwarding packet. Requeuing
 * is prevented if the according interface is shutting down
 * (e.g. if batadv_forw_packet_list_steal() was called for this
 * packet earlier).
 *
 * Calling batadv_forw_packet_queue() after a call to
 * batadv_forw_packet_steal() is forbidden!
 *
 * Caller needs to ensure that forw_packet->delayed_work was initialized.
 */
static void batadv_forw_packet_queue(struct batadv_forw_packet *forw_packet,
				     spinlock_t *lock, struct hlist_head *head,
				     unsigned long send_time)
{
	spin_lock_bh(lock);

	/* did purging routine steal it from us? */
	if (batadv_forw_packet_was_stolen(forw_packet)) {
		/* If you got it for free() without trouble, then
		 * don't get back into the queue after stealing...
		 */
		WARN_ONCE(hlist_fake(&forw_packet->cleanup_list),
			  "Requeuing after batadv_forw_packet_steal() not allowed!\n");

		spin_unlock_bh(lock);
		return;
	}

	hlist_del_init(&forw_packet->list);
	hlist_add_head(&forw_packet->list, head);

	queue_delayed_work(batadv_event_workqueue,
			   &forw_packet->delayed_work,
			   send_time - jiffies);
	spin_unlock_bh(lock);
}

/**
 * batadv_forw_packet_bcast_queue() - try to queue a broadcast packet
 * @bat_priv: the bat priv with all the soft interface information
 * @forw_packet: the forwarding packet to queue
 * @send_time: timestamp (jiffies) when the packet is to be sent
 *
 * This function tries to (re)queue a broadcast packet.
 *
 * Caller needs to ensure that forw_packet->delayed_work was initialized.
 */
static void
batadv_forw_packet_bcast_queue(struct batadv_priv *bat_priv,
			       struct batadv_forw_packet *forw_packet,
			       unsigned long send_time)
{
	batadv_forw_packet_queue(forw_packet, &bat_priv->forw_bcast_list_lock,
				 &bat_priv->forw_bcast_list, send_time);
}

/**
 * batadv_forw_packet_ogmv1_queue() - try to queue an OGMv1 packet
 * @bat_priv: the bat priv with all the soft interface information
 * @forw_packet: the forwarding packet to queue
 * @send_time: timestamp (jiffies) when the packet is to be sent
 *
 * This function tries to (re)queue an OGMv1 packet.
 *
 * Caller needs to ensure that forw_packet->delayed_work was initialized.
 */
void batadv_forw_packet_ogmv1_queue(struct batadv_priv *bat_priv,
				    struct batadv_forw_packet *forw_packet,
				    unsigned long send_time)
{
	batadv_forw_packet_queue(forw_packet, &bat_priv->forw_bat_list_lock,
				 &bat_priv->forw_bat_list, send_time);
}

/**
 * batadv_forw_bcast_packet_to_list() - queue broadcast packet for transmissions
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: broadcast packet to add
 * @delay: number of jiffies to wait before sending
 * @own_packet: true if it is a self-generated broadcast packet
 * @if_in: the interface where the packet was received on
 * @if_out: the outgoing interface to queue on
 *
 * Adds a broadcast packet to the queue and sets up timers. Broadcast packets
 * are sent multiple times to increase probability for being received.
 *
 * Return: NETDEV_TX_OK on success and NETDEV_TX_BUSY on errors.
 */
static int batadv_forw_bcast_packet_to_list(struct batadv_priv *bat_priv,
					    struct sk_buff *skb,
					    unsigned long delay,
					    bool own_packet,
					    struct batadv_hard_iface *if_in,
					    struct batadv_hard_iface *if_out)
{
	struct batadv_forw_packet *forw_packet;
	unsigned long send_time = jiffies;
	struct sk_buff *newskb;

	newskb = skb_copy(skb, GFP_ATOMIC);
	if (!newskb)
		goto err;

	forw_packet = batadv_forw_packet_alloc(if_in, if_out,
					       &bat_priv->bcast_queue_left,
					       bat_priv, newskb);
	if (!forw_packet)
		goto err_packet_free;

	forw_packet->own = own_packet;

	INIT_DELAYED_WORK(&forw_packet->delayed_work,
			  batadv_send_outstanding_bcast_packet);

	send_time += delay ? delay : msecs_to_jiffies(5);

	batadv_forw_packet_bcast_queue(bat_priv, forw_packet, send_time);
	return NETDEV_TX_OK;

err_packet_free:
	kfree_skb(newskb);
err:
	return NETDEV_TX_BUSY;
}

/**
 * batadv_forw_bcast_packet_if() - forward and queue a broadcast packet
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: broadcast packet to add
 * @delay: number of jiffies to wait before sending
 * @own_packet: true if it is a self-generated broadcast packet
 * @if_in: the interface where the packet was received on
 * @if_out: the outgoing interface to forward to
 *
 * Transmits a broadcast packet on the specified interface either immediately
 * or if a delay is given after that. Furthermore, queues additional
 * retransmissions if this interface is a wireless one.
 *
 * Return: NETDEV_TX_OK on success and NETDEV_TX_BUSY on errors.
 */
static int batadv_forw_bcast_packet_if(struct batadv_priv *bat_priv,
				       struct sk_buff *skb,
				       unsigned long delay,
				       bool own_packet,
				       struct batadv_hard_iface *if_in,
				       struct batadv_hard_iface *if_out)
{
	unsigned int num_bcasts = if_out->num_bcasts;
	struct sk_buff *newskb;
	int ret = NETDEV_TX_OK;

	if (!delay) {
		newskb = skb_copy(skb, GFP_ATOMIC);
		if (!newskb)
			return NETDEV_TX_BUSY;

		batadv_send_broadcast_skb(newskb, if_out);
		num_bcasts--;
	}

	/* delayed broadcast or rebroadcasts? */
	if (num_bcasts >= 1) {
		BATADV_SKB_CB(skb)->num_bcasts = num_bcasts;

		ret = batadv_forw_bcast_packet_to_list(bat_priv, skb, delay,
						       own_packet, if_in,
						       if_out);
	}

	return ret;
}

/**
 * batadv_send_no_broadcast() - check whether (re)broadcast is necessary
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: broadcast packet to check
 * @own_packet: true if it is a self-generated broadcast packet
 * @if_out: the outgoing interface checked and considered for (re)broadcast
 *
 * Return: False if a packet needs to be (re)broadcasted on the given interface,
 * true otherwise.
 */
static bool batadv_send_no_broadcast(struct batadv_priv *bat_priv,
				     struct sk_buff *skb, bool own_packet,
				     struct batadv_hard_iface *if_out)
{
	struct batadv_hardif_neigh_node *neigh_node = NULL;
	struct batadv_bcast_packet *bcast_packet;
	u8 *orig_neigh;
	u8 *neigh_addr;
	char *type;
	int ret;

	if (!own_packet) {
		neigh_addr = eth_hdr(skb)->h_source;
		neigh_node = batadv_hardif_neigh_get(if_out,
						     neigh_addr);
	}

	bcast_packet = (struct batadv_bcast_packet *)skb->data;
	orig_neigh = neigh_node ? neigh_node->orig : NULL;

	ret = batadv_hardif_no_broadcast(if_out, bcast_packet->orig,
					 orig_neigh);

	if (neigh_node)
		batadv_hardif_neigh_put(neigh_node);

	/* ok, may broadcast */
	if (!ret)
		return false;

	/* no broadcast */
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

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "BCAST packet from orig %pM on %s suppressed: %s\n",
		   bcast_packet->orig,
		   if_out->net_dev->name, type);

	return true;
}

/**
 * __batadv_forw_bcast_packet() - forward and queue a broadcast packet
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: broadcast packet to add
 * @delay: number of jiffies to wait before sending
 * @own_packet: true if it is a self-generated broadcast packet
 *
 * Transmits a broadcast packet either immediately or if a delay is given
 * after that. Furthermore, queues additional retransmissions on wireless
 * interfaces.
 *
 * This call clones the given skb, hence the caller needs to take into
 * account that the data segment of the given skb might not be
 * modifiable anymore.
 *
 * Return: NETDEV_TX_OK on success and NETDEV_TX_BUSY on errors.
 */
static int __batadv_forw_bcast_packet(struct batadv_priv *bat_priv,
				      struct sk_buff *skb,
				      unsigned long delay,
				      bool own_packet)
{
	struct batadv_hard_iface *hard_iface;
	struct batadv_hard_iface *primary_if;
	int ret = NETDEV_TX_OK;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		return NETDEV_TX_BUSY;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->soft_iface != bat_priv->soft_iface)
			continue;

		if (!kref_get_unless_zero(&hard_iface->refcount))
			continue;

		if (batadv_send_no_broadcast(bat_priv, skb, own_packet,
					     hard_iface)) {
			batadv_hardif_put(hard_iface);
			continue;
		}

		ret = batadv_forw_bcast_packet_if(bat_priv, skb, delay,
						  own_packet, primary_if,
						  hard_iface);
		batadv_hardif_put(hard_iface);

		if (ret == NETDEV_TX_BUSY)
			break;
	}
	rcu_read_unlock();

	batadv_hardif_put(primary_if);
	return ret;
}

/**
 * batadv_forw_bcast_packet() - forward and queue a broadcast packet
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: broadcast packet to add
 * @delay: number of jiffies to wait before sending
 * @own_packet: true if it is a self-generated broadcast packet
 *
 * Transmits a broadcast packet either immediately or if a delay is given
 * after that. Furthermore, queues additional retransmissions on wireless
 * interfaces.
 *
 * Return: NETDEV_TX_OK on success and NETDEV_TX_BUSY on errors.
 */
int batadv_forw_bcast_packet(struct batadv_priv *bat_priv,
			     struct sk_buff *skb,
			     unsigned long delay,
			     bool own_packet)
{
	return __batadv_forw_bcast_packet(bat_priv, skb, delay, own_packet);
}

/**
 * batadv_send_bcast_packet() - send and queue a broadcast packet
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: broadcast packet to add
 * @delay: number of jiffies to wait before sending
 * @own_packet: true if it is a self-generated broadcast packet
 *
 * Transmits a broadcast packet either immediately or if a delay is given
 * after that. Furthermore, queues additional retransmissions on wireless
 * interfaces.
 *
 * Consumes the provided skb.
 */
void batadv_send_bcast_packet(struct batadv_priv *bat_priv,
			      struct sk_buff *skb,
			      unsigned long delay,
			      bool own_packet)
{
	__batadv_forw_bcast_packet(bat_priv, skb, delay, own_packet);
	consume_skb(skb);
}

/**
 * batadv_forw_packet_bcasts_left() - check if a retransmission is necessary
 * @forw_packet: the forwarding packet to check
 *
 * Checks whether a given packet has any (re)transmissions left on the provided
 * interface.
 *
 * hard_iface may be NULL: In that case the number of transmissions this skb had
 * so far is compared with the maximum amount of retransmissions independent of
 * any interface instead.
 *
 * Return: True if (re)transmissions are left, false otherwise.
 */
static bool
batadv_forw_packet_bcasts_left(struct batadv_forw_packet *forw_packet)
{
	return BATADV_SKB_CB(forw_packet->skb)->num_bcasts;
}

/**
 * batadv_forw_packet_bcasts_dec() - decrement retransmission counter of a
 *  packet
 * @forw_packet: the packet to decrease the counter for
 */
static void
batadv_forw_packet_bcasts_dec(struct batadv_forw_packet *forw_packet)
{
	BATADV_SKB_CB(forw_packet->skb)->num_bcasts--;
}

/**
 * batadv_forw_packet_is_rebroadcast() - check packet for previous transmissions
 * @forw_packet: the packet to check
 *
 * Return: True if this packet was transmitted before, false otherwise.
 */
bool batadv_forw_packet_is_rebroadcast(struct batadv_forw_packet *forw_packet)
{
	unsigned char num_bcasts = BATADV_SKB_CB(forw_packet->skb)->num_bcasts;

	return num_bcasts != forw_packet->if_outgoing->num_bcasts;
}

/**
 * batadv_send_outstanding_bcast_packet() - transmit a queued broadcast packet
 * @work: work queue item
 *
 * Transmits a queued broadcast packet and if necessary reschedules it.
 */
static void batadv_send_outstanding_bcast_packet(struct work_struct *work)
{
	unsigned long send_time = jiffies + msecs_to_jiffies(5);
	struct batadv_forw_packet *forw_packet;
	struct delayed_work *delayed_work;
	struct batadv_priv *bat_priv;
	struct sk_buff *skb1;
	bool dropped = false;

	delayed_work = to_delayed_work(work);
	forw_packet = container_of(delayed_work, struct batadv_forw_packet,
				   delayed_work);
	bat_priv = netdev_priv(forw_packet->if_incoming->soft_iface);

	if (atomic_read(&bat_priv->mesh_state) == BATADV_MESH_DEACTIVATING) {
		dropped = true;
		goto out;
	}

	if (batadv_dat_drop_broadcast_packet(bat_priv, forw_packet)) {
		dropped = true;
		goto out;
	}

	/* send a copy of the saved skb */
	skb1 = skb_clone(forw_packet->skb, GFP_ATOMIC);
	if (!skb1)
		goto out;

	batadv_send_broadcast_skb(skb1, forw_packet->if_outgoing);
	batadv_forw_packet_bcasts_dec(forw_packet);

	if (batadv_forw_packet_bcasts_left(forw_packet)) {
		batadv_forw_packet_bcast_queue(bat_priv, forw_packet,
					       send_time);
		return;
	}

out:
	/* do we get something for free()? */
	if (batadv_forw_packet_steal(forw_packet,
				     &bat_priv->forw_bcast_list_lock))
		batadv_forw_packet_free(forw_packet, dropped);
}

/**
 * batadv_purge_outstanding_packets() - stop/purge scheduled bcast/OGMv1 packets
 * @bat_priv: the bat priv with all the soft interface information
 * @hard_iface: the hard interface to cancel and purge bcast/ogm packets on
 *
 * This method cancels and purges any broadcast and OGMv1 packet on the given
 * hard_iface. If hard_iface is NULL, broadcast and OGMv1 packets on all hard
 * interfaces will be canceled and purged.
 *
 * This function might sleep.
 */
void
batadv_purge_outstanding_packets(struct batadv_priv *bat_priv,
				 const struct batadv_hard_iface *hard_iface)
{
	struct hlist_head head = HLIST_HEAD_INIT;

	if (hard_iface)
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "%s(): %s\n",
			   __func__, hard_iface->net_dev->name);
	else
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "%s()\n", __func__);

	/* claim bcast list for free() */
	spin_lock_bh(&bat_priv->forw_bcast_list_lock);
	batadv_forw_packet_list_steal(&bat_priv->forw_bcast_list, &head,
				      hard_iface);
	spin_unlock_bh(&bat_priv->forw_bcast_list_lock);

	/* claim batman packet list for free() */
	spin_lock_bh(&bat_priv->forw_bat_list_lock);
	batadv_forw_packet_list_steal(&bat_priv->forw_bat_list, &head,
				      hard_iface);
	spin_unlock_bh(&bat_priv->forw_bat_list_lock);

	/* then cancel or wait for packet workers to finish and free */
	batadv_forw_packet_list_free(&head);
}
