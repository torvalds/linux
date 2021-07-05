// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>

struct nft_last_priv {
	unsigned long	last_jiffies;
	unsigned int	last_set;
};

static const struct nla_policy nft_last_policy[NFTA_LAST_MAX + 1] = {
	[NFTA_LAST_SET] = { .type = NLA_U32 },
	[NFTA_LAST_MSECS] = { .type = NLA_U64 },
};

static int nft_last_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
			 const struct nlattr * const tb[])
{
	struct nft_last_priv *priv = nft_expr_priv(expr);
	u64 last_jiffies;
	int err;

	if (tb[NFTA_LAST_MSECS]) {
		err = nf_msecs_to_jiffies64(tb[NFTA_LAST_MSECS], &last_jiffies);
		if (err < 0)
			return err;

		priv->last_jiffies = jiffies + (unsigned long)last_jiffies;
		priv->last_set = 1;
	}

	return 0;
}

static void nft_last_eval(const struct nft_expr *expr,
			  struct nft_regs *regs, const struct nft_pktinfo *pkt)
{
	struct nft_last_priv *priv = nft_expr_priv(expr);

	priv->last_jiffies = jiffies;
	priv->last_set = 1;
}

static int nft_last_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	struct nft_last_priv *priv = nft_expr_priv(expr);
	__be64 msecs;

	if (time_before(jiffies, priv->last_jiffies))
		priv->last_set = 0;

	if (priv->last_set)
		msecs = nf_jiffies64_to_msecs(jiffies - priv->last_jiffies);
	else
		msecs = 0;

	if (nla_put_be32(skb, NFTA_LAST_SET, htonl(priv->last_set)) ||
	    nla_put_be64(skb, NFTA_LAST_MSECS, msecs, NFTA_LAST_PAD))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static const struct nft_expr_ops nft_last_ops = {
	.type		= &nft_last_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_last_priv)),
	.eval		= nft_last_eval,
	.init		= nft_last_init,
	.dump		= nft_last_dump,
};

struct nft_expr_type nft_last_type __read_mostly = {
	.name		= "last",
	.ops		= &nft_last_ops,
	.policy		= nft_last_policy,
	.maxattr	= NFTA_LAST_MAX,
	.flags		= NFT_EXPR_STATEFUL,
	.owner		= THIS_MODULE,
};
