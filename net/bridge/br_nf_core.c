// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Handle firewalling core
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *	Bart De Schuymer		<bdschuym@pandora.be>
 *
 *	Lennert dedicates this file to Kerstin Wurdinger.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/in_route.h>
#include <linux/inetdevice.h>
#include <net/route.h>

#include "br_private.h"
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

static void fake_update_pmtu(struct dst_entry *dst, struct sock *sk,
			     struct sk_buff *skb, u32 mtu,
			     bool confirm_neigh)
{
}

static void fake_redirect(struct dst_entry *dst, struct sock *sk,
			  struct sk_buff *skb)
{
}

static u32 *fake_cow_metrics(struct dst_entry *dst, unsigned long old)
{
	return NULL;
}

static struct neighbour *fake_neigh_lookup(const struct dst_entry *dst,
					   struct sk_buff *skb,
					   const void *daddr)
{
	return NULL;
}

static unsigned int fake_mtu(const struct dst_entry *dst)
{
	return dst->dev->mtu;
}

static struct dst_ops fake_dst_ops = {
	.family		= AF_INET,
	.update_pmtu	= fake_update_pmtu,
	.redirect	= fake_redirect,
	.cow_metrics	= fake_cow_metrics,
	.neigh_lookup	= fake_neigh_lookup,
	.mtu		= fake_mtu,
};

/*
 * Initialize bogus route table used to keep netfilter happy.
 * Currently, we fill in the PMTU entry because netfilter
 * refragmentation needs it, and the rt_flags entry because
 * ipt_REJECT needs it.  Future netfilter modules might
 * require us to fill additional fields.
 */
void br_netfilter_rtable_init(struct net_bridge *br)
{
	struct rtable *rt = &br->fake_rtable;

	rcuref_init(&rt->dst.__rcuref, 1);
	rt->dst.dev = br->dev;
	dst_init_metrics(&rt->dst, br->metrics, false);
	dst_metric_set(&rt->dst, RTAX_MTU, br->dev->mtu);
	rt->dst.flags	= DST_NOXFRM | DST_FAKE_RTABLE;
	rt->dst.ops = &fake_dst_ops;
}

int __init br_nf_core_init(void)
{
	return dst_entries_init(&fake_dst_ops);
}

void br_nf_core_fini(void)
{
	dst_entries_destroy(&fake_dst_ops);
}
