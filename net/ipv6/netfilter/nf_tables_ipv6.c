/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2012-2013 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ipv6.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_ipv6.h>

static unsigned int nft_do_chain_ipv6(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);
	nft_set_pktinfo_ipv6(&pkt, skb);

	return nft_do_chain(&pkt, priv);
}

static const struct nf_chain_type filter_ipv6 = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_IPV6,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_INET_LOCAL_IN) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_FORWARD) |
			  (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING),
	.hooks		= {
		[NF_INET_LOCAL_IN]	= nft_do_chain_ipv6,
		[NF_INET_LOCAL_OUT]	= nft_do_chain_ipv6,
		[NF_INET_FORWARD]	= nft_do_chain_ipv6,
		[NF_INET_PRE_ROUTING]	= nft_do_chain_ipv6,
		[NF_INET_POST_ROUTING]	= nft_do_chain_ipv6,
	},
};

static int __init nf_tables_ipv6_init(void)
{
	return nft_register_chain_type(&filter_ipv6);
}

static void __exit nf_tables_ipv6_exit(void)
{
	nft_unregister_chain_type(&filter_ipv6);
}

module_init(nf_tables_ipv6_init);
module_exit(nf_tables_ipv6_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_CHAIN(AF_INET6, "filter");
