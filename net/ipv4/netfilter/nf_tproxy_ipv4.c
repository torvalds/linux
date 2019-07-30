// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2008 BalaBit IT Ltd.
 * Author: Krisztian Kovacs
 */

#include <net/netfilter/nf_tproxy.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/inet_sock.h>
#include <linux/ip.h>
#include <net/checksum.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <linux/inetdevice.h>

struct sock *
nf_tproxy_handle_time_wait4(struct net *net, struct sk_buff *skb,
			 __be32 laddr, __be16 lport, struct sock *sk)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct tcphdr _hdr, *hp;

	hp = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_hdr), &_hdr);
	if (hp == NULL) {
		inet_twsk_put(inet_twsk(sk));
		return NULL;
	}

	if (hp->syn && !hp->rst && !hp->ack && !hp->fin) {
		/* SYN to a TIME_WAIT socket, we'd rather redirect it
		 * to a listener socket if there's one */
		struct sock *sk2;

		sk2 = nf_tproxy_get_sock_v4(net, skb, iph->protocol,
					    iph->saddr, laddr ? laddr : iph->daddr,
					    hp->source, lport ? lport : hp->dest,
					    skb->dev, NF_TPROXY_LOOKUP_LISTENER);
		if (sk2) {
			inet_twsk_deschedule_put(inet_twsk(sk));
			sk = sk2;
		}
	}

	return sk;
}
EXPORT_SYMBOL_GPL(nf_tproxy_handle_time_wait4);

__be32 nf_tproxy_laddr4(struct sk_buff *skb, __be32 user_laddr, __be32 daddr)
{
	struct in_device *indev;
	__be32 laddr;

	if (user_laddr)
		return user_laddr;

	laddr = 0;
	indev = __in_dev_get_rcu(skb->dev);
	for_primary_ifa(indev) {
		laddr = ifa->ifa_local;
		break;
	} endfor_ifa(indev);

	return laddr ? laddr : daddr;
}
EXPORT_SYMBOL_GPL(nf_tproxy_laddr4);

struct sock *
nf_tproxy_get_sock_v4(struct net *net, struct sk_buff *skb,
		      const u8 protocol,
		      const __be32 saddr, const __be32 daddr,
		      const __be16 sport, const __be16 dport,
		      const struct net_device *in,
		      const enum nf_tproxy_lookup_t lookup_type)
{
	struct sock *sk;

	switch (protocol) {
	case IPPROTO_TCP: {
		struct tcphdr _hdr, *hp;

		hp = skb_header_pointer(skb, ip_hdrlen(skb),
					sizeof(struct tcphdr), &_hdr);
		if (hp == NULL)
			return NULL;

		switch (lookup_type) {
		case NF_TPROXY_LOOKUP_LISTENER:
			sk = inet_lookup_listener(net, &tcp_hashinfo, skb,
						    ip_hdrlen(skb) +
						      __tcp_hdrlen(hp),
						    saddr, sport,
						    daddr, dport,
						    in->ifindex, 0);

			if (sk && !refcount_inc_not_zero(&sk->sk_refcnt))
				sk = NULL;
			/* NOTE: we return listeners even if bound to
			 * 0.0.0.0, those are filtered out in
			 * xt_socket, since xt_TPROXY needs 0 bound
			 * listeners too
			 */
			break;
		case NF_TPROXY_LOOKUP_ESTABLISHED:
			sk = inet_lookup_established(net, &tcp_hashinfo,
						    saddr, sport, daddr, dport,
						    in->ifindex);
			break;
		default:
			BUG();
		}
		break;
		}
	case IPPROTO_UDP:
		sk = udp4_lib_lookup(net, saddr, sport, daddr, dport,
				     in->ifindex);
		if (sk) {
			int connected = (sk->sk_state == TCP_ESTABLISHED);
			int wildcard = (inet_sk(sk)->inet_rcv_saddr == 0);

			/* NOTE: we return listeners even if bound to
			 * 0.0.0.0, those are filtered out in
			 * xt_socket, since xt_TPROXY needs 0 bound
			 * listeners too
			 */
			if ((lookup_type == NF_TPROXY_LOOKUP_ESTABLISHED &&
			      (!connected || wildcard)) ||
			    (lookup_type == NF_TPROXY_LOOKUP_LISTENER && connected)) {
				sock_put(sk);
				sk = NULL;
			}
		}
		break;
	default:
		WARN_ON(1);
		sk = NULL;
	}

	pr_debug("tproxy socket lookup: proto %u %08x:%u -> %08x:%u, lookup type: %d, sock %p\n",
		 protocol, ntohl(saddr), ntohs(sport), ntohl(daddr), ntohs(dport), lookup_type, sk);

	return sk;
}
EXPORT_SYMBOL_GPL(nf_tproxy_get_sock_v4);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Balazs Scheidler, Krisztian Kovacs");
MODULE_DESCRIPTION("Netfilter IPv4 transparent proxy support");
