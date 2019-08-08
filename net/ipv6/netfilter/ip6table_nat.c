// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * Based on Rusty Russell's IPv4 NAT code. Development of IPv6 NAT
 * funded by Astaro.
 */

#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>

#include <net/netfilter/nf_nat.h>

static int __net_init ip6table_nat_table_init(struct net *net);

static const struct xt_table nf_nat_ipv6_table = {
	.name		= "nat",
	.valid_hooks	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.me		= THIS_MODULE,
	.af		= NFPROTO_IPV6,
	.table_init	= ip6table_nat_table_init,
};

static unsigned int ip6table_nat_do_chain(void *priv,
					  struct sk_buff *skb,
					  const struct nf_hook_state *state)
{
	return ip6t_do_table(skb, state, state->net->ipv6.ip6table_nat);
}

static const struct nf_hook_ops nf_nat_ipv6_ops[] = {
	{
		.hook		= ip6table_nat_do_chain,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	{
		.hook		= ip6table_nat_do_chain,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
	{
		.hook		= ip6table_nat_do_chain,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	{
		.hook		= ip6table_nat_do_chain,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
};

static int ip6t_nat_register_lookups(struct net *net)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(nf_nat_ipv6_ops); i++) {
		ret = nf_nat_ipv6_register_fn(net, &nf_nat_ipv6_ops[i]);
		if (ret) {
			while (i)
				nf_nat_ipv6_unregister_fn(net, &nf_nat_ipv6_ops[--i]);

			return ret;
		}
	}

	return 0;
}

static void ip6t_nat_unregister_lookups(struct net *net)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nf_nat_ipv6_ops); i++)
		nf_nat_ipv6_unregister_fn(net, &nf_nat_ipv6_ops[i]);
}

static int __net_init ip6table_nat_table_init(struct net *net)
{
	struct ip6t_replace *repl;
	int ret;

	if (net->ipv6.ip6table_nat)
		return 0;

	repl = ip6t_alloc_initial_table(&nf_nat_ipv6_table);
	if (repl == NULL)
		return -ENOMEM;
	ret = ip6t_register_table(net, &nf_nat_ipv6_table, repl,
				  NULL, &net->ipv6.ip6table_nat);
	if (ret < 0) {
		kfree(repl);
		return ret;
	}

	ret = ip6t_nat_register_lookups(net);
	if (ret < 0) {
		ip6t_unregister_table(net, net->ipv6.ip6table_nat, NULL);
		net->ipv6.ip6table_nat = NULL;
	}
	kfree(repl);
	return ret;
}

static void __net_exit ip6table_nat_net_exit(struct net *net)
{
	if (!net->ipv6.ip6table_nat)
		return;
	ip6t_nat_unregister_lookups(net);
	ip6t_unregister_table(net, net->ipv6.ip6table_nat, NULL);
	net->ipv6.ip6table_nat = NULL;
}

static struct pernet_operations ip6table_nat_net_ops = {
	.exit	= ip6table_nat_net_exit,
};

static int __init ip6table_nat_init(void)
{
	int ret = register_pernet_subsys(&ip6table_nat_net_ops);

	if (ret)
		return ret;

	ret = ip6table_nat_table_init(&init_net);
	if (ret)
		unregister_pernet_subsys(&ip6table_nat_net_ops);
	return ret;
}

static void __exit ip6table_nat_exit(void)
{
	unregister_pernet_subsys(&ip6table_nat_net_ops);
}

module_init(ip6table_nat_init);
module_exit(ip6table_nat_exit);

MODULE_LICENSE("GPL");
