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

#include <linux/config.h>
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
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_REJECT.h>

MODULE_AUTHOR("Yasuyuki KOZAKAI <yasuyuki.kozakai@toshiba.co.jp>");
MODULE_DESCRIPTION("IP6 tables REJECT target module");
MODULE_LICENSE("GPL");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* Send RST reply */
static void send_reset(struct sk_buff *oldskb)
{
	struct sk_buff *nskb;
	struct tcphdr otcph, *tcph;
	unsigned int otcplen, hh_len;
	int tcphoff, needs_ack;
	struct ipv6hdr *oip6h = oldskb->nh.ipv6h, *ip6h;
	struct dst_entry *dst = NULL;
	u8 proto;
	struct flowi fl;

	if ((!(ipv6_addr_type(&oip6h->saddr) & IPV6_ADDR_UNICAST)) ||
	    (!(ipv6_addr_type(&oip6h->daddr) & IPV6_ADDR_UNICAST))) {
		DEBUGP("ip6t_REJECT: addr is not unicast.\n");
		return;
	}

	proto = oip6h->nexthdr;
	tcphoff = ipv6_skip_exthdr(oldskb, ((u8*)(oip6h+1) - oldskb->data), &proto);

	if ((tcphoff < 0) || (tcphoff > oldskb->len)) {
		DEBUGP("ip6t_REJECT: Can't get TCP header.\n");
		return;
	}

	otcplen = oldskb->len - tcphoff;

	/* IP header checks: fragment, too short. */
	if ((proto != IPPROTO_TCP) || (otcplen < sizeof(struct tcphdr))) {
		DEBUGP("ip6t_REJECT: proto(%d) != IPPROTO_TCP, or too short. otcplen = %d\n",
			proto, otcplen);
		return;
	}

	if (skb_copy_bits(oldskb, tcphoff, &otcph, sizeof(struct tcphdr)))
		BUG();

	/* No RST for RST. */
	if (otcph.rst) {
		DEBUGP("ip6t_REJECT: RST is set\n");
		return;
	}

	/* Check checksum. */
	if (csum_ipv6_magic(&oip6h->saddr, &oip6h->daddr, otcplen, IPPROTO_TCP,
			    skb_checksum(oldskb, tcphoff, otcplen, 0))) {
		DEBUGP("ip6t_REJECT: TCP checksum is invalid\n");
		return;
	}

	memset(&fl, 0, sizeof(fl));
	fl.proto = IPPROTO_TCP;
	ipv6_addr_copy(&fl.fl6_src, &oip6h->daddr);
	ipv6_addr_copy(&fl.fl6_dst, &oip6h->saddr);
	fl.fl_ip_sport = otcph.dest;
	fl.fl_ip_dport = otcph.source;
	dst = ip6_route_output(NULL, &fl);
	if (dst == NULL)
		return;
	if (dst->error || xfrm_lookup(&dst, &fl, NULL, 0))
		return;

	hh_len = (dst->dev->hard_header_len + 15)&~15;
	nskb = alloc_skb(hh_len + 15 + dst->header_len + sizeof(struct ipv6hdr)
			 + sizeof(struct tcphdr) + dst->trailer_len,
			 GFP_ATOMIC);

	if (!nskb) {
		if (net_ratelimit())
			printk("ip6t_REJECT: Can't alloc skb\n");
		dst_release(dst);
		return;
	}

	nskb->dst = dst;

	skb_reserve(nskb, hh_len + dst->header_len);

	ip6h = nskb->nh.ipv6h = (struct ipv6hdr *)
					skb_put(nskb, sizeof(struct ipv6hdr));
	ip6h->version = 6;
	ip6h->hop_limit = dst_metric(dst, RTAX_HOPLIMIT);
	ip6h->nexthdr = IPPROTO_TCP;
	ip6h->payload_len = htons(sizeof(struct tcphdr));
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
	tcph->check = csum_ipv6_magic(&nskb->nh.ipv6h->saddr,
				      &nskb->nh.ipv6h->daddr,
				      sizeof(struct tcphdr), IPPROTO_TCP,
				      csum_partial((char *)tcph,
						   sizeof(struct tcphdr), 0));

	nf_ct_attach(nskb, oldskb);

	NF_HOOK(PF_INET6, NF_IP6_LOCAL_OUT, nskb, NULL, nskb->dst->dev,
		dst_output);
}

static inline void
send_unreach(struct sk_buff *skb_in, unsigned char code, unsigned int hooknum)
{
	if (hooknum == NF_IP6_LOCAL_OUT && skb_in->dev == NULL)
		skb_in->dev = &loopback_dev;

	icmpv6_send(skb_in, ICMPV6_DEST_UNREACH, code, 0, NULL);
}

static unsigned int reject6_target(struct sk_buff **pskb,
			   const struct net_device *in,
			   const struct net_device *out,
			   unsigned int hooknum,
			   const struct xt_target *target,
			   const void *targinfo,
			   void *userinfo)
{
	const struct ip6t_reject_info *reject = targinfo;

	DEBUGP(KERN_DEBUG "%s: medium point\n", __FUNCTION__);
	/* WARNING: This code causes reentry within ip6tables.
	   This means that the ip6tables jump stack is now crap.  We
	   must return an absolute verdict. --RR */
    	switch (reject->with) {
    	case IP6T_ICMP6_NO_ROUTE:
    		send_unreach(*pskb, ICMPV6_NOROUTE, hooknum);
    		break;
    	case IP6T_ICMP6_ADM_PROHIBITED:
    		send_unreach(*pskb, ICMPV6_ADM_PROHIBITED, hooknum);
    		break;
    	case IP6T_ICMP6_NOT_NEIGHBOUR:
    		send_unreach(*pskb, ICMPV6_NOT_NEIGHBOUR, hooknum);
    		break;
    	case IP6T_ICMP6_ADDR_UNREACH:
    		send_unreach(*pskb, ICMPV6_ADDR_UNREACH, hooknum);
    		break;
    	case IP6T_ICMP6_PORT_UNREACH:
    		send_unreach(*pskb, ICMPV6_PORT_UNREACH, hooknum);
    		break;
    	case IP6T_ICMP6_ECHOREPLY:
		/* Do nothing */
		break;
	case IP6T_TCP_RESET:
		send_reset(*pskb);
		break;
	default:
		if (net_ratelimit())
			printk(KERN_WARNING "ip6t_REJECT: case %u not handled yet\n", reject->with);
		break;
	}

	return NF_DROP;
}

static int check(const char *tablename,
		 const void *entry,
		 const struct xt_target *target,
		 void *targinfo,
		 unsigned int targinfosize,
		 unsigned int hook_mask)
{
 	const struct ip6t_reject_info *rejinfo = targinfo;
	const struct ip6t_entry *e = entry;

	if (rejinfo->with == IP6T_ICMP6_ECHOREPLY) {
		printk("ip6t_REJECT: ECHOREPLY is not supported.\n");
		return 0;
	} else if (rejinfo->with == IP6T_TCP_RESET) {
		/* Must specify that it's a TCP packet */
		if (e->ipv6.proto != IPPROTO_TCP
		    || (e->ipv6.invflags & IP6T_INV_PROTO)) {
			DEBUGP("ip6t_REJECT: TCP_RESET illegal for non-tcp\n");
			return 0;
		}
	}
	return 1;
}

static struct ip6t_target ip6t_reject_reg = {
	.name		= "REJECT",
	.target		= reject6_target,
	.targetsize	= sizeof(struct ip6t_reject_info),
	.table		= "filter",
	.hooks		= (1 << NF_IP6_LOCAL_IN) | (1 << NF_IP6_FORWARD) |
			  (1 << NF_IP6_LOCAL_OUT),
	.checkentry	= check,
	.me		= THIS_MODULE
};

static int __init ip6t_reject_init(void)
{
	if (ip6t_register_target(&ip6t_reject_reg))
		return -EINVAL;
	return 0;
}

static void __exit ip6t_reject_fini(void)
{
	ip6t_unregister_target(&ip6t_reject_reg);
}

module_init(ip6t_reject_init);
module_exit(ip6t_reject_fini);
