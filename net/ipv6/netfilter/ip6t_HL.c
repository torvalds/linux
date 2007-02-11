/*
 * Hop Limit modification target for ip6tables
 * Maciej Soltysiak <solt@dns.toxicfilms.tv>
 * Based on HW's TTL module
 *
 * This software is distributed under the terms of GNU GPL
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6/ip6t_HL.h>

MODULE_AUTHOR("Maciej Soltysiak <solt@dns.toxicfilms.tv>");
MODULE_DESCRIPTION("IP6 tables Hop Limit modification module");
MODULE_LICENSE("GPL");

static unsigned int ip6t_hl_target(struct sk_buff **pskb,
				   const struct net_device *in,
				   const struct net_device *out,
				   unsigned int hooknum,
				   const struct xt_target *target,
				   const void *targinfo)
{
	struct ipv6hdr *ip6h;
	const struct ip6t_HL_info *info = targinfo;
	int new_hl;

	if (!skb_make_writable(pskb, (*pskb)->len))
		return NF_DROP;

	ip6h = (*pskb)->nh.ipv6h;

	switch (info->mode) {
		case IP6T_HL_SET:
			new_hl = info->hop_limit;
			break;
		case IP6T_HL_INC:
			new_hl = ip6h->hop_limit + info->hop_limit;
			if (new_hl > 255)
				new_hl = 255;
			break;
		case IP6T_HL_DEC:
			new_hl = ip6h->hop_limit - info->hop_limit;
			if (new_hl < 0)
				new_hl = 0;
			break;
		default:
			new_hl = ip6h->hop_limit;
			break;
	}

	ip6h->hop_limit = new_hl;

	return XT_CONTINUE;
}

static int ip6t_hl_checkentry(const char *tablename,
		const void *entry,
		const struct xt_target *target,
		void *targinfo,
		unsigned int hook_mask)
{
	struct ip6t_HL_info *info = targinfo;

	if (info->mode > IP6T_HL_MAXMODE) {
		printk(KERN_WARNING "ip6t_HL: invalid or unknown Mode %u\n",
			info->mode);
		return 0;
	}
	if ((info->mode != IP6T_HL_SET) && (info->hop_limit == 0)) {
		printk(KERN_WARNING "ip6t_HL: increment/decrement doesn't "
			"make sense with value 0\n");
		return 0;
	}
	return 1;
}

static struct xt_target ip6t_HL = {
	.name 		= "HL",
	.family		= AF_INET6,
	.target		= ip6t_hl_target,
	.targetsize	= sizeof(struct ip6t_HL_info),
	.table		= "mangle",
	.checkentry	= ip6t_hl_checkentry,
	.me		= THIS_MODULE
};

static int __init ip6t_hl_init(void)
{
	return xt_register_target(&ip6t_HL);
}

static void __exit ip6t_hl_fini(void)
{
	xt_unregister_target(&ip6t_HL);
}

module_init(ip6t_hl_init);
module_exit(ip6t_hl_fini);
