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
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_NFQUEUE.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("iptables NFQUEUE target");
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
	   const struct ipt_entry *e,
           void *targinfo,
           unsigned int targinfosize,
           unsigned int hook_mask)
{
	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_NFQ_info))) {
		printk(KERN_WARNING "NFQUEUE: targinfosize %u != %Zu\n",
		       targinfosize,
		       IPT_ALIGN(sizeof(struct ipt_NFQ_info)));
		return 0;
	}

	return 1;
}

static struct ipt_target ipt_NFQ_reg = {
	.name		= "NFQUEUE",
	.target		= target,
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_target(&ipt_NFQ_reg);
}

static void __exit fini(void)
{
	ipt_unregister_target(&ipt_NFQ_reg);
}

module_init(init);
module_exit(fini);
