// SPDX-License-Identifier: GPL-2.0-only
/*
 * This is a module which is used for setting the skb->priority field
 * of an skb for qdisc classification.
 */

/* (C) 2001-2002 Patrick McHardy <kaber@trash.net>
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/checksum.h>

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_CLASSIFY.h>
#include <linux/netfilter_arp.h>

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: Qdisc classification");
MODULE_ALIAS("ipt_CLASSIFY");
MODULE_ALIAS("ip6t_CLASSIFY");
MODULE_ALIAS("arpt_CLASSIFY");

static unsigned int
classify_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_classify_target_info *clinfo = par->targinfo;

	skb->priority = clinfo->priority;
	return XT_CONTINUE;
}

static struct xt_target classify_tg_reg[] __read_mostly = {
	{
		.name       = "CLASSIFY",
		.revision   = 0,
		.family     = NFPROTO_UNSPEC,
		.hooks      = (1 << NF_INET_LOCAL_OUT) | (1 << NF_INET_FORWARD) |
		              (1 << NF_INET_POST_ROUTING),
		.target     = classify_tg,
		.targetsize = sizeof(struct xt_classify_target_info),
		.me         = THIS_MODULE,
	},
	{
		.name       = "CLASSIFY",
		.revision   = 0,
		.family     = NFPROTO_ARP,
		.hooks      = (1 << NF_ARP_OUT) | (1 << NF_ARP_FORWARD),
		.target     = classify_tg,
		.targetsize = sizeof(struct xt_classify_target_info),
		.me         = THIS_MODULE,
	},
};

static int __init classify_tg_init(void)
{
	return xt_register_targets(classify_tg_reg, ARRAY_SIZE(classify_tg_reg));
}

static void __exit classify_tg_exit(void)
{
	xt_unregister_targets(classify_tg_reg, ARRAY_SIZE(classify_tg_reg));
}

module_init(classify_tg_init);
module_exit(classify_tg_exit);
