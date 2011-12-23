/*
 * (C) 2011 Pablo Neira Ayuso <pablo@netfilter.org>
 * (C) 2011 Intra2net AG <http://www.intra2net.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 (or any
 * later at your option) as published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/nfnetlink_acct.h>
#include <linux/netfilter/xt_nfacct.h>

MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_DESCRIPTION("Xtables: match for the extended accounting infrastructure");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_nfacct");
MODULE_ALIAS("ip6t_nfacct");

static bool nfacct_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_nfacct_match_info *info = par->targinfo;

	nfnl_acct_update(skb, info->nfacct);

	return true;
}

static int
nfacct_mt_checkentry(const struct xt_mtchk_param *par)
{
	struct xt_nfacct_match_info *info = par->matchinfo;
	struct nf_acct *nfacct;

	nfacct = nfnl_acct_find_get(info->name);
	if (nfacct == NULL) {
		pr_info("xt_nfacct: accounting object with name `%s' "
			"does not exists\n", info->name);
		return -ENOENT;
	}
	info->nfacct = nfacct;
	return 0;
}

static void
nfacct_mt_destroy(const struct xt_mtdtor_param *par)
{
	const struct xt_nfacct_match_info *info = par->matchinfo;

	nfnl_acct_put(info->nfacct);
}

static struct xt_match nfacct_mt_reg __read_mostly = {
	.name       = "nfacct",
	.family     = NFPROTO_UNSPEC,
	.checkentry = nfacct_mt_checkentry,
	.match      = nfacct_mt,
	.destroy    = nfacct_mt_destroy,
	.matchsize  = sizeof(struct xt_nfacct_match_info),
	.me         = THIS_MODULE,
};

static int __init nfacct_mt_init(void)
{
	return xt_register_match(&nfacct_mt_reg);
}

static void __exit nfacct_mt_exit(void)
{
	xt_unregister_match(&nfacct_mt_reg);
}

module_init(nfacct_mt_init);
module_exit(nfacct_mt_exit);
