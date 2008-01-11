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
MODULE_DESCRIPTION("iptables helper match module");
MODULE_ALIAS("ipt_helper");
MODULE_ALIAS("ip6t_helper");


static bool
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      bool *hotdrop)
{
	const struct xt_helper_info *info = matchinfo;
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

static bool check(const char *tablename,
		  const void *inf,
		  const struct xt_match *match,
		  void *matchinfo,
		  unsigned int hook_mask)
{
	struct xt_helper_info *info = matchinfo;

	if (nf_ct_l3proto_try_module_get(match->family) < 0) {
		printk(KERN_WARNING "can't load conntrack support for "
				    "proto=%d\n", match->family);
		return false;
	}
	info->name[29] = '\0';
	return true;
}

static void
destroy(const struct xt_match *match, void *matchinfo)
{
	nf_ct_l3proto_module_put(match->family);
}

static struct xt_match xt_helper_match[] __read_mostly = {
	{
		.name		= "helper",
		.family		= AF_INET,
		.checkentry	= check,
		.match		= match,
		.destroy	= destroy,
		.matchsize	= sizeof(struct xt_helper_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "helper",
		.family		= AF_INET6,
		.checkentry	= check,
		.match		= match,
		.destroy	= destroy,
		.matchsize	= sizeof(struct xt_helper_info),
		.me		= THIS_MODULE,
	},
};

static int __init xt_helper_init(void)
{
	return xt_register_matches(xt_helper_match,
				   ARRAY_SIZE(xt_helper_match));
}

static void __exit xt_helper_fini(void)
{
	xt_unregister_matches(xt_helper_match, ARRAY_SIZE(xt_helper_match));
}

module_init(xt_helper_init);
module_exit(xt_helper_fini);

