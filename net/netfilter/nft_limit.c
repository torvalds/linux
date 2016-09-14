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
	u32		burst;
	bool		invert;
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
		return limit->invert;
	}
	limit->tokens = tokens;
	spin_unlock_bh(&limit_lock);
	return !limit->invert;
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

	if (tb[NFTA_LIMIT_BURST]) {
		u64 rate;

		limit->burst = ntohl(nla_get_be32(tb[NFTA_LIMIT_BURST]));

		rate = limit->rate + limit->burst;
		if (rate < limit->rate)
			return -EOVERFLOW;

		limit->rate = rate;
	}
	if (tb[NFTA_LIMIT_FLAGS]) {
		u32 flags = ntohl(nla_get_be32(tb[NFTA_LIMIT_FLAGS]));

		if (flags & NFT_LIMIT_F_INV)
			limit->invert = true;
	}
	limit->last = ktime_get_ns();

	return 0;
}

static int nft_limit_dump(struct sk_buff *skb, const struct nft_limit *limit,
			  enum nft_limit_type type)
{
	u32 flags = limit->invert ? NFT_LIMIT_F_INV : 0;
	u64 secs = div_u64(limit->nsecs, NSEC_PER_SEC);
	u64 rate = limit->rate - limit->burst;

	if (nla_put_be64(skb, NFTA_LIMIT_RATE, cpu_to_be64(rate),
			 NFTA_LIMIT_PAD) ||
	    nla_put_be64(skb, NFTA_LIMIT_UNIT, cpu_to_be64(secs),
			 NFTA_LIMIT_PAD) ||
	    nla_put_be32(skb, NFTA_LIMIT_BURST, htonl(limit->burst)) ||
	    nla_put_be32(skb, NFTA_LIMIT_TYPE, htonl(type)) ||
	    nla_put_be32(skb, NFTA_LIMIT_FLAGS, htonl(flags)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

struct nft_limit_pkts {
	struct nft_limit	limit;
	u64			cost;
};

static void nft_limit_pkts_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	struct nft_limit_pkts *priv = nft_expr_priv(expr);

	if (nft_limit_eval(&priv->limit, priv->cost))
		regs->verdict.code = NFT_BREAK;
}

static const struct nla_policy nft_limit_policy[NFTA_LIMIT_MAX + 1] = {
	[NFTA_LIMIT_RATE]	= { .type = NLA_U64 },
	[NFTA_LIMIT_UNIT]	= { .type = NLA_U64 },
	[NFTA_LIMIT_BURST]	= { .type = NLA_U32 },
	[NFTA_LIMIT_TYPE]	= { .type = NLA_U32 },
	[NFTA_LIMIT_FLAGS]	= { .type = NLA_U32 },
};

static int nft_limit_pkts_init(const struct nft_ctx *ctx,
			       const struct nft_expr *expr,
			       const struct nlattr * const tb[])
{
	struct nft_limit_pkts *priv = nft_expr_priv(expr);
	int err;

	err = nft_limit_init(&priv->limit, tb);
	if (err < 0)
		return err;

	priv->cost = div_u64(priv->limit.nsecs, priv->limit.rate);
	return 0;
}

static int nft_limit_pkts_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_limit_pkts *priv = nft_expr_priv(expr);

	return nft_limit_dump(skb, &priv->limit, NFT_LIMIT_PKTS);
}

static struct nft_expr_type nft_limit_type;
static const struct nft_expr_ops nft_limit_pkts_ops = {
	.type		= &nft_limit_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_limit_pkts)),
	.eval		= nft_limit_pkts_eval,
	.init		= nft_limit_pkts_init,
	.dump		= nft_limit_pkts_dump,
};

static void nft_limit_pkt_bytes_eval(const struct nft_expr *expr,
				     struct nft_regs *regs,
				     const struct nft_pktinfo *pkt)
{
	struct nft_limit *priv = nft_expr_priv(expr);
	u64 cost = div_u64(priv->nsecs * pkt->skb->len, priv->rate);

	if (nft_limit_eval(priv, cost))
		regs->verdict.code = NFT_BREAK;
}

static int nft_limit_pkt_bytes_init(const struct nft_ctx *ctx,
				    const struct nft_expr *expr,
				    const struct nlattr * const tb[])
{
	struct nft_limit *priv = nft_expr_priv(expr);

	return nft_limit_init(priv, tb);
}

static int nft_limit_pkt_bytes_dump(struct sk_buff *skb,
				    const struct nft_expr *expr)
{
	const struct nft_limit *priv = nft_expr_priv(expr);

	return nft_limit_dump(skb, priv, NFT_LIMIT_PKT_BYTES);
}

static const struct nft_expr_ops nft_limit_pkt_bytes_ops = {
	.type		= &nft_limit_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_limit)),
	.eval		= nft_limit_pkt_bytes_eval,
	.init		= nft_limit_pkt_bytes_init,
	.dump		= nft_limit_pkt_bytes_dump,
};

static const struct nft_expr_ops *
nft_limit_select_ops(const struct nft_ctx *ctx,
		     const struct nlattr * const tb[])
{
	if (tb[NFTA_LIMIT_TYPE] == NULL)
		return &nft_limit_pkts_ops;

	switch (ntohl(nla_get_be32(tb[NFTA_LIMIT_TYPE]))) {
	case NFT_LIMIT_PKTS:
		return &nft_limit_pkts_ops;
	case NFT_LIMIT_PKT_BYTES:
		return &nft_limit_pkt_bytes_ops;
	}
	return ERR_PTR(-EOPNOTSUPP);
}

static struct nft_expr_type nft_limit_type __read_mostly = {
	.name		= "limit",
	.select_ops	= nft_limit_select_ops,
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
