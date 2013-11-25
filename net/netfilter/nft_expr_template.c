/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>

struct nft_template {

};

static void nft_template_eval(const struct nft_expr *expr,
			      struct nft_data data[NFT_REG_MAX + 1],
			      const struct nft_pktinfo *pkt)
{
	struct nft_template *priv = nft_expr_priv(expr);

}

static const struct nla_policy nft_template_policy[NFTA_TEMPLATE_MAX + 1] = {
	[NFTA_TEMPLATE_ATTR]		= { .type = NLA_U32 },
};

static int nft_template_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_template *priv = nft_expr_priv(expr);

	return 0;
}

static void nft_template_destroy(const struct nft_ctx *ctx,
			       const struct nft_expr *expr)
{
	struct nft_template *priv = nft_expr_priv(expr);

}

static int nft_template_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_template *priv = nft_expr_priv(expr);

	NLA_PUT_BE32(skb, NFTA_TEMPLATE_ATTR, priv->field);
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_template_type;
static const struct nft_expr_ops nft_template_ops = {
	.type		= &nft_template_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_template)),
	.eval		= nft_template_eval,
	.init		= nft_template_init,
	.destroy	= nft_template_destroy,
	.dump		= nft_template_dump,
};

static struct nft_expr_type nft_template_type __read_mostly = {
	.name		= "template",
	.ops		= &nft_template_ops,
	.policy		= nft_template_policy,
	.maxattr	= NFTA_TEMPLATE_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_template_module_init(void)
{
	return nft_register_expr(&nft_template_type);
}

static void __exit nft_template_module_exit(void)
{
	nft_unregister_expr(&nft_template_type);
}

module_init(nft_template_module_init);
module_exit(nft_template_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_EXPR("template");
