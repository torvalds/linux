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
#include <linux/in.h>
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

static unsigned int
nf_nat_redirect(struct sk_buff *skb, const struct nf_nat_range2 *range,
		const union nf_inet_addr *newdst)
{
	struct nf_nat_range2 newrange;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);

	memset(&newrange, 0, sizeof(newrange));

	newrange.flags		= range->flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr	= *newdst;
	newrange.max_addr	= *newdst;
	newrange.min_proto	= range->min_proto;
	newrange.max_proto	= range->max_proto;

	return nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_DST);
}

unsigned int
nf_nat_redirect_ipv4(struct sk_buff *skb, const struct nf_nat_range2 *range,
		     unsigned int hooknum)
{
	union nf_inet_addr newdst = {};

	WARN_ON(hooknum != NF_INET_PRE_ROUTING &&
		hooknum != NF_INET_LOCAL_OUT);

	/* Local packets: make them go to loopback */
	if (hooknum == NF_INET_LOCAL_OUT) {
		newdst.ip = htonl(INADDR_LOOPBACK);
	} else {
		const struct in_device *indev;

		indev = __in_dev_get_rcu(skb->dev);
		if (indev) {
			const struct in_ifaddr *ifa;

			ifa = rcu_dereference(indev->ifa_list);
			if (ifa)
				newdst.ip = ifa->ifa_local;
		}

		if (!newdst.ip)
			return NF_DROP;
	}

	return nf_nat_redirect(skb, range, &newdst);
}
EXPORT_SYMBOL_GPL(nf_nat_redirect_ipv4);

static const struct in6_addr loopback_addr = IN6ADDR_LOOPBACK_INIT;

unsigned int
nf_nat_redirect_ipv6(struct sk_buff *skb, const struct nf_nat_range2 *range,
		     unsigned int hooknum)
{
	union nf_inet_addr newdst = {};

	if (hooknum == NF_INET_LOCAL_OUT) {
		newdst.in6 = loopback_addr;
	} else {
		struct inet6_dev *idev;
		struct inet6_ifaddr *ifa;
		bool addr = false;

		idev = __in6_dev_get(skb->dev);
		if (idev != NULL) {
			read_lock_bh(&idev->lock);
			list_for_each_entry(ifa, &idev->addr_list, if_list) {
				newdst.in6 = ifa->addr;
				addr = true;
				break;
			}
			read_unlock_bh(&idev->lock);
		}

		if (!addr)
			return NF_DROP;
	}

	return nf_nat_redirect(skb, range, &newdst);
}
EXPORT_SYMBOL_GPL(nf_nat_redirect_ipv6);
