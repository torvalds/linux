/*
 *      NetBIOS name service broadcast connection tracking helper
 *
 *      (c) 2005 Patrick McHardy <kaber@trash.net>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
/*
 *      This helper tracks locally originating NetBIOS name service
 *      requests by issuing permanent expectations (valid until
 *      timing out) matching all reply connections from the
 *      destination network. The only NetBIOS specific thing is
 *      actually the port number.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/if_addr.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <net/route.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>

#define NMBD_PORT	137

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("NetBIOS name service broadcast connection tracking helper");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ip_conntrack_netbios_ns");
MODULE_ALIAS_NFCT_HELPER("netbios_ns");

static unsigned int timeout __read_mostly = 3;
module_param(timeout, uint, 0400);
MODULE_PARM_DESC(timeout, "timeout for master connection/replies in seconds");

static int help(struct sk_buff *skb, unsigned int protoff,
		struct nf_conn *ct, enum ip_conntrack_info ctinfo)
{
	struct nf_conntrack_expect *exp;
	struct iphdr *iph = ip_hdr(skb);
	struct rtable *rt = skb->rtable;
	struct in_device *in_dev;
	__be32 mask = 0;

	/* we're only interested in locally generated packets */
	if (skb->sk == NULL)
		goto out;
	if (rt == NULL || !(rt->rt_flags & RTCF_BROADCAST))
		goto out;
	if (CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL)
		goto out;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(rt->u.dst.dev);
	if (in_dev != NULL) {
		for_primary_ifa(in_dev) {
			if (ifa->ifa_broadcast == iph->daddr) {
				mask = ifa->ifa_mask;
				break;
			}
		} endfor_ifa(in_dev);
	}
	rcu_read_unlock();

	if (mask == 0)
		goto out;

	exp = nf_ct_expect_alloc(ct);
	if (exp == NULL)
		goto out;

	exp->tuple                = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
	exp->tuple.src.u.udp.port = htons(NMBD_PORT);

	exp->mask.src.u3.ip       = mask;
	exp->mask.src.u.udp.port  = htons(0xFFFF);

	exp->expectfn             = NULL;
	exp->flags                = NF_CT_EXPECT_PERMANENT;
	exp->class		  = NF_CT_EXPECT_CLASS_DEFAULT;
	exp->helper               = NULL;

	nf_ct_expect_related(exp);
	nf_ct_expect_put(exp);

	nf_ct_refresh(ct, skb, timeout * HZ);
out:
	return NF_ACCEPT;
}

static struct nf_conntrack_expect_policy exp_policy = {
	.max_expected	= 1,
};

static struct nf_conntrack_helper helper __read_mostly = {
	.name			= "netbios-ns",
	.tuple.src.l3num	= AF_INET,
	.tuple.src.u.udp.port	= cpu_to_be16(NMBD_PORT),
	.tuple.dst.protonum	= IPPROTO_UDP,
	.me			= THIS_MODULE,
	.help			= help,
	.expect_policy		= &exp_policy,
};

static int __init nf_conntrack_netbios_ns_init(void)
{
	exp_policy.timeout = timeout;
	return nf_conntrack_helper_register(&helper);
}

static void __exit nf_conntrack_netbios_ns_fini(void)
{
	nf_conntrack_helper_unregister(&helper);
}

module_init(nf_conntrack_netbios_ns_init);
module_exit(nf_conntrack_netbios_ns_fini);
