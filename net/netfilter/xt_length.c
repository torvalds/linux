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

static bool
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      bool *hotdrop)
{
	const struct xt_length_info *info = matchinfo;
	u_int16_t pktlen = ntohs(ip_hdr(skb)->tot_len);

	return (pktlen >= info->min && pktlen <= info->max) ^ info->invert;
}

static bool
match6(const struct sk_buff *skb,
       const struct net_device *in,
       const struct net_device *out,
       const struct xt_match *match,
       const void *matchinfo,
       int offset,
       unsigned int protoff,
       bool *hotdrop)
{
	const struct xt_length_info *info = matchinfo;
	const u_int16_t pktlen = ntohs(ipv6_hdr(skb)->payload_len) +
				 sizeof(struct ipv6hdr);

	return (pktlen >= info->min && pktlen <= info->max) ^ info->invert;
}

static struct xt_match xt_length_match[] __read_mostly = {
	{
		.name		= "length",
		.family		= AF_INET,
		.match		= match,
		.matchsize	= sizeof(struct xt_length_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "length",
		.family		= AF_INET6,
		.match		= match6,
		.matchsize	= sizeof(struct xt_length_info),
		.me		= THIS_MODULE,
	},
};

static int __init xt_length_init(void)
{
	return xt_register_matches(xt_length_match,
				   ARRAY_SIZE(xt_length_match));
}

static void __exit xt_length_fini(void)
{
	xt_unregister_matches(xt_length_match, ARRAY_SIZE(xt_length_match));
}

module_init(xt_length_init);
module_exit(xt_length_fini);
