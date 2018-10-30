/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on Rusty Russell's IPv6 MASQUERADE target. Development of IPv6
 * NAT funded by Astaro.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_nat.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <net/netfilter/ipv6/nf_nat_masquerade.h>

static unsigned int
masquerade_tg6(struct sk_buff *skb, const struct xt_action_param *par)
{
	return nf_nat_masquerade_ipv6(skb, par->targinfo, xt_out(par));
}

static int masquerade_tg6_checkentry(const struct xt_tgchk_param *par)
{
	const struct nf_nat_range2 *range = par->targinfo;

	if (range->flags & NF_NAT_RANGE_MAP_IPS)
		return -EINVAL;
	return nf_ct_netns_get(par->net, par->family);
}

static void masquerade_tg6_destroy(const struct xt_tgdtor_param *par)
{
	nf_ct_netns_put(par->net, par->family);
}

static struct xt_target masquerade_tg6_reg __read_mostly = {
	.name		= "MASQUERADE",
	.family		= NFPROTO_IPV6,
	.checkentry	= masquerade_tg6_checkentry,
	.destroy	= masquerade_tg6_destroy,
	.target		= masquerade_tg6,
	.targetsize	= sizeof(struct nf_nat_range),
	.table		= "nat",
	.hooks		= 1 << NF_INET_POST_ROUTING,
	.me		= THIS_MODULE,
};

static int __init masquerade_tg6_init(void)
{
	int err;

	err = xt_register_target(&masquerade_tg6_reg);
	if (err == 0)
		nf_nat_masquerade_ipv6_register_notifier();

	return err;
}
static void __exit masquerade_tg6_exit(void)
{
	nf_nat_masquerade_ipv6_unregister_notifier();
	xt_unregister_target(&masquerade_tg6_reg);
}

module_init(masquerade_tg6_init);
module_exit(masquerade_tg6_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Xtables: automatic address SNAT");
