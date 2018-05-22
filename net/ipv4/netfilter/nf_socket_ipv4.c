/*
 * Copyright (C) 2007-2008 BalaBit IT Ltd.
 * Author: Krisztian Kovacs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <net/sock.h>
#include <net/inet_sock.h>
#include <net/netfilter/nf_socket.h>
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack.h>
#endif

static int
extract_icmp4_fields(const struct sk_buff *skb, u8 *protocol,
		     __be32 *raddr, __be32 *laddr,
		     __be16 *rport, __be16 *lport)
{
	unsigned int outside_hdrlen = ip_hdrlen(skb);
	struct iphdr *inside_iph, _inside_iph;
	struct icmphdr *icmph, _icmph;
	__be16 *ports, _ports[2];

	icmph = skb_header_pointer(skb, outside_hdrlen,
				   sizeof(_icmph), &_icmph);
	if (icmph == NULL)
		return 1;

	switch (icmph->type) {
	case ICMP_DEST_UNREACH:
	case ICMP_SOURCE_QUENCH:
	case ICMP_REDIRECT:
	case ICMP_TIME_EXCEEDED:
	case ICMP_PARAMETERPROB:
		break;
	default:
		return 1;
	}

	inside_iph = skb_header_pointer(skb, outside_hdrlen +
					sizeof(struct icmphdr),
					sizeof(_inside_iph), &_inside_iph);
	if (inside_iph == NULL)
		return 1;

	if (inside_iph->protocol != IPPROTO_TCP &&
	    inside_iph->protocol != IPPROTO_UDP)
		return 1;

	ports = skb_header_pointer(skb, outside_hdrlen +
				   sizeof(struct icmphdr) +
				   (inside_iph->ihl << 2),
				   sizeof(_ports), &_ports);
	if (ports == NULL)
		return 1;

	/* the inside IP packet is the one quoted from our side, thus
	 * its saddr is the local address */
	*protocol = inside_iph->protocol;
	*laddr = inside_iph->saddr;
	*lport = ports[0];
	*raddr = inside_iph->daddr;
	*rport = ports[1];

	return 0;
}

static struct sock *
nf_socket_get_sock_v4(struct net *net, struct sk_buff *skb, const int doff,
		      const u8 protocol,
		      const __be32 saddr, const __be32 daddr,
		      const __be16 sport, const __be16 dport,
		      const struct net_device *in)
{
	switch (protocol) {
	case IPPROTO_TCP:
		return inet_lookup(net, &tcp_hashinfo, skb, doff,
				   saddr, sport, daddr, dport,
				   in->ifindex);
	case IPPROTO_UDP:
		return udp4_lib_lookup(net, saddr, sport, daddr, dport,
				       in->ifindex);
	}
	return NULL;
}

struct sock *nf_sk_lookup_slow_v4(struct net *net, const struct sk_buff *skb,
				  const struct net_device *indev)
{
	__be32 uninitialized_var(daddr), uninitialized_var(saddr);
	__be16 uninitialized_var(dport), uninitialized_var(sport);
	const struct iphdr *iph = ip_hdr(skb);
	struct sk_buff *data_skb = NULL;
	u8 uninitialized_var(protocol);
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	enum ip_conntrack_info ctinfo;
	struct nf_conn const *ct;
#endif
	int doff = 0;

	if (iph->protocol == IPPROTO_UDP || iph->protocol == IPPROTO_TCP) {
		struct tcphdr _hdr;
		struct udphdr *hp;

		hp = skb_header_pointer(skb, ip_hdrlen(skb),
					iph->protocol == IPPROTO_UDP ?
					sizeof(*hp) : sizeof(_hdr), &_hdr);
		if (hp == NULL)
			return NULL;

		protocol = iph->protocol;
		saddr = iph->saddr;
		sport = hp->source;
		daddr = iph->daddr;
		dport = hp->dest;
		data_skb = (struct sk_buff *)skb;
		doff = iph->protocol == IPPROTO_TCP ?
			ip_hdrlen(skb) + __tcp_hdrlen((struct tcphdr *)hp) :
			ip_hdrlen(skb) + sizeof(*hp);

	} else if (iph->protocol == IPPROTO_ICMP) {
		if (extract_icmp4_fields(skb, &protocol, &saddr, &daddr,
					 &sport, &dport))
			return NULL;
	} else {
		return NULL;
	}

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	/* Do the lookup with the original socket address in
	 * case this is a reply packet of an established
	 * SNAT-ted connection.
	 */
	ct = nf_ct_get(skb, &ctinfo);
	if (ct &&
	    ((iph->protocol != IPPROTO_ICMP &&
	      ctinfo == IP_CT_ESTABLISHED_REPLY) ||
	     (iph->protocol == IPPROTO_ICMP &&
	      ctinfo == IP_CT_RELATED_REPLY)) &&
	    (ct->status & IPS_SRC_NAT_DONE)) {

		daddr = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.ip;
		dport = (iph->protocol == IPPROTO_TCP) ?
			ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.tcp.port :
			ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port;
	}
#endif

	return nf_socket_get_sock_v4(net, data_skb, doff, protocol, saddr,
				     daddr, sport, dport, indev);
}
EXPORT_SYMBOL_GPL(nf_sk_lookup_slow_v4);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krisztian Kovacs, Balazs Scheidler");
MODULE_DESCRIPTION("Netfilter IPv4 socket lookup infrastructure");
