/* TTL modification target for IP tables
 * (C) 2000,2005 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/checksum.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ipt_TTL.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("IP tables TTL modification module");
MODULE_LICENSE("GPL");

static unsigned int
ipt_ttl_target(struct sk_buff **pskb,
	       const struct net_device *in, const struct net_device *out,
	       unsigned int hooknum, const struct xt_target *target,
	       const void *targinfo)
{
	struct iphdr *iph;
	const struct ipt_TTL_info *info = targinfo;
	int new_ttl;

	if (!skb_make_writable(pskb, (*pskb)->len))
		return NF_DROP;

	iph = ip_hdr(*pskb);

	switch (info->mode) {
		case IPT_TTL_SET:
			new_ttl = info->ttl;
			break;
		case IPT_TTL_INC:
			new_ttl = iph->ttl + info->ttl;
			if (new_ttl > 255)
				new_ttl = 255;
			break;
		case IPT_TTL_DEC:
			new_ttl = iph->ttl - info->ttl;
			if (new_ttl < 0)
				new_ttl = 0;
			break;
		default:
			new_ttl = iph->ttl;
			break;
	}

	if (new_ttl != iph->ttl) {
		nf_csum_replace2(&iph->check, htons(iph->ttl << 8),
					      htons(new_ttl << 8));
		iph->ttl = new_ttl;
	}

	return XT_CONTINUE;
}

static int ipt_ttl_checkentry(const char *tablename,
		const void *e,
		const struct xt_target *target,
		void *targinfo,
		unsigned int hook_mask)
{
	struct ipt_TTL_info *info = targinfo;

	if (info->mode > IPT_TTL_MAXMODE) {
		printk(KERN_WARNING "ipt_TTL: invalid or unknown Mode %u\n",
			info->mode);
		return 0;
	}
	if ((info->mode != IPT_TTL_SET) && (info->ttl == 0))
		return 0;
	return 1;
}

static struct xt_target ipt_TTL = {
	.name 		= "TTL",
	.family		= AF_INET,
	.target 	= ipt_ttl_target,
	.targetsize	= sizeof(struct ipt_TTL_info),
	.table		= "mangle",
	.checkentry 	= ipt_ttl_checkentry,
	.me 		= THIS_MODULE,
};

static int __init ipt_ttl_init(void)
{
	return xt_register_target(&ipt_TTL);
}

static void __exit ipt_ttl_fini(void)
{
	xt_unregister_target(&ipt_TTL);
}

module_init(ipt_ttl_init);
module_exit(ipt_ttl_fini);
