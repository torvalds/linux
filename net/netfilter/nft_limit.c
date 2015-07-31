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
	u64		last;
	u64		tokens;
	u64		tokens_max;
	u64		rate;
	u64		nsecs;
};

static void nft_limit_pkts_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	struct nft_limit *priv = nft_expr_priv(expr);
	u64 now, tokens, cost = div_u64(priv->nsecs, priv->rate);
	s64 delta;

	spin_lock_bh(&limit_lock);
	now = ktime_get_ns();
	tokens = priv->tokens + now - priv->last;
	if (tokens > priv->tokens_max)
		tokens = priv->tokens_max;

	priv->last = now;
	delta = tokens - cost;
	if (delta >= 0) {
		priv->tokens = delta;
		spin_unlock_bh(&limit_lock);
		return;
	}
	priv->tokens = tokens;
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
	u64 unit;

	if (tb[NFTA_LIMIT_RATE] == NULL ||
	    tb[NFTA_LIMIT_UNIT] == NULL)
		return -EINVAL;

	priv->rate = be64_to_cpu(nla_get_be64(tb[NFTA_LIMIT_RATE]));
	unit = be64_to_cpu(nla_get_be64(tb[NFTA_LIMIT_UNIT]));
	priv->nsecs = unit * NSEC_PER_SEC;
	if (priv->rate == 0 || priv->nsecs < unit)
		return -EOVERFLOW;
	priv->tokens = priv->tokens_max = priv->nsecs;
	priv->last = ktime_get_ns();
	return 0;
}

static int nft_limit_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_limit *priv = nft_expr_priv(expr);
	u64 secs = div_u64(priv->nsecs, NSEC_PER_SEC);

	if (nla_put_be64(skb, NFTA_LIMIT_RATE, cpu_to_be64(priv->rate)) ||
	    nla_put_be64(skb, NFTA_LIMIT_UNIT, cpu_to_be64(secs)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_limit_type;
static const struct nft_expr_ops nft_limit_pkts_ops = {
	.type		= &nft_limit_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_limit)),
	.eval		= nft_limit_pkts_eval,
	.init		= nft_limit_init,
	.dump		= nft_limit_dump,
};

static struct nft_expr_type nft_limit_type __read_mostly = {
	.name		= "limit",
	.ops		= &nft_limit_pkts_ops,
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
