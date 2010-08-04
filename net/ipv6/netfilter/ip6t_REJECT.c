/*
 * IP6 tables REJECT target module
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on net/ipv4/netfilter/ipt_REJECT.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmpv6.h>
#include <linux/netdevice.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <net/icmp.h>
#include <net/ip6_checksum.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/flow.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_REJECT.h>

MODULE_AUTHOR("Yasuyuki KOZAKAI <yasuyuki.kozakai@toshiba.co.jp>");
MODULE_DESCRIPTION("Xtables: packet \"rejection\" target for IPv6");
MODULE_LICENSE("GPL");

/* Send RST reply */
static void send_reset(struct net *net, struct sk_buff *oldskb)
{
	struct sk_buff *nskb;
	struct tcphdr otcph, *tcph;
	unsigned int otcplen, hh_len;
	int tcphoff, needs_ack;
	const struct ipv6hdr *oip6h = ipv6_hdr(oldskb);
	struct ipv6hdr *ip6h;
	struct dst_entry *dst = NULL;
	u8 proto;
	struct flowi fl;

	if ((!(ipv6_addr_type(&oip6h->saddr) & IPV6_ADDR_UNICAST)) ||
	    (!(ipv6_addr_type(&oip6h->daddr) & IPV6_ADDR_UNICAST))) {
		pr_debug("addr is not unicast.\n");
		return;
	}

	proto = oip6h->nexthdr;
	tcphoff = ipv6_skip_exthdr(oldskb, ((u8*)(oip6h+1) - oldskb->data), &proto);

	if ((tcphoff < 0) || (tcphoff > oldskb->len)) {
		pr_debug("Cannot get TCP header.\n");
		return;
	}

	otcplen = oldskb->len - tcphoff;

	/* IP header checks: fragment, too short. */
	if (proto != IPPROTO_TCP || otcplen < sizeof(struct tcphdr)) {
		pr_debug("proto(%d) != IPPROTO_TCP, "
			 "or too short. otcplen = %d\n",
			 proto, otcplen);
		return;
	}

	if (skb_copy_bits(oldskb, tcphoff, &otcph, sizeof(struct tcphdr)))
		BUG();

	/* No RST for RST. */
	if (otcph.rst) {
		pr_debug("RST is set\n");
		return;
	}

	/* Check checksum. */
	if (csum_ipv6_magic(&oip6h->saddr, &oip6h->daddr, otcplen, IPPROTO_TCP,
			    skb_checksum(oldskb, tcphoff, otcplen, 0))) {
		pr_debug("TCP checksum is invalid\n");
		return;
	}

	memset(&fl, 0, sizeof(fl));
	fl.proto = IPPROTO_TCP;
	ipv6_addr_copy(&fl.fl6_src, &oip6h->daddr);
	ipv6_addr_copy(&fl.fl6_dst, &oip6h->saddr);
	fl.fl_ip_sport = otcph.dest;
	fl.fl_ip_dport = otcph.source;
	security_skb_classify_flow(oldskb, &fl);
	dst = ip6_route_output(net, NULL, &fl);
	if (dst == NULL || dst->error) {
		dst_release(dst);
		return;
	}
	if (xfrm_lookup(net, &dst, &fl, NULL, 0))
		return;

	hh_len = (dst->dev->hard_header_len + 15)&~15;
	nskb = alloc_skb(hh_len + 15 + dst->header_len + sizeof(struct ipv6hdr)
			 + sizeof(struct tcphdr) + dst->trailer_len,
			 GFP_ATOMIC);

	if (!nskb) {
		if (net_ratelimit())
			pr_debug("cannot alloc skb\n");
		dst_release(dst);
		return;
	}

	skb_dst_set(nskb, dst);

	skb_reserve(nskb, hh_len + dst->header_len);

	skb_put(nskb, sizeof(struct ipv6hdr));
	skb_reset_network_header(nskb);
	ip6h = ipv6_hdr(nskb);
	ip6h->version = 6;
	ip6h->hop_limit = dst_metric(dst, RTAX_HOPLIMIT);
	ip6h->nexthdr = IPPROTO_TCP;
	ipv6_addr_copy(&ip6h->saddr, &oip6h->daddr);
	ipv6_addr_copy(&ip6h->daddr, &oip6h->saddr);

	tcph = (struct tcphdr *)skb_put(nskb, sizeof(struct tcphdr));
	/* Truncate to length (no data) */
	tcph->doff = sizeof(struct tcphdr)/4;
	tcph->source = otcph.dest;
	tcph->dest = otcph.source;

	if (otcph.ack) {
		needs_ack = 0;
		tcph->seq = otcph.ack_seq;
		tcph->ack_seq = 0;
	} else {
		needs_ack = 1;
		tcph->ack_seq = htonl(ntohl(otcph.seq) + otcph.syn + otcph.fin
				      + otcplen - (otcph.doff<<2));
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

	nf_ct_attach(nskb, oldskb);

	ip6_local_out(nskb);
}

static inline void
send_unreach(struct net *net, struct sk_buff *skb_in, unsigned char code,
	     unsigned int hooknum)
{
	if (hooknum == NF_INET_LOCAL_OUT && skb_in->dev == NULL)
		skb_in->dev = net->loopback_dev;

	icmpv6_send(skb_in, ICMPV6_DEST_UNREACH, code, 0);
}

static unsigned int
reject_tg6(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ip6t_reject_info *reject = par->targinfo;
	struct net *net = dev_net((par->in != NULL) ? par->in : par->out);

	pr_debug("%s: medium point\n", __func__);
	switch (reject->with) {
	case IP6T_ICMP6_NO_ROUTE:
		send_unreach(net, skb, ICMPV6_NOROUTE, par->hooknum);
		break;
	case IP6T_ICMP6_ADM_PROHIBITED:
		send_unreach(net, skb, ICMPV6_ADM_PROHIBITED, par->hooknum);
		break;
	case IP6T_ICMP6_NOT_NEIGHBOUR:
		send_unreach(net, skb, ICMPV6_NOT_NEIGHBOUR, par->hooknum);
		break;
	case IP6T_ICMP6_ADDR_UNREACH:
		send_unreach(net, skb, ICMPV6_ADDR_UNREACH, par->hooknum);
		break;
	case IP6T_ICMP6_PORT_UNREACH:
		send_unreach(net, skb, ICMPV6_PORT_UNREACH, par->hooknum);
		break;
	case IP6T_ICMP6_ECHOREPLY:
		/* Do nothing */
		break;
	case IP6T_TCP_RESET:
		send_reset(net, skb);
		break;
	default:
		if (net_ratelimit())
			pr_info("case %u not handled yet\n", reject->with);
		break;
	}

	return NF_DROP;
}

static int reject_tg6_check(const struct xt_tgchk_param *par)
{
	const struct ip6t_reject_info *rejinfo = par->targinfo;
	const struct ip6t_entry *e = par->entryinfo;

	if (rejinfo->with == IP6T_ICMP6_ECHOREPLY) {
		pr_info("ECHOREPLY is not supported.\n");
		return -EINVAL;
	} else if (rejinfo->with == IP6T_TCP_RESET) {
		/* Must specify that it's a TCP packet */
		if (e->ipv6.proto != IPPROTO_TCP ||
		    (e->ipv6.invflags & XT_INV_PROTO)) {
			pr_info("TCP_RESET illegal for non-tcp\n");
			return -EINVAL;
		}
	}
	return 0;
}

static struct xt_target reject_tg6_reg __read_mostly = {
	.name		= "REJECT",
	.family		= NFPROTO_IPV6,
	.target		= reject_tg6,
	.targetsize	= sizeof(struct ip6t_reject_info),
	.table		= "filter",
	.hooks		= (1 << NF_INET_LOCAL_IN) | (1 << NF_INET_FORWARD) |
			  (1 << NF_INET_LOCAL_OUT),
	.checkentry	= reject_tg6_check,
	.me		= THIS_MODULE
};

static int __init reject_tg6_init(void)
{
	return xt_register_target(&reject_tg6_reg);
}

static void __exit reject_tg6_exit(void)
{
	xt_unregister_target(&reject_tg6_reg);
}

module_init(reject_tg6_init);
module_exit(reject_tg6_exit);
