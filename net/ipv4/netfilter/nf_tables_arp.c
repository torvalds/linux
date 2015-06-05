/*
 * Copyright (c) 2008-2010 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2013 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netfilter_arp.h>
#include <net/netfilter/nf_tables.h>

static unsigned int
nft_do_chain_arp(const struct nf_hook_ops *ops,
		  struct sk_buff *skb,
		  const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, ops, skb, state);

	return nft_do_chain(&pkt, ops);
}

static struct nft_af_info nft_af_arp __read_mostly = {
	.family		= NFPROTO_ARP,
	.nhooks		= NF_ARP_NUMHOOKS,
	.owner		= THIS_MODULE,
	.nops		= 1,
	.hooks		= {
		[NF_ARP_IN]		= nft_do_chain_arp,
		[NF_ARP_OUT]		= nft_do_chain_arp,
		[NF_ARP_FORWARD]	= nft_do_chain_arp,
	},
};

static int nf_tables_arp_init_net(struct net *net)
{
	net->nft.arp = kmalloc(sizeof(struct nft_af_info), GFP_KERNEL);
	if (net->nft.arp== NULL)
		return -ENOMEM;

	memcpy(net->nft.arp, &nft_af_arp, sizeof(nft_af_arp));

	if (nft_register_afinfo(net, net->nft.arp) < 0)
		goto err;

	return 0;
err:
	kfree(net->nft.arp);
	return -ENOMEM;
}

static void nf_tables_arp_exit_net(struct net *net)
{
	nft_unregister_afinfo(net->nft.arp);
	kfree(net->nft.arp);
}

static struct pernet_operations nf_tables_arp_net_ops = {
	.init   = nf_tables_arp_init_net,
	.exit   = nf_tables_arp_exit_net,
};

static const struct nf_chain_type filter_arp = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_ARP,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_ARP_IN) |
			  (1 << NF_ARP_OUT) |
			  (1 << NF_ARP_FORWARD),
};

static int __init nf_tables_arp_init(void)
{
	int ret;

	nft_register_chain_type(&filter_arp);
	ret = register_pernet_subsys(&nf_tables_arp_net_ops);
	if (ret < 0)
		nft_unregister_chain_type(&filter_arp);

	return ret;
}

static void __exit nf_tables_arp_exit(void)
{
	unregister_pernet_subsys(&nf_tables_arp_net_ops);
	nft_unregister_chain_type(&filter_arp);
}

module_init(nf_tables_arp_init);
module_exit(nf_tables_arp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_FAMILY(3); /* NFPROTO_ARP */
