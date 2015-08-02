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

static inline bool nft_limit_eval(struct nft_limit *limit, u64 cost)
{
	u64 now, tokens;
	s64 delta;

	spin_lock_bh(&limit_lock);
	now = ktime_get_ns();
	tokens = limit->tokens + now - limit->last;
	if (tokens > limit->tokens_max)
		tokens = limit->tokens_max;

	limit->last = now;
	delta = tokens - cost;
	if (delta >= 0) {
		limit->tokens = delta;
		spin_unlock_bh(&limit_lock);
		return false;
	}
	limit->tokens = tokens;
	spin_unlock_bh(&limit_lock);
	return true;
}

static int nft_limit_init(struct nft_limit *limit,
			  const struct nlattr * const tb[])
{
	u64 unit;

	if (tb[NFTA_LIMIT_RATE] == NULL ||
	    tb[NFTA_LIMIT_UNIT] == NULL)
		return -EINVAL;

	limit->rate = be64_to_cpu(nla_get_be64(tb[NFTA_LIMIT_RATE]));
	unit = be64_to_cpu(nla_get_be64(tb[NFTA_LIMIT_UNIT]));
	limit->nsecs = unit * NSEC_PER_SEC;
	if (limit->rate == 0 || limit->nsecs < unit)
		return -EOVERFLOW;
	limit->tokens = limit->tokens_max = limit->nsecs;
	limit->last = ktime_get_ns();

	return 0;
}

static int nft_limit_dump(struct sk_buff *skb, const struct nft_limit *limit)
{
	u64 secs = div_u64(limit->nsecs, NSEC_PER_SEC);

	if (nla_put_be64(skb, NFTA_LIMIT_RATE, cpu_to_be64(limit->rate)) ||
	    nla_put_be64(skb, NFTA_LIMIT_UNIT, cpu_to_be64(secs)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static void nft_limit_pkts_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	struct nft_limit *priv = nft_expr_priv(expr);

	if (nft_limit_eval(priv, div_u64(priv->nsecs, priv->rate)))
		regs->verdict.code = NFT_BREAK;
}

static const struct nla_policy nft_limit_policy[NFTA_LIMIT_MAX + 1] = {
	[NFTA_LIMIT_RATE]	= { .type = NLA_U64 },
	[NFTA_LIMIT_UNIT]	= { .type = NLA_U64 },
};

static int nft_limit_pkts_init(const struct nft_ctx *ctx,
			       const struct nft_expr *expr,
			       const struct nlattr * const tb[])
{
	struct nft_limit *priv = nft_expr_priv(expr);

	return nft_limit_init(priv, tb);
}

static int nft_limit_pkts_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_limit *priv = nft_expr_priv(expr);

	return nft_limit_dump(skb, priv);
}

static struct nft_expr_type nft_limit_type;
static const struct nft_expr_ops nft_limit_pkts_ops = {
	.type		= &nft_limit_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_limit)),
	.eval		= nft_limit_pkts_eval,
	.init		= nft_limit_pkts_init,
	.dump		= nft_limit_pkts_dump,
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
