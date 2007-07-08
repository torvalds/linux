/*
 * Module for modifying the secmark field of the skb, for use by
 * security subsystems.
 *
 * Based on the nfmark match by:
 * (C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *
 * (C) 2006 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/selinux.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_SECMARK.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Morris <jmorris@redhat.com>");
MODULE_DESCRIPTION("ip[6]tables SECMARK modification module");
MODULE_ALIAS("ipt_SECMARK");
MODULE_ALIAS("ip6t_SECMARK");

#define PFX "SECMARK: "

static u8 mode;

static unsigned int target(struct sk_buff **pskb, const struct net_device *in,
			   const struct net_device *out, unsigned int hooknum,
			   const struct xt_target *target,
			   const void *targinfo)
{
	u32 secmark = 0;
	const struct xt_secmark_target_info *info = targinfo;

	BUG_ON(info->mode != mode);

	switch (mode) {
	case SECMARK_MODE_SEL:
		secmark = info->u.sel.selsid;
		break;

	default:
		BUG();
	}

	(*pskb)->secmark = secmark;
	return XT_CONTINUE;
}

static bool checkentry_selinux(struct xt_secmark_target_info *info)
{
	int err;
	struct xt_secmark_target_selinux_info *sel = &info->u.sel;

	sel->selctx[SECMARK_SELCTX_MAX - 1] = '\0';

	err = selinux_string_to_sid(sel->selctx, &sel->selsid);
	if (err) {
		if (err == -EINVAL)
			printk(KERN_INFO PFX "invalid SELinux context \'%s\'\n",
			       sel->selctx);
		return false;
	}

	if (!sel->selsid) {
		printk(KERN_INFO PFX "unable to map SELinux context \'%s\'\n",
		       sel->selctx);
		return false;
	}

	err = selinux_relabel_packet_permission(sel->selsid);
	if (err) {
		printk(KERN_INFO PFX "unable to obtain relabeling permission\n");
		return false;
	}

	return true;
}

static bool checkentry(const char *tablename, const void *entry,
		       const struct xt_target *target, void *targinfo,
		       unsigned int hook_mask)
{
	struct xt_secmark_target_info *info = targinfo;

	if (mode && mode != info->mode) {
		printk(KERN_INFO PFX "mode already set to %hu cannot mix with "
		       "rules for mode %hu\n", mode, info->mode);
		return false;
	}

	switch (info->mode) {
	case SECMARK_MODE_SEL:
		if (!checkentry_selinux(info))
			return false;
		break;

	default:
		printk(KERN_INFO PFX "invalid mode: %hu\n", info->mode);
		return false;
	}

	if (!mode)
		mode = info->mode;
	return true;
}

static struct xt_target xt_secmark_target[] __read_mostly = {
	{
		.name		= "SECMARK",
		.family		= AF_INET,
		.checkentry	= checkentry,
		.target		= target,
		.targetsize	= sizeof(struct xt_secmark_target_info),
		.table		= "mangle",
		.me		= THIS_MODULE,
	},
	{
		.name		= "SECMARK",
		.family		= AF_INET6,
		.checkentry	= checkentry,
		.target		= target,
		.targetsize	= sizeof(struct xt_secmark_target_info),
		.table		= "mangle",
		.me		= THIS_MODULE,
	},
};

static int __init xt_secmark_init(void)
{
	return xt_register_targets(xt_secmark_target,
				   ARRAY_SIZE(xt_secmark_target));
}

static void __exit xt_secmark_fini(void)
{
	xt_unregister_targets(xt_secmark_target, ARRAY_SIZE(xt_secmark_target));
}

module_init(xt_secmark_init);
module_exit(xt_secmark_fini);
