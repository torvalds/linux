/* Redirect.  Simple mapping which alters dst to a local IP address. */
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netdevice.h>
#include <linux/if.h>
#include <linux/inetdevice.h>
#include <net/protocol.h>
#include <net/checksum.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_nat_rule.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("Xtables: Connection redirection to localhost");

/* FIXME: Take multiple ranges --RR */
static bool
redirect_tg_check(const char *tablename, const void *e,
                  const struct xt_target *target, void *targinfo,
                  unsigned int hook_mask)
{
	const struct nf_nat_multi_range_compat *mr = targinfo;

	if (mr->range[0].flags & IP_NAT_RANGE_MAP_IPS) {
		pr_debug("redirect_check: bad MAP_IPS.\n");
		return false;
	}
	if (mr->rangesize != 1) {
		pr_debug("redirect_check: bad rangesize %u.\n", mr->rangesize);
		return false;
	}
	return true;
}

static unsigned int
redirect_tg(struct sk_buff *skb, const struct xt_target_param *par)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	__be32 newdst;
	const struct nf_nat_multi_range_compat *mr = par->targinfo;
	struct nf_nat_range newrange;

	NF_CT_ASSERT(par->hooknum == NF_INET_PRE_ROUTING ||
		     par->hooknum == NF_INET_LOCAL_OUT);

	ct = nf_ct_get(skb, &ctinfo);
	NF_CT_ASSERT(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED));

	/* Local packets: make them go to loopback */
	if (par->hooknum == NF_INET_LOCAL_OUT)
		newdst = htonl(0x7F000001);
	else {
		struct in_device *indev;
		struct in_ifaddr *ifa;

		newdst = 0;

		rcu_read_lock();
		indev = __in_dev_get_rcu(skb->dev);
		if (indev && (ifa = indev->ifa_list))
			newdst = ifa->ifa_local;
		rcu_read_unlock();

		if (!newdst)
			return NF_DROP;
	}

	/* Transfer from original range. */
	newrange = ((struct nf_nat_range)
		{ mr->range[0].flags | IP_NAT_RANGE_MAP_IPS,
		  newdst, newdst,
		  mr->range[0].min, mr->range[0].max });

	/* Hand modified range to generic setup. */
	return nf_nat_setup_info(ct, &newrange, IP_NAT_MANIP_DST);
}

static struct xt_target redirect_tg_reg __read_mostly = {
	.name		= "REDIRECT",
	.family		= NFPROTO_IPV4,
	.target		= redirect_tg,
	.targetsize	= sizeof(struct nf_nat_multi_range_compat),
	.table		= "nat",
	.hooks		= (1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_LOCAL_OUT),
	.checkentry	= redirect_tg_check,
	.me		= THIS_MODULE,
};

static int __init redirect_tg_init(void)
{
	return xt_register_target(&redirect_tg_reg);
}

static void __exit redirect_tg_exit(void)
{
	xt_unregister_target(&redirect_tg_reg);
}

module_init(redirect_tg_init);
module_exit(redirect_tg_exit);
