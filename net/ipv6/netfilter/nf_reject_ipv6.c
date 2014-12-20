/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/ip6_fib.h>
#include <net/ip6_checksum.h>
#include <net/netfilter/ipv6/nf_reject.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/ipv6/nf_reject.h>

const struct tcphdr *nf_reject_ip6_tcphdr_get(struct sk_buff *oldskb,
					      struct tcphdr *otcph,
					      unsigned int *otcplen, int hook)
{
	const struct ipv6hdr *oip6h = ipv6_hdr(oldskb);
	u8 proto;
	__be16 frag_off;
	int tcphoff;

	proto = oip6h->nexthdr;
	tcphoff = ipv6_skip_exthdr(oldskb, ((u8*)(oip6h+1) - oldskb->data),
				   &proto, &frag_off);

	if ((tcphoff < 0) || (tcphoff > oldskb->len)) {
		pr_debug("Cannot get TCP header.\n");
		return NULL;
	}

	*otcplen = oldskb->len - tcphoff;

	/* IP header checks: fragment, too short. */
	if (proto != IPPROTO_TCP || *otcplen < sizeof(struct tcphdr)) {
		pr_debug("proto(%d) != IPPROTO_TCP or too short (len = %d)\n",
			 proto, *otcplen);
		return NULL;
	}

	otcph = skb_header_pointer(oldskb, tcphoff, sizeof(struct tcphdr),
				   otcph);
	if (otcph == NULL)
		return NULL;

	/* No RST for RST. */
	if (otcph->rst) {
		pr_debug("RST is set\n");
		return NULL;
	}

	/* Check checksum. */
	if (nf_ip6_checksum(oldskb, hook, tcphoff, IPPROTO_TCP)) {
		pr_debug("TCP checksum is invalid\n");
		return NULL;
	}

	return otcph;
}
EXPORT_SYMBOL_GPL(nf_reject_ip6_tcphdr_get);

struct ipv6hdr *nf_reject_ip6hdr_put(struct sk_buff *nskb,
				     const struct sk_buff *oldskb,
				     __be16 protocol, int hoplimit)
{
	struct ipv6hdr *ip6h;
	const struct ipv6hdr *oip6h = ipv6_hdr(oldskb);
#define DEFAULT_TOS_VALUE	0x0U
	const __u8 tclass = DEFAULT_TOS_VALUE;

	skb_put(nskb, sizeof(struct ipv6hdr));
	skb_reset_network_header(nskb);
	ip6h = ipv6_hdr(nskb);
	ip6_flow_hdr(ip6h, tclass, 0);
	ip6h->hop_limit = hoplimit;
	ip6h->nexthdr = protocol;
	ip6h->saddr = oip6h->daddr;
	ip6h->daddr = oip6h->saddr;

	nskb->protocol = htons(ETH_P_IPV6);

	return ip6h;
}
EXPORT_SYMBOL_GPL(nf_reject_ip6hdr_put);

void nf_reject_ip6_tcphdr_put(struct sk_buff *nskb,
			      const struct sk_buff *oldskb,
			      const struct tcphdr *oth, unsigned int otcplen)
{
	struct tcphdr *tcph;
	int needs_ack;

	skb_reset_transport_header(nskb);
	tcph = (struct tcphdr *)skb_put(nskb, sizeof(struct tcphdr));
	/* Truncate to length (no data) */
	tcph->doff = sizeof(struct tcphdr)/4;
	tcph->source = oth->dest;
	tcph->dest = oth->source;

	if (oth->ack) {
		needs_ack = 0;
		tcph->seq = oth->ack_seq;
		tcph->ack_seq = 0;
	} else {
		needs_ack = 1;
		tcph->ack_seq = htonl(ntohl(oth->seq) + oth->syn + oth->fin +
				      otcplen - (oth->doff<<2));
		tcph->seq = 0;
	}

	/* Reset flags */
	((u_int8_t *)tcph)[13] = 0;
	tcph->rst = 1;
	tcph->ack = needs_ack;
	tcph->window = 0;
	tcph->urg_ptr = 0;
	tcph->check = 0;

	/* Adjust TCP checksum */
	tcph->check = csum_ipv6_magic(&ipv6_hdr(nskb)->saddr,
				      &ipv6_hdr(nskb)->daddr,
				      sizeof(struct tcphdr), IPPROTO_TCP,
				      csum_partial(tcph,
						   sizeof(struct tcphdr), 0));
}
EXPORT_SYMBOL_GPL(nf_reject_ip6_tcphdr_put);

void nf_send_reset6(struct net *net, struct sk_buff *oldskb, int hook)
{
	struct sk_buff *nskb;
	struct tcphdr _otcph;
	const struct tcphdr *otcph;
	unsigned int otcplen, hh_len;
	const struct ipv6hdr *oip6h = ipv6_hdr(oldskb);
	struct ipv6hdr *ip6h;
	struct dst_entry *dst = NULL;
	struct flowi6 fl6;

	if ((!(ipv6_addr_type(&oip6h->saddr) & IPV6_ADDR_UNICAST)) ||
	    (!(ipv6_addr_type(&oip6h->daddr) & IPV6_ADDR_UNICAST))) {
		pr_debug("addr is not unicast.\n");
		return;
	}

	otcph = nf_reject_ip6_tcphdr_get(oldskb, &_otcph, &otcplen, hook);
	if (!otcph)
		return;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_proto = IPPROTO_TCP;
	fl6.saddr = oip6h->daddr;
	fl6.daddr = oip6h->saddr;
	fl6.fl6_sport = otcph->dest;
	fl6.fl6_dport = otcph->source;
	security_skb_classify_flow(oldskb, flowi6_to_flowi(&fl6));
	dst = ip6_route_output(net, NULL, &fl6);
	if (dst == NULL || dst->error) {
		dst_release(dst);
		return;
	}
	dst = xfrm_lookup(net, dst, flowi6_to_flowi(&fl6), NULL, 0);
	if (IS_ERR(dst))
		return;

	hh_len = (dst->dev->hard_header_len + 15)&~15;
	nskb = alloc_skb(hh_len + 15 + dst->header_len + sizeof(struct ipv6hdr)
			 + sizeof(struct tcphdr) + dst->trailer_len,
			 GFP_ATOMIC);

	if (!nskb) {
		net_dbg_ratelimited("cannot alloc skb\n");
		dst_release(dst);
		return;
	}

	skb_dst_set(nskb, dst);

	skb_reserve(nskb, hh_len + dst->header_len);
	ip6h = nf_reject_ip6hdr_put(nskb, oldskb, IPPROTO_TCP,
				    ip6_dst_hoplimit(dst));
	nf_reject_ip6_tcphdr_put(nskb, oldskb, otcph, otcplen);

	nf_ct_attach(nskb, oldskb);

#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	/* If we use ip6_local_out for bridged traffic, the MAC source on
	 * the RST will be ours, instead of the destination's.  This confuses
	 * some routers/firewalls, and they drop the packet.  So we need to
	 * build the eth header using the original destination's MAC as the
	 * source, and send the RST packet directly.
	 */
	if (oldskb->nf_bridge) {
		struct ethhdr *oeth = eth_hdr(oldskb);
		nskb->dev = oldskb->nf_bridge->physindev;
		nskb->protocol = htons(ETH_P_IPV6);
		ip6h->payload_len = htons(sizeof(struct tcphdr));
		if (dev_hard_header(nskb, nskb->dev, ntohs(nskb->protocol),
				    oeth->h_source, oeth->h_dest, nskb->len) < 0)
			return;
		dev_queue_xmit(nskb);
	} else
#endif
		ip6_local_out(nskb);
}
EXPORT_SYMBOL_GPL(nf_send_reset6);

MODULE_LICENSE("GPL");
