// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2008 BalaBit IT Ltd.
 * Author: Krisztian Kovacs
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <net/sock.h>
#include <net/inet_sock.h>
#include <net/inet6_hashtables.h>
#include <net/netfilter/nf_socket.h>
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack.h>
#endif

static int
extract_icmp6_fields(const struct sk_buff *skb,
		     unsigned int outside_hdrlen,
		     int *protocol,
		     const struct in6_addr **raddr,
		     const struct in6_addr **laddr,
		     __be16 *rport,
		     __be16 *lport,
		     struct ipv6hdr *ipv6_var)
{
	const struct ipv6hdr *inside_iph;
	struct icmp6hdr *icmph, _icmph;
	__be16 *ports, _ports[2];
	u8 inside_nexthdr;
	__be16 inside_fragoff;
	int inside_hdrlen;

	icmph = skb_header_pointer(skb, outside_hdrlen,
				   sizeof(_icmph), &_icmph);
	if (icmph == NULL)
		return 1;

	if (icmph->icmp6_type & ICMPV6_INFOMSG_MASK)
		return 1;

	inside_iph = skb_header_pointer(skb, outside_hdrlen + sizeof(_icmph),
					sizeof(*ipv6_var), ipv6_var);
	if (inside_iph == NULL)
		return 1;
	inside_nexthdr = inside_iph->nexthdr;

	inside_hdrlen = ipv6_skip_exthdr(skb, outside_hdrlen + sizeof(_icmph) +
					      sizeof(*ipv6_var),
					 &inside_nexthdr, &inside_fragoff);
	if (inside_hdrlen < 0)
		return 1; /* hjm: Packet has no/incomplete transport layer headers. */

	if (inside_nexthdr != IPPROTO_TCP &&
	    inside_nexthdr != IPPROTO_UDP)
		return 1;

	ports = skb_header_pointer(skb, inside_hdrlen,
				   sizeof(_ports), &_ports);
	if (ports == NULL)
		return 1;

	/* the inside IP packet is the one quoted from our side, thus
	 * its saddr is the local address */
	*protocol = inside_nexthdr;
	*laddr = &inside_iph->saddr;
	*lport = ports[0];
	*raddr = &inside_iph->daddr;
	*rport = ports[1];

	return 0;
}

static struct sock *
nf_socket_get_sock_v6(struct net *net, struct sk_buff *skb, int doff,
		      const u8 protocol,
		      const struct in6_addr *saddr, const struct in6_addr *daddr,
		      const __be16 sport, const __be16 dport,
		      const struct net_device *in)
{
	switch (protocol) {
	case IPPROTO_TCP:
		return inet6_lookup(net, &tcp_hashinfo, skb, doff,
				    saddr, sport, daddr, dport,
				    in->ifindex);
	case IPPROTO_UDP:
		return udp6_lib_lookup(net, saddr, sport, daddr, dport,
				       in->ifindex);
	}

	return NULL;
}

struct sock *nf_sk_lookup_slow_v6(struct net *net, const struct sk_buff *skb,
				  const struct net_device *indev)
{
	__be16 uninitialized_var(dport), uninitialized_var(sport);
	const struct in6_addr *daddr = NULL, *saddr = NULL;
	struct ipv6hdr *iph = ipv6_hdr(skb);
	struct sk_buff *data_skb = NULL;
	int doff = 0;
	int thoff = 0, tproto;

	tproto = ipv6_find_hdr(skb, &thoff, -1, NULL, NULL);
	if (tproto < 0) {
		pr_debug("unable to find transport header in IPv6 packet, dropping\n");
		return NULL;
	}

	if (tproto == IPPROTO_UDP || tproto == IPPROTO_TCP) {
		struct tcphdr _hdr;
		struct udphdr *hp;

		hp = skb_header_pointer(skb, thoff, tproto == IPPROTO_UDP ?
					sizeof(*hp) : sizeof(_hdr), &_hdr);
		if (hp == NULL)
			return NULL;

		saddr = &iph->saddr;
		sport = hp->source;
		daddr = &iph->daddr;
		dport = hp->dest;
		data_skb = (struct sk_buff *)skb;
		doff = tproto == IPPROTO_TCP ?
			thoff + __tcp_hdrlen((struct tcphdr *)hp) :
			thoff + sizeof(*hp);

	} else if (tproto == IPPROTO_ICMPV6) {
		struct ipv6hdr ipv6_var;

		if (extract_icmp6_fields(skb, thoff, &tproto, &saddr, &daddr,
					 &sport, &dport, &ipv6_var))
			return NULL;
	} else {
		return NULL;
	}

	return nf_socket_get_sock_v6(net, data_skb, doff, tproto, saddr, daddr,
				     sport, dport, indev);
}
EXPORT_SYMBOL_GPL(nf_sk_lookup_slow_v6);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krisztian Kovacs, Balazs Scheidler");
MODULE_DESCRIPTION("Netfilter IPv6 socket lookup infrastructure");
