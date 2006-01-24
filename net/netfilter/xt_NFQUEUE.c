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
       const void *targinfo,
       void *userinfo)
{
	const struct xt_NFQ_info *tinfo = targinfo;

	return NF_QUEUE_NR(tinfo->queuenum);
}

static int
checkentry(const char *tablename,
	   const void *entry,
           void *targinfo,
           unsigned int targinfosize,
           unsigned int hook_mask)
{
	if (targinfosize != XT_ALIGN(sizeof(struct xt_NFQ_info))) {
		printk(KERN_WARNING "NFQUEUE: targinfosize %u != %Zu\n",
		       targinfosize,
		       XT_ALIGN(sizeof(struct xt_NFQ_info)));
		return 0;
	}

	return 1;
}

static struct xt_target ipt_NFQ_reg = {
	.name		= "NFQUEUE",
	.target		= target,
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static struct xt_target ip6t_NFQ_reg = {
	.name		= "NFQUEUE",
	.target		= target,
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static struct xt_target arpt_NFQ_reg = {
	.name		= "NFQUEUE",
	.target		= target,
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	int ret;
	ret = xt_register_target(AF_INET, &ipt_NFQ_reg);
	if (ret)
		return ret;
	ret = xt_register_target(AF_INET6, &ip6t_NFQ_reg);
	if (ret)
		goto out_ip;
	ret = xt_register_target(NF_ARP, &arpt_NFQ_reg);
	if (ret)
		goto out_ip6;

	return ret;
out_ip6:
	xt_unregister_target(AF_INET6, &ip6t_NFQ_reg);
out_ip:
	xt_unregister_target(AF_INET, &ipt_NFQ_reg);

	return ret;
}

static void __exit fini(void)
{
	xt_unregister_target(NF_ARP, &arpt_NFQ_reg);
	xt_unregister_target(AF_INET6, &ip6t_NFQ_reg);
	xt_unregister_target(AF_INET, &ipt_NFQ_reg);
}

module_init(init);
module_exit(fini);
