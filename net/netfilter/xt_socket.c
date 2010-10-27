/*
 * Transparent proxy support for Linux/iptables
 *
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
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <net/sock.h>
#include <net/inet_sock.h>
#include <net/netfilter/nf_tproxy_core.h>
#include <net/netfilter/ipv4/nf_defrag_ipv4.h>

#include <linux/netfilter/xt_socket.h>

#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
#define XT_SOCKET_HAVE_CONNTRACK 1
#include <net/netfilter/nf_conntrack.h>
#endif

static int
extract_icmp_fields(const struct sk_buff *skb,
		    u8 *protocol,
		    __be32 *raddr,
		    __be32 *laddr,
		    __be16 *rport,
		    __be16 *lport)
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


static bool
socket_match(const struct sk_buff *skb, struct xt_action_param *par,
	     const struct xt_socket_mtinfo1 *info)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct udphdr _hdr, *hp = NULL;
	struct sock *sk;
	__be32 daddr, saddr;
	__be16 dport, sport;
	u8 protocol;
#ifdef XT_SOCKET_HAVE_CONNTRACK
	struct nf_conn const *ct;
	enum ip_conntrack_info ctinfo;
#endif

	if (iph->protocol == IPPROTO_UDP || iph->protocol == IPPROTO_TCP) {
		hp = skb_header_pointer(skb, ip_hdrlen(skb),
					sizeof(_hdr), &_hdr);
		if (hp == NULL)
			return false;

		protocol = iph->protocol;
		saddr = iph->saddr;
		sport = hp->source;
		daddr = iph->daddr;
		dport = hp->dest;

	} else if (iph->protocol == IPPROTO_ICMP) {
		if (extract_icmp_fields(skb, &protocol, &saddr, &daddr,
					&sport, &dport))
			return false;
	} else {
		return false;
	}

#ifdef XT_SOCKET_HAVE_CONNTRACK
	/* Do the lookup with the original socket address in case this is a
	 * reply packet of an established SNAT-ted connection. */

	ct = nf_ct_get(skb, &ctinfo);
	if (ct && !nf_ct_is_untracked(ct) &&
	    ((iph->protocol != IPPROTO_ICMP &&
	      ctinfo == IP_CT_IS_REPLY + IP_CT_ESTABLISHED) ||
	     (iph->protocol == IPPROTO_ICMP &&
	      ctinfo == IP_CT_IS_REPLY + IP_CT_RELATED)) &&
	    (ct->status & IPS_SRC_NAT_DONE)) {

		daddr = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.ip;
		dport = (iph->protocol == IPPROTO_TCP) ?
			ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.tcp.port :
			ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port;
	}
#endif

	sk = nf_tproxy_get_sock_v4(dev_net(skb->dev), protocol,
				   saddr, daddr, sport, dport, par->in, false);
	if (sk != NULL) {
		bool wildcard;
		bool transparent = true;

		/* Ignore sockets listening on INADDR_ANY */
		wildcard = (sk->sk_state != TCP_TIME_WAIT &&
			    inet_sk(sk)->inet_rcv_saddr == 0);

		/* Ignore non-transparent sockets,
		   if XT_SOCKET_TRANSPARENT is used */
		if (info && info->flags & XT_SOCKET_TRANSPARENT)
			transparent = ((sk->sk_state != TCP_TIME_WAIT &&
					inet_sk(sk)->transparent) ||
				       (sk->sk_state == TCP_TIME_WAIT &&
					inet_twsk(sk)->tw_transparent));

		nf_tproxy_put_sock(sk);

		if (wildcard || !transparent)
			sk = NULL;
	}

	pr_debug("proto %u %08x:%u -> %08x:%u (orig %08x:%u) sock %p\n",
		 protocol, ntohl(saddr), ntohs(sport),
		 ntohl(daddr), ntohs(dport),
		 ntohl(iph->daddr), hp ? ntohs(hp->dest) : 0, sk);

	return (sk != NULL);
}

static bool
socket_mt_v0(const struct sk_buff *skb, struct xt_action_param *par)
{
	return socket_match(skb, par, NULL);
}

static bool
socket_mt_v1(const struct sk_buff *skb, struct xt_action_param *par)
{
	return socket_match(skb, par, par->matchinfo);
}

static struct xt_match socket_mt_reg[] __read_mostly = {
	{
		.name		= "socket",
		.revision	= 0,
		.family		= NFPROTO_IPV4,
		.match		= socket_mt_v0,
		.hooks		= (1 << NF_INET_PRE_ROUTING) |
				  (1 << NF_INET_LOCAL_IN),
		.me		= THIS_MODULE,
	},
	{
		.name		= "socket",
		.revision	= 1,
		.family		= NFPROTO_IPV4,
		.match		= socket_mt_v1,
		.matchsize	= sizeof(struct xt_socket_mtinfo1),
		.hooks		= (1 << NF_INET_PRE_ROUTING) |
				  (1 << NF_INET_LOCAL_IN),
		.me		= THIS_MODULE,
	},
};

static int __init socket_mt_init(void)
{
	nf_defrag_ipv4_enable();
	return xt_register_matches(socket_mt_reg, ARRAY_SIZE(socket_mt_reg));
}

static void __exit socket_mt_exit(void)
{
	xt_unregister_matches(socket_mt_reg, ARRAY_SIZE(socket_mt_reg));
}

module_init(socket_mt_init);
module_exit(socket_mt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krisztian Kovacs, Balazs Scheidler");
MODULE_DESCRIPTION("x_tables socket match module");
MODULE_ALIAS("ipt_socket");
