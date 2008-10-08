/* iptables module to match on related connections */
/*
 * (C) 2001 Martin Josefsson <gandalf@wlug.westbo.se>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_helper.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Josefsson <gandalf@netfilter.org>");
MODULE_DESCRIPTION("Xtables: Related connection matching");
MODULE_ALIAS("ipt_helper");
MODULE_ALIAS("ip6t_helper");


static bool
helper_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct xt_helper_info *info = par->matchinfo;
	const struct nf_conn *ct;
	const struct nf_conn_help *master_help;
	const struct nf_conntrack_helper *helper;
	enum ip_conntrack_info ctinfo;
	bool ret = info->invert;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct || !ct->master)
		return ret;

	master_help = nfct_help(ct->master);
	if (!master_help)
		return ret;

	/* rcu_read_lock()ed by nf_hook_slow */
	helper = rcu_dereference(master_help->helper);
	if (!helper)
		return ret;

	if (info->name[0] == '\0')
		ret = !ret;
	else
		ret ^= !strncmp(helper->name, info->name,
				strlen(helper->name));
	return ret;
}

static bool helper_mt_check(const struct xt_mtchk_param *par)
{
	struct xt_helper_info *info = par->matchinfo;

	if (nf_ct_l3proto_try_module_get(par->family) < 0) {
		printk(KERN_WARNING "can't load conntrack support for "
				    "proto=%u\n", par->family);
		return false;
	}
	info->name[29] = '\0';
	return true;
}

static void helper_mt_destroy(const struct xt_mtdtor_param *par)
{
	nf_ct_l3proto_module_put(par->family);
}

static struct xt_match helper_mt_reg __read_mostly = {
	.name       = "helper",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.checkentry = helper_mt_check,
	.match      = helper_mt,
	.destroy    = helper_mt_destroy,
	.matchsize  = sizeof(struct xt_helper_info),
	.me         = THIS_MODULE,
};

static int __init helper_mt_init(void)
{
	return xt_register_match(&helper_mt_reg);
}

static void __exit helper_mt_exit(void)
{
	xt_unregister_match(&helper_mt_reg);
}

module_init(helper_mt_init);
module_exit(helper_mt_exit);
