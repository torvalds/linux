/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_ipv6.h>
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/ipv6.h>

static unsigned int nft_nat_do_chain(void *priv,
				     struct sk_buff *skb,
				     const struct nf_hook_state *state,
				     struct nf_conn *ct)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);
	nft_set_pktinfo_ipv6(&pkt, skb);

	return nft_do_chain(&pkt, priv);
}

static unsigned int nft_nat_ipv6_fn(void *priv,
				    struct sk_buff *skb,
				    const struct nf_hook_state *state)
{
	return nf_nat_ipv6_fn(priv, skb, state, nft_nat_do_chain);
}

static unsigned int nft_nat_ipv6_in(void *priv,
				    struct sk_buff *skb,
				    const struct nf_hook_state *state)
{
	return nf_nat_ipv6_in(priv, skb, state, nft_nat_do_chain);
}

static unsigned int nft_nat_ipv6_out(void *priv,
				     struct sk_buff *skb,
				     const struct nf_hook_state *state)
{
	return nf_nat_ipv6_out(priv, skb, state, nft_nat_do_chain);
}

static unsigned int nft_nat_ipv6_local_fn(void *priv,
					  struct sk_buff *skb,
					  const struct nf_hook_state *state)
{
	return nf_nat_ipv6_local_fn(priv, skb, state, nft_nat_do_chain);
}

static const struct nft_chain_type nft_chain_nat_ipv6 = {
	.name		= "nat",
	.type		= NFT_CHAIN_T_NAT,
	.family		= NFPROTO_IPV6,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.hooks		= {
		[NF_INET_PRE_ROUTING]	= nft_nat_ipv6_in,
		[NF_INET_POST_ROUTING]	= nft_nat_ipv6_out,
		[NF_INET_LOCAL_OUT]	= nft_nat_ipv6_local_fn,
		[NF_INET_LOCAL_IN]	= nft_nat_ipv6_fn,
	},
};

static int __init nft_chain_nat_ipv6_init(void)
{
	nft_register_chain_type(&nft_chain_nat_ipv6);

	return 0;
}

static void __exit nft_chain_nat_ipv6_exit(void)
{
	nft_unregister_chain_type(&nft_chain_nat_ipv6);
}

module_init(nft_chain_nat_ipv6_init);
module_exit(nft_chain_nat_ipv6_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tomasz Bursztyka <tomasz.bursztyka@linux.intel.com>");
MODULE_ALIAS_NFT_CHAIN(AF_INET6, "nat");
