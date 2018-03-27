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
#include <linux/ipv6.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/netfilter/nf_tables_ipv6.h>
#include <net/ip.h>

static unsigned int nft_do_chain_inet(void *priv, struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);

	switch (state->pf) {
	case NFPROTO_IPV4:
		nft_set_pktinfo_ipv4(&pkt, skb);
		break;
	case NFPROTO_IPV6:
		nft_set_pktinfo_ipv6(&pkt, skb);
		break;
	default:
		break;
	}

	return nft_do_chain(&pkt, priv);
}

static const struct nft_chain_type filter_inet = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_INET,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_INET_LOCAL_IN) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_FORWARD) |
			  (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING),
	.hooks		= {
		[NF_INET_LOCAL_IN]	= nft_do_chain_inet,
		[NF_INET_LOCAL_OUT]	= nft_do_chain_inet,
		[NF_INET_FORWARD]	= nft_do_chain_inet,
		[NF_INET_PRE_ROUTING]	= nft_do_chain_inet,
		[NF_INET_POST_ROUTING]	= nft_do_chain_inet,
        },
};

static int __init nf_tables_inet_init(void)
{
	return nft_register_chain_type(&filter_inet);
}

static void __exit nf_tables_inet_exit(void)
{
	nft_unregister_chain_type(&filter_inet);
}

module_init(nf_tables_inet_init);
module_exit(nf_tables_inet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_CHAIN(1, "filter");
