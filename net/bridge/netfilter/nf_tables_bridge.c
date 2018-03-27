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
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/netfilter/nf_tables_ipv6.h>

static unsigned int
nft_do_chain_bridge(void *priv,
		    struct sk_buff *skb,
		    const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);

	switch (eth_hdr(skb)->h_proto) {
	case htons(ETH_P_IP):
		nft_set_pktinfo_ipv4_validate(&pkt, skb);
		break;
	case htons(ETH_P_IPV6):
		nft_set_pktinfo_ipv6_validate(&pkt, skb);
		break;
	default:
		nft_set_pktinfo_unspec(&pkt, skb);
		break;
	}

	return nft_do_chain(&pkt, priv);
}

static const struct nft_chain_type filter_bridge = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_BRIDGE,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_BR_PRE_ROUTING) |
			  (1 << NF_BR_LOCAL_IN) |
			  (1 << NF_BR_FORWARD) |
			  (1 << NF_BR_LOCAL_OUT) |
			  (1 << NF_BR_POST_ROUTING),
	.hooks		= {
		[NF_BR_PRE_ROUTING]	= nft_do_chain_bridge,
		[NF_BR_LOCAL_IN]	= nft_do_chain_bridge,
		[NF_BR_FORWARD]		= nft_do_chain_bridge,
		[NF_BR_LOCAL_OUT]	= nft_do_chain_bridge,
		[NF_BR_POST_ROUTING]	= nft_do_chain_bridge,
	},
};

static int __init nf_tables_bridge_init(void)
{
	nft_register_chain_type(&filter_bridge);

	return 0;
}

static void __exit nf_tables_bridge_exit(void)
{
	nft_unregister_chain_type(&filter_bridge);
}

module_init(nf_tables_bridge_init);
module_exit(nf_tables_bridge_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_CHAIN(AF_BRIDGE, "filter");
