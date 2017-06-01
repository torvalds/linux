/*
 * net/dsa/tag_ksz.c - Microchip KSZ Switch tag format handling
 * Copyright (c) 2017 Microchip Technology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <net/dsa.h>
#include "dsa_priv.h"

/* For Ingress (Host -> KSZ), 2 bytes are added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|tag0(1byte)|tag1(1byte)|FCS(4bytes)
 * ---------------------------------------------------------------------------
 * tag0 : Prioritization (not used now)
 * tag1 : each bit represents port (eg, 0x01=port1, 0x02=port2, 0x10=port5)
 *
 * For Egress (KSZ -> Host), 1 byte is added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|tag0(1byte)|FCS(4bytes)
 * ---------------------------------------------------------------------------
 * tag0 : zero-based value represents port
 *	  (eg, 0x00=port1, 0x02=port3, 0x06=port7)
 */

#define	KSZ_INGRESS_TAG_LEN	2
#define	KSZ_EGRESS_TAG_LEN	1

static struct sk_buff *ksz_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct sk_buff *nskb;
	int padlen;
	u8 *tag;

	padlen = (skb->len >= ETH_ZLEN) ? 0 : ETH_ZLEN - skb->len;

	if (skb_tailroom(skb) >= padlen + KSZ_INGRESS_TAG_LEN) {
		nskb = skb;
	} else {
		nskb = alloc_skb(NET_IP_ALIGN + skb->len +
				 padlen + KSZ_INGRESS_TAG_LEN, GFP_ATOMIC);
		if (!nskb) {
			kfree_skb(skb);
			return NULL;
		}
		skb_reserve(nskb, NET_IP_ALIGN);

		skb_reset_mac_header(nskb);
		skb_set_network_header(nskb,
				       skb_network_header(skb) - skb->head);
		skb_set_transport_header(nskb,
					 skb_transport_header(skb) - skb->head);
		skb_copy_and_csum_dev(skb, skb_put(nskb, skb->len));
		kfree_skb(skb);
	}

	/* skb is freed when it fails */
	if (skb_put_padto(nskb, nskb->len + padlen))
		return NULL;

	tag = skb_put(nskb, KSZ_INGRESS_TAG_LEN);
	tag[0] = 0;
	tag[1] = 1 << p->dp->index; /* destination port */

	return nskb;
}

static struct sk_buff *ksz_rcv(struct sk_buff *skb, struct net_device *dev,
			       struct packet_type *pt,
			       struct net_device *orig_dev)
{
	struct dsa_switch_tree *dst = dev->dsa_ptr;
	struct dsa_switch *ds;
	u8 *tag;
	int source_port;

	ds = dst->cpu_dp->ds;

	tag = skb_tail_pointer(skb) - KSZ_EGRESS_TAG_LEN;

	source_port = tag[0] & 7;
	if (source_port >= ds->num_ports || !ds->ports[source_port].netdev)
		return NULL;

	pskb_trim_rcsum(skb, skb->len - KSZ_EGRESS_TAG_LEN);

	skb->dev = ds->ports[source_port].netdev;

	return skb;
}

const struct dsa_device_ops ksz_netdev_ops = {
	.xmit	= ksz_xmit,
	.rcv	= ksz_rcv,
};
