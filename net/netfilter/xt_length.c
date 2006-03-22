/* Kernel module to match packet length. */
/* (C) 1999-2001 James Morris <jmorros@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <net/ip.h>

#include <linux/netfilter/xt_length.h>
#include <linux/netfilter/x_tables.h>

MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");
MODULE_DESCRIPTION("IP tables packet length matching module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_length");
MODULE_ALIAS("ip6t_length");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_length_info *info = matchinfo;
	u_int16_t pktlen = ntohs(skb->nh.iph->tot_len);
	
	return (pktlen >= info->min && pktlen <= info->max) ^ info->invert;
}

static int
match6(const struct sk_buff *skb,
       const struct net_device *in,
       const struct net_device *out,
       const struct xt_match *match,
       const void *matchinfo,
       int offset,
       unsigned int protoff,
       int *hotdrop)
{
	const struct xt_length_info *info = matchinfo;
	u_int16_t pktlen = ntohs(skb->nh.ipv6h->payload_len) + sizeof(struct ipv6hdr);
	
	return (pktlen >= info->min && pktlen <= info->max) ^ info->invert;
}

static struct xt_match length_match = {
	.name		= "length",
	.match		= match,
	.matchsize	= sizeof(struct xt_length_info),
	.family		= AF_INET,
	.me		= THIS_MODULE,
};

static struct xt_match length6_match = {
	.name		= "length",
	.match		= match6,
	.matchsize	= sizeof(struct xt_length_info),
	.family		= AF_INET6,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	int ret;
	ret = xt_register_match(&length_match);
	if (ret)
		return ret;
	ret = xt_register_match(&length6_match);
	if (ret)
		xt_unregister_match(&length_match);

	return ret;
}

static void __exit fini(void)
{
	xt_unregister_match(&length_match);
	xt_unregister_match(&length6_match);
}

module_init(init);
module_exit(fini);
