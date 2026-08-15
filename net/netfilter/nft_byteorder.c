// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/unaligned.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>

struct nft_byteorder {
	u8			sreg;
	u8			dreg;
	enum nft_byteorder_ops	op:8;
	u8			size;
};

void nft_byteorder_eval(const struct nft_expr *expr,
			struct nft_regs *regs,
			const struct nft_pktinfo *pkt)
{
	const struct nft_byteorder *priv = nft_expr_priv(expr);
	const u32 *src = &regs->data[priv->sreg];
	u32 *dst = &regs->data[priv->dreg];

	switch (priv->size) {
	case 8: {
		u64 *dst64 = (void *)dst;
		u64 src64;

		switch (priv->op) {
		case NFT_BYTEORDER_NTOH:
			src64 = nft_reg_load64(src);

			nft_reg_store64(dst64, be64_to_cpu((__force __be64)src64));
			break;
		case NFT_BYTEORDER_HTON:
			src64 = (__force __u64)cpu_to_be64(nft_reg_load64(src));

			nft_reg_store64(dst64, src64);
			break;
		}
		break;
	}
	case 4:
		switch (priv->op) {
		case NFT_BYTEORDER_NTOH:
			*dst = ntohl((__force __be32)*src);
			break;
		case NFT_BYTEORDER_HTON:
			*dst = (__force __u32)htonl(*src);
			break;
		}
		break;
	case 2:
		switch (priv->op) {
		case NFT_BYTEORDER_NTOH:
			nft_reg_store16(dst, ntohs(nft_reg_load_be16(src)));
			break;
		case NFT_BYTEORDER_HTON:
			nft_reg_store_be16(dst, htons(nft_reg_load16(src)));
			break;
		}
		break;
	}
}

static const struct nla_policy nft_byteorder_policy[NFTA_BYTEORDER_MAX + 1] = {
	[NFTA_BYTEORDER_SREG]	= NLA_POLICY_MAX(NLA_BE32, NFT_REG32_MAX),
	[NFTA_BYTEORDER_DREG]	= NLA_POLICY_MAX(NLA_BE32, NFT_REG32_MAX),
	[NFTA_BYTEORDER_OP]	= NLA_POLICY_MAX(NLA_BE32, 255),
	[NFTA_BYTEORDER_LEN]	= NLA_POLICY_MAX(NLA_BE32, 255),
	[NFTA_BYTEORDER_SIZE]	= NLA_POLICY_MAX(NLA_BE32, 255),
};

static int nft_byteorder_init(const struct nft_ctx *ctx,
			      const struct nft_expr *expr,
			      const struct nlattr * const tb[])
{
	struct nft_byteorder *priv = nft_expr_priv(expr);
	u32 size, len;
	int err;

	if (tb[NFTA_BYTEORDER_SREG] == NULL ||
	    tb[NFTA_BYTEORDER_DREG] == NULL ||
	    tb[NFTA_BYTEORDER_LEN] == NULL ||
	    tb[NFTA_BYTEORDER_SIZE] == NULL ||
	    tb[NFTA_BYTEORDER_OP] == NULL)
		return -EINVAL;

	priv->op = ntohl(nla_get_be32(tb[NFTA_BYTEORDER_OP]));
	switch (priv->op) {
	case NFT_BYTEORDER_NTOH:
	case NFT_BYTEORDER_HTON:
		break;
	default:
		return -EINVAL;
	}

	err = nft_parse_u32_check(tb[NFTA_BYTEORDER_SIZE], U8_MAX, &size);
	if (err < 0)
		return err;

	priv->size = size;

	switch (priv->size) {
	case 2:
	case 4:
	case 8:
		break;
	default:
		return -EINVAL;
	}

	err = nft_parse_u32_check(tb[NFTA_BYTEORDER_LEN], U8_MAX, &len);
	if (err < 0)
		return err;

	/* no longer support multi-reg conversions */
	if (len != size)
		return -EOPNOTSUPP;

	err = nft_parse_register_load(ctx, tb[NFTA_BYTEORDER_SREG], &priv->sreg,
				      len);
	if (err < 0)
		return err;

	err = nft_parse_register_store(ctx, tb[NFTA_BYTEORDER_DREG],
				       &priv->dreg, NULL, NFT_DATA_VALUE,
				       len);
	if (err < 0)
		return err;

	if (nft_reg_overlap(priv->sreg, priv->dreg, len))
		return -EINVAL;

	return 0;
}

static int nft_byteorder_dump(struct sk_buff *skb,
			      const struct nft_expr *expr, bool reset)
{
	const struct nft_byteorder *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_BYTEORDER_SREG, priv->sreg))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_BYTEORDER_DREG, priv->dreg))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_BYTEORDER_OP, htonl(priv->op)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_BYTEORDER_SIZE, htonl(priv->size)))
		goto nla_put_failure;
	/* compatibility for old userspace which permitted size != len */
	if (nla_put_be32(skb, NFTA_BYTEORDER_LEN, htonl(priv->size)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static const struct nft_expr_ops nft_byteorder_ops = {
	.type		= &nft_byteorder_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_byteorder)),
	.eval		= nft_byteorder_eval,
	.init		= nft_byteorder_init,
	.dump		= nft_byteorder_dump,
};

struct nft_expr_type nft_byteorder_type __read_mostly = {
	.name		= "byteorder",
	.ops		= &nft_byteorder_ops,
	.policy		= nft_byteorder_policy,
	.maxattr	= NFTA_BYTEORDER_MAX,
	.owner		= THIS_MODULE,
};
