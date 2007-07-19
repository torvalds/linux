/* iptables module for using new netfilter netlink queue
 *
 * (C) 2005 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter.h>
#include <linux/netfilter_arp.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_NFQUEUE.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("[ip,ip6,arp]_tables NFQUEUE target");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_NFQUEUE");
MODULE_ALIAS("ip6t_NFQUEUE");
MODULE_ALIAS("arpt_NFQUEUE");

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const struct xt_target *target,
       const void *targinfo)
{
	const struct xt_NFQ_info *tinfo = targinfo;

	return NF_QUEUE_NR(tinfo->queuenum);
}

static struct xt_target xt_nfqueue_target[] __read_mostly = {
	{
		.name		= "NFQUEUE",
		.family		= AF_INET,
		.target		= target,
		.targetsize	= sizeof(struct xt_NFQ_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "NFQUEUE",
		.family		= AF_INET6,
		.target		= target,
		.targetsize	= sizeof(struct xt_NFQ_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "NFQUEUE",
		.family		= NF_ARP,
		.target		= target,
		.targetsize	= sizeof(struct xt_NFQ_info),
		.me		= THIS_MODULE,
	},
};

static int __init xt_nfqueue_init(void)
{
	return xt_register_targets(xt_nfqueue_target,
				   ARRAY_SIZE(xt_nfqueue_target));
}

static void __exit xt_nfqueue_fini(void)
{
	xt_unregister_targets(xt_nfqueue_target, ARRAY_SIZE(xt_nfqueue_target));
}

module_init(xt_nfqueue_init);
module_exit(xt_nfqueue_fini);
