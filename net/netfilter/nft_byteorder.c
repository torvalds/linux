// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <asm/unaligned.h>
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
	u8			len;
	u8			size;
};

void nft_byteorder_eval(const struct nft_expr *expr,
			struct nft_regs *regs,
			const struct nft_pktinfo *pkt)
{
	const struct nft_byteorder *priv = nft_expr_priv(expr);
	u32 *src = &regs->data[priv->sreg];
	u32 *dst = &regs->data[priv->dreg];
	u16 *s16, *d16;
	unsigned int i;

	s16 = (void *)src;
	d16 = (void *)dst;

	switch (priv->size) {
	case 8: {
		u64 *dst64 = (void *)dst;
		u64 src64;

		switch (priv->op) {
		case NFT_BYTEORDER_NTOH:
			for (i = 0; i < priv->len / 8; i++) {
				src64 = nft_reg_load64(&src[i]);
				nft_reg_store64(&dst64[i],
						be64_to_cpu((__force __be64)src64));
			}
			break;
		case NFT_BYTEORDER_HTON:
			for (i = 0; i < priv->len / 8; i++) {
				src64 = (__force __u64)
					cpu_to_be64(nft_reg_load64(&src[i]));
				nft_reg_store64(&dst64[i], src64);
			}
			break;
		}
		break;
	}
	case 4:
		switch (priv->op) {
		case NFT_BYTEORDER_NTOH:
			for (i = 0; i < priv->len / 4; i++)
				dst[i] = ntohl((__force __be32)src[i]);
			break;
		case NFT_BYTEORDER_HTON:
			for (i = 0; i < priv->len / 4; i++)
				dst[i] = (__force __u32)htonl(src[i]);
			break;
		}
		break;
	case 2:
		switch (priv->op) {
		case NFT_BYTEORDER_NTOH:
			for (i = 0; i < priv->len / 2; i++)
				d16[i] = ntohs((__force __be16)s16[i]);
			break;
		case NFT_BYTEORDER_HTON:
			for (i = 0; i < priv->len / 2; i++)
				d16[i] = (__force __u16)htons(s16[i]);
			break;
		}
		break;
	}
}

static const struct nla_policy nft_byteorder_policy[NFTA_BYTEORDER_MAX + 1] = {
	[NFTA_BYTEORDER_SREG]	= { .type = NLA_U32 },
	[NFTA_BYTEORDER_DREG]	= { .type = NLA_U32 },
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

	priv->len = len;

	err = nft_parse_register_load(tb[NFTA_BYTEORDER_SREG], &priv->sreg,
				      priv->len);
	if (err < 0)
		return err;

	return nft_parse_register_store(ctx, tb[NFTA_BYTEORDER_DREG],
					&priv->dreg, NULL, NFT_DATA_VALUE,
					priv->len);
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
	if (nla_put_be32(skb, NFTA_BYTEORDER_LEN, htonl(priv->len)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_BYTEORDER_SIZE, htonl(priv->size)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static bool nft_byteorder_reduce(struct nft_regs_track *track,
				 const struct nft_expr *expr)
{
	struct nft_byteorder *priv = nft_expr_priv(expr);

	nft_reg_track_cancel(track, priv->dreg, priv->len);

	return false;
}

static const struct nft_expr_ops nft_byteorder_ops = {
	.type		= &nft_byteorder_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_byteorder)),
	.eval		= nft_byteorder_eval,
	.init		= nft_byteorder_init,
	.dump		= nft_byteorder_dump,
	.reduce		= nft_byteorder_reduce,
};

struct nft_expr_type nft_byteorder_type __read_mostly = {
	.name		= "byteorder",
	.ops		= &nft_byteorder_ops,
	.policy		= nft_byteorder_policy,
	.maxattr	= NFTA_BYTEORDER_MAX,
	.owner		= THIS_MODULE,
};
