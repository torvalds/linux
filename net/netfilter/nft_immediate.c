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
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>

struct nft_immediate_expr {
	struct nft_data		data;
	enum nft_registers	dreg:8;
	u8			dlen;
};

static void nft_immediate_eval(const struct nft_expr *expr,
			       struct nft_regs *regs,
			       const struct nft_pktinfo *pkt)
{
	const struct nft_immediate_expr *priv = nft_expr_priv(expr);

	nft_data_copy(&regs->data[priv->dreg], &priv->data, priv->dlen);
}

static const struct nla_policy nft_immediate_policy[NFTA_IMMEDIATE_MAX + 1] = {
	[NFTA_IMMEDIATE_DREG]	= { .type = NLA_U32 },
	[NFTA_IMMEDIATE_DATA]	= { .type = NLA_NESTED },
};

static int nft_immediate_init(const struct nft_ctx *ctx,
			      const struct nft_expr *expr,
			      const struct nlattr * const tb[])
{
	struct nft_immediate_expr *priv = nft_expr_priv(expr);
	struct nft_data_desc desc;
	int err;

	if (tb[NFTA_IMMEDIATE_DREG] == NULL ||
	    tb[NFTA_IMMEDIATE_DATA] == NULL)
		return -EINVAL;

	err = nft_data_init(ctx, &priv->data, sizeof(priv->data), &desc,
			    tb[NFTA_IMMEDIATE_DATA]);
	if (err < 0)
		return err;

	if (desc.len > U8_MAX)
		return -ERANGE;

	priv->dlen = desc.len;

	priv->dreg = nft_parse_register(tb[NFTA_IMMEDIATE_DREG]);
	err = nft_validate_register_store(ctx, priv->dreg, &priv->data,
					  desc.type, desc.len);
	if (err < 0)
		goto err1;

	return 0;

err1:
	nft_data_uninit(&priv->data, desc.type);
	return err;
}

static void nft_immediate_destroy(const struct nft_ctx *ctx,
				  const struct nft_expr *expr)
{
	const struct nft_immediate_expr *priv = nft_expr_priv(expr);
	return nft_data_uninit(&priv->data, nft_dreg_to_type(priv->dreg));
}

static int nft_immediate_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_immediate_expr *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_IMMEDIATE_DREG, priv->dreg))
		goto nla_put_failure;

	return nft_data_dump(skb, NFTA_IMMEDIATE_DATA, &priv->data,
			     nft_dreg_to_type(priv->dreg), priv->dlen);

nla_put_failure:
	return -1;
}

static int nft_immediate_validate(const struct nft_ctx *ctx,
				  const struct nft_expr *expr,
				  const struct nft_data **data)
{
	const struct nft_immediate_expr *priv = nft_expr_priv(expr);

	if (priv->dreg == NFT_REG_VERDICT)
		*data = &priv->data;

	return 0;
}

static struct nft_expr_type nft_imm_type;
static const struct nft_expr_ops nft_imm_ops = {
	.type		= &nft_imm_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_immediate_expr)),
	.eval		= nft_immediate_eval,
	.init		= nft_immediate_init,
	.destroy	= nft_immediate_destroy,
	.dump		= nft_immediate_dump,
	.validate	= nft_immediate_validate,
};

static struct nft_expr_type nft_imm_type __read_mostly = {
	.name		= "immediate",
	.ops		= &nft_imm_ops,
	.policy		= nft_immediate_policy,
	.maxattr	= NFTA_IMMEDIATE_MAX,
	.owner		= THIS_MODULE,
};

int __init nft_immediate_module_init(void)
{
	return nft_register_expr(&nft_imm_type);
}

void nft_immediate_module_exit(void)
{
	nft_unregister_expr(&nft_imm_type);
}
