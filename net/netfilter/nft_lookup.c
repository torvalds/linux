/*
 * Copyright (c) 2009 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>

struct nft_lookup {
	struct nft_set			*set;
	enum nft_registers		sreg:8;
	enum nft_registers		dreg:8;
	bool				invert;
	struct nft_set_binding		binding;
};

static void nft_lookup_eval(const struct nft_expr *expr,
			    struct nft_regs *regs,
			    const struct nft_pktinfo *pkt)
{
	const struct nft_lookup *priv = nft_expr_priv(expr);
	const struct nft_set *set = priv->set;
	const struct nft_set_ext *ext;
	bool found;

	found = set->ops->lookup(pkt->net, set, &regs->data[priv->sreg], &ext) ^
		priv->invert;

	if (!found) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	if (set->flags & NFT_SET_MAP)
		nft_data_copy(&regs->data[priv->dreg],
			      nft_set_ext_data(ext), set->dlen);

}

static const struct nla_policy nft_lookup_policy[NFTA_LOOKUP_MAX + 1] = {
	[NFTA_LOOKUP_SET]	= { .type = NLA_STRING },
	[NFTA_LOOKUP_SET_ID]	= { .type = NLA_U32 },
	[NFTA_LOOKUP_SREG]	= { .type = NLA_U32 },
	[NFTA_LOOKUP_DREG]	= { .type = NLA_U32 },
	[NFTA_LOOKUP_FLAGS]	= { .type = NLA_U32 },
};

static int nft_lookup_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_lookup *priv = nft_expr_priv(expr);
	u8 genmask = nft_genmask_next(ctx->net);
	struct nft_set *set;
	u32 flags;
	int err;

	if (tb[NFTA_LOOKUP_SET] == NULL ||
	    tb[NFTA_LOOKUP_SREG] == NULL)
		return -EINVAL;

	set = nf_tables_set_lookup(ctx->table, tb[NFTA_LOOKUP_SET], genmask);
	if (IS_ERR(set)) {
		if (tb[NFTA_LOOKUP_SET_ID]) {
			set = nf_tables_set_lookup_byid(ctx->net,
							tb[NFTA_LOOKUP_SET_ID],
							genmask);
		}
		if (IS_ERR(set))
			return PTR_ERR(set);
	}

	if (set->flags & NFT_SET_EVAL)
		return -EOPNOTSUPP;

	priv->sreg = nft_parse_register(tb[NFTA_LOOKUP_SREG]);
	err = nft_validate_register_load(priv->sreg, set->klen);
	if (err < 0)
		return err;

	if (tb[NFTA_LOOKUP_FLAGS]) {
		flags = ntohl(nla_get_be32(tb[NFTA_LOOKUP_FLAGS]));

		if (flags & ~NFT_LOOKUP_F_INV)
			return -EINVAL;

		if (flags & NFT_LOOKUP_F_INV) {
			if (set->flags & NFT_SET_MAP)
				return -EINVAL;
			priv->invert = true;
		}
	}

	if (tb[NFTA_LOOKUP_DREG] != NULL) {
		if (priv->invert)
			return -EINVAL;
		if (!(set->flags & NFT_SET_MAP))
			return -EINVAL;

		priv->dreg = nft_parse_register(tb[NFTA_LOOKUP_DREG]);
		err = nft_validate_register_store(ctx, priv->dreg, NULL,
						  set->dtype, set->dlen);
		if (err < 0)
			return err;
	} else if (set->flags & NFT_SET_MAP)
		return -EINVAL;

	priv->binding.flags = set->flags & NFT_SET_MAP;

	err = nf_tables_bind_set(ctx, set, &priv->binding);
	if (err < 0)
		return err;

	priv->set = set;
	return 0;
}

static void nft_lookup_destroy(const struct nft_ctx *ctx,
			       const struct nft_expr *expr)
{
	struct nft_lookup *priv = nft_expr_priv(expr);

	nf_tables_unbind_set(ctx, priv->set, &priv->binding);
}

static int nft_lookup_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_lookup *priv = nft_expr_priv(expr);
	u32 flags = priv->invert ? NFT_LOOKUP_F_INV : 0;

	if (nla_put_string(skb, NFTA_LOOKUP_SET, priv->set->name))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_LOOKUP_SREG, priv->sreg))
		goto nla_put_failure;
	if (priv->set->flags & NFT_SET_MAP)
		if (nft_dump_register(skb, NFTA_LOOKUP_DREG, priv->dreg))
			goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_LOOKUP_FLAGS, htonl(flags)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_lookup_type;
static const struct nft_expr_ops nft_lookup_ops = {
	.type		= &nft_lookup_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_lookup)),
	.eval		= nft_lookup_eval,
	.init		= nft_lookup_init,
	.destroy	= nft_lookup_destroy,
	.dump		= nft_lookup_dump,
};

static struct nft_expr_type nft_lookup_type __read_mostly = {
	.name		= "lookup",
	.ops		= &nft_lookup_ops,
	.policy		= nft_lookup_policy,
	.maxattr	= NFTA_LOOKUP_MAX,
	.owner		= THIS_MODULE,
};

int __init nft_lookup_module_init(void)
{
	return nft_register_expr(&nft_lookup_type);
}

void nft_lookup_module_exit(void)
{
	nft_unregister_expr(&nft_lookup_type);
}
