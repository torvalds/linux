/*
 *	IPV6 GSO/GRO offload support
 *	Linux INET6 implementation
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *      UDPv6 GSO support
 */
#include <linux/skbuff.h>
#include <net/protocol.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <net/ip6_checksum.h>
#include "ip6_offload.h"

static int udp6_ufo_send_check(struct sk_buff *skb)
{
	const struct ipv6hdr *ipv6h;
	struct udphdr *uh;

	/* UDP Tunnel offload on ipv6 is not yet supported. */
	if (skb->encapsulation)
		return -EINVAL;

	if (!pskb_may_pull(skb, sizeof(*uh)))
		return -EINVAL;

	ipv6h = ipv6_hdr(skb);
	uh = udp_hdr(skb);

	uh->check = ~csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr, skb->len,
				     IPPROTO_UDP, 0);
	skb->csum_start = skb_transport_header(skb) - skb->head;
	skb->csum_offset = offsetof(struct udphdr, check);
	skb->ip_summed = CHECKSUM_PARTIAL;
	return 0;
}

static struct sk_buff *udp6_ufo_fragment(struct sk_buff *skb,
	netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	unsigned int mss;
	unsigned int unfrag_ip6hlen, unfrag_len;
	struct frag_hdr *fptr;
	u8 *packet_start, *prevhdr;
	u8 nexthdr;
	u8 frag_hdr_sz = sizeof(struct frag_hdr);
	int offset;
	__wsum csum;
	int tnl_hlen;

	mss = skb_shinfo(skb)->gso_size;
	if (unlikely(skb->len <= mss))
		goto out;

	if (skb_gso_ok(skb, features | NETIF_F_GSO_ROBUST)) {
		/* Packet is from an untrusted source, reset gso_segs. */
		int type = skb_shinfo(skb)->gso_type;

		if (unlikely(type & ~(SKB_GSO_UDP |
				      SKB_GSO_DODGY |
				      SKB_GSO_UDP_TUNNEL |
				      SKB_GSO_GRE |
				      SKB_GSO_MPLS) ||
			     !(type & (SKB_GSO_UDP))))
			goto out;

		skb_shinfo(skb)->gso_segs = DIV_ROUND_UP(skb->len, mss);

		segs = NULL;
		goto out;
	}

	/* Do software UFO. Complete and fill in the UDP checksum as HW cannot
	 * do checksum of UDP packets sent as multiple IP fragments.
	 */
	offset = skb_checksum_start_offset(skb);
	csum = skb_checksum(skb, offset, skb->len - offset, 0);
	offset += skb->csum_offset;
	*(__sum16 *)(skb->data + offset) = csum_fold(csum);
	skb->ip_summed = CHECKSUM_NONE;

	/* Check if there is enough headroom to insert fragment header. */
	tnl_hlen = skb_tnl_header_len(skb);
	if (skb_headroom(skb) < (tnl_hlen + frag_hdr_sz)) {
		if (gso_pskb_expand_head(skb, tnl_hlen + frag_hdr_sz))
			goto out;
	}

	/* Find the unfragmentable header and shift it left by frag_hdr_sz
	 * bytes to insert fragment header.
	 */
	unfrag_ip6hlen = ip6_find_1stfragopt(skb, &prevhdr);
	nexthdr = *prevhdr;
	*prevhdr = NEXTHDR_FRAGMENT;
	unfrag_len = (skb_network_header(skb) - skb_mac_header(skb)) +
		     unfrag_ip6hlen + tnl_hlen;
	packet_start = (u8 *) skb->head + SKB_GSO_CB(skb)->mac_offset;
	memmove(packet_start-frag_hdr_sz, packet_start, unfrag_len);

	SKB_GSO_CB(skb)->mac_offset -= frag_hdr_sz;
	skb->mac_header -= frag_hdr_sz;
	skb->network_header -= frag_hdr_sz;

	fptr = (struct frag_hdr *)(skb_network_header(skb) + unfrag_ip6hlen);
	fptr->nexthdr = nexthdr;
	fptr->reserved = 0;
	ipv6_select_ident(fptr, (struct rt6_info *)skb_dst(skb));

	/* Fragment the skb. ipv6 header and the remaining fields of the
	 * fragment header are updated in ipv6_gso_segment()
	 */
	segs = skb_segment(skb, features);

out:
	return segs;
}
static const struct net_offload udpv6_offload = {
	.callbacks = {
		.gso_send_check =	udp6_ufo_send_check,
		.gso_segment	=	udp6_ufo_fragment,
	},
};

int __init udp_offload_init(void)
{
	return inet6_add_offload(&udpv6_offload, IPPROTO_UDP);
}
