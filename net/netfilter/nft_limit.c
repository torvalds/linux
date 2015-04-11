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
#include <linux/spinlock.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>

static DEFINE_SPINLOCK(limit_lock);

struct nft_limit {
	u64		tokens;
	u64		rate;
	u64		unit;
	unsigned long	stamp;
};

static void nft_limit_eval(const struct nft_expr *expr,
			   struct nft_regs *regs,
			   const struct nft_pktinfo *pkt)
{
	struct nft_limit *priv = nft_expr_priv(expr);

	spin_lock_bh(&limit_lock);
	if (time_after_eq(jiffies, priv->stamp)) {
		priv->tokens = priv->rate;
		priv->stamp = jiffies + priv->unit * HZ;
	}

	if (priv->tokens >= 1) {
		priv->tokens--;
		spin_unlock_bh(&limit_lock);
		return;
	}
	spin_unlock_bh(&limit_lock);

	regs->verdict.code = NFT_BREAK;
}

static const struct nla_policy nft_limit_policy[NFTA_LIMIT_MAX + 1] = {
	[NFTA_LIMIT_RATE]	= { .type = NLA_U64 },
	[NFTA_LIMIT_UNIT]	= { .type = NLA_U64 },
};

static int nft_limit_init(const struct nft_ctx *ctx,
			  const struct nft_expr *expr,
			  const struct nlattr * const tb[])
{
	struct nft_limit *priv = nft_expr_priv(expr);

	if (tb[NFTA_LIMIT_RATE] == NULL ||
	    tb[NFTA_LIMIT_UNIT] == NULL)
		return -EINVAL;

	priv->rate   = be64_to_cpu(nla_get_be64(tb[NFTA_LIMIT_RATE]));
	priv->unit   = be64_to_cpu(nla_get_be64(tb[NFTA_LIMIT_UNIT]));
	priv->stamp  = jiffies + priv->unit * HZ;
	priv->tokens = priv->rate;
	return 0;
}

static int nft_limit_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_limit *priv = nft_expr_priv(expr);

	if (nla_put_be64(skb, NFTA_LIMIT_RATE, cpu_to_be64(priv->rate)))
		goto nla_put_failure;
	if (nla_put_be64(skb, NFTA_LIMIT_UNIT, cpu_to_be64(priv->unit)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_limit_type;
static const struct nft_expr_ops nft_limit_ops = {
	.type		= &nft_limit_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_limit)),
	.eval		= nft_limit_eval,
	.init		= nft_limit_init,
	.dump		= nft_limit_dump,
};

static struct nft_expr_type nft_limit_type __read_mostly = {
	.name		= "limit",
	.ops		= &nft_limit_ops,
	.policy		= nft_limit_policy,
	.maxattr	= NFTA_LIMIT_MAX,
	.flags		= NFT_EXPR_STATEFUL,
	.owner		= THIS_MODULE,
};

static int __init nft_limit_module_init(void)
{
	return nft_register_expr(&nft_limit_type);
}

static void __exit nft_limit_module_exit(void)
{
	nft_unregister_expr(&nft_limit_type);
}

module_init(nft_limit_module_init);
module_exit(nft_limit_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_EXPR("limit");
