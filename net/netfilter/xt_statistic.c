/*
 * Copyright (c) 2006 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on ipt_random and ipt_nth by Fabrice MARIE <fabrice@netfilter.org>.
 */

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/net.h>

#include <linux/netfilter/xt_statistic.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("xtables statistical match module");
MODULE_ALIAS("ipt_statistic");
MODULE_ALIAS("ip6t_statistic");

static DEFINE_SPINLOCK(nth_lock);

static int
match(const struct sk_buff *skb,
      const struct net_device *in, const struct net_device *out,
      const struct xt_match *match, const void *matchinfo,
      int offset, unsigned int protoff, int *hotdrop)
{
	struct xt_statistic_info *info = (struct xt_statistic_info *)matchinfo;
	int ret = info->flags & XT_STATISTIC_INVERT ? 1 : 0;

	switch (info->mode) {
	case XT_STATISTIC_MODE_RANDOM:
		if ((net_random() & 0x7FFFFFFF) < info->u.random.probability)
			ret ^= 1;
		break;
	case XT_STATISTIC_MODE_NTH:
		info = info->master;
		spin_lock_bh(&nth_lock);
		if (info->u.nth.count++ == info->u.nth.every) {
			info->u.nth.count = 0;
			ret ^= 1;
		}
		spin_unlock_bh(&nth_lock);
		break;
	}

	return ret;
}

static int
checkentry(const char *tablename, const void *entry,
	   const struct xt_match *match, void *matchinfo,
	   unsigned int matchsize, unsigned int hook_mask)
{
	struct xt_statistic_info *info = (struct xt_statistic_info *)matchinfo;

	if (info->mode > XT_STATISTIC_MODE_MAX ||
	    info->flags & ~XT_STATISTIC_MASK)
		return 0;
	info->master = info;
	return 1;
}

static struct xt_match statistic_match = {
	.name		= "statistic",
	.match		= match,
	.matchsize	= sizeof(struct xt_statistic_info),
	.checkentry	= checkentry,
	.family		= AF_INET,
	.me		= THIS_MODULE,
};

static struct xt_match statistic_match6 = {
	.name		= "statistic",
	.match		= match,
	.matchsize	= sizeof(struct xt_statistic_info),
	.checkentry	= checkentry,
	.family		= AF_INET6,
	.me		= THIS_MODULE,
};

static int __init xt_statistic_init(void)
{
	int ret;

	ret = xt_register_match(&statistic_match);
	if (ret)
		goto err1;

	ret = xt_register_match(&statistic_match6);
	if (ret)
		goto err2;
	return ret;
err2:
	xt_unregister_match(&statistic_match);
err1:
	return ret;
}

static void __exit xt_statistic_fini(void)
{
	xt_unregister_match(&statistic_match6);
	xt_unregister_match(&statistic_match);
}

module_init(xt_statistic_init);
module_exit(xt_statistic_fini);
