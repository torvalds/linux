/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 *
 * The HSR spec says never to forward the same frame twice on the same
 * interface. A frame is identified by its source MAC address and its HSR
 * sequence number. This code keeps track of senders and their sequence numbers
 * to allow filtering of duplicate frames, and to detect HSR ring errors.
 */

#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include "hsr_main.h"
#include "hsr_framereg.h"
#include "hsr_netlink.h"


struct hsr_node {
	struct list_head	mac_list;
	unsigned char		MacAddressA[ETH_ALEN];
	unsigned char		MacAddressB[ETH_ALEN];
	enum hsr_dev_idx	AddrB_if;/* The local slave through which AddrB
					  * frames are received from this node
					  */
	unsigned long		time_in[HSR_MAX_SLAVE];
	bool			time_in_stale[HSR_MAX_SLAVE];
	u16			seq_out[HSR_MAX_DEV];
	struct rcu_head		rcu_head;
};

/*	TODO: use hash lists for mac addresses (linux/jhash.h)?    */



/* Search for mac entry. Caller must hold rcu read lock.
 */
static struct hsr_node *find_node_by_AddrA(struct list_head *node_db,
					   const unsigned char addr[ETH_ALEN])
{
	struct hsr_node *node;

	list_for_each_entry_rcu(node, node_db, mac_list) {
		if (ether_addr_equal(node->MacAddressA, addr))
			return node;
	}

	return NULL;
}


/* Search for mac entry. Caller must hold rcu read lock.
 */
static struct hsr_node *find_node_by_AddrB(struct list_head *node_db,
					   const unsigned char addr[ETH_ALEN])
{
	struct hsr_node *node;

	list_for_each_entry_rcu(node, node_db, mac_list) {
		if (ether_addr_equal(node->MacAddressB, addr))
			return node;
	}

	return NULL;
}


/* Search for mac entry. Caller must hold rcu read lock.
 */
struct hsr_node *hsr_find_node(struct list_head *node_db, struct sk_buff *skb)
{
	struct hsr_node *node;
	struct ethhdr *ethhdr;

	if (!skb_mac_header_was_set(skb))
		return NULL;

	ethhdr = (struct ethhdr *) skb_mac_header(skb);

	list_for_each_entry_rcu(node, node_db, mac_list) {
		if (ether_addr_equal(node->MacAddressA, ethhdr->h_source))
			return node;
		if (ether_addr_equal(node->MacAddressB, ethhdr->h_source))
			return node;
	}

	return NULL;
}


/* Helper for device init; the self_node_db is used in hsr_rcv() to recognize
 * frames from self that's been looped over the HSR ring.
 */
int hsr_create_self_node(struct list_head *self_node_db,
			 unsigned char addr_a[ETH_ALEN],
			 unsigned char addr_b[ETH_ALEN])
{
	struct hsr_node *node, *oldnode;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	ether_addr_copy(node->MacAddressA, addr_a);
	ether_addr_copy(node->MacAddressB, addr_b);

	rcu_read_lock();
	oldnode = list_first_or_null_rcu(self_node_db,
						struct hsr_node, mac_list);
	if (oldnode) {
		list_replace_rcu(&oldnode->mac_list, &node->mac_list);
		rcu_read_unlock();
		synchronize_rcu();
		kfree(oldnode);
	} else {
		rcu_read_unlock();
		list_add_tail_rcu(&node->mac_list, self_node_db);
	}

	return 0;
}


/* Add/merge node to the database of nodes. 'skb' must contain an HSR
 * supervision frame.
 * - If the supervision header's MacAddressA field is not yet in the database,
 * this frame is from an hitherto unknown node - add it to the database.
 * - If the sender's MAC address is not the same as its MacAddressA address,
 * the node is using PICS_SUBS (address substitution). Record the sender's
 * address as the node's MacAddressB.
 *
 * This function needs to work even if the sender node has changed one of its
 * slaves' MAC addresses. In this case, there are four different cases described
 * by (Addr-changed, received-from) pairs as follows. Note that changing the
 * SlaveA address is equal to changing the node's own address:
 *
 * - (AddrB, SlaveB): The new AddrB will be recorded by PICS_SUBS code since
 *		      node == NULL.
 * - (AddrB, SlaveA): Will work as usual (the AddrB change won't be detected
 *		      from this frame).
 *
 * - (AddrA, SlaveB): The old node will be found. We need to detect this and
 *		      remove the node.
 * - (AddrA, SlaveA): A new node will be registered (non-PICS_SUBS at first).
 *		      The old one will be pruned after HSR_NODE_FORGET_TIME.
 *
 * We also need to detect if the sender's SlaveA and SlaveB cables have been
 * swapped.
 */
struct hsr_node *hsr_merge_node(struct hsr_priv *hsr,
				struct hsr_node *node,
				struct sk_buff *skb,
				enum hsr_dev_idx dev_idx)
{
	struct hsr_sup_payload *hsr_sp;
	struct hsr_ethhdr_sp *hsr_ethsup;
	int i;
	unsigned long now;

	hsr_ethsup = (struct hsr_ethhdr_sp *) skb_mac_header(skb);
	hsr_sp = (struct hsr_sup_payload *) skb->data;

	if (node && !ether_addr_equal(node->MacAddressA, hsr_sp->MacAddressA)) {
		/* Node has changed its AddrA, frame was received from SlaveB */
		list_del_rcu(&node->mac_list);
		kfree_rcu(node, rcu_head);
		node = NULL;
	}

	if (node && (dev_idx == node->AddrB_if) &&
	    !ether_addr_equal(node->MacAddressB, hsr_ethsup->ethhdr.h_source)) {
		/* Cables have been swapped */
		list_del_rcu(&node->mac_list);
		kfree_rcu(node, rcu_head);
		node = NULL;
	}

	if (node && (dev_idx != node->AddrB_if) &&
	    (node->AddrB_if != HSR_DEV_NONE) &&
	    !ether_addr_equal(node->MacAddressA, hsr_ethsup->ethhdr.h_source)) {
		/* Cables have been swapped */
		list_del_rcu(&node->mac_list);
		kfree_rcu(node, rcu_head);
		node = NULL;
	}

	if (node)
		return node;

	node = find_node_by_AddrA(&hsr->node_db, hsr_sp->MacAddressA);
	if (node) {
		/* Node is known, but frame was received from an unknown
		 * address. Node is PICS_SUBS capable; merge its AddrB.
		 */
		ether_addr_copy(node->MacAddressB, hsr_ethsup->ethhdr.h_source);
		node->AddrB_if = dev_idx;
		return node;
	}

	node = kzalloc(sizeof(*node), GFP_ATOMIC);
	if (!node)
		return NULL;

	ether_addr_copy(node->MacAddressA, hsr_sp->MacAddressA);
	ether_addr_copy(node->MacAddressB, hsr_ethsup->ethhdr.h_source);
	if (!ether_addr_equal(hsr_sp->MacAddressA, hsr_ethsup->ethhdr.h_source))
		node->AddrB_if = dev_idx;
	else
		node->AddrB_if = HSR_DEV_NONE;

	/* We are only interested in time diffs here, so use current jiffies
	 * as initialization. (0 could trigger an spurious ring error warning).
	 */
	now = jiffies;
	for (i = 0; i < HSR_MAX_SLAVE; i++)
		node->time_in[i] = now;
	for (i = 0; i < HSR_MAX_DEV; i++)
		node->seq_out[i] = ntohs(hsr_ethsup->hsr_sup.sequence_nr) - 1;

	list_add_tail_rcu(&node->mac_list, &hsr->node_db);

	return node;
}


/* 'skb' is a frame meant for this host, that is to be passed to upper layers.
 *
 * If the frame was sent by a node's B interface, replace the sender
 * address with that node's "official" address (MacAddressA) so that upper
 * layers recognize where it came from.
 */
void hsr_addr_subst_source(struct hsr_priv *hsr, struct sk_buff *skb)
{
	struct ethhdr *ethhdr;
	struct hsr_node *node;

	if (!skb_mac_header_was_set(skb)) {
		WARN_ONCE(1, "%s: Mac header not set\n", __func__);
		return;
	}
	ethhdr = (struct ethhdr *) skb_mac_header(skb);

	rcu_read_lock();
	node = find_node_by_AddrB(&hsr->node_db, ethhdr->h_source);
	if (node)
		ether_addr_copy(ethhdr->h_source, node->MacAddressA);
	rcu_read_unlock();
}


/* 'skb' is a frame meant for another host.
 * 'hsr_dev_idx' is the HSR index of the outgoing device
 *
 * Substitute the target (dest) MAC address if necessary, so the it matches the
 * recipient interface MAC address, regardless of whether that is the
 * recipient's A or B interface.
 * This is needed to keep the packets flowing through switches that learn on
 * which "side" the different interfaces are.
 */
void hsr_addr_subst_dest(struct hsr_priv *hsr, struct ethhdr *ethhdr,
			 enum hsr_dev_idx dev_idx)
{
	struct hsr_node *node;

	rcu_read_lock();
	node = find_node_by_AddrA(&hsr->node_db, ethhdr->h_dest);
	if (node && (node->AddrB_if == dev_idx))
		ether_addr_copy(ethhdr->h_dest, node->MacAddressB);
	rcu_read_unlock();
}


/* seq_nr_after(a, b) - return true if a is after (higher in sequence than) b,
 * false otherwise.
 */
static bool seq_nr_after(u16 a, u16 b)
{
	/* Remove inconsistency where
	 * seq_nr_after(a, b) == seq_nr_before(a, b)
	 */
	if ((int) b - a == 32768)
		return false;

	return (((s16) (b - a)) < 0);
}
#define seq_nr_before(a, b)		seq_nr_after((b), (a))
#define seq_nr_after_or_eq(a, b)	(!seq_nr_before((a), (b)))
#define seq_nr_before_or_eq(a, b)	(!seq_nr_after((a), (b)))


void hsr_register_frame_in(struct hsr_node *node, enum hsr_dev_idx dev_idx)
{
	if ((dev_idx < 0) || (dev_idx >= HSR_MAX_SLAVE)) {
		WARN_ONCE(1, "%s: Invalid dev_idx (%d)\n", __func__, dev_idx);
		return;
	}
	node->time_in[dev_idx] = jiffies;
	node->time_in_stale[dev_idx] = false;
}


/* 'skb' is a HSR Ethernet frame (with a HSR tag inserted), with a valid
 * ethhdr->h_source address and skb->mac_header set.
 *
 * Return:
 *	 1 if frame can be shown to have been sent recently on this interface,
 *	 0 otherwise, or
 *	 negative error code on error
 */
int hsr_register_frame_out(struct hsr_node *node, enum hsr_dev_idx dev_idx,
			   struct sk_buff *skb)
{
	struct hsr_ethhdr *hsr_ethhdr;
	u16 sequence_nr;

	if ((dev_idx < 0) || (dev_idx >= HSR_MAX_DEV)) {
		WARN_ONCE(1, "%s: Invalid dev_idx (%d)\n", __func__, dev_idx);
		return -EINVAL;
	}
	if (!skb_mac_header_was_set(skb)) {
		WARN_ONCE(1, "%s: Mac header not set\n", __func__);
		return -EINVAL;
	}
	hsr_ethhdr = (struct hsr_ethhdr *) skb_mac_header(skb);

	sequence_nr = ntohs(hsr_ethhdr->hsr_tag.sequence_nr);
	if (seq_nr_before_or_eq(sequence_nr, node->seq_out[dev_idx]))
		return 1;

	node->seq_out[dev_idx] = sequence_nr;
	return 0;
}



static bool is_late(struct hsr_node *node, enum hsr_dev_idx dev_idx)
{
	enum hsr_dev_idx other;

	if (node->time_in_stale[dev_idx])
		return true;

	if (dev_idx == HSR_DEV_SLAVE_A)
		other = HSR_DEV_SLAVE_B;
	else
		other = HSR_DEV_SLAVE_A;

	if (node->time_in_stale[other])
		return false;

	if (time_after(node->time_in[other], node->time_in[dev_idx] +
		       msecs_to_jiffies(MAX_SLAVE_DIFF)))
		return true;

	return false;
}


/* Remove stale sequence_nr records. Called by timer every
 * HSR_LIFE_CHECK_INTERVAL (two seconds or so).
 */
void hsr_prune_nodes(unsigned long data)
{
	struct hsr_priv *hsr;
	struct hsr_node *node;
	unsigned long timestamp;
	unsigned long time_a, time_b;

	hsr = (struct hsr_priv *) data;

	rcu_read_lock();
	list_for_each_entry_rcu(node, &hsr->node_db, mac_list) {
		/* Shorthand */
		time_a = node->time_in[HSR_DEV_SLAVE_A];
		time_b = node->time_in[HSR_DEV_SLAVE_B];

		/* Check for timestamps old enough to risk wrap-around */
		if (time_after(jiffies, time_a + MAX_JIFFY_OFFSET/2))
			node->time_in_stale[HSR_DEV_SLAVE_A] = true;
		if (time_after(jiffies, time_b + MAX_JIFFY_OFFSET/2))
			node->time_in_stale[HSR_DEV_SLAVE_B] = true;

		/* Get age of newest frame from node.
		 * At least one time_in is OK here; nodes get pruned long
		 * before both time_ins can get stale
		 */
		timestamp = time_a;
		if (node->time_in_stale[HSR_DEV_SLAVE_A] ||
		    (!node->time_in_stale[HSR_DEV_SLAVE_B] &&
		    time_after(time_b, time_a)))
			timestamp = time_b;

		/* Warn of ring error only as long as we get frames at all */
		if (time_is_after_jiffies(timestamp +
					msecs_to_jiffies(1.5*MAX_SLAVE_DIFF))) {

			if (is_late(node, HSR_DEV_SLAVE_A))
				hsr_nl_ringerror(hsr, node->MacAddressA,
						 HSR_DEV_SLAVE_A);
			else if (is_late(node, HSR_DEV_SLAVE_B))
				hsr_nl_ringerror(hsr, node->MacAddressA,
						 HSR_DEV_SLAVE_B);
		}

		/* Prune old entries */
		if (time_is_before_jiffies(timestamp +
					msecs_to_jiffies(HSR_NODE_FORGET_TIME))) {
			hsr_nl_nodedown(hsr, node->MacAddressA);
			list_del_rcu(&node->mac_list);
			/* Note that we need to free this entry later: */
			kfree_rcu(node, rcu_head);
		}
	}
	rcu_read_unlock();
}


void *hsr_get_next_node(struct hsr_priv *hsr, void *_pos,
			unsigned char addr[ETH_ALEN])
{
	struct hsr_node *node;

	if (!_pos) {
		node = list_first_or_null_rcu(&hsr->node_db,
					      struct hsr_node, mac_list);
		if (node)
			ether_addr_copy(addr, node->MacAddressA);
		return node;
	}

	node = _pos;
	list_for_each_entry_continue_rcu(node, &hsr->node_db, mac_list) {
		ether_addr_copy(addr, node->MacAddressA);
		return node;
	}

	return NULL;
}


int hsr_get_node_data(struct hsr_priv *hsr,
		      const unsigned char *addr,
		      unsigned char addr_b[ETH_ALEN],
		      unsigned int *addr_b_ifindex,
		      int *if1_age,
		      u16 *if1_seq,
		      int *if2_age,
		      u16 *if2_seq)
{
	struct hsr_node *node;
	unsigned long tdiff;


	rcu_read_lock();
	node = find_node_by_AddrA(&hsr->node_db, addr);
	if (!node) {
		rcu_read_unlock();
		return -ENOENT;	/* No such entry */
	}

	ether_addr_copy(addr_b, node->MacAddressB);

	tdiff = jiffies - node->time_in[HSR_DEV_SLAVE_A];
	if (node->time_in_stale[HSR_DEV_SLAVE_A])
		*if1_age = INT_MAX;
#if HZ <= MSEC_PER_SEC
	else if (tdiff > msecs_to_jiffies(INT_MAX))
		*if1_age = INT_MAX;
#endif
	else
		*if1_age = jiffies_to_msecs(tdiff);

	tdiff = jiffies - node->time_in[HSR_DEV_SLAVE_B];
	if (node->time_in_stale[HSR_DEV_SLAVE_B])
		*if2_age = INT_MAX;
#if HZ <= MSEC_PER_SEC
	else if (tdiff > msecs_to_jiffies(INT_MAX))
		*if2_age = INT_MAX;
#endif
	else
		*if2_age = jiffies_to_msecs(tdiff);

	/* Present sequence numbers as if they were incoming on interface */
	*if1_seq = node->seq_out[HSR_DEV_SLAVE_B];
	*if2_seq = node->seq_out[HSR_DEV_SLAVE_A];

	if ((node->AddrB_if != HSR_DEV_NONE) && hsr->slave[node->AddrB_if])
		*addr_b_ifindex = hsr->slave[node->AddrB_if]->ifindex;
	else
		*addr_b_ifindex = -1;

	rcu_read_unlock();

	return 0;
}
