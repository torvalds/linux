// SPDX-License-Identifier: GPL-2.0-only
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2011 Patrick McHardy <kaber@trash.net>
 */

#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/ip.h>
#include <net/ip.h>

#include <net/netfilter/nf_nat.h>

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

static const struct nf_hook_ops nf_nat_ipv4_ops[] = {
	{
		.hook		= iptable_nat_do_chain,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_NAT_DST,
	},
	{
		.hook		= iptable_nat_do_chain,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_NAT_SRC,
	},
	{
		.hook		= iptable_nat_do_chain,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_NAT_DST,
	},
	{
		.hook		= iptable_nat_do_chain,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_NAT_SRC,
	},
};

static int ipt_nat_register_lookups(struct net *net)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(nf_nat_ipv4_ops); i++) {
		ret = nf_nat_ipv4_register_fn(net, &nf_nat_ipv4_ops[i]);
		if (ret) {
			while (i)
				nf_nat_ipv4_unregister_fn(net, &nf_nat_ipv4_ops[--i]);

			return ret;
		}
	}

	return 0;
}

static void ipt_nat_unregister_lookups(struct net *net)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nf_nat_ipv4_ops); i++)
		nf_nat_ipv4_unregister_fn(net, &nf_nat_ipv4_ops[i]);
}

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
				 NULL, &net->ipv4.nat_table);
	if (ret < 0) {
		kfree(repl);
		return ret;
	}

	ret = ipt_nat_register_lookups(net);
	if (ret < 0) {
		ipt_unregister_table(net, net->ipv4.nat_table, NULL);
		net->ipv4.nat_table = NULL;
	}

	kfree(repl);
	return ret;
}

static void __net_exit iptable_nat_net_pre_exit(struct net *net)
{
	if (net->ipv4.nat_table)
		ipt_nat_unregister_lookups(net);
}

static void __net_exit iptable_nat_net_exit(struct net *net)
{
	if (!net->ipv4.nat_table)
		return;
	ipt_unregister_table_exit(net, net->ipv4.nat_table);
	net->ipv4.nat_table = NULL;
}

static struct pernet_operations iptable_nat_net_ops = {
	.pre_exit = iptable_nat_net_pre_exit,
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
