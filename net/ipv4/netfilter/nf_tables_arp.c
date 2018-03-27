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
nft_do_chain_arp(void *priv,
		  struct sk_buff *skb,
		  const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);
	nft_set_pktinfo_unspec(&pkt, skb);

	return nft_do_chain(&pkt, priv);
}

static const struct nft_chain_type filter_arp = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_ARP,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_ARP_IN) |
			  (1 << NF_ARP_OUT),
	.hooks		= {
		[NF_ARP_IN]		= nft_do_chain_arp,
		[NF_ARP_OUT]		= nft_do_chain_arp,
	},
};

static int __init nf_tables_arp_init(void)
{
	return nft_register_chain_type(&filter_arp);
}

static void __exit nf_tables_arp_exit(void)
{
	nft_unregister_chain_type(&filter_arp);
}

module_init(nf_tables_arp_init);
module_exit(nf_tables_arp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_CHAIN(3, "filter"); /* NFPROTO_ARP */
