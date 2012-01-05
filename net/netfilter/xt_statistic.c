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
#include <linux/slab.h>

#include <linux/netfilter/xt_statistic.h>
#include <linux/netfilter/x_tables.h>
#include <linux/module.h>

struct xt_statistic_priv {
	atomic_t count;
} ____cacheline_aligned_in_smp;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Xtables: statistics-based matching (\"Nth\", random)");
MODULE_ALIAS("ipt_statistic");
MODULE_ALIAS("ip6t_statistic");

static bool
statistic_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_statistic_info *info = par->matchinfo;
	bool ret = info->flags & XT_STATISTIC_INVERT;
	int nval, oval;

	switch (info->mode) {
	case XT_STATISTIC_MODE_RANDOM:
		if ((net_random() & 0x7FFFFFFF) < info->u.random.probability)
			ret = !ret;
		break;
	case XT_STATISTIC_MODE_NTH:
		do {
			oval = atomic_read(&info->master->count);
			nval = (oval == info->u.nth.every) ? 0 : oval + 1;
		} while (atomic_cmpxchg(&info->master->count, oval, nval) != oval);
		if (nval == 0)
			ret = !ret;
		break;
	}

	return ret;
}

static int statistic_mt_check(const struct xt_mtchk_param *par)
{
	struct xt_statistic_info *info = par->matchinfo;

	if (info->mode > XT_STATISTIC_MODE_MAX ||
	    info->flags & ~XT_STATISTIC_MASK)
		return -EINVAL;

	info->master = kzalloc(sizeof(*info->master), GFP_KERNEL);
	if (info->master == NULL)
		return -ENOMEM;
	atomic_set(&info->master->count, info->u.nth.count);

	return 0;
}

static void statistic_mt_destroy(const struct xt_mtdtor_param *par)
{
	const struct xt_statistic_info *info = par->matchinfo;

	kfree(info->master);
}

static struct xt_match xt_statistic_mt_reg __read_mostly = {
	.name       = "statistic",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.match      = statistic_mt,
	.checkentry = statistic_mt_check,
	.destroy    = statistic_mt_destroy,
	.matchsize  = sizeof(struct xt_statistic_info),
	.me         = THIS_MODULE,
};

static int __init statistic_mt_init(void)
{
	return xt_register_match(&xt_statistic_mt_reg);
}

static void __exit statistic_mt_exit(void)
{
	xt_unregister_match(&xt_statistic_mt_reg);
}

module_init(statistic_mt_init);
module_exit(statistic_mt_exit);
