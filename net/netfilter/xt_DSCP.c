/* x_tables module for setting the IPv4/IPv6 DSCP field, Version 1.8
 *
 * (C) 2002 by Harald Welte <laforge@netfilter.org>
 * based on ipt_FTOS.c (C) 2000 by Matthew G. Marsh <mgm@paktronix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * See RFC2474 for a description of the DSCP field within the IP Header.
*/

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/dsfield.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_DSCP.h>
#include <linux/netfilter_ipv4/ipt_TOS.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("Xtables: DSCP/TOS field modification");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_DSCP");
MODULE_ALIAS("ip6t_DSCP");
MODULE_ALIAS("ipt_TOS");
MODULE_ALIAS("ip6t_TOS");

static unsigned int
dscp_tg(struct sk_buff *skb, const struct net_device *in,
        const struct net_device *out, unsigned int hooknum,
        const struct xt_target *target, const void *targinfo)
{
	const struct xt_DSCP_info *dinfo = targinfo;
	u_int8_t dscp = ipv4_get_dsfield(ip_hdr(skb)) >> XT_DSCP_SHIFT;

	if (dscp != dinfo->dscp) {
		if (!skb_make_writable(skb, sizeof(struct iphdr)))
			return NF_DROP;

		ipv4_change_dsfield(ip_hdr(skb), (__u8)(~XT_DSCP_MASK),
				    dinfo->dscp << XT_DSCP_SHIFT);

	}
	return XT_CONTINUE;
}

static unsigned int
dscp_tg6(struct sk_buff *skb, const struct net_device *in,
         const struct net_device *out, unsigned int hooknum,
         const struct xt_target *target, const void *targinfo)
{
	const struct xt_DSCP_info *dinfo = targinfo;
	u_int8_t dscp = ipv6_get_dsfield(ipv6_hdr(skb)) >> XT_DSCP_SHIFT;

	if (dscp != dinfo->dscp) {
		if (!skb_make_writable(skb, sizeof(struct ipv6hdr)))
			return NF_DROP;

		ipv6_change_dsfield(ipv6_hdr(skb), (__u8)(~XT_DSCP_MASK),
				    dinfo->dscp << XT_DSCP_SHIFT);
	}
	return XT_CONTINUE;
}

static bool
dscp_tg_check(const char *tablename, const void *e_void,
              const struct xt_target *target, void *targinfo,
              unsigned int hook_mask)
{
	const u_int8_t dscp = ((struct xt_DSCP_info *)targinfo)->dscp;

	if (dscp > XT_DSCP_MAX) {
		printk(KERN_WARNING "DSCP: dscp %x out of range\n", dscp);
		return false;
	}
	return true;
}

static unsigned int
tos_tg_v0(struct sk_buff *skb, const struct net_device *in,
          const struct net_device *out, unsigned int hooknum,
          const struct xt_target *target, const void *targinfo)
{
	const struct ipt_tos_target_info *info = targinfo;
	struct iphdr *iph = ip_hdr(skb);
	u_int8_t oldtos;

	if ((iph->tos & IPTOS_TOS_MASK) != info->tos) {
		if (!skb_make_writable(skb, sizeof(struct iphdr)))
			return NF_DROP;

		iph      = ip_hdr(skb);
		oldtos   = iph->tos;
		iph->tos = (iph->tos & IPTOS_PREC_MASK) | info->tos;
		csum_replace2(&iph->check, htons(oldtos), htons(iph->tos));
	}

	return XT_CONTINUE;
}

static bool
tos_tg_check_v0(const char *tablename, const void *e_void,
                const struct xt_target *target, void *targinfo,
                unsigned int hook_mask)
{
	const u_int8_t tos = ((struct ipt_tos_target_info *)targinfo)->tos;

	if (tos != IPTOS_LOWDELAY && tos != IPTOS_THROUGHPUT &&
	    tos != IPTOS_RELIABILITY && tos != IPTOS_MINCOST &&
	    tos != IPTOS_NORMALSVC) {
		printk(KERN_WARNING "TOS: bad tos value %#x\n", tos);
		return false;
	}

	return true;
}

static unsigned int
tos_tg(struct sk_buff *skb, const struct net_device *in,
       const struct net_device *out, unsigned int hooknum,
       const struct xt_target *target, const void *targinfo)
{
	const struct xt_tos_target_info *info = targinfo;
	struct iphdr *iph = ip_hdr(skb);
	u_int8_t orig, nv;

	orig = ipv4_get_dsfield(iph);
	nv   = (orig & ~info->tos_mask) ^ info->tos_value;

	if (orig != nv) {
		if (!skb_make_writable(skb, sizeof(struct iphdr)))
			return NF_DROP;
		iph = ip_hdr(skb);
		ipv4_change_dsfield(iph, 0, nv);
	}

	return XT_CONTINUE;
}

static unsigned int
tos_tg6(struct sk_buff *skb, const struct net_device *in,
        const struct net_device *out, unsigned int hooknum,
        const struct xt_target *target, const void *targinfo)
{
	const struct xt_tos_target_info *info = targinfo;
	struct ipv6hdr *iph = ipv6_hdr(skb);
	u_int8_t orig, nv;

	orig = ipv6_get_dsfield(iph);
	nv   = (orig & info->tos_mask) ^ info->tos_value;

	if (orig != nv) {
		if (!skb_make_writable(skb, sizeof(struct iphdr)))
			return NF_DROP;
		iph = ipv6_hdr(skb);
		ipv6_change_dsfield(iph, 0, nv);
	}

	return XT_CONTINUE;
}

static struct xt_target dscp_tg_reg[] __read_mostly = {
	{
		.name		= "DSCP",
		.family		= AF_INET,
		.checkentry	= dscp_tg_check,
		.target		= dscp_tg,
		.targetsize	= sizeof(struct xt_DSCP_info),
		.table		= "mangle",
		.me		= THIS_MODULE,
	},
	{
		.name		= "DSCP",
		.family		= AF_INET6,
		.checkentry	= dscp_tg_check,
		.target		= dscp_tg6,
		.targetsize	= sizeof(struct xt_DSCP_info),
		.table		= "mangle",
		.me		= THIS_MODULE,
	},
	{
		.name		= "TOS",
		.revision	= 0,
		.family		= AF_INET,
		.table		= "mangle",
		.target		= tos_tg_v0,
		.targetsize	= sizeof(struct ipt_tos_target_info),
		.checkentry	= tos_tg_check_v0,
		.me		= THIS_MODULE,
	},
	{
		.name		= "TOS",
		.revision	= 1,
		.family		= AF_INET,
		.table		= "mangle",
		.target		= tos_tg,
		.targetsize	= sizeof(struct xt_tos_target_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "TOS",
		.revision	= 1,
		.family		= AF_INET6,
		.table		= "mangle",
		.target		= tos_tg6,
		.targetsize	= sizeof(struct xt_tos_target_info),
		.me		= THIS_MODULE,
	},
};

static int __init dscp_tg_init(void)
{
	return xt_register_targets(dscp_tg_reg, ARRAY_SIZE(dscp_tg_reg));
}

static void __exit dscp_tg_exit(void)
{
	xt_unregister_targets(dscp_tg_reg, ARRAY_SIZE(dscp_tg_reg));
}

module_init(dscp_tg_init);
module_exit(dscp_tg_exit);
