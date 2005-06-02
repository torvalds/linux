/*
 *	Spanning tree protocol; BPDU handling
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_stp_bpdu.c,v 1.3 2001/11/10 02:35:25 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/netfilter_bridge.h>

#include "br_private.h"
#include "br_private_stp.h"

#define JIFFIES_TO_TICKS(j) (((j) << 8) / HZ)
#define TICKS_TO_JIFFIES(j) (((j) * HZ) >> 8)

static void br_send_bpdu(struct net_bridge_port *p, unsigned char *data, int length)
{
	struct net_device *dev;
	struct sk_buff *skb;
	int size;

	if (!p->br->stp_enabled)
		return;

	size = length + 2*ETH_ALEN + 2;
	if (size < 60)
		size = 60;

	dev = p->dev;

	if ((skb = dev_alloc_skb(size)) == NULL) {
		printk(KERN_INFO "br: memory squeeze!\n");
		return;
	}

	skb->dev = dev;
	skb->protocol = htons(ETH_P_802_2);
	skb->mac.raw = skb_put(skb, size);
	memcpy(skb->mac.raw, bridge_ula, ETH_ALEN);
	memcpy(skb->mac.raw+ETH_ALEN, dev->dev_addr, ETH_ALEN);
	skb->mac.raw[2*ETH_ALEN] = 0;
	skb->mac.raw[2*ETH_ALEN+1] = length;
	skb->nh.raw = skb->mac.raw + 2*ETH_ALEN + 2;
	memcpy(skb->nh.raw, data, length);
	memset(skb->nh.raw + length, 0xa5, size - length - 2*ETH_ALEN - 2);

	NF_HOOK(PF_BRIDGE, NF_BR_LOCAL_OUT, skb, NULL, skb->dev,
		dev_queue_xmit);
}

static __inline__ void br_set_ticks(unsigned char *dest, int jiff)
{
	__u16 ticks;

	ticks = JIFFIES_TO_TICKS(jiff);
	dest[0] = (ticks >> 8) & 0xFF;
	dest[1] = ticks & 0xFF;
}

static __inline__ int br_get_ticks(unsigned char *dest)
{
	return TICKS_TO_JIFFIES((dest[0] << 8) | dest[1]);
}

/* called under bridge lock */
void br_send_config_bpdu(struct net_bridge_port *p, struct br_config_bpdu *bpdu)
{
	unsigned char buf[38];

	buf[0] = 0x42;
	buf[1] = 0x42;
	buf[2] = 0x03;
	buf[3] = 0;
	buf[4] = 0;
	buf[5] = 0;
	buf[6] = BPDU_TYPE_CONFIG;
	buf[7] = (bpdu->topology_change ? 0x01 : 0) |
		(bpdu->topology_change_ack ? 0x80 : 0);
	buf[8] = bpdu->root.prio[0];
	buf[9] = bpdu->root.prio[1];
	buf[10] = bpdu->root.addr[0];
	buf[11] = bpdu->root.addr[1];
	buf[12] = bpdu->root.addr[2];
	buf[13] = bpdu->root.addr[3];
	buf[14] = bpdu->root.addr[4];
	buf[15] = bpdu->root.addr[5];
	buf[16] = (bpdu->root_path_cost >> 24) & 0xFF;
	buf[17] = (bpdu->root_path_cost >> 16) & 0xFF;
	buf[18] = (bpdu->root_path_cost >> 8) & 0xFF;
	buf[19] = bpdu->root_path_cost & 0xFF;
	buf[20] = bpdu->bridge_id.prio[0];
	buf[21] = bpdu->bridge_id.prio[1];
	buf[22] = bpdu->bridge_id.addr[0];
	buf[23] = bpdu->bridge_id.addr[1];
	buf[24] = bpdu->bridge_id.addr[2];
	buf[25] = bpdu->bridge_id.addr[3];
	buf[26] = bpdu->bridge_id.addr[4];
	buf[27] = bpdu->bridge_id.addr[5];
	buf[28] = (bpdu->port_id >> 8) & 0xFF;
	buf[29] = bpdu->port_id & 0xFF;

	br_set_ticks(buf+30, bpdu->message_age);
	br_set_ticks(buf+32, bpdu->max_age);
	br_set_ticks(buf+34, bpdu->hello_time);
	br_set_ticks(buf+36, bpdu->forward_delay);

	br_send_bpdu(p, buf, 38);
}

/* called under bridge lock */
void br_send_tcn_bpdu(struct net_bridge_port *p)
{
	unsigned char buf[7];

	buf[0] = 0x42;
	buf[1] = 0x42;
	buf[2] = 0x03;
	buf[3] = 0;
	buf[4] = 0;
	buf[5] = 0;
	buf[6] = BPDU_TYPE_TCN;
	br_send_bpdu(p, buf, 7);
}

static const unsigned char header[6] = {0x42, 0x42, 0x03, 0x00, 0x00, 0x00};

/* NO locks */
int br_stp_handle_bpdu(struct sk_buff *skb)
{
	struct net_bridge_port *p = skb->dev->br_port;
	struct net_bridge *br = p->br;
	unsigned char *buf;

	/* insert into forwarding database after filtering to avoid spoofing */
	br_fdb_update(p->br, p, eth_hdr(skb)->h_source);

	/* need at least the 802 and STP headers */
	if (!pskb_may_pull(skb, sizeof(header)+1) ||
	    memcmp(skb->data, header, sizeof(header)))
		goto err;

	buf = skb_pull(skb, sizeof(header));

	spin_lock_bh(&br->lock);
	if (p->state == BR_STATE_DISABLED 
	    || !(br->dev->flags & IFF_UP)
	    || !br->stp_enabled)
		goto out;

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
	spin_unlock_bh(&br->lock);
 err:
	kfree_skb(skb);
	return 0;
}
