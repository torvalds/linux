// SPDX-License-Identifier: GPL-2.0-only
/*
 * "security" table
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
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/slab.h>
#include <net/ip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Morris <jmorris <at> redhat.com>");
MODULE_DESCRIPTION("iptables security table, for MAC rules");

#define SECURITY_VALID_HOOKS	(1 << NF_INET_LOCAL_IN) | \
				(1 << NF_INET_FORWARD) | \
				(1 << NF_INET_LOCAL_OUT)

static const struct xt_table security_table = {
	.name		= "security",
	.valid_hooks	= SECURITY_VALID_HOOKS,
	.me		= THIS_MODULE,
	.af		= NFPROTO_IPV4,
	.priority	= NF_IP_PRI_SECURITY,
};

static unsigned int
iptable_security_hook(void *priv, struct sk_buff *skb,
		      const struct nf_hook_state *state)
{
	return ipt_do_table(skb, state, priv);
}

static struct nf_hook_ops *sectbl_ops __read_mostly;

static int iptable_security_table_init(struct net *net)
{
	struct ipt_replace *repl;
	int ret;

	repl = ipt_alloc_initial_table(&security_table);
	if (repl == NULL)
		return -ENOMEM;
	ret = ipt_register_table(net, &security_table, repl, sectbl_ops);
	kfree(repl);
	return ret;
}

static void __net_exit iptable_security_net_pre_exit(struct net *net)
{
	ipt_unregister_table_pre_exit(net, "security");
}

static void __net_exit iptable_security_net_exit(struct net *net)
{
	ipt_unregister_table_exit(net, "security");
}

static struct pernet_operations iptable_security_net_ops = {
	.pre_exit = iptable_security_net_pre_exit,
	.exit = iptable_security_net_exit,
};

static int __init iptable_security_init(void)
{
	int ret = xt_register_template(&security_table,
				       iptable_security_table_init);

	if (ret < 0)
		return ret;

	sectbl_ops = xt_hook_ops_alloc(&security_table, iptable_security_hook);
	if (IS_ERR(sectbl_ops)) {
		xt_unregister_template(&security_table);
		return PTR_ERR(sectbl_ops);
	}

	ret = register_pernet_subsys(&iptable_security_net_ops);
	if (ret < 0) {
		xt_unregister_template(&security_table);
		kfree(sectbl_ops);
		return ret;
	}

	return ret;
}

static void __exit iptable_security_fini(void)
{
	unregister_pernet_subsys(&iptable_security_net_ops);
	kfree(sectbl_ops);
	xt_unregister_template(&security_table);
}

module_init(iptable_security_init);
module_exit(iptable_security_fini);
