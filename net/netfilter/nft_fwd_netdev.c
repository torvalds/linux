/*
 * Copyright (c) 2015 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_dup_netdev.h>

struct nft_fwd_netdev {
	enum nft_registers	sreg_dev:8;
};

static void nft_fwd_netdev_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	struct nft_fwd_netdev *priv = nft_expr_priv(expr);
	int oif = regs->data[priv->sreg_dev];

	nf_fwd_netdev_egress(pkt, oif);
	regs->verdict.code = NF_STOLEN;
}

static const struct nla_policy nft_fwd_netdev_policy[NFTA_FWD_MAX + 1] = {
	[NFTA_FWD_SREG_DEV]	= { .type = NLA_U32 },
};

static int nft_fwd_netdev_init(const struct nft_ctx *ctx,
			       const struct nft_expr *expr,
			       const struct nlattr * const tb[])
{
	struct nft_fwd_netdev *priv = nft_expr_priv(expr);

	if (tb[NFTA_FWD_SREG_DEV] == NULL)
		return -EINVAL;

	priv->sreg_dev = nft_parse_register(tb[NFTA_FWD_SREG_DEV]);
	return nft_validate_register_load(priv->sreg_dev, sizeof(int));
}

static const struct nft_expr_ops nft_fwd_netdev_ingress_ops;

static int nft_fwd_netdev_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	struct nft_fwd_netdev *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_FWD_SREG_DEV, priv->sreg_dev))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_fwd_netdev_type;
static const struct nft_expr_ops nft_fwd_netdev_ops = {
	.type		= &nft_fwd_netdev_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_fwd_netdev)),
	.eval		= nft_fwd_netdev_eval,
	.init		= nft_fwd_netdev_init,
	.dump		= nft_fwd_netdev_dump,
};

static struct nft_expr_type nft_fwd_netdev_type __read_mostly = {
	.family		= NFPROTO_NETDEV,
	.name		= "fwd",
	.ops		= &nft_fwd_netdev_ops,
	.policy		= nft_fwd_netdev_policy,
	.maxattr	= NFTA_FWD_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_fwd_netdev_module_init(void)
{
	return nft_register_expr(&nft_fwd_netdev_type);
}

static void __exit nft_fwd_netdev_module_exit(void)
{
	nft_unregister_expr(&nft_fwd_netdev_type);
}

module_init(nft_fwd_netdev_module_init);
module_exit(nft_fwd_netdev_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_AF_EXPR(5, "fwd");
