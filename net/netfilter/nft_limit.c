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
};

struct nft_limit_priv {
	struct nft_limit *limit;
	u64		tokens_max;
	u64		rate;
	u64		nsecs;
	u32		burst;
	bool		invert;
};

static inline bool nft_limit_eval(struct nft_limit_priv *priv, u64 cost)
{
	u64 now, tokens;
	s64 delta;

	spin_lock_bh(&priv->limit->lock);
	now = ktime_get_ns();
	tokens = priv->limit->tokens + now - priv->limit->last;
	if (tokens > priv->tokens_max)
		tokens = priv->tokens_max;

	priv->limit->last = now;
	delta = tokens - cost;
	if (delta >= 0) {
		priv->limit->tokens = delta;
		spin_unlock_bh(&priv->limit->lock);
		return priv->invert;
	}
	priv->limit->tokens = tokens;
	spin_unlock_bh(&priv->limit->lock);
	return !priv->invert;
}

/* Use same default as in iptables. */
#define NFT_LIMIT_PKT_BURST_DEFAULT	5

static int nft_limit_init(struct nft_limit_priv *priv,
			  const struct nlattr * const tb[], bool pkts)
{
	u64 unit, tokens, rate_with_burst;
	bool invert = false;

	if (tb[NFTA_LIMIT_RATE] == NULL ||
	    tb[NFTA_LIMIT_UNIT] == NULL)
		return -EINVAL;

	priv->rate = be64_to_cpu(nla_get_be64(tb[NFTA_LIMIT_RATE]));
	if (priv->rate == 0)
		return -EINVAL;

	unit = be64_to_cpu(nla_get_be64(tb[NFTA_LIMIT_UNIT]));
	if (check_mul_overflow(unit, NSEC_PER_SEC, &priv->nsecs))
		return -EOVERFLOW;

	if (tb[NFTA_LIMIT_BURST])
		priv->burst = ntohl(nla_get_be32(tb[NFTA_LIMIT_BURST]));

	if (pkts && priv->burst == 0)
		priv->burst = NFT_LIMIT_PKT_BURST_DEFAULT;

	if (check_add_overflow(priv->rate, priv->burst, &rate_with_burst))
		return -EOVERFLOW;

	if (pkts) {
		u64 tmp = div64_u64(priv->nsecs, priv->rate);

		if (check_mul_overflow(tmp, priv->burst, &tokens))
			return -EOVERFLOW;
	} else {
		u64 tmp;

		/* The token bucket size limits the number of tokens can be
		 * accumulated. tokens_max specifies the bucket size.
		 * tokens_max = unit * (rate + burst) / rate.
		 */
		if (check_mul_overflow(priv->nsecs, rate_with_burst, &tmp))
			return -EOVERFLOW;

		tokens = div64_u64(tmp, priv->rate);
	}

	if (tb[NFTA_LIMIT_FLAGS]) {
		u32 flags = ntohl(nla_get_be32(tb[NFTA_LIMIT_FLAGS]));

		if (flags & ~NFT_LIMIT_F_INV)
			return -EOPNOTSUPP;

		if (flags & NFT_LIMIT_F_INV)
			invert = true;
	}

	priv->limit = kmalloc(sizeof(*priv->limit), GFP_KERNEL_ACCOUNT);
	if (!priv->limit)
		return -ENOMEM;

	priv->limit->tokens = tokens;
	priv->tokens_max = priv->limit->tokens;
	priv->invert = invert;
	priv->limit->last = ktime_get_ns();
	spin_lock_init(&priv->limit->lock);

	return 0;
}

static int nft_limit_dump(struct sk_buff *skb, const struct nft_limit_priv *priv,
			  enum nft_limit_type type)
{
	u32 flags = priv->invert ? NFT_LIMIT_F_INV : 0;
	u64 secs = div_u64(priv->nsecs, NSEC_PER_SEC);

	if (nla_put_be64(skb, NFTA_LIMIT_RATE, cpu_to_be64(priv->rate),
			 NFTA_LIMIT_PAD) ||
	    nla_put_be64(skb, NFTA_LIMIT_UNIT, cpu_to_be64(secs),
			 NFTA_LIMIT_PAD) ||
	    nla_put_be32(skb, NFTA_LIMIT_BURST, htonl(priv->burst)) ||
	    nla_put_be32(skb, NFTA_LIMIT_TYPE, htonl(type)) ||
	    nla_put_be32(skb, NFTA_LIMIT_FLAGS, htonl(flags)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static void nft_limit_destroy(const struct nft_ctx *ctx,
			      const struct nft_limit_priv *priv)
{
	kfree(priv->limit);
}

static int nft_limit_clone(struct nft_limit_priv *priv_dst,
			   const struct nft_limit_priv *priv_src)
{
	priv_dst->tokens_max = priv_src->tokens_max;
	priv_dst->rate = priv_src->rate;
	priv_dst->nsecs = priv_src->nsecs;
	priv_dst->burst = priv_src->burst;
	priv_dst->invert = priv_src->invert;

	priv_dst->limit = kmalloc(sizeof(*priv_dst->limit), GFP_ATOMIC);
	if (!priv_dst->limit)
		return -ENOMEM;

	spin_lock_init(&priv_dst->limit->lock);
	priv_dst->limit->tokens = priv_src->tokens_max;
	priv_dst->limit->last = ktime_get_ns();

	return 0;
}

struct nft_limit_priv_pkts {
	struct nft_limit_priv	limit;
	u64			cost;
};

static void nft_limit_pkts_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	struct nft_limit_priv_pkts *priv = nft_expr_priv(expr);

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
	struct nft_limit_priv_pkts *priv = nft_expr_priv(expr);
	int err;

	err = nft_limit_init(&priv->limit, tb, true);
	if (err < 0)
		return err;

	priv->cost = div64_u64(priv->limit.nsecs, priv->limit.rate);
	return 0;
}

static int nft_limit_pkts_dump(struct sk_buff *skb,
			       const struct nft_expr *expr, bool reset)
{
	const struct nft_limit_priv_pkts *priv = nft_expr_priv(expr);

	return nft_limit_dump(skb, &priv->limit, NFT_LIMIT_PKTS);
}

static void nft_limit_pkts_destroy(const struct nft_ctx *ctx,
				   const struct nft_expr *expr)
{
	const struct nft_limit_priv_pkts *priv = nft_expr_priv(expr);

	nft_limit_destroy(ctx, &priv->limit);
}

static int nft_limit_pkts_clone(struct nft_expr *dst, const struct nft_expr *src)
{
	struct nft_limit_priv_pkts *priv_dst = nft_expr_priv(dst);
	struct nft_limit_priv_pkts *priv_src = nft_expr_priv(src);

	priv_dst->cost = priv_src->cost;

	return nft_limit_clone(&priv_dst->limit, &priv_src->limit);
}

static struct nft_expr_type nft_limit_type;
static const struct nft_expr_ops nft_limit_pkts_ops = {
	.type		= &nft_limit_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_limit_priv_pkts)),
	.eval		= nft_limit_pkts_eval,
	.init		= nft_limit_pkts_init,
	.destroy	= nft_limit_pkts_destroy,
	.clone		= nft_limit_pkts_clone,
	.dump		= nft_limit_pkts_dump,
	.reduce		= NFT_REDUCE_READONLY,
};

static void nft_limit_bytes_eval(const struct nft_expr *expr,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt)
{
	struct nft_limit_priv *priv = nft_expr_priv(expr);
	u64 cost = div64_u64(priv->nsecs * pkt->skb->len, priv->rate);

	if (nft_limit_eval(priv, cost))
		regs->verdict.code = NFT_BREAK;
}

static int nft_limit_bytes_init(const struct nft_ctx *ctx,
				const struct nft_expr *expr,
				const struct nlattr * const tb[])
{
	struct nft_limit_priv *priv = nft_expr_priv(expr);

	return nft_limit_init(priv, tb, false);
}

static int nft_limit_bytes_dump(struct sk_buff *skb,
				const struct nft_expr *expr, bool reset)
{
	const struct nft_limit_priv *priv = nft_expr_priv(expr);

	return nft_limit_dump(skb, priv, NFT_LIMIT_PKT_BYTES);
}

static void nft_limit_bytes_destroy(const struct nft_ctx *ctx,
				    const struct nft_expr *expr)
{
	const struct nft_limit_priv *priv = nft_expr_priv(expr);

	nft_limit_destroy(ctx, priv);
}

static int nft_limit_bytes_clone(struct nft_expr *dst, const struct nft_expr *src)
{
	struct nft_limit_priv *priv_dst = nft_expr_priv(dst);
	struct nft_limit_priv *priv_src = nft_expr_priv(src);

	return nft_limit_clone(priv_dst, priv_src);
}

static const struct nft_expr_ops nft_limit_bytes_ops = {
	.type		= &nft_limit_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_limit_priv)),
	.eval		= nft_limit_bytes_eval,
	.init		= nft_limit_bytes_init,
	.dump		= nft_limit_bytes_dump,
	.clone		= nft_limit_bytes_clone,
	.destroy	= nft_limit_bytes_destroy,
	.reduce		= NFT_REDUCE_READONLY,
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
	struct nft_limit_priv_pkts *priv = nft_obj_data(obj);

	if (nft_limit_eval(&priv->limit, priv->cost))
		regs->verdict.code = NFT_BREAK;
}

static int nft_limit_obj_pkts_init(const struct nft_ctx *ctx,
				   const struct nlattr * const tb[],
				   struct nft_object *obj)
{
	struct nft_limit_priv_pkts *priv = nft_obj_data(obj);
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
	const struct nft_limit_priv_pkts *priv = nft_obj_data(obj);

	return nft_limit_dump(skb, &priv->limit, NFT_LIMIT_PKTS);
}

static void nft_limit_obj_pkts_destroy(const struct nft_ctx *ctx,
				       struct nft_object *obj)
{
	struct nft_limit_priv_pkts *priv = nft_obj_data(obj);

	nft_limit_destroy(ctx, &priv->limit);
}

static struct nft_object_type nft_limit_obj_type;
static const struct nft_object_ops nft_limit_obj_pkts_ops = {
	.type		= &nft_limit_obj_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_limit_priv_pkts)),
	.init		= nft_limit_obj_pkts_init,
	.destroy	= nft_limit_obj_pkts_destroy,
	.eval		= nft_limit_obj_pkts_eval,
	.dump		= nft_limit_obj_pkts_dump,
};

static void nft_limit_obj_bytes_eval(struct nft_object *obj,
				     struct nft_regs *regs,
				     const struct nft_pktinfo *pkt)
{
	struct nft_limit_priv *priv = nft_obj_data(obj);
	u64 cost = div64_u64(priv->nsecs * pkt->skb->len, priv->rate);

	if (nft_limit_eval(priv, cost))
		regs->verdict.code = NFT_BREAK;
}

static int nft_limit_obj_bytes_init(const struct nft_ctx *ctx,
				    const struct nlattr * const tb[],
				    struct nft_object *obj)
{
	struct nft_limit_priv *priv = nft_obj_data(obj);

	return nft_limit_init(priv, tb, false);
}

static int nft_limit_obj_bytes_dump(struct sk_buff *skb,
				    struct nft_object *obj,
				    bool reset)
{
	const struct nft_limit_priv *priv = nft_obj_data(obj);

	return nft_limit_dump(skb, priv, NFT_LIMIT_PKT_BYTES);
}

static void nft_limit_obj_bytes_destroy(const struct nft_ctx *ctx,
					struct nft_object *obj)
{
	struct nft_limit_priv *priv = nft_obj_data(obj);

	nft_limit_destroy(ctx, priv);
}

static struct nft_object_type nft_limit_obj_type;
static const struct nft_object_ops nft_limit_obj_bytes_ops = {
	.type		= &nft_limit_obj_type,
	.size		= sizeof(struct nft_limit_priv),
	.init		= nft_limit_obj_bytes_init,
	.destroy	= nft_limit_obj_bytes_destroy,
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
MODULE_DESCRIPTION("nftables limit expression support");
