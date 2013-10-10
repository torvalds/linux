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
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>

static void nft_payload_eval(const struct nft_expr *expr,
			     struct nft_data data[NFT_REG_MAX + 1],
			     const struct nft_pktinfo *pkt)
{
	const struct nft_payload *priv = nft_expr_priv(expr);
	const struct sk_buff *skb = pkt->skb;
	struct nft_data *dest = &data[priv->dreg];
	int offset;

	switch (priv->base) {
	case NFT_PAYLOAD_LL_HEADER:
		if (!skb_mac_header_was_set(skb))
			goto err;
		offset = skb_mac_header(skb) - skb->data;
		break;
	case NFT_PAYLOAD_NETWORK_HEADER:
		offset = skb_network_offset(skb);
		break;
	case NFT_PAYLOAD_TRANSPORT_HEADER:
		offset = skb_transport_offset(skb);
		break;
	default:
		BUG();
	}
	offset += priv->offset;

	if (skb_copy_bits(skb, offset, dest->data, priv->len) < 0)
		goto err;
	return;
err:
	data[NFT_REG_VERDICT].verdict = NFT_BREAK;
}

static const struct nla_policy nft_payload_policy[NFTA_PAYLOAD_MAX + 1] = {
	[NFTA_PAYLOAD_DREG]	= { .type = NLA_U32 },
	[NFTA_PAYLOAD_BASE]	= { .type = NLA_U32 },
	[NFTA_PAYLOAD_OFFSET]	= { .type = NLA_U32 },
	[NFTA_PAYLOAD_LEN]	= { .type = NLA_U32 },
};

static int nft_payload_init(const struct nft_ctx *ctx,
			    const struct nft_expr *expr,
			    const struct nlattr * const tb[])
{
	struct nft_payload *priv = nft_expr_priv(expr);
	int err;

	priv->base   = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_BASE]));
	priv->offset = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_OFFSET]));
	priv->len    = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_LEN]));

	priv->dreg = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_DREG]));
	err = nft_validate_output_register(priv->dreg);
	if (err < 0)
		return err;
	return nft_validate_data_load(ctx, priv->dreg, NULL, NFT_DATA_VALUE);
}

static int nft_payload_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_payload *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_PAYLOAD_DREG, htonl(priv->dreg)) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_BASE, htonl(priv->base)) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_OFFSET, htonl(priv->offset)) ||
	    nla_put_be32(skb, NFTA_PAYLOAD_LEN, htonl(priv->len)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_payload_type;
static const struct nft_expr_ops nft_payload_ops = {
	.type		= &nft_payload_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_payload)),
	.eval		= nft_payload_eval,
	.init		= nft_payload_init,
	.dump		= nft_payload_dump,
};

const struct nft_expr_ops nft_payload_fast_ops = {
	.type		= &nft_payload_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_payload)),
	.eval		= nft_payload_eval,
	.init		= nft_payload_init,
	.dump		= nft_payload_dump,
};

static const struct nft_expr_ops *nft_payload_select_ops(const struct nlattr * const tb[])
{
	enum nft_payload_bases base;
	unsigned int offset, len;

	if (tb[NFTA_PAYLOAD_DREG] == NULL ||
	    tb[NFTA_PAYLOAD_BASE] == NULL ||
	    tb[NFTA_PAYLOAD_OFFSET] == NULL ||
	    tb[NFTA_PAYLOAD_LEN] == NULL)
		return ERR_PTR(-EINVAL);

	base = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_BASE]));
	switch (base) {
	case NFT_PAYLOAD_LL_HEADER:
	case NFT_PAYLOAD_NETWORK_HEADER:
	case NFT_PAYLOAD_TRANSPORT_HEADER:
		break;
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}

	offset = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_OFFSET]));
	len = ntohl(nla_get_be32(tb[NFTA_PAYLOAD_LEN]));
	if (len == 0 || len > FIELD_SIZEOF(struct nft_data, data))
		return ERR_PTR(-EINVAL);

	if (len <= 4 && IS_ALIGNED(offset, len) && base != NFT_PAYLOAD_LL_HEADER)
		return &nft_payload_fast_ops;
	else
		return &nft_payload_ops;
}

static struct nft_expr_type nft_payload_type __read_mostly = {
	.name		= "payload",
	.select_ops	= nft_payload_select_ops,
	.policy		= nft_payload_policy,
	.maxattr	= NFTA_PAYLOAD_MAX,
	.owner		= THIS_MODULE,
};

int __init nft_payload_module_init(void)
{
	return nft_register_expr(&nft_payload_type);
}

void nft_payload_module_exit(void)
{
	nft_unregister_expr(&nft_payload_type);
}
