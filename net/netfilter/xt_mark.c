/*
 *	xt_mark - Netfilter module to match NFMARK value
 *
 *	(C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *	Copyright © CC Computer Consultants GmbH, 2007 - 2008
 *	Jan Engelhardt <jengelh@computergmbh.de>
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
mark_mt_v0(const struct sk_buff *skb, const struct net_device *in,
           const struct net_device *out, const struct xt_match *match,
           const void *matchinfo, int offset, unsigned int protoff,
           bool *hotdrop)
{
	const struct xt_mark_info *info = matchinfo;

	return ((skb->mark & info->mask) == info->mark) ^ info->invert;
}

static bool
mark_mt(const struct sk_buff *skb, const struct net_device *in,
        const struct net_device *out, const struct xt_match *match,
        const void *matchinfo, int offset, unsigned int protoff, bool *hotdrop)
{
	const struct xt_mark_mtinfo1 *info = matchinfo;

	return ((skb->mark & info->mask) == info->mark) ^ info->invert;
}

static bool
mark_mt_check_v0(const char *tablename, const void *entry,
                 const struct xt_match *match, void *matchinfo,
                 unsigned int hook_mask)
{
	const struct xt_mark_info *minfo = matchinfo;

	if (minfo->mark > 0xffffffff || minfo->mask > 0xffffffff) {
		printk(KERN_WARNING "mark: only supports 32bit mark\n");
		return false;
	}
	return true;
}

#ifdef CONFIG_COMPAT
struct compat_xt_mark_info {
	compat_ulong_t	mark, mask;
	u_int8_t	invert;
	u_int8_t	__pad1;
	u_int16_t	__pad2;
};

static void mark_mt_compat_from_user_v0(void *dst, void *src)
{
	const struct compat_xt_mark_info *cm = src;
	struct xt_mark_info m = {
		.mark	= cm->mark,
		.mask	= cm->mask,
		.invert	= cm->invert,
	};
	memcpy(dst, &m, sizeof(m));
}

static int mark_mt_compat_to_user_v0(void __user *dst, void *src)
{
	const struct xt_mark_info *m = src;
	struct compat_xt_mark_info cm = {
		.mark	= m->mark,
		.mask	= m->mask,
		.invert	= m->invert,
	};
	return copy_to_user(dst, &cm, sizeof(cm)) ? -EFAULT : 0;
}
#endif /* CONFIG_COMPAT */

static struct xt_match mark_mt_reg[] __read_mostly = {
	{
		.name		= "mark",
		.revision	= 0,
		.family		= NFPROTO_UNSPEC,
		.checkentry	= mark_mt_check_v0,
		.match		= mark_mt_v0,
		.matchsize	= sizeof(struct xt_mark_info),
#ifdef CONFIG_COMPAT
		.compatsize	= sizeof(struct compat_xt_mark_info),
		.compat_from_user = mark_mt_compat_from_user_v0,
		.compat_to_user	= mark_mt_compat_to_user_v0,
#endif
		.me		= THIS_MODULE,
	},
	{
		.name           = "mark",
		.revision       = 1,
		.family         = NFPROTO_UNSPEC,
		.match          = mark_mt,
		.matchsize      = sizeof(struct xt_mark_mtinfo1),
		.me             = THIS_MODULE,
	},
};

static int __init mark_mt_init(void)
{
	return xt_register_matches(mark_mt_reg, ARRAY_SIZE(mark_mt_reg));
}

static void __exit mark_mt_exit(void)
{
	xt_unregister_matches(mark_mt_reg, ARRAY_SIZE(mark_mt_reg));
}

module_init(mark_mt_init);
module_exit(mark_mt_exit);
