/*
 *	Handle firewalling
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *	Bart De Schuymer		<bdschuym@pandora.be>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Lennert dedicates this file to Kerstin Wurdinger.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/if_pppox.h>
#include <linux/ppp_defs.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_arp.h>
#include <linux/in_route.h>
#include <linux/inetdevice.h>

#include <net/ip.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/route.h>
#include <net/netfilter/br_netfilter.h>

#include <linux/uaccess.h>
#include "br_private.h"
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

/* We only check the length. A bridge shouldn't do any hop-by-hop stuff
 * anyway
 */
static int br_nf_check_hbh_len(struct sk_buff *skb)
{
	unsigned char *raw = (u8 *)(ipv6_hdr(skb) + 1);
	u32 pkt_len;
	const unsigned char *nh = skb_network_header(skb);
	int off = raw - nh;
	int len = (raw[1] + 1) << 3;

	if ((raw + len) - skb->data > skb_headlen(skb))
		goto bad;

	off += 2;
	len -= 2;

	while (len > 0) {
		int optlen = nh[off + 1] + 2;

		switch (nh[off]) {
		case IPV6_TLV_PAD1:
			optlen = 1;
			break;

		case IPV6_TLV_PADN:
			break;

		case IPV6_TLV_JUMBO:
			if (nh[off + 1] != 4 || (off & 3) != 2)
				goto bad;
			pkt_len = ntohl(*(__be32 *)(nh + off + 2));
			if (pkt_len <= IPV6_MAXPLEN ||
			    ipv6_hdr(skb)->payload_len)
				goto bad;
			if (pkt_len > skb->len - sizeof(struct ipv6hdr))
				goto bad;
			if (pskb_trim_rcsum(skb,
					    pkt_len + sizeof(struct ipv6hdr)))
				goto bad;
			nh = skb_network_header(skb);
			break;
		default:
			if (optlen > len)
				goto bad;
			break;
		}
		off += optlen;
		len -= optlen;
	}
	if (len == 0)
		return 0;
bad:
	return -1;
}

int br_validate_ipv6(struct net *net, struct sk_buff *skb)
{
	const struct ipv6hdr *hdr;
	struct inet6_dev *idev = __in6_dev_get(skb->dev);
	u32 pkt_len;
	u8 ip6h_len = sizeof(struct ipv6hdr);

	if (!pskb_may_pull(skb, ip6h_len))
		goto inhdr_error;

	if (skb->len < ip6h_len)
		goto drop;

	hdr = ipv6_hdr(skb);

	if (hdr->version != 6)
		goto inhdr_error;

	pkt_len = ntohs(hdr->payload_len);

	if (pkt_len || hdr->nexthdr != NEXTHDR_HOP) {
		if (pkt_len + ip6h_len > skb->len) {
			__IP6_INC_STATS(net, idev,
					IPSTATS_MIB_INTRUNCATEDPKTS);
			goto drop;
		}
		if (pskb_trim_rcsum(skb, pkt_len + ip6h_len)) {
			__IP6_INC_STATS(net, idev,
					IPSTATS_MIB_INDISCARDS);
			goto drop;
		}
	}
	if (hdr->nexthdr == NEXTHDR_HOP && br_nf_check_hbh_len(skb))
		goto drop;

	memset(IP6CB(skb), 0, sizeof(struct inet6_skb_parm));
	/* No IP options in IPv6 header; however it should be
	 * checked if some next headers need special treatment
	 */
	return 0;

inhdr_error:
	__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
drop:
	return -1;
}

static inline bool
br_nf_ipv6_daddr_was_changed(const struct sk_buff *skb,
			     const struct nf_bridge_info *nf_bridge)
{
	return memcmp(&nf_bridge->ipv6_daddr, &ipv6_hdr(skb)->daddr,
		      sizeof(ipv6_hdr(skb)->daddr)) != 0;
}

/* PF_BRIDGE/PRE_ROUTING: Undo the changes made for ip6tables
 * PREROUTING and continue the bridge PRE_ROUTING hook. See comment
 * for br_nf_pre_routing_finish(), same logic is used here but
 * equivalent IPv6 function ip6_route_input() called indirectly.
 */
static int br_nf_pre_routing_finish_ipv6(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);
	struct rtable *rt;
	struct net_device *dev = skb->dev;
	const struct nf_ipv6_ops *v6ops = nf_get_ipv6_ops();

	nf_bridge->frag_max_size = IP6CB(skb)->frag_max_size;

	if (nf_bridge->pkt_otherhost) {
		skb->pkt_type = PACKET_OTHERHOST;
		nf_bridge->pkt_otherhost = false;
	}
	nf_bridge->in_prerouting = 0;
	if (br_nf_ipv6_daddr_was_changed(skb, nf_bridge)) {
		skb_dst_drop(skb);
		v6ops->route_input(skb);

		if (skb_dst(skb)->error) {
			kfree_skb(skb);
			return 0;
		}

		if (skb_dst(skb)->dev == dev) {
			skb->dev = nf_bridge->physindev;
			nf_bridge_update_protocol(skb);
			nf_bridge_push_encap_header(skb);
			br_nf_hook_thresh(NF_BR_PRE_ROUTING,
					  net, sk, skb, skb->dev, NULL,
					  br_nf_pre_routing_finish_bridge);
			return 0;
		}
		ether_addr_copy(eth_hdr(skb)->h_dest, dev->dev_addr);
		skb->pkt_type = PACKET_HOST;
	} else {
		rt = bridge_parent_rtable(nf_bridge->physindev);
		if (!rt) {
			kfree_skb(skb);
			return 0;
		}
		skb_dst_set_noref(skb, &rt->dst);
	}

	skb->dev = nf_bridge->physindev;
	nf_bridge_update_protocol(skb);
	nf_bridge_push_encap_header(skb);
	br_nf_hook_thresh(NF_BR_PRE_ROUTING, net, sk, skb,
			  skb->dev, NULL, br_handle_frame_finish);

	return 0;
}

/* Replicate the checks that IPv6 does on packet reception and pass the packet
 * to ip6tables.
 */
unsigned int br_nf_pre_routing_ipv6(void *priv,
				    struct sk_buff *skb,
				    const struct nf_hook_state *state)
{
	struct nf_bridge_info *nf_bridge;

	if (br_validate_ipv6(state->net, skb))
		return NF_DROP;

	nf_bridge = nf_bridge_alloc(skb);
	if (!nf_bridge)
		return NF_DROP;
	if (!setup_pre_routing(skb))
		return NF_DROP;

	nf_bridge = nf_bridge_info_get(skb);
	nf_bridge->ipv6_daddr = ipv6_hdr(skb)->daddr;

	skb->protocol = htons(ETH_P_IPV6);
	NF_HOOK(NFPROTO_IPV6, NF_INET_PRE_ROUTING, state->net, state->sk, skb,
		skb->dev, NULL,
		br_nf_pre_routing_finish_ipv6);

	return NF_STOLEN;
}
