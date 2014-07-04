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
 * In addition to routines for registering and unregistering HSR support, this
 * file also contains the receive routine that handles all incoming frames with
 * Ethertype (protocol) ETH_P_PRP (HSRv0), and network device event handling.
 */

#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/timer.h>
#include <linux/etherdevice.h>
#include "hsr_main.h"
#include "hsr_device.h"
#include "hsr_netlink.h"
#include "hsr_framereg.h"


/* List of all registered virtual HSR devices */
static LIST_HEAD(hsr_list);

void register_hsr_master(struct hsr_priv *hsr)
{
	list_add_tail_rcu(&hsr->hsr_list, &hsr_list);
}

void unregister_hsr_master(struct hsr_priv *hsr)
{
	struct hsr_priv *hsr_it;

	list_for_each_entry(hsr_it, &hsr_list, hsr_list)
		if (hsr_it == hsr) {
			list_del_rcu(&hsr_it->hsr_list);
			return;
		}
}

bool is_hsr_slave(struct net_device *dev)
{
	struct hsr_priv *hsr_it;

	list_for_each_entry_rcu(hsr_it, &hsr_list, hsr_list) {
		if (dev == hsr_it->slave[0])
			return true;
		if (dev == hsr_it->slave[1])
			return true;
	}

	return false;
}


/* If dev is a HSR slave device, return the virtual master device. Return NULL
 * otherwise.
 */
static struct hsr_priv *get_hsr_master(struct net_device *dev)
{
	struct hsr_priv *hsr;

	rcu_read_lock();
	list_for_each_entry_rcu(hsr, &hsr_list, hsr_list)
		if ((dev == hsr->slave[0]) ||
		    (dev == hsr->slave[1])) {
			rcu_read_unlock();
			return hsr;
		}

	rcu_read_unlock();
	return NULL;
}


/* If dev is a HSR slave device, return the other slave device. Return NULL
 * otherwise.
 */
static struct net_device *get_other_slave(struct hsr_priv *hsr,
					  struct net_device *dev)
{
	if (dev == hsr->slave[0])
		return hsr->slave[1];
	if (dev == hsr->slave[1])
		return hsr->slave[0];

	return NULL;
}


static int hsr_netdev_notify(struct notifier_block *nb, unsigned long event,
			     void *ptr)
{
	struct net_device *slave, *other_slave;
	struct hsr_priv *hsr;
	int old_operstate;
	int mtu_max;
	int res;
	struct net_device *dev;

	dev = netdev_notifier_info_to_dev(ptr);

	hsr = get_hsr_master(dev);
	if (hsr) {
		/* dev is a slave device */
		slave = dev;
		other_slave = get_other_slave(hsr, slave);
	} else {
		if (!is_hsr_master(dev))
			return NOTIFY_DONE;
		hsr = netdev_priv(dev);
		slave = hsr->slave[0];
		other_slave = hsr->slave[1];
	}

	switch (event) {
	case NETDEV_UP:		/* Administrative state DOWN */
	case NETDEV_DOWN:	/* Administrative state UP */
	case NETDEV_CHANGE:	/* Link (carrier) state changes */
		old_operstate = hsr->dev->operstate;
		hsr_set_carrier(hsr->dev, slave, other_slave);
		/* netif_stacked_transfer_operstate() cannot be used here since
		 * it doesn't set IF_OPER_LOWERLAYERDOWN (?)
		 */
		hsr_set_operstate(hsr->dev, slave, other_slave);
		hsr_check_announce(hsr->dev, old_operstate);
		break;
	case NETDEV_CHANGEADDR:

		/* This should not happen since there's no ndo_set_mac_address()
		 * for HSR devices - i.e. not supported.
		 */
		if (dev == hsr->dev)
			break;

		if (dev == hsr->slave[0])
			ether_addr_copy(hsr->dev->dev_addr,
					hsr->slave[0]->dev_addr);

		/* Make sure we recognize frames from ourselves in hsr_rcv() */
		res = hsr_create_self_node(&hsr->self_node_db,
					   hsr->dev->dev_addr,
					   hsr->slave[1] ?
						hsr->slave[1]->dev_addr :
						hsr->dev->dev_addr);
		if (res)
			netdev_warn(hsr->dev,
				    "Could not update HSR node address.\n");

		if (dev == hsr->slave[0])
			call_netdevice_notifiers(NETDEV_CHANGEADDR, hsr->dev);
		break;
	case NETDEV_CHANGEMTU:
		if (dev == hsr->dev)
			break; /* Handled in ndo_change_mtu() */
		mtu_max = hsr_get_max_mtu(hsr);
		if (hsr->dev->mtu > mtu_max)
			dev_set_mtu(hsr->dev, mtu_max);
		break;
	case NETDEV_UNREGISTER:
		if (dev == hsr->slave[0])
			hsr->slave[0] = NULL;
		if (dev == hsr->slave[1])
			hsr->slave[1] = NULL;

		/* There should really be a way to set a new slave device... */

		break;
	case NETDEV_PRE_TYPE_CHANGE:
		/* HSR works only on Ethernet devices. Refuse slave to change
		 * its type.
		 */
		return NOTIFY_BAD;
	}

	return NOTIFY_DONE;
}


static struct timer_list prune_timer;

static void prune_nodes_all(unsigned long data)
{
	struct hsr_priv *hsr;

	rcu_read_lock();
	list_for_each_entry_rcu(hsr, &hsr_list, hsr_list)
		hsr_prune_nodes(hsr);
	rcu_read_unlock();

	prune_timer.expires = jiffies + msecs_to_jiffies(PRUNE_PERIOD);
	add_timer(&prune_timer);
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
static int hsr_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt, struct net_device *orig_dev)
{
	struct hsr_priv *hsr;
	struct net_device *other_slave;
	struct hsr_node *node;
	bool deliver_to_self;
	struct sk_buff *skb_deliver;
	enum hsr_dev_idx dev_in_idx, dev_other_idx;
	bool dup_out;
	int ret;

	hsr = get_hsr_master(dev);

	if (!hsr) {
		/* Non-HSR-slave device 'dev' is connected to a HSR network */
		kfree_skb(skb);
		dev->stats.rx_errors++;
		return NET_RX_SUCCESS;
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
		return NET_RX_SUCCESS;
	}

	/* Is this frame a candidate for local reception? */
	deliver_to_self = false;
	if ((skb->pkt_type == PACKET_HOST) ||
	    (skb->pkt_type == PACKET_MULTICAST) ||
	    (skb->pkt_type == PACKET_BROADCAST))
		deliver_to_self = true;
	else if (ether_addr_equal(eth_hdr(skb)->h_dest,
				     hsr->dev->dev_addr)) {
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
			return NET_RX_DROP;
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
		return NET_RX_SUCCESS;
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
		return NET_RX_SUCCESS;
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

	return NET_RX_SUCCESS;
}


static struct packet_type hsr_pt __read_mostly = {
	.type = htons(ETH_P_PRP),
	.func = hsr_rcv,
};

static struct notifier_block hsr_nb = {
	.notifier_call = hsr_netdev_notify,	/* Slave event notifications */
};


static int __init hsr_init(void)
{
	int res;

	BUILD_BUG_ON(sizeof(struct hsr_tag) != HSR_HLEN);

	dev_add_pack(&hsr_pt);

	init_timer(&prune_timer);
	prune_timer.function = prune_nodes_all;
	prune_timer.data = 0;
	prune_timer.expires = jiffies + msecs_to_jiffies(PRUNE_PERIOD);
	add_timer(&prune_timer);

	register_netdevice_notifier(&hsr_nb);

	res = hsr_netlink_init();

	return res;
}

static void __exit hsr_exit(void)
{
	unregister_netdevice_notifier(&hsr_nb);
	del_timer_sync(&prune_timer);
	hsr_netlink_exit();
	dev_remove_pack(&hsr_pt);
}

module_init(hsr_init);
module_exit(hsr_exit);
MODULE_LICENSE("GPL");
