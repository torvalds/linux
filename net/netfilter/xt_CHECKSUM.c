/* iptables module for the packet checksum mangling
 *
 * (C) 2002 by Harald Welte <laforge@netfilter.org>
 * (C) 2010 Red Hat, Inc.
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_CHECKSUM.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael S. Tsirkin <mst@redhat.com>");
MODULE_DESCRIPTION("Xtables: checksum modification");
MODULE_ALIAS("ipt_CHECKSUM");
MODULE_ALIAS("ip6t_CHECKSUM");

static unsigned int
checksum_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	if (skb->ip_summed == CHECKSUM_PARTIAL && !skb_is_gso(skb))
		skb_checksum_help(skb);

	return XT_CONTINUE;
}

static int checksum_tg_check(const struct xt_tgchk_param *par)
{
	const struct xt_CHECKSUM_info *einfo = par->targinfo;
	const struct ip6t_ip6 *i6 = par->entryinfo;
	const struct ipt_ip *i4 = par->entryinfo;

	if (einfo->operation & ~XT_CHECKSUM_OP_FILL) {
		pr_info_ratelimited("unsupported CHECKSUM operation %x\n",
				    einfo->operation);
		return -EINVAL;
	}
	if (!einfo->operation)
		return -EINVAL;

	switch (par->family) {
	case NFPROTO_IPV4:
		if (i4->proto == IPPROTO_UDP &&
		    (i4->invflags & XT_INV_PROTO) == 0)
			return 0;
		break;
	case NFPROTO_IPV6:
		if ((i6->flags & IP6T_F_PROTO) &&
		    i6->proto == IPPROTO_UDP &&
		    (i6->invflags & XT_INV_PROTO) == 0)
			return 0;
		break;
	}

	pr_warn_once("CHECKSUM should be avoided.  If really needed, restrict with \"-p udp\" and only use in OUTPUT\n");
	return 0;
}

static struct xt_target checksum_tg_reg __read_mostly = {
	.name		= "CHECKSUM",
	.family		= NFPROTO_UNSPEC,
	.target		= checksum_tg,
	.targetsize	= sizeof(struct xt_CHECKSUM_info),
	.table		= "mangle",
	.checkentry	= checksum_tg_check,
	.me		= THIS_MODULE,
};

static int __init checksum_tg_init(void)
{
	return xt_register_target(&checksum_tg_reg);
}

static void __exit checksum_tg_exit(void)
{
	xt_unregister_target(&checksum_tg_reg);
}

module_init(checksum_tg_init);
module_exit(checksum_tg_exit);
