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

static unsigned int
masquerade_tg6(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct nf_nat_range *range = par->targinfo;
	enum ip_conntrack_info ctinfo;
	struct in6_addr src;
	struct nf_conn *ct;
	struct nf_nat_range newrange;

	ct = nf_ct_get(skb, &ctinfo);
	NF_CT_ASSERT(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED ||
			    ctinfo == IP_CT_RELATED_REPLY));

	if (ipv6_dev_get_saddr(dev_net(par->out), par->out,
			       &ipv6_hdr(skb)->daddr, 0, &src) < 0)
		return NF_DROP;

	nfct_nat(ct)->masq_index = par->out->ifindex;

	newrange.flags		= range->flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr.in6	= src;
	newrange.max_addr.in6	= src;
	newrange.min_proto	= range->min_proto;
	newrange.max_proto	= range->max_proto;

	return nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_SRC);
}

static int masquerade_tg6_checkentry(const struct xt_tgchk_param *par)
{
	const struct nf_nat_range *range = par->targinfo;

	if (range->flags & NF_NAT_RANGE_MAP_IPS)
		return -EINVAL;
	return 0;
}

static int device_cmp(struct nf_conn *ct, void *ifindex)
{
	const struct nf_conn_nat *nat = nfct_nat(ct);

	if (!nat)
		return 0;
	if (nf_ct_l3num(ct) != NFPROTO_IPV6)
		return 0;
	return nat->masq_index == (int)(long)ifindex;
}

static int masq_device_event(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
	const struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);

	if (event == NETDEV_DOWN)
		nf_ct_iterate_cleanup(net, device_cmp,
				      (void *)(long)dev->ifindex);

	return NOTIFY_DONE;
}

static struct notifier_block masq_dev_notifier = {
	.notifier_call	= masq_device_event,
};

static int masq_inet_event(struct notifier_block *this,
			   unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa = ptr;
	struct netdev_notifier_info info;

	netdev_notifier_info_init(&info, ifa->idev->dev);
	return masq_device_event(this, event, &info);
}

static struct notifier_block masq_inet_notifier = {
	.notifier_call	= masq_inet_event,
};

static struct xt_target masquerade_tg6_reg __read_mostly = {
	.name		= "MASQUERADE",
	.family		= NFPROTO_IPV6,
	.checkentry	= masquerade_tg6_checkentry,
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
	if (err == 0) {
		register_netdevice_notifier(&masq_dev_notifier);
		register_inet6addr_notifier(&masq_inet_notifier);
	}

	return err;
}
static void __exit masquerade_tg6_exit(void)
{
	unregister_inet6addr_notifier(&masq_inet_notifier);
	unregister_netdevice_notifier(&masq_dev_notifier);
	xt_unregister_target(&masquerade_tg6_reg);
}

module_init(masquerade_tg6_init);
module_exit(masquerade_tg6_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Xtables: automatic address SNAT");
