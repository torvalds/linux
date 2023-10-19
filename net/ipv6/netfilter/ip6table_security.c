// SPDX-License-Identifier: GPL-2.0-only
/*
 * "security" table for IPv6
 *
 * This is for use by Mandatory Access Control (MAC) security models,
 * which need to be able to manage security policy in separate context
 * to DAC.
 *
 * Based on iptable_mangle.c
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2004 Netfilter Core Team <coreteam <at> netfilter.org>
 * Copyright (C) 2008 Red Hat, Inc., James Morris <jmorris <at> redhat.com>
 */
#include <linux/module.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Morris <jmorris <at> redhat.com>");
MODULE_DESCRIPTION("ip6tables security table, for MAC rules");

#define SECURITY_VALID_HOOKS	(1 << NF_INET_LOCAL_IN) | \
				(1 << NF_INET_FORWARD) | \
				(1 << NF_INET_LOCAL_OUT)

static const struct xt_table security_table = {
	.name		= "security",
	.valid_hooks	= SECURITY_VALID_HOOKS,
	.me		= THIS_MODULE,
	.af		= NFPROTO_IPV6,
	.priority	= NF_IP6_PRI_SECURITY,
};

static struct nf_hook_ops *sectbl_ops __read_mostly;

static int ip6table_security_table_init(struct net *net)
{
	struct ip6t_replace *repl;
	int ret;

	repl = ip6t_alloc_initial_table(&security_table);
	if (repl == NULL)
		return -ENOMEM;
	ret = ip6t_register_table(net, &security_table, repl, sectbl_ops);
	kfree(repl);
	return ret;
}

static void __net_exit ip6table_security_net_pre_exit(struct net *net)
{
	ip6t_unregister_table_pre_exit(net, "security");
}

static void __net_exit ip6table_security_net_exit(struct net *net)
{
	ip6t_unregister_table_exit(net, "security");
}

static struct pernet_operations ip6table_security_net_ops = {
	.pre_exit = ip6table_security_net_pre_exit,
	.exit = ip6table_security_net_exit,
};

static int __init ip6table_security_init(void)
{
	int ret = xt_register_template(&security_table,
				       ip6table_security_table_init);

	if (ret < 0)
		return ret;

	sectbl_ops = xt_hook_ops_alloc(&security_table, ip6t_do_table);
	if (IS_ERR(sectbl_ops)) {
		xt_unregister_template(&security_table);
		return PTR_ERR(sectbl_ops);
	}

	ret = register_pernet_subsys(&ip6table_security_net_ops);
	if (ret < 0) {
		kfree(sectbl_ops);
		xt_unregister_template(&security_table);
		return ret;
	}

	return ret;
}

static void __exit ip6table_security_fini(void)
{
	unregister_pernet_subsys(&ip6table_security_net_ops);
	xt_unregister_template(&security_table);
	kfree(sectbl_ops);
}

module_init(ip6table_security_init);
module_exit(ip6table_security_fini);
