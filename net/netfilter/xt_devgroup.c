/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <linux/netfilter/xt_devgroup.h>
#include <linux/netfilter/x_tables.h>

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: Device group match");
MODULE_ALIAS("ipt_devgroup");
MODULE_ALIAS("ip6t_devgroup");

static bool devgroup_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_devgroup_info *info = par->matchinfo;

	if (info->flags & XT_DEVGROUP_MATCH_SRC &&
	    (((info->src_group ^ par->in->group) & info->src_mask ? 1 : 0) ^
	     ((info->flags & XT_DEVGROUP_INVERT_SRC) ? 1 : 0)))
		return false;

	if (info->flags & XT_DEVGROUP_MATCH_DST &&
	    (((info->dst_group ^ par->out->group) & info->dst_mask ? 1 : 0) ^
	     ((info->flags & XT_DEVGROUP_INVERT_DST) ? 1 : 0)))
		return false;

	return true;
}

static int devgroup_mt_checkentry(const struct xt_mtchk_param *par)
{
	const struct xt_devgroup_info *info = par->matchinfo;

	if (info->flags & ~(XT_DEVGROUP_MATCH_SRC | XT_DEVGROUP_INVERT_SRC |
			    XT_DEVGROUP_MATCH_DST | XT_DEVGROUP_INVERT_DST))
		return -EINVAL;

	if (info->flags & XT_DEVGROUP_MATCH_SRC &&
	    par->hook_mask & ~((1 << NF_INET_PRE_ROUTING) |
			       (1 << NF_INET_LOCAL_IN) |
			       (1 << NF_INET_FORWARD)))
		return -EINVAL;

	if (info->flags & XT_DEVGROUP_MATCH_DST &&
	    par->hook_mask & ~((1 << NF_INET_FORWARD) |
			       (1 << NF_INET_LOCAL_OUT) |
			       (1 << NF_INET_POST_ROUTING)))
		return -EINVAL;

	return 0;
}

static struct xt_match devgroup_mt_reg __read_mostly = {
	.name		= "devgroup",
	.match		= devgroup_mt,
	.checkentry	= devgroup_mt_checkentry,
	.matchsize	= sizeof(struct xt_devgroup_info),
	.family		= NFPROTO_UNSPEC,
	.me		= THIS_MODULE
};

static int __init devgroup_mt_init(void)
{
	return xt_register_match(&devgroup_mt_reg);
}

static void __exit devgroup_mt_exit(void)
{
	xt_unregister_match(&devgroup_mt_reg);
}

module_init(devgroup_mt_init);
module_exit(devgroup_mt_exit);
