// SPDX-License-Identifier: GPL-2.0+
/*
 * net/dsa/tag_ksz.c - Microchip KSZ Switch tag format handling
 * Copyright (c) 2017 Microchip Technology
 */

#include <linux/dsa/ksz_common.h>
#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/ptp_classify.h>
#include <net/dsa.h>

#include "tag.h"

#define KSZ8795_NAME "ksz8795"
#define KSZ9477_NAME "ksz9477"
#define KSZ9893_NAME "ksz9893"
#define LAN937X_NAME "lan937x"

/* Typically only one byte is used for tail tag. */
#define KSZ_PTP_TAG_LEN			4
#define KSZ_EGRESS_TAG_LEN		1
#define KSZ_INGRESS_TAG_LEN		1

#define KSZ_HWTS_EN  0

struct ksz_tagger_private {
	struct ksz_tagger_data data; /* Must be first */
	unsigned long state;
	struct kthread_worker *xmit_worker;
};

static struct ksz_tagger_private *
ksz_tagger_private(struct dsa_switch *ds)
{
	return ds->tagger_data;
}

static void ksz_hwtstamp_set_state(struct dsa_switch *ds, bool on)
{
	struct ksz_tagger_private *priv = ksz_tagger_private(ds);

	if (on)
		set_bit(KSZ_HWTS_EN, &priv->state);
	else
		clear_bit(KSZ_HWTS_EN, &priv->state);
}

static void ksz_disconnect(struct dsa_switch *ds)
{
	struct ksz_tagger_private *priv = ds->tagger_data;

	kthread_destroy_worker(priv->xmit_worker);
	kfree(priv);
	ds->tagger_data = NULL;
}

static int ksz_connect(struct dsa_switch *ds)
{
	struct ksz_tagger_data *tagger_data;
	struct kthread_worker *xmit_worker;
	struct ksz_tagger_private *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	xmit_worker = kthread_create_worker(0, "dsa%d:%d_xmit",
					    ds->dst->index, ds->index);
	if (IS_ERR(xmit_worker)) {
		ret = PTR_ERR(xmit_worker);
		kfree(priv);
		return ret;
	}

	priv->xmit_worker = xmit_worker;
	/* Export functions for switch driver use */
	tagger_data = &priv->data;
	tagger_data->hwtstamp_set_state = ksz_hwtstamp_set_state;
	ds->tagger_data = priv;

	return 0;
}

static struct sk_buff *ksz_common_rcv(struct sk_buff *skb,
				      struct net_device *dev,
				      unsigned int port, unsigned int len)
{
	skb->dev = dsa_conduit_find_user(dev, 0, port);
	if (!skb->dev)
		return NULL;

	if (pskb_trim_rcsum(skb, skb->len - len))
		return NULL;

	dsa_default_offload_fwd_mark(skb);

	return skb;
}

/*
 * For Ingress (Host -> KSZ8795), 1 byte is added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|tag(1byte)|FCS(4bytes)
 * ---------------------------------------------------------------------------
 * tag : each bit represents port (eg, 0x01=port1, 0x02=port2, 0x10=port5)
 *
 * For Egress (KSZ8795 -> Host), 1 byte is added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|tag0(1byte)|FCS(4bytes)
 * ---------------------------------------------------------------------------
 * tag0 : zero-based value represents port
 *	  (eg, 0x0=port1, 0x2=port3, 0x3=port4)
 */

#define KSZ8795_TAIL_TAG_EG_PORT_M	GENMASK(1, 0)
#define KSZ8795_TAIL_TAG_OVERRIDE	BIT(6)
#define KSZ8795_TAIL_TAG_LOOKUP		BIT(7)

static struct sk_buff *ksz8795_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_user_to_port(dev);
	struct ethhdr *hdr;
	u8 *tag;

	if (skb->ip_summed == CHECKSUM_PARTIAL && skb_checksum_help(skb))
		return NULL;

	/* Tag encoding */
	tag = skb_put(skb, KSZ_INGRESS_TAG_LEN);
	hdr = skb_eth_hdr(skb);

	*tag = 1 << dp->index;
	if (is_link_local_ether_addr(hdr->h_dest))
		*tag |= KSZ8795_TAIL_TAG_OVERRIDE;

	return skb;
}

static struct sk_buff *ksz8795_rcv(struct sk_buff *skb, struct net_device *dev)
{
	u8 *tag = skb_tail_pointer(skb) - KSZ_EGRESS_TAG_LEN;

	return ksz_common_rcv(skb, dev, tag[0] & KSZ8795_TAIL_TAG_EG_PORT_M,
			      KSZ_EGRESS_TAG_LEN);
}

static const struct dsa_device_ops ksz8795_netdev_ops = {
	.name	= KSZ8795_NAME,
	.proto	= DSA_TAG_PROTO_KSZ8795,
	.xmit	= ksz8795_xmit,
	.rcv	= ksz8795_rcv,
	.needed_tailroom = KSZ_INGRESS_TAG_LEN,
};

DSA_TAG_DRIVER(ksz8795_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_KSZ8795, KSZ8795_NAME);

/*
 * For Ingress (Host -> KSZ9477), 2/6 bytes are added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|ts(4bytes)|tag0(1byte)|tag1(1byte)|
 * FCS(4bytes)
 * ---------------------------------------------------------------------------
 * ts   : time stamp (Present only if PTP is enabled in the Hardware)
 * tag0 : Prioritization (not used now)
 * tag1 : each bit represents port (eg, 0x01=port1, 0x02=port2, 0x10=port5)
 *
 * For Egress (KSZ9477 -> Host), 1/5 bytes is added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|ts(4bytes)|tag0(1byte)|FCS(4bytes)
 * ---------------------------------------------------------------------------
 * ts   : time stamp (Present only if bit 7 of tag0 is set)
 * tag0 : zero-based value represents port
 *	  (eg, 0x00=port1, 0x02=port3, 0x06=port7)
 */

#define KSZ9477_INGRESS_TAG_LEN		2
#define KSZ9477_PTP_TAG_LEN		4
#define KSZ9477_PTP_TAG_INDICATION	BIT(7)

#define KSZ9477_TAIL_TAG_EG_PORT_M	GENMASK(2, 0)
#define KSZ9477_TAIL_TAG_PRIO		GENMASK(8, 7)
#define KSZ9477_TAIL_TAG_OVERRIDE	BIT(9)
#define KSZ9477_TAIL_TAG_LOOKUP		BIT(10)

static void ksz_rcv_timestamp(struct sk_buff *skb, u8 *tag)
{
	u8 *tstamp_raw = tag - KSZ_PTP_TAG_LEN;
	ktime_t tstamp;

	tstamp = ksz_decode_tstamp(get_unaligned_be32(tstamp_raw));
	KSZ_SKB_CB(skb)->tstamp = tstamp;
}

/* Time stamp tag *needs* to be inserted if PTP is enabled in hardware.
 * Regardless of Whether it is a PTP frame or not.
 */
static void ksz_xmit_timestamp(struct dsa_port *dp, struct sk_buff *skb)
{
	struct ksz_tagger_private *priv;
	struct ptp_header *ptp_hdr;
	unsigned int ptp_type;
	u32 tstamp_raw = 0;
	s64 correction;

	priv = ksz_tagger_private(dp->ds);

	if (!test_bit(KSZ_HWTS_EN, &priv->state))
		return;

	if (!KSZ_SKB_CB(skb)->update_correction)
		goto output_tag;

	ptp_type = KSZ_SKB_CB(skb)->ptp_type;

	ptp_hdr = ptp_parse_header(skb, ptp_type);
	if (!ptp_hdr)
		goto output_tag;

	correction = (s64)get_unaligned_be64(&ptp_hdr->correction);

	if (correction < 0) {
		struct timespec64 ts;

		ts = ns_to_timespec64(-correction >> 16);
		tstamp_raw = ((ts.tv_sec & 3) << 30) | ts.tv_nsec;

		/* Set correction field to 0 and update UDP checksum */
		ptp_header_update_correction(skb, ptp_type, ptp_hdr, 0);
	}

output_tag:
	put_unaligned_be32(tstamp_raw, skb_put(skb, KSZ_PTP_TAG_LEN));
}

/* Defer transmit if waiting for egress time stamp is required.  */
static struct sk_buff *ksz_defer_xmit(struct dsa_port *dp, struct sk_buff *skb)
{
	struct ksz_tagger_data *tagger_data = ksz_tagger_data(dp->ds);
	struct ksz_tagger_private *priv = ksz_tagger_private(dp->ds);
	void (*xmit_work_fn)(struct kthread_work *work);
	struct sk_buff *clone = KSZ_SKB_CB(skb)->clone;
	struct ksz_deferred_xmit_work *xmit_work;
	struct kthread_worker *xmit_worker;

	if (!clone)
		return skb;  /* no deferred xmit for this packet */

	xmit_work_fn = tagger_data->xmit_work_fn;
	xmit_worker = priv->xmit_worker;

	if (!xmit_work_fn || !xmit_worker)
		return NULL;

	xmit_work = kzalloc(sizeof(*xmit_work), GFP_ATOMIC);
	if (!xmit_work)
		return NULL;

	kthread_init_work(&xmit_work->work, xmit_work_fn);
	/* Increase refcount so the kfree_skb in dsa_user_xmit
	 * won't really free the packet.
	 */
	xmit_work->dp = dp;
	xmit_work->skb = skb_get(skb);

	kthread_queue_work(xmit_worker, &xmit_work->work);

	return NULL;
}

static struct sk_buff *ksz9477_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	u16 queue_mapping = skb_get_queue_mapping(skb);
	u8 prio = netdev_txq_to_tc(dev, queue_mapping);
	struct dsa_port *dp = dsa_user_to_port(dev);
	struct ethhdr *hdr;
	__be16 *tag;
	u16 val;

	if (skb->ip_summed == CHECKSUM_PARTIAL && skb_checksum_help(skb))
		return NULL;

	/* Tag encoding */
	ksz_xmit_timestamp(dp, skb);

	tag = skb_put(skb, KSZ9477_INGRESS_TAG_LEN);
	hdr = skb_eth_hdr(skb);

	val = BIT(dp->index);

	val |= FIELD_PREP(KSZ9477_TAIL_TAG_PRIO, prio);

	if (is_link_local_ether_addr(hdr->h_dest))
		val |= KSZ9477_TAIL_TAG_OVERRIDE;

	if (dev->features & NETIF_F_HW_HSR_DUP) {
		struct net_device *hsr_dev = dp->hsr_dev;
		struct dsa_port *other_dp;

		dsa_hsr_foreach_port(other_dp, dp->ds, hsr_dev)
			val |= BIT(other_dp->index);
	}

	*tag = cpu_to_be16(val);

	return ksz_defer_xmit(dp, skb);
}

static struct sk_buff *ksz9477_rcv(struct sk_buff *skb, struct net_device *dev)
{
	/* Tag decoding */
	u8 *tag = skb_tail_pointer(skb) - KSZ_EGRESS_TAG_LEN;
	unsigned int port = tag[0] & KSZ9477_TAIL_TAG_EG_PORT_M;
	unsigned int len = KSZ_EGRESS_TAG_LEN;

	/* Extra 4-bytes PTP timestamp */
	if (tag[0] & KSZ9477_PTP_TAG_INDICATION) {
		ksz_rcv_timestamp(skb, tag);
		len += KSZ_PTP_TAG_LEN;
	}

	return ksz_common_rcv(skb, dev, port, len);
}

static const struct dsa_device_ops ksz9477_netdev_ops = {
	.name	= KSZ9477_NAME,
	.proto	= DSA_TAG_PROTO_KSZ9477,
	.xmit	= ksz9477_xmit,
	.rcv	= ksz9477_rcv,
	.connect = ksz_connect,
	.disconnect = ksz_disconnect,
	.needed_tailroom = KSZ9477_INGRESS_TAG_LEN + KSZ_PTP_TAG_LEN,
};

DSA_TAG_DRIVER(ksz9477_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_KSZ9477, KSZ9477_NAME);

#define KSZ9893_TAIL_TAG_PRIO		GENMASK(4, 3)
#define KSZ9893_TAIL_TAG_OVERRIDE	BIT(5)
#define KSZ9893_TAIL_TAG_LOOKUP		BIT(6)

static struct sk_buff *ksz9893_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	u16 queue_mapping = skb_get_queue_mapping(skb);
	u8 prio = netdev_txq_to_tc(dev, queue_mapping);
	struct dsa_port *dp = dsa_user_to_port(dev);
	struct ethhdr *hdr;
	u8 *tag;

	if (skb->ip_summed == CHECKSUM_PARTIAL && skb_checksum_help(skb))
		return NULL;

	/* Tag encoding */
	ksz_xmit_timestamp(dp, skb);

	tag = skb_put(skb, KSZ_INGRESS_TAG_LEN);
	hdr = skb_eth_hdr(skb);

	*tag = BIT(dp->index);

	*tag |= FIELD_PREP(KSZ9893_TAIL_TAG_PRIO, prio);

	if (is_link_local_ether_addr(hdr->h_dest))
		*tag |= KSZ9893_TAIL_TAG_OVERRIDE;

	return ksz_defer_xmit(dp, skb);
}

static const struct dsa_device_ops ksz9893_netdev_ops = {
	.name	= KSZ9893_NAME,
	.proto	= DSA_TAG_PROTO_KSZ9893,
	.xmit	= ksz9893_xmit,
	.rcv	= ksz9477_rcv,
	.connect = ksz_connect,
	.disconnect = ksz_disconnect,
	.needed_tailroom = KSZ_INGRESS_TAG_LEN + KSZ_PTP_TAG_LEN,
};

DSA_TAG_DRIVER(ksz9893_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_KSZ9893, KSZ9893_NAME);

/* For xmit, 2/6 bytes are added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|ts(4bytes)|tag0(1byte)|tag1(1byte)|
 * FCS(4bytes)
 * ---------------------------------------------------------------------------
 * ts   : time stamp (Present only if PTP is enabled in the Hardware)
 * tag0 : represents tag override, lookup and valid
 * tag1 : each bit represents port (eg, 0x01=port1, 0x02=port2, 0x80=port8)
 *
 * For rcv, 1/5 bytes is added before FCS.
 * ---------------------------------------------------------------------------
 * DA(6bytes)|SA(6bytes)|....|Data(nbytes)|ts(4bytes)|tag0(1byte)|FCS(4bytes)
 * ---------------------------------------------------------------------------
 * ts   : time stamp (Present only if bit 7 of tag0 is set)
 * tag0 : zero-based value represents port
 *	  (eg, 0x00=port1, 0x02=port3, 0x07=port8)
 */
#define LAN937X_EGRESS_TAG_LEN		2

#define LAN937X_TAIL_TAG_BLOCKING_OVERRIDE	BIT(11)
#define LAN937X_TAIL_TAG_LOOKUP			BIT(12)
#define LAN937X_TAIL_TAG_VALID			BIT(13)
#define LAN937X_TAIL_TAG_PRIO			GENMASK(10, 8)
#define LAN937X_TAIL_TAG_PORT_MASK		7

static struct sk_buff *lan937x_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	u16 queue_mapping = skb_get_queue_mapping(skb);
	u8 prio = netdev_txq_to_tc(dev, queue_mapping);
	struct dsa_port *dp = dsa_user_to_port(dev);
	const struct ethhdr *hdr = eth_hdr(skb);
	__be16 *tag;
	u16 val;

	if (skb->ip_summed == CHECKSUM_PARTIAL && skb_checksum_help(skb))
		return NULL;

	ksz_xmit_timestamp(dp, skb);

	tag = skb_put(skb, LAN937X_EGRESS_TAG_LEN);

	val = BIT(dp->index);

	val |= FIELD_PREP(LAN937X_TAIL_TAG_PRIO, prio);

	if (is_link_local_ether_addr(hdr->h_dest))
		val |= LAN937X_TAIL_TAG_BLOCKING_OVERRIDE;

	/* Tail tag valid bit - This bit should always be set by the CPU */
	val |= LAN937X_TAIL_TAG_VALID;

	put_unaligned_be16(val, tag);

	return ksz_defer_xmit(dp, skb);
}

static const struct dsa_device_ops lan937x_netdev_ops = {
	.name	= LAN937X_NAME,
	.proto	= DSA_TAG_PROTO_LAN937X,
	.xmit	= lan937x_xmit,
	.rcv	= ksz9477_rcv,
	.connect = ksz_connect,
	.disconnect = ksz_disconnect,
	.needed_tailroom = LAN937X_EGRESS_TAG_LEN + KSZ_PTP_TAG_LEN,
};

DSA_TAG_DRIVER(lan937x_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_LAN937X, LAN937X_NAME);

static struct dsa_tag_driver *dsa_tag_driver_array[] = {
	&DSA_TAG_DRIVER_NAME(ksz8795_netdev_ops),
	&DSA_TAG_DRIVER_NAME(ksz9477_netdev_ops),
	&DSA_TAG_DRIVER_NAME(ksz9893_netdev_ops),
	&DSA_TAG_DRIVER_NAME(lan937x_netdev_ops),
};

module_dsa_tag_drivers(dsa_tag_driver_array);

MODULE_DESCRIPTION("DSA tag driver for Microchip 8795/937x/9477/9893 families of switches");
MODULE_LICENSE("GPL");
