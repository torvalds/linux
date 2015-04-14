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
#include <linux/netdevice.h>
#include <net/protocol.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <net/ip6_checksum.h>
#include "ip6_offload.h"

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
				      SKB_GSO_UDP_TUNNEL_CSUM |
				      SKB_GSO_TUNNEL_REMCSUM |
				      SKB_GSO_GRE |
				      SKB_GSO_GRE_CSUM |
				      SKB_GSO_IPIP |
				      SKB_GSO_SIT) ||
			     !(type & (SKB_GSO_UDP))))
			goto out;

		skb_shinfo(skb)->gso_segs = DIV_ROUND_UP(skb->len, mss);

		/* Set the IPv6 fragment id if not set yet */
		if (!skb_shinfo(skb)->ip6_frag_id)
			ipv6_proxy_select_ident(skb);

		segs = NULL;
		goto out;
	}

	if (skb->encapsulation && skb_shinfo(skb)->gso_type &
	    (SKB_GSO_UDP_TUNNEL|SKB_GSO_UDP_TUNNEL_CSUM))
		segs = skb_udp_tunnel_segment(skb, features, true);
	else {
		const struct ipv6hdr *ipv6h;
		struct udphdr *uh;

		if (!pskb_may_pull(skb, sizeof(struct udphdr)))
			goto out;

		/* Do software UFO. Complete and fill in the UDP checksum as HW cannot
		 * do checksum of UDP packets sent as multiple IP fragments.
		 */

		uh = udp_hdr(skb);
		ipv6h = ipv6_hdr(skb);

		uh->check = 0;
		csum = skb_checksum(skb, 0, skb->len, 0);
		uh->check = udp_v6_check(skb->len, &ipv6h->saddr,
					  &ipv6h->daddr, csum);

		if (uh->check == 0)
			uh->check = CSUM_MANGLED_0;

		skb->ip_summed = CHECKSUM_NONE;

		/* Check if there is enough headroom to insert fragment header. */
		tnl_hlen = skb_tnl_header_len(skb);
		if (skb->mac_header < (tnl_hlen + frag_hdr_sz)) {
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
		if (!skb_shinfo(skb)->ip6_frag_id)
			ipv6_proxy_select_ident(skb);
		fptr->identification = skb_shinfo(skb)->ip6_frag_id;

		/* Fragment the skb. ipv6 header and the remaining fields of the
		 * fragment header are updated in ipv6_gso_segment()
		 */
		segs = skb_segment(skb, features);
	}

out:
	return segs;
}

static struct sk_buff **udp6_gro_receive(struct sk_buff **head,
					 struct sk_buff *skb)
{
	struct udphdr *uh = udp_gro_udphdr(skb);

	if (unlikely(!uh))
		goto flush;

	/* Don't bother verifying checksum if we're going to flush anyway. */
	if (NAPI_GRO_CB(skb)->flush)
		goto skip;

	if (skb_gro_checksum_validate_zero_check(skb, IPPROTO_UDP, uh->check,
						 ip6_gro_compute_pseudo))
		goto flush;
	else if (uh->check)
		skb_gro_checksum_try_convert(skb, IPPROTO_UDP, uh->check,
					     ip6_gro_compute_pseudo);

skip:
	NAPI_GRO_CB(skb)->is_ipv6 = 1;
	return udp_gro_receive(head, skb, uh);

flush:
	NAPI_GRO_CB(skb)->flush = 1;
	return NULL;
}

static int udp6_gro_complete(struct sk_buff *skb, int nhoff)
{
	const struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	struct udphdr *uh = (struct udphdr *)(skb->data + nhoff);

	if (uh->check) {
		skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL_CSUM;
		uh->check = ~udp_v6_check(skb->len - nhoff, &ipv6h->saddr,
					  &ipv6h->daddr, 0);
	} else {
		skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL;
	}

	return udp_gro_complete(skb, nhoff);
}

static const struct net_offload udpv6_offload = {
	.callbacks = {
		.gso_segment	=	udp6_ufo_fragment,
		.gro_receive	=	udp6_gro_receive,
		.gro_complete	=	udp6_gro_complete,
	},
};

int __init udp_offload_init(void)
{
	return inet6_add_offload(&udpv6_offload, IPPROTO_UDP);
}
