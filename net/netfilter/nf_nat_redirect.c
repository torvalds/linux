// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * Based on Rusty Russell's IPv4 REDIRECT target. Development of IPv6
 * NAT funded by Astaro.
 */

#include <linux/if.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/types.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/x_tables.h>
#include <net/addrconf.h>
#include <net/checksum.h>
#include <net/protocol.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_redirect.h>

unsigned int
nf_nat_redirect_ipv4(struct sk_buff *skb,
		     const struct nf_nat_ipv4_multi_range_compat *mr,
		     unsigned int hooknum)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	__be32 newdst;
	struct nf_nat_range2 newrange;

	WARN_ON(hooknum != NF_INET_PRE_ROUTING &&
		hooknum != NF_INET_LOCAL_OUT);

	ct = nf_ct_get(skb, &ctinfo);
	WARN_ON(!(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED)));

	/* Local packets: make them go to loopback */
	if (hooknum == NF_INET_LOCAL_OUT) {
		newdst = htonl(0x7F000001);
	} else {
		const struct in_device *indev;

		newdst = 0;

		indev = __in_dev_get_rcu(skb->dev);
		if (indev) {
			const struct in_ifaddr *ifa;

			ifa = rcu_dereference(indev->ifa_list);
			if (ifa)
				newdst = ifa->ifa_local;
		}

		if (!newdst)
			return NF_DROP;
	}

	/* Transfer from original range. */
	memset(&newrange.min_addr, 0, sizeof(newrange.min_addr));
	memset(&newrange.max_addr, 0, sizeof(newrange.max_addr));
	newrange.flags	     = mr->range[0].flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr.ip = newdst;
	newrange.max_addr.ip = newdst;
	newrange.min_proto   = mr->range[0].min;
	newrange.max_proto   = mr->range[0].max;

	/* Hand modified range to generic setup. */
	return nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_DST);
}
EXPORT_SYMBOL_GPL(nf_nat_redirect_ipv4);

static const struct in6_addr loopback_addr = IN6ADDR_LOOPBACK_INIT;

unsigned int
nf_nat_redirect_ipv6(struct sk_buff *skb, const struct nf_nat_range2 *range,
		     unsigned int hooknum)
{
	struct nf_nat_range2 newrange;
	struct in6_addr newdst;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);
	if (hooknum == NF_INET_LOCAL_OUT) {
		newdst = loopback_addr;
	} else {
		struct inet6_dev *idev;
		struct inet6_ifaddr *ifa;
		bool addr = false;

		idev = __in6_dev_get(skb->dev);
		if (idev != NULL) {
			read_lock_bh(&idev->lock);
			list_for_each_entry(ifa, &idev->addr_list, if_list) {
				newdst = ifa->addr;
				addr = true;
				break;
			}
			read_unlock_bh(&idev->lock);
		}

		if (!addr)
			return NF_DROP;
	}

	newrange.flags		= range->flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr.in6	= newdst;
	newrange.max_addr.in6	= newdst;
	newrange.min_proto	= range->min_proto;
	newrange.max_proto	= range->max_proto;

	return nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_DST);
}
EXPORT_SYMBOL_GPL(nf_nat_redirect_ipv6);
