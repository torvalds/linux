// SPDX-License-Identifier: GPL-2.0
/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 *
 * The HSR spec says never to forward the same frame twice on the same
 * interface. A frame is identified by its source MAC address and its HSR
 * sequence number. This code keeps track of senders and their sequence numbers
 * to allow filtering of duplicate frames, and to detect HSR ring errors.
 * Same code handles filtering of duplicates for PRP as well.
 */

#include <kunit/visibility.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include "hsr_main.h"
#include "hsr_framereg.h"
#include "hsr_netlink.h"

bool hsr_addr_is_redbox(struct hsr_priv *hsr, unsigned char *addr)
{
	if (!hsr->redbox || !is_valid_ether_addr(hsr->macaddress_redbox))
		return false;

	return ether_addr_equal(addr, hsr->macaddress_redbox);
}

bool hsr_addr_is_self(struct hsr_priv *hsr, unsigned char *addr)
{
	struct hsr_self_node *sn;
	bool ret = false;

	rcu_read_lock();
	sn = rcu_dereference(hsr->self_node);
	if (!sn) {
		WARN_ONCE(1, "HSR: No self node\n");
		goto out;
	}

	if (ether_addr_equal(addr, sn->macaddress_A) ||
	    ether_addr_equal(addr, sn->macaddress_B))
		ret = true;
out:
	rcu_read_unlock();
	return ret;
}

/* Search for mac entry. Caller must hold rcu read lock.
 */
static struct hsr_node *find_node_by_addr_A(struct list_head *node_db,
					    const unsigned char addr[ETH_ALEN])
{
	struct hsr_node *node;

	list_for_each_entry_rcu(node, node_db, mac_list) {
		if (ether_addr_equal(node->macaddress_A, addr))
			return node;
	}

	return NULL;
}

/* Check if node for a given MAC address is already present in data base
 */
bool hsr_is_node_in_db(struct list_head *node_db,
		       const unsigned char addr[ETH_ALEN])
{
	return !!find_node_by_addr_A(node_db, addr);
}

/* Helper for device init; the self_node is used in hsr_rcv() to recognize
 * frames from self that's been looped over the HSR ring.
 */
int hsr_create_self_node(struct hsr_priv *hsr,
			 const unsigned char addr_a[ETH_ALEN],
			 const unsigned char addr_b[ETH_ALEN])
{
	struct hsr_self_node *sn, *old;

	sn = kmalloc(sizeof(*sn), GFP_KERNEL);
	if (!sn)
		return -ENOMEM;

	ether_addr_copy(sn->macaddress_A, addr_a);
	ether_addr_copy(sn->macaddress_B, addr_b);

	spin_lock_bh(&hsr->list_lock);
	old = rcu_replace_pointer(hsr->self_node, sn,
				  lockdep_is_held(&hsr->list_lock));
	spin_unlock_bh(&hsr->list_lock);

	if (old)
		kfree_rcu(old, rcu_head);
	return 0;
}

void hsr_del_self_node(struct hsr_priv *hsr)
{
	struct hsr_self_node *old;

	spin_lock_bh(&hsr->list_lock);
	old = rcu_replace_pointer(hsr->self_node, NULL,
				  lockdep_is_held(&hsr->list_lock));
	spin_unlock_bh(&hsr->list_lock);
	if (old)
		kfree_rcu(old, rcu_head);
}

static void hsr_free_node(struct hsr_node *node)
{
	xa_destroy(&node->seq_blocks);
	kfree(node->block_buf);
	kfree(node);
}

static void hsr_free_node_rcu(struct rcu_head *rn)
{
	struct hsr_node *node = container_of(rn, struct hsr_node, rcu_head);

	hsr_free_node(node);
}

void hsr_del_nodes(struct list_head *node_db)
{
	struct hsr_node *node;
	struct hsr_node *tmp;

	list_for_each_entry_safe(node, tmp, node_db, mac_list) {
		list_del(&node->mac_list);
		hsr_free_node(node);
	}
}

void prp_handle_san_frame(bool san, enum hsr_port_type port,
			  struct hsr_node *node)
{
	/* Mark if the SAN node is over LAN_A or LAN_B */
	if (port == HSR_PT_SLAVE_A) {
		node->san_a = true;
		return;
	}

	if (port == HSR_PT_SLAVE_B)
		node->san_b = true;
}

/* Allocate an hsr_node and add it to node_db. 'addr' is the node's address_A.
 */
static struct hsr_node *hsr_add_node(struct hsr_priv *hsr,
				     struct list_head *node_db,
				     unsigned char addr[], bool san,
				     enum hsr_port_type rx_port)
{
	struct hsr_node *new_node, *node = NULL;
	unsigned long now;
	size_t block_sz;
	int i;

	new_node = kzalloc(sizeof(*new_node), GFP_ATOMIC);
	if (!new_node)
		return NULL;

	ether_addr_copy(new_node->macaddress_A, addr);
	spin_lock_init(&new_node->seq_out_lock);

	if (hsr->prot_version == PRP_V1)
		new_node->seq_port_cnt = 1;
	else
		new_node->seq_port_cnt = HSR_PT_PORTS - 1;

	block_sz = hsr_seq_block_size(new_node);
	new_node->block_buf = kcalloc(HSR_MAX_SEQ_BLOCKS, block_sz, GFP_ATOMIC);
	if (!new_node->block_buf)
		goto free;

	xa_init(&new_node->seq_blocks);

	/* We are only interested in time diffs here, so use current jiffies
	 * as initialization. (0 could trigger an spurious ring error warning).
	 */
	now = jiffies;
	for (i = 0; i < HSR_PT_PORTS; i++) {
		new_node->time_in[i] = now;
	}

	if (san && hsr->proto_ops->handle_san_frame)
		hsr->proto_ops->handle_san_frame(san, rx_port, new_node);

	spin_lock_bh(&hsr->list_lock);
	list_for_each_entry_rcu(node, node_db, mac_list,
				lockdep_is_held(&hsr->list_lock)) {
		if (ether_addr_equal(node->macaddress_A, addr))
			goto out;
		if (ether_addr_equal(node->macaddress_B, addr))
			goto out;
	}
	list_add_tail_rcu(&new_node->mac_list, node_db);
	spin_unlock_bh(&hsr->list_lock);
	return new_node;
out:
	spin_unlock_bh(&hsr->list_lock);
	kfree(new_node->block_buf);
free:
	kfree(new_node);
	return node;
}

void prp_update_san_info(struct hsr_node *node, bool is_sup)
{
	if (!is_sup)
		return;

	node->san_a = false;
	node->san_b = false;
}

/* Get the hsr_node from which 'skb' was sent.
 */
struct hsr_node *hsr_get_node(struct hsr_port *port, struct list_head *node_db,
			      struct sk_buff *skb, bool is_sup,
			      enum hsr_port_type rx_port)
{
	struct hsr_priv *hsr = port->hsr;
	struct hsr_node *node;
	struct ethhdr *ethhdr;
	struct prp_rct *rct;
	bool san = false;

	if (!skb_mac_header_was_set(skb))
		return NULL;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	list_for_each_entry_rcu(node, node_db, mac_list) {
		if (ether_addr_equal(node->macaddress_A, ethhdr->h_source)) {
			if (hsr->proto_ops->update_san_info)
				hsr->proto_ops->update_san_info(node, is_sup);
			return node;
		}
		if (ether_addr_equal(node->macaddress_B, ethhdr->h_source)) {
			if (hsr->proto_ops->update_san_info)
				hsr->proto_ops->update_san_info(node, is_sup);
			return node;
		}
	}

	/* Check if required node is not in proxy nodes table */
	list_for_each_entry_rcu(node, &hsr->proxy_node_db, mac_list) {
		if (ether_addr_equal(node->macaddress_A, ethhdr->h_source)) {
			if (hsr->proto_ops->update_san_info)
				hsr->proto_ops->update_san_info(node, is_sup);
			return node;
		}
	}

	/* Everyone may create a node entry, connected node to a HSR/PRP
	 * device.
	 */
	if (ethhdr->h_proto == htons(ETH_P_PRP) ||
	    ethhdr->h_proto == htons(ETH_P_HSR)) {
		/* Check if skb contains hsr_ethhdr */
		if (skb->mac_len < sizeof(struct hsr_ethhdr))
			return NULL;
	} else {
		rct = skb_get_PRP_rct(skb);
		if (!rct && rx_port != HSR_PT_MASTER)
			san = true;
	}

	return hsr_add_node(hsr, node_db, ethhdr->h_source, san, rx_port);
}

static bool hsr_seq_block_is_old(struct hsr_seq_block *block)
{
	unsigned long expiry = msecs_to_jiffies(HSR_ENTRY_FORGET_TIME);

	return time_is_before_jiffies(block->time + expiry);
}

static void hsr_forget_seq_block(struct hsr_node *node,
				 struct hsr_seq_block *block)
{
	if (block->time)
		xa_erase(&node->seq_blocks, block->block_idx);
	block->time = 0;
}

/* Get the currently active sequence number block. If there is no block yet, or
 * the existing one is expired, a new block is created. The idea is to maintain
 * a "sparse bitmap" where a bitmap for the whole sequence number space is
 * split into blocks and not all blocks exist all the time. The blocks can
 * expire after time (in low traffic situations) or when they are replaced in
 * the backing fixed size buffer (in high traffic situations).
 */
VISIBLE_IF_KUNIT struct hsr_seq_block *hsr_get_seq_block(struct hsr_node *node,
							 u16 block_idx)
{
	struct hsr_seq_block *block, *res;
	size_t block_sz;

	block = xa_load(&node->seq_blocks, block_idx);

	if (block && hsr_seq_block_is_old(block)) {
		hsr_forget_seq_block(node, block);
		block = NULL;
	}

	if (!block) {
		block_sz = hsr_seq_block_size(node);
		block = node->block_buf + node->next_block * block_sz;
		hsr_forget_seq_block(node, block);

		memset(block, 0, block_sz);
		block->time = jiffies;
		block->block_idx = block_idx;

		res = xa_store(&node->seq_blocks, block_idx, block, GFP_ATOMIC);
		if (xa_is_err(res)) {
			block->time = 0;
			return NULL;
		}

		node->next_block =
			(node->next_block + 1) & (HSR_MAX_SEQ_BLOCKS - 1);
	}

	return block;
}
EXPORT_SYMBOL_IF_KUNIT(hsr_get_seq_block);

/* Use the Supervision frame's info about an eventual macaddress_B for merging
 * nodes that has previously had their macaddress_B registered as a separate
 * node.
 */
void hsr_handle_sup_frame(struct hsr_frame_info *frame)
{
	struct hsr_node *node_curr = frame->node_src;
	struct hsr_port *port_rcv = frame->port_rcv;
	struct hsr_seq_block *src_blk, *merge_blk;
	struct hsr_priv *hsr = port_rcv->hsr;
	struct hsr_sup_tlv *hsr_sup_tlv;
	struct hsr_sup_payload *hsr_sp;
	struct hsr_node *node_real;
	struct sk_buff *skb = NULL;
	struct list_head *node_db;
	struct ethhdr *ethhdr;
	unsigned int total_pull_size = 0;
	unsigned int pull_size = 0;
	unsigned long idx;
	int i;

	/* Here either frame->skb_hsr or frame->skb_prp should be
	 * valid as supervision frame always will have protocol
	 * header info.
	 */
	if (frame->skb_hsr)
		skb = frame->skb_hsr;
	else if (frame->skb_prp)
		skb = frame->skb_prp;
	else if (frame->skb_std)
		skb = frame->skb_std;
	if (!skb)
		return;

	/* Leave the ethernet header. */
	pull_size = sizeof(struct ethhdr);
	skb_pull(skb, pull_size);
	total_pull_size += pull_size;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* And leave the HSR tag. */
	if (ethhdr->h_proto == htons(ETH_P_HSR)) {
		pull_size = sizeof(struct hsr_tag);
		skb_pull(skb, pull_size);
		total_pull_size += pull_size;
	}

	/* And leave the HSR sup tag. */
	pull_size = sizeof(struct hsr_sup_tag);
	skb_pull(skb, pull_size);
	total_pull_size += pull_size;

	/* get HSR sup payload */
	hsr_sp = (struct hsr_sup_payload *)skb->data;

	/* Merge node_curr (registered on macaddress_B) into node_real */
	node_db = &port_rcv->hsr->node_db;
	node_real = find_node_by_addr_A(node_db, hsr_sp->macaddress_A);
	if (!node_real)
		/* No frame received from AddrA of this node yet */
		node_real = hsr_add_node(hsr, node_db, hsr_sp->macaddress_A,
					 true, port_rcv->type);
	if (!node_real)
		goto done; /* No mem */
	if (node_real == node_curr)
		/* Node has already been merged */
		goto done;

	/* Leave the first HSR sup payload. */
	pull_size = sizeof(struct hsr_sup_payload);
	skb_pull(skb, pull_size);
	total_pull_size += pull_size;

	/* Get second supervision tlv */
	hsr_sup_tlv = (struct hsr_sup_tlv *)skb->data;
	/* And check if it is a redbox mac TLV */
	if (hsr_sup_tlv->HSR_TLV_type == PRP_TLV_REDBOX_MAC) {
		/* We could stop here after pushing hsr_sup_payload,
		 * or proceed and allow macaddress_B and for redboxes.
		 */
		/* Sanity check length */
		if (hsr_sup_tlv->HSR_TLV_length != 6)
			goto done;

		/* Leave the second HSR sup tlv. */
		pull_size = sizeof(struct hsr_sup_tlv);
		skb_pull(skb, pull_size);
		total_pull_size += pull_size;

		/* Get redbox mac address. */
		hsr_sp = (struct hsr_sup_payload *)skb->data;

		/* Check if redbox mac and node mac are equal. */
		if (!ether_addr_equal(node_real->macaddress_A, hsr_sp->macaddress_A)) {
			/* This is a redbox supervision frame for a VDAN! */
			goto done;
		}
	}

	ether_addr_copy(node_real->macaddress_B, ethhdr->h_source);
	spin_lock_bh(&node_real->seq_out_lock);
	for (i = 0; i < HSR_PT_PORTS; i++) {
		if (!node_curr->time_in_stale[i] &&
		    time_after(node_curr->time_in[i], node_real->time_in[i])) {
			node_real->time_in[i] = node_curr->time_in[i];
			node_real->time_in_stale[i] =
						node_curr->time_in_stale[i];
		}
	}

	xa_for_each(&node_curr->seq_blocks, idx, src_blk) {
		if (hsr_seq_block_is_old(src_blk))
			continue;

		merge_blk = hsr_get_seq_block(node_real, src_blk->block_idx);
		if (!merge_blk)
			continue;
		merge_blk->time = min(merge_blk->time, src_blk->time);
		for (i = 0; i < node_real->seq_port_cnt; i++) {
			bitmap_or(merge_blk->seq_nrs[i], merge_blk->seq_nrs[i],
				  src_blk->seq_nrs[i], HSR_SEQ_BLOCK_SIZE);
		}
	}
	spin_unlock_bh(&node_real->seq_out_lock);
	node_real->addr_B_port = port_rcv->type;

	spin_lock_bh(&hsr->list_lock);
	if (!node_curr->removed) {
		list_del_rcu(&node_curr->mac_list);
		node_curr->removed = true;
		call_rcu(&node_curr->rcu_head, hsr_free_node_rcu);
	}
	spin_unlock_bh(&hsr->list_lock);

done:
	/* Push back here */
	skb_push(skb, total_pull_size);
}

/* 'skb' is a frame meant for this host, that is to be passed to upper layers.
 *
 * If the frame was sent by a node's B interface, replace the source
 * address with that node's "official" address (macaddress_A) so that upper
 * layers recognize where it came from.
 */
void hsr_addr_subst_source(struct hsr_node *node, struct sk_buff *skb)
{
	if (!skb_mac_header_was_set(skb)) {
		WARN_ONCE(1, "%s: Mac header not set\n", __func__);
		return;
	}

	memcpy(&eth_hdr(skb)->h_source, node->macaddress_A, ETH_ALEN);
}

/* 'skb' is a frame meant for another host.
 * 'port' is the outgoing interface
 *
 * Substitute the target (dest) MAC address if necessary, so the it matches the
 * recipient interface MAC address, regardless of whether that is the
 * recipient's A or B interface.
 * This is needed to keep the packets flowing through switches that learn on
 * which "side" the different interfaces are.
 */
void hsr_addr_subst_dest(struct hsr_node *node_src, struct sk_buff *skb,
			 struct hsr_port *port)
{
	struct hsr_node *node_dst;

	if (!skb_mac_header_was_set(skb)) {
		WARN_ONCE(1, "%s: Mac header not set\n", __func__);
		return;
	}

	if (!is_unicast_ether_addr(eth_hdr(skb)->h_dest))
		return;

	node_dst = find_node_by_addr_A(&port->hsr->node_db,
				       eth_hdr(skb)->h_dest);
	if (!node_dst && port->hsr->redbox)
		node_dst = find_node_by_addr_A(&port->hsr->proxy_node_db,
					       eth_hdr(skb)->h_dest);

	if (!node_dst) {
		if (port->hsr->prot_version != PRP_V1 && net_ratelimit())
			netdev_err(skb->dev, "%s: Unknown node\n", __func__);
		return;
	}
	if (port->type != node_dst->addr_B_port)
		return;

	if (is_valid_ether_addr(node_dst->macaddress_B))
		ether_addr_copy(eth_hdr(skb)->h_dest, node_dst->macaddress_B);
}

void hsr_register_frame_in(struct hsr_node *node, struct hsr_port *port,
			   u16 sequence_nr)
{
	node->time_in[port->type] = jiffies;
	node->time_in_stale[port->type] = false;
}

/* Duplicate discard algorithm: we maintain a bitmap where we set a bit for
 * every seen sequence number. The bitmap is split into blocks and the block
 * management is detailed in hsr_get_seq_block(). In any case, we err on the
 * side of accepting a packet, as the specification requires the algorithm to
 * be "designed such that it never rejects a legitimate frame, while occasional
 * acceptance of a duplicate can be tolerated." (IEC 62439-3:2021, 4.1.10.3).
 * While this requirement is explicit for PRP, applying it to HSR does no harm
 * either.
 *
 * 'frame' is the frame to be sent
 * 'port_type' is the type of the outgoing interface
 *
 * Return:
 *	 1 if frame can be shown to have been sent recently on this interface,
 *	 0 otherwise
 */
static int hsr_check_duplicate(struct hsr_frame_info *frame,
			       unsigned int port_type)
{
	u16 sequence_nr, seq_bit, block_idx;
	struct hsr_seq_block *block;
	struct hsr_node *node;

	node = frame->node_src;
	sequence_nr = frame->sequence_nr;

	if (WARN_ON_ONCE(port_type >= node->seq_port_cnt))
		return 0;

	spin_lock_bh(&node->seq_out_lock);

	block_idx = hsr_seq_block_index(sequence_nr);
	block = hsr_get_seq_block(node, block_idx);
	if (!block)
		goto out_new;

	seq_bit = hsr_seq_block_bit(sequence_nr);
	if (__test_and_set_bit(seq_bit, block->seq_nrs[port_type]))
		goto out_seen;

out_new:
	spin_unlock_bh(&node->seq_out_lock);
	return 0;

out_seen:
	spin_unlock_bh(&node->seq_out_lock);
	return 1;
}

/* HSR duplicate discard: we check if the same frame has already been sent on
 * this outgoing interface. The check follows the general duplicate discard
 * algorithm.
 *
 * 'port' is the outgoing interface
 * 'frame' is the frame to be sent
 *
 * Return:
 *	 1 if frame can be shown to have been sent recently on this interface,
 *	 0 otherwise
 */
int hsr_register_frame_out(struct hsr_port *port, struct hsr_frame_info *frame)
{
	return hsr_check_duplicate(frame, port->type - 1);
}

/* PRP duplicate discard: we only consider frames that are received on port A
 * or port B and should go to the master port. For those, we check if they have
 * already been received by the host, i.e., master port. The check uses the
 * general duplicate discard algorithm, but without tracking multiple ports.
 *
 * 'port' is the outgoing interface
 * 'frame' is the frame to be sent
 *
 * Return:
 *	 1 if frame can be shown to have been sent recently on this interface,
 *	 0 otherwise
 */
int prp_register_frame_out(struct hsr_port *port, struct hsr_frame_info *frame)
{
	/* out-going frames are always in order */
	if (frame->port_rcv->type == HSR_PT_MASTER)
		return 0;

	/* for PRP we should only forward frames from the slave ports
	 * to the master port
	 */
	if (port->type != HSR_PT_MASTER)
		return 1;

	return hsr_check_duplicate(frame, 0);
}
EXPORT_SYMBOL_IF_KUNIT(prp_register_frame_out);

static struct hsr_port *get_late_port(struct hsr_priv *hsr,
				      struct hsr_node *node)
{
	if (node->time_in_stale[HSR_PT_SLAVE_A])
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_A);
	if (node->time_in_stale[HSR_PT_SLAVE_B])
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_B);

	if (time_after(node->time_in[HSR_PT_SLAVE_B],
		       node->time_in[HSR_PT_SLAVE_A] +
					msecs_to_jiffies(MAX_SLAVE_DIFF)))
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_A);
	if (time_after(node->time_in[HSR_PT_SLAVE_A],
		       node->time_in[HSR_PT_SLAVE_B] +
					msecs_to_jiffies(MAX_SLAVE_DIFF)))
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_B);

	return NULL;
}

/* Remove stale sequence_nr records. Called by timer every
 * HSR_LIFE_CHECK_INTERVAL (two seconds or so).
 */
void hsr_prune_nodes(struct timer_list *t)
{
	struct hsr_priv *hsr = timer_container_of(hsr, t, prune_timer);
	struct hsr_node *node;
	struct hsr_node *tmp;
	struct hsr_port *port;
	unsigned long timestamp;
	unsigned long time_a, time_b;

	spin_lock_bh(&hsr->list_lock);
	list_for_each_entry_safe(node, tmp, &hsr->node_db, mac_list) {
		/* Don't prune own node. Neither time_in[HSR_PT_SLAVE_A]
		 * nor time_in[HSR_PT_SLAVE_B], will ever be updated for
		 * the master port. Thus the master node will be repeatedly
		 * pruned leading to packet loss.
		 */
		if (hsr_addr_is_self(hsr, node->macaddress_A))
			continue;

		/* Shorthand */
		time_a = node->time_in[HSR_PT_SLAVE_A];
		time_b = node->time_in[HSR_PT_SLAVE_B];

		/* Check for timestamps old enough to risk wrap-around */
		if (time_after(jiffies, time_a + MAX_JIFFY_OFFSET / 2))
			node->time_in_stale[HSR_PT_SLAVE_A] = true;
		if (time_after(jiffies, time_b + MAX_JIFFY_OFFSET / 2))
			node->time_in_stale[HSR_PT_SLAVE_B] = true;

		/* Get age of newest frame from node.
		 * At least one time_in is OK here; nodes get pruned long
		 * before both time_ins can get stale
		 */
		timestamp = time_a;
		if (node->time_in_stale[HSR_PT_SLAVE_A] ||
		    (!node->time_in_stale[HSR_PT_SLAVE_B] &&
		    time_after(time_b, time_a)))
			timestamp = time_b;

		/* Warn of ring error only as long as we get frames at all */
		if (time_is_after_jiffies(timestamp +
				msecs_to_jiffies(1.5 * MAX_SLAVE_DIFF))) {
			rcu_read_lock();
			port = get_late_port(hsr, node);
			if (port)
				hsr_nl_ringerror(hsr, node->macaddress_A, port);
			rcu_read_unlock();
		}

		/* Prune old entries */
		if (time_is_before_jiffies(timestamp +
				msecs_to_jiffies(HSR_NODE_FORGET_TIME))) {
			hsr_nl_nodedown(hsr, node->macaddress_A);
			if (!node->removed) {
				list_del_rcu(&node->mac_list);
				node->removed = true;
				/* Note that we need to free this entry later: */
				call_rcu(&node->rcu_head, hsr_free_node_rcu);
			}
		}
	}
	spin_unlock_bh(&hsr->list_lock);

	/* Restart timer */
	mod_timer(&hsr->prune_timer,
		  jiffies + msecs_to_jiffies(PRUNE_PERIOD));
}

void hsr_prune_proxy_nodes(struct timer_list *t)
{
	struct hsr_priv *hsr = timer_container_of(hsr, t, prune_proxy_timer);
	unsigned long timestamp;
	struct hsr_node *node;
	struct hsr_node *tmp;

	spin_lock_bh(&hsr->list_lock);
	list_for_each_entry_safe(node, tmp, &hsr->proxy_node_db, mac_list) {
		/* Don't prune RedBox node. */
		if (hsr_addr_is_redbox(hsr, node->macaddress_A))
			continue;

		timestamp = node->time_in[HSR_PT_INTERLINK];

		/* Prune old entries */
		if (time_is_before_jiffies(timestamp +
				msecs_to_jiffies(HSR_PROXY_NODE_FORGET_TIME))) {
			hsr_nl_nodedown(hsr, node->macaddress_A);
			if (!node->removed) {
				list_del_rcu(&node->mac_list);
				node->removed = true;
				/* Note that we need to free this entry later: */
				call_rcu(&node->rcu_head, hsr_free_node_rcu);
			}
		}
	}

	spin_unlock_bh(&hsr->list_lock);

	/* Restart timer */
	mod_timer(&hsr->prune_proxy_timer,
		  jiffies + msecs_to_jiffies(PRUNE_PROXY_PERIOD));
}

void *hsr_get_next_node(struct hsr_priv *hsr, void *_pos,
			unsigned char addr[ETH_ALEN])
{
	struct hsr_node *node;

	if (!_pos) {
		node = list_first_or_null_rcu(&hsr->node_db,
					      struct hsr_node, mac_list);
		if (node)
			ether_addr_copy(addr, node->macaddress_A);
		return node;
	}

	node = _pos;
	list_for_each_entry_continue_rcu(node, &hsr->node_db, mac_list) {
		ether_addr_copy(addr, node->macaddress_A);
		return node;
	}

	return NULL;
}

/* Fill the last sequence number that has been received from node on if1 by
 * finding the last sequence number sent on port B; accordingly get the last
 * received sequence number for if2 using sent sequence numbers on port A.
 */
static void fill_last_seq_nrs(struct hsr_node *node, u16 *if1_seq, u16 *if2_seq)
{
	struct hsr_seq_block *block;
	unsigned int block_off;
	size_t block_sz;
	u16 seq_bit;

	spin_lock_bh(&node->seq_out_lock);

	/* Get last inserted block */
	block_off = (node->next_block - 1) & (HSR_MAX_SEQ_BLOCKS - 1);
	block_sz = hsr_seq_block_size(node);
	block = node->block_buf + block_off * block_sz;

	if (!bitmap_empty(block->seq_nrs[HSR_PT_SLAVE_B - 1],
			  HSR_SEQ_BLOCK_SIZE)) {
		seq_bit = find_last_bit(block->seq_nrs[HSR_PT_SLAVE_B - 1],
					HSR_SEQ_BLOCK_SIZE);
		*if1_seq = (block->block_idx << HSR_SEQ_BLOCK_SHIFT) | seq_bit;
	}
	if (!bitmap_empty(block->seq_nrs[HSR_PT_SLAVE_A - 1],
			  HSR_SEQ_BLOCK_SIZE)) {
		seq_bit = find_last_bit(block->seq_nrs[HSR_PT_SLAVE_A - 1],
					HSR_SEQ_BLOCK_SIZE);
		*if2_seq = (block->block_idx << HSR_SEQ_BLOCK_SHIFT) | seq_bit;
	}
	spin_unlock_bh(&node->seq_out_lock);
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
	struct hsr_port *port;
	unsigned long tdiff;

	node = find_node_by_addr_A(&hsr->node_db, addr);
	if (!node)
		return -ENOENT;

	ether_addr_copy(addr_b, node->macaddress_B);

	tdiff = jiffies - node->time_in[HSR_PT_SLAVE_A];
	if (node->time_in_stale[HSR_PT_SLAVE_A])
		*if1_age = INT_MAX;
#if HZ <= MSEC_PER_SEC
	else if (tdiff > msecs_to_jiffies(INT_MAX))
		*if1_age = INT_MAX;
#endif
	else
		*if1_age = jiffies_to_msecs(tdiff);

	tdiff = jiffies - node->time_in[HSR_PT_SLAVE_B];
	if (node->time_in_stale[HSR_PT_SLAVE_B])
		*if2_age = INT_MAX;
#if HZ <= MSEC_PER_SEC
	else if (tdiff > msecs_to_jiffies(INT_MAX))
		*if2_age = INT_MAX;
#endif
	else
		*if2_age = jiffies_to_msecs(tdiff);

	/* Present sequence numbers as if they were incoming on interface */
	*if1_seq = 0;
	*if2_seq = 0;
	if (hsr->prot_version != PRP_V1)
		fill_last_seq_nrs(node, if1_seq, if2_seq);

	if (node->addr_B_port != HSR_PT_NONE) {
		port = hsr_port_get_hsr(hsr, node->addr_B_port);
		*addr_b_ifindex = port->dev->ifindex;
	} else {
		*addr_b_ifindex = -1;
	}

	return 0;
}
