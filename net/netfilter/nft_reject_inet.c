/*
 * Copyright (c) 2014 Patrick McHardy <kaber@trash.net>
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

static void nft_reject_inet_eval(const struct nft_expr *expr,
				 struct nft_data data[NFT_REG_MAX + 1],
				 const struct nft_pktinfo *pkt)
{
	switch (pkt->ops->pf) {
	case NFPROTO_IPV4:
		return nft_reject_ipv4_eval(expr, data, pkt);
	case NFPROTO_IPV6:
		return nft_reject_ipv6_eval(expr, data, pkt);
	}
}

static struct nft_expr_type nft_reject_inet_type;
static const struct nft_expr_ops nft_reject_inet_ops = {
	.type		= &nft_reject_inet_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_reject)),
	.eval		= nft_reject_inet_eval,
	.init		= nft_reject_init,
	.dump		= nft_reject_dump,
};

static struct nft_expr_type nft_reject_inet_type __read_mostly = {
	.family		= NFPROTO_INET,
	.name		= "reject",
	.ops		= &nft_reject_inet_ops,
	.policy		= nft_reject_policy,
	.maxattr	= NFTA_REJECT_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_reject_inet_module_init(void)
{
	return nft_register_expr(&nft_reject_inet_type);
}

static void __exit nft_reject_inet_module_exit(void)
{
	nft_unregister_expr(&nft_reject_inet_type);
}

module_init(nft_reject_inet_module_init);
module_exit(nft_reject_inet_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_AF_EXPR(1, "reject");
