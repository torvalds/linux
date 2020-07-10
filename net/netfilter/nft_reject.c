// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2013 Eric Leblond <eric@regit.org>
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
#include <linux/icmp.h>
#include <linux/icmpv6.h>

const struct nla_policy nft_reject_policy[NFTA_REJECT_MAX + 1] = {
	[NFTA_REJECT_TYPE]		= { .type = NLA_U32 },
	[NFTA_REJECT_ICMP_CODE]		= { .type = NLA_U8 },
};
EXPORT_SYMBOL_GPL(nft_reject_policy);

int nft_reject_validate(const struct nft_ctx *ctx,
			const struct nft_expr *expr,
			const struct nft_data **data)
{
	return nft_chain_validate_hooks(ctx->chain,
					(1 << NF_INET_LOCAL_IN) |
					(1 << NF_INET_FORWARD) |
					(1 << NF_INET_LOCAL_OUT));
}
EXPORT_SYMBOL_GPL(nft_reject_validate);

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
	default:
		break;
	}

	return 0;

nla_put_failure:
	return -1;
}
EXPORT_SYMBOL_GPL(nft_reject_dump);

static u8 icmp_code_v4[NFT_REJECT_ICMPX_MAX + 1] = {
	[NFT_REJECT_ICMPX_NO_ROUTE]		= ICMP_NET_UNREACH,
	[NFT_REJECT_ICMPX_PORT_UNREACH]		= ICMP_PORT_UNREACH,
	[NFT_REJECT_ICMPX_HOST_UNREACH]		= ICMP_HOST_UNREACH,
	[NFT_REJECT_ICMPX_ADMIN_PROHIBITED]	= ICMP_PKT_FILTERED,
};

int nft_reject_icmp_code(u8 code)
{
	if (WARN_ON_ONCE(code > NFT_REJECT_ICMPX_MAX))
		return ICMP_NET_UNREACH;

	return icmp_code_v4[code];
}

EXPORT_SYMBOL_GPL(nft_reject_icmp_code);


static u8 icmp_code_v6[NFT_REJECT_ICMPX_MAX + 1] = {
	[NFT_REJECT_ICMPX_NO_ROUTE]		= ICMPV6_NOROUTE,
	[NFT_REJECT_ICMPX_PORT_UNREACH]		= ICMPV6_PORT_UNREACH,
	[NFT_REJECT_ICMPX_HOST_UNREACH]		= ICMPV6_ADDR_UNREACH,
	[NFT_REJECT_ICMPX_ADMIN_PROHIBITED]	= ICMPV6_ADM_PROHIBITED,
};

int nft_reject_icmpv6_code(u8 code)
{
	if (WARN_ON_ONCE(code > NFT_REJECT_ICMPX_MAX))
		return ICMPV6_NOROUTE;

	return icmp_code_v6[code];
}

EXPORT_SYMBOL_GPL(nft_reject_icmpv6_code);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Netfilter x_tables over nftables module");
