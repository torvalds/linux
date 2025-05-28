// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) B.A.T.M.A.N. contributors:
 *
 * Linus Lüssing
 */

#include "multicast.h"
#include "main.h"

#include <linux/bug.h>
#include <linux/build_bug.h>
#include <linux/byteorder/generic.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/gfp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/ipv6.h>
#include <linux/limits.h>
#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <uapi/linux/batadv_packet.h>

#include "bridge_loop_avoidance.h"
#include "originator.h"
#include "send.h"
#include "translation-table.h"

#define batadv_mcast_forw_tracker_for_each_dest(dest, num_dests) \
	for (; num_dests; num_dests--, (dest) += ETH_ALEN)

#define batadv_mcast_forw_tracker_for_each_dest2(dest1, dest2, num_dests) \
	for (; num_dests; num_dests--, (dest1) += ETH_ALEN, (dest2) += ETH_ALEN)

/**
 * batadv_mcast_forw_skb_push() - skb_push and memorize amount of pushed bytes
 * @skb: the skb to push onto
 * @size: the amount of bytes to push
 * @len: stores the total amount of bytes pushed
 *
 * Performs an skb_push() onto the given skb and adds the amount of pushed bytes
 * to the given len pointer.
 *
 * Return: the return value of the skb_push() call.
 */
static void *batadv_mcast_forw_skb_push(struct sk_buff *skb, size_t size,
					unsigned short *len)
{
	*len += size;
	return skb_push(skb, size);
}

/**
 * batadv_mcast_forw_push_padding() - push 2 padding bytes to skb's front
 * @skb: the skb to push onto
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Pushes two padding bytes to the front of the given skb.
 *
 * Return: On success a pointer to the first byte of the two pushed padding
 * bytes within the skb. NULL otherwise.
 */
static char *
batadv_mcast_forw_push_padding(struct sk_buff *skb, unsigned short *tvlv_len)
{
	const int pad_len = 2;
	char *padding;

	if (skb_headroom(skb) < pad_len)
		return NULL;

	padding = batadv_mcast_forw_skb_push(skb, pad_len, tvlv_len);
	memset(padding, 0, pad_len);

	return padding;
}

/**
 * batadv_mcast_forw_push_est_padding() - push padding bytes if necessary
 * @skb: the skb to potentially push the padding onto
 * @count: the (estimated) number of originators the multicast packet needs to
 *  be sent to
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * If the number of destination entries is even then this adds two
 * padding bytes to the end of the tracker TVLV.
 *
 * Return: true on success or if no padding is needed, false otherwise.
 */
static bool
batadv_mcast_forw_push_est_padding(struct sk_buff *skb, int count,
				   unsigned short *tvlv_len)
{
	if (!(count % 2) && !batadv_mcast_forw_push_padding(skb, tvlv_len))
		return false;

	return true;
}

/**
 * batadv_mcast_forw_orig_entry() - get orig_node from an hlist node
 * @node: the hlist node to get the orig_node from
 * @entry_offset: the offset of the hlist node within the orig_node struct
 *
 * Return: The orig_node containing the hlist node on success, NULL on error.
 */
static struct batadv_orig_node *
batadv_mcast_forw_orig_entry(struct hlist_node *node,
			     size_t entry_offset)
{
	/* sanity check */
	switch (entry_offset) {
	case offsetof(struct batadv_orig_node, mcast_want_all_ipv4_node):
	case offsetof(struct batadv_orig_node, mcast_want_all_ipv6_node):
	case offsetof(struct batadv_orig_node, mcast_want_all_rtr4_node):
	case offsetof(struct batadv_orig_node, mcast_want_all_rtr6_node):
		break;
	default:
		WARN_ON(1);
		return NULL;
	}

	return (struct batadv_orig_node *)((void *)node - entry_offset);
}

/**
 * batadv_mcast_forw_push_dest() - push an originator MAC address onto an skb
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the skb to push the destination address onto
 * @vid: the vlan identifier
 * @orig_node: the originator node to get the MAC address from
 * @num_dests: a pointer to store the number of pushed addresses in
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * If the orig_node is a BLA backbone gateway, if there is not enough skb
 * headroom available or if num_dests is already at its maximum (65535) then
 * neither the skb nor num_dests is changed. Otherwise the originator's MAC
 * address is pushed onto the given skb and num_dests incremented by one.
 *
 * Return: true if the orig_node is a backbone gateway or if an orig address
 *  was pushed successfully, false otherwise.
 */
static bool batadv_mcast_forw_push_dest(struct batadv_priv *bat_priv,
					struct sk_buff *skb, unsigned short vid,
					struct batadv_orig_node *orig_node,
					unsigned short *num_dests,
					unsigned short *tvlv_len)
{
	BUILD_BUG_ON(sizeof_field(struct batadv_tvlv_mcast_tracker, num_dests)
		     != sizeof(__be16));

	/* Avoid sending to other BLA gateways - they already got the frame from
	 * the LAN side we share with them.
	 * TODO: Refactor to take BLA into account earlier in mode check.
	 */
	if (batadv_bla_is_backbone_gw_orig(bat_priv, orig_node->orig, vid))
		return true;

	if (skb_headroom(skb) < ETH_ALEN || *num_dests == U16_MAX)
		return false;

	batadv_mcast_forw_skb_push(skb, ETH_ALEN, tvlv_len);
	ether_addr_copy(skb->data, orig_node->orig);
	(*num_dests)++;

	return true;
}

/**
 * batadv_mcast_forw_push_dests_list() - push originators from list onto an skb
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the skb to push the destination addresses onto
 * @vid: the vlan identifier
 * @head: the list to gather originators from
 * @entry_offset: offset of an hlist node in an orig_node structure
 * @num_dests: a pointer to store the number of pushed addresses in
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Push the MAC addresses of all originators in the given list onto the given
 * skb.
 *
 * Return: true on success, false otherwise.
 */
static int batadv_mcast_forw_push_dests_list(struct batadv_priv *bat_priv,
					     struct sk_buff *skb,
					     unsigned short vid,
					     struct hlist_head *head,
					     size_t entry_offset,
					     unsigned short *num_dests,
					     unsigned short *tvlv_len)
{
	struct hlist_node *node;
	struct batadv_orig_node *orig_node;

	rcu_read_lock();
	__hlist_for_each_rcu(node, head) {
		orig_node = batadv_mcast_forw_orig_entry(node, entry_offset);
		if (!orig_node ||
		    !batadv_mcast_forw_push_dest(bat_priv, skb, vid, orig_node,
						 num_dests, tvlv_len)) {
			rcu_read_unlock();
			return false;
		}
	}
	rcu_read_unlock();

	return true;
}

/**
 * batadv_mcast_forw_push_tt() - push originators with interest through TT
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the skb to push the destination addresses onto
 * @vid: the vlan identifier
 * @num_dests: a pointer to store the number of pushed addresses in
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Push the MAC addresses of all originators which have indicated interest in
 * this multicast packet through the translation table onto the given skb.
 *
 * Return: true on success, false otherwise.
 */
static bool
batadv_mcast_forw_push_tt(struct batadv_priv *bat_priv, struct sk_buff *skb,
			  unsigned short vid, unsigned short *num_dests,
			  unsigned short *tvlv_len)
{
	struct batadv_tt_orig_list_entry *orig_entry;

	struct batadv_tt_global_entry *tt_global;
	const u8 *addr = eth_hdr(skb)->h_dest;

	/* ok */
	int ret = true;

	tt_global = batadv_tt_global_hash_find(bat_priv, addr, vid);
	if (!tt_global)
		goto out;

	rcu_read_lock();
	hlist_for_each_entry_rcu(orig_entry, &tt_global->orig_list, list) {
		if (!batadv_mcast_forw_push_dest(bat_priv, skb, vid,
						 orig_entry->orig_node,
						 num_dests, tvlv_len)) {
			ret = false;
			break;
		}
	}
	rcu_read_unlock();

	batadv_tt_global_entry_put(tt_global);

out:
	return ret;
}

/**
 * batadv_mcast_forw_push_want_all() - push originators with want-all flag
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the skb to push the destination addresses onto
 * @vid: the vlan identifier
 * @num_dests: a pointer to store the number of pushed addresses in
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Push the MAC addresses of all originators which have indicated interest in
 * this multicast packet through the want-all flag onto the given skb.
 *
 * Return: true on success, false otherwise.
 */
static bool batadv_mcast_forw_push_want_all(struct batadv_priv *bat_priv,
					    struct sk_buff *skb,
					    unsigned short vid,
					    unsigned short *num_dests,
					    unsigned short *tvlv_len)
{
	struct hlist_head *head = NULL;
	size_t offset;
	int ret;

	switch (eth_hdr(skb)->h_proto) {
	case htons(ETH_P_IP):
		head = &bat_priv->mcast.want_all_ipv4_list;
		offset = offsetof(struct batadv_orig_node,
				  mcast_want_all_ipv4_node);
		break;
	case htons(ETH_P_IPV6):
		head = &bat_priv->mcast.want_all_ipv6_list;
		offset = offsetof(struct batadv_orig_node,
				  mcast_want_all_ipv6_node);
		break;
	default:
		return false;
	}

	ret = batadv_mcast_forw_push_dests_list(bat_priv, skb, vid, head,
						offset, num_dests, tvlv_len);
	if (!ret)
		return false;

	return true;
}

/**
 * batadv_mcast_forw_push_want_rtr() - push originators with want-router flag
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the skb to push the destination addresses onto
 * @vid: the vlan identifier
 * @num_dests: a pointer to store the number of pushed addresses in
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Push the MAC addresses of all originators which have indicated interest in
 * this multicast packet through the want-all-rtr flag onto the given skb.
 *
 * Return: true on success, false otherwise.
 */
static bool batadv_mcast_forw_push_want_rtr(struct batadv_priv *bat_priv,
					    struct sk_buff *skb,
					    unsigned short vid,
					    unsigned short *num_dests,
					    unsigned short *tvlv_len)
{
	struct hlist_head *head = NULL;
	size_t offset;
	int ret;

	switch (eth_hdr(skb)->h_proto) {
	case htons(ETH_P_IP):
		head = &bat_priv->mcast.want_all_rtr4_list;
		offset = offsetof(struct batadv_orig_node,
				  mcast_want_all_rtr4_node);
		break;
	case htons(ETH_P_IPV6):
		head = &bat_priv->mcast.want_all_rtr6_list;
		offset = offsetof(struct batadv_orig_node,
				  mcast_want_all_rtr6_node);
		break;
	default:
		return false;
	}

	ret = batadv_mcast_forw_push_dests_list(bat_priv, skb, vid, head,
						offset, num_dests, tvlv_len);
	if (!ret)
		return false;

	return true;
}

/**
 * batadv_mcast_forw_scrape() - remove bytes within skb data
 * @skb: the skb to remove bytes from
 * @offset: the offset from the skb data from which to scrape
 * @len: the amount of bytes to scrape starting from the offset
 *
 * Scrapes/removes len bytes from the given skb at the given offset from the
 * skb data.
 *
 * Caller needs to ensure that the region from the skb data's start up
 * to/including the to be removed bytes are linearized.
 */
static void batadv_mcast_forw_scrape(struct sk_buff *skb,
				     unsigned short offset,
				     unsigned short len)
{
	char *to, *from;

	SKB_LINEAR_ASSERT(skb);

	to = skb_pull(skb, len);
	from = to - len;

	memmove(to, from, offset);
}

/**
 * batadv_mcast_forw_push_scrape_padding() - remove TVLV padding
 * @skb: the skb to potentially adjust the TVLV's padding on
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Remove two padding bytes from the end of the multicast tracker TVLV,
 * from before the payload data.
 *
 * Caller needs to ensure that the TVLV bytes are linearized.
 */
static void batadv_mcast_forw_push_scrape_padding(struct sk_buff *skb,
						  unsigned short *tvlv_len)
{
	const int pad_len = 2;

	batadv_mcast_forw_scrape(skb, *tvlv_len - pad_len, pad_len);
	*tvlv_len -= pad_len;
}

/**
 * batadv_mcast_forw_push_insert_padding() - insert TVLV padding
 * @skb: the skb to potentially adjust the TVLV's padding on
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Inserts two padding bytes at the end of the multicast tracker TVLV,
 * before the payload data in the given skb.
 *
 * Return: true on success, false otherwise.
 */
static bool batadv_mcast_forw_push_insert_padding(struct sk_buff *skb,
						  unsigned short *tvlv_len)
{
	unsigned short offset =	*tvlv_len;
	char *to, *from = skb->data;

	to = batadv_mcast_forw_push_padding(skb, tvlv_len);
	if (!to)
		return false;

	memmove(to, from, offset);
	memset(to + offset, 0, *tvlv_len - offset);
	return true;
}

/**
 * batadv_mcast_forw_push_adjust_padding() - adjust padding if necessary
 * @skb: the skb to potentially adjust the TVLV's padding on
 * @count: the estimated number of originators the multicast packet needs to
 *  be sent to
 * @num_dests_pushed: the number of originators that were actually added to the
 *  multicast packet's tracker TVLV
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Adjusts the padding in the multicast packet's tracker TVLV depending on the
 * initially estimated amount of destinations versus the amount of destinations
 * that were actually added to the tracker TVLV.
 *
 * If the initial estimate was correct or at least the oddness was the same then
 * no padding adjustment is performed.
 * If the initially estimated number was even, so padding was initially added,
 * but it turned out to be odd then padding is removed.
 * If the initially estimated number was odd, so no padding was initially added,
 * but it turned out to be even then padding is added.
 *
 * Return: true if no padding adjustment is needed or the adjustment was
 * successful, false otherwise.
 */
static bool
batadv_mcast_forw_push_adjust_padding(struct sk_buff *skb, int *count,
				      unsigned short num_dests_pushed,
				      unsigned short *tvlv_len)
{
	int ret = true;

	if (likely((num_dests_pushed % 2) == (*count % 2)))
		goto out;

	/**
	 * estimated even number of destinations, but turned out to be odd
	 * -> remove padding
	 */
	if (!(*count % 2) && (num_dests_pushed % 2))
		batadv_mcast_forw_push_scrape_padding(skb, tvlv_len);
	/**
	 * estimated odd number of destinations, but turned out to be even
	 * -> add padding
	 */
	else if ((*count % 2) && (!(num_dests_pushed % 2)))
		ret = batadv_mcast_forw_push_insert_padding(skb, tvlv_len);

out:
	*count = num_dests_pushed;
	return ret;
}

/**
 * batadv_mcast_forw_push_dests() - push originator addresses onto an skb
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the skb to push the destination addresses onto
 * @vid: the vlan identifier
 * @is_routable: indicates whether the destination is routable
 * @count: the number of originators the multicast packet needs to be sent to
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Push the MAC addresses of all originators which have indicated interest in
 * this multicast packet onto the given skb.
 *
 * Return: -ENOMEM if there is not enough skb headroom available. Otherwise, on
 * success 0.
 */
static int
batadv_mcast_forw_push_dests(struct batadv_priv *bat_priv, struct sk_buff *skb,
			     unsigned short vid, int is_routable, int *count,
			     unsigned short *tvlv_len)
{
	unsigned short num_dests = 0;

	if (!batadv_mcast_forw_push_est_padding(skb, *count, tvlv_len))
		goto err;

	if (!batadv_mcast_forw_push_tt(bat_priv, skb, vid, &num_dests,
				       tvlv_len))
		goto err;

	if (!batadv_mcast_forw_push_want_all(bat_priv, skb, vid, &num_dests,
					     tvlv_len))
		goto err;

	if (is_routable &&
	    !batadv_mcast_forw_push_want_rtr(bat_priv, skb, vid, &num_dests,
					     tvlv_len))
		goto err;

	if (!batadv_mcast_forw_push_adjust_padding(skb, count, num_dests,
						   tvlv_len))
		goto err;

	return 0;
err:
	return -ENOMEM;
}

/**
 * batadv_mcast_forw_push_tracker() - push a multicast tracker TVLV header
 * @skb: the skb to push the tracker TVLV onto
 * @num_dests: the number of destination addresses to set in the header
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Pushes a multicast tracker TVLV header onto the given skb, including the
 * generic TVLV header but excluding the destination MAC addresses.
 *
 * The provided num_dests value is taken into consideration to set the
 * num_dests field in the tracker header and to set the appropriate TVLV length
 * value fields.
 *
 * Return: -ENOMEM if there is not enough skb headroom available. Otherwise, on
 * success 0.
 */
static int batadv_mcast_forw_push_tracker(struct sk_buff *skb, int num_dests,
					  unsigned short *tvlv_len)
{
	struct batadv_tvlv_mcast_tracker *mcast_tracker;
	struct batadv_tvlv_hdr *tvlv_hdr;
	unsigned int tvlv_value_len;

	if (skb_headroom(skb) < sizeof(*mcast_tracker) + sizeof(*tvlv_hdr))
		return -ENOMEM;

	tvlv_value_len = sizeof(*mcast_tracker) + *tvlv_len;
	if (tvlv_value_len + sizeof(*tvlv_hdr) > U16_MAX)
		return -ENOMEM;

	batadv_mcast_forw_skb_push(skb, sizeof(*mcast_tracker), tvlv_len);
	mcast_tracker = (struct batadv_tvlv_mcast_tracker *)skb->data;
	mcast_tracker->num_dests = htons(num_dests);

	skb_reset_network_header(skb);

	batadv_mcast_forw_skb_push(skb, sizeof(*tvlv_hdr), tvlv_len);
	tvlv_hdr = (struct batadv_tvlv_hdr *)skb->data;
	tvlv_hdr->type = BATADV_TVLV_MCAST_TRACKER;
	tvlv_hdr->version = 1;
	tvlv_hdr->len = htons(tvlv_value_len);

	return 0;
}

/**
 * batadv_mcast_forw_push_tvlvs() - push a multicast tracker TVLV onto an skb
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the skb to push the tracker TVLV onto
 * @vid: the vlan identifier
 * @is_routable: indicates whether the destination is routable
 * @count: the number of originators the multicast packet needs to be sent to
 * @tvlv_len: stores the amount of currently pushed TVLV bytes
 *
 * Pushes a multicast tracker TVLV onto the given skb, including the collected
 * destination MAC addresses and the generic TVLV header.
 *
 * Return: -ENOMEM if there is not enough skb headroom available. Otherwise, on
 * success 0.
 */
static int
batadv_mcast_forw_push_tvlvs(struct batadv_priv *bat_priv, struct sk_buff *skb,
			     unsigned short vid, int is_routable, int count,
			     unsigned short *tvlv_len)
{
	int ret;

	ret = batadv_mcast_forw_push_dests(bat_priv, skb, vid, is_routable,
					   &count, tvlv_len);
	if (ret < 0)
		return ret;

	ret = batadv_mcast_forw_push_tracker(skb, count, tvlv_len);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * batadv_mcast_forw_push_hdr() - push a multicast packet header onto an skb
 * @skb: the skb to push the header onto
 * @tvlv_len: the total TVLV length value to set in the header
 *
 * Pushes a batman-adv multicast packet header onto the given skb and sets
 * the provided total TVLV length value in it.
 *
 * Caller needs to ensure enough skb headroom is available.
 *
 * Return: -ENOMEM if there is not enough skb headroom available. Otherwise, on
 * success 0.
 */
static int
batadv_mcast_forw_push_hdr(struct sk_buff *skb, unsigned short tvlv_len)
{
	struct batadv_mcast_packet *mcast_packet;

	if (skb_headroom(skb) < sizeof(*mcast_packet))
		return -ENOMEM;

	skb_push(skb, sizeof(*mcast_packet));

	mcast_packet = (struct batadv_mcast_packet *)skb->data;
	mcast_packet->version = BATADV_COMPAT_VERSION;
	mcast_packet->ttl = BATADV_TTL;
	mcast_packet->packet_type = BATADV_MCAST;
	mcast_packet->reserved = 0;
	mcast_packet->tvlv_len = htons(tvlv_len);

	return 0;
}

/**
 * batadv_mcast_forw_scrub_dests() - scrub destinations in a tracker TVLV
 * @bat_priv: the bat priv with all the mesh interface information
 * @comp_neigh: next hop neighbor to scrub+collect destinations for
 * @dest: start MAC entry in original skb's tracker TVLV
 * @next_dest: start MAC entry in to be sent skb's tracker TVLV
 * @num_dests: number of remaining destination MAC entries to iterate over
 *
 * This sorts destination entries into either the original batman-adv
 * multicast packet or the skb (copy) that is going to be sent to comp_neigh
 * next.
 *
 * In preparation for the next, to be (unicast) transmitted batman-adv multicast
 * packet skb to be sent to the given neighbor node, tries to collect all
 * originator MAC addresses that have the given neighbor node as their next hop
 * in the to be transmitted skb (copy), which next_dest points into. That is we
 * zero all destination entries in next_dest which do not have comp_neigh as
 * their next hop. And zero all destination entries in the original skb that
 * would have comp_neigh as their next hop (to avoid redundant transmissions and
 * duplicated payload later).
 */
static void
batadv_mcast_forw_scrub_dests(struct batadv_priv *bat_priv,
			      struct batadv_neigh_node *comp_neigh, u8 *dest,
			      u8 *next_dest, u16 num_dests)
{
	struct batadv_neigh_node *next_neigh;

	/* skip first entry, this is what we are comparing with */
	eth_zero_addr(dest);
	dest += ETH_ALEN;
	next_dest += ETH_ALEN;
	num_dests--;

	batadv_mcast_forw_tracker_for_each_dest2(dest, next_dest, num_dests) {
		if (is_zero_ether_addr(next_dest))
			continue;

		/* sanity check, we expect unicast destinations */
		if (is_multicast_ether_addr(next_dest)) {
			eth_zero_addr(dest);
			eth_zero_addr(next_dest);
			continue;
		}

		next_neigh = batadv_orig_to_router(bat_priv, next_dest, NULL);
		if (!next_neigh) {
			eth_zero_addr(next_dest);
			continue;
		}

		if (!batadv_compare_eth(next_neigh->addr, comp_neigh->addr)) {
			eth_zero_addr(next_dest);
			batadv_neigh_node_put(next_neigh);
			continue;
		}

		/* found an entry for our next packet to transmit, so remove it
		 * from the original packet
		 */
		eth_zero_addr(dest);
		batadv_neigh_node_put(next_neigh);
	}
}

/**
 * batadv_mcast_forw_shrink_fill() - swap slot with next non-zero destination
 * @slot: the to be filled zero-MAC destination entry in a tracker TVLV
 * @num_dests_slot: remaining entries in tracker TVLV from/including slot
 *
 * Searches for the next non-zero-MAC destination entry in a tracker TVLV after
 * the given slot pointer. And if found, swaps it with the zero-MAC destination
 * entry which the slot points to.
 *
 * Return: true if slot was swapped/filled successfully, false otherwise.
 */
static bool batadv_mcast_forw_shrink_fill(u8 *slot, u16 num_dests_slot)
{
	u16 num_dests_filler;
	u8 *filler;

	/* sanity check, should not happen */
	if (!num_dests_slot)
		return false;

	num_dests_filler = num_dests_slot - 1;
	filler = slot + ETH_ALEN;

	/* find a candidate to fill the empty slot */
	batadv_mcast_forw_tracker_for_each_dest(filler, num_dests_filler) {
		if (is_zero_ether_addr(filler))
			continue;

		ether_addr_copy(slot, filler);
		eth_zero_addr(filler);
		return true;
	}

	return false;
}

/**
 * batadv_mcast_forw_shrink_pack_dests() - pack destinations of a tracker TVLV
 * @skb: the batman-adv multicast packet to compact destinations in
 *
 * Compacts the originator destination MAC addresses in the multicast tracker
 * TVLV of the given multicast packet. This is done by moving all non-zero
 * MAC addresses in direction of the skb head and all zero MAC addresses in skb
 * tail direction, within the multicast tracker TVLV.
 *
 * Return: The number of consecutive zero MAC address destinations which are
 * now at the end of the multicast tracker TVLV.
 */
static int batadv_mcast_forw_shrink_pack_dests(struct sk_buff *skb)
{
	struct batadv_tvlv_mcast_tracker *mcast_tracker;
	unsigned char *skb_net_hdr;
	u16 num_dests_slot;
	u8 *slot;

	skb_net_hdr = skb_network_header(skb);
	mcast_tracker = (struct batadv_tvlv_mcast_tracker *)skb_net_hdr;
	num_dests_slot = ntohs(mcast_tracker->num_dests);

	slot = (u8 *)mcast_tracker + sizeof(*mcast_tracker);

	batadv_mcast_forw_tracker_for_each_dest(slot, num_dests_slot) {
		/* find an empty slot */
		if (!is_zero_ether_addr(slot))
			continue;

		if (!batadv_mcast_forw_shrink_fill(slot, num_dests_slot))
			/* could not find a filler, so we successfully packed
			 * and can stop - and must not reduce num_dests_slot!
			 */
			break;
	}

	/* num_dests_slot is now the amount of reduced, zeroed
	 * destinations at the end of the tracker TVLV
	 */
	return num_dests_slot;
}

/**
 * batadv_mcast_forw_shrink_align_offset() - get new alignment offset
 * @num_dests_old: the old, to be updated amount of destination nodes
 * @num_dests_reduce: the number of destinations that were removed
 *
 * Calculates the amount of potential extra alignment offset that is needed to
 * adjust the TVLV padding after the change in destination nodes.
 *
 * Return:
 *	0: If no change to padding is needed.
 *	2: If padding needs to be removed.
 *	-2: If padding needs to be added.
 */
static short
batadv_mcast_forw_shrink_align_offset(unsigned int num_dests_old,
				      unsigned int num_dests_reduce)
{
	/* even amount of removed destinations -> no alignment change */
	if (!(num_dests_reduce % 2))
		return 0;

	/* even to odd amount of destinations -> remove padding */
	if (!(num_dests_old % 2))
		return 2;

	/* odd to even amount of destinations -> add padding */
	return -2;
}

/**
 * batadv_mcast_forw_shrink_update_headers() - update shrunk mc packet headers
 * @skb: the batman-adv multicast packet to update headers of
 * @num_dests_reduce: the number of destinations that were removed
 *
 * This updates any fields of a batman-adv multicast packet that are affected
 * by the reduced number of destinations in the multicast tracket TVLV. In
 * particular this updates:
 *
 * The num_dest field of the multicast tracker TVLV.
 * The TVLV length field of the according generic TVLV header.
 * The batman-adv multicast packet's total TVLV length field.
 *
 * Return: The offset in skb's tail direction at which the new batman-adv
 * multicast packet header needs to start.
 */
static unsigned int
batadv_mcast_forw_shrink_update_headers(struct sk_buff *skb,
					unsigned int num_dests_reduce)
{
	struct batadv_tvlv_mcast_tracker *mcast_tracker;
	struct batadv_mcast_packet *mcast_packet;
	struct batadv_tvlv_hdr *tvlv_hdr;
	unsigned char *skb_net_hdr;
	unsigned int offset;
	short align_offset;
	u16 num_dests;

	skb_net_hdr = skb_network_header(skb);
	mcast_tracker = (struct batadv_tvlv_mcast_tracker *)skb_net_hdr;
	num_dests = ntohs(mcast_tracker->num_dests);

	align_offset = batadv_mcast_forw_shrink_align_offset(num_dests,
							     num_dests_reduce);
	offset = ETH_ALEN * num_dests_reduce + align_offset;
	num_dests -= num_dests_reduce;

	/* update tracker header */
	mcast_tracker->num_dests = htons(num_dests);

	/* update tracker's tvlv header's length field */
	tvlv_hdr = (struct batadv_tvlv_hdr *)(skb_network_header(skb) -
					      sizeof(*tvlv_hdr));
	tvlv_hdr->len = htons(ntohs(tvlv_hdr->len) - offset);

	/* update multicast packet header's tvlv length field */
	mcast_packet = (struct batadv_mcast_packet *)skb->data;
	mcast_packet->tvlv_len = htons(ntohs(mcast_packet->tvlv_len) - offset);

	return offset;
}

/**
 * batadv_mcast_forw_shrink_move_headers() - move multicast headers by offset
 * @skb: the batman-adv multicast packet to move headers for
 * @offset: a non-negative offset to move headers by, towards the skb tail
 *
 * Moves the batman-adv multicast packet header, its multicast tracker TVLV and
 * any TVLVs in between by the given offset in direction towards the tail.
 */
static void
batadv_mcast_forw_shrink_move_headers(struct sk_buff *skb, unsigned int offset)
{
	struct batadv_tvlv_mcast_tracker *mcast_tracker;
	unsigned char *skb_net_hdr;
	unsigned int len;
	u16 num_dests;

	skb_net_hdr = skb_network_header(skb);
	mcast_tracker = (struct batadv_tvlv_mcast_tracker *)skb_net_hdr;
	num_dests = ntohs(mcast_tracker->num_dests);
	len = skb_network_offset(skb) + sizeof(*mcast_tracker);
	len += num_dests * ETH_ALEN;

	batadv_mcast_forw_scrape(skb, len, offset);
}

/**
 * batadv_mcast_forw_shrink_tracker() - remove zero addresses in a tracker tvlv
 * @skb: the batman-adv multicast packet to (potentially) shrink
 *
 * Removes all destinations with a zero MAC addresses (00:00:00:00:00:00) from
 * the given batman-adv multicast packet's tracker TVLV and updates headers
 * accordingly to maintain a valid batman-adv multicast packet.
 */
static void batadv_mcast_forw_shrink_tracker(struct sk_buff *skb)
{
	unsigned int offset;
	u16 dests_reduced;

	dests_reduced = batadv_mcast_forw_shrink_pack_dests(skb);
	if (!dests_reduced)
		return;

	offset = batadv_mcast_forw_shrink_update_headers(skb, dests_reduced);
	batadv_mcast_forw_shrink_move_headers(skb, offset);
}

/**
 * batadv_mcast_forw_packet() - forward a batman-adv multicast packet
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the received or locally generated batman-adv multicast packet
 * @local_xmit: indicates that the packet was locally generated and not received
 *
 * Parses the tracker TVLV of a batman-adv multicast packet and forwards the
 * packet as indicated in this TVLV.
 *
 * Caller needs to set the skb network header to the start of the multicast
 * tracker TVLV (excluding the generic TVLV header) and the skb transport header
 * to the next byte after this multicast tracker TVLV.
 *
 * Caller needs to free the skb.
 *
 * Return: NET_RX_SUCCESS or NET_RX_DROP on success or a negative error
 * code on failure. NET_RX_SUCCESS if the received packet is supposed to be
 * decapsulated and forwarded to the own mesh interface, NET_RX_DROP otherwise.
 */
static int batadv_mcast_forw_packet(struct batadv_priv *bat_priv,
				    struct sk_buff *skb, bool local_xmit)
{
	struct batadv_tvlv_mcast_tracker *mcast_tracker;
	struct batadv_neigh_node *neigh_node;
	unsigned long offset, num_dests_off;
	struct sk_buff *nexthop_skb;
	unsigned char *skb_net_hdr;
	bool local_recv = false;
	unsigned int tvlv_len;
	bool xmitted = false;
	u8 *dest, *next_dest;
	u16 num_dests;
	int ret;

	/* (at least) TVLV part needs to be linearized */
	SKB_LINEAR_ASSERT(skb);

	/* check if num_dests is within skb length */
	num_dests_off = offsetof(struct batadv_tvlv_mcast_tracker, num_dests);
	if (num_dests_off > skb_network_header_len(skb))
		return -EINVAL;

	skb_net_hdr = skb_network_header(skb);
	mcast_tracker = (struct batadv_tvlv_mcast_tracker *)skb_net_hdr;
	num_dests = ntohs(mcast_tracker->num_dests);

	dest = (u8 *)mcast_tracker + sizeof(*mcast_tracker);

	/* check if full tracker tvlv is within skb length */
	tvlv_len = sizeof(*mcast_tracker) + ETH_ALEN * num_dests;
	if (tvlv_len > skb_network_header_len(skb))
		return -EINVAL;

	/* invalidate checksum: */
	skb->ip_summed = CHECKSUM_NONE;

	batadv_mcast_forw_tracker_for_each_dest(dest, num_dests) {
		if (is_zero_ether_addr(dest))
			continue;

		/* only unicast originator addresses supported */
		if (is_multicast_ether_addr(dest)) {
			eth_zero_addr(dest);
			continue;
		}

		if (batadv_is_my_mac(bat_priv, dest)) {
			eth_zero_addr(dest);
			local_recv = true;
			continue;
		}

		neigh_node = batadv_orig_to_router(bat_priv, dest, NULL);
		if (!neigh_node) {
			eth_zero_addr(dest);
			continue;
		}

		nexthop_skb = skb_copy(skb, GFP_ATOMIC);
		if (!nexthop_skb) {
			batadv_neigh_node_put(neigh_node);
			return -ENOMEM;
		}

		offset = dest - skb->data;
		next_dest = nexthop_skb->data + offset;

		batadv_mcast_forw_scrub_dests(bat_priv, neigh_node, dest,
					      next_dest, num_dests);
		batadv_mcast_forw_shrink_tracker(nexthop_skb);

		batadv_inc_counter(bat_priv, BATADV_CNT_MCAST_TX);
		batadv_add_counter(bat_priv, BATADV_CNT_MCAST_TX_BYTES,
				   nexthop_skb->len + ETH_HLEN);
		xmitted = true;
		ret = batadv_send_unicast_skb(nexthop_skb, neigh_node);

		batadv_neigh_node_put(neigh_node);

		if (ret < 0)
			return ret;
	}

	if (xmitted) {
		if (local_xmit) {
			batadv_inc_counter(bat_priv, BATADV_CNT_MCAST_TX_LOCAL);
			batadv_add_counter(bat_priv,
					   BATADV_CNT_MCAST_TX_LOCAL_BYTES,
					   skb->len -
					   skb_transport_offset(skb));
		} else {
			batadv_inc_counter(bat_priv, BATADV_CNT_MCAST_FWD);
			batadv_add_counter(bat_priv, BATADV_CNT_MCAST_FWD_BYTES,
					   skb->len + ETH_HLEN);
		}
	}

	if (local_recv)
		return NET_RX_SUCCESS;
	else
		return NET_RX_DROP;
}

/**
 * batadv_mcast_forw_tracker_tvlv_handler() - handle an mcast tracker tvlv
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the received batman-adv multicast packet
 *
 * Parses the tracker TVLV of an incoming batman-adv multicast packet and
 * forwards the packet as indicated in this TVLV.
 *
 * Caller needs to set the skb network header to the start of the multicast
 * tracker TVLV (excluding the generic TVLV header) and the skb transport header
 * to the next byte after this multicast tracker TVLV.
 *
 * Caller needs to free the skb.
 *
 * Return: NET_RX_SUCCESS or NET_RX_DROP on success or a negative error
 * code on failure. NET_RX_SUCCESS if the received packet is supposed to be
 * decapsulated and forwarded to the own mesh interface, NET_RX_DROP otherwise.
 */
int batadv_mcast_forw_tracker_tvlv_handler(struct batadv_priv *bat_priv,
					   struct sk_buff *skb)
{
	return batadv_mcast_forw_packet(bat_priv, skb, false);
}

/**
 * batadv_mcast_forw_packet_hdrlen() - multicast packet header length
 * @num_dests: number of destination nodes
 *
 * Calculates the total batman-adv multicast packet header length for a given
 * number of destination nodes (excluding the outer ethernet frame).
 *
 * Return: The calculated total batman-adv multicast packet header length.
 */
unsigned int batadv_mcast_forw_packet_hdrlen(unsigned int num_dests)
{
	/**
	 * If the number of destination entries is even then we need to add
	 * two byte padding to the tracker TVLV.
	 */
	int padding = (!(num_dests % 2)) ? 2 : 0;

	return padding + num_dests * ETH_ALEN +
	       sizeof(struct batadv_tvlv_mcast_tracker) +
	       sizeof(struct batadv_tvlv_hdr) +
	       sizeof(struct batadv_mcast_packet);
}

/**
 * batadv_mcast_forw_expand_head() - expand headroom for an mcast packet
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the multicast packet to send
 *
 * Tries to expand an skb's headroom so that its head to tail is 1298
 * bytes (minimum IPv6 MTU + vlan ethernet header size) large.
 *
 * Return: -EINVAL if the given skb's length is too large or -ENOMEM on memory
 * allocation failure. Otherwise, on success, zero is returned.
 */
static int batadv_mcast_forw_expand_head(struct batadv_priv *bat_priv,
					 struct sk_buff *skb)
{
	int hdr_size = VLAN_ETH_HLEN + IPV6_MIN_MTU - skb->len;

	 /* TODO: Could be tightened to actual number of destination nodes?
	  * But it's tricky, number of destinations might have increased since
	  * we last checked.
	  */
	if (hdr_size < 0) {
		/* batadv_mcast_forw_mode_check_count() should ensure we do not
		 * end up here
		 */
		WARN_ON(1);
		return -EINVAL;
	}

	if (skb_headroom(skb) < hdr_size &&
	    pskb_expand_head(skb, hdr_size, 0, GFP_ATOMIC) < 0)
		return -ENOMEM;

	return 0;
}

/**
 * batadv_mcast_forw_push() - encapsulate skb in a batman-adv multicast packet
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the multicast packet to encapsulate and send
 * @vid: the vlan identifier
 * @is_routable: indicates whether the destination is routable
 * @count: the number of originators the multicast packet needs to be sent to
 *
 * Encapsulates the given multicast packet in a batman-adv multicast packet.
 * A multicast tracker TVLV with destination originator addresses for any node
 * that signaled interest in it, that is either via the translation table or the
 * according want-all flags, is attached accordingly.
 *
 * Return: true on success, false otherwise.
 */
bool batadv_mcast_forw_push(struct batadv_priv *bat_priv, struct sk_buff *skb,
			    unsigned short vid, int is_routable, int count)
{
	unsigned short tvlv_len = 0;
	int ret;

	if (batadv_mcast_forw_expand_head(bat_priv, skb) < 0)
		goto err;

	skb_reset_transport_header(skb);

	ret = batadv_mcast_forw_push_tvlvs(bat_priv, skb, vid, is_routable,
					   count, &tvlv_len);
	if (ret < 0)
		goto err;

	ret = batadv_mcast_forw_push_hdr(skb, tvlv_len);
	if (ret < 0)
		goto err;

	return true;

err:
	if (tvlv_len)
		skb_pull(skb, tvlv_len);

	return false;
}

/**
 * batadv_mcast_forw_mcsend() - send a self prepared batman-adv multicast packet
 * @bat_priv: the bat priv with all the mesh interface information
 * @skb: the multicast packet to encapsulate and send
 *
 * Transmits a batman-adv multicast packet that was locally prepared and
 * consumes/frees it.
 *
 * Return: NET_XMIT_DROP on memory allocation failure. NET_XMIT_SUCCESS
 * otherwise.
 */
int batadv_mcast_forw_mcsend(struct batadv_priv *bat_priv,
			     struct sk_buff *skb)
{
	int ret = batadv_mcast_forw_packet(bat_priv, skb, true);

	if (ret < 0) {
		kfree_skb(skb);
		return NET_XMIT_DROP;
	}

	consume_skb(skb);
	return NET_XMIT_SUCCESS;
}
