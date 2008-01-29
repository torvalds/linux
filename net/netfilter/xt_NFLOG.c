/*
 * Copyright (c) 2006 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/skbuff.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_NFLOG.h>
#include <net/netfilter/nf_log.h>

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Xtables: packet logging to netlink using NFLOG");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_NFLOG");
MODULE_ALIAS("ip6t_NFLOG");

static unsigned int
nflog_tg(struct sk_buff *skb, const struct net_device *in,
         const struct net_device *out, unsigned int hooknum,
         const struct xt_target *target, const void *targinfo)
{
	const struct xt_nflog_info *info = targinfo;
	struct nf_loginfo li;

	li.type		     = NF_LOG_TYPE_ULOG;
	li.u.ulog.copy_len   = info->len;
	li.u.ulog.group	     = info->group;
	li.u.ulog.qthreshold = info->threshold;

	nf_log_packet(target->family, hooknum, skb, in, out, &li,
		      "%s", info->prefix);
	return XT_CONTINUE;
}

static bool
nflog_tg_check(const char *tablename, const void *entry,
               const struct xt_target *target, void *targetinfo,
               unsigned int hookmask)
{
	const struct xt_nflog_info *info = targetinfo;

	if (info->flags & ~XT_NFLOG_MASK)
		return false;
	if (info->prefix[sizeof(info->prefix) - 1] != '\0')
		return false;
	return true;
}

static struct xt_target nflog_tg_reg[] __read_mostly = {
	{
		.name		= "NFLOG",
		.family		= AF_INET,
		.checkentry	= nflog_tg_check,
		.target		= nflog_tg,
		.targetsize	= sizeof(struct xt_nflog_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "NFLOG",
		.family		= AF_INET6,
		.checkentry	= nflog_tg_check,
		.target		= nflog_tg,
		.targetsize	= sizeof(struct xt_nflog_info),
		.me		= THIS_MODULE,
	},
};

static int __init nflog_tg_init(void)
{
	return xt_register_targets(nflog_tg_reg, ARRAY_SIZE(nflog_tg_reg));
}

static void __exit nflog_tg_exit(void)
{
	xt_unregister_targets(nflog_tg_reg, ARRAY_SIZE(nflog_tg_reg));
}

module_init(nflog_tg_init);
module_exit(nflog_tg_exit);
