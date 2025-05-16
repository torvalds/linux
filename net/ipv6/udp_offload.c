// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPV6 GSO/GRO offload support
 *	Linux INET6 implementation
 *
 *      UDPv6 GSO support
 */
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/indirect_call_wrapper.h>
#include <net/protocol.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <net/ip6_checksum.h>
#include "ip6_offload.h"
#include <net/gro.h>
#include <net/gso.h>

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
			return __udp_gso_segment(skb, features, true);

		mss = skb_shinfo(skb)->gso_size;
		if (unlikely(skb->len <= mss))
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

static struct sock *udp6_gro_lookup_skb(struct sk_buff *skb, __be16 sport,
					__be16 dport)
{
	const struct ipv6hdr *iph = skb_gro_network_header(skb);
	struct net *net = dev_net_rcu(skb->dev);
	int iif, sdif;

	inet6_get_iif_sdif(skb, &iif, &sdif);

	return __udp6_lib_lookup(net, &iph->saddr, sport,
				 &iph->daddr, dport, iif,
				 sdif, net->ipv4.udp_table, NULL);
}

INDIRECT_CALLABLE_SCOPE
struct sk_buff *udp6_gro_receive(struct list_head *head, struct sk_buff *skb)
{
	struct udphdr *uh = udp_gro_udphdr(skb);
	struct sock *sk = NULL;
	struct sk_buff *pp;

	if (unlikely(!uh))
		goto flush;

	/* Don't bother verifying checksum if we're going to flush anyway. */
	if (NAPI_GRO_CB(skb)->flush)
		goto skip;

	if (skb_gro_checksum_validate_zero_check(skb, IPPROTO_UDP, uh->check,
						 ip6_gro_compute_pseudo))
		goto flush;
	else if (uh->check)
		skb_gro_checksum_try_convert(skb, IPPROTO_UDP,
					     ip6_gro_compute_pseudo);

skip:
	NAPI_GRO_CB(skb)->is_ipv6 = 1;

	if (static_branch_unlikely(&udpv6_encap_needed_key))
		sk = udp6_gro_lookup_skb(skb, uh->source, uh->dest);

	pp = udp_gro_receive(head, skb, uh, sk);
	return pp;

flush:
	NAPI_GRO_CB(skb)->flush = 1;
	return NULL;
}

INDIRECT_CALLABLE_SCOPE int udp6_gro_complete(struct sk_buff *skb, int nhoff)
{
	const u16 offset = NAPI_GRO_CB(skb)->network_offsets[skb->encapsulation];
	const struct ipv6hdr *ipv6h = (struct ipv6hdr *)(skb->data + offset);
	struct udphdr *uh = (struct udphdr *)(skb->data + nhoff);

	/* do fraglist only if there is no outer UDP encap (or we already processed it) */
	if (NAPI_GRO_CB(skb)->is_flist && !NAPI_GRO_CB(skb)->encap_mark) {
		uh->len = htons(skb->len - nhoff);

		skb_shinfo(skb)->gso_type |= (SKB_GSO_FRAGLIST|SKB_GSO_UDP_L4);
		skb_shinfo(skb)->gso_segs = NAPI_GRO_CB(skb)->count;

		__skb_incr_checksum_unnecessary(skb);

		return 0;
	}

	if (uh->check)
		uh->check = ~udp_v6_check(skb->len - nhoff, &ipv6h->saddr,
					  &ipv6h->daddr, 0);

	return udp_gro_complete(skb, nhoff, udp6_lib_lookup_skb);
}

int __init udpv6_offload_init(void)
{
	net_hotdata.udpv6_offload = (struct net_offload) {
		.callbacks = {
			.gso_segment	=	udp6_ufo_fragment,
			.gro_receive	=	udp6_gro_receive,
			.gro_complete	=	udp6_gro_complete,
		},
	};
	return inet6_add_offload(&net_hotdata.udpv6_offload, IPPROTO_UDP);
}

int udpv6_offload_exit(void)
{
	return inet6_del_offload(&net_hotdata.udpv6_offload, IPPROTO_UDP);
}
