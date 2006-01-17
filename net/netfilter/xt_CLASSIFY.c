/*
 * This is a module which is used for setting the skb->priority field
 * of an skb for qdisc classification.
 */

/* (C) 2001-2002 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/checksum.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_CLASSIFY.h>

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("iptables qdisc classification target module");
MODULE_ALIAS("ipt_CLASSIFY");

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const void *targinfo,
       void *userinfo)
{
	const struct xt_classify_target_info *clinfo = targinfo;

	if ((*pskb)->priority != clinfo->priority)
		(*pskb)->priority = clinfo->priority;

	return XT_CONTINUE;
}

static int
checkentry(const char *tablename,
           const void *e,
           void *targinfo,
           unsigned int targinfosize,
           unsigned int hook_mask)
{
	if (targinfosize != XT_ALIGN(sizeof(struct xt_classify_target_info))){
		printk(KERN_ERR "CLASSIFY: invalid size (%u != %Zu).\n",
		       targinfosize,
		       XT_ALIGN(sizeof(struct xt_classify_target_info)));
		return 0;
	}
	
	if (hook_mask & ~((1 << NF_IP_LOCAL_OUT) | (1 << NF_IP_FORWARD) |
	                  (1 << NF_IP_POST_ROUTING))) {
		printk(KERN_ERR "CLASSIFY: only valid in LOCAL_OUT, FORWARD "
		                "and POST_ROUTING.\n");
		return 0;
	}

	if (strcmp(tablename, "mangle") != 0) {
		printk(KERN_ERR "CLASSIFY: can only be called from "
		                "\"mangle\" table, not \"%s\".\n",
		                tablename);
		return 0;
	}

	return 1;
}

static struct xt_target classify_reg = { 
	.name 		= "CLASSIFY", 
	.target 	= target,
	.checkentry	= checkentry,
	.me 		= THIS_MODULE,
};
static struct xt_target classify6_reg = { 
	.name 		= "CLASSIFY", 
	.target 	= target,
	.checkentry	= checkentry,
	.me 		= THIS_MODULE,
};


static int __init init(void)
{
	int ret;

	ret = xt_register_target(AF_INET, &classify_reg);
	if (ret)
		return ret;

	ret = xt_register_target(AF_INET6, &classify6_reg);
	if (ret)
		xt_unregister_target(AF_INET, &classify_reg);

	return ret;
}

static void __exit fini(void)
{
	xt_unregister_target(AF_INET, &classify_reg);
	xt_unregister_target(AF_INET6, &classify6_reg);
}

module_init(init);
module_exit(fini);
