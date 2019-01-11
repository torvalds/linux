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
	int err;

	mss = skb_shinfo(skb)->gso_size;
	if (unlikely(skb->len <= mss))
		goto out;

	if (skb->encapsulation && skb_shinfo(skb)->gso_type &
	    (SKB_GSO_UDP_TUNNEL|SKB_GSO_UDP_TUNNEL_CSUM))
		segs = skb_udp_tunnel_segment(skb, features, true);
	else {
		const struct ipv6hdr *ipv6h;
		struct udphdr *uh;

		if (!(skb_shinfo(skb)->gso_type & (SKB_GSO_UDP | SKB_GSO_UDP_L4)))
			goto out;

		if (!pskb_may_pull(skb, sizeof(struct udphdr)))
			goto out;

		if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4)
			return __udp_gso_segment(skb, features);

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

		skb->ip_summed = CHECKSUM_UNNECESSARY;

		/* If there is no outer header we can fake a checksum offload
		 * due to the fact that we have already done the checksum in
		 * software prior to segmenting the frame.
		 */
		if (!skb->encap_hdr_csum)
			features |= NETIF_F_HW_CSUM;

		/* Check if there is enough headroom to insert fragment header. */
		tnl_hlen = skb_tnl_header_len(skb);
		if (skb->mac_header < (tnl_hlen + frag_hdr_sz)) {
			if (gso_pskb_expand_head(skb, tnl_hlen + frag_hdr_sz))
				goto out;
		}

		/* Find the unfragmentable header and shift it left by frag_hdr_sz
		 * bytes to insert fragment header.
		 */
		err = ip6_find_1stfragopt(skb, &prevhdr);
		if (err < 0)
			return ERR_PTR(err);
		unfrag_ip6hlen = err;
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
		fptr->identification = ipv6_proxy_select_ident(dev_net(skb->dev), skb);

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
	return udp_gro_receive(head, skb, uh, udp6_lib_lookup_skb);

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

	return udp_gro_complete(skb, nhoff, udp6_lib_lookup_skb);
}

static const struct net_offload udpv6_offload = {
	.callbacks = {
		.gso_segment	=	udp6_ufo_fragment,
		.gro_receive	=	udp6_gro_receive,
		.gro_complete	=	udp6_gro_complete,
	},
};

int udpv6_offload_init(void)
{
	return inet6_add_offload(&udpv6_offload, IPPROTO_UDP);
}

int udpv6_offload_exit(void)
{
	return inet6_del_offload(&udpv6_offload, IPPROTO_UDP);
}
