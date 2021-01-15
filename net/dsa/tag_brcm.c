// SPDX-License-Identifier: GPL-2.0+
/*
 * Broadcom tag support
 *
 * Copyright (C) 2014 Broadcom Corporation
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

#if IS_ENABLED(CONFIG_NET_DSA_TAG_BRCM) || \
	IS_ENABLED(CONFIG_NET_DSA_TAG_BRCM_PREPEND)

static struct sk_buff *brcm_tag_xmit_ll(struct sk_buff *skb,
					struct net_device *dev,
					unsigned int offset)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	u16 queue = skb_get_queue_mapping(skb);
	u8 *brcm_tag;

	/* The Ethernet switch we are interfaced with needs packets to be at
	 * least 64 bytes (including FCS) otherwise they will be discarded when
	 * they enter the switch port logic. When Broadcom tags are enabled, we
	 * need to make sure that packets are at least 68 bytes
	 * (including FCS and tag) because the length verification is done after
	 * the Broadcom tag is stripped off the ingress packet.
	 *
	 * Let dsa_slave_xmit() free the SKB
	 */
	if (__skb_put_padto(skb, ETH_ZLEN + BRCM_TAG_LEN, false))
		return NULL;

	skb_push(skb, BRCM_TAG_LEN);

	if (offset)
		memmove(skb->data, skb->data + BRCM_TAG_LEN, offset);

	brcm_tag = skb->data + offset;

	/* Set the ingress opcode, traffic class, tag enforcment is
	 * deprecated
	 */
	brcm_tag[0] = (1 << BRCM_OPCODE_SHIFT) |
		       ((queue & BRCM_IG_TC_MASK) << BRCM_IG_TC_SHIFT);
	brcm_tag[1] = 0;
	brcm_tag[2] = 0;
	if (dp->index == 8)
		brcm_tag[2] = BRCM_IG_DSTMAP2_MASK;
	brcm_tag[3] = (1 << dp->index) & BRCM_IG_DSTMAP1_MASK;

	/* Now tell the master network device about the desired output queue
	 * as well
	 */
	skb_set_queue_mapping(skb, BRCM_TAG_SET_PORT_QUEUE(dp->index, queue));

	return skb;
}

/* Frames with this tag have one of these two layouts:
 * -----------------------------------
 * | MAC DA | MAC SA | 4b tag | Type | DSA_TAG_PROTO_BRCM
 * -----------------------------------
 * -----------------------------------
 * | 4b tag | MAC DA | MAC SA | Type | DSA_TAG_PROTO_BRCM_PREPEND
 * -----------------------------------
 * In both cases, at receive time, skb->data points 2 bytes before the actual
 * Ethernet type field and we have an offset of 4bytes between where skb->data
 * and where the payload starts. So the same low-level receive function can be
 * used.
 */
static struct sk_buff *brcm_tag_rcv_ll(struct sk_buff *skb,
				       struct net_device *dev,
				       struct packet_type *pt,
				       unsigned int offset)
{
	int source_port;
	u8 *brcm_tag;

	if (unlikely(!pskb_may_pull(skb, BRCM_TAG_LEN)))
		return NULL;

	brcm_tag = skb->data - offset;

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

	skb->dev = dsa_master_find_slave(dev, 0, source_port);
	if (!skb->dev)
		return NULL;

	/* Remove Broadcom tag and update checksum */
	skb_pull_rcsum(skb, BRCM_TAG_LEN);

	skb->offload_fwd_mark = 1;

	return skb;
}
#endif

#if IS_ENABLED(CONFIG_NET_DSA_TAG_BRCM)
static struct sk_buff *brcm_tag_xmit(struct sk_buff *skb,
				     struct net_device *dev)
{
	/* Build the tag after the MAC Source Address */
	return brcm_tag_xmit_ll(skb, dev, 2 * ETH_ALEN);
}


static struct sk_buff *brcm_tag_rcv(struct sk_buff *skb, struct net_device *dev,
				    struct packet_type *pt)
{
	struct sk_buff *nskb;

	/* skb->data points to the EtherType, the tag is right before it */
	nskb = brcm_tag_rcv_ll(skb, dev, pt, 2);
	if (!nskb)
		return nskb;

	/* Move the Ethernet DA and SA */
	memmove(nskb->data - ETH_HLEN,
		nskb->data - ETH_HLEN - BRCM_TAG_LEN,
		2 * ETH_ALEN);

	return nskb;
}

static const struct dsa_device_ops brcm_netdev_ops = {
	.name	= "brcm",
	.proto	= DSA_TAG_PROTO_BRCM,
	.xmit	= brcm_tag_xmit,
	.rcv	= brcm_tag_rcv,
	.overhead = BRCM_TAG_LEN,
};

DSA_TAG_DRIVER(brcm_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_BRCM);
#endif

#if IS_ENABLED(CONFIG_NET_DSA_TAG_BRCM_PREPEND)
static struct sk_buff *brcm_tag_xmit_prepend(struct sk_buff *skb,
					     struct net_device *dev)
{
	/* tag is prepended to the packet */
	return brcm_tag_xmit_ll(skb, dev, 0);
}

static struct sk_buff *brcm_tag_rcv_prepend(struct sk_buff *skb,
					    struct net_device *dev,
					    struct packet_type *pt)
{
	/* tag is prepended to the packet */
	return brcm_tag_rcv_ll(skb, dev, pt, ETH_HLEN);
}

static const struct dsa_device_ops brcm_prepend_netdev_ops = {
	.name	= "brcm-prepend",
	.proto	= DSA_TAG_PROTO_BRCM_PREPEND,
	.xmit	= brcm_tag_xmit_prepend,
	.rcv	= brcm_tag_rcv_prepend,
	.overhead = BRCM_TAG_LEN,
};

DSA_TAG_DRIVER(brcm_prepend_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_BRCM_PREPEND);
#endif

static struct dsa_tag_driver *dsa_tag_driver_array[] =	{
#if IS_ENABLED(CONFIG_NET_DSA_TAG_BRCM)
	&DSA_TAG_DRIVER_NAME(brcm_netdev_ops),
#endif
#if IS_ENABLED(CONFIG_NET_DSA_TAG_BRCM_PREPEND)
	&DSA_TAG_DRIVER_NAME(brcm_prepend_netdev_ops),
#endif
};

module_dsa_tag_drivers(dsa_tag_driver_array);

MODULE_LICENSE("GPL");
