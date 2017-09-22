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

static struct sk_buff *udp6_tunnel_segment(struct sk_buff *skb,
					   netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);

	if (skb->encapsulation && skb_shinfo(skb)->gso_type &
	    (SKB_GSO_UDP_TUNNEL|SKB_GSO_UDP_TUNNEL_CSUM))
		segs = skb_udp_tunnel_segment(skb, features, true);

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
		.gso_segment	=	udp6_tunnel_segment,
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
