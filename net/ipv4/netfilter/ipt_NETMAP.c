/* NETMAP - static NAT mapping of IP network addresses (1:1).
 * The mapping can be applied to source (POSTROUTING),
 * destination (PREROUTING), or both (with separate rules).
 */

/* (C) 2000-2001 Svenning Soerensen <svenning@post5.tele.dk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_nat_rule.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Svenning Soerensen <svenning@post5.tele.dk>");
MODULE_DESCRIPTION("Xtables: 1:1 NAT mapping of IPv4 subnets");

static int netmap_tg_check(const struct xt_tgchk_param *par)
{
	const struct nf_nat_multi_range_compat *mr = par->targinfo;

	if (!(mr->range[0].flags & IP_NAT_RANGE_MAP_IPS)) {
		pr_debug("bad MAP_IPS.\n");
		return -EINVAL;
	}
	if (mr->rangesize != 1) {
		pr_debug("bad rangesize %u.\n", mr->rangesize);
		return -EINVAL;
	}
	return 0;
}

static unsigned int
netmap_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	__be32 new_ip, netmask;
	const struct nf_nat_multi_range_compat *mr = par->targinfo;
	struct nf_nat_range newrange;

	NF_CT_ASSERT(par->hooknum == NF_INET_PRE_ROUTING ||
		     par->hooknum == NF_INET_POST_ROUTING ||
		     par->hooknum == NF_INET_LOCAL_OUT);
	ct = nf_ct_get(skb, &ctinfo);

	netmask = ~(mr->range[0].min_ip ^ mr->range[0].max_ip);

	if (par->hooknum == NF_INET_PRE_ROUTING ||
	    par->hooknum == NF_INET_LOCAL_OUT)
		new_ip = ip_hdr(skb)->daddr & ~netmask;
	else
		new_ip = ip_hdr(skb)->saddr & ~netmask;
	new_ip |= mr->range[0].min_ip & netmask;

	newrange = ((struct nf_nat_range)
		{ mr->range[0].flags | IP_NAT_RANGE_MAP_IPS,
		  new_ip, new_ip,
		  mr->range[0].min, mr->range[0].max });

	/* Hand modified range to generic setup. */
	return nf_nat_setup_info(ct, &newrange, HOOK2MANIP(par->hooknum));
}

static struct xt_target netmap_tg_reg __read_mostly = {
	.name 		= "NETMAP",
	.family		= NFPROTO_IPV4,
	.target 	= netmap_tg,
	.targetsize	= sizeof(struct nf_nat_multi_range_compat),
	.table		= "nat",
	.hooks		= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT),
	.checkentry 	= netmap_tg_check,
	.me 		= THIS_MODULE
};

static int __init netmap_tg_init(void)
{
	return xt_register_target(&netmap_tg_reg);
}

static void __exit netmap_tg_exit(void)
{
	xt_unregister_target(&netmap_tg_reg);
}

module_init(netmap_tg_init);
module_exit(netmap_tg_exit);
