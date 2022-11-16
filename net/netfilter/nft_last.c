// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>

struct nft_last {
	unsigned long	jiffies;
	unsigned int	set;
};

struct nft_last_priv {
	struct nft_last	*last;
};

static const struct nla_policy nft_last_policy[NFTA_LAST_MAX + 1] = {
	[NFTA_LAST_SET] = { .type = NLA_U32 },
	[NFTA_LAST_MSECS] = { .type = NLA_U64 },
};

static int nft_last_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
			 const struct nlattr * const tb[])
{
	struct nft_last_priv *priv = nft_expr_priv(expr);
	struct nft_last *last;
	u64 last_jiffies;
	int err;

	last = kzalloc(sizeof(*last), GFP_KERNEL_ACCOUNT);
	if (!last)
		return -ENOMEM;

	if (tb[NFTA_LAST_SET])
		last->set = ntohl(nla_get_be32(tb[NFTA_LAST_SET]));

	if (last->set && tb[NFTA_LAST_MSECS]) {
		err = nf_msecs_to_jiffies64(tb[NFTA_LAST_MSECS], &last_jiffies);
		if (err < 0)
			goto err;

		last->jiffies = jiffies - (unsigned long)last_jiffies;
	}
	priv->last = last;

	return 0;
err:
	kfree(last);

	return err;
}

static void nft_last_eval(const struct nft_expr *expr,
			  struct nft_regs *regs, const struct nft_pktinfo *pkt)
{
	struct nft_last_priv *priv = nft_expr_priv(expr);
	struct nft_last *last = priv->last;

	if (READ_ONCE(last->jiffies) != jiffies)
		WRITE_ONCE(last->jiffies, jiffies);
	if (READ_ONCE(last->set) == 0)
		WRITE_ONCE(last->set, 1);
}

static int nft_last_dump(struct sk_buff *skb,
			 const struct nft_expr *expr, bool reset)
{
	struct nft_last_priv *priv = nft_expr_priv(expr);
	struct nft_last *last = priv->last;
	unsigned long last_jiffies = READ_ONCE(last->jiffies);
	u32 last_set = READ_ONCE(last->set);
	__be64 msecs;

	if (time_before(jiffies, last_jiffies)) {
		WRITE_ONCE(last->set, 0);
		last_set = 0;
	}

	if (last_set)
		msecs = nf_jiffies64_to_msecs(jiffies - last_jiffies);
	else
		msecs = 0;

	if (nla_put_be32(skb, NFTA_LAST_SET, htonl(last_set)) ||
	    nla_put_be64(skb, NFTA_LAST_MSECS, msecs, NFTA_LAST_PAD))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static void nft_last_destroy(const struct nft_ctx *ctx,
			     const struct nft_expr *expr)
{
	struct nft_last_priv *priv = nft_expr_priv(expr);

	kfree(priv->last);
}

static int nft_last_clone(struct nft_expr *dst, const struct nft_expr *src)
{
	struct nft_last_priv *priv_dst = nft_expr_priv(dst);

	priv_dst->last = kzalloc(sizeof(*priv_dst->last), GFP_ATOMIC);
	if (!priv_dst->last)
		return -ENOMEM;

	return 0;
}

static const struct nft_expr_ops nft_last_ops = {
	.type		= &nft_last_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_last_priv)),
	.eval		= nft_last_eval,
	.init		= nft_last_init,
	.destroy	= nft_last_destroy,
	.clone		= nft_last_clone,
	.dump		= nft_last_dump,
	.reduce		= NFT_REDUCE_READONLY,
};

struct nft_expr_type nft_last_type __read_mostly = {
	.name		= "last",
	.ops		= &nft_last_ops,
	.policy		= nft_last_policy,
	.maxattr	= NFTA_LAST_MAX,
	.flags		= NFT_EXPR_STATEFUL,
	.owner		= THIS_MODULE,
};
