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
#include <linux/atomic.h>
#include <linux/netdevice.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_nat.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <net/netfilter/ipv6/nf_nat_masquerade.h>

unsigned int
nf_nat_masquerade_ipv6(struct sk_buff *skb, const struct nf_nat_range *range,
		       const struct net_device *out)
{
	enum ip_conntrack_info ctinfo;
	struct in6_addr src;
	struct nf_conn *ct;
	struct nf_nat_range newrange;

	ct = nf_ct_get(skb, &ctinfo);
	NF_CT_ASSERT(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED ||
			    ctinfo == IP_CT_RELATED_REPLY));

	if (ipv6_dev_get_saddr(nf_ct_net(ct), out,
			       &ipv6_hdr(skb)->daddr, 0, &src) < 0)
		return NF_DROP;

	nfct_nat(ct)->masq_index = out->ifindex;

	newrange.flags		= range->flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr.in6	= src;
	newrange.max_addr.in6	= src;
	newrange.min_proto	= range->min_proto;
	newrange.max_proto	= range->max_proto;

	return nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_SRC);
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv6);

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
				      (void *)(long)dev->ifindex, 0, 0);

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

static atomic_t masquerade_notifier_refcount = ATOMIC_INIT(0);

void nf_nat_masquerade_ipv6_register_notifier(void)
{
	/* check if the notifier is already set */
	if (atomic_inc_return(&masquerade_notifier_refcount) > 1)
		return;

	register_netdevice_notifier(&masq_dev_notifier);
	register_inet6addr_notifier(&masq_inet_notifier);
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv6_register_notifier);

void nf_nat_masquerade_ipv6_unregister_notifier(void)
{
	/* check if the notifier still has clients */
	if (atomic_dec_return(&masquerade_notifier_refcount) > 0)
		return;

	unregister_inet6addr_notifier(&masq_inet_notifier);
	unregister_netdevice_notifier(&masq_dev_notifier);
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv6_unregister_notifier);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
