/* Kernel module to match packet length. */
/* (C) 1999-2001 James Morris <jmorros@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter_ipv4/ipt_length.h>
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");
MODULE_DESCRIPTION("IP tables packet length matching module");
MODULE_LICENSE("GPL");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      int *hotdrop)
{
	const struct ipt_length_info *info = matchinfo;
	u_int16_t pktlen = ntohs(skb->nh.iph->tot_len);
	
	return (pktlen >= info->min && pktlen <= info->max) ^ info->invert;
}

static int
checkentry(const char *tablename,
           const struct ipt_ip *ip,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	if (matchsize != IPT_ALIGN(sizeof(struct ipt_length_info)))
		return 0;

	return 1;
}

static struct ipt_match length_match = {
	.name		= "length",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_match(&length_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&length_match);
}

module_init(init);
module_exit(fini);
