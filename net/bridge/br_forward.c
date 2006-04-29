/*
 *	Forwarding decision
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_forward.c,v 1.4 2001/08/14 22:05:57 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/netfilter_bridge.h>
#include "br_private.h"

static inline int should_deliver(const struct net_bridge_port *p, 
				 const struct sk_buff *skb)
{
	if (skb->dev == p->dev ||
	    p->state != BR_STATE_FORWARDING)
		return 0;

	return 1;
}

static inline unsigned packet_length(const struct sk_buff *skb)
{
	return skb->len - (skb->protocol == htons(ETH_P_8021Q) ? VLAN_HLEN : 0);
}

int br_dev_queue_push_xmit(struct sk_buff *skb)
{
	/* drop mtu oversized packets except tso */
	if (packet_length(skb) > skb->dev->mtu && !skb_shinfo(skb)->tso_size)
		kfree_skb(skb);
	else {
#ifdef CONFIG_BRIDGE_NETFILTER
		/* ip_refrag calls ip_fragment, doesn't copy the MAC header. */
		nf_bridge_maybe_copy_header(skb);
#endif
		skb_push(skb, ETH_HLEN);

		dev_queue_xmit(skb);
	}

	return 0;
}

int br_forward_finish(struct sk_buff *skb)
{
	NF_HOOK(PF_BRIDGE, NF_BR_POST_ROUTING, skb, NULL, skb->dev,
			br_dev_queue_push_xmit);

	return 0;
}

static void __br_deliver(const struct net_bridge_port *to, struct sk_buff *skb)
{
	skb->dev = to->dev;
	NF_HOOK(PF_BRIDGE, NF_BR_LOCAL_OUT, skb, NULL, skb->dev,
			br_forward_finish);
}

static void __br_forward(const struct net_bridge_port *to, struct sk_buff *skb)
{
	struct net_device *indev;

	indev = skb->dev;
	skb->dev = to->dev;
	skb->ip_summed = CHECKSUM_NONE;

	NF_HOOK(PF_BRIDGE, NF_BR_FORWARD, skb, indev, skb->dev,
			br_forward_finish);
}

/* called with rcu_read_lock */
void br_deliver(const struct net_bridge_port *to, struct sk_buff *skb)
{
	if (should_deliver(to, skb)) {
		__br_deliver(to, skb);
		return;
	}

	kfree_skb(skb);
}

/* called with rcu_read_lock */
void br_forward(const struct net_bridge_port *to, struct sk_buff *skb)
{
	if (should_deliver(to, skb)) {
		__br_forward(to, skb);
		return;
	}

	kfree_skb(skb);
}

/* called under bridge lock */
static void br_flood(struct net_bridge *br, struct sk_buff *skb, int clone,
	void (*__packet_hook)(const struct net_bridge_port *p, 
			      struct sk_buff *skb))
{
	struct net_bridge_port *p;
	struct net_bridge_port *prev;

	if (clone) {
		struct sk_buff *skb2;

		if ((skb2 = skb_clone(skb, GFP_ATOMIC)) == NULL) {
			br->statistics.tx_dropped++;
			return;
		}

		skb = skb2;
	}

	prev = NULL;

	list_for_each_entry_rcu(p, &br->port_list, list) {
		if (should_deliver(p, skb)) {
			if (prev != NULL) {
				struct sk_buff *skb2;

				if ((skb2 = skb_clone(skb, GFP_ATOMIC)) == NULL) {
					br->statistics.tx_dropped++;
					kfree_skb(skb);
					return;
				}

				__packet_hook(prev, skb2);
			}

			prev = p;
		}
	}

	if (prev != NULL) {
		__packet_hook(prev, skb);
		return;
	}

	kfree_skb(skb);
}


/* called with rcu_read_lock */
void br_flood_deliver(struct net_bridge *br, struct sk_buff *skb, int clone)
{
	br_flood(br, skb, clone, __br_deliver);
}

/* called under bridge lock */
void br_flood_forward(struct net_bridge *br, struct sk_buff *skb, int clone)
{
	br_flood(br, skb, clone, __br_forward);
}
