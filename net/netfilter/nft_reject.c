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
#include <net/icmp.h>
#include <net/netfilter/ipv4/nf_reject.h>

#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
#include <net/netfilter/ipv6/nf_reject.h>
#endif

struct nft_reject {
	enum nft_reject_types	type:8;
	u8			icmp_code;
	u8			family;
};

static void nft_reject_eval(const struct nft_expr *expr,
			      struct nft_data data[NFT_REG_MAX + 1],
			      const struct nft_pktinfo *pkt)
{
	struct nft_reject *priv = nft_expr_priv(expr);
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	struct net *net = dev_net((pkt->in != NULL) ? pkt->in : pkt->out);
#endif
	switch (priv->type) {
	case NFT_REJECT_ICMP_UNREACH:
		if (priv->family == NFPROTO_IPV4)
			nf_send_unreach(pkt->skb, priv->icmp_code);
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
		else if (priv->family == NFPROTO_IPV6)
			nf_send_unreach6(net, pkt->skb, priv->icmp_code,
				      pkt->ops->hooknum);
#endif
		break;
	case NFT_REJECT_TCP_RST:
		if (priv->family == NFPROTO_IPV4)
			nf_send_reset(pkt->skb, pkt->ops->hooknum);
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
		else if (priv->family == NFPROTO_IPV6)
			nf_send_reset6(net, pkt->skb, pkt->ops->hooknum);
#endif
		break;
	}

	data[NFT_REG_VERDICT].verdict = NF_DROP;
}

static const struct nla_policy nft_reject_policy[NFTA_REJECT_MAX + 1] = {
	[NFTA_REJECT_TYPE]		= { .type = NLA_U32 },
	[NFTA_REJECT_ICMP_CODE]		= { .type = NLA_U8 },
};

static int nft_reject_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_reject *priv = nft_expr_priv(expr);

	if (tb[NFTA_REJECT_TYPE] == NULL)
		return -EINVAL;

	priv->family = ctx->afi->family;
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

static int nft_reject_dump(struct sk_buff *skb, const struct nft_expr *expr)
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

static struct nft_expr_type nft_reject_type;
static const struct nft_expr_ops nft_reject_ops = {
	.type		= &nft_reject_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_reject)),
	.eval		= nft_reject_eval,
	.init		= nft_reject_init,
	.dump		= nft_reject_dump,
};

static struct nft_expr_type nft_reject_type __read_mostly = {
	.name		= "reject",
	.ops		= &nft_reject_ops,
	.policy		= nft_reject_policy,
	.maxattr	= NFTA_REJECT_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_reject_module_init(void)
{
	return nft_register_expr(&nft_reject_type);
}

static void __exit nft_reject_module_exit(void)
{
	nft_unregister_expr(&nft_reject_type);
}

module_init(nft_reject_module_init);
module_exit(nft_reject_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_EXPR("reject");
