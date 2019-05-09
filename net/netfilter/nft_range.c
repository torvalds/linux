/*
 * Copyright (c) 2016 Pablo Neira Ayuso <pablo@netfilter.org>
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
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>

struct nft_range_expr {
	struct nft_data		data_from;
	struct nft_data		data_to;
	enum nft_registers	sreg:8;
	u8			len;
	enum nft_range_ops	op:8;
};

void nft_range_eval(const struct nft_expr *expr,
		    struct nft_regs *regs, const struct nft_pktinfo *pkt)
{
	const struct nft_range_expr *priv = nft_expr_priv(expr);
	int d1, d2;

	d1 = memcmp(&regs->data[priv->sreg], &priv->data_from, priv->len);
	d2 = memcmp(&regs->data[priv->sreg], &priv->data_to, priv->len);
	switch (priv->op) {
	case NFT_RANGE_EQ:
		if (d1 < 0 || d2 > 0)
			regs->verdict.code = NFT_BREAK;
		break;
	case NFT_RANGE_NEQ:
		if (d1 >= 0 && d2 <= 0)
			regs->verdict.code = NFT_BREAK;
		break;
	}
}

static const struct nla_policy nft_range_policy[NFTA_RANGE_MAX + 1] = {
	[NFTA_RANGE_SREG]		= { .type = NLA_U32 },
	[NFTA_RANGE_OP]			= { .type = NLA_U32 },
	[NFTA_RANGE_FROM_DATA]		= { .type = NLA_NESTED },
	[NFTA_RANGE_TO_DATA]		= { .type = NLA_NESTED },
};

static int nft_range_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
			const struct nlattr * const tb[])
{
	struct nft_range_expr *priv = nft_expr_priv(expr);
	struct nft_data_desc desc_from, desc_to;
	int err;
	u32 op;

	if (!tb[NFTA_RANGE_SREG]      ||
	    !tb[NFTA_RANGE_OP]	      ||
	    !tb[NFTA_RANGE_FROM_DATA] ||
	    !tb[NFTA_RANGE_TO_DATA])
		return -EINVAL;

	err = nft_data_init(NULL, &priv->data_from, sizeof(priv->data_from),
			    &desc_from, tb[NFTA_RANGE_FROM_DATA]);
	if (err < 0)
		return err;

	err = nft_data_init(NULL, &priv->data_to, sizeof(priv->data_to),
			    &desc_to, tb[NFTA_RANGE_TO_DATA]);
	if (err < 0)
		goto err1;

	if (desc_from.len != desc_to.len) {
		err = -EINVAL;
		goto err2;
	}

	priv->sreg = nft_parse_register(tb[NFTA_RANGE_SREG]);
	err = nft_validate_register_load(priv->sreg, desc_from.len);
	if (err < 0)
		goto err2;

	err = nft_parse_u32_check(tb[NFTA_RANGE_OP], U8_MAX, &op);
	if (err < 0)
		goto err2;

	switch (op) {
	case NFT_RANGE_EQ:
	case NFT_RANGE_NEQ:
		break;
	default:
		err = -EINVAL;
		goto err2;
	}

	priv->op  = op;
	priv->len = desc_from.len;
	return 0;
err2:
	nft_data_release(&priv->data_to, desc_to.type);
err1:
	nft_data_release(&priv->data_from, desc_from.type);
	return err;
}

static int nft_range_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_range_expr *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_RANGE_SREG, priv->sreg))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_RANGE_OP, htonl(priv->op)))
		goto nla_put_failure;

	if (nft_data_dump(skb, NFTA_RANGE_FROM_DATA, &priv->data_from,
			  NFT_DATA_VALUE, priv->len) < 0 ||
	    nft_data_dump(skb, NFTA_RANGE_TO_DATA, &priv->data_to,
			  NFT_DATA_VALUE, priv->len) < 0)
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static const struct nft_expr_ops nft_range_ops = {
	.type		= &nft_range_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_range_expr)),
	.eval		= nft_range_eval,
	.init		= nft_range_init,
	.dump		= nft_range_dump,
};

struct nft_expr_type nft_range_type __read_mostly = {
	.name		= "range",
	.ops		= &nft_range_ops,
	.policy		= nft_range_policy,
	.maxattr	= NFTA_RANGE_MAX,
	.owner		= THIS_MODULE,
};
