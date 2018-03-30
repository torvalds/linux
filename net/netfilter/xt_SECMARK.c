/*
 * Module for modifying the secmark field of the skb, for use by
 * security subsystems.
 *
 * Based on the nfmark match by:
 * (C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *
 * (C) 2006,2008 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/security.h>
#include <linux/skbuff.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_SECMARK.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Morris <jmorris@redhat.com>");
MODULE_DESCRIPTION("Xtables: packet security mark modification");
MODULE_ALIAS("ipt_SECMARK");
MODULE_ALIAS("ip6t_SECMARK");

#define PFX "SECMARK: "

static u8 mode;

static unsigned int
secmark_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	u32 secmark = 0;
	const struct xt_secmark_target_info *info = par->targinfo;

	BUG_ON(info->mode != mode);

	switch (mode) {
	case SECMARK_MODE_SEL:
		secmark = info->secid;
		break;
	default:
		BUG();
	}

	skb->secmark = secmark;
	return XT_CONTINUE;
}

static int checkentry_lsm(struct xt_secmark_target_info *info)
{
	int err;

	info->secctx[SECMARK_SECCTX_MAX - 1] = '\0';
	info->secid = 0;

	err = security_secctx_to_secid(info->secctx, strlen(info->secctx),
				       &info->secid);
	if (err) {
		if (err == -EINVAL)
			pr_info_ratelimited("invalid security context \'%s\'\n",
					    info->secctx);
		return err;
	}

	if (!info->secid) {
		pr_info_ratelimited("unable to map security context \'%s\'\n",
				    info->secctx);
		return -ENOENT;
	}

	err = security_secmark_relabel_packet(info->secid);
	if (err) {
		pr_info_ratelimited("unable to obtain relabeling permission\n");
		return err;
	}

	security_secmark_refcount_inc();
	return 0;
}

static int secmark_tg_check(const struct xt_tgchk_param *par)
{
	struct xt_secmark_target_info *info = par->targinfo;
	int err;

	if (strcmp(par->table, "mangle") != 0 &&
	    strcmp(par->table, "security") != 0) {
		pr_info_ratelimited("only valid in \'mangle\' or \'security\' table, not \'%s\'\n",
				    par->table);
		return -EINVAL;
	}

	if (mode && mode != info->mode) {
		pr_info_ratelimited("mode already set to %hu cannot mix with rules for mode %hu\n",
				    mode, info->mode);
		return -EINVAL;
	}

	switch (info->mode) {
	case SECMARK_MODE_SEL:
		break;
	default:
		pr_info_ratelimited("invalid mode: %hu\n", info->mode);
		return -EINVAL;
	}

	err = checkentry_lsm(info);
	if (err)
		return err;

	if (!mode)
		mode = info->mode;
	return 0;
}

static void secmark_tg_destroy(const struct xt_tgdtor_param *par)
{
	switch (mode) {
	case SECMARK_MODE_SEL:
		security_secmark_refcount_dec();
	}
}

static struct xt_target secmark_tg_reg __read_mostly = {
	.name       = "SECMARK",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.checkentry = secmark_tg_check,
	.destroy    = secmark_tg_destroy,
	.target     = secmark_tg,
	.targetsize = sizeof(struct xt_secmark_target_info),
	.me         = THIS_MODULE,
};

static int __init secmark_tg_init(void)
{
	return xt_register_target(&secmark_tg_reg);
}

static void __exit secmark_tg_exit(void)
{
	xt_unregister_target(&secmark_tg_reg);
}

module_init(secmark_tg_init);
module_exit(secmark_tg_exit);
