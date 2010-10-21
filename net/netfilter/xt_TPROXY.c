/*
 * Transparent proxy support for Linux/iptables
 *
 * Copyright (c) 2006-2007 BalaBit IT Ltd.
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

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter/xt_TPROXY.h>

#include <net/netfilter/ipv4/nf_defrag_ipv4.h>
#include <net/netfilter/nf_tproxy_core.h>

/**
 * tproxy_handle_time_wait() - handle TCP TIME_WAIT reopen redirections
 * @skb:	The skb being processed.
 * @par:	Iptables target parameters.
 * @sk:		The TIME_WAIT TCP socket found by the lookup.
 *
 * We have to handle SYN packets arriving to TIME_WAIT sockets
 * differently: instead of reopening the connection we should rather
 * redirect the new connection to the proxy if there's a listener
 * socket present.
 *
 * tproxy_handle_time_wait() consumes the socket reference passed in.
 *
 * Returns the listener socket if there's one, the TIME_WAIT socket if
 * no such listener is found, or NULL if the TCP header is incomplete.
 */
static struct sock *
tproxy_handle_time_wait(struct sk_buff *skb, const struct xt_action_param *par, struct sock *sk)
{
	const struct iphdr *iph = ip_hdr(skb);
	const struct xt_tproxy_target_info *tgi = par->targinfo;
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
					    iph->saddr, tgi->laddr ? tgi->laddr : iph->daddr,
					    hp->source, tgi->lport ? tgi->lport : hp->dest,
					    par->in, NFT_LOOKUP_LISTENER);
		if (sk2) {
			/* yeah, there's one, let's kill the TIME_WAIT
			 * socket and redirect to the listener
			 */
			inet_twsk_deschedule(inet_twsk(sk), &tcp_death_row);
			inet_twsk_put(inet_twsk(sk));
			sk = sk2;
		}
	}

	return sk;
}

static unsigned int
tproxy_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct iphdr *iph = ip_hdr(skb);
	const struct xt_tproxy_target_info *tgi = par->targinfo;
	struct udphdr _hdr, *hp;
	struct sock *sk;

	hp = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return NF_DROP;

	sk = nf_tproxy_get_sock_v4(dev_net(skb->dev), iph->protocol,
				   iph->saddr, iph->daddr,
				   hp->source, hp->dest,
				   par->in, NFT_LOOKUP_ESTABLISHED);

	/* UDP has no TCP_TIME_WAIT state, so we never enter here */
	if (sk && sk->sk_state == TCP_TIME_WAIT)
		sk = tproxy_handle_time_wait(skb, par, sk);
	else if (!sk)
		sk = nf_tproxy_get_sock_v4(dev_net(skb->dev), iph->protocol,
					   iph->saddr, tgi->laddr ? tgi->laddr : iph->daddr,
					   hp->source, tgi->lport ? tgi->lport : hp->dest,
					   par->in, NFT_LOOKUP_LISTENER);

	/* NOTE: assign_sock consumes our sk reference */
	if (sk && nf_tproxy_assign_sock(skb, sk)) {
		/* This should be in a separate target, but we don't do multiple
		   targets on the same rule yet */
		skb->mark = (skb->mark & ~tgi->mark_mask) ^ tgi->mark_value;

		pr_debug("redirecting: proto %u %08x:%u -> %08x:%u, mark: %x\n",
			 iph->protocol, ntohl(iph->daddr), ntohs(hp->dest),
			 ntohl(tgi->laddr), ntohs(tgi->lport), skb->mark);
		return NF_ACCEPT;
	}

	pr_debug("no socket, dropping: proto %u %08x:%u -> %08x:%u, mark: %x\n",
		 iph->protocol, ntohl(iph->daddr), ntohs(hp->dest),
		 ntohl(tgi->laddr), ntohs(tgi->lport), skb->mark);
	return NF_DROP;
}

static int tproxy_tg_check(const struct xt_tgchk_param *par)
{
	const struct ipt_ip *i = par->entryinfo;

	if ((i->proto == IPPROTO_TCP || i->proto == IPPROTO_UDP)
	    && !(i->invflags & IPT_INV_PROTO))
		return 0;

	pr_info("Can be used only in combination with "
		"either -p tcp or -p udp\n");
	return -EINVAL;
}

static struct xt_target tproxy_tg_reg __read_mostly = {
	.name		= "TPROXY",
	.family		= NFPROTO_IPV4,
	.table		= "mangle",
	.target		= tproxy_tg,
	.targetsize	= sizeof(struct xt_tproxy_target_info),
	.checkentry	= tproxy_tg_check,
	.hooks		= 1 << NF_INET_PRE_ROUTING,
	.me		= THIS_MODULE,
};

static int __init tproxy_tg_init(void)
{
	nf_defrag_ipv4_enable();
	return xt_register_target(&tproxy_tg_reg);
}

static void __exit tproxy_tg_exit(void)
{
	xt_unregister_target(&tproxy_tg_reg);
}

module_init(tproxy_tg_init);
module_exit(tproxy_tg_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krisztian Kovacs");
MODULE_DESCRIPTION("Netfilter transparent proxy (TPROXY) target module.");
MODULE_ALIAS("ipt_TPROXY");
