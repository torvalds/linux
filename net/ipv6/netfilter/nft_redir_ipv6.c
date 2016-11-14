/*
 * Copyright (c) 2014 Arturo Borrero Gonzalez <arturo.borrero.glez@gmail.com>
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
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nft_redir.h>
#include <net/netfilter/nf_nat_redirect.h>

static void nft_redir_ipv6_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	struct nft_redir *priv = nft_expr_priv(expr);
	struct nf_nat_range range;

	memset(&range, 0, sizeof(range));
	if (priv->sreg_proto_min) {
		range.min_proto.all =
			*(__be16 *)&regs->data[priv->sreg_proto_min],
		range.max_proto.all =
			*(__be16 *)&regs->data[priv->sreg_proto_max],
		range.flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
	}

	range.flags |= priv->flags;

	regs->verdict.code =
		nf_nat_redirect_ipv6(pkt->skb, &range, nft_hook(pkt));
}

static struct nft_expr_type nft_redir_ipv6_type;
static const struct nft_expr_ops nft_redir_ipv6_ops = {
	.type		= &nft_redir_ipv6_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_redir)),
	.eval		= nft_redir_ipv6_eval,
	.init		= nft_redir_init,
	.dump		= nft_redir_dump,
	.validate	= nft_redir_validate,
};

static struct nft_expr_type nft_redir_ipv6_type __read_mostly = {
	.family		= NFPROTO_IPV6,
	.name		= "redir",
	.ops		= &nft_redir_ipv6_ops,
	.policy		= nft_redir_policy,
	.maxattr	= NFTA_REDIR_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_redir_ipv6_module_init(void)
{
	return nft_register_expr(&nft_redir_ipv6_type);
}

static void __exit nft_redir_ipv6_module_exit(void)
{
	nft_unregister_expr(&nft_redir_ipv6_type);
}

module_init(nft_redir_ipv6_module_init);
module_exit(nft_redir_ipv6_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arturo Borrero Gonzalez <arturo.borrero.glez@gmail.com>");
MODULE_ALIAS_NFT_AF_EXPR(AF_INET6, "redir");
