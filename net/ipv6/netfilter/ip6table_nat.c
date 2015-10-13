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

static const struct xt_table nf_nat_ipv6_table = {
	.name		= "nat",
	.valid_hooks	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.me		= THIS_MODULE,
	.af		= NFPROTO_IPV6,
};

static unsigned int ip6table_nat_do_chain(void *priv,
					  struct sk_buff *skb,
					  const struct nf_hook_state *state,
					  struct nf_conn *ct)
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

static struct nf_hook_ops nf_nat_ipv6_ops[] __read_mostly = {
	/* Before packet filtering, change destination */
	{
		.hook		= ip6table_nat_in,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= ip6table_nat_out,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
	/* Before packet filtering, change destination */
	{
		.hook		= ip6table_nat_local_fn,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= ip6table_nat_fn,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
};

static int __net_init ip6table_nat_net_init(struct net *net)
{
	struct ip6t_replace *repl;

	repl = ip6t_alloc_initial_table(&nf_nat_ipv6_table);
	if (repl == NULL)
		return -ENOMEM;
	net->ipv6.ip6table_nat = ip6t_register_table(net, &nf_nat_ipv6_table, repl);
	kfree(repl);
	return PTR_ERR_OR_ZERO(net->ipv6.ip6table_nat);
}

static void __net_exit ip6table_nat_net_exit(struct net *net)
{
	ip6t_unregister_table(net, net->ipv6.ip6table_nat);
}

static struct pernet_operations ip6table_nat_net_ops = {
	.init	= ip6table_nat_net_init,
	.exit	= ip6table_nat_net_exit,
};

static int __init ip6table_nat_init(void)
{
	int err;

	err = register_pernet_subsys(&ip6table_nat_net_ops);
	if (err < 0)
		goto err1;

	err = nf_register_hooks(nf_nat_ipv6_ops, ARRAY_SIZE(nf_nat_ipv6_ops));
	if (err < 0)
		goto err2;
	return 0;

err2:
	unregister_pernet_subsys(&ip6table_nat_net_ops);
err1:
	return err;
}

static void __exit ip6table_nat_exit(void)
{
	nf_unregister_hooks(nf_nat_ipv6_ops, ARRAY_SIZE(nf_nat_ipv6_ops));
	unregister_pernet_subsys(&ip6table_nat_net_ops);
}

module_init(ip6table_nat_init);
module_exit(ip6table_nat_exit);

MODULE_LICENSE("GPL");
