// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
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

struct nft_limit {
	spinlock_t	lock;
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

	spin_lock_bh(&limit->lock);
	now = ktime_get_ns();
	tokens = limit->tokens + now - limit->last;
	if (tokens > limit->tokens_max)
		tokens = limit->tokens_max;

	limit->last = now;
	delta = tokens - cost;
	if (delta >= 0) {
		limit->tokens = delta;
		spin_unlock_bh(&limit->lock);
		return limit->invert;
	}
	limit->tokens = tokens;
	spin_unlock_bh(&limit->lock);
	return !limit->invert;
}

/* Use same default as in iptables. */
#define NFT_LIMIT_PKT_BURST_DEFAULT	5

static int nft_limit_init(struct nft_limit *limit,
			  const struct nlattr * const tb[], bool pkts)
{
	u64 unit, tokens;

	if (tb[NFTA_LIMIT_RATE] == NULL ||
	    tb[NFTA_LIMIT_UNIT] == NULL)
		return -EINVAL;

	limit->rate = be64_to_cpu(nla_get_be64(tb[NFTA_LIMIT_RATE]));
	unit = be64_to_cpu(nla_get_be64(tb[NFTA_LIMIT_UNIT]));
	limit->nsecs = unit * NSEC_PER_SEC;
	if (limit->rate == 0 || limit->nsecs < unit)
		return -EOVERFLOW;

	if (tb[NFTA_LIMIT_BURST])
		limit->burst = ntohl(nla_get_be32(tb[NFTA_LIMIT_BURST]));

	if (pkts && limit->burst == 0)
		limit->burst = NFT_LIMIT_PKT_BURST_DEFAULT;

	if (limit->rate + limit->burst < limit->rate)
		return -EOVERFLOW;

	if (pkts) {
		tokens = div_u64(limit->nsecs, limit->rate) * limit->burst;
	} else {
		/* The token bucket size limits the number of tokens can be
		 * accumulated. tokens_max specifies the bucket size.
		 * tokens_max = unit * (rate + burst) / rate.
		 */
		tokens = div_u64(limit->nsecs * (limit->rate + limit->burst),
				 limit->rate);
	}

	limit->tokens = tokens;
	limit->tokens_max = limit->tokens;

	if (tb[NFTA_LIMIT_FLAGS]) {
		u32 flags = ntohl(nla_get_be32(tb[NFTA_LIMIT_FLAGS]));

		if (flags & NFT_LIMIT_F_INV)
			limit->invert = true;
	}
	limit->last = ktime_get_ns();
	spin_lock_init(&limit->lock);

	return 0;
}

static int nft_limit_dump(struct sk_buff *skb, const struct nft_limit *limit,
			  enum nft_limit_type type)
{
	u32 flags = limit->invert ? NFT_LIMIT_F_INV : 0;
	u64 secs = div_u64(limit->nsecs, NSEC_PER_SEC);

	if (nla_put_be64(skb, NFTA_LIMIT_RATE, cpu_to_be64(limit->rate),
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

	err = nft_limit_init(&priv->limit, tb, true);
	if (err < 0)
		return err;

	priv->cost = div64_u64(priv->limit.nsecs, priv->limit.rate);
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

static void nft_limit_bytes_eval(const struct nft_expr *expr,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt)
{
	struct nft_limit *priv = nft_expr_priv(expr);
	u64 cost = div64_u64(priv->nsecs * pkt->skb->len, priv->rate);

	if (nft_limit_eval(priv, cost))
		regs->verdict.code = NFT_BREAK;
}

static int nft_limit_bytes_init(const struct nft_ctx *ctx,
				const struct nft_expr *expr,
				const struct nlattr * const tb[])
{
	struct nft_limit *priv = nft_expr_priv(expr);

	return nft_limit_init(priv, tb, false);
}

static int nft_limit_bytes_dump(struct sk_buff *skb,
				const struct nft_expr *expr)
{
	const struct nft_limit *priv = nft_expr_priv(expr);

	return nft_limit_dump(skb, priv, NFT_LIMIT_PKT_BYTES);
}

static const struct nft_expr_ops nft_limit_bytes_ops = {
	.type		= &nft_limit_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_limit)),
	.eval		= nft_limit_bytes_eval,
	.init		= nft_limit_bytes_init,
	.dump		= nft_limit_bytes_dump,
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
		return &nft_limit_bytes_ops;
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

static void nft_limit_obj_pkts_eval(struct nft_object *obj,
				    struct nft_regs *regs,
				    const struct nft_pktinfo *pkt)
{
	struct nft_limit_pkts *priv = nft_obj_data(obj);

	if (nft_limit_eval(&priv->limit, priv->cost))
		regs->verdict.code = NFT_BREAK;
}

static int nft_limit_obj_pkts_init(const struct nft_ctx *ctx,
				   const struct nlattr * const tb[],
				   struct nft_object *obj)
{
	struct nft_limit_pkts *priv = nft_obj_data(obj);
	int err;

	err = nft_limit_init(&priv->limit, tb, true);
	if (err < 0)
		return err;

	priv->cost = div64_u64(priv->limit.nsecs, priv->limit.rate);
	return 0;
}

static int nft_limit_obj_pkts_dump(struct sk_buff *skb,
				   struct nft_object *obj,
				   bool reset)
{
	const struct nft_limit_pkts *priv = nft_obj_data(obj);

	return nft_limit_dump(skb, &priv->limit, NFT_LIMIT_PKTS);
}

static struct nft_object_type nft_limit_obj_type;
static const struct nft_object_ops nft_limit_obj_pkts_ops = {
	.type		= &nft_limit_obj_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_limit_pkts)),
	.init		= nft_limit_obj_pkts_init,
	.eval		= nft_limit_obj_pkts_eval,
	.dump		= nft_limit_obj_pkts_dump,
};

static void nft_limit_obj_bytes_eval(struct nft_object *obj,
				     struct nft_regs *regs,
				     const struct nft_pktinfo *pkt)
{
	struct nft_limit *priv = nft_obj_data(obj);
	u64 cost = div64_u64(priv->nsecs * pkt->skb->len, priv->rate);

	if (nft_limit_eval(priv, cost))
		regs->verdict.code = NFT_BREAK;
}

static int nft_limit_obj_bytes_init(const struct nft_ctx *ctx,
				    const struct nlattr * const tb[],
				    struct nft_object *obj)
{
	struct nft_limit *priv = nft_obj_data(obj);

	return nft_limit_init(priv, tb, false);
}

static int nft_limit_obj_bytes_dump(struct sk_buff *skb,
				    struct nft_object *obj,
				    bool reset)
{
	const struct nft_limit *priv = nft_obj_data(obj);

	return nft_limit_dump(skb, priv, NFT_LIMIT_PKT_BYTES);
}

static struct nft_object_type nft_limit_obj_type;
static const struct nft_object_ops nft_limit_obj_bytes_ops = {
	.type		= &nft_limit_obj_type,
	.size		= sizeof(struct nft_limit),
	.init		= nft_limit_obj_bytes_init,
	.eval		= nft_limit_obj_bytes_eval,
	.dump		= nft_limit_obj_bytes_dump,
};

static const struct nft_object_ops *
nft_limit_obj_select_ops(const struct nft_ctx *ctx,
			 const struct nlattr * const tb[])
{
	if (!tb[NFTA_LIMIT_TYPE])
		return &nft_limit_obj_pkts_ops;

	switch (ntohl(nla_get_be32(tb[NFTA_LIMIT_TYPE]))) {
	case NFT_LIMIT_PKTS:
		return &nft_limit_obj_pkts_ops;
	case NFT_LIMIT_PKT_BYTES:
		return &nft_limit_obj_bytes_ops;
	}
	return ERR_PTR(-EOPNOTSUPP);
}

static struct nft_object_type nft_limit_obj_type __read_mostly = {
	.select_ops	= nft_limit_obj_select_ops,
	.type		= NFT_OBJECT_LIMIT,
	.maxattr	= NFTA_LIMIT_MAX,
	.policy		= nft_limit_policy,
	.owner		= THIS_MODULE,
};

static int __init nft_limit_module_init(void)
{
	int err;

	err = nft_register_obj(&nft_limit_obj_type);
	if (err < 0)
		return err;

	err = nft_register_expr(&nft_limit_type);
	if (err < 0)
		goto err1;

	return 0;
err1:
	nft_unregister_obj(&nft_limit_obj_type);
	return err;
}

static void __exit nft_limit_module_exit(void)
{
	nft_unregister_expr(&nft_limit_type);
	nft_unregister_obj(&nft_limit_obj_type);
}

module_init(nft_limit_module_init);
module_exit(nft_limit_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_EXPR("limit");
MODULE_ALIAS_NFT_OBJ(NFT_OBJECT_LIMIT);
