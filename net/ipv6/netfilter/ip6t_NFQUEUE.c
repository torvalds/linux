/* ip6tables module for using new netfilter netlink queue
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
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv4/ipt_NFQUEUE.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("ip6tables NFQUEUE target");
MODULE_LICENSE("GPL");

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const void *targinfo,
       void *userinfo)
{
	const struct ipt_NFQ_info *tinfo = targinfo;

	return NF_QUEUE_NR(tinfo->queuenum);
}

static int
checkentry(const char *tablename,
	   const struct ip6t_entry *e,
           void *targinfo,
           unsigned int targinfosize,
           unsigned int hook_mask)
{
	if (targinfosize != IP6T_ALIGN(sizeof(struct ipt_NFQ_info))) {
		printk(KERN_WARNING "NFQUEUE: targinfosize %u != %Zu\n",
		       targinfosize,
		       IP6T_ALIGN(sizeof(struct ipt_NFQ_info)));
		return 0;
	}

	return 1;
}

static struct ip6t_target ipt_NFQ_reg = {
	.name		= "NFQUEUE",
	.target		= target,
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ip6t_register_target(&ipt_NFQ_reg);
}

static void __exit fini(void)
{
	ip6t_unregister_target(&ipt_NFQ_reg);
}

module_init(init);
module_exit(fini);
