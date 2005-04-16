#ifndef _X25DEVICE_H
#define _X25DEVICE_H

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/skbuff.h>

static inline unsigned short x25_type_trans(struct sk_buff *skb,
					    struct net_device *dev)
{
	skb->mac.raw = skb->data;
	skb->input_dev = skb->dev = dev;
	skb->pkt_type = PACKET_HOST;
	
	return htons(ETH_P_X25);
}
#endif
