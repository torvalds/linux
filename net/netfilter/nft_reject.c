/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2013 Eric Leblond <eric@regit.org>
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
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_reject.h>

const struct nla_policy nft_reject_policy[NFTA_REJECT_MAX + 1] = {
	[NFTA_REJECT_TYPE]		= { .type = NLA_U32 },
	[NFTA_REJECT_ICMP_CODE]		= { .type = NLA_U8 },
};
EXPORT_SYMBOL_GPL(nft_reject_policy);

int nft_reject_init(const struct nft_ctx *ctx,
		    const struct nft_expr *expr,
		    const struct nlattr * const tb[])
{
	struct nft_reject *priv = nft_expr_priv(expr);

	if (tb[NFTA_REJECT_TYPE] == NULL)
		return -EINVAL;

	priv->type = ntohl(nla_get_be32(tb[NFTA_REJECT_TYPE]));
	switch (priv->type) {
	case NFT_REJECT_ICMP_UNREACH:
		if (tb[NFTA_REJECT_ICMP_CODE] == NULL)
			return -EINVAL;
		priv->icmp_code = nla_get_u8(tb[NFTA_REJECT_ICMP_CODE]);
	case NFT_REJECT_TCP_RST:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nft_reject_init);

int nft_reject_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_reject *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_REJECT_TYPE, htonl(priv->type)))
		goto nla_put_failure;

	switch (priv->type) {
	case NFT_REJECT_ICMP_UNREACH:
		if (nla_put_u8(skb, NFTA_REJECT_ICMP_CODE, priv->icmp_code))
			goto nla_put_failure;
		break;
	}

	return 0;

nla_put_failure:
	return -1;
}
EXPORT_SYMBOL_GPL(nft_reject_dump);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
