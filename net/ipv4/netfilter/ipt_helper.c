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
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_helper.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Josefsson <gandalf@netfilter.org>");
MODULE_DESCRIPTION("iptables helper match module");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      int *hotdrop)
{
	const struct ipt_helper_info *info = matchinfo;
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	int ret = info->invert;
	
	ct = ip_conntrack_get((struct sk_buff *)skb, &ctinfo);
	if (!ct) {
		DEBUGP("ipt_helper: Eek! invalid conntrack?\n");
		return ret;
	}

	if (!ct->master) {
		DEBUGP("ipt_helper: conntrack %p has no master\n", ct);
		return ret;
	}

	read_lock_bh(&ip_conntrack_lock);
	if (!ct->master->helper) {
		DEBUGP("ipt_helper: master ct %p has no helper\n", 
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

static int check(const char *tablename,
		 const struct ipt_ip *ip,
		 void *matchinfo,
		 unsigned int matchsize,
		 unsigned int hook_mask)
{
	struct ipt_helper_info *info = matchinfo;

	info->name[29] = '\0';

	/* verify size */
	if (matchsize != IPT_ALIGN(sizeof(struct ipt_helper_info)))
		return 0;

	return 1;
}

static struct ipt_match helper_match = {
	.name		= "helper",
	.match		= &match,
	.checkentry	= &check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	need_ip_conntrack();
	return ipt_register_match(&helper_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&helper_match);
}

module_init(init);
module_exit(fini);

