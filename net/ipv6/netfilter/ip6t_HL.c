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
MODULE_DESCRIPTION("Xtables: IPv6 Hop Limit field modification target");
MODULE_LICENSE("GPL");

static unsigned int
hl_tg6(struct sk_buff *skb, const struct xt_target_param *par)
{
	struct ipv6hdr *ip6h;
	const struct ip6t_HL_info *info = par->targinfo;
	int new_hl;

	if (!skb_make_writable(skb, skb->len))
		return NF_DROP;

	ip6h = ipv6_hdr(skb);

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

static bool
hl_tg6_check(const char *tablename, const void *entry,
             const struct xt_target *target, void *targinfo,
             unsigned int hook_mask)
{
	const struct ip6t_HL_info *info = targinfo;

	if (info->mode > IP6T_HL_MAXMODE) {
		printk(KERN_WARNING "ip6t_HL: invalid or unknown Mode %u\n",
			info->mode);
		return false;
	}
	if (info->mode != IP6T_HL_SET && info->hop_limit == 0) {
		printk(KERN_WARNING "ip6t_HL: increment/decrement doesn't "
			"make sense with value 0\n");
		return false;
	}
	return true;
}

static struct xt_target hl_tg6_reg __read_mostly = {
	.name 		= "HL",
	.family		= NFPROTO_IPV6,
	.target		= hl_tg6,
	.targetsize	= sizeof(struct ip6t_HL_info),
	.table		= "mangle",
	.checkentry	= hl_tg6_check,
	.me		= THIS_MODULE
};

static int __init hl_tg6_init(void)
{
	return xt_register_target(&hl_tg6_reg);
}

static void __exit hl_tg6_exit(void)
{
	xt_unregister_target(&hl_tg6_reg);
}

module_init(hl_tg6_init);
module_exit(hl_tg6_exit);
