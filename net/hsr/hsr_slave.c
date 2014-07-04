/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 */

#include "hsr_slave.h"
#include <linux/etherdevice.h>
#include "hsr_main.h"
#include "hsr_framereg.h"


static struct sk_buff *hsr_pull_tag(struct sk_buff *skb)
{
	struct hsr_tag *hsr_tag;
	struct sk_buff *skb2;

	skb2 = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb2))
		goto err_free;
	skb = skb2;

	if (unlikely(!pskb_may_pull(skb, HSR_HLEN)))
		goto err_free;

	hsr_tag = (struct hsr_tag *) skb->data;
	skb->protocol = hsr_tag->encap_proto;
	skb_pull(skb, HSR_HLEN);

	return skb;

err_free:
	kfree_skb(skb);
	return NULL;
}


/* The uses I can see for these HSR supervision frames are:
 * 1) Use the frames that are sent after node initialization ("HSR_TLV.Type =
 *    22") to reset any sequence_nr counters belonging to that node. Useful if
 *    the other node's counter has been reset for some reason.
 *    --
 *    Or not - resetting the counter and bridging the frame would create a
 *    loop, unfortunately.
 *
 * 2) Use the LifeCheck frames to detect ring breaks. I.e. if no LifeCheck
 *    frame is received from a particular node, we know something is wrong.
 *    We just register these (as with normal frames) and throw them away.
 *
 * 3) Allow different MAC addresses for the two slave interfaces, using the
 *    MacAddressA field.
 */
static bool is_supervision_frame(struct hsr_priv *hsr, struct sk_buff *skb)
{
	struct hsr_sup_tag *hsr_stag;

	if (!ether_addr_equal(eth_hdr(skb)->h_dest,
			      hsr->sup_multicast_addr))
		return false;

	hsr_stag = (struct hsr_sup_tag *) skb->data;
	if (get_hsr_stag_path(hsr_stag) != 0x0f)
		return false;
	if ((hsr_stag->HSR_TLV_Type != HSR_TLV_ANNOUNCE) &&
	    (hsr_stag->HSR_TLV_Type != HSR_TLV_LIFE_CHECK))
		return false;
	if (hsr_stag->HSR_TLV_Length != 12)
		return false;

	return true;
}


/* Implementation somewhat according to IEC-62439-3, p. 43
 */
rx_handler_result_t hsr_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct net_device *dev = skb->dev;
	struct hsr_priv *hsr;
	struct net_device *other_slave;
	struct hsr_node *node;
	bool deliver_to_self;
	struct sk_buff *skb_deliver;
	enum hsr_dev_idx dev_in_idx, dev_other_idx;
	bool dup_out;
	int ret;

	if (eth_hdr(skb)->h_proto != htons(ETH_P_PRP))
		return RX_HANDLER_PASS;

	hsr = get_hsr_master(dev);
	if (!hsr) {
		WARN_ON_ONCE(1);
		return RX_HANDLER_PASS;
	}

	if (dev == hsr->slave[0]) {
		dev_in_idx = HSR_DEV_SLAVE_A;
		dev_other_idx = HSR_DEV_SLAVE_B;
	} else {
		dev_in_idx = HSR_DEV_SLAVE_B;
		dev_other_idx = HSR_DEV_SLAVE_A;
	}

	node = hsr_find_node(&hsr->self_node_db, skb);
	if (node) {
		/* Always kill frames sent by ourselves */
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;
	}

	/* Is this frame a candidate for local reception? */
	deliver_to_self = false;
	if ((skb->pkt_type == PACKET_HOST) ||
	    (skb->pkt_type == PACKET_MULTICAST) ||
	    (skb->pkt_type == PACKET_BROADCAST))
		deliver_to_self = true;
	else if (ether_addr_equal(eth_hdr(skb)->h_dest, hsr->dev->dev_addr)) {
		skb->pkt_type = PACKET_HOST;
		deliver_to_self = true;
	}


	rcu_read_lock(); /* node_db */
	node = hsr_find_node(&hsr->node_db, skb);

	if (is_supervision_frame(hsr, skb)) {
		skb_pull(skb, sizeof(struct hsr_sup_tag));
		node = hsr_merge_node(hsr, node, skb, dev_in_idx);
		if (!node) {
			rcu_read_unlock(); /* node_db */
			kfree_skb(skb);
			hsr->dev->stats.rx_dropped++;
			return RX_HANDLER_CONSUMED;
		}
		skb_push(skb, sizeof(struct hsr_sup_tag));
		deliver_to_self = false;
	}

	if (!node) {
		/* Source node unknown; this might be a HSR frame from
		 * another net (different multicast address). Ignore it.
		 */
		rcu_read_unlock(); /* node_db */
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;
	}

	/* Register ALL incoming frames as outgoing through the other interface.
	 * This allows us to register frames as incoming only if they are valid
	 * for the receiving interface, without using a specific counter for
	 * incoming frames.
	 */
	dup_out = hsr_register_frame_out(node, dev_other_idx, skb);
	if (!dup_out)
		hsr_register_frame_in(node, dev_in_idx);

	/* Forward this frame? */
	if (!dup_out && (skb->pkt_type != PACKET_HOST))
		other_slave = get_other_slave(hsr, dev);
	else
		other_slave = NULL;

	if (hsr_register_frame_out(node, HSR_DEV_MASTER, skb))
		deliver_to_self = false;

	rcu_read_unlock(); /* node_db */

	if (!deliver_to_self && !other_slave) {
		kfree_skb(skb);
		/* Circulated frame; silently remove it. */
		return RX_HANDLER_CONSUMED;
	}

	skb_deliver = skb;
	if (deliver_to_self && other_slave) {
		/* skb_clone() is not enough since we will strip the hsr tag
		 * and do address substitution below
		 */
		skb_deliver = pskb_copy(skb, GFP_ATOMIC);
		if (!skb_deliver) {
			deliver_to_self = false;
			hsr->dev->stats.rx_dropped++;
		}
	}

	if (deliver_to_self) {
		bool multicast_frame;

		skb_deliver = hsr_pull_tag(skb_deliver);
		if (!skb_deliver) {
			hsr->dev->stats.rx_dropped++;
			goto forward;
		}
#if !defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
		/* Move everything in the header that is after the HSR tag,
		 * to work around alignment problems caused by the 6-byte HSR
		 * tag. In practice, this removes/overwrites the HSR tag in
		 * the header and restores a "standard" packet.
		 */
		memmove(skb_deliver->data - HSR_HLEN, skb_deliver->data,
			skb_headlen(skb_deliver));

		/* Adjust skb members so they correspond with the move above.
		 * This cannot possibly underflow skb->data since hsr_pull_tag()
		 * above succeeded.
		 * At this point in the protocol stack, the transport and
		 * network headers have not been set yet, and we haven't touched
		 * the mac header nor the head. So we only need to adjust data
		 * and tail:
		 */
		skb_deliver->data -= HSR_HLEN;
		skb_deliver->tail -= HSR_HLEN;
#endif
		skb_deliver->dev = hsr->dev;
		hsr_addr_subst_source(hsr, skb_deliver);
		multicast_frame = (skb_deliver->pkt_type == PACKET_MULTICAST);
		ret = netif_rx(skb_deliver);
		if (ret == NET_RX_DROP) {
			hsr->dev->stats.rx_dropped++;
		} else {
			hsr->dev->stats.rx_packets++;
			hsr->dev->stats.rx_bytes += skb->len;
			if (multicast_frame)
				hsr->dev->stats.multicast++;
		}
	}

forward:
	if (other_slave) {
		skb_push(skb, ETH_HLEN);
		skb->dev = other_slave;
		dev_queue_xmit(skb);
	}

	return RX_HANDLER_CONSUMED;
}
