/*
 * Copyright (c) 2013 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/mroute.h>
#include <linux/init.h>
#include <linux/in6.h>
#include <linux/inetdevice.h>
#include <linux/netfilter_ipv4.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>

#include <net/ip.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/ip_tunnels.h>
#include <net/arp.h>
#include <net/checksum.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>

int iptunnel_xmit(struct sock *sk, struct rtable *rt, struct sk_buff *skb,
		  __be32 src, __be32 dst, __u8 proto,
		  __u8 tos, __u8 ttl, __be16 df, bool xnet)
{
	int pkt_len = skb->len;
	struct iphdr *iph;
	int err;

	skb_scrub_packet(skb, xnet);

	skb_clear_hash(skb);
	skb_dst_set(skb, &rt->dst);
	memset(IPCB(skb), 0, sizeof(*IPCB(skb)));

	/* Push down and install the IP header. */
	skb_push(skb, sizeof(struct iphdr));
	skb_reset_network_header(skb);

	iph = ip_hdr(skb);

	iph->version	=	4;
	iph->ihl	=	sizeof(struct iphdr) >> 2;
	iph->frag_off	=	df;
	iph->protocol	=	proto;
	iph->tos	=	tos;
	iph->daddr	=	dst;
	iph->saddr	=	src;
	iph->ttl	=	ttl;
	__ip_select_ident(iph, skb_shinfo(skb)->gso_segs ?: 1);

	err = ip_local_out_sk(sk, skb);
	if (unlikely(net_xmit_eval(err)))
		pkt_len = 0;
	return pkt_len;
}
EXPORT_SYMBOL_GPL(iptunnel_xmit);

int iptunnel_pull_header(struct sk_buff *skb, int hdr_len, __be16 inner_proto)
{
	if (unlikely(!pskb_may_pull(skb, hdr_len)))
		return -ENOMEM;

	skb_pull_rcsum(skb, hdr_len);

	if (inner_proto == htons(ETH_P_TEB)) {
		struct ethhdr *eh = (struct ethhdr *)skb->data;

		if (unlikely(!pskb_may_pull(skb, ETH_HLEN)))
			return -ENOMEM;

		if (likely(ntohs(eh->h_proto) >= ETH_P_802_3_MIN))
			skb->protocol = eh->h_proto;
		else
			skb->protocol = htons(ETH_P_802_2);

	} else {
		skb->protocol = inner_proto;
	}

	nf_reset(skb);
	secpath_reset(skb);
	skb_clear_hash_if_not_l4(skb);
	skb_dst_drop(skb);
	skb->vlan_tci = 0;
	skb_set_queue_mapping(skb, 0);
	skb->pkt_type = PACKET_HOST;
	return 0;
}
EXPORT_SYMBOL_GPL(iptunnel_pull_header);

struct sk_buff *iptunnel_handle_offloads(struct sk_buff *skb,
					 bool csum_help,
					 int gso_type_mask)
{
	int err;

	if (likely(!skb->encapsulation)) {
		skb_reset_inner_headers(skb);
		skb->encapsulation = 1;
	}

	if (skb_is_gso(skb)) {
		err = skb_unclone(skb, GFP_ATOMIC);
		if (unlikely(err))
			goto error;
		skb_shinfo(skb)->gso_type |= gso_type_mask;
		return skb;
	}

	/* If packet is not gso and we are resolving any partial checksum,
	 * clear encapsulation flag. This allows setting CHECKSUM_PARTIAL
	 * on the outer header without confusing devices that implement
	 * NETIF_F_IP_CSUM with encapsulation.
	 */
	if (csum_help)
		skb->encapsulation = 0;

	if (skb->ip_summed == CHECKSUM_PARTIAL && csum_help) {
		err = skb_checksum_help(skb);
		if (unlikely(err))
			goto error;
	} else if (skb->ip_summed != CHECKSUM_PARTIAL)
		skb->ip_summed = CHECKSUM_NONE;

	return skb;
error:
	kfree_skb(skb);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(iptunnel_handle_offloads);

/* Often modified stats are per cpu, other are shared (netdev->stats) */
struct rtnl_link_stats64 *ip_tunnel_get_stats64(struct net_device *dev,
						struct rtnl_link_stats64 *tot)
{
	int i;

	for_each_possible_cpu(i) {
		const struct pcpu_sw_netstats *tstats =
						   per_cpu_ptr(dev->tstats, i);
		u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
		unsigned int start;

		do {
			start = u64_stats_fetch_begin_irq(&tstats->syncp);
			rx_packets = tstats->rx_packets;
			tx_packets = tstats->tx_packets;
			rx_bytes = tstats->rx_bytes;
			tx_bytes = tstats->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&tstats->syncp, start));

		tot->rx_packets += rx_packets;
		tot->tx_packets += tx_packets;
		tot->rx_bytes   += rx_bytes;
		tot->tx_bytes   += tx_bytes;
	}

	tot->multicast = dev->stats.multicast;

	tot->rx_crc_errors = dev->stats.rx_crc_errors;
	tot->rx_fifo_errors = dev->stats.rx_fifo_errors;
	tot->rx_length_errors = dev->stats.rx_length_errors;
	tot->rx_frame_errors = dev->stats.rx_frame_errors;
	tot->rx_errors = dev->stats.rx_errors;

	tot->tx_fifo_errors = dev->stats.tx_fifo_errors;
	tot->tx_carrier_errors = dev->stats.tx_carrier_errors;
	tot->tx_dropped = dev->stats.tx_dropped;
	tot->tx_aborted_errors = dev->stats.tx_aborted_errors;
	tot->tx_errors = dev->stats.tx_errors;

	tot->collisions  = dev->stats.collisions;

	return tot;
}
EXPORT_SYMBOL_GPL(ip_tunnel_get_stats64);
