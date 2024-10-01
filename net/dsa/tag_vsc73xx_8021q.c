// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright (C) 2024 Pawel Dembicki <paweldembicki@gmail.com>
 */
#include <linux/dsa/8021q.h>

#include "tag.h"
#include "tag_8021q.h"

#define VSC73XX_8021Q_NAME "vsc73xx-8021q"

static struct sk_buff *
vsc73xx_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct dsa_port *dp = dsa_user_to_port(netdev);
	u16 queue_mapping = skb_get_queue_mapping(skb);
	u16 tx_vid = dsa_tag_8021q_standalone_vid(dp);
	u8 pcp;

	if (skb->offload_fwd_mark) {
		unsigned int bridge_num = dsa_port_bridge_num_get(dp);
		struct net_device *br = dsa_port_bridge_dev_get(dp);

		if (br_vlan_enabled(br))
			return skb;

		tx_vid = dsa_tag_8021q_bridge_vid(bridge_num);
	}

	pcp = netdev_txq_to_tc(netdev, queue_mapping);

	return dsa_8021q_xmit(skb, netdev, ETH_P_8021Q,
			      ((pcp << VLAN_PRIO_SHIFT) | tx_vid));
}

static struct sk_buff *
vsc73xx_rcv(struct sk_buff *skb, struct net_device *netdev)
{
	int src_port = -1, switch_id = -1, vbid = -1, vid = -1;

	dsa_8021q_rcv(skb, &src_port, &switch_id, &vbid, &vid);

	skb->dev = dsa_tag_8021q_find_user(netdev, src_port, switch_id,
					   vid, vbid);
	if (!skb->dev) {
		dev_warn_ratelimited(&netdev->dev,
				     "Couldn't decode source port\n");
		return NULL;
	}

	dsa_default_offload_fwd_mark(skb);

	return skb;
}

static const struct dsa_device_ops vsc73xx_8021q_netdev_ops = {
	.name			= VSC73XX_8021Q_NAME,
	.proto			= DSA_TAG_PROTO_VSC73XX_8021Q,
	.xmit			= vsc73xx_xmit,
	.rcv			= vsc73xx_rcv,
	.needed_headroom	= VLAN_HLEN,
	.promisc_on_conduit	= true,
};

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DSA tag driver for VSC73XX family of switches, using VLAN");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_VSC73XX_8021Q, VSC73XX_8021Q_NAME);

module_dsa_tag_driver(vsc73xx_8021q_netdev_ops);
