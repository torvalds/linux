/* Copyright (C) 2013-2016  B.A.T.M.A.N. contributors:
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "fragmentation.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/byteorder/generic.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include <linux/if_ether.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include "hard-interface.h"
#include "originator.h"
#include "packet.h"
#include "routing.h"
#include "send.h"
#include "soft-interface.h"

/**
 * batadv_frag_clear_chain - delete entries in the fragment buffer chain
 * @head: head of chain with entries.
 * @dropped: whether the chain is cleared because all fragments are dropped
 *
 * Free fragments in the passed hlist. Should be called with appropriate lock.
 */
static void batadv_frag_clear_chain(struct hlist_head *head, bool dropped)
{
	struct batadv_frag_list_entry *entry;
	struct hlist_node *node;

	hlist_for_each_entry_safe(entry, node, head, list) {
		hlist_del(&entry->list);

		if (dropped)
			kfree_skb(entry->skb);
		else
			consume_skb(entry->skb);

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
	u8 i;

	for (i = 0; i < BATADV_FRAG_BUFFER_COUNT; i++) {
		chain = &orig_node->fragments[i];
		spin_lock_bh(&chain->lock);

		if (!check_cb || check_cb(chain)) {
			batadv_frag_clear_chain(&chain->fragment_list, true);
			chain->size = 0;
		}

		spin_unlock_bh(&chain->lock);
	}
}

/**
 * batadv_frag_size_limit - maximum possible size of packet to be fragmented
 *
 * Return: the maximum size of payload that can be fragmented.
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
 * Return: true if chain is empty and caller can just insert the new fragment
 * without searching for the right position.
 */
static bool batadv_frag_init_chain(struct batadv_frag_table_entry *chain,
				   u16 seqno)
{
	lockdep_assert_held(&chain->lock);

	if (chain->seqno == seqno)
		return false;

	if (!hlist_empty(&chain->fragment_list))
		batadv_frag_clear_chain(&chain->fragment_list, true);

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
 * Return: true if skb is buffered, false on error. If the chain has all the
 * fragments needed to merge the packet, the chain is moved to the passed head
 * to avoid locking the chain in the table.
 */
static bool batadv_frag_insert_packet(struct batadv_orig_node *orig_node,
				      struct sk_buff *skb,
				      struct hlist_head *chain_out)
{
	struct batadv_frag_table_entry *chain;
	struct batadv_frag_list_entry *frag_entry_new = NULL, *frag_entry_curr;
	struct batadv_frag_list_entry *frag_entry_last = NULL;
	struct batadv_frag_packet *frag_packet;
	u8 bucket;
	u16 seqno, hdr_size = sizeof(struct batadv_frag_packet);
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
		hlist_add_head(&frag_entry_new->list, &chain->fragment_list);
		chain->size = skb->len - hdr_size;
		chain->timestamp = jiffies;
		chain->total_size = ntohs(frag_packet->total_size);
		ret = true;
		goto out;
	}

	/* Find the position for the new fragment. */
	hlist_for_each_entry(frag_entry_curr, &chain->fragment_list, list) {
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

		/* store current entry because it could be the last in list */
		frag_entry_last = frag_entry_curr;
	}

	/* Reached the end of the list, so insert after 'frag_entry_last'. */
	if (likely(frag_entry_last)) {
		hlist_add_behind(&frag_entry_new->list, &frag_entry_last->list);
		chain->size += skb->len - hdr_size;
		chain->timestamp = jiffies;
		ret = true;
	}

out:
	if (chain->size > batadv_frag_size_limit() ||
	    chain->total_size != ntohs(frag_packet->total_size) ||
	    chain->total_size > batadv_frag_size_limit()) {
		/* Clear chain if total size of either the list or the packet
		 * exceeds the maximum size of one merged packet. Don't allow
		 * packets to have different total_size.
		 */
		batadv_frag_clear_chain(&chain->fragment_list, true);
		chain->size = 0;
	} else if (ntohs(frag_packet->total_size) == chain->size) {
		/* All fragments received. Hand over chain to caller. */
		hlist_move_list(&chain->fragment_list, chain_out);
		chain->size = 0;
	}

err_unlock:
	spin_unlock_bh(&chain->lock);

err:
	if (!ret) {
		kfree(frag_entry_new);
		kfree_skb(skb);
	}

	return ret;
}

/**
 * batadv_frag_merge_packets - merge a chain of fragments
 * @chain: head of chain with fragments
 *
 * Expand the first skb in the chain and copy the content of the remaining
 * skb's into the expanded one. After doing so, clear the chain.
 *
 * Return: the merged skb or NULL on error.
 */
static struct sk_buff *
batadv_frag_merge_packets(struct hlist_head *chain)
{
	struct batadv_frag_packet *packet;
	struct batadv_frag_list_entry *entry;
	struct sk_buff *skb_out;
	int size, hdr_size = sizeof(struct batadv_frag_packet);
	bool dropped = false;

	/* Remove first entry, as this is the destination for the rest of the
	 * fragments.
	 */
	entry = hlist_entry(chain->first, struct batadv_frag_list_entry, list);
	hlist_del(&entry->list);
	skb_out = entry->skb;
	kfree(entry);

	packet = (struct batadv_frag_packet *)skb_out->data;
	size = ntohs(packet->total_size);

	/* Make room for the rest of the fragments. */
	if (pskb_expand_head(skb_out, 0, size - skb_out->len, GFP_ATOMIC) < 0) {
		kfree_skb(skb_out);
		skb_out = NULL;
		dropped = true;
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
	batadv_frag_clear_chain(chain, dropped);
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
 * to NULL; 3) Error: Return false and free skb.
 *
 * Return: true when packet is merged or buffered, false when skb is not not
 * used.
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

	skb_out = batadv_frag_merge_packets(&head);
	if (!skb_out)
		goto out_err;

out:
	ret = true;
out_err:
	*skb = skb_out;
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
 * Return: true if the fragment is consumed/forwarded, false otherwise.
 */
bool batadv_frag_skb_fwd(struct sk_buff *skb,
			 struct batadv_hard_iface *recv_if,
			 struct batadv_orig_node *orig_node_src)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_orig_node *orig_node_dst;
	struct batadv_neigh_node *neigh_node = NULL;
	struct batadv_frag_packet *packet;
	u16 total_size;
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

		packet->ttl--;
		batadv_send_unicast_skb(skb, neigh_node);
		ret = true;
	}

out:
	if (orig_node_dst)
		batadv_orig_node_put(orig_node_dst);
	if (neigh_node)
		batadv_neigh_node_put(neigh_node);
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
 * Return: the new fragment, NULL on error.
 */
static struct sk_buff *batadv_frag_create(struct sk_buff *skb,
					  struct batadv_frag_packet *frag_head,
					  unsigned int mtu)
{
	struct sk_buff *skb_fragment;
	unsigned int header_size = sizeof(*frag_head);
	unsigned int fragment_size = mtu - header_size;

	skb_fragment = netdev_alloc_skb(NULL, mtu + ETH_HLEN);
	if (!skb_fragment)
		goto err;

	skb_fragment->priority = skb->priority;

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
 * Return: the netdev tx status or a negative errno code on a failure
 */
int batadv_frag_send_packet(struct sk_buff *skb,
			    struct batadv_orig_node *orig_node,
			    struct batadv_neigh_node *neigh_node)
{
	struct batadv_priv *bat_priv;
	struct batadv_hard_iface *primary_if = NULL;
	struct batadv_frag_packet frag_header;
	struct sk_buff *skb_fragment;
	unsigned int mtu = neigh_node->if_incoming->net_dev->mtu;
	unsigned int header_size = sizeof(frag_header);
	unsigned int max_fragment_size, max_packet_size;
	int ret;

	/* To avoid merge and refragmentation at next-hops we never send
	 * fragments larger than BATADV_FRAG_MAX_FRAG_SIZE
	 */
	mtu = min_t(unsigned int, mtu, BATADV_FRAG_MAX_FRAG_SIZE);
	max_fragment_size = mtu - header_size;
	max_packet_size = max_fragment_size * BATADV_FRAG_MAX_FRAGMENTS;

	/* Don't even try to fragment, if we need more than 16 fragments */
	if (skb->len > max_packet_size) {
		ret = -EAGAIN;
		goto free_skb;
	}

	bat_priv = orig_node->bat_priv;
	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if) {
		ret = -EINVAL;
		goto free_skb;
	}

	/* Create one header to be copied to all fragments */
	frag_header.packet_type = BATADV_UNICAST_FRAG;
	frag_header.version = BATADV_COMPAT_VERSION;
	frag_header.ttl = BATADV_TTL;
	frag_header.seqno = htons(atomic_inc_return(&bat_priv->frag_seqno));
	frag_header.reserved = 0;
	frag_header.no = 0;
	frag_header.total_size = htons(skb->len);

	/* skb->priority values from 256->263 are magic values to
	 * directly indicate a specific 802.1d priority.  This is used
	 * to allow 802.1d priority to be passed directly in from VLAN
	 * tags, etc.
	 */
	if (skb->priority >= 256 && skb->priority <= 263)
		frag_header.priority = skb->priority - 256;

	ether_addr_copy(frag_header.orig, primary_if->net_dev->dev_addr);
	ether_addr_copy(frag_header.dest, orig_node->orig);

	/* Eat and send fragments from the tail of skb */
	while (skb->len > max_fragment_size) {
		skb_fragment = batadv_frag_create(skb, &frag_header, mtu);
		if (!skb_fragment) {
			ret = -ENOMEM;
			goto put_primary_if;
		}

		batadv_inc_counter(bat_priv, BATADV_CNT_FRAG_TX);
		batadv_add_counter(bat_priv, BATADV_CNT_FRAG_TX_BYTES,
				   skb_fragment->len + ETH_HLEN);
		ret = batadv_send_unicast_skb(skb_fragment, neigh_node);
		if (ret != NET_XMIT_SUCCESS) {
			ret = NET_XMIT_DROP;
			goto put_primary_if;
		}

		frag_header.no++;

		/* The initial check in this function should cover this case */
		if (frag_header.no == BATADV_FRAG_MAX_FRAGMENTS - 1) {
			ret = -EINVAL;
			goto put_primary_if;
		}
	}

	/* Make room for the fragment header. */
	if (batadv_skb_head_push(skb, header_size) < 0 ||
	    pskb_expand_head(skb, header_size + ETH_HLEN, 0, GFP_ATOMIC) < 0) {
		ret = -ENOMEM;
		goto put_primary_if;
	}

	memcpy(skb->data, &frag_header, header_size);

	/* Send the last fragment */
	batadv_inc_counter(bat_priv, BATADV_CNT_FRAG_TX);
	batadv_add_counter(bat_priv, BATADV_CNT_FRAG_TX_BYTES,
			   skb->len + ETH_HLEN);
	ret = batadv_send_unicast_skb(skb, neigh_node);
	/* skb was consumed */
	skb = NULL;

put_primary_if:
	batadv_hardif_put(primary_if);
free_skb:
	kfree_skb(skb);

	return ret;
}
