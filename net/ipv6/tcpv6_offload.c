// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPV6 GSO/GRO offload support
 *	Linux INET6 implementation
 *
 *      TCPv6 GSO/GRO support
 */
#include <linux/indirect_call_wrapper.h>
#include <linux/skbuff.h>
#include <net/gro.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/ip6_checksum.h>
#include "ip6_offload.h"

INDIRECT_CALLABLE_SCOPE
struct sk_buff *tcp6_gro_receive(struct list_head *head, struct sk_buff *skb)
{
	/* Don't bother verifying checksum if we're going to flush anyway. */
	if (!NAPI_GRO_CB(skb)->flush &&
	    skb_gro_checksum_validate(skb, IPPROTO_TCP,
				      ip6_gro_compute_pseudo)) {
		NAPI_GRO_CB(skb)->flush = 1;
		return NULL;
	}

	return tcp_gro_receive(head, skb);
}

INDIRECT_CALLABLE_SCOPE int tcp6_gro_complete(struct sk_buff *skb, int thoff)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct tcphdr *th = tcp_hdr(skb);

	th->check = ~tcp_v6_check(skb->len - thoff, &iph->saddr,
				  &iph->daddr, 0);
	skb_shinfo(skb)->gso_type |= SKB_GSO_TCPV6;

	tcp_gro_complete(skb);
	return 0;
}

static struct sk_buff *tcp6_gso_segment(struct sk_buff *skb,
					netdev_features_t features)
{
	struct tcphdr *th;

	if (!(skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6))
		return ERR_PTR(-EINVAL);

	if (!pskb_may_pull(skb, sizeof(*th)))
		return ERR_PTR(-EINVAL);

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
