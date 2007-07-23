/* This is a module which is used for setting the NFMARK field of an skb. */

/* (C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/checksum.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_MARK.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("ip[6]tables MARK modification module");
MODULE_ALIAS("ipt_MARK");
MODULE_ALIAS("ip6t_MARK");

static unsigned int
target_v0(struct sk_buff **pskb,
	  const struct net_device *in,
	  const struct net_device *out,
	  unsigned int hooknum,
	  const struct xt_target *target,
	  const void *targinfo)
{
	const struct xt_mark_target_info *markinfo = targinfo;

	(*pskb)->mark = markinfo->mark;
	return XT_CONTINUE;
}

static unsigned int
target_v1(struct sk_buff **pskb,
	  const struct net_device *in,
	  const struct net_device *out,
	  unsigned int hooknum,
	  const struct xt_target *target,
	  const void *targinfo)
{
	const struct xt_mark_target_info_v1 *markinfo = targinfo;
	int mark = 0;

	switch (markinfo->mode) {
	case XT_MARK_SET:
		mark = markinfo->mark;
		break;

	case XT_MARK_AND:
		mark = (*pskb)->mark & markinfo->mark;
		break;

	case XT_MARK_OR:
		mark = (*pskb)->mark | markinfo->mark;
		break;
	}

	(*pskb)->mark = mark;
	return XT_CONTINUE;
}


static bool
checkentry_v0(const char *tablename,
	      const void *entry,
	      const struct xt_target *target,
	      void *targinfo,
	      unsigned int hook_mask)
{
	const struct xt_mark_target_info *markinfo = targinfo;

	if (markinfo->mark > 0xffffffff) {
		printk(KERN_WARNING "MARK: Only supports 32bit wide mark\n");
		return false;
	}
	return true;
}

static bool
checkentry_v1(const char *tablename,
	      const void *entry,
	      const struct xt_target *target,
	      void *targinfo,
	      unsigned int hook_mask)
{
	const struct xt_mark_target_info_v1 *markinfo = targinfo;

	if (markinfo->mode != XT_MARK_SET
	    && markinfo->mode != XT_MARK_AND
	    && markinfo->mode != XT_MARK_OR) {
		printk(KERN_WARNING "MARK: unknown mode %u\n",
		       markinfo->mode);
		return false;
	}
	if (markinfo->mark > 0xffffffff) {
		printk(KERN_WARNING "MARK: Only supports 32bit wide mark\n");
		return false;
	}
	return true;
}

#ifdef CONFIG_COMPAT
struct compat_xt_mark_target_info_v1 {
	compat_ulong_t	mark;
	u_int8_t	mode;
	u_int8_t	__pad1;
	u_int16_t	__pad2;
};

static void compat_from_user_v1(void *dst, void *src)
{
	const struct compat_xt_mark_target_info_v1 *cm = src;
	struct xt_mark_target_info_v1 m = {
		.mark	= cm->mark,
		.mode	= cm->mode,
	};
	memcpy(dst, &m, sizeof(m));
}

static int compat_to_user_v1(void __user *dst, void *src)
{
	const struct xt_mark_target_info_v1 *m = src;
	struct compat_xt_mark_target_info_v1 cm = {
		.mark	= m->mark,
		.mode	= m->mode,
	};
	return copy_to_user(dst, &cm, sizeof(cm)) ? -EFAULT : 0;
}
#endif /* CONFIG_COMPAT */

static struct xt_target xt_mark_target[] __read_mostly = {
	{
		.name		= "MARK",
		.family		= AF_INET,
		.revision	= 0,
		.checkentry	= checkentry_v0,
		.target		= target_v0,
		.targetsize	= sizeof(struct xt_mark_target_info),
		.table		= "mangle",
		.me		= THIS_MODULE,
	},
	{
		.name		= "MARK",
		.family		= AF_INET,
		.revision	= 1,
		.checkentry	= checkentry_v1,
		.target		= target_v1,
		.targetsize	= sizeof(struct xt_mark_target_info_v1),
#ifdef CONFIG_COMPAT
		.compatsize	= sizeof(struct compat_xt_mark_target_info_v1),
		.compat_from_user = compat_from_user_v1,
		.compat_to_user	= compat_to_user_v1,
#endif
		.table		= "mangle",
		.me		= THIS_MODULE,
	},
	{
		.name		= "MARK",
		.family		= AF_INET6,
		.revision	= 0,
		.checkentry	= checkentry_v0,
		.target		= target_v0,
		.targetsize	= sizeof(struct xt_mark_target_info),
		.table		= "mangle",
		.me		= THIS_MODULE,
	},
};

static int __init xt_mark_init(void)
{
	return xt_register_targets(xt_mark_target, ARRAY_SIZE(xt_mark_target));
}

static void __exit xt_mark_fini(void)
{
	xt_unregister_targets(xt_mark_target, ARRAY_SIZE(xt_mark_target));
}

module_init(xt_mark_init);
module_exit(xt_mark_fini);
