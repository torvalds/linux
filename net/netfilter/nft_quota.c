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
#include <linux/atomic.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>

struct nft_quota {
	u64		quota;
	bool		invert;
	atomic64_t	remain;
};

static inline bool nft_overquota(struct nft_quota *priv,
				 const struct nft_pktinfo *pkt)
{
	return atomic64_sub_return(pkt->skb->len, &priv->remain) < 0;
}

static void nft_quota_eval(const struct nft_expr *expr,
			   struct nft_regs *regs,
			   const struct nft_pktinfo *pkt)
{
	struct nft_quota *priv = nft_expr_priv(expr);

	if (nft_overquota(priv, pkt) ^ priv->invert)
		regs->verdict.code = NFT_BREAK;
}

static const struct nla_policy nft_quota_policy[NFTA_QUOTA_MAX + 1] = {
	[NFTA_QUOTA_BYTES]	= { .type = NLA_U64 },
	[NFTA_QUOTA_FLAGS]	= { .type = NLA_U32 },
};

static int nft_quota_init(const struct nft_ctx *ctx,
			  const struct nft_expr *expr,
			  const struct nlattr * const tb[])
{
	struct nft_quota *priv = nft_expr_priv(expr);
	u32 flags = 0;
	u64 quota;

	if (!tb[NFTA_QUOTA_BYTES])
		return -EINVAL;

	quota = be64_to_cpu(nla_get_be64(tb[NFTA_QUOTA_BYTES]));
	if (quota > S64_MAX)
		return -EOVERFLOW;

	if (tb[NFTA_QUOTA_FLAGS]) {
		flags = ntohl(nla_get_be32(tb[NFTA_QUOTA_FLAGS]));
		if (flags & ~NFT_QUOTA_F_INV)
			return -EINVAL;
	}

	priv->quota = quota;
	priv->invert = (flags & NFT_QUOTA_F_INV) ? true : false;
	atomic64_set(&priv->remain, quota);

	return 0;
}

static int nft_quota_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_quota *priv = nft_expr_priv(expr);
	u32 flags = priv->invert ? NFT_QUOTA_F_INV : 0;

	if (nla_put_be64(skb, NFTA_QUOTA_BYTES, cpu_to_be64(priv->quota),
			 NFTA_QUOTA_PAD) ||
	    nla_put_be32(skb, NFTA_QUOTA_FLAGS, htonl(flags)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_quota_type;
static const struct nft_expr_ops nft_quota_ops = {
	.type		= &nft_quota_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_quota)),
	.eval		= nft_quota_eval,
	.init		= nft_quota_init,
	.dump		= nft_quota_dump,
};

static struct nft_expr_type nft_quota_type __read_mostly = {
	.name		= "quota",
	.ops		= &nft_quota_ops,
	.policy		= nft_quota_policy,
	.maxattr	= NFTA_QUOTA_MAX,
	.flags		= NFT_EXPR_STATEFUL,
	.owner		= THIS_MODULE,
};

static int __init nft_quota_module_init(void)
{
        return nft_register_expr(&nft_quota_type);
}

static void __exit nft_quota_module_exit(void)
{
        nft_unregister_expr(&nft_quota_type);
}

module_init(nft_quota_module_init);
module_exit(nft_quota_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_EXPR("quota");
