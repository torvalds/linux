/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>

struct nft_meta {
	enum nft_meta_keys	key;
};

static void nft_meta_eval(const struct nft_expr *expr,
			  struct nft_data *nfres,
			  struct nft_data *data,
			  const struct nft_pktinfo *pkt)
{
	const struct nft_meta *meta = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
	u32 val = data->data[0];

	switch (meta->key) {
	case NFT_META_MARK:
		skb->mark = val;
		break;
	case NFT_META_PRIORITY:
		skb->priority = val;
		break;
	case NFT_META_NFTRACE:
		skb->nf_trace = val;
		break;
#ifdef CONFIG_NETWORK_SECMARK
	case NFT_META_SECMARK:
		skb->secmark = val;
		break;
#endif
	default:
		WARN_ON(1);
	}
}

static const struct nla_policy nft_meta_policy[NFTA_META_MAX + 1] = {
	[NFTA_META_KEY]		= { .type = NLA_U32 },
};

static int nft_meta_init(const struct nft_expr *expr, struct nlattr *tb[])
{
	struct nft_meta *meta = nft_expr_priv(expr);

	if (tb[NFTA_META_KEY] == NULL)
		return -EINVAL;

	meta->key = ntohl(nla_get_be32(tb[NFTA_META_KEY]));
	switch (meta->key) {
	case NFT_META_MARK:
	case NFT_META_PRIORITY:
	case NFT_META_NFTRACE:
#ifdef CONFIG_NETWORK_SECMARK
	case NFT_META_SECMARK:
#endif
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nft_meta_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	struct nft_meta *meta = nft_expr_priv(expr);

	NLA_PUT_BE32(skb, NFTA_META_KEY, htonl(meta->key));
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_ops meta_target __read_mostly = {
	.name		= "meta",
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_meta)),
	.owner		= THIS_MODULE,
	.eval		= nft_meta_eval,
	.init		= nft_meta_init,
	.dump		= nft_meta_dump,
	.policy		= nft_meta_policy,
	.maxattr	= NFTA_META_MAX,
};

static int __init nft_meta_target_init(void)
{
	return nft_register_expr(&meta_target);
}

static void __exit nft_meta_target_exit(void)
{
	nft_unregister_expr(&meta_target);
}

module_init(nft_meta_target_init);
module_exit(nft_meta_target_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_EXPR("meta");
