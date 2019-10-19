// SPDX-License-Identifier: GPL-2.0
/*
 * This module tests the blackhole_dev that is created during the
 * net subsystem initialization. The test this module performs is
 * by injecting an skb into the stack with skb->dev as the
 * blackhole_dev and expects kernel to behave in a sane manner
 * (in other words, *not crash*)!
 *
 * Copyright (c) 2018, Mahesh Bandewar <maheshb@google.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/udp.h>
#include <linux/ipv6.h>

#include <net/dst.h>

#define SKB_SIZE  256
#define HEAD_SIZE (14+40+8)	/* Ether + IPv6 + UDP */
#define TAIL_SIZE 32		/* random tail-room */

#define UDP_PORT 1234

static int __init test_blackholedev_init(void)
{
	struct ipv6hdr *ip6h;
	struct sk_buff *skb;
	struct ethhdr *ethh;
	struct udphdr *uh;
	int data_len;
	int ret;

	skb = alloc_skb(SKB_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	/* Reserve head-room for the headers */
	skb_reserve(skb, HEAD_SIZE);

	/* Add data to the skb */
	data_len = SKB_SIZE - (HEAD_SIZE + TAIL_SIZE);
	memset(__skb_put(skb, data_len), 0xf, data_len);

	/* Add protocol data */
	/* (Transport) UDP */
	uh = (struct udphdr *)skb_push(skb, sizeof(struct udphdr));
	skb_set_transport_header(skb, 0);
	uh->source = uh->dest = htons(UDP_PORT);
	uh->len = htons(data_len);
	uh->check = 0;
	/* (Network) IPv6 */
	ip6h = (struct ipv6hdr *)skb_push(skb, sizeof(struct ipv6hdr));
	skb_set_network_header(skb, 0);
	ip6h->hop_limit = 32;
	ip6h->payload_len = data_len + sizeof(struct udphdr);
	ip6h->nexthdr = IPPROTO_UDP;
	ip6h->saddr = in6addr_loopback;
	ip6h->daddr = in6addr_loopback;
	/* Ether */
	ethh = (struct ethhdr *)skb_push(skb, sizeof(struct ethhdr));
	skb_set_mac_header(skb, 0);

	skb->protocol = htons(ETH_P_IPV6);
	skb->pkt_type = PACKET_HOST;
	skb->dev = blackhole_netdev;

	/* Now attempt to send the packet */
	ret = dev_queue_xmit(skb);

	switch (ret) {
	case NET_XMIT_SUCCESS:
		pr_warn("dev_queue_xmit() returned NET_XMIT_SUCCESS\n");
		break;
	case NET_XMIT_DROP:
		pr_warn("dev_queue_xmit() returned NET_XMIT_DROP\n");
		break;
	case NET_XMIT_CN:
		pr_warn("dev_queue_xmit() returned NET_XMIT_CN\n");
		break;
	default:
		pr_err("dev_queue_xmit() returned UNKNOWN(%d)\n", ret);
	}

	return 0;
}

static void __exit test_blackholedev_exit(void)
{
	pr_warn("test_blackholedev module terminating.\n");
}

module_init(test_blackholedev_init);
module_exit(test_blackholedev_exit);

MODULE_AUTHOR("Mahesh Bandewar <maheshb@google.com>");
MODULE_LICENSE("GPL");
