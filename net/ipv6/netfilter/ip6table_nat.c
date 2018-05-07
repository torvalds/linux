/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_l3proto.h>

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

static unsigned int ip6table_nat_fn(void *priv,
				    struct sk_buff *skb,
				    const struct nf_hook_state *state)
{
	return nf_nat_ipv6_fn(priv, skb, state, ip6table_nat_do_chain);
}

static unsigned int ip6table_nat_in(void *priv,
				    struct sk_buff *skb,
				    const struct nf_hook_state *state)
{
	return nf_nat_ipv6_in(priv, skb, state, ip6table_nat_do_chain);
}

static unsigned int ip6table_nat_out(void *priv,
				     struct sk_buff *skb,
				     const struct nf_hook_state *state)
{
	return nf_nat_ipv6_out(priv, skb, state, ip6table_nat_do_chain);
}

static unsigned int ip6table_nat_local_fn(void *priv,
					  struct sk_buff *skb,
					  const struct nf_hook_state *state)
{
	return nf_nat_ipv6_local_fn(priv, skb, state, ip6table_nat_do_chain);
}

static const struct nf_hook_ops nf_nat_ipv6_ops[] = {
	/* Before packet filtering, change destination */
	{
		.hook		= ip6table_nat_in,
		.pf		= NFPROTO_IPV6,
		.nat_hook	= true,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= ip6table_nat_out,
		.pf		= NFPROTO_IPV6,
		.nat_hook	= true,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
	/* Before packet filtering, change destination */
	{
		.hook		= ip6table_nat_local_fn,
		.pf		= NFPROTO_IPV6,
		.nat_hook	= true,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= ip6table_nat_fn,
		.nat_hook	= true,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
};

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
				  nf_nat_ipv6_ops, &net->ipv6.ip6table_nat);
	kfree(repl);
	return ret;
}

static void __net_exit ip6table_nat_net_exit(struct net *net)
{
	if (!net->ipv6.ip6table_nat)
		return;
	ip6t_unregister_table(net, net->ipv6.ip6table_nat, nf_nat_ipv6_ops);
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
