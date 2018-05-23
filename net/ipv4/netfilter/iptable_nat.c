/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/ip.h>
#include <net/ip.h>

#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_l3proto.h>

static int __net_init iptable_nat_table_init(struct net *net);

static const struct xt_table nf_nat_ipv4_table = {
	.name		= "nat",
	.valid_hooks	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.me		= THIS_MODULE,
	.af		= NFPROTO_IPV4,
	.table_init	= iptable_nat_table_init,
};

static unsigned int iptable_nat_do_chain(void *priv,
					 struct sk_buff *skb,
					 const struct nf_hook_state *state)
{
	return ipt_do_table(skb, state, state->net->ipv4.nat_table);
}

static unsigned int iptable_nat_ipv4_fn(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	return nf_nat_ipv4_fn(priv, skb, state, iptable_nat_do_chain);
}

static unsigned int iptable_nat_ipv4_in(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	return nf_nat_ipv4_in(priv, skb, state, iptable_nat_do_chain);
}

static unsigned int iptable_nat_ipv4_out(void *priv,
					 struct sk_buff *skb,
					 const struct nf_hook_state *state)
{
	return nf_nat_ipv4_out(priv, skb, state, iptable_nat_do_chain);
}

static unsigned int iptable_nat_ipv4_local_fn(void *priv,
					      struct sk_buff *skb,
					      const struct nf_hook_state *state)
{
	return nf_nat_ipv4_local_fn(priv, skb, state, iptable_nat_do_chain);
}

static const struct nf_hook_ops nf_nat_ipv4_ops[] = {
	/* Before packet filtering, change destination */
	{
		.hook		= iptable_nat_ipv4_in,
		.pf		= NFPROTO_IPV4,
		.nat_hook	= true,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= iptable_nat_ipv4_out,
		.pf		= NFPROTO_IPV4,
		.nat_hook	= true,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_NAT_SRC,
	},
	/* Before packet filtering, change destination */
	{
		.hook		= iptable_nat_ipv4_local_fn,
		.pf		= NFPROTO_IPV4,
		.nat_hook	= true,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= iptable_nat_ipv4_fn,
		.pf		= NFPROTO_IPV4,
		.nat_hook	= true,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_NAT_SRC,
	},
};

static int __net_init iptable_nat_table_init(struct net *net)
{
	struct ipt_replace *repl;
	int ret;

	if (net->ipv4.nat_table)
		return 0;

	repl = ipt_alloc_initial_table(&nf_nat_ipv4_table);
	if (repl == NULL)
		return -ENOMEM;
	ret = ipt_register_table(net, &nf_nat_ipv4_table, repl,
				 nf_nat_ipv4_ops, &net->ipv4.nat_table);
	kfree(repl);
	return ret;
}

static void __net_exit iptable_nat_net_exit(struct net *net)
{
	if (!net->ipv4.nat_table)
		return;
	ipt_unregister_table(net, net->ipv4.nat_table, nf_nat_ipv4_ops);
	net->ipv4.nat_table = NULL;
}

static struct pernet_operations iptable_nat_net_ops = {
	.exit	= iptable_nat_net_exit,
};

static int __init iptable_nat_init(void)
{
	int ret = register_pernet_subsys(&iptable_nat_net_ops);

	if (ret)
		return ret;

	ret = iptable_nat_table_init(&init_net);
	if (ret)
		unregister_pernet_subsys(&iptable_nat_net_ops);
	return ret;
}

static void __exit iptable_nat_exit(void)
{
	unregister_pernet_subsys(&iptable_nat_net_ops);
}

module_init(iptable_nat_init);
module_exit(iptable_nat_exit);

MODULE_LICENSE("GPL");
