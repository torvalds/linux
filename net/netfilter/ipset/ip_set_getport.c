/* Copyright (C) 2003-2011 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Get Layer-4 data from the packets */

#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/sctp.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <net/ip.h>
#include <net/ipv6.h>

#include <linux/netfilter/ipset/ip_set_getport.h>
#include <linux/export.h>

/* We must handle non-linear skbs */
static bool
get_port(const struct sk_buff *skb, int protocol, unsigned int protooff,
	 bool src, __be16 *port, u8 *proto)
{
	switch (protocol) {
	case IPPROTO_TCP: {
		struct tcphdr _tcph;
		const struct tcphdr *th;

		th = skb_header_pointer(skb, protooff, sizeof(_tcph), &_tcph);
		if (th == NULL)
			/* No choice either */
			return false;

		*port = src ? th->source : th->dest;
		break;
	}
	case IPPROTO_SCTP: {
		sctp_sctphdr_t _sh;
		const sctp_sctphdr_t *sh;

		sh = skb_header_pointer(skb, protooff, sizeof(_sh), &_sh);
		if (sh == NULL)
			/* No choice either */
			return false;

		*port = src ? sh->source : sh->dest;
		break;
	}
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE: {
		struct udphdr _udph;
		const struct udphdr *uh;

		uh = skb_header_pointer(skb, protooff, sizeof(_udph), &_udph);
		if (uh == NULL)
			/* No choice either */
			return false;

		*port = src ? uh->source : uh->dest;
		break;
	}
	case IPPROTO_ICMP: {
		struct icmphdr _ich;
		const struct icmphdr *ic;

		ic = skb_header_pointer(skb, protooff, sizeof(_ich), &_ich);
		if (ic == NULL)
			return false;

		*port = (__force __be16)htons((ic->type << 8) | ic->code);
		break;
	}
	case IPPROTO_ICMPV6: {
		struct icmp6hdr _ich;
		const struct icmp6hdr *ic;

		ic = skb_header_pointer(skb, protooff, sizeof(_ich), &_ich);
		if (ic == NULL)
			return false;

		*port = (__force __be16)
			htons((ic->icmp6_type << 8) | ic->icmp6_code);
		break;
	}
	default:
		break;
	}
	*proto = protocol;

	return true;
}

bool
ip_set_get_ip4_port(const struct sk_buff *skb, bool src,
		    __be16 *port, u8 *proto)
{
	const struct iphdr *iph = ip_hdr(skb);
	unsigned int protooff = ip_hdrlen(skb);
	int protocol = iph->protocol;

	/* See comments at tcp_match in ip_tables.c */
	if (protocol <= 0 || (ntohs(iph->frag_off) & IP_OFFSET))
		return false;

	return get_port(skb, protocol, protooff, src, port, proto);
}
EXPORT_SYMBOL_GPL(ip_set_get_ip4_port);

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
bool
ip_set_get_ip6_port(const struct sk_buff *skb, bool src,
		    __be16 *port, u8 *proto)
{
	int protoff;
	u8 nexthdr;
	__be16 frag_off;

	nexthdr = ipv6_hdr(skb)->nexthdr;
	protoff = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &nexthdr,
				   &frag_off);
	if (protoff < 0)
		return false;

	return get_port(skb, nexthdr, protoff, src, port, proto);
}
EXPORT_SYMBOL_GPL(ip_set_get_ip6_port);
#endif

bool
ip_set_get_ip_port(const struct sk_buff *skb, u8 pf, bool src, __be16 *port)
{
	bool ret;
	u8 proto;

	switch (pf) {
	case NFPROTO_IPV4:
		ret = ip_set_get_ip4_port(skb, src, port, &proto);
		break;
	case NFPROTO_IPV6:
		ret = ip_set_get_ip6_port(skb, src, port, &proto);
		break;
	default:
		return false;
	}
	if (!ret)
		return ret;
	switch (proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(ip_set_get_ip_port);
