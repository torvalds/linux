/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2013 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter_bridge.h>
#include <net/netfilter/nf_tables.h>

static unsigned int
nft_do_chain_bridge(const struct nf_hook_ops *ops,
		    struct sk_buff *skb,
		    const struct net_device *in,
		    const struct net_device *out,
		    int (*okfn)(struct sk_buff *))
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, ops, skb, in, out);

	return nft_do_chain(&pkt, ops);
}

static struct nft_af_info nft_af_bridge __read_mostly = {
	.family		= NFPROTO_BRIDGE,
	.nhooks		= NF_BR_NUMHOOKS,
	.owner		= THIS_MODULE,
	.nops		= 1,
	.hooks		= {
		[NF_BR_PRE_ROUTING]	= nft_do_chain_bridge,
		[NF_BR_LOCAL_IN]	= nft_do_chain_bridge,
		[NF_BR_FORWARD]		= nft_do_chain_bridge,
		[NF_BR_LOCAL_OUT]	= nft_do_chain_bridge,
		[NF_BR_POST_ROUTING]	= nft_do_chain_bridge,
	},
};

static int nf_tables_bridge_init_net(struct net *net)
{
	net->nft.bridge = kmalloc(sizeof(struct nft_af_info), GFP_KERNEL);
	if (net->nft.bridge == NULL)
		return -ENOMEM;

	memcpy(net->nft.bridge, &nft_af_bridge, sizeof(nft_af_bridge));

	if (nft_register_afinfo(net, net->nft.bridge) < 0)
		goto err;

	return 0;
err:
	kfree(net->nft.bridge);
	return -ENOMEM;
}

static void nf_tables_bridge_exit_net(struct net *net)
{
	nft_unregister_afinfo(net->nft.bridge);
	kfree(net->nft.bridge);
}

static struct pernet_operations nf_tables_bridge_net_ops = {
	.init	= nf_tables_bridge_init_net,
	.exit	= nf_tables_bridge_exit_net,
};

static const struct nf_chain_type filter_bridge = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_BRIDGE,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_BR_LOCAL_IN) |
			  (1 << NF_BR_FORWARD) |
			  (1 << NF_BR_LOCAL_OUT),
};

static int __init nf_tables_bridge_init(void)
{
	int ret;

	nft_register_chain_type(&filter_bridge);
	ret = register_pernet_subsys(&nf_tables_bridge_net_ops);
	if (ret < 0)
		nft_unregister_chain_type(&filter_bridge);

	return ret;
}

static void __exit nf_tables_bridge_exit(void)
{
	unregister_pernet_subsys(&nf_tables_bridge_net_ops);
	nft_unregister_chain_type(&filter_bridge);
}

module_init(nf_tables_bridge_init);
module_exit(nf_tables_bridge_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_FAMILY(AF_BRIDGE);
