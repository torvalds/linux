/*
 *	xt_mark - Netfilter module to match NFMARK value
 *
 *	(C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *	Copyright © CC Computer Consultants GmbH, 2007 - 2008
 *	Jan Engelhardt <jengelh@medozas.de>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter/xt_mark.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("Xtables: packet mark match");
MODULE_ALIAS("ipt_mark");
MODULE_ALIAS("ip6t_mark");

static bool
mark_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct xt_mark_mtinfo1 *info = par->matchinfo;

	return ((skb->mark & info->mask) == info->mark) ^ info->invert;
}

static struct xt_match mark_mt_reg __read_mostly = {
	.name           = "mark",
	.revision       = 1,
	.family         = NFPROTO_UNSPEC,
	.match          = mark_mt,
	.matchsize      = sizeof(struct xt_mark_mtinfo1),
	.me             = THIS_MODULE,
};

static int __init mark_mt_init(void)
{
	return xt_register_match(&mark_mt_reg);
}

static void __exit mark_mt_exit(void)
{
	xt_unregister_match(&mark_mt_reg);
}

module_init(mark_mt_init);
module_exit(mark_mt_exit);
