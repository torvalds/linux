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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

static unsigned int
ip6table_security_hook(unsigned int hook, struct sk_buff *skb,
		       const struct net_device *in,
		       const struct net_device *out,
		       int (*okfn)(struct sk_buff *))
{
	const struct net *net = dev_net((in != NULL) ? in : out);

	return ip6t_do_table(skb, hook, in, out, net->ipv6.ip6table_security);
}

static struct nf_hook_ops *sectbl_ops __read_mostly;

static int __net_init ip6table_security_net_init(struct net *net)
{
	struct ip6t_replace *repl;

	repl = ip6t_alloc_initial_table(&security_table);
	if (repl == NULL)
		return -ENOMEM;
	net->ipv6.ip6table_security =
		ip6t_register_table(net, &security_table, repl);
	kfree(repl);
	if (IS_ERR(net->ipv6.ip6table_security))
		return PTR_ERR(net->ipv6.ip6table_security);

	return 0;
}

static void __net_exit ip6table_security_net_exit(struct net *net)
{
	ip6t_unregister_table(net, net->ipv6.ip6table_security);
}

static struct pernet_operations ip6table_security_net_ops = {
	.init = ip6table_security_net_init,
	.exit = ip6table_security_net_exit,
};

static int __init ip6table_security_init(void)
{
	int ret;

	ret = register_pernet_subsys(&ip6table_security_net_ops);
	if (ret < 0)
		return ret;

	sectbl_ops = xt_hook_link(&security_table, ip6table_security_hook);
	if (IS_ERR(sectbl_ops)) {
		ret = PTR_ERR(sectbl_ops);
		goto cleanup_table;
	}

	return ret;

cleanup_table:
	unregister_pernet_subsys(&ip6table_security_net_ops);
	return ret;
}

static void __exit ip6table_security_fini(void)
{
	xt_hook_unlink(&security_table, sectbl_ops);
	unregister_pernet_subsys(&ip6table_security_net_ops);
}

module_init(ip6table_security_init);
module_exit(ip6table_security_fini);
