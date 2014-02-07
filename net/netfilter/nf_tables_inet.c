/*
 * Copyright (c) 2012-2014 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/netfilter/nf_tables_ipv6.h>
#include <net/ip.h>

static void nft_inet_hook_ops_init(struct nf_hook_ops *ops, unsigned int n)
{
	struct nft_af_info *afi;

	if (n == 1)
		afi = &nft_af_ipv4;
	else
		afi = &nft_af_ipv6;

	ops->pf = afi->family;
	if (afi->hooks[ops->hooknum])
		ops->hook = afi->hooks[ops->hooknum];
}

static struct nft_af_info nft_af_inet __read_mostly = {
	.family		= NFPROTO_INET,
	.nhooks		= NF_INET_NUMHOOKS,
	.owner		= THIS_MODULE,
	.nops		= 2,
	.hook_ops_init	= nft_inet_hook_ops_init,
};

static int __net_init nf_tables_inet_init_net(struct net *net)
{
	net->nft.inet = kmalloc(sizeof(struct nft_af_info), GFP_KERNEL);
	if (net->nft.inet == NULL)
		return -ENOMEM;
	memcpy(net->nft.inet, &nft_af_inet, sizeof(nft_af_inet));

	if (nft_register_afinfo(net, net->nft.inet) < 0)
		goto err;

	return 0;

err:
	kfree(net->nft.inet);
	return -ENOMEM;
}

static void __net_exit nf_tables_inet_exit_net(struct net *net)
{
	nft_unregister_afinfo(net->nft.inet);
	kfree(net->nft.inet);
}

static struct pernet_operations nf_tables_inet_net_ops = {
	.init	= nf_tables_inet_init_net,
	.exit	= nf_tables_inet_exit_net,
};

static const struct nf_chain_type filter_inet = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_INET,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_INET_LOCAL_IN) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_FORWARD) |
			  (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING),
};

static int __init nf_tables_inet_init(void)
{
	int ret;

	nft_register_chain_type(&filter_inet);
	ret = register_pernet_subsys(&nf_tables_inet_net_ops);
	if (ret < 0)
		nft_unregister_chain_type(&filter_inet);

	return ret;
}

static void __exit nf_tables_inet_exit(void)
{
	unregister_pernet_subsys(&nf_tables_inet_net_ops);
	nft_unregister_chain_type(&filter_inet);
}

module_init(nf_tables_inet_init);
module_exit(nf_tables_inet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_FAMILY(1);
