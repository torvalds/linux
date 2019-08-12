// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      broadcast connection tracking helper
 *
 *      (c) 2005 Patrick McHardy <kaber@trash.net>
 */

#include <linux/module.h>
#include <linux/ip.h>
#include <net/route.h>
#include <linux/inetdevice.h>
#include <linux/skbuff.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>

int nf_conntrack_broadcast_help(struct sk_buff *skb,
				struct nf_conn *ct,
				enum ip_conntrack_info ctinfo,
				unsigned int timeout)
{
	struct nf_conntrack_expect *exp;
	struct iphdr *iph = ip_hdr(skb);
	struct rtable *rt = skb_rtable(skb);
	struct in_device *in_dev;
	struct nf_conn_help *help = nfct_help(ct);
	__be32 mask = 0;

	/* we're only interested in locally generated packets */
	if (skb->sk == NULL || !net_eq(nf_ct_net(ct), sock_net(skb->sk)))
		goto out;
	if (rt == NULL || !(rt->rt_flags & RTCF_BROADCAST))
		goto out;
	if (CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL)
		goto out;

	in_dev = __in_dev_get_rcu(rt->dst.dev);
	if (in_dev != NULL) {
		const struct in_ifaddr *ifa;

		in_dev_for_each_ifa_rcu(ifa, in_dev) {
			if (ifa->ifa_flags & IFA_F_SECONDARY)
				continue;

			if (ifa->ifa_broadcast == iph->daddr) {
				mask = ifa->ifa_mask;
				break;
			}
		}
	}

	if (mask == 0)
		goto out;

	exp = nf_ct_expect_alloc(ct);
	if (exp == NULL)
		goto out;

	exp->tuple                = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
	exp->tuple.src.u.udp.port = help->helper->tuple.src.u.udp.port;

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
EXPORT_SYMBOL_GPL(nf_conntrack_broadcast_help);

MODULE_LICENSE("GPL");
