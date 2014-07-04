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
#include <linux/if_arp.h>
#include "hsr_main.h"
#include "hsr_device.h"
#include "hsr_framereg.h"


static int hsr_check_dev_ok(struct net_device *dev)
{
	/* Don't allow HSR on non-ethernet like devices */
	if ((dev->flags & IFF_LOOPBACK) || (dev->type != ARPHRD_ETHER) ||
	    (dev->addr_len != ETH_ALEN)) {
		netdev_info(dev, "Cannot use loopback or non-ethernet device as HSR slave.\n");
		return -EINVAL;
	}

	/* Don't allow enslaving hsr devices */
	if (is_hsr_master(dev)) {
		netdev_info(dev, "Cannot create trees of HSR devices.\n");
		return -EINVAL;
	}

	if (hsr_port_exists(dev)) {
		netdev_info(dev, "This device is already a HSR slave.\n");
		return -EINVAL;
	}

	if (dev->priv_flags & IFF_802_1Q_VLAN) {
		netdev_info(dev, "HSR on top of VLAN is not yet supported in this driver.\n");
		return -EINVAL;
	}

	/* HSR over bonded devices has not been tested, but I'm not sure it
	 * won't work...
	 */

	return 0;
}


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
	struct hsr_port *port, *other_port, *master;
	struct hsr_priv *hsr;
	struct hsr_node *node;
	bool deliver_to_self;
	struct sk_buff *skb_deliver;
	bool dup_out;
	int ret;

	if (eth_hdr(skb)->h_proto != htons(ETH_P_PRP))
		return RX_HANDLER_PASS;

	rcu_read_lock(); /* ports & node */

	port = hsr_port_get_rcu(skb->dev);
	hsr = port->hsr;
	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);

	node = hsr_find_node(&hsr->self_node_db, skb);
	if (node) {
		/* Always kill frames sent by ourselves */
		kfree_skb(skb);
		ret = RX_HANDLER_CONSUMED;
		goto finish;
	}

	/* Is this frame a candidate for local reception? */
	deliver_to_self = false;
	if ((skb->pkt_type == PACKET_HOST) ||
	    (skb->pkt_type == PACKET_MULTICAST) ||
	    (skb->pkt_type == PACKET_BROADCAST))
		deliver_to_self = true;
	else if (ether_addr_equal(eth_hdr(skb)->h_dest,
				  master->dev->dev_addr)) {
		skb->pkt_type = PACKET_HOST;
		deliver_to_self = true;
	}

	node = hsr_find_node(&hsr->node_db, skb);

	if (is_supervision_frame(hsr, skb)) {
		skb_pull(skb, sizeof(struct hsr_sup_tag));
		node = hsr_merge_node(node, skb, port);
		if (!node) {
			kfree_skb(skb);
			master->dev->stats.rx_dropped++;
			ret = RX_HANDLER_CONSUMED;
			goto finish;
		}
		skb_push(skb, sizeof(struct hsr_sup_tag));
		deliver_to_self = false;
	}

	if (!node) {
		/* Source node unknown; this might be a HSR frame from
		 * another net (different multicast address). Ignore it.
		 */
		kfree_skb(skb);
		ret = RX_HANDLER_CONSUMED;
		goto finish;
	}

	if (port->type == HSR_PT_SLAVE_A)
		other_port = hsr_port_get_hsr(hsr, HSR_PT_SLAVE_B);
	else
		other_port = hsr_port_get_hsr(hsr, HSR_PT_SLAVE_A);

	/* Register ALL incoming frames as outgoing through the other interface.
	 * This allows us to register frames as incoming only if they are valid
	 * for the receiving interface, without using a specific counter for
	 * incoming frames.
	 */
	if (other_port)
		dup_out = hsr_register_frame_out(node, other_port, skb);
	else
		dup_out = 0;
	if (!dup_out)
		hsr_register_frame_in(node, port);

	/* Forward this frame? */
	if (dup_out || (skb->pkt_type == PACKET_HOST))
		other_port = NULL;

	if (hsr_register_frame_out(node, master, skb))
		deliver_to_self = false;

	if (!deliver_to_self && !other_port) {
		kfree_skb(skb);
		/* Circulated frame; silently remove it. */
		ret = RX_HANDLER_CONSUMED;
		goto finish;
	}

	skb_deliver = skb;
	if (deliver_to_self && other_port) {
		/* skb_clone() is not enough since we will strip the hsr tag
		 * and do address substitution below
		 */
		skb_deliver = pskb_copy(skb, GFP_ATOMIC);
		if (!skb_deliver) {
			deliver_to_self = false;
			master->dev->stats.rx_dropped++;
		}
	}

	if (deliver_to_self) {
		bool multicast_frame;

		skb_deliver = hsr_pull_tag(skb_deliver);
		if (!skb_deliver) {
			master->dev->stats.rx_dropped++;
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
		skb_deliver->dev = master->dev;
		hsr_addr_subst_source(hsr, skb_deliver);
		multicast_frame = (skb_deliver->pkt_type == PACKET_MULTICAST);
		ret = netif_rx(skb_deliver);
		if (ret == NET_RX_DROP) {
			master->dev->stats.rx_dropped++;
		} else {
			master->dev->stats.rx_packets++;
			master->dev->stats.rx_bytes += skb->len;
			if (multicast_frame)
				master->dev->stats.multicast++;
		}
	}

forward:
	if (other_port) {
		skb_push(skb, ETH_HLEN);
		skb->dev = other_port->dev;
		dev_queue_xmit(skb);
	}

	ret = RX_HANDLER_CONSUMED;

finish:
	rcu_read_unlock();
	return ret;
}

/* Setup device to be added to the HSR bridge. */
static int hsr_portdev_setup(struct net_device *dev, struct hsr_port *port)
{
	int res;

	dev_hold(dev);
	res = dev_set_promiscuity(dev, 1);
	if (res)
		goto fail_promiscuity;
	res = netdev_rx_handler_register(dev, hsr_handle_frame, port);
	if (res)
		goto fail_rx_handler;
	dev_disable_lro(dev);

	/* FIXME:
	 * What does net device "adjacency" mean? Should we do
	 * res = netdev_master_upper_dev_link(port->dev, port->hsr->dev); ?
	 */

	return 0;

fail_rx_handler:
	dev_set_promiscuity(dev, -1);
fail_promiscuity:
	dev_put(dev);

	return res;
}

int hsr_add_port(struct hsr_priv *hsr, struct net_device *dev,
		 enum hsr_port_type type)
{
	struct hsr_port *port, *master;
	int res;

	if (type != HSR_PT_MASTER) {
		res = hsr_check_dev_ok(dev);
		if (res)
			return res;
	}

	port = hsr_port_get_hsr(hsr, type);
	if (port != NULL)
		return -EBUSY;	/* This port already exists */

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (port == NULL)
		return -ENOMEM;

	if (type != HSR_PT_MASTER) {
		res = hsr_portdev_setup(dev, port);
		if (res)
			goto fail_dev_setup;
	}

	port->hsr = hsr;
	port->dev = dev;
	port->type = type;

	list_add_tail_rcu(&port->port_list, &hsr->ports);
	synchronize_rcu();

	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);

	/* Set required header length */
	if (dev->hard_header_len + HSR_HLEN > master->dev->hard_header_len)
		master->dev->hard_header_len = dev->hard_header_len + HSR_HLEN;

	netdev_update_features(master->dev);
	dev_set_mtu(master->dev, hsr_get_max_mtu(hsr));

	return 0;

fail_dev_setup:
	kfree(port);
	return res;
}

void hsr_del_port(struct hsr_port *port)
{
	struct hsr_priv *hsr;
	struct hsr_port *master;

	hsr = port->hsr;
	master = hsr_port_get_hsr(hsr, HSR_PT_MASTER);
	list_del_rcu(&port->port_list);

	if (port != master) {
		netdev_update_features(master->dev);
		dev_set_mtu(master->dev, hsr_get_max_mtu(hsr));
		netdev_rx_handler_unregister(port->dev);
		dev_set_promiscuity(port->dev, -1);
	}

	/* FIXME?
	 * netdev_upper_dev_unlink(port->dev, port->hsr->dev);
	 */

	synchronize_rcu();
	dev_put(port->dev);
}
