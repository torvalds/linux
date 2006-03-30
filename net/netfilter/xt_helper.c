/* iptables module to match on related connections */
/*
 * (C) 2001 Martin Josefsson <gandalf@wlug.westbo.se>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *   19 Mar 2002 Harald Welte <laforge@gnumonks.org>:
 *   		 - Port to newnat infrastructure
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#if defined(CONFIG_IP_NF_CONNTRACK) || defined(CONFIG_IP_NF_CONNTRACK_MODULE)
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#else
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_helper.h>
#endif
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_helper.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Josefsson <gandalf@netfilter.org>");
MODULE_DESCRIPTION("iptables helper match module");
MODULE_ALIAS("ipt_helper");
MODULE_ALIAS("ip6t_helper");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

#if defined(CONFIG_IP_NF_CONNTRACK) || defined(CONFIG_IP_NF_CONNTRACK_MODULE)
static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_helper_info *info = matchinfo;
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	int ret = info->invert;
	
	ct = ip_conntrack_get((struct sk_buff *)skb, &ctinfo);
	if (!ct) {
		DEBUGP("xt_helper: Eek! invalid conntrack?\n");
		return ret;
	}

	if (!ct->master) {
		DEBUGP("xt_helper: conntrack %p has no master\n", ct);
		return ret;
	}

	read_lock_bh(&ip_conntrack_lock);
	if (!ct->master->helper) {
		DEBUGP("xt_helper: master ct %p has no helper\n", 
			exp->expectant);
		goto out_unlock;
	}

	DEBUGP("master's name = %s , info->name = %s\n", 
		ct->master->helper->name, info->name);

	if (info->name[0] == '\0')
		ret ^= 1;
	else
		ret ^= !strncmp(ct->master->helper->name, info->name, 
		                strlen(ct->master->helper->name));
out_unlock:
	read_unlock_bh(&ip_conntrack_lock);
	return ret;
}

#else /* CONFIG_IP_NF_CONNTRACK */

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_helper_info *info = matchinfo;
	struct nf_conn *ct;
	struct nf_conn_help *master_help;
	enum ip_conntrack_info ctinfo;
	int ret = info->invert;
	
	ct = nf_ct_get((struct sk_buff *)skb, &ctinfo);
	if (!ct) {
		DEBUGP("xt_helper: Eek! invalid conntrack?\n");
		return ret;
	}

	if (!ct->master) {
		DEBUGP("xt_helper: conntrack %p has no master\n", ct);
		return ret;
	}

	read_lock_bh(&nf_conntrack_lock);
	master_help = nfct_help(ct->master);
	if (!master_help || !master_help->helper) {
		DEBUGP("xt_helper: master ct %p has no helper\n", 
			exp->expectant);
		goto out_unlock;
	}

	DEBUGP("master's name = %s , info->name = %s\n", 
		ct->master->helper->name, info->name);

	if (info->name[0] == '\0')
		ret ^= 1;
	else
		ret ^= !strncmp(master_help->helper->name, info->name,
		                strlen(master_help->helper->name));
out_unlock:
	read_unlock_bh(&nf_conntrack_lock);
	return ret;
}
#endif

static int check(const char *tablename,
		 const void *inf,
		 const struct xt_match *match,
		 void *matchinfo,
		 unsigned int matchsize,
		 unsigned int hook_mask)
{
	struct xt_helper_info *info = matchinfo;

#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	if (nf_ct_l3proto_try_module_get(match->family) < 0) {
		printk(KERN_WARNING "can't load nf_conntrack support for "
				    "proto=%d\n", match->family);
		return 0;
	}
#endif
	info->name[29] = '\0';
	return 1;
}

static void
destroy(const struct xt_match *match, void *matchinfo, unsigned int matchsize)
{
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	nf_ct_l3proto_module_put(match->family);
#endif
}

static struct xt_match helper_match = {
	.name		= "helper",
	.match		= match,
	.matchsize	= sizeof(struct xt_helper_info),
	.checkentry	= check,
	.destroy	= destroy,
	.family		= AF_INET,
	.me		= THIS_MODULE,
};
static struct xt_match helper6_match = {
	.name		= "helper",
	.match		= match,
	.matchsize	= sizeof(struct xt_helper_info),
	.checkentry	= check,
	.destroy	= destroy,
	.family		= AF_INET6,
	.me		= THIS_MODULE,
};

static int __init xt_helper_init(void)
{
	int ret;
	need_conntrack();

	ret = xt_register_match(&helper_match);
	if (ret < 0)
		return ret;

	ret = xt_register_match(&helper6_match);
	if (ret < 0)
		xt_unregister_match(&helper_match);

	return ret;
}

static void __exit xt_helper_fini(void)
{
	xt_unregister_match(&helper_match);
	xt_unregister_match(&helper6_match);
}

module_init(xt_helper_init);
module_exit(xt_helper_fini);

