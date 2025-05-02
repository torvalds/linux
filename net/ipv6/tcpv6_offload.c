// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPV6 GSO/GRO offload support
 *	Linux INET6 implementation
 *
 *      TCPv6 GSO/GRO support
 */
#include <linux/indirect_call_wrapper.h>
#include <linux/skbuff.h>
#include <net/inet6_hashtables.h>
#include <net/gro.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/ip6_checksum.h>
#include "ip6_offload.h"

static void tcp6_check_fraglist_gro(struct list_head *head, struct sk_buff *skb,
				    struct tcphdr *th)
{
#if IS_ENABLED(CONFIG_IPV6)
	const struct ipv6hdr *hdr;
	struct sk_buff *p;
	struct sock *sk;
	struct net *net;
	int iif, sdif;

	if (likely(!(skb->dev->features & NETIF_F_GRO_FRAGLIST)))
		return;

	p = tcp_gro_lookup(head, th);
	if (p) {
		NAPI_GRO_CB(skb)->is_flist = NAPI_GRO_CB(p)->is_flist;
		return;
	}

	inet6_get_iif_sdif(skb, &iif, &sdif);
	hdr = skb_gro_network_header(skb);
	net = dev_net_rcu(skb->dev);
	sk = __inet6_lookup_established(net, net->ipv4.tcp_death_row.hashinfo,
					&hdr->saddr, th->source,
					&hdr->daddr, ntohs(th->dest),
					iif, sdif);
	NAPI_GRO_CB(skb)->is_flist = !sk;
	if (sk)
		sock_gen_put(sk);
#endif /* IS_ENABLED(CONFIG_IPV6) */
}

INDIRECT_CALLABLE_SCOPE
struct sk_buff *tcp6_gro_receive(struct list_head *head, struct sk_buff *skb)
{
	struct tcphdr *th;

	/* Don't bother verifying checksum if we're going to flush anyway. */
	if (!NAPI_GRO_CB(skb)->flush &&
	    skb_gro_checksum_validate(skb, IPPROTO_TCP,
				      ip6_gro_compute_pseudo))
		goto flush;

	th = tcp_gro_pull_header(skb);
	if (!th)
		goto flush;

	tcp6_check_fraglist_gro(head, skb, th);

	return tcp_gro_receive(head, skb, th);

flush:
	NAPI_GRO_CB(skb)->flush = 1;
	return NULL;
}

INDIRECT_CALLABLE_SCOPE int tcp6_gro_complete(struct sk_buff *skb, int thoff)
{
	const u16 offset = NAPI_GRO_CB(skb)->network_offsets[skb->encapsulation];
	const struct ipv6hdr *iph = (struct ipv6hdr *)(skb->data + offset);
	struct tcphdr *th = tcp_hdr(skb);

	if (unlikely(NAPI_GRO_CB(skb)->is_flist)) {
		skb_shinfo(skb)->gso_type |= SKB_GSO_FRAGLIST | SKB_GSO_TCPV6;
		skb_shinfo(skb)->gso_segs = NAPI_GRO_CB(skb)->count;

		__skb_incr_checksum_unnecessary(skb);

		return 0;
	}

	th->check = ~tcp_v6_check(skb->len - thoff, &iph->saddr,
				  &iph->daddr, 0);
	skb_shinfo(skb)->gso_type |= SKB_GSO_TCPV6;

	tcp_gro_complete(skb);
	return 0;
}

static void __tcpv6_gso_segment_csum(struct sk_buff *seg,
				     struct in6_addr *oldip,
				     const struct in6_addr *newip,
				     __be16 *oldport, __be16 newport)
{
	struct tcphdr *th = tcp_hdr(seg);

	if (!ipv6_addr_equal(oldip, newip)) {
		inet_proto_csum_replace16(&th->check, seg,
					  oldip->s6_addr32,
					  newip->s6_addr32,
					  true);
		*oldip = *newip;
	}

	if (*oldport == newport)
		return;

	inet_proto_csum_replace2(&th->check, seg, *oldport, newport, false);
	*oldport = newport;
}

static struct sk_buff *__tcpv6_gso_segment_list_csum(struct sk_buff *segs)
{
	const struct tcphdr *th;
	const struct ipv6hdr *iph;
	struct sk_buff *seg;
	struct tcphdr *th2;
	struct ipv6hdr *iph2;

	seg = segs;
	th = tcp_hdr(seg);
	iph = ipv6_hdr(seg);
	th2 = tcp_hdr(seg->next);
	iph2 = ipv6_hdr(seg->next);

	if (!(*(const u32 *)&th->source ^ *(const u32 *)&th2->source) &&
	    ipv6_addr_equal(&iph->saddr, &iph2->saddr) &&
	    ipv6_addr_equal(&iph->daddr, &iph2->daddr))
		return segs;

	while ((seg = seg->next)) {
		th2 = tcp_hdr(seg);
		iph2 = ipv6_hdr(seg);

		__tcpv6_gso_segment_csum(seg, &iph2->saddr, &iph->saddr,
					 &th2->source, th->source);
		__tcpv6_gso_segment_csum(seg, &iph2->daddr, &iph->daddr,
					 &th2->dest, th->dest);
	}

	return segs;
}

static struct sk_buff *__tcp6_gso_segment_list(struct sk_buff *skb,
					      netdev_features_t features)
{
	skb = skb_segment_list(skb, features, skb_mac_header_len(skb));
	if (IS_ERR(skb))
		return skb;

	return __tcpv6_gso_segment_list_csum(skb);
}

static struct sk_buff *tcp6_gso_segment(struct sk_buff *skb,
					netdev_features_t features)
{
	struct tcphdr *th;

	if (!(skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6))
		return ERR_PTR(-EINVAL);

	if (!pskb_may_pull(skb, sizeof(*th)))
		return ERR_PTR(-EINVAL);

	if (skb_shinfo(skb)->gso_type & SKB_GSO_FRAGLIST) {
		struct tcphdr *th = tcp_hdr(skb);

		if (skb_pagelen(skb) - th->doff * 4 == skb_shinfo(skb)->gso_size)
			return __tcp6_gso_segment_list(skb, features);

		skb->ip_summed = CHECKSUM_NONE;
	}

	if (unlikely(skb->ip_summed != CHECKSUM_PARTIAL)) {
		const struct ipv6hdr *ipv6h = ipv6_hdr(skb);
		struct tcphdr *th = tcp_hdr(skb);

		/* Set up pseudo header, usually expect stack to have done
		 * this.
		 */

		th->check = 0;
		skb->ip_summed = CHECKSUM_PARTIAL;
		__tcp_v6_send_check(skb, &ipv6h->saddr, &ipv6h->daddr);
	}

	return tcp_gso_segment(skb, features);
}

int __init tcpv6_offload_init(void)
{
	net_hotdata.tcpv6_offload = (struct net_offload) {
		.callbacks = {
			.gso_segment	=	tcp6_gso_segment,
			.gro_receive	=	tcp6_gro_receive,
			.gro_complete	=	tcp6_gro_complete,
		},
	};
	return inet6_add_offload(&net_hotdata.tcpv6_offload, IPPROTO_TCP);
}
