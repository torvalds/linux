/*
 * Copyright (c) 2016 Laura Garcia <nevola@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <linux/jhash.h>

struct nft_hash {
	enum nft_registers      sreg:8;
	enum nft_registers      dreg:8;
	u8			len;
	u32			modulus;
	u32			seed;
};

static void nft_hash_eval(const struct nft_expr *expr,
			  struct nft_regs *regs,
			  const struct nft_pktinfo *pkt)
{
	struct nft_hash *priv = nft_expr_priv(expr);
	const void *data = &regs->data[priv->sreg];

	regs->data[priv->dreg] =
		reciprocal_scale(jhash(data, priv->len, priv->seed),
				 priv->modulus);
}

static const struct nla_policy nft_hash_policy[NFTA_HASH_MAX + 1] = {
	[NFTA_HASH_SREG]	= { .type = NLA_U32 },
	[NFTA_HASH_DREG]	= { .type = NLA_U32 },
	[NFTA_HASH_LEN]		= { .type = NLA_U32 },
	[NFTA_HASH_MODULUS]	= { .type = NLA_U32 },
	[NFTA_HASH_SEED]	= { .type = NLA_U32 },
};

static int nft_hash_init(const struct nft_ctx *ctx,
			 const struct nft_expr *expr,
			 const struct nlattr * const tb[])
{
	struct nft_hash *priv = nft_expr_priv(expr);
	u32 len;

	if (!tb[NFTA_HASH_SREG] ||
	    !tb[NFTA_HASH_DREG] ||
	    !tb[NFTA_HASH_LEN]  ||
	    !tb[NFTA_HASH_SEED] ||
	    !tb[NFTA_HASH_MODULUS])
		return -EINVAL;

	priv->sreg = nft_parse_register(tb[NFTA_HASH_SREG]);
	priv->dreg = nft_parse_register(tb[NFTA_HASH_DREG]);

	len = ntohl(nla_get_be32(tb[NFTA_HASH_LEN]));
	if (len == 0 || len > U8_MAX)
		return -ERANGE;

	priv->len = len;

	priv->modulus = ntohl(nla_get_be32(tb[NFTA_HASH_MODULUS]));
	if (priv->modulus <= 1)
		return -ERANGE;

	priv->seed = ntohl(nla_get_be32(tb[NFTA_HASH_SEED]));

	return nft_validate_register_load(priv->sreg, len) &&
	       nft_validate_register_store(ctx, priv->dreg, NULL,
					   NFT_DATA_VALUE, sizeof(u32));
}

static int nft_hash_dump(struct sk_buff *skb,
			 const struct nft_expr *expr)
{
	const struct nft_hash *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_HASH_SREG, priv->sreg))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_HASH_DREG, priv->dreg))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_HASH_LEN, priv->len))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_HASH_MODULUS, priv->modulus))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_HASH_SEED, priv->seed))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_hash_type;
static const struct nft_expr_ops nft_hash_ops = {
	.type		= &nft_hash_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_hash)),
	.eval		= nft_hash_eval,
	.init		= nft_hash_init,
	.dump		= nft_hash_dump,
};

static struct nft_expr_type nft_hash_type __read_mostly = {
	.name		= "hash",
	.ops		= &nft_hash_ops,
	.policy		= nft_hash_policy,
	.maxattr	= NFTA_HASH_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_hash_module_init(void)
{
	return nft_register_expr(&nft_hash_type);
}

static void __exit nft_hash_module_exit(void)
{
	nft_unregister_expr(&nft_hash_type);
}

module_init(nft_hash_module_init);
module_exit(nft_hash_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Laura Garcia <nevola@gmail.com>");
MODULE_ALIAS_NFT_EXPR("hash");
