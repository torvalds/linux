// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
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
#include <net/netfilter/nf_tables_offload.h>

struct nft_bitwise {
	enum nft_registers	sreg:8;
	enum nft_registers	dreg:8;
	u8			len;
	struct nft_data		mask;
	struct nft_data		xor;
};

void nft_bitwise_eval(const struct nft_expr *expr,
		      struct nft_regs *regs, const struct nft_pktinfo *pkt)
{
	const struct nft_bitwise *priv = nft_expr_priv(expr);
	const u32 *src = &regs->data[priv->sreg];
	u32 *dst = &regs->data[priv->dreg];
	unsigned int i;

	for (i = 0; i < DIV_ROUND_UP(priv->len, 4); i++)
		dst[i] = (src[i] & priv->mask.data[i]) ^ priv->xor.data[i];
}

static const struct nla_policy nft_bitwise_policy[NFTA_BITWISE_MAX + 1] = {
	[NFTA_BITWISE_SREG]	= { .type = NLA_U32 },
	[NFTA_BITWISE_DREG]	= { .type = NLA_U32 },
	[NFTA_BITWISE_LEN]	= { .type = NLA_U32 },
	[NFTA_BITWISE_MASK]	= { .type = NLA_NESTED },
	[NFTA_BITWISE_XOR]	= { .type = NLA_NESTED },
};

static int nft_bitwise_init(const struct nft_ctx *ctx,
			    const struct nft_expr *expr,
			    const struct nlattr * const tb[])
{
	struct nft_bitwise *priv = nft_expr_priv(expr);
	struct nft_data_desc d1, d2;
	u32 len;
	int err;

	if (tb[NFTA_BITWISE_SREG] == NULL ||
	    tb[NFTA_BITWISE_DREG] == NULL ||
	    tb[NFTA_BITWISE_LEN] == NULL ||
	    tb[NFTA_BITWISE_MASK] == NULL ||
	    tb[NFTA_BITWISE_XOR] == NULL)
		return -EINVAL;

	err = nft_parse_u32_check(tb[NFTA_BITWISE_LEN], U8_MAX, &len);
	if (err < 0)
		return err;

	priv->len = len;

	priv->sreg = nft_parse_register(tb[NFTA_BITWISE_SREG]);
	err = nft_validate_register_load(priv->sreg, priv->len);
	if (err < 0)
		return err;

	priv->dreg = nft_parse_register(tb[NFTA_BITWISE_DREG]);
	err = nft_validate_register_store(ctx, priv->dreg, NULL,
					  NFT_DATA_VALUE, priv->len);
	if (err < 0)
		return err;

	err = nft_data_init(NULL, &priv->mask, sizeof(priv->mask), &d1,
			    tb[NFTA_BITWISE_MASK]);
	if (err < 0)
		return err;
	if (d1.len != priv->len) {
		err = -EINVAL;
		goto err1;
	}

	err = nft_data_init(NULL, &priv->xor, sizeof(priv->xor), &d2,
			    tb[NFTA_BITWISE_XOR]);
	if (err < 0)
		goto err1;
	if (d2.len != priv->len) {
		err = -EINVAL;
		goto err2;
	}

	return 0;
err2:
	nft_data_release(&priv->xor, d2.type);
err1:
	nft_data_release(&priv->mask, d1.type);
	return err;
}

static int nft_bitwise_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_bitwise *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_BITWISE_SREG, priv->sreg))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_BITWISE_DREG, priv->dreg))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_BITWISE_LEN, htonl(priv->len)))
		goto nla_put_failure;

	if (nft_data_dump(skb, NFTA_BITWISE_MASK, &priv->mask,
			  NFT_DATA_VALUE, priv->len) < 0)
		goto nla_put_failure;

	if (nft_data_dump(skb, NFTA_BITWISE_XOR, &priv->xor,
			  NFT_DATA_VALUE, priv->len) < 0)
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static struct nft_data zero;

static int nft_bitwise_offload(struct nft_offload_ctx *ctx,
                               struct nft_flow_rule *flow,
                               const struct nft_expr *expr)
{
	const struct nft_bitwise *priv = nft_expr_priv(expr);
	struct nft_offload_reg *reg = &ctx->regs[priv->dreg];

	if (memcmp(&priv->xor, &zero, sizeof(priv->xor)) ||
	    priv->sreg != priv->dreg || priv->len != reg->len)
		return -EOPNOTSUPP;

	memcpy(&reg->mask, &priv->mask, sizeof(priv->mask));

	return 0;
}

static const struct nft_expr_ops nft_bitwise_ops = {
	.type		= &nft_bitwise_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_bitwise)),
	.eval		= nft_bitwise_eval,
	.init		= nft_bitwise_init,
	.dump		= nft_bitwise_dump,
	.offload	= nft_bitwise_offload,
};

struct nft_expr_type nft_bitwise_type __read_mostly = {
	.name		= "bitwise",
	.ops		= &nft_bitwise_ops,
	.policy		= nft_bitwise_policy,
	.maxattr	= NFTA_BITWISE_MAX,
	.owner		= THIS_MODULE,
};
