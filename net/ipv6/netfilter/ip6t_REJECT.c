/*
 * IP6 tables REJECT target module
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Copyright (c) 2005-2007 Patrick McHardy <kaber@trash.net>
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
#include <net/icmp.h>
#include <net/flow.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_REJECT.h>

#include <net/netfilter/ipv6/nf_reject.h>

MODULE_AUTHOR("Yasuyuki KOZAKAI <yasuyuki.kozakai@toshiba.co.jp>");
MODULE_DESCRIPTION("Xtables: packet \"rejection\" target for IPv6");
MODULE_LICENSE("GPL");

static unsigned int
reject_tg6(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ip6t_reject_info *reject = par->targinfo;
	struct net *net = xt_net(par);

	switch (reject->with) {
	case IP6T_ICMP6_NO_ROUTE:
		nf_send_unreach6(net, skb, ICMPV6_NOROUTE, xt_hooknum(par));
		break;
	case IP6T_ICMP6_ADM_PROHIBITED:
		nf_send_unreach6(net, skb, ICMPV6_ADM_PROHIBITED,
				 xt_hooknum(par));
		break;
	case IP6T_ICMP6_NOT_NEIGHBOUR:
		nf_send_unreach6(net, skb, ICMPV6_NOT_NEIGHBOUR,
				 xt_hooknum(par));
		break;
	case IP6T_ICMP6_ADDR_UNREACH:
		nf_send_unreach6(net, skb, ICMPV6_ADDR_UNREACH,
				 xt_hooknum(par));
		break;
	case IP6T_ICMP6_PORT_UNREACH:
		nf_send_unreach6(net, skb, ICMPV6_PORT_UNREACH,
				 xt_hooknum(par));
		break;
	case IP6T_ICMP6_ECHOREPLY:
		/* Do nothing */
		break;
	case IP6T_TCP_RESET:
		nf_send_reset6(net, skb, xt_hooknum(par));
		break;
	case IP6T_ICMP6_POLICY_FAIL:
		nf_send_unreach6(net, skb, ICMPV6_POLICY_FAIL, xt_hooknum(par));
		break;
	case IP6T_ICMP6_REJECT_ROUTE:
		nf_send_unreach6(net, skb, ICMPV6_REJECT_ROUTE,
				 xt_hooknum(par));
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
		if (!(e->ipv6.flags & IP6T_F_PROTO) ||
		    e->ipv6.proto != IPPROTO_TCP ||
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
