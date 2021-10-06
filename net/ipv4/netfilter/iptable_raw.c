// SPDX-License-Identifier: GPL-2.0-only
/*
 * 'raw' table, which is the very first hooked in at PRE_ROUTING and LOCAL_OUT .
 *
 * Copyright (C) 2003 Jozsef Kadlecsik <kadlec@netfilter.org>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/slab.h>
#include <net/ip.h>

#define RAW_VALID_HOOKS ((1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_LOCAL_OUT))

static bool raw_before_defrag __read_mostly;
MODULE_PARM_DESC(raw_before_defrag, "Enable raw table before defrag");
module_param(raw_before_defrag, bool, 0000);

static const struct xt_table packet_raw = {
	.name = "raw",
	.valid_hooks =  RAW_VALID_HOOKS,
	.me = THIS_MODULE,
	.af = NFPROTO_IPV4,
	.priority = NF_IP_PRI_RAW,
};

static const struct xt_table packet_raw_before_defrag = {
	.name = "raw",
	.valid_hooks =  RAW_VALID_HOOKS,
	.me = THIS_MODULE,
	.af = NFPROTO_IPV4,
	.priority = NF_IP_PRI_RAW_BEFORE_DEFRAG,
};

/* The work comes in here from netfilter.c. */
static unsigned int
iptable_raw_hook(void *priv, struct sk_buff *skb,
		 const struct nf_hook_state *state)
{
	return ipt_do_table(skb, state, priv);
}

static struct nf_hook_ops *rawtable_ops __read_mostly;

static int __net_init iptable_raw_table_init(struct net *net)
{
	struct ipt_replace *repl;
	const struct xt_table *table = &packet_raw;
	int ret;

	if (raw_before_defrag)
		table = &packet_raw_before_defrag;

	repl = ipt_alloc_initial_table(table);
	if (repl == NULL)
		return -ENOMEM;
	ret = ipt_register_table(net, table, repl, rawtable_ops);
	kfree(repl);
	return ret;
}

static void __net_exit iptable_raw_net_pre_exit(struct net *net)
{
	ipt_unregister_table_pre_exit(net, "raw");
}

static void __net_exit iptable_raw_net_exit(struct net *net)
{
	ipt_unregister_table_exit(net, "raw");
}

static struct pernet_operations iptable_raw_net_ops = {
	.pre_exit = iptable_raw_net_pre_exit,
	.exit = iptable_raw_net_exit,
};

static int __init iptable_raw_init(void)
{
	int ret;
	const struct xt_table *table = &packet_raw;

	if (raw_before_defrag) {
		table = &packet_raw_before_defrag;

		pr_info("Enabling raw table before defrag\n");
	}

	ret = xt_register_template(table,
				   iptable_raw_table_init);
	if (ret < 0)
		return ret;

	rawtable_ops = xt_hook_ops_alloc(table, iptable_raw_hook);
	if (IS_ERR(rawtable_ops)) {
		xt_unregister_template(table);
		return PTR_ERR(rawtable_ops);
	}

	ret = register_pernet_subsys(&iptable_raw_net_ops);
	if (ret < 0) {
		xt_unregister_template(table);
		kfree(rawtable_ops);
		return ret;
	}

	return ret;
}

static void __exit iptable_raw_fini(void)
{
	unregister_pernet_subsys(&iptable_raw_net_ops);
	kfree(rawtable_ops);
	xt_unregister_template(&packet_raw);
}

module_init(iptable_raw_init);
module_exit(iptable_raw_fini);
MODULE_LICENSE("GPL");
