// SPDX-License-Identifier: GPL-2.0-only
/*
 * IPv6 raw table, a port of the IPv4 raw table to IPv6
 *
 * Copyright (C) 2003 Jozsef Kadlecsik <kadlec@netfilter.org>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/slab.h>

#define RAW_VALID_HOOKS ((1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_LOCAL_OUT))

static int __net_init ip6table_raw_table_init(struct net *net);

static bool raw_before_defrag __read_mostly;
MODULE_PARM_DESC(raw_before_defrag, "Enable raw table before defrag");
module_param(raw_before_defrag, bool, 0000);

static const struct xt_table packet_raw = {
	.name = "raw",
	.valid_hooks = RAW_VALID_HOOKS,
	.me = THIS_MODULE,
	.af = NFPROTO_IPV6,
	.priority = NF_IP6_PRI_RAW,
	.table_init = ip6table_raw_table_init,
};

static const struct xt_table packet_raw_before_defrag = {
	.name = "raw",
	.valid_hooks = RAW_VALID_HOOKS,
	.me = THIS_MODULE,
	.af = NFPROTO_IPV6,
	.priority = NF_IP6_PRI_RAW_BEFORE_DEFRAG,
	.table_init = ip6table_raw_table_init,
};

/* The work comes in here from netfilter.c. */
static unsigned int
ip6table_raw_hook(void *priv, struct sk_buff *skb,
		  const struct nf_hook_state *state)
{
	return ip6t_do_table(skb, state, state->net->ipv6.ip6table_raw);
}

static struct nf_hook_ops *rawtable_ops __read_mostly;

static int __net_init ip6table_raw_table_init(struct net *net)
{
	struct ip6t_replace *repl;
	const struct xt_table *table = &packet_raw;
	int ret;

	if (raw_before_defrag)
		table = &packet_raw_before_defrag;

	if (net->ipv6.ip6table_raw)
		return 0;

	repl = ip6t_alloc_initial_table(table);
	if (repl == NULL)
		return -ENOMEM;
	ret = ip6t_register_table(net, table, repl, rawtable_ops,
				  &net->ipv6.ip6table_raw);
	kfree(repl);
	return ret;
}

static void __net_exit ip6table_raw_net_pre_exit(struct net *net)
{
	if (net->ipv6.ip6table_raw)
		ip6t_unregister_table_pre_exit(net, net->ipv6.ip6table_raw,
					       rawtable_ops);
}

static void __net_exit ip6table_raw_net_exit(struct net *net)
{
	if (!net->ipv6.ip6table_raw)
		return;
	ip6t_unregister_table_exit(net, net->ipv6.ip6table_raw);
	net->ipv6.ip6table_raw = NULL;
}

static struct pernet_operations ip6table_raw_net_ops = {
	.pre_exit = ip6table_raw_net_pre_exit,
	.exit = ip6table_raw_net_exit,
};

static int __init ip6table_raw_init(void)
{
	int ret;
	const struct xt_table *table = &packet_raw;

	if (raw_before_defrag) {
		table = &packet_raw_before_defrag;

		pr_info("Enabling raw table before defrag\n");
	}

	/* Register hooks */
	rawtable_ops = xt_hook_ops_alloc(table, ip6table_raw_hook);
	if (IS_ERR(rawtable_ops))
		return PTR_ERR(rawtable_ops);

	ret = register_pernet_subsys(&ip6table_raw_net_ops);
	if (ret < 0) {
		kfree(rawtable_ops);
		return ret;
	}

	ret = ip6table_raw_table_init(&init_net);
	if (ret) {
		unregister_pernet_subsys(&ip6table_raw_net_ops);
		kfree(rawtable_ops);
	}
	return ret;
}

static void __exit ip6table_raw_fini(void)
{
	unregister_pernet_subsys(&ip6table_raw_net_ops);
	kfree(rawtable_ops);
}

module_init(ip6table_raw_init);
module_exit(ip6table_raw_fini);
MODULE_LICENSE("GPL");
