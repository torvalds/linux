// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Motorcomm YT921x Switch Extended CPU Port Tagging
 *
 * Copyright (c) 2025 David Yang <mmyangfl@gmail.com>
 *
 * +----+----+-------+-----+----+---------
 * | DA | SA | TagET | Tag | ET | Payload ...
 * +----+----+-------+-----+----+---------
 *   6    6      2      6    2       N
 *
 * Tag Ethertype: CPU_TAG_TPID_TPID (default: ETH_P_YT921X = 0x9988)
 *   * Hardcoded for the moment, but still configurable. Discuss it if there
 *     are conflicts somewhere and/or you want to change it for some reason.
 * Tag:
 *   2: VLAN Tag
 *   2: Rx Port
 *     15b: Rx Port Valid
 *     14b-11b: Rx Port
 *     10b-0b: Cmd?
 *   2: Tx Port(s)
 *     15b: Tx Port(s) Valid
 *     10b-0b: Tx Port(s) Mask
 */

#include <linux/etherdevice.h>

#include "tag.h"

#define YT921X_TAG_NAME	"yt921x"

#define YT921X_TAG_LEN	8

#define YT921X_TAG_PORT_EN		BIT(15)
#define YT921X_TAG_RX_PORT_M		GENMASK(14, 11)
#define YT921X_TAG_RX_CMD_M		GENMASK(10, 0)
#define  YT921X_TAG_RX_CMD(x)			FIELD_PREP(YT921X_TAG_RX_CMD_M, (x))
#define  YT921X_TAG_RX_CMD_FORWARDED		0x80
#define  YT921X_TAG_RX_CMD_UNK_UCAST		0xb2
#define  YT921X_TAG_RX_CMD_UNK_MCAST		0xb4
#define YT921X_TAG_TX_PORTS		GENMASK(10, 0)

static struct sk_buff *
yt921x_tag_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	__be16 *tag;
	u16 tx;

	skb_push(skb, YT921X_TAG_LEN);
	dsa_alloc_etype_header(skb, YT921X_TAG_LEN);

	tag = dsa_etype_header_pos_tx(skb);

	tag[0] = htons(ETH_P_YT921X);
	/* VLAN tag unrelated when TX */
	tag[1] = 0;
	tag[2] = 0;
	tx = FIELD_PREP(YT921X_TAG_TX_PORTS, dsa_xmit_port_mask(skb, netdev)) |
	     YT921X_TAG_PORT_EN;
	tag[3] = htons(tx);

	return skb;
}

static struct sk_buff *
yt921x_tag_rcv(struct sk_buff *skb, struct net_device *netdev)
{
	unsigned int port;
	__be16 *tag;
	u16 cmd;
	u16 rx;

	if (unlikely(!pskb_may_pull(skb, YT921X_TAG_LEN)))
		return NULL;

	tag = dsa_etype_header_pos_rx(skb);

	if (unlikely(tag[0] != htons(ETH_P_YT921X))) {
		dev_warn_ratelimited(&netdev->dev,
				     "Unexpected EtherType 0x%04x\n",
				     ntohs(tag[0]));
		return NULL;
	}

	/* Locate which port this is coming from */
	rx = ntohs(tag[2]);
	if (unlikely((rx & YT921X_TAG_PORT_EN) == 0)) {
		dev_warn_ratelimited(&netdev->dev,
				     "Unexpected rx tag 0x%04x\n", rx);
		return NULL;
	}

	port = FIELD_GET(YT921X_TAG_RX_PORT_M, rx);
	skb->dev = dsa_conduit_find_user(netdev, 0, port);
	if (unlikely(!skb->dev)) {
		dev_warn_ratelimited(&netdev->dev,
				     "Couldn't decode source port %u\n", port);
		return NULL;
	}

	cmd = FIELD_GET(YT921X_TAG_RX_CMD_M, rx);
	switch (cmd) {
	case YT921X_TAG_RX_CMD_FORWARDED:
		/* Already forwarded by hardware */
		dsa_default_offload_fwd_mark(skb);
		break;
	case YT921X_TAG_RX_CMD_UNK_UCAST:
	case YT921X_TAG_RX_CMD_UNK_MCAST:
		/* NOTE: hardware doesn't distinguish between TRAP (copy to CPU
		 * only) and COPY (forward and copy to CPU). In order to perform
		 * a soft switch, NEVER use COPY action in the switch driver.
		 */
		break;
	default:
		dev_warn_ratelimited(&netdev->dev,
				     "Unexpected rx cmd 0x%02x\n", cmd);
		break;
	}

	/* Remove YT921x tag and update checksum */
	skb_pull_rcsum(skb, YT921X_TAG_LEN);
	dsa_strip_etype_header(skb, YT921X_TAG_LEN);

	return skb;
}

static const struct dsa_device_ops yt921x_netdev_ops = {
	.name	= YT921X_TAG_NAME,
	.proto	= DSA_TAG_PROTO_YT921X,
	.xmit	= yt921x_tag_xmit,
	.rcv	= yt921x_tag_rcv,
	.needed_headroom = YT921X_TAG_LEN,
};

MODULE_DESCRIPTION("DSA tag driver for Motorcomm YT921x switches");
MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_YT921X, YT921X_TAG_NAME);

module_dsa_tag_driver(yt921x_netdev_ops);
