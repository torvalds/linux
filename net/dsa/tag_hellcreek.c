// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * net/dsa/tag_hellcreek.c - Hirschmann Hellcreek switch tag format handling
 *
 * Copyright (C) 2019,2020 Linutronix GmbH
 * Author Kurt Kanzenbach <kurt@linutronix.de>
 *
 * Based on tag_ksz.c.
 */

#include <linux/skbuff.h>
#include <net/dsa.h>

#include "tag.h"

#define HELLCREEK_NAME		"hellcreek"

#define HELLCREEK_TAG_LEN	1

static struct sk_buff *hellcreek_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct dsa_port *dp = dsa_user_to_port(dev);
	u8 *tag;

	/* Calculate checksums (if required) before adding the trailer tag to
	 * avoid including it in calculations. That would lead to wrong
	 * checksums after the switch strips the tag.
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL &&
	    skb_checksum_help(skb))
		return NULL;

	/* Tag encoding */
	tag  = skb_put(skb, HELLCREEK_TAG_LEN);
	*tag = BIT(dp->index);

	return skb;
}

static struct sk_buff *hellcreek_rcv(struct sk_buff *skb,
				     struct net_device *dev)
{
	/* Tag decoding */
	u8 *tag = skb_tail_pointer(skb) - HELLCREEK_TAG_LEN;
	unsigned int port = tag[0] & 0x03;

	skb->dev = dsa_conduit_find_user(dev, 0, port);
	if (!skb->dev) {
		netdev_warn_once(dev, "Failed to get source port: %d\n", port);
		return NULL;
	}

	if (pskb_trim_rcsum(skb, skb->len - HELLCREEK_TAG_LEN))
		return NULL;

	dsa_default_offload_fwd_mark(skb);

	return skb;
}

static const struct dsa_device_ops hellcreek_netdev_ops = {
	.name	  = HELLCREEK_NAME,
	.proto	  = DSA_TAG_PROTO_HELLCREEK,
	.xmit	  = hellcreek_xmit,
	.rcv	  = hellcreek_rcv,
	.needed_tailroom = HELLCREEK_TAG_LEN,
};

MODULE_LICENSE("Dual MIT/GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_HELLCREEK, HELLCREEK_NAME);

module_dsa_tag_driver(hellcreek_netdev_ops);
