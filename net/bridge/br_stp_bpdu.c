/*
 *	Spanning tree protocol; BPDU handling
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/netfilter_bridge.h>
#include <linux/etherdevice.h>
#include <linux/llc.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <net/llc.h>
#include <net/llc_pdu.h>
#include <net/stp.h>
#include <asm/unaligned.h>

#include "br_private.h"
#include "br_private_stp.h"

#define STP_HZ		256

#define LLC_RESERVE sizeof(struct llc_pdu_un)

static void br_send_bpdu(struct net_bridge_port *p,
			 const unsigned char *data, int length)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(length+LLC_RESERVE);
	if (!skb)
		return;

	skb->dev = p->dev;
	skb->protocol = htons(ETH_P_802_2);

	skb_reserve(skb, LLC_RESERVE);
	memcpy(__skb_put(skb, length), data, length);

	llc_pdu_header_init(skb, LLC_PDU_TYPE_U, LLC_SAP_BSPAN,
			    LLC_SAP_BSPAN, LLC_PDU_CMD);
	llc_pdu_init_as_ui_cmd(skb);

	llc_mac_hdr_init(skb, p->dev->dev_addr, p->br->group_addr);

	NF_HOOK(NFPROTO_BRIDGE, NF_BR_LOCAL_OUT, skb, NULL, skb->dev,
		dev_queue_xmit);
}

static inline void br_set_ticks(unsigned char *dest, int j)
{
	unsigned long ticks = (STP_HZ * j)/ HZ;

	put_unaligned_be16(ticks, dest);
}

static inline int br_get_ticks(const unsigned char *src)
{
	unsigned long ticks = get_unaligned_be16(src);

	return DIV_ROUND_UP(ticks * HZ, STP_HZ);
}

/* called under bridge lock */
void br_send_config_bpdu(struct net_bridge_port *p, struct br_config_bpdu *bpdu)
{
	unsigned char buf[35];

	if (p->br->stp_enabled != BR_KERNEL_STP)
		return;

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = BPDU_TYPE_CONFIG;
	buf[4] = (bpdu->topology_change ? 0x01 : 0) |
		(bpdu->topology_change_ack ? 0x80 : 0);
	buf[5] = bpdu->root.prio[0];
	buf[6] = bpdu->root.prio[1];
	buf[7] = bpdu->root.addr[0];
	buf[8] = bpdu->root.addr[1];
	buf[9] = bpdu->root.addr[2];
	buf[10] = bpdu->root.addr[3];
	buf[11] = bpdu->root.addr[4];
	buf[12] = bpdu->root.addr[5];
	buf[13] = (bpdu->root_path_cost >> 24) & 0xFF;
	buf[14] = (bpdu->root_path_cost >> 16) & 0xFF;
	buf[15] = (bpdu->root_path_cost >> 8) & 0xFF;
	buf[16] = bpdu->root_path_cost & 0xFF;
	buf[17] = bpdu->bridge_id.prio[0];
	buf[18] = bpdu->bridge_id.prio[1];
	buf[19] = bpdu->bridge_id.addr[0];
	buf[20] = bpdu->bridge_id.addr[1];
	buf[21] = bpdu->bridge_id.addr[2];
	buf[22] = bpdu->bridge_id.addr[3];
	buf[23] = bpdu->bridge_id.addr[4];
	buf[24] = bpdu->bridge_id.addr[5];
	buf[25] = (bpdu->port_id >> 8) & 0xFF;
	buf[26] = bpdu->port_id & 0xFF;

	br_set_ticks(buf+27, bpdu->message_age);
	br_set_ticks(buf+29, bpdu->max_age);
	br_set_ticks(buf+31, bpdu->hello_time);
	br_set_ticks(buf+33, bpdu->forward_delay);

	br_send_bpdu(p, buf, 35);
}

/* called under bridge lock */
void br_send_tcn_bpdu(struct net_bridge_port *p)
{
	unsigned char buf[4];

	if (p->br->stp_enabled != BR_KERNEL_STP)
		return;

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = BPDU_TYPE_TCN;
	br_send_bpdu(p, buf, 4);
}

/*
 * Called from llc.
 *
 * NO locks, but rcu_read_lock
 */
void br_stp_rcv(const struct stp_proto *proto, struct sk_buff *skb,
		struct net_device *dev)
{
	const unsigned char *dest = eth_hdr(skb)->h_dest;
	struct net_bridge_port *p;
	struct net_bridge *br;
	const unsigned char *buf;

	if (!br_port_exists(dev))
		goto err;
	p = br_port_get_rcu(dev);

	if (!pskb_may_pull(skb, 4))
		goto err;

	/* compare of protocol id and version */
	buf = skb->data;
	if (buf[0] != 0 || buf[1] != 0 || buf[2] != 0)
		goto err;

	br = p->br;
	spin_lock(&br->lock);

	if (br->stp_enabled != BR_KERNEL_STP)
		goto out;

	if (!(br->dev->flags & IFF_UP))
		goto out;

	if (p->state == BR_STATE_DISABLED)
		goto out;

	if (compare_ether_addr(dest, br->group_addr) != 0)
		goto out;

	buf = skb_pull(skb, 3);

	if (buf[0] == BPDU_TYPE_CONFIG) {
		struct br_config_bpdu bpdu;

		if (!pskb_may_pull(skb, 32))
			goto out;

		buf = skb->data;
		bpdu.topology_change = (buf[1] & 0x01) ? 1 : 0;
		bpdu.topology_change_ack = (buf[1] & 0x80) ? 1 : 0;

		bpdu.root.prio[0] = buf[2];
		bpdu.root.prio[1] = buf[3];
		bpdu.root.addr[0] = buf[4];
		bpdu.root.addr[1] = buf[5];
		bpdu.root.addr[2] = buf[6];
		bpdu.root.addr[3] = buf[7];
		bpdu.root.addr[4] = buf[8];
		bpdu.root.addr[5] = buf[9];
		bpdu.root_path_cost =
			(buf[10] << 24) |
			(buf[11] << 16) |
			(buf[12] << 8) |
			buf[13];
		bpdu.bridge_id.prio[0] = buf[14];
		bpdu.bridge_id.prio[1] = buf[15];
		bpdu.bridge_id.addr[0] = buf[16];
		bpdu.bridge_id.addr[1] = buf[17];
		bpdu.bridge_id.addr[2] = buf[18];
		bpdu.bridge_id.addr[3] = buf[19];
		bpdu.bridge_id.addr[4] = buf[20];
		bpdu.bridge_id.addr[5] = buf[21];
		bpdu.port_id = (buf[22] << 8) | buf[23];

		bpdu.message_age = br_get_ticks(buf+24);
		bpdu.max_age = br_get_ticks(buf+26);
		bpdu.hello_time = br_get_ticks(buf+28);
		bpdu.forward_delay = br_get_ticks(buf+30);

		br_received_config_bpdu(p, &bpdu);
	}

	else if (buf[0] == BPDU_TYPE_TCN) {
		br_received_tcn_bpdu(p);
	}
 out:
	spin_unlock(&br->lock);
 err:
	kfree_skb(skb);
}
