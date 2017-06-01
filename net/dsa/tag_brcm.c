/*
 * Broadcom tag support
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "dsa_priv.h"

/* This tag length is 4 bytes, older ones were 6 bytes, we do not
 * handle them
 */
#define BRCM_TAG_LEN	4

/* Tag is constructed and desconstructed using byte by byte access
 * because the tag is placed after the MAC Source Address, which does
 * not make it 4-bytes aligned, so this might cause unaligned accesses
 * on most systems where this is used.
 */

/* Ingress and egress opcodes */
#define BRCM_OPCODE_SHIFT	5
#define BRCM_OPCODE_MASK	0x7

/* Ingress fields */
/* 1st byte in the tag */
#define BRCM_IG_TC_SHIFT	2
#define BRCM_IG_TC_MASK		0x7
/* 2nd byte in the tag */
#define BRCM_IG_TE_MASK		0x3
#define BRCM_IG_TS_SHIFT	7
/* 3rd byte in the tag */
#define BRCM_IG_DSTMAP2_MASK	1
#define BRCM_IG_DSTMAP1_MASK	0xff

/* Egress fields */

/* 2nd byte in the tag */
#define BRCM_EG_CID_MASK	0xff

/* 3rd byte in the tag */
#define BRCM_EG_RC_MASK		0xff
#define  BRCM_EG_RC_RSVD	(3 << 6)
#define  BRCM_EG_RC_EXCEPTION	(1 << 5)
#define  BRCM_EG_RC_PROT_SNOOP	(1 << 4)
#define  BRCM_EG_RC_PROT_TERM	(1 << 3)
#define  BRCM_EG_RC_SWITCH	(1 << 2)
#define  BRCM_EG_RC_MAC_LEARN	(1 << 1)
#define  BRCM_EG_RC_MIRROR	(1 << 0)
#define BRCM_EG_TC_SHIFT	5
#define BRCM_EG_TC_MASK		0x7
#define BRCM_EG_PID_MASK	0x1f

static struct sk_buff *brcm_tag_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	u8 *brcm_tag;

	if (skb_cow_head(skb, BRCM_TAG_LEN) < 0)
		goto out_free;

	skb_push(skb, BRCM_TAG_LEN);

	memmove(skb->data, skb->data + BRCM_TAG_LEN, 2 * ETH_ALEN);

	/* Build the tag after the MAC Source Address */
	brcm_tag = skb->data + 2 * ETH_ALEN;

	/* Set the ingress opcode, traffic class, tag enforcment is
	 * deprecated
	 */
	brcm_tag[0] = (1 << BRCM_OPCODE_SHIFT) |
			((skb->priority << BRCM_IG_TC_SHIFT) & BRCM_IG_TC_MASK);
	brcm_tag[1] = 0;
	brcm_tag[2] = 0;
	if (p->dp->index == 8)
		brcm_tag[2] = BRCM_IG_DSTMAP2_MASK;
	brcm_tag[3] = (1 << p->dp->index) & BRCM_IG_DSTMAP1_MASK;

	return skb;

out_free:
	kfree_skb(skb);
	return NULL;
}

static struct sk_buff *brcm_tag_rcv(struct sk_buff *skb, struct net_device *dev,
				    struct packet_type *pt,
				    struct net_device *orig_dev)
{
	struct dsa_switch_tree *dst = dev->dsa_ptr;
	struct dsa_switch *ds;
	int source_port;
	u8 *brcm_tag;

	ds = dst->cpu_dp->ds;

	if (unlikely(!pskb_may_pull(skb, BRCM_TAG_LEN)))
		return NULL;

	/* skb->data points to the EtherType, the tag is right before it */
	brcm_tag = skb->data - 2;

	/* The opcode should never be different than 0b000 */
	if (unlikely((brcm_tag[0] >> BRCM_OPCODE_SHIFT) & BRCM_OPCODE_MASK))
		return NULL;

	/* We should never see a reserved reason code without knowing how to
	 * handle it
	 */
	if (unlikely(brcm_tag[2] & BRCM_EG_RC_RSVD))
		return NULL;

	/* Locate which port this is coming from */
	source_port = brcm_tag[3] & BRCM_EG_PID_MASK;

	/* Validate port against switch setup, either the port is totally */
	if (source_port >= ds->num_ports || !ds->ports[source_port].netdev)
		return NULL;

	/* Remove Broadcom tag and update checksum */
	skb_pull_rcsum(skb, BRCM_TAG_LEN);

	/* Move the Ethernet DA and SA */
	memmove(skb->data - ETH_HLEN,
		skb->data - ETH_HLEN - BRCM_TAG_LEN,
		2 * ETH_ALEN);

	skb->dev = ds->ports[source_port].netdev;

	return skb;
}

const struct dsa_device_ops brcm_netdev_ops = {
	.xmit	= brcm_tag_xmit,
	.rcv	= brcm_tag_rcv,
};
