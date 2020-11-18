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

static int __net_init iptable_raw_table_init(struct net *net);

static bool raw_before_defrag __read_mostly;
MODULE_PARM_DESC(raw_before_defrag, "Enable raw table before defrag");
module_param(raw_before_defrag, bool, 0000);

static const struct xt_table packet_raw = {
	.name = "raw",
	.valid_hooks =  RAW_VALID_HOOKS,
	.me = THIS_MODULE,
	.af = NFPROTO_IPV4,
	.priority = NF_IP_PRI_RAW,
	.table_init = iptable_raw_table_init,
};

static const struct xt_table packet_raw_before_defrag = {
	.name = "raw",
	.valid_hooks =  RAW_VALID_HOOKS,
	.me = THIS_MODULE,
	.af = NFPROTO_IPV4,
	.priority = NF_IP_PRI_RAW_BEFORE_DEFRAG,
	.table_init = iptable_raw_table_init,
};

/* The work comes in here from netfilter.c. */
static unsigned int
iptable_raw_hook(void *priv, struct sk_buff *skb,
		 const struct nf_hook_state *state)
{
	return ipt_do_table(skb, state, state->net->ipv4.iptable_raw);
}

static struct nf_hook_ops *rawtable_ops __read_mostly;

static int __net_init iptable_raw_table_init(struct net *net)
{
	struct ipt_replace *repl;
	const struct xt_table *table = &packet_raw;
	int ret;

	if (raw_before_defrag)
		table = &packet_raw_before_defrag;

	if (net->ipv4.iptable_raw)
		return 0;

	repl = ipt_alloc_initial_table(table);
	if (repl == NULL)
		return -ENOMEM;
	ret = ipt_register_table(net, table, repl, rawtable_ops,
				 &net->ipv4.iptable_raw);
	kfree(repl);
	return ret;
}

static void __net_exit iptable_raw_net_pre_exit(struct net *net)
{
	if (net->ipv4.iptable_raw)
		ipt_unregister_table_pre_exit(net, net->ipv4.iptable_raw,
					      rawtable_ops);
}

static void __net_exit iptable_raw_net_exit(struct net *net)
{
	if (!net->ipv4.iptable_raw)
		return;
	ipt_unregister_table_exit(net, net->ipv4.iptable_raw);
	net->ipv4.iptable_raw = NULL;
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

	rawtable_ops = xt_hook_ops_alloc(table, iptable_raw_hook);
	if (IS_ERR(rawtable_ops))
		return PTR_ERR(rawtable_ops);

	ret = register_pernet_subsys(&iptable_raw_net_ops);
	if (ret < 0) {
		kfree(rawtable_ops);
		return ret;
	}

	ret = iptable_raw_table_init(&init_net);
	if (ret) {
		unregister_pernet_subsys(&iptable_raw_net_ops);
		kfree(rawtable_ops);
	}

	return ret;
}

static void __exit iptable_raw_fini(void)
{
	unregister_pernet_subsys(&iptable_raw_net_ops);
	kfree(rawtable_ops);
}

module_init(iptable_raw_init);
module_exit(iptable_raw_fini);
MODULE_LICENSE("GPL");
