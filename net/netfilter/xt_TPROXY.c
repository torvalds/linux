/*
 * Transparent proxy support for Linux/iptables
 *
 * Copyright (c) 2006-2010 BalaBit IT Ltd.
 * Author: Balazs Scheidler, Krisztian Kovacs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/checksum.h>
#include <net/udp.h>
#include <net/inet_sock.h>
#include <linux/inetdevice.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>

#include <net/netfilter/ipv4/nf_defrag_ipv4.h>

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
#define XT_TPROXY_HAVE_IPV6 1
#include <net/if_inet6.h>
#include <net/addrconf.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>
#endif

#include <net/netfilter/nf_tproxy_core.h>
#include <linux/netfilter/xt_TPROXY.h>

static bool tproxy_sk_is_transparent(struct sock *sk)
{
	if (sk->sk_state != TCP_TIME_WAIT) {
		if (inet_sk(sk)->transparent)
			return true;
		sock_put(sk);
	} else {
		if (inet_twsk(sk)->tw_transparent)
			return true;
		inet_twsk_put(inet_twsk(sk));
	}
	return false;
}

static inline __be32
tproxy_laddr4(struct sk_buff *skb, __be32 user_laddr, __be32 daddr)
{
	struct in_device *indev;
	__be32 laddr;

	if (user_laddr)
		return user_laddr;

	laddr = 0;
	rcu_read_lock();
	indev = __in_dev_get_rcu(skb->dev);
	for_primary_ifa(indev) {
		laddr = ifa->ifa_local;
		break;
	} endfor_ifa(indev);
	rcu_read_unlock();

	return laddr ? laddr : daddr;
}

/**
 * tproxy_handle_time_wait4() - handle IPv4 TCP TIME_WAIT reopen redirections
 * @skb:	The skb being processed.
 * @laddr:	IPv4 address to redirect to or zero.
 * @lport:	TCP port to redirect to or zero.
 * @sk:		The TIME_WAIT TCP socket found by the lookup.
 *
 * We have to handle SYN packets arriving to TIME_WAIT sockets
 * differently: instead of reopening the connection we should rather
 * redirect the new connection to the proxy if there's a listener
 * socket present.
 *
 * tproxy_handle_time_wait4() consumes the socket reference passed in.
 *
 * Returns the listener socket if there's one, the TIME_WAIT socket if
 * no such listener is found, or NULL if the TCP header is incomplete.
 */
static struct sock *
tproxy_handle_time_wait4(struct sk_buff *skb, __be32 laddr, __be16 lport,
			struct sock *sk)
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

		sk2 = nf_tproxy_get_sock_v4(dev_net(skb->dev), iph->protocol,
					    iph->saddr, laddr ? laddr : iph->daddr,
					    hp->source, lport ? lport : hp->dest,
					    skb->dev, NFT_LOOKUP_LISTENER);
		if (sk2) {
			inet_twsk_deschedule(inet_twsk(sk), &tcp_death_row);
			inet_twsk_put(inet_twsk(sk));
			sk = sk2;
		}
	}

	return sk;
}

static unsigned int
tproxy_tg4(struct sk_buff *skb, __be32 laddr, __be16 lport,
	   u_int32_t mark_mask, u_int32_t mark_value)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct udphdr _hdr, *hp;
	struct sock *sk;

	hp = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return NF_DROP;

	/* check if there's an ongoing connection on the packet
	 * addresses, this happens if the redirect already happened
	 * and the current packet belongs to an already established
	 * connection */
	sk = nf_tproxy_get_sock_v4(dev_net(skb->dev), iph->protocol,
				   iph->saddr, iph->daddr,
				   hp->source, hp->dest,
				   skb->dev, NFT_LOOKUP_ESTABLISHED);

	laddr = tproxy_laddr4(skb, laddr, iph->daddr);
	if (!lport)
		lport = hp->dest;

	/* UDP has no TCP_TIME_WAIT state, so we never enter here */
	if (sk && sk->sk_state == TCP_TIME_WAIT)
		/* reopening a TIME_WAIT connection needs special handling */
		sk = tproxy_handle_time_wait4(skb, laddr, lport, sk);
	else if (!sk)
		/* no, there's no established connection, check if
		 * there's a listener on the redirected addr/port */
		sk = nf_tproxy_get_sock_v4(dev_net(skb->dev), iph->protocol,
					   iph->saddr, laddr,
					   hp->source, lport,
					   skb->dev, NFT_LOOKUP_LISTENER);

	/* NOTE: assign_sock consumes our sk reference */
	if (sk && tproxy_sk_is_transparent(sk)) {
		/* This should be in a separate target, but we don't do multiple
		   targets on the same rule yet */
		skb->mark = (skb->mark & ~mark_mask) ^ mark_value;

		pr_debug("redirecting: proto %hhu %pI4:%hu -> %pI4:%hu, mark: %x\n",
			 iph->protocol, &iph->daddr, ntohs(hp->dest),
			 &laddr, ntohs(lport), skb->mark);

		nf_tproxy_assign_sock(skb, sk);
		return NF_ACCEPT;
	}

	pr_debug("no socket, dropping: proto %hhu %pI4:%hu -> %pI4:%hu, mark: %x\n",
		 iph->protocol, &iph->saddr, ntohs(hp->source),
		 &iph->daddr, ntohs(hp->dest), skb->mark);
	return NF_DROP;
}

static unsigned int
tproxy_tg4_v0(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_tproxy_target_info *tgi = par->targinfo;

	return tproxy_tg4(skb, tgi->laddr, tgi->lport, tgi->mark_mask, tgi->mark_value);
}

static unsigned int
tproxy_tg4_v1(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_tproxy_target_info_v1 *tgi = par->targinfo;

	return tproxy_tg4(skb, tgi->laddr.ip, tgi->lport, tgi->mark_mask, tgi->mark_value);
}

#ifdef XT_TPROXY_HAVE_IPV6

static inline const struct in6_addr *
tproxy_laddr6(struct sk_buff *skb, const struct in6_addr *user_laddr,
	      const struct in6_addr *daddr)
{
	struct inet6_dev *indev;
	struct inet6_ifaddr *ifa;
	struct in6_addr *laddr;

	if (!ipv6_addr_any(user_laddr))
		return user_laddr;
	laddr = NULL;

	rcu_read_lock();
	indev = __in6_dev_get(skb->dev);
	if (indev)
		list_for_each_entry(ifa, &indev->addr_list, if_list) {
			if (ifa->flags & (IFA_F_TENTATIVE | IFA_F_DEPRECATED))
				continue;

			laddr = &ifa->addr;
			break;
		}
	rcu_read_unlock();

	return laddr ? laddr : daddr;
}

/**
 * tproxy_handle_time_wait6() - handle IPv6 TCP TIME_WAIT reopen redirections
 * @skb:	The skb being processed.
 * @tproto:	Transport protocol.
 * @thoff:	Transport protocol header offset.
 * @par:	Iptables target parameters.
 * @sk:		The TIME_WAIT TCP socket found by the lookup.
 *
 * We have to handle SYN packets arriving to TIME_WAIT sockets
 * differently: instead of reopening the connection we should rather
 * redirect the new connection to the proxy if there's a listener
 * socket present.
 *
 * tproxy_handle_time_wait6() consumes the socket reference passed in.
 *
 * Returns the listener socket if there's one, the TIME_WAIT socket if
 * no such listener is found, or NULL if the TCP header is incomplete.
 */
static struct sock *
tproxy_handle_time_wait6(struct sk_buff *skb, int tproto, int thoff,
			 const struct xt_action_param *par,
			 struct sock *sk)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct tcphdr _hdr, *hp;
	const struct xt_tproxy_target_info_v1 *tgi = par->targinfo;

	hp = skb_header_pointer(skb, thoff, sizeof(_hdr), &_hdr);
	if (hp == NULL) {
		inet_twsk_put(inet_twsk(sk));
		return NULL;
	}

	if (hp->syn && !hp->rst && !hp->ack && !hp->fin) {
		/* SYN to a TIME_WAIT socket, we'd rather redirect it
		 * to a listener socket if there's one */
		struct sock *sk2;

		sk2 = nf_tproxy_get_sock_v6(dev_net(skb->dev), tproto,
					    &iph->saddr,
					    tproxy_laddr6(skb, &tgi->laddr.in6, &iph->daddr),
					    hp->source,
					    tgi->lport ? tgi->lport : hp->dest,
					    skb->dev, NFT_LOOKUP_LISTENER);
		if (sk2) {
			inet_twsk_deschedule(inet_twsk(sk), &tcp_death_row);
			inet_twsk_put(inet_twsk(sk));
			sk = sk2;
		}
	}

	return sk;
}

static unsigned int
tproxy_tg6_v1(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	const struct xt_tproxy_target_info_v1 *tgi = par->targinfo;
	struct udphdr _hdr, *hp;
	struct sock *sk;
	const struct in6_addr *laddr;
	__be16 lport;
	int thoff;
	int tproto;

	tproto = ipv6_find_hdr(skb, &thoff, -1, NULL);
	if (tproto < 0) {
		pr_debug("unable to find transport header in IPv6 packet, dropping\n");
		return NF_DROP;
	}

	hp = skb_header_pointer(skb, thoff, sizeof(_hdr), &_hdr);
	if (hp == NULL) {
		pr_debug("unable to grab transport header contents in IPv6 packet, dropping\n");
		return NF_DROP;
	}

	/* check if there's an ongoing connection on the packet
	 * addresses, this happens if the redirect already happened
	 * and the current packet belongs to an already established
	 * connection */
	sk = nf_tproxy_get_sock_v6(dev_net(skb->dev), tproto,
				   &iph->saddr, &iph->daddr,
				   hp->source, hp->dest,
				   par->in, NFT_LOOKUP_ESTABLISHED);

	laddr = tproxy_laddr6(skb, &tgi->laddr.in6, &iph->daddr);
	lport = tgi->lport ? tgi->lport : hp->dest;

	/* UDP has no TCP_TIME_WAIT state, so we never enter here */
	if (sk && sk->sk_state == TCP_TIME_WAIT)
		/* reopening a TIME_WAIT connection needs special handling */
		sk = tproxy_handle_time_wait6(skb, tproto, thoff, par, sk);
	else if (!sk)
		/* no there's no established connection, check if
		 * there's a listener on the redirected addr/port */
		sk = nf_tproxy_get_sock_v6(dev_net(skb->dev), tproto,
					   &iph->saddr, laddr,
					   hp->source, lport,
					   par->in, NFT_LOOKUP_LISTENER);

	/* NOTE: assign_sock consumes our sk reference */
	if (sk && tproxy_sk_is_transparent(sk)) {
		/* This should be in a separate target, but we don't do multiple
		   targets on the same rule yet */
		skb->mark = (skb->mark & ~tgi->mark_mask) ^ tgi->mark_value;

		pr_debug("redirecting: proto %hhu %pI6:%hu -> %pI6:%hu, mark: %x\n",
			 tproto, &iph->saddr, ntohs(hp->source),
			 laddr, ntohs(lport), skb->mark);

		nf_tproxy_assign_sock(skb, sk);
		return NF_ACCEPT;
	}

	pr_debug("no socket, dropping: proto %hhu %pI6:%hu -> %pI6:%hu, mark: %x\n",
		 tproto, &iph->saddr, ntohs(hp->source),
		 &iph->daddr, ntohs(hp->dest), skb->mark);

	return NF_DROP;
}

static int tproxy_tg6_check(const struct xt_tgchk_param *par)
{
	const struct ip6t_ip6 *i = par->entryinfo;

	if ((i->proto == IPPROTO_TCP || i->proto == IPPROTO_UDP)
	    && !(i->flags & IP6T_INV_PROTO))
		return 0;

	pr_info("Can be used only in combination with "
		"either -p tcp or -p udp\n");
	return -EINVAL;
}
#endif

static int tproxy_tg4_check(const struct xt_tgchk_param *par)
{
	const struct ipt_ip *i = par->entryinfo;

	if ((i->proto == IPPROTO_TCP || i->proto == IPPROTO_UDP)
	    && !(i->invflags & IPT_INV_PROTO))
		return 0;

	pr_info("Can be used only in combination with "
		"either -p tcp or -p udp\n");
	return -EINVAL;
}

static struct xt_target tproxy_tg_reg[] __read_mostly = {
	{
		.name		= "TPROXY",
		.family		= NFPROTO_IPV4,
		.table		= "mangle",
		.target		= tproxy_tg4_v0,
		.revision	= 0,
		.targetsize	= sizeof(struct xt_tproxy_target_info),
		.checkentry	= tproxy_tg4_check,
		.hooks		= 1 << NF_INET_PRE_ROUTING,
		.me		= THIS_MODULE,
	},
	{
		.name		= "TPROXY",
		.family		= NFPROTO_IPV4,
		.table		= "mangle",
		.target		= tproxy_tg4_v1,
		.revision	= 1,
		.targetsize	= sizeof(struct xt_tproxy_target_info_v1),
		.checkentry	= tproxy_tg4_check,
		.hooks		= 1 << NF_INET_PRE_ROUTING,
		.me		= THIS_MODULE,
	},
#ifdef XT_TPROXY_HAVE_IPV6
	{
		.name		= "TPROXY",
		.family		= NFPROTO_IPV6,
		.table		= "mangle",
		.target		= tproxy_tg6_v1,
		.revision	= 1,
		.targetsize	= sizeof(struct xt_tproxy_target_info_v1),
		.checkentry	= tproxy_tg6_check,
		.hooks		= 1 << NF_INET_PRE_ROUTING,
		.me		= THIS_MODULE,
	},
#endif

};

static int __init tproxy_tg_init(void)
{
	nf_defrag_ipv4_enable();
#ifdef XT_TPROXY_HAVE_IPV6
	nf_defrag_ipv6_enable();
#endif

	return xt_register_targets(tproxy_tg_reg, ARRAY_SIZE(tproxy_tg_reg));
}

static void __exit tproxy_tg_exit(void)
{
	xt_unregister_targets(tproxy_tg_reg, ARRAY_SIZE(tproxy_tg_reg));
}

module_init(tproxy_tg_init);
module_exit(tproxy_tg_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Balazs Scheidler, Krisztian Kovacs");
MODULE_DESCRIPTION("Netfilter transparent proxy (TPROXY) target module.");
MODULE_ALIAS("ipt_TPROXY");
MODULE_ALIAS("ip6t_TPROXY");
