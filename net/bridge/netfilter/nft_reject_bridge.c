/*
 * Copyright (c) 2014 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_reject.h>

static void nft_reject_bridge_eval(const struct nft_expr *expr,
				 struct nft_data data[NFT_REG_MAX + 1],
				 const struct nft_pktinfo *pkt)
{
	switch (eth_hdr(pkt->skb)->h_proto) {
	case htons(ETH_P_IP):
		return nft_reject_ipv4_eval(expr, data, pkt);
	case htons(ETH_P_IPV6):
		return nft_reject_ipv6_eval(expr, data, pkt);
	default:
		/* No explicit way to reject this protocol, drop it. */
		data[NFT_REG_VERDICT].verdict = NF_DROP;
		break;
	}
}

static struct nft_expr_type nft_reject_bridge_type;
static const struct nft_expr_ops nft_reject_bridge_ops = {
	.type		= &nft_reject_bridge_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_reject)),
	.eval		= nft_reject_bridge_eval,
	.init		= nft_reject_init,
	.dump		= nft_reject_dump,
};

static struct nft_expr_type nft_reject_bridge_type __read_mostly = {
	.family		= NFPROTO_BRIDGE,
	.name		= "reject",
	.ops		= &nft_reject_bridge_ops,
	.policy		= nft_reject_policy,
	.maxattr	= NFTA_REJECT_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_reject_bridge_module_init(void)
{
	return nft_register_expr(&nft_reject_bridge_type);
}

static void __exit nft_reject_bridge_module_exit(void)
{
	nft_unregister_expr(&nft_reject_bridge_type);
}

module_init(nft_reject_bridge_module_init);
module_exit(nft_reject_bridge_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_AF_EXPR(AF_BRIDGE, "reject");
