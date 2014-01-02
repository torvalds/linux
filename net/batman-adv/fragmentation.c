/* Copyright (C) 2013 B.A.T.M.A.N. contributors:
 *
 * Martin Hundebøll <martin@hundeboll.net>
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
#include "fragmentation.h"
#include "send.h"
#include "originator.h"
#include "routing.h"
#include "hard-interface.h"
#include "soft-interface.h"


/**
 * batadv_frag_clear_chain - delete entries in the fragment buffer chain
 * @head: head of chain with entries.
 *
 * Free fragments in the passed hlist. Should be called with appropriate lock.
 */
static void batadv_frag_clear_chain(struct hlist_head *head)
{
	struct batadv_frag_list_entry *entry;
	struct hlist_node *node;

	hlist_for_each_entry_safe(entry, node, head, list) {
		hlist_del(&entry->list);
		kfree_skb(entry->skb);
		kfree(entry);
	}
}

/**
 * batadv_frag_purge_orig - free fragments associated to an orig
 * @orig_node: originator to free fragments from
 * @check_cb: optional function to tell if an entry should be purged
 */
void batadv_frag_purge_orig(struct batadv_orig_node *orig_node,
			    bool (*check_cb)(struct batadv_frag_table_entry *))
{
	struct batadv_frag_table_entry *chain;
	uint8_t i;

	for (i = 0; i < BATADV_FRAG_BUFFER_COUNT; i++) {
		chain = &orig_node->fragments[i];
		spin_lock_bh(&orig_node->fragments[i].lock);

		if (!check_cb || check_cb(chain)) {
			batadv_frag_clear_chain(&orig_node->fragments[i].head);
			orig_node->fragments[i].size = 0;
		}

		spin_unlock_bh(&orig_node->fragments[i].lock);
	}
}

/**
 * batadv_frag_size_limit - maximum possible size of packet to be fragmented
 *
 * Returns the maximum size of payload that can be fragmented.
 */
static int batadv_frag_size_limit(void)
{
	int limit = BATADV_FRAG_MAX_FRAG_SIZE;

	limit -= sizeof(struct batadv_frag_packet);
	limit *= BATADV_FRAG_MAX_FRAGMENTS;

	return limit;
}

/**
 * batadv_frag_init_chain - check and prepare fragment chain for new fragment
 * @chain: chain in fragments table to init
 * @seqno: sequence number of the received fragment
 *
 * Make chain ready for a fragment with sequence number "seqno". Delete existing
 * entries if they have an "old" sequence number.
 *
 * Caller must hold chain->lock.
 *
 * Returns true if chain is empty and caller can just insert the new fragment
 * without searching for the right position.
 */
static bool batadv_frag_init_chain(struct batadv_frag_table_entry *chain,
				   uint16_t seqno)
{
	if (chain->seqno == seqno)
		return false;

	if (!hlist_empty(&chain->head))
		batadv_frag_clear_chain(&chain->head);

	chain->size = 0;
	chain->seqno = seqno;

	return true;
}

/**
 * batadv_frag_insert_packet - insert a fragment into a fragment chain
 * @orig_node: originator that the fragment was received from
 * @skb: skb to insert
 * @chain_out: list head to attach complete chains of fragments to
 *
 * Insert a new fragment into the reverse ordered chain in the right table
 * entry. The hash table entry is cleared if "old" fragments exist in it.
 *
 * Returns true if skb is buffered, false on error. If the chain has all the
 * fragments needed to merge the packet, the chain is moved to the passed head
 * to avoid locking the chain in the table.
 */
static bool batadv_frag_insert_packet(struct batadv_orig_node *orig_node,
				      struct sk_buff *skb,
				      struct hlist_head *chain_out)
{
	struct batadv_frag_table_entry *chain;
	struct batadv_frag_list_entry *frag_entry_new = NULL, *frag_entry_curr;
	struct batadv_frag_packet *frag_packet;
	uint8_t bucket;
	uint16_t seqno, hdr_size = sizeof(struct batadv_frag_packet);
	bool ret = false;

	/* Linearize packet to avoid linearizing 16 packets in a row when doing
	 * the later merge. Non-linear merge should be added to remove this
	 * linearization.
	 */
	if (skb_linearize(skb) < 0)
		goto err;

	frag_packet = (struct batadv_frag_packet *)skb->data;
	seqno = ntohs(frag_packet->seqno);
	bucket = seqno % BATADV_FRAG_BUFFER_COUNT;

	frag_entry_new = kmalloc(sizeof(*frag_entry_new), GFP_ATOMIC);
	if (!frag_entry_new)
		goto err;

	frag_entry_new->skb = skb;
	frag_entry_new->no = frag_packet->no;

	/* Select entry in the "chain table" and delete any prior fragments
	 * with another sequence number. batadv_frag_init_chain() returns true,
	 * if the list is empty at return.
	 */
	chain = &orig_node->fragments[bucket];
	spin_lock_bh(&chain->lock);
	if (batadv_frag_init_chain(chain, seqno)) {
		hlist_add_head(&frag_entry_new->list, &chain->head);
		chain->size = skb->len - hdr_size;
		chain->timestamp = jiffies;
		ret = true;
		goto out;
	}

	/* Find the position for the new fragment. */
	hlist_for_each_entry(frag_entry_curr, &chain->head, list) {
		/* Drop packet if fragment already exists. */
		if (frag_entry_curr->no == frag_entry_new->no)
			goto err_unlock;

		/* Order fragments from highest to lowest. */
		if (frag_entry_curr->no < frag_entry_new->no) {
			hlist_add_before(&frag_entry_new->list,
					 &frag_entry_curr->list);
			chain->size += skb->len - hdr_size;
			chain->timestamp = jiffies;
			ret = true;
			goto out;
		}
	}

	/* Reached the end of the list, so insert after 'frag_entry_curr'. */
	if (likely(frag_entry_curr)) {
		hlist_add_after(&frag_entry_curr->list, &frag_entry_new->list);
		chain->size += skb->len - hdr_size;
		chain->timestamp = jiffies;
		ret = true;
	}

out:
	if (chain->size > batadv_frag_size_limit() ||
	    ntohs(frag_packet->total_size) > batadv_frag_size_limit()) {
		/* Clear chain if total size of either the list or the packet
		 * exceeds the maximum size of one merged packet.
		 */
		batadv_frag_clear_chain(&chain->head);
		chain->size = 0;
	} else if (ntohs(frag_packet->total_size) == chain->size) {
		/* All fragments received. Hand over chain to caller. */
		hlist_move_list(&chain->head, chain_out);
		chain->size = 0;
	}

err_unlock:
	spin_unlock_bh(&chain->lock);

err:
	if (!ret)
		kfree(frag_entry_new);

	return ret;
}

/**
 * batadv_frag_merge_packets - merge a chain of fragments
 * @chain: head of chain with fragments
 * @skb: packet with total size of skb after merging
 *
 * Expand the first skb in the chain and copy the content of the remaining
 * skb's into the expanded one. After doing so, clear the chain.
 *
 * Returns the merged skb or NULL on error.
 */
static struct sk_buff *
batadv_frag_merge_packets(struct hlist_head *chain, struct sk_buff *skb)
{
	struct batadv_frag_packet *packet;
	struct batadv_frag_list_entry *entry;
	struct sk_buff *skb_out = NULL;
	int size, hdr_size = sizeof(struct batadv_frag_packet);

	/* Make sure incoming skb has non-bogus data. */
	packet = (struct batadv_frag_packet *)skb->data;
	size = ntohs(packet->total_size);
	if (size > batadv_frag_size_limit())
		goto free;

	/* Remove first entry, as this is the destination for the rest of the
	 * fragments.
	 */
	entry = hlist_entry(chain->first, struct batadv_frag_list_entry, list);
	hlist_del(&entry->list);
	skb_out = entry->skb;
	kfree(entry);

	/* Make room for the rest of the fragments. */
	if (pskb_expand_head(skb_out, 0, size - skb->len, GFP_ATOMIC) < 0) {
		kfree_skb(skb_out);
		skb_out = NULL;
		goto free;
	}

	/* Move the existing MAC header to just before the payload. (Override
	 * the fragment header.)
	 */
	skb_pull_rcsum(skb_out, hdr_size);
	memmove(skb_out->data - ETH_HLEN, skb_mac_header(skb_out), ETH_HLEN);
	skb_set_mac_header(skb_out, -ETH_HLEN);
	skb_reset_network_header(skb_out);
	skb_reset_transport_header(skb_out);

	/* Copy the payload of the each fragment into the last skb */
	hlist_for_each_entry(entry, chain, list) {
		size = entry->skb->len - hdr_size;
		memcpy(skb_put(skb_out, size), entry->skb->data + hdr_size,
		       size);
	}

free:
	/* Locking is not needed, because 'chain' is not part of any orig. */
	batadv_frag_clear_chain(chain);
	return skb_out;
}

/**
 * batadv_frag_skb_buffer - buffer fragment for later merge
 * @skb: skb to buffer
 * @orig_node_src: originator that the skb is received from
 *
 * Add fragment to buffer and merge fragments if possible.
 *
 * There are three possible outcomes: 1) Packet is merged: Return true and
 * set *skb to merged packet; 2) Packet is buffered: Return true and set *skb
 * to NULL; 3) Error: Return false and leave skb as is.
 */
bool batadv_frag_skb_buffer(struct sk_buff **skb,
			    struct batadv_orig_node *orig_node_src)
{
	struct sk_buff *skb_out = NULL;
	struct hlist_head head = HLIST_HEAD_INIT;
	bool ret = false;

	/* Add packet to buffer and table entry if merge is possible. */
	if (!batadv_frag_insert_packet(orig_node_src, *skb, &head))
		goto out_err;

	/* Leave if more fragments are needed to merge. */
	if (hlist_empty(&head))
		goto out;

	skb_out = batadv_frag_merge_packets(&head, *skb);
	if (!skb_out)
		goto out_err;

out:
	*skb = skb_out;
	ret = true;
out_err:
	return ret;
}

/**
 * batadv_frag_skb_fwd - forward fragments that would exceed MTU when merged
 * @skb: skb to forward
 * @recv_if: interface that the skb is received on
 * @orig_node_src: originator that the skb is received from
 *
 * Look up the next-hop of the fragments payload and check if the merged packet
 * will exceed the MTU towards the next-hop. If so, the fragment is forwarded
 * without merging it.
 *
 * Returns true if the fragment is consumed/forwarded, false otherwise.
 */
bool batadv_frag_skb_fwd(struct sk_buff *skb,
			 struct batadv_hard_iface *recv_if,
			 struct batadv_orig_node *orig_node_src)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_orig_node *orig_node_dst = NULL;
	struct batadv_neigh_node *neigh_node = NULL;
	struct batadv_frag_packet *packet;
	uint16_t total_size;
	bool ret = false;

	packet = (struct batadv_frag_packet *)skb->data;
	orig_node_dst = batadv_orig_hash_find(bat_priv, packet->dest);
	if (!orig_node_dst)
		goto out;

	neigh_node = batadv_find_router(bat_priv, orig_node_dst, recv_if);
	if (!neigh_node)
		goto out;

	/* Forward the fragment, if the merged packet would be too big to
	 * be assembled.
	 */
	total_size = ntohs(packet->total_size);
	if (total_size > neigh_node->if_incoming->net_dev->mtu) {
		batadv_inc_counter(bat_priv, BATADV_CNT_FRAG_FWD);
		batadv_add_counter(bat_priv, BATADV_CNT_FRAG_FWD_BYTES,
				   skb->len + ETH_HLEN);

		packet->header.ttl--;
		batadv_send_skb_packet(skb, neigh_node->if_incoming,
				       neigh_node->addr);
		ret = true;
	}

out:
	if (orig_node_dst)
		batadv_orig_node_free_ref(orig_node_dst);
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	return ret;
}

/**
 * batadv_frag_create - create a fragment from skb
 * @skb: skb to create fragment from
 * @frag_head: header to use in new fragment
 * @mtu: size of new fragment
 *
 * Split the passed skb into two fragments: A new one with size matching the
 * passed mtu and the old one with the rest. The new skb contains data from the
 * tail of the old skb.
 *
 * Returns the new fragment, NULL on error.
 */
static struct sk_buff *batadv_frag_create(struct sk_buff *skb,
					  struct batadv_frag_packet *frag_head,
					  unsigned int mtu)
{
	struct sk_buff *skb_fragment;
	unsigned header_size = sizeof(*frag_head);
	unsigned fragment_size = mtu - header_size;

	skb_fragment = netdev_alloc_skb(NULL, mtu + ETH_HLEN);
	if (!skb_fragment)
		goto err;

	skb->priority = TC_PRIO_CONTROL;

	/* Eat the last mtu-bytes of the skb */
	skb_reserve(skb_fragment, header_size + ETH_HLEN);
	skb_split(skb, skb_fragment, skb->len - fragment_size);

	/* Add the header */
	skb_push(skb_fragment, header_size);
	memcpy(skb_fragment->data, frag_head, header_size);

err:
	return skb_fragment;
}

/**
 * batadv_frag_send_packet - create up to 16 fragments from the passed skb
 * @skb: skb to create fragments from
 * @orig_node: final destination of the created fragments
 * @neigh_node: next-hop of the created fragments
 *
 * Returns true on success, false otherwise.
 */
bool batadv_frag_send_packet(struct sk_buff *skb,
			     struct batadv_orig_node *orig_node,
			     struct batadv_neigh_node *neigh_node)
{
	struct batadv_priv *bat_priv;
	struct batadv_hard_iface *primary_if;
	struct batadv_frag_packet frag_header;
	struct sk_buff *skb_fragment;
	unsigned mtu = neigh_node->if_incoming->net_dev->mtu;
	unsigned header_size = sizeof(frag_header);
	unsigned max_fragment_size, max_packet_size;

	/* To avoid merge and refragmentation at next-hops we never send
	 * fragments larger than BATADV_FRAG_MAX_FRAG_SIZE
	 */
	mtu = min_t(unsigned, mtu, BATADV_FRAG_MAX_FRAG_SIZE);
	max_fragment_size = (mtu - header_size - ETH_HLEN);
	max_packet_size = max_fragment_size * BATADV_FRAG_MAX_FRAGMENTS;

	/* Don't even try to fragment, if we need more than 16 fragments */
	if (skb->len > max_packet_size)
		goto out_err;

	bat_priv = orig_node->bat_priv;
	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out_err;

	/* Create one header to be copied to all fragments */
	frag_header.header.packet_type = BATADV_UNICAST_FRAG;
	frag_header.header.version = BATADV_COMPAT_VERSION;
	frag_header.header.ttl = BATADV_TTL;
	frag_header.seqno = htons(atomic_inc_return(&bat_priv->frag_seqno));
	frag_header.reserved = 0;
	frag_header.no = 0;
	frag_header.total_size = htons(skb->len);
	memcpy(frag_header.orig, primary_if->net_dev->dev_addr, ETH_ALEN);
	memcpy(frag_header.dest, orig_node->orig, ETH_ALEN);

	/* Eat and send fragments from the tail of skb */
	while (skb->len > max_fragment_size) {
		skb_fragment = batadv_frag_create(skb, &frag_header, mtu);
		if (!skb_fragment)
			goto out_err;

		batadv_inc_counter(bat_priv, BATADV_CNT_FRAG_TX);
		batadv_add_counter(bat_priv, BATADV_CNT_FRAG_TX_BYTES,
				   skb_fragment->len + ETH_HLEN);
		batadv_send_skb_packet(skb_fragment, neigh_node->if_incoming,
				       neigh_node->addr);
		frag_header.no++;

		/* The initial check in this function should cover this case */
		if (frag_header.no == BATADV_FRAG_MAX_FRAGMENTS - 1)
			goto out_err;
	}

	/* Make room for the fragment header. */
	if (batadv_skb_head_push(skb, header_size) < 0 ||
	    pskb_expand_head(skb, header_size + ETH_HLEN, 0, GFP_ATOMIC) < 0)
		goto out_err;

	memcpy(skb->data, &frag_header, header_size);

	/* Send the last fragment */
	batadv_inc_counter(bat_priv, BATADV_CNT_FRAG_TX);
	batadv_add_counter(bat_priv, BATADV_CNT_FRAG_TX_BYTES,
			   skb->len + ETH_HLEN);
	batadv_send_skb_packet(skb, neigh_node->if_incoming, neigh_node->addr);

	return true;
out_err:
	return false;
}
